/* util.h -- the small things: strings, time, files, logging. */
#ifndef BANANATUBE_UTIL_H
#define BANANATUBE_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- logging -----------------------------------------------------------------------------------------
 * One line to stderr (the launcher redirects it to banana/logs/bananatube.log). No levels: a game device
 * wants the line, not a framework. */
void log_line(const char *fmt, ...);

/* ---- strings ----------------------------------------------------------------------------------------- */
size_t str_copy(char *dst, size_t cap, const char *src);        /* always NUL-terminates; returns strlen(src) */
bool   str_starts(const char *s, const char *prefix);
bool   str_ends(const char *s, const char *suffix);
bool   str_contains(const char *s, const char *part);
/* A JSON-ish key lookup is not here on purpose: the backends use cJSON, which is already in the tree. */

/* ---- time -------------------------------------------------------------------------------------------- */
/* "12:34" for under an hour, "1:02:03" over, and "--:--" for a negative or unknown length. */
void fmt_duration(int seconds, char *out, size_t cap);

/* milliseconds since an arbitrary point, monotonic */
int64_t now_ms(void);

/* ---- files ------------------------------------------------------------------------------------------- */
/* Reads a whole file into a malloc'd NUL-terminated buffer, or NULL. *size_out (may be NULL) is the
 * length without the terminator. */
char *file_read(const char *path, size_t *size_out);

/* ---- numbers ----------------------------------------------------------------------------------------- */
int   clampi(int v, int lo, int hi);
float clampf(float v, float lo, float hi);

/* Writes `n` zero-padded decimal digits, e.g. pad(7, 2) writes "07". Returns the digits written. */
int   fmt_pad(int n, int width, char *out, size_t cap);

/* Percent-encodes for a query string: everything outside A-Za-z0-9-_.~ becomes %XX. Returns the length
 * written (the output is always NUL-terminated; a short buffer truncates). */
size_t url_encode(const char *in, char *out, size_t cap);

/* A view count as a person would read it: 987, 12K, 3.4M. An unknown (negative) count is "". */
void fmt_views(long long views, char *out, size_t cap);

/* ---- the command line -------------------------------------------------------------------------------- */
/* Is this argument something to search for? The menu starts an app with the entry it found, which for this
 * app is its own marker file -- /usr/share/banana/apps/bananatube/BananaTube.app -- and a search for that
 * path is not what opening YouTube should do. An option ("--shot") and a path (absolute, or ending in .app)
 * are not queries; anything else is. */
bool arg_is_query(const char *arg);

#endif /* BANANATUBE_UTIL_H */
