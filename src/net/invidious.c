/* invidious.c -- an Invidious (or Piped) instance: one GET for a search, one for a video.
 *
 * Nothing here spawns a process or solves a signature: an instance hands over plain URLs, which is the whole
 * reason it is the first choice. The trade is that public instances get blocked or rate-limited from time to
 * time, which is what conf.backend = auto is for: net/resolver.c falls through to InnerTube.
 */
#include "net/invidious.h"
#include "net/http.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Tried in order when bananatube.conf names no instance. Kept short, so a slow one cannot hold a search for
 * the whole timeout. */
static const char *const INSTANCES[] = {
    "https://inv.nadeko.net",
    "https://invidious.nerdvpn.de",
    "https://yewtu.be",
    "https://invidious.f5.si",
};
#define N_INSTANCES ((int)(sizeof INSTANCES / sizeof INSTANCES[0]))

static int search_piped(const Conf *c, const char *query, SearchResult *out, int max);

const char *invidious_instance(const Conf *c, int which)
{
    if (c && c->instance[0]) return which == 0 ? c->instance : NULL;
    return which >= 0 && which < N_INSTANCES ? INSTANCES[which] : NULL;
}

/* "video/mp4; codecs=\"avc1.640028, mp4a.40.2\"" -> mime plus the codec strings. Instances are not
 * consistent about this field (some send vcodec/acodec beside it), so both are read. */
static void split_type(const char *type, Format *f)
{
    if (!type) return;
    const char *semi = strchr(type, ';');
    size_t n = semi ? (size_t)(semi - type) : strlen(type);
    if (n >= sizeof f->mime) n = sizeof f->mime - 1;
    memcpy(f->mime, type, n);
    f->mime[n] = '\0';

    const char *q = strstr(type, "codecs=\"");
    if (!q) return;
    q += 8;
    const char *end = strchr(q, '"');
    size_t len = end ? (size_t)(end - q) : strlen(q);
    char codecs[64];
    if (len >= sizeof codecs) len = sizeof codecs - 1;
    memcpy(codecs, q, len);
    codecs[len] = '\0';

    char *comma = strchr(codecs, ',');
    if (comma) *comma = '\0';
    if (str_starts(f->mime, "audio/")) str_copy(f->acodec, sizeof f->acodec, codecs);
    else                               str_copy(f->vcodec, sizeof f->vcodec, codecs);
    if (comma) str_copy(f->acodec, sizeof f->acodec, comma + 1);
}

/* Invidious /api/v1/videos and /api/v1/search use this shape for one format. */
static Format format_from_invidious(const cJSON *o)
{
    Format f;
    memset(&f, 0, sizeof f);

    char type[64] = "", url[URL_LEN] = "", vcodec[24] = "", acodec[24] = "", q[24] = "";
    json_str(o, "type", type, sizeof type);
    split_type(type, &f);
    json_str(o, "vcodec", vcodec, sizeof vcodec);
    json_str(o, "acodec", acodec, sizeof acodec);
    if (vcodec[0]) str_copy(f.vcodec, sizeof f.vcodec, vcodec);
    if (acodec[0]) str_copy(f.acodec, sizeof f.acodec, acodec);

    json_str(o, "url", url, sizeof url);
    f.has_url = url[0] != '\0';          /* a signatureCipher instead: left false, never solved here */
    str_copy(f.url, sizeof f.url, url);

    f.width  = json_int(o, "width", 0);
    f.height = json_int(o, "height", 0);
    f.fps    = json_int(o, "fps", 0);
    f.bitrate_kbps = json_int(o, "bitrate", 0) / 1000;

    json_str(o, "qualityLabel", q, sizeof q);       /* "720p60": fall back to it when width/height are absent */
    if (!f.height && q[0]) {
        int h = atoi(q);
        if (h > 0) f.height = h;
        if (strstr(q, "60") && !f.fps) f.fps = 60;
    }
    f.video = f.vcodec[0] != '\0' || str_starts(f.mime, "video/");
    f.audio = f.acodec[0] != '\0' || str_starts(f.mime, "audio/");
    return f;
}

/* Piped's /streams/<id>: videoStreams[] and audioStreams[]. */
static Format format_from_piped(const cJSON *o, bool audio)
{
    Format f;
    memset(&f, 0, sizeof f);

    char url[URL_LEN] = "", mime[32] = "", codec[24] = "", quality[24] = "";
    json_str(o, "url", url, sizeof url);
    json_str(o, "mimeType", mime, sizeof mime);
    json_str(o, "codec", codec, sizeof codec);
    json_str(o, "quality", quality, sizeof quality);
    str_copy(f.url, sizeof f.url, url);
    f.has_url = url[0] != '\0';

    if (str_contains(mime, "webm")) str_copy(f.mime, sizeof f.mime, audio ? "audio/webm" : "video/webm");
    else if (mime[0])               str_copy(f.mime, sizeof f.mime, audio ? "audio/mp4" : "video/mp4");

    if (audio) {
        f.audio = true;
        str_copy(f.acodec, sizeof f.acodec, codec);
        f.audio_kbps = json_int(o, "bitrate", 0) / 1000;
        return f;
    }

    f.video = true;
    str_copy(f.vcodec, sizeof f.vcodec, codec);
    f.width  = json_int(o, "width", 0);
    f.height = json_int(o, "height", 0);
    f.fps    = json_int(o, "fps", 0);
    f.bitrate_kbps = json_int(o, "bitrate", 0) / 1000;
    if (!f.height && quality[0]) { int h = atoi(quality); if (h > 0) f.height = h; }
    return f;
}

/* ---- search ------------------------------------------------------------------------------------------- */

static int search_invidious(const Conf *c, const char *query, SearchResult *out, int max)
{
    char esc[512], url[URL_LEN];
    url_encode(query, esc, sizeof esc);
    snprintf(url, sizeof url, "%s/api/v1/search?q=%s&type=video&page=1", invidious_instance(c, 0), esc);

    HttpResponse r;
    if (!http_get(url, c->search_timeout_ms, &r)) {
        log_line("invidious: search failed: %s", http_error(&r));
        http_response_free(&r);
        return 0;
    }
    int n = invidious_parse_search(r.data, out, max);
    http_response_free(&r);
    return n > 0 ? n : search_piped(c, query, out, max);
}

int invidious_parse_search(const char *json, SearchResult *out, int max)
{
    if (!json) return 0;
    cJSON *root = json_parse(json, strlen(json), "invidious search");
    if (!root) return 0;

    int n = 0;
    const cJSON *item = NULL;
    /* Invidious: a bare array. Piped: the same entries under "items", with different key names. */
    const cJSON *list = cJSON_IsArray(root) ? root : json_at(root, "items");
    cJSON_ArrayForEach(item, list) {
        if (n >= max) break;
        if (!cJSON_IsObject(item)) continue;
        SearchResult *s = &out[n];
        memset(s, 0, sizeof *s);
        json_str(item, "videoId", s->id, sizeof s->id);
        json_str(item, "title", s->title, sizeof s->title);
        json_str(item, "author", s->channel, sizeof s->channel);
        if (!s->id[0]) {                              /* Piped: url is "/watch?v=ID" */
            char u[128] = "";
            json_str(item, "url", u, sizeof u);
            const char *eq = strstr(u, "v=");
            if (eq) str_copy(s->id, sizeof s->id, eq + 2);
        }
        if (!s->channel[0]) json_str(item, "uploaderName", s->channel, sizeof s->channel);
        s->duration_sec = json_int(item, "lengthSeconds", json_int(item, "duration", -1));
        s->views = json_ll(item, "viewCount", json_ll(item, "views", -1));
        s->live = json_bool(item, "liveNow", false);
        if (s->id[0]) n++;
    }
    cJSON_Delete(root);
    return n;
}

static int search_piped(const Conf *c, const char *query, SearchResult *out, int max)
{
    char esc[512], url[URL_LEN];
    url_encode(query, esc, sizeof esc);
    snprintf(url, sizeof url, "%s/search?q=%s&filter=videos", invidious_instance(c, 0), esc);
    HttpResponse r;
    if (!http_get(url, c->search_timeout_ms, &r)) { http_response_free(&r); return 0; }
    cJSON *root = json_parse(r.data, r.len, "piped search");
    http_response_free(&r);
    if (!root) return 0;

    int n = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, json_at(root, "items")) {
        if (n >= max) break;
        SearchResult *s = &out[n];
        memset(s, 0, sizeof *s);
        char u[128] = "";
        json_str(item, "url", u, sizeof u);
        const char *id = strstr(u, "v=");
        if (id) str_copy(s->id, sizeof s->id, id + 2);
        json_str(item, "title", s->title, sizeof s->title);
        json_str(item, "uploaderName", s->channel, sizeof s->channel);
        s->duration_sec = json_int(item, "duration", -1);
        s->views = json_ll(item, "views", -1);
        if (s->id[0]) n++;
    }
    cJSON_Delete(root);
    return n;
}

static int invidious_search(const Conf *c, const char *query, SearchResult *out, int max)
{
    return search_invidious(c, query, out, max);
}

/* ---- one video ---------------------------------------------------------------------------------------- */

static int invidious_detail(const Conf *c, const char *id, VideoDetail *out)
{
    char url[URL_LEN];
    snprintf(url, sizeof url, "%s/api/v1/videos/%s", invidious_instance(c, 0), id);
    HttpResponse r;
    if (!http_get(url, c->search_timeout_ms, &r)) {
        log_line("invidious: video failed: %s", http_error(&r));
        http_response_free(&r);
        return -1;
    }
    int ok = invidious_parse_video(r.data ? r.data : "", id, out);
    http_response_free(&r);
    return ok;
}

int invidious_parse_video(const char *json, const char *id, VideoDetail *out)
{
    cJSON *root = json_parse(json, strlen(json), "invidious video");
    if (!root) return -1;
    memset(out, 0, sizeof *out);
    str_copy(out->id, sizeof out->id, id);
    json_str(root, "title", out->title, sizeof out->title);
    json_str(root, "author", out->channel, sizeof out->channel);
    json_str(root, "description", out->description, sizeof out->description);
    out->duration_sec = json_int(root, "lengthSeconds", -1);
    out->views = json_ll(root, "viewCount", -1);
    out->likes = json_ll(root, "likeCount", -1);
    json_str(root, "publishedText", out->published, sizeof out->published);
    cJSON_Delete(root);
    return 0;
}

/* ---- the streams -------------------------------------------------------------------------------------- */

static int collect_invidious(const cJSON *arr, Format *out, int max, int n)
{
    const cJSON *o = NULL;
    cJSON_ArrayForEach(o, arr) {
        if (n >= max) break;
        if (cJSON_IsObject(o)) out[n++] = format_from_invidious(o);
    }
    return n;
}

static int collect_piped(const cJSON *arr, Format *out, int max, int n, bool audio)
{
    const cJSON *o = NULL;
    cJSON_ArrayForEach(o, arr) {
        if (n >= max) break;
        if (cJSON_IsObject(o)) out[n++] = format_from_piped(o, audio);
    }
    return n;
}

static ResolveStatus invidious_streams(const Conf *c, const char *id, Format *out, int max)
{
    char url[URL_LEN];
    snprintf(url, sizeof url, "%s/api/v1/videos/%s", invidious_instance(c, 0), id);
    HttpResponse r;
    if (!http_get(url, c->search_timeout_ms, &r)) { http_response_free(&r); return RESOLVE_NO_NETWORK; }
    ResolveStatus st;
    invidious_parse_formats(r.data ? r.data : "", out, max, &st);
    http_response_free(&r);
    return st;
}

int invidious_parse_formats(const char *json, Format *out, int max, ResolveStatus *status)
{
    if (status) *status = RESOLVE_ALL_FAILED;
    cJSON *root = json_parse(json, strlen(json), "invidious video");
    if (!root) { if (status) *status = RESOLVE_INSTANCE_FAILED; return 0; }

    int n = 0;
    const cJSON *adaptive = json_at(root, "adaptiveFormats");
    if (adaptive) n = collect_invidious(adaptive, out, max, n);
    const cJSON *muxed = json_at(root, "formatStreams");     /* video+audio together: no second stream to sync */
    if (muxed) n = collect_invidious(muxed, out, max, n);

    if (n == 0) {                                            /* Piped's shape, same base URL */
        const cJSON *vs = json_at(root, "videoStreams");
        const cJSON *as = json_at(root, "audioStreams");
        if (vs) n = collect_piped(vs, out, max, n, false);
        if (as) n = collect_piped(as, out, max, n, true);
    }

    cJSON_Delete(root);
    if (n == 0) { if (status) *status = RESOLVE_INSTANCE_FAILED; return 0; }
    if (status) {
        *status = RESOLVE_NEEDS_CIPHER;                      /* until one turns out to have a URL */
        for (int i = 0; i < n; i++) if (out[i].has_url) { *status = RESOLVE_OK; break; }
    }
    return n;
}

const Backend backend_invidious = {
    .name = "invidious",
    .search = invidious_search,
    .detail = invidious_detail,
    .streams = invidious_streams,
};
