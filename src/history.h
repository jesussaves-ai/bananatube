/* history.h -- what has been watched, and where it was left.
 *
 * A small JSON file on the card (banana/bananatube-history.json): at most HISTORY_MAX entries, most recent
 * first, plus the last search so a restart comes back to it. JSON rather than SQLite because the data is a few
 * kilobytes, cJSON is already here for the backends, and a file that can be read and edited on a computer is
 * worth more on a handheld than a query language. The whole file is rewritten on save; at this size that is
 * nothing, and it is written on a pause, a stop and an exit rather than per frame.
 */
#ifndef BANANATUBE_HISTORY_H
#define BANANATUBE_HISTORY_H

#include "net/backend.h"

#include <stdbool.h>
#include <stdint.h>

#define HISTORY_MAX 50

typedef struct {
    char    id[VIDEO_ID_LEN];
    char    title[TITLE_LEN];
    char    channel[CHANNEL_LEN];
    int     duration_sec;
    double  position;        /* where playback stopped, in seconds */
    int64_t watched_at;      /* unix time of the last time it was opened */
} HistoryItem;

typedef struct {
    HistoryItem item[HISTORY_MAX];
    int         count;                 /* most recent first */
    char        query[96];             /* the last search, so a restart comes back to it */
} History;

void history_clear(History *h);
bool history_load(History *h, const char *path);       /* a missing or broken file: an empty history */
bool history_save(const History *h, const char *path);

/* Records that a video was opened: moves it to the front, keeps its position. */
void history_touch(History *h, const char *id, const char *title, const char *channel, int duration_sec);
/* Where it got to. Does nothing for a video that is not in the list. */
void history_progress(History *h, const char *id, double position);
const HistoryItem *history_find(const History *h, const char *id);

/* The entries' indices, most recent first (what the list shows). Returns how many it wrote. */
int  history_order(const History *h, int *out, int max);

#endif /* BANANATUBE_HISTORY_H */
