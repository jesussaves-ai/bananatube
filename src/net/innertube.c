/* innertube.c -- YouTube's own /youtubei/v1 API, without a browser and without yt-dlp.
 *
 * The Android VR client is asked for, because it is the one that still answers with plain stream URLs:
 * no signatureCipher to solve, no po_token round-trip. A reply that does come back ciphered is reported as
 * RESOLVE_NEEDS_CIPHER and net/resolver.c moves on rather than shipping a JavaScript interpreter.
 *
 * Responses are walked rather than addressed by path: YouTube moves its renderers around between builds
 * (twoColumnSearchResultsRenderer one month, sectionListRenderer's contents the next), but a "videoRenderer"
 * in the tree is always a video. One recursive pass over a few hundred nodes costs nothing.
 */
#include "net/innertube.h"
#include "net/http.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INNERTUBE_SEARCH "https://www.youtube.com/youtubei/v1/search?prettyPrint=false"
#define INNERTUBE_PLAYER "https://www.youtube.com/youtubei/v1/player?prettyPrint=false"

/* The key the Android VR client sends. It is public (every client ships one); BANANATUBE_KEY overrides it
 * without a rebuild, which is what to use when YouTube rotates it. */
#define INNERTUBE_KEY_DEFAULT "AIzaSyA8eiZmM1FaDVjRy-df2KTyQ_vz_yYM39w"

static const char *innertube_key(void)
{
    const char *k = getenv("BANANATUBE_KEY");
    return (k && *k) ? k : INNERTUBE_KEY_DEFAULT;
}

/* The client block both calls send. */
static void client_context(char *out, size_t cap)
{
    snprintf(out, cap,
        "\"context\":{\"client\":{"
        "\"clientName\":\"ANDROID_VR\","
        "\"clientVersion\":\"1.64.0\","
        "\"deviceMake\":\"Oculus\","
        "\"deviceModel\":\"Quest 3\","
        "\"osName\":\"Android\","
        "\"osVersion\":\"12\","
        "\"androidSdkVersion\":32,"
        "\"hl\":\"en\",\"gl\":\"US\","
        "\"userAgent\":\"com.google.android.apps.youtube.vr.oculus/1.64.0 (Linux; U; Android 12; GB) gzip\""
        "}}");
}

/* A YouTube text field: a plain string (videoDetails.title), {simpleText}, or {runs:[{text}]} -- the
 * player reply uses the first for a video's own fields and the last two for search results. */
static const char *yt_text(const cJSON *o, char *out, size_t cap)
{
    if (out && cap) out[0] = '\0';
    if (!o || !out || cap == 0) return "";
    if (cJSON_IsString(o) && o->valuestring) { str_copy(out, cap, o->valuestring); return out; }

    const cJSON *simple = cJSON_GetObjectItemCaseSensitive(o, "simpleText");
    if (cJSON_IsString(simple) && simple->valuestring) { str_copy(out, cap, simple->valuestring); return out; }

    const cJSON *runs = cJSON_GetObjectItemCaseSensitive(o, "runs");
    if (cJSON_IsArray(runs)) {
        size_t w = 0;
        const cJSON *r = NULL;
        cJSON_ArrayForEach(r, runs) {
            const cJSON *t = cJSON_GetObjectItemCaseSensitive(r, "text");
            if (!cJSON_IsString(t) || !t->valuestring) continue;
            w += str_copy(out + w, cap - w, t->valuestring);
            if (w + 1 >= cap) break;
        }
    }
    return out;
}

/* "1,234,567 views" -> 1234567; -1 when there is no number. */
static long long parse_count(const char *s)
{
    if (!s) return -1;
    long long v = 0;
    bool any = false;
    for (; *s; s++) {
        if (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); any = true; }
        else if (any && (*s == ',' || *s == '.' || *s == ' ')) continue;
        else if (any) break;
    }
    return any ? v : -1;
}

/* "12:34" or "1:02:03" -> seconds, or -1. */
static int parse_length(const char *s)
{
    if (!s || !*s) return -1;
    int parts[3] = {0, 0, 0}, n = 0;
    const char *p = s;
    while (*p && n < 3) {
        if (*p < '0' || *p > '9') return n ? parts[n > 2 ? 2 : n - 1] : -1;   /* "LIVE" and the like */
        int v = 0;
        while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
        parts[n++] = v;
        if (*p == ':') p++;
    }
    if (n == 3) return parts[0] * 3600 + parts[1] * 60 + parts[2];
    if (n == 2) return parts[0] * 60 + parts[1];
    return n == 1 ? parts[0] : -1;
}

/* ---- search ------------------------------------------------------------------------------------------- */

static void walk_video_renderers(const cJSON *node, SearchResult *out, int max, int *n)
{
    if (!node || *n >= max) return;
    if (cJSON_IsObject(node)) {
        const cJSON *vr = cJSON_GetObjectItemCaseSensitive(node, "videoRenderer");
        if (cJSON_IsObject(vr)) {
            SearchResult *s = &out[*n];
            memset(s, 0, sizeof *s);
            json_str(vr, "videoId", s->id, sizeof s->id);
            yt_text(cJSON_GetObjectItemCaseSensitive(vr, "title"), s->title, sizeof s->title);
            yt_text(cJSON_GetObjectItemCaseSensitive(vr, "ownerText"), s->channel, sizeof s->channel);
            if (!s->channel[0]) yt_text(cJSON_GetObjectItemCaseSensitive(vr, "longBylineText"), s->channel, sizeof s->channel);
            char t[32] = "";
            yt_text(cJSON_GetObjectItemCaseSensitive(vr, "lengthText"), t, sizeof t);
            s->duration_sec = parse_length(t);
            char v[48] = "";
            yt_text(cJSON_GetObjectItemCaseSensitive(vr, "viewCountText"), v, sizeof v);
            s->views = parse_count(v);
            s->live = json_bool(vr, "isLiveNow", false);
            if (s->id[0]) (*n)++;
            if (*n >= max) return;
        }
    }
    const cJSON *child = NULL;
    cJSON_ArrayForEach(child, node) walk_video_renderers(child, out, max, n);
}

static int innertube_search(const Conf *c, const char *query, SearchResult *out, int max)
{
    char ctx[640], body[1280], esc[512];
    client_context(ctx, sizeof ctx);
    url_encode(query, esc, sizeof esc);
    snprintf(body, sizeof body, "{%s,\"query\":\"%s\",\"params\":\"EgIQAQ%%3D%%3D\"}", ctx, esc);

    char url[256];
    snprintf(url, sizeof url, "%s&key=%s", INNERTUBE_SEARCH, innertube_key());

    HttpResponse r;
    if (!http_post_json(url, body, c->search_timeout_ms, &r)) {
        log_line("innertube: search failed: %s", http_error(&r));
        http_response_free(&r);
        return 0;
    }
    int n = innertube_parse_search(r.data ? r.data : "", out, max);
    http_response_free(&r);
    return n;
}

int innertube_parse_search(const char *json, SearchResult *out, int max)
{
    cJSON *root = json_parse(json, strlen(json), "innertube search");
    if (!root) return 0;
    int n = 0;
    walk_video_renderers(root, out, max, &n);
    cJSON_Delete(root);
    return n;
}

/* ---- one video ---------------------------------------------------------------------------------------- */

static int innertube_detail(const Conf *c, const char *id, VideoDetail *out)
{
    char ctx[640], body[1024];
    client_context(ctx, sizeof ctx);
    snprintf(body, sizeof body,
        "{%s,\"videoId\":\"%s\",\"contentCheckOk\":true,\"racyCheckOk\":true}", ctx, id);

    char url[256];
    snprintf(url, sizeof url, "%s&key=%s", INNERTUBE_PLAYER, innertube_key());

    HttpResponse r;
    if (!http_post_json(url, body, c->search_timeout_ms, &r)) { http_response_free(&r); return -1; }
    int ok = innertube_parse_video(r.data ? r.data : "", id, out);
    http_response_free(&r);
    return ok;
}

int innertube_parse_video(const char *json, const char *id, VideoDetail *out)
{
    cJSON *root = json_parse(json, strlen(json), "innertube player");
    if (!root) return -1;

    memset(out, 0, sizeof *out);
    str_copy(out->id, sizeof out->id, id);
    const cJSON *d = json_at(root, "videoDetails");
    yt_text(cJSON_GetObjectItemCaseSensitive(d, "title"), out->title, sizeof out->title);
    yt_text(cJSON_GetObjectItemCaseSensitive(d, "author"), out->channel, sizeof out->channel);
    yt_text(cJSON_GetObjectItemCaseSensitive(d, "shortDescription"), out->description, sizeof out->description);
    out->duration_sec = json_int(d, "lengthSeconds", -1);
    {
        char vc[32] = "";
        json_str(d, "viewCount", vc, sizeof vc);
        out->views = parse_count(vc);
    }
    cJSON_Delete(root);
    return 0;
}

/* ---- the streams -------------------------------------------------------------------------------------- */

static Format format_from_innertube(const cJSON *o)
{
    Format f;
    memset(&f, 0, sizeof f);

    char mime[64] = "", url[URL_LEN] = "", q[24] = "";
    json_str(o, "mimeType", mime, sizeof mime);
    json_str(o, "url", url, sizeof url);
    f.has_url = url[0] != '\0';               /* signatureCipher instead: left false */
    str_copy(f.url, sizeof f.url, url);

    /* "video/mp4; codecs=\"avc1.64001f\"" */
    const char *sc = strstr(mime, "codecs=\"");
    const char *semi = strchr(mime, ';');
    size_t mn = semi ? (size_t)(semi - mime) : strlen(mime);
    if (mn >= sizeof f.mime) mn = sizeof f.mime - 1;
    memcpy(f.mime, mime, mn);
    f.mime[mn] = '\0';
    if (sc) {
        sc += 8;
        const char *end = strchr(sc, '"');
        size_t len = end ? (size_t)(end - sc) : strlen(sc);
        char codecs[48];
        if (len >= sizeof codecs) len = sizeof codecs - 1;
        memcpy(codecs, sc, len);
        codecs[len] = '\0';
        char *comma = strchr(codecs, ',');
        if (comma) *comma = '\0';
        if (str_starts(f.mime, "audio/")) str_copy(f.acodec, sizeof f.acodec, codecs);
        else                               str_copy(f.vcodec, sizeof f.vcodec, codecs);
        if (comma) str_copy(f.acodec, sizeof f.acodec, comma + 1);
    }

    f.width  = json_int(o, "width", 0);
    f.height = json_int(o, "height", 0);
    f.fps    = json_int(o, "fps", 0);
    f.bitrate_kbps = json_int(o, "bitrate", 0) / 1000;
    json_str(o, "qualityLabel", q, sizeof q);
    if (!f.height && q[0]) { int h = atoi(q); if (h > 0) f.height = h; }
    f.video = f.vcodec[0] != '\0' || str_starts(f.mime, "video/");
    f.audio = f.acodec[0] != '\0' || str_starts(f.mime, "audio/");
    if (!f.video && !f.audio) {                /* the mime was odd: fall back to the quality label */
        if (q[0]) f.video = true;
    }
    return f;
}

static ResolveStatus innertube_streams(const Conf *c, const char *id, Format *out, int max)
{
    char ctx[640], body[1024];
    client_context(ctx, sizeof ctx);
    snprintf(body, sizeof body,
        "{%s,\"videoId\":\"%s\",\"contentCheckOk\":true,\"racyCheckOk\":true}", ctx, id);

    char url[256];
    snprintf(url, sizeof url, "%s&key=%s", INNERTUBE_PLAYER, innertube_key());

    HttpResponse r;
    if (!http_post_json(url, body, c->search_timeout_ms, &r)) { http_response_free(&r); return RESOLVE_NO_NETWORK; }
    ResolveStatus st;
    innertube_parse_formats(r.data ? r.data : "", out, max, &st);
    http_response_free(&r);
    return st;
}

int innertube_parse_formats(const char *json, Format *out, int max, ResolveStatus *status)
{
    if (status) *status = RESOLVE_ALL_FAILED;
    cJSON *root = json_parse(json, strlen(json), "innertube player");
    if (!root) { if (status) *status = RESOLVE_INSTANCE_FAILED; return 0; }

    int n = 0;
    const cJSON *sd = json_at(root, "streamingData");
    const char *lists[2] = { "adaptiveFormats", "formats" };
    for (int li = 0; li < 2 && n < max; li++) {
        const cJSON *arr = json_at(sd, lists[li]);
        const cJSON *o = NULL;
        cJSON_ArrayForEach(o, arr) {
            if (n >= max) break;
            if (cJSON_IsObject(o)) out[n++] = format_from_innertube(o);
        }
    }

    /* a playability error ("This video is not available") arrives with no streamingData at all */
    if (n == 0) {
        char status[64] = "";
        yt_text(json_at(root, "playabilityStatus"), status, sizeof status);
        json_str(json_at(root, "playabilityStatus"), "reason", status, sizeof status);
        if (status[0]) log_line("innertube: %s", status);
    }

    cJSON_Delete(root);
    if (n == 0) { if (status) *status = RESOLVE_INSTANCE_FAILED; return 0; }
    if (status) {
        *status = RESOLVE_NEEDS_CIPHER;
        for (int i = 0; i < n; i++) if (out[i].has_url) { *status = RESOLVE_OK; break; }
    }
    return n;
}

const Backend backend_innertube = {
    .name = "innertube",
    .search = innertube_search,
    .detail = innertube_detail,
    .streams = innertube_streams,
};
