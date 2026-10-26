/* resolver.c -- from a video id to something libmpv can open.
 *
 * The order is the plan's: the configured backend's own URLs first (no process, no cipher), then the other
 * backend, and only if both fail a single, tightly scoped yt-dlp run. yt-dlp is never used to search: it
 * would spawn a Python interpreter on every keystroke.
 */
#include "net/backend.h"
#include "net/http.h"
#include "net/resolver.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
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

/* ---- the last resort: one yt-dlp run, at the moment a video is picked ---------------------------------- */

static bool ytdlp_available(void)
{
    return system("command -v yt-dlp >/dev/null 2>&1") == 0;
}

/* Runs yt-dlp -g once and takes the first URL it prints. The process exits (and its ~60 MB with it) before
 * libmpv is asked to open the URL. */
static bool ytdlp_resolve(const Conf *c, const char *video_id, char *out, size_t cap)
{
    char cmd[512];
    snprintf(cmd, sizeof cmd,
        "yt-dlp -g --no-cache-dir --no-playlist --no-warnings "
        "--cookies /data/banana/yt-cookies.txt "
        "-f 'best[height<=%d][vcodec^=avc1][ext=mp4]/best[height<=%d][vcodec^=avc1]/best[height<=%d]' "
        "'https://www.youtube.com/watch?v=%s' 2>/dev/null",
        (int)c->quality, (int)c->quality, (int)c->quality, video_id);

    log_line("resolver: falling back to yt-dlp (%d)", (int)c->quality);
    FILE *p = popen(cmd, "r");
    if (!p) return false;

    out[0] = '\0';
    if (fgets(out, (int)cap, p)) {
        size_t n = strlen(out);
        while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r')) out[--n] = '\0';
    }
    int st = pclose(p);
    return out[0] && st == 0;
}

/* ---- the entry point ----------------------------------------------------------------------------------- */

/* Asks the backends (and yt-dlp as a last resort) for the streams. Called twice at most, from
 * resolver_resolve() below. */
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
        out->muxed = muxed || out->audio_url[0] != '\0';
        log_line("resolver: %s %dx%d@%d %s%s (%d kbps audio)", order[b]->name,
                 out->width, out->height, out->fps, out->vcodec,
                 out->muxed ? " muxed" : " + separate audio", out->audio_kbps);
        return RESOLVE_OK;
    }

    char url[URL_LEN];
    if (ytdlp_available() && ytdlp_resolve(c, video_id, url, sizeof url)) {
        str_copy(out->url, sizeof out->url, url);
        out->muxed = true;
        log_line("resolver: yt-dlp gave a stream");
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
    http_set_ip_family(AF_UNSPEC);
    ResolveStatus st = resolve_once(c, video_id, out);
    if (st != RESOLVE_OK) return st;
    if (stream_fetches(c, out->url)) return RESOLVE_OK;

    /* The URL came from an address the CDN will not serve it to. Ask again over IPv4 -- the address family a
     * handheld's media fetch usually ends up using, and the one that does not rotate -- and use that URL if it
     * fetches. If the second attempt is no better, the first is returned as it was: the player's failure will
     * say the same thing it said before, and the log above says why. */
    log_line("resolver: asking again over IPv4, for a URL bound to that address");
    http_set_ip_family(AF_INET);
    StreamUrl second;
    ResolveStatus st2 = resolve_once(c, video_id, &second);
    bool ok = (st2 == RESOLVE_OK) && stream_fetches(c, second.url);
    http_set_ip_family(AF_UNSPEC);
    if (ok) { *out = second; return RESOLVE_OK; }
    return st;
}
