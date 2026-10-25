/* conf.c */
#include "conf.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void conf_defaults(Conf *c)
{
    memset(c, 0, sizeof *c);
    c->backend = BACKEND_AUTO;
    /* The built-in instance is only a starting point: public Invidious instances come and go, so the list in
     * net/invidious.c is tried in order and bananatube.conf overrides it. */
    c->instance[0] = '\0';
    c->quality = QUALITY_480;
    c->audio_only_on_lid = true;
    c->history_size = 50;
    c->cache_size = 12;
    c->search_timeout_ms = 6000;
}

const char *conf_quality_name(Quality q)
{
    switch (q) {
    case QUALITY_360: return "360p";
    case QUALITY_720: return "720p";
    case QUALITY_480:
    default:          return "480p";
    }
}

bool conf_quality_from_name(const char *s, Quality *out)
{
    if (!s) return false;
    if (strcmp(s, "360p") == 0 || strcmp(s, "360") == 0) { *out = QUALITY_360; return true; }
    if (strcmp(s, "480p") == 0 || strcmp(s, "480") == 0) { *out = QUALITY_480; return true; }
    if (strcmp(s, "720p") == 0 || strcmp(s, "720") == 0) { *out = QUALITY_720; return true; }
    return false;
}

const char *conf_backend_name(BackendKind b)
{
    switch (b) {
    case BACKEND_INVIDIOUS: return "invidious";
    case BACKEND_INNERTUBE: return "innertube";
    case BACKEND_AUTO:
    default:                return "auto";
    }
}

static bool is_yes(const char *v)
{
    return strcmp(v, "yes") == 0 || strcmp(v, "true") == 0 || strcmp(v, "on") == 0 || strcmp(v, "1") == 0;
}

bool conf_set(Conf *c, const char *key, const char *value)
{
    if (!key || !*key) return false;

    if (strcmp(key, "backend") == 0) {
        if (strcmp(value, "auto") == 0)           c->backend = BACKEND_AUTO;
        else if (strcmp(value, "invidious") == 0) c->backend = BACKEND_INVIDIOUS;
        else if (strcmp(value, "innertube") == 0) c->backend = BACKEND_INNERTUBE;
        else return false;
        return true;
    }
    if (strcmp(key, "instance") == 0) {
        str_copy(c->instance, sizeof c->instance, value);
        /* a trailing slash makes every URL we build have "//api": drop it */
        size_t n = strlen(c->instance);
        while (n > 0 && c->instance[n - 1] == '/') c->instance[--n] = '\0';
        return true;
    }
    if (strcmp(key, "quality") == 0) {
        Quality q;
        if (!conf_quality_from_name(value, &q)) return false;
        c->quality = q;
        return true;
    }
    if (strcmp(key, "audio_only_on_lid") == 0) { c->audio_only_on_lid = is_yes(value); return true; }
    if (strcmp(key, "history_size") == 0)      { c->history_size = clampi(atoi(value), 0, 1000); return true; }
    if (strcmp(key, "cache_size") == 0)        { c->cache_size = clampi(atoi(value), 1, 48); return true; }
    if (strcmp(key, "search_timeout_ms") == 0) { c->search_timeout_ms = clampi(atoi(value), 500, 60000); return true; }
    return false;
}

bool conf_load(Conf *c, const char *path)
{
    conf_defaults(c);
    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '#' || *s == ';' || *s == '\n' || *s == '\0') continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = s;
        char *val = eq + 1;
        /* trim both ends */
        char *e = key + strlen(key);
        while (e > key && (e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
        while (*val == ' ' || *val == '\t') val++;
        e = val + strlen(val);
        while (e > val && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
        conf_set(c, key, val);       /* an unknown key or a bad value keeps the default, quietly */
    }
    fclose(f);
    return true;
}

bool conf_save(const Conf *c, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fprintf(f,
        "# BananaTube: settings for the YouTube client (Settings > System, or edit this file).\n"
        "#\n"
        "# backend            auto, invidious or innertube. \"auto\" tries your Invidious instance first and\n"
        "#                    falls back to YouTube's own InnerTube API.\n"
        "# instance           an Invidious or Piped base URL, e.g. https://inv.nadeko.net (empty: a built-in list)\n"
        "# quality            360p, 480p or 720p. The RK3568's video unit decodes H.264 and VP9, not AV1, and\n"
        "#                    the panel is 480 tall, so 480p is the default.\n"
        "# audio_only_on_lid  yes: closing the lid keeps the sound playing and drops the video\n"
        "# history_size       how many watched videos to keep\n"
        "# cache_size         how many thumbnails to hold in video memory (12 is about 1.4 MB)\n"
        "backend = %s\n"
        "instance = %s\n"
        "quality = %s\n"
        "audio_only_on_lid = %s\n"
        "history_size = %d\n"
        "cache_size = %d\n"
        "search_timeout_ms = %d\n",
        conf_backend_name(c->backend), c->instance, conf_quality_name(c->quality),
        c->audio_only_on_lid ? "yes" : "no",
        c->history_size, c->cache_size, c->search_timeout_ms);
    fclose(f);
    return true;
}
