/* tests/test.h -- a check() that counts, so every test is one file with a main() and a summary. */
#ifndef BANANATUBE_TEST_H
#define BANANATUBE_TEST_H

#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int t_pass = 0, t_fail = 0;

#define check(cond) do { \
    if (cond) { t_pass++; } \
    else { t_fail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define check_str(a, b) do { \
    const char *_a = (a), *_b = (b); \
    if (_a && _b && strcmp(_a, _b) == 0) { t_pass++; } \
    else { t_fail++; printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, \
                            _a ? _a : "(null)", _b ? _b : "(null)"); } \
} while (0)

#define check_int(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a == _b) { t_pass++; } \
    else { t_fail++; printf("  FAIL %s:%d: %lld != %lld\n", __FILE__, __LINE__, _a, _b); } \
} while (0)

#define test_done() do { \
    printf("  %d checks, %d failed\n", t_pass + t_fail, t_fail); \
    return t_fail ? 1 : 0; \
} while (0)

/* tests/data, so a test can run from anywhere */
static inline const char *test_data(const char *name)
{
    static char path[512];
    snprintf(path, sizeof path, "%s/%s", TEST_DATA_DIR, name);
    return path;
}

static inline char *test_read(const char *name)
{
    FILE *f = fopen(test_data(name), "rb");
    if (!f) { printf("  FAIL: no %s (is TEST_DATA_DIR right?)\n", name); return NULL; }
    static char buf[1 << 20];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

#endif
