/* resolver.c -- from a video id to something libmpv can open.
 *
 * The configured backend's own URLs first (no process, no cipher), then the other backend. Both are plain HTTP;
 * nothing is spawned on the search or resolve path.
 */
#include "net/backend.h"
#include "net/http.h"
#include "net/resolver.h"
#include "util.h"

#include <sys/socket.h>
#include <string.h>

#define FORMAT_MAX 32      /* 32 formats is a whole video's worth; URL_LEN is 2 KB each */

const char *resolve_status_text(ResolveStatus s)
{
    switch (s) {
    case RESOLVE_OK:                  return "ready";
    case RESOLVE_NO_NETWORK:          return "No Wi-Fi, or the site could not be reached.";
    case RESOLVE_INSTANCE_FAILED:     return "The video source did not answer. Try again, or set another instance in bananatube.conf.";
    case RESOLVE_NO_SUITABLE_FORMAT:  return "This video has no format the handheld can decode.";
    case RESOLVE_NEEDS_CIPHER:        return "The video's stream is scrambled. Set instance to a working Invidious server.";
    case RESOLVE_ALL_FAILED:
    default:                          return "Could not play this video.";
    }
}

/* ---- the format rules ---------------------------------------------------------------------------------- */

bool codec_hardware_decodable(const char *vcodec)
{
    if (!vcodec || !*vcodec) return true;        /* some instances omit it: allow, ranked last */
    /* AV1 has no decoder in the RK3568's video unit: software AV1 at 480p uses every A55 core. */
    if (str_starts(vcodec, "av01") || str_starts(vcodec, "av1")) return false;
    if (str_starts(vcodec, "avc1") || str_starts(vcodec, "avc3")) return true;   /* H.264 */
    if (str_starts(vcodec, "vp09") || str_starts(vcodec, "vp9"))  return true;
    if (str_starts(vcodec, "vp08") || str_starts(vcodec, "vp8"))  return true;
    if (str_starts(vcodec, "hev1") || str_starts(vcodec, "hvc1")) return true;   /* H.265, rarely offered */
    if (str_starts(vcodec, "mp4v"))                              return true;
    return false;
}

/* Lower is better: H.264 first (the VPU's cheapest path and the one with the widest support), then VP9, then
 * anything else the unit can decode (an unknown or missing codec string lands here). */
static int codec_rank(const char *vcodec)
{
    if (str_starts(vcodec, "avc1") || str_starts(vcodec, "avc3")) return 0;
    if (str_starts(vcodec, "vp09") || str_starts(vcodec, "vp9"))  return 1;
    return 2;
}

int select_video_format(const Format *f, int n, int max_height, int max_fps)
{
    int best = -1, best_rank = 99, best_height = -1, best_bitrate = 0;

    for (int i = 0; i < n; i++) {
        const Format *v = &f[i];
        if (!v->video || !v->has_url || !v->url[0]) continue;
        if (!codec_hardware_decodable(v->vcodec)) continue;
        if (v->fps > 0 && max_fps > 0 && v->fps > max_fps) continue;
        if (max_height > 0 && v->height > max_height) continue;
        if (v->height <= 0) continue;                     /* no size: cannot tell what it is */

        int rank = codec_rank(v->vcodec);
        bool better = false;
        if (best < 0) better = true;
        else if (rank < best_rank) better = true;                        /* a better codec wins outright */
        else if (rank == best_rank && v->height > best_height) better = true;   /* then the bigger picture */
        else if (rank == best_rank && v->height == best_height && v->bitrate_kbps < best_bitrate) better = true;

        if (better) {
            best = i; best_rank = rank; best_height = v->height; best_bitrate = v->bitrate_kbps;
        }
    }
    return best;
}

int select_audio_format(const Format *f, int n, bool allow_webm)
{
    int best = -1, best_kbps = -1;
    for (int i = 0; i < n; i++) {
        const Format *a = &f[i];
        if (!a->audio || a->video || !a->has_url || !a->url[0]) continue;    /* audio-only entries */
        if (!allow_webm && str_starts(a->mime, "audio/webm")) continue;
        if (!a->original) continue;                 /* a dub: same bitrate, another language */
        if (a->audio_kbps > best_kbps) { best = i; best_kbps = a->audio_kbps; }
    }
    if (best >= 0) return best;

    /* No audio-only entry: fall back to anything with sound in it. */
    for (int i = 0; i < n; i++)
        if (f[i].audio && f[i].has_url && f[i].url[0]) return i;
    return -1;
}

/* ---- the backends, in order ---------------------------------------------------------------------------- */

static int backend_order(const Conf *c, const Backend **out)
{
    switch (c->backend) {
    case BACKEND_INVIDIOUS: out[0] = &backend_invidious; return 1;
    case BACKEND_INNERTUBE: out[0] = &backend_innertube; return 1;
    case BACKEND_AUTO:
    default:
        out[0] = &backend_invidious;
        out[1] = &backend_innertube;
        return 2;
    }
}

/* ---- the entry point ----------------------------------------------------------------------------------- */

/* Asks the backends for the streams. Called twice at most, from resolver_resolve() below. */
static ResolveStatus resolve_once(const Conf *c, const char *video_id, StreamUrl *out)
{
    memset(out, 0, sizeof *out);

    const Backend *order[2];
    int nb = backend_order(c, order);
    ResolveStatus worst = RESOLVE_ALL_FAILED;

    for (int b = 0; b < nb; b++) {
        Format fmts[FORMAT_MAX];
        ResolveStatus st = order[b]->streams(c, video_id, fmts, FORMAT_MAX);
        if (st != RESOLVE_OK) {
            log_line("resolver: %s gave %s", order[b]->name, resolve_status_text(st));
            if (st == RESOLVE_NO_SUITABLE_FORMAT || st == RESOLVE_NEEDS_CIPHER) worst = st;
            continue;
        }

        int vi = select_video_format(fmts, FORMAT_MAX, (int)c->quality, FORMAT_MAX_FPS);
        if (vi < 0) { worst = RESOLVE_NO_SUITABLE_FORMAT; continue; }

        str_copy(out->url, sizeof out->url, fmts[vi].url);
        out->width = fmts[vi].width;
        out->height = fmts[vi].height;
        out->fps = fmts[vi].fps;
        str_copy(out->vcodec, sizeof out->vcodec, fmts[vi].vcodec);

        /* a "formatStreams"/"formats" entry already carries its sound; an adaptive video-only one needs the
         * audio stream beside it (libmpv plays both, sync'ing on the timestamps) */
        bool muxed = fmts[vi].acodec[0] != '\0';
        if (!muxed) {
            int ai = select_audio_format(fmts, FORMAT_MAX, true);
            if (ai >= 0) {
                str_copy(out->audio_url, sizeof out->audio_url, fmts[ai].url);
                out->audio_kbps = fmts[ai].audio_kbps;
            }
        }
        out->muxed = muxed;
        log_line("resolver: %s %dx%d@%d %s%s (%d kbps audio)", order[b]->name,
                 out->width, out->height, out->fps, out->vcodec,
                 out->muxed ? " muxed" : " + separate audio", out->audio_kbps);
        return RESOLVE_OK;
    }

    return worst;
}

/* Does the CDN actually serve this URL? A stream URL is bound to the address that asked the API for it -- the
 * ip= parameter is inside its signature -- so a URL can be perfectly well formed and still be answered 403,
 * which is exactly what "the video resolved and would not load" was: a complete 1118-character URL, and
 * ffmpeg getting 403 Forbidden. Asking for one byte from the same program that asked for the URL is the only
 * way to tell, and it costs a few milliseconds. */
static bool stream_fetches(const Conf *c, const char *url)
{
    HttpResponse r;
    if (!http_get_range(url, c->search_timeout_ms, &r)) {
        log_line("resolver: the media URL did not fetch at all: %s", http_error(&r));
        http_response_free(&r);
        return false;
    }
    bool ok = (r.status == 200 || r.status == 206);
    log_line("resolver: the media URL fetches %s (HTTP %ld from %s to %s)", ok ? "fine" : "badly",
             r.status, r.local_ip[0] ? r.local_ip : "?", r.peer_ip[0] ? r.peer_ip : "?");
    http_response_free(&r);
    return ok;
}

ResolveStatus resolver_resolve(const Conf *c, const char *video_id, StreamUrl *out)
{
    /* A stream URL is bound to the address that asked the API for it -- the ip= parameter is inside its
     * signature -- and libmpv resolves the host again for its own fetch. Ask and validate over IPv4 first (the
     * family a handheld's media fetch usually ends up using, and the one that does not rotate), so the URL
     * handed to the player is bound to the address it will use. A URL validated over the default family came
     * back "HTTP 403 Forbidden" from the CDN when the player fetched it over IPv4: the signature's ip= was an
     * IPv6 address the player never used. */
    StreamUrl s;
    http_set_ip_family(AF_INET);
    ResolveStatus st = resolve_once(c, video_id, &s);
    if (st == RESOLVE_OK && stream_fetches(c, s.url)) {
        *out = s;
        http_set_ip_family(AF_UNSPEC);
        return RESOLVE_OK;
    }

    /* An IPv6-only network, or a CDN that refused that address: ask again over whatever the resolver picks. */
    log_line("resolver: asking again over any address family");
    http_set_ip_family(AF_UNSPEC);
    st = resolve_once(c, video_id, &s);
    bool ok = (st == RESOLVE_OK) && stream_fetches(c, s.url);
    http_set_ip_family(AF_UNSPEC);
    if (ok) { *out = s; return RESOLVE_OK; }
    return st;
}
