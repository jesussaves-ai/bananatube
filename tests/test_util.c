#include "test.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    const char *path = "/tmp/bananatube-util-atomic.txt";
    unlink(path);
    check(file_write_atomic(path, "hello", 5));
    size_t n = 0;
    char *got = file_read(path, &n);
    check(got != NULL);
    check_int((int)n, 5);
    check_str(got, "hello");
    free(got);

    check(!file_write_atomic("/tmp/bananatube-no-such-dir/x", "x", 1));

    const char *big = "/tmp/bananatube-util-too-big.txt";
    unlink(big);
    truncate(big, 2 * 1024 * 1024 + 1);
    check(file_read(big, &n) == NULL);
    unlink(big);

    const char *ini = "/tmp/bananatube-util-ini.conf";
    FILE *f = fopen(ini, "w");
    check(f != NULL);
    if (f) { fputs("# c\ntheme = light\naccent=blue\n", f); fclose(f); }
    char v[32];
    check(file_key_value(ini, "theme", v, sizeof v));
    check_str(v, "light");
    check(file_key_value(ini, "accent", v, sizeof v));
    check_str(v, "blue");
    check(!file_key_value(ini, "missing", v, sizeof v));
    check(!file_key_value("/tmp/bananatube-no-such-ini", "theme", v, sizeof v));

    unlink(path);
    unlink(ini);
    test_done();
}
