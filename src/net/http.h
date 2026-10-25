/* http.h -- libcurl, kept to what a search and a video page need. */
#ifndef BANANATUBE_HTTP_H
#define BANANATUBE_HTTP_H

#include <stdbool.h>
#include <stddef.h>

#include <cjson/cJSON.h>

/* A response body, capped: no single API reply on a 1 GB device deserves more than this. */
#define HTTP_MAX_BODY (2u * 1024u * 1024u)

typedef struct {
    char  *data;                 /* malloc'd, NUL-terminated (may be NULL when the transfer failed) */
    size_t len;
    long   status;               /* HTTP status code, 0 when the transfer did not complete */
    char   local_ip[64];         /* the local address the request went out from, and the peer's */
    char   peer_ip[64];
    char   error[160];
} HttpResponse;

void http_init(void);            /* curl_global_init, once */
void http_fini(void);

bool http_get(const char *url, int timeout_ms, HttpResponse *out);

/* A GET of the first couple of bytes (`Range: bytes=0-1`), to ask a media URL whether it will be served. A
 * stream URL is bound to the address that asked the API for it -- the ip= parameter is part of its signature --
 * so checking it from the same place and the same program is the only way to tell a URL that is fine from one
 * the CDN will refuse with a 403. */
bool http_get_range(const char *url, int timeout_ms, HttpResponse *out);

/* Which address family to make requests from: AF_UNSPEC (the system's choice), AF_INET or AF_INET6. The
 * resolver flips this when a media URL does not fetch, because a handheld on a dual-stack link can ask over
 * one address and fetch over another. */
void http_set_ip_family(int family);
bool http_post_json(const char *url, const char *body, int timeout_ms, HttpResponse *out);
void http_response_free(HttpResponse *r);

/* True when the transfer completed with a 2xx status and a body. */
bool http_ok(const HttpResponse *r);
/* A short reason for a failed transfer, for the log and the UI. */
const char *http_error(const HttpResponse *r);

/* ---- cJSON, without the NULL checks -------------------------------------------------------------------- */

/* The value at a dotted path ("contents.twoColumnSearchResultsRenderer.contents.0"), or NULL. A numeric
 * segment indexes an array. */
const cJSON *json_at(const cJSON *root, const char *path);

/* A string member (empty when missing), a number (dflt when missing), a bool. */
const char *json_str(const cJSON *o, const char *key, char *out, size_t cap);
int         json_int(const cJSON *o, const char *key, int dflt);
double      json_num(const cJSON *o, const char *key, double dflt);
long long   json_ll(const cJSON *o, const char *key, long long dflt);
bool        json_bool(const cJSON *o, const char *key, bool dflt);

/* Parses a body, or NULL (logging the offset). The caller owns the result. */
cJSON *json_parse(const char *data, size_t len, const char *what);

#endif /* BANANATUBE_HTTP_H */
