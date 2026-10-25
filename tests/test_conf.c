/* The settings file: defaults, one key at a time, and the values that must be refused. */
#include "conf.h"
#include "test.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    Conf c;
    conf_defaults(&c);
    check_int(c.backend, BACKEND_AUTO);
    check_int(c.quality, QUALITY_480);
    check(c.audio_only_on_lid);
    check_int(c.history_size, 50);
    check_int(c.cache_size, 12);

    /* each key */
    check(conf_set(&c, "backend", "innertube"));
    check_int(c.backend, BACKEND_INNERTUBE);
    check(conf_set(&c, "quality", "720p"));
    check_int(c.quality, QUALITY_720);
    check(conf_set(&c, "audio_only_on_lid", "no"));
    check(!c.audio_only_on_lid);
    check(conf_set(&c, "history_size", "12"));
    check_int(c.history_size, 12);

    /* a trailing slash has to go, or every URL we build has "//api" in it */
    check(conf_set(&c, "instance", "https://inv.example/"));
    check_str(c.instance, "https://inv.example");

    /* a value the setting does not take is refused, and the old one stands */
    check(!conf_set(&c, "backend", "nonsense"));
    check_int(c.backend, BACKEND_INNERTUBE);
    check(!conf_set(&c, "quality", "1080p"));
    check_int(c.quality, QUALITY_720);
    check(!conf_set(&c, "nonsense_key", "1"));

    /* out-of-range numbers are clamped, not taken */
    check(conf_set(&c, "cache_size", "9999"));
    check_int(c.cache_size, 48);
    check(conf_set(&c, "cache_size", "0"));
    check_int(c.cache_size, 1);

    /* names round-trip */
    Quality q;
    check(conf_quality_from_name("360p", &q) && q == QUALITY_360);
    check(!conf_quality_from_name("1080p", &q));
    check_str(conf_quality_name(QUALITY_480), "480p");
    check_str(conf_backend_name(BACKEND_INNERTUBE), "innertube");

    /* a file: comments, spaces, an unknown key, and the file winning over the defaults */
    char path[] = "/tmp/bananatube-conf-XXXXXX";
    int fd = mkstemp(path);
    check(fd >= 0);
    if (fd >= 0) {
        FILE *f = fdopen(fd, "w");
        fputs("# a comment\n  quality = 360p  \n; another\ninstance=https://a.example\nnonsense = 5\n", f);
        fclose(f);

        Conf from_file;
        check(conf_load(&from_file, path));
        check_int(from_file.quality, QUALITY_360);
        check_str(from_file.instance, "https://a.example");
        check_int(from_file.history_size, 50);           /* untouched: the default */
        unlink(path);
    }

    /* a missing file is the defaults, not an error */
    Conf missing;
    check(!conf_load(&missing, "/tmp/bananatube-does-not-exist"));
    check_int(missing.quality, QUALITY_480);

    test_done();
}
