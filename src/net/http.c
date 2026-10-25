/* http.c */
#include "net/http.h"
#include "util.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
    bool   overflow;             /* the body grew past HTTP_MAX_BODY: stop, do not OOM */
    size_t limit;
} Sink;

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    Sink *s = userdata;
    size_t n = size * nmemb;
    if (s->overflow) return 0;
    if (s->len + n + 1 > s->limit) {
        /* Take what fits and stop: a reply this size is not one we can use anyway. */
        size_t room = s->limit > s->len + 1 ? s->limit - s->len - 1 : 0;
        if (room) memcpy(s->data + s->len, ptr, room), s->len += room;
        s->overflow = true;
        return n;                /* pretending we took it all makes curl end the transfer cleanly */
    }
    char *grown = realloc(s->data, s->len + n + 1);
    if (!grown) { s->overflow = true; return 0; }
    s->data = grown;
    memcpy(s->data + s->len, ptr, n);
    s->len += n;
    s->data[s->len] = '\0';
    return n;
}

static bool transfer(const char *url, const char *body, const char *content_type, int timeout_ms,
                     HttpResponse *out)
{
    memset(out, 0, sizeof *out);

    CURL *c = curl_easy_init();
    if (!c) { str_copy(out->error, sizeof out->error, "curl_easy_init failed"); return false; }

    struct curl_slist *hdr = NULL;
    Sink sink = { .limit = HTTP_MAX_BODY };

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, (long)timeout_ms);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 3000L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");          /* let curl ask for gzip/deflate */
    curl_easy_setopt(c, CURLOPT_USERAGENT, "BananaTube/0.1 (BananaOS; RG DS)");
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);                 /* the VPU and the threads: no SIGALRM */
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);

    if (body) {
        hdr = curl_slist_append(hdr, "Content-Type: application/json");
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    }
    if (content_type) hdr = curl_slist_append(hdr, content_type);
    if (hdr) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);

    CURLcode rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &out->status);

    if (rc != CURLE_OK) {
        str_copy(out->error, sizeof out->error, curl_easy_strerror(rc));
        free(sink.data);
        sink.data = NULL;
    } else if (sink.overflow) {
        str_copy(out->error, sizeof out->error, "reply too large");
        free(sink.data);
        sink.data = NULL;
    }

    if (hdr) curl_slist_free_all(hdr);
    curl_easy_cleanup(c);

    out->data = sink.data;
    out->len = sink.data ? sink.len : 0;
    return out->data != NULL && out->status >= 200 && out->status < 300;
}

void http_init(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void http_fini(void)
{
    curl_global_cleanup();
}

bool http_get(const char *url, int timeout_ms, HttpResponse *out)
{
    return transfer(url, NULL, NULL, timeout_ms, out);
}

bool http_post_json(const char *url, const char *body, int timeout_ms, HttpResponse *out)
{
    return transfer(url, body, NULL, timeout_ms, out);
}

void http_response_free(HttpResponse *r)
{
    free(r->data);
    r->data = NULL;
    r->len = 0;
}

bool http_ok(const HttpResponse *r)
{
    return r->data && r->status >= 200 && r->status < 300;
}

const char *http_error(const HttpResponse *r)
{
    if (r->error[0]) return r->error;
    if (r->status) { static char buf[48]; snprintf(buf, sizeof buf, "HTTP %ld", r->status); return buf; }
    return "no reply";
}

/* ---- cJSON, without the NULL checks -------------------------------------------------------------------- */

const cJSON *json_at(const cJSON *root, const char *path)
{
    if (!root || !path) return NULL;
    const cJSON *cur = root;
    const char *p = path;
    char key[96];
    while (*p && cur) {
        const char *dot = strchr(p, '.');
        size_t n = dot ? (size_t)(dot - p) : strlen(p);
        if (n >= sizeof key) return NULL;
        memcpy(key, p, n);
        key[n] = '\0';

        /* a numeric segment indexes an array when we are on one, a key otherwise */
        bool all_digits = n > 0;
        for (size_t i = 0; i < n; i++) if (key[i] < '0' || key[i] > '9') { all_digits = false; break; }
        if (all_digits && cJSON_IsArray(cur)) cur = cJSON_GetArrayItem(cur, atoi(key));
        else cur = cJSON_GetObjectItemCaseSensitive(cur, key);

        if (!dot) break;
        p = dot + 1;
    }
    return cur;
}

const char *json_str(const cJSON *o, const char *key, char *out, size_t cap)
{
    if (out && cap) out[0] = '\0';
    const cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, key) : NULL;
    if (!v || !cJSON_IsString(v) || !v->valuestring) return out ? out : "";
    if (out) str_copy(out, cap, v->valuestring);
    return out ? out : v->valuestring;
}

int json_int(const cJSON *o, const char *key, int dflt)
{
    const cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, key) : NULL;
    if (!v) return dflt;
    if (cJSON_IsNumber(v)) return (int)v->valuedouble;
    if (cJSON_IsString(v) && v->valuestring) return atoi(v->valuestring);
    return dflt;
}

double json_num(const cJSON *o, const char *key, double dflt)
{
    const cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, key) : NULL;
    if (!v) return dflt;
    if (cJSON_IsNumber(v)) return v->valuedouble;
    if (cJSON_IsString(v) && v->valuestring) return atof(v->valuestring);
    return dflt;
}

long long json_ll(const cJSON *o, const char *key, long long dflt)
{
    const cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, key) : NULL;
    if (!v) return dflt;
    if (cJSON_IsNumber(v)) return (long long)v->valuedouble;
    if (cJSON_IsString(v) && v->valuestring) return atoll(v->valuestring);
    return dflt;
}

bool json_bool(const cJSON *o, const char *key, bool dflt)
{
    const cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, key) : NULL;
    if (!v) return dflt;
    if (cJSON_IsBool(v)) return cJSON_IsTrue(v);
    if (cJSON_IsNumber(v)) return v->valuedouble != 0;
    return dflt;
}

cJSON *json_parse(const char *data, size_t len, const char *what)
{
    if (!data) return NULL;
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        const char *at = cJSON_GetErrorPtr();
        log_line("%s: the reply is not JSON (near \"%.32s\")", what ? what : "reply", at ? at : "?");
    }
    return root;
}
