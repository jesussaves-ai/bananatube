/* util.c */
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

void log_line(const char *fmt, ...)
{
    fprintf(stderr, "bananatube: ");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

size_t str_copy(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) return 0;
    if (!src) { dst[0] = '\0'; return 0; }
    size_t n = strlen(src);
    size_t c = n < cap - 1 ? n : cap - 1;
    memcpy(dst, src, c);
    dst[c] = '\0';
    return n;
}

bool str_starts(const char *s, const char *prefix)
{
    if (!s || !prefix) return false;
    size_t n = strlen(prefix);
    return strncmp(s, prefix, n) == 0;
}

bool str_ends(const char *s, const char *suffix)
{
    if (!s || !suffix) return false;
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

bool arg_is_query(const char *arg)
{
    if (!arg || !*arg)  return false;
    if (arg[0] == '-')  return false;              /* an option, not a search */
    if (arg[0] == '/')  return false;              /* an absolute path */
    if (str_ends(arg, ".app")) return false;       /* the shelf's marker file */
    return true;
}

bool str_contains(const char *s, const char *part)
{
    return s && part && strstr(s, part) != NULL;
}

void fmt_duration(int seconds, char *out, size_t cap)
{
    if (!out || cap == 0) return;
    if (seconds < 0) { str_copy(out, cap, "--:--"); return; }
    int h = seconds / 3600, m = (seconds / 60) % 60, s = seconds % 60;
    if (h > 0) snprintf(out, cap, "%d:%02d:%02d", h, m, s);
    else       snprintf(out, cap, "%d:%02d", m, s);
}

int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

char *file_read(const char *path, size_t *size_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0 || (unsigned long)n > FILE_READ_MAX) { fclose(f); return NULL; }
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    if (size_out) *size_out = got;
    return buf;
}

/* A best-effort directory sync: the file itself is already fsynced, and exFAT may not accept this. */
static void sync_parent(const char *path)
{
    char dir[512];
    str_copy(dir, sizeof dir, path);
    char *slash = strrchr(dir, '/');
    if (!slash) return;
    if (slash == dir) slash[1] = '\0';
    else *slash = '\0';
    int fd = open(dir, O_RDONLY | O_DIRECTORY);
    if (fd >= 0) { fsync(fd); close(fd); }
}

bool file_write_atomic(const char *path, const void *data, size_t len)
{
    if (!path || (!data && len)) return false;
    char tmp[512];
    int n = snprintf(tmp, sizeof tmp, "%s.new", path);
    if (n < 0 || (size_t)n >= sizeof tmp) return false;

    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    const char *p = data;
    size_t left = len;
    while (left) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR) continue;
            close(fd);
            unlink(tmp);
            return false;
        }
        p += w;
        left -= (size_t)w;
    }
    if (fsync(fd) != 0 || close(fd) != 0) { unlink(tmp); return false; }
    if (rename(tmp, path) != 0) { unlink(tmp); return false; }
    sync_parent(path);
    return true;
}

bool file_key_value(const char *path, const char *key, char *out, size_t cap)
{
    if (out && cap) out[0] = '\0';
    if (!path || !key || !out || cap == 0) return false;
    FILE *f = fopen(path, "r");
    if (!f) return false;

    bool found = false;
    char line[512];
    while (!found && fgets(line, sizeof line, f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '#' || *s == ';' || *s == '\n' || *s == '\0') continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char *k = s;
        char *e = k + strlen(k);
        while (e > k && (e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
        if (strcmp(k, key) != 0) continue;
        char *v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        e = v + strlen(v);
        while (e > v && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
        str_copy(out, cap, v);
        found = true;
    }
    fclose(f);
    return found;
}

int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

int fmt_pad(int n, int width, char *out, size_t cap)
{
    if (!out || cap == 0) return 0;
    if (width < 1) width = 1;
    if ((size_t)width >= cap) width = (int)cap - 1;
    for (int i = width - 1; i >= 0; i--) {
        out[i] = (char)('0' + (n % 10));
        n /= 10;
    }
    out[width] = '\0';
    return width;
}

size_t url_encode(const char *in, char *out, size_t cap)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t w = 0;
    if (!out || cap == 0) return 0;
    for (const unsigned char *p = (const unsigned char *)in; p && *p; p++) {
        unsigned char c = *p;
        bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                     c == '-' || c == '_' || c == '.' || c == '~';
        if (plain) {
            if (w + 1 >= cap) break;
            out[w++] = (char)c;
        } else {
            if (w + 3 >= cap) break;
            out[w++] = '%';
            out[w++] = hex[c >> 4];
            out[w++] = hex[c & 15];
        }
    }
    out[w] = '\0';
    return w;
}

void fmt_views(long long views, char *out, size_t cap)
{
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (views < 0) return;
    if (views < 1000) { snprintf(out, cap, "%lld", views); return; }
    if (views < 1000000) { snprintf(out, cap, "%lldK", views / 1000); return; }
    long long m10 = views / 100000;                     /* tenths of a million */
    if (m10 >= 100) { snprintf(out, cap, "%lldM", views / 1000000); return; }
    snprintf(out, cap, "%lld.%lldM", m10 / 10, m10 % 10);
}
