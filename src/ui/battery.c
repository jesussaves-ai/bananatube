/* battery.c -- the header's percentage, read from /sys/class/power_supply. See battery.h for the rule. */
#include "ui/battery.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One attribute of one supply, e.g. <sysdir>/battery/capacity. The value is a word or a number on one line. */
static void read_attr(const char *dir, const char *name, const char *attr, char *out, size_t cap)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s/%s", dir, name, attr);
    FILE *f = fopen(path, "r");
    out[0] = '\0';
    if (!f) return;
    if (fgets(out, (int)cap, f)) {
        size_t n = strlen(out);
        while (n && (out[n - 1] == '\n' || out[n - 1] == '\r')) out[--n] = '\0';
    }
    fclose(f);
}

void battery_read(Battery *b, const char *sysdir)
{
    b->have = false;
    b->percent = 100;
    b->charging = false;

    DIR *d = opendir(sysdir);
    if (!d) return;                       /* no such tree, or none readable: no battery to report */
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char type[64], val[64];
        read_attr(sysdir, e->d_name, "type", type, sizeof type);
        if (!b->have && strcmp(type, "Battery") == 0) {
            read_attr(sysdir, e->d_name, "capacity", val, sizeof val);
            b->percent = atoi(val);
            if (b->percent < 0) b->percent = 0;
            if (b->percent > 100) b->percent = 100;
            b->have = true;
        }
        /* Not `status`: the gauge says Discharging while it is on the cable, so any supply that says it is
         * online is the answer. */
        read_attr(sysdir, e->d_name, "online", val, sizeof val);
        if (atoi(val) == 1) b->charging = true;
    }
    closedir(d);
}

bool battery_get(Battery *b, const char *sysdir, double now_s, double secs)
{
    static double last_s;
    static bool got;

    if (got && now_s - last_s < secs) return false;
    battery_read(b, sysdir);
    last_s = now_s;
    got = true;
    return true;
}
