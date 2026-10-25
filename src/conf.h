/* conf.h -- banana/bananatube.conf on the card, and its defaults. */
#ifndef BANANATUBE_CONF_H
#define BANANATUBE_CONF_H

#include <stdbool.h>

typedef enum { BACKEND_AUTO = 0, BACKEND_INVIDIOUS, BACKEND_INNERTUBE } BackendKind;

/* The height we ask the stream selector for. 480p is the default: it is the panel's own height and it halves
 * the VPU's work against 720p. */
typedef enum { QUALITY_360 = 360, QUALITY_480 = 480, QUALITY_720 = 720 } Quality;

typedef struct {
    BackendKind backend;              /* auto: the instance first, InnerTube when that fails */
    char        instance[256];        /* an Invidious or Piped base URL; empty: the built-in list */
    Quality     quality;
    bool        audio_only_on_lid;    /* the lid closes during playback: keep the sound, drop the video */
    int         history_size;         /* how many watched videos to keep */
    int         cache_size;           /* how many thumbnails to hold as textures (see net/thumbnail.c) */
    int         search_timeout_ms;    /* how long to wait on the backend before giving up */
} Conf;

void conf_defaults(Conf *c);
bool conf_load(Conf *c, const char *path);          /* missing or unreadable: defaults, false */
bool conf_save(const Conf *c, const char *path);
/* One key=value pair, as the file's lines are. false when the key is not one of ours or the value is not
 * one it takes -- the caller keeps the old value. Also what tests drive. */
bool conf_set(Conf *c, const char *key, const char *value);

const char *conf_quality_name(Quality q);           /* "480p" */
bool        conf_quality_from_name(const char *s, Quality *out);
const char *conf_backend_name(BackendKind b);       /* "invidious" */

#endif /* BANANATUBE_CONF_H */
