/* worker.h -- the two background jobs: a search, and the thumbnails.
 *
 * Both are one thread each (the thumbnails are two), and neither ever touches the frame: a worker puts what it
 * found in a queue and the main loop takes it. That is what keeps a 300 ms search from freezing the keyboard
 * someone is typing into, and a decode from stuttering a scroll.
 */
#ifndef BANANATUBE_WORKER_H
#define BANANATUBE_WORKER_H

#include "net/backend.h"
#include "net/thumbnail.h"

#include <stdbool.h>
#include <stddef.h>

#define DONE_MAX 32          /* finished pictures the main thread may take in one go */

/* ---- a search ----------------------------------------------------------------------------------------- */

typedef struct SearchJob SearchJob;

SearchJob *search_job_start(const Conf *c);
void       search_job_stop(SearchJob *j);

/* Starts a search. False when one is already running (the caller keeps its spinner up). */
bool       search_job_begin(SearchJob *j, const char *query);
bool       search_job_running(SearchJob *j);

/* Takes the result of a finished search once: true when one was waiting. `results` may run out of room for a
 * long list, which is fine -- nothing needs forty results on a 640x480 screen. */
bool       search_job_take(SearchJob *j, SearchResult *results, int max, int *count,
                           char *message, size_t message_cap);

/* ---- a thumbnail -------------------------------------------------------------------------------------- */

/* What a worker produced: the picture for `video_id`, to go in the caller's slot `slot`. `pixels` is the
 * caller's to free (or hand to its table) and is NULL when the fetch or the decode failed. */
typedef struct {
    int      slot;
    char     video_id[VIDEO_ID_LEN];
    uint8_t *pixels;
    int      w, h;
} ThumbDone;

/* For tests, and for anything that wants to serve pictures from somewhere else: fetch the JPEG for a video
 * into a malloc'd buffer. Returns false when there is no picture. */
typedef bool (*ThumbFetchFn)(const char *video_id, uint8_t **data, size_t *len, void *user);

typedef struct ThumbWorker ThumbWorker;

ThumbWorker *thumb_worker_start(int threads);
void         thumb_worker_stop(ThumbWorker *w);
void         thumb_worker_set_fetch(ThumbWorker *w, ThumbFetchFn fn, void *user);

/* Asks for a video's picture. False when that video is already queued or in flight: the grid asks every frame
 * and must not start the same download twice. */
bool         thumb_worker_request(ThumbWorker *w, int slot, const char *video_id);
bool         thumb_worker_pending(ThumbWorker *w);      /* something is queued or in flight */

/* The main thread takes what has finished. Returns how many it wrote to `out`. */
int          thumb_worker_drain(ThumbWorker *w, ThumbDone *out, int max);

#endif /* BANANATUBE_WORKER_H */
