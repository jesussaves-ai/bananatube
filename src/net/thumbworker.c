/* thumbworker.c -- the video grid's pictures, fetched and decoded off the frame.
 *
 * The frame must never wait for a download: a worker fetches the JPEG with libcurl, decodes it with libjpeg at
 * a fraction of its size (which is what thumb_decode does) and puts it in the done queue. The main loop takes
 * it, stores the pixels in its own table and marks the frame dirty. Two workers, niced: the panels' 60 Hz
 * comes first.
 */
#include "net/worker.h"
#include "net/http.h"
#include "util.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define QUEUE_MAX 64

typedef struct {
    int  slot;
    char video_id[VIDEO_ID_LEN];
} ThumbJob;

struct ThumbWorker {
    pthread_t       thread[4];
    int             nthreads;

    pthread_mutex_t lock;
    pthread_cond_t  wake;
    bool            quit;

    ThumbJob        queue[QUEUE_MAX];
    int             qn;                        /* queued */
    int             qhead;

    ThumbDone       done[DONE_MAX];
    int             dn;                        /* finished, waiting for the main thread */

    /* every id queued or in flight, so the grid can ask every frame without starting a download twice */
    char            asked[QUEUE_MAX][VIDEO_ID_LEN];
    int             asked_n;

    ThumbFetchFn    fetch;                     /* tests: serve the bytes from somewhere else */
    void           *fetch_user;
};

static void asked_add(ThumbWorker *w, const char *id)
{
    for (int i = 0; i < w->asked_n; i++) if (strcmp(w->asked[i], id) == 0) return;
    if (w->asked_n < QUEUE_MAX) str_copy(w->asked[w->asked_n++], VIDEO_ID_LEN, id);
}

static void asked_remove(ThumbWorker *w, const char *id)
{
    for (int i = 0; i < w->asked_n; i++) {
        if (strcmp(w->asked[i], id) != 0) continue;
        w->asked[i][0] = '\0';
        /* keep the array dense, so it never fills with tombstones */
        for (int k = i; k + 1 < w->asked_n; k++) memcpy(w->asked[k], w->asked[k + 1], VIDEO_ID_LEN);
        w->asked_n--;
        return;
    }
}

/* The real fetch: the small picture, and the big one if that is not there (some videos have only that). */
static bool http_fetch(const char *video_id, uint8_t **data, size_t *len, void *user)
{
    (void)user;
    const ThumbSize sizes[2] = { THUMB_SMALL, THUMB_LARGE };
    for (int i = 0; i < 2; i++) {
        char url[192];
        thumb_url(video_id, sizes[i], url, sizeof url);
        HttpResponse r;
        if (!http_get(url, 6000, &r)) { http_response_free(&r); continue; }
        *data = (uint8_t *)r.data;                  /* handed over as-is: the caller frees it */
        *len = r.len;
        return true;
    }
    return false;
}

static void *thumb_main(void *arg)
{
    ThumbWorker *w = arg;
    nice(5);                                        /* the panels come first */

    for (;;) {
        pthread_mutex_lock(&w->lock);
        while (!w->quit && w->qn == 0) pthread_cond_wait(&w->wake, &w->lock);
        if (w->quit) { pthread_mutex_unlock(&w->lock); return NULL; }
        ThumbJob job = w->queue[w->qhead];
        w->qhead = (w->qhead + 1) % QUEUE_MAX;
        w->qn--;
        pthread_mutex_unlock(&w->lock);

        uint8_t *data = NULL;
        size_t len = 0;
        uint8_t *pixels = NULL;
        int pw = 0, ph = 0;
        bool got = false;
        ThumbFetchFn fn = w->fetch ? w->fetch : http_fetch;
        if (fn(job.video_id, &data, &len, w->fetch_user) && data)
            got = thumb_decode(data, len, &pixels, &pw, &ph, THUMB_DRAW_W, THUMB_DRAW_H);
        free(data);

        pthread_mutex_lock(&w->lock);
        asked_remove(w, job.video_id);
        if (w->dn < DONE_MAX) {
            w->done[w->dn].slot = job.slot;
            str_copy(w->done[w->dn].video_id, VIDEO_ID_LEN, job.video_id);
            w->done[w->dn].pixels = got ? pixels : NULL;
            w->done[w->dn].w = got ? pw : 0;
            w->done[w->dn].h = got ? ph : 0;
            w->dn++;
        } else {
            free(pixels);                            /* the main thread is not keeping up: drop it */
        }
        pthread_mutex_unlock(&w->lock);
    }
}

ThumbWorker *thumb_worker_start(int threads)
{
    if (threads < 1) threads = 1;
    if (threads > 4) threads = 4;

    ThumbWorker *w = calloc(1, sizeof *w);
    if (!w) return NULL;
    w->nthreads = threads;
    pthread_mutex_init(&w->lock, NULL);
    pthread_cond_init(&w->wake, NULL);
    for (int i = 0; i < threads; i++)
        if (pthread_create(&w->thread[i], NULL, thumb_main, w) != 0) {
            w->nthreads = i;
            thumb_worker_stop(w);
            return NULL;
        }
    return w;
}

void thumb_worker_stop(ThumbWorker *w)
{
    if (!w) return;
    pthread_mutex_lock(&w->lock);
    w->quit = true;
    pthread_cond_broadcast(&w->wake);
    pthread_mutex_unlock(&w->lock);
    for (int i = 0; i < w->nthreads; i++) pthread_join(w->thread[i], NULL);
    for (int i = 0; i < w->dn; i++) free(w->done[i].pixels);
    pthread_mutex_destroy(&w->lock);
    pthread_cond_destroy(&w->wake);
    free(w);
}

void thumb_worker_set_fetch(ThumbWorker *w, ThumbFetchFn fn, void *user)
{
    if (!w) return;
    pthread_mutex_lock(&w->lock);
    w->fetch = fn;
    w->fetch_user = user;
    pthread_mutex_unlock(&w->lock);
}

bool thumb_worker_request(ThumbWorker *w, int slot, const char *video_id)
{
    if (!w || !video_id || !*video_id) return false;
    pthread_mutex_lock(&w->lock);
    for (int i = 0; i < w->asked_n; i++) {
        if (strcmp(w->asked[i], video_id) == 0) { pthread_mutex_unlock(&w->lock); return false; }
    }
    if (w->qn >= QUEUE_MAX) { pthread_mutex_unlock(&w->lock); return false; }
    asked_add(w, video_id);
    w->queue[(w->qhead + w->qn) % QUEUE_MAX].slot = slot;
    str_copy(w->queue[(w->qhead + w->qn) % QUEUE_MAX].video_id, VIDEO_ID_LEN, video_id);
    w->qn++;
    pthread_cond_signal(&w->wake);
    pthread_mutex_unlock(&w->lock);
    return true;
}

bool thumb_worker_pending(ThumbWorker *w)
{
    if (!w) return false;
    pthread_mutex_lock(&w->lock);
    bool p = w->qn > 0 || w->asked_n > 0;
    pthread_mutex_unlock(&w->lock);
    return p;
}

int thumb_worker_drain(ThumbWorker *w, ThumbDone *out, int max)
{
    if (!w) return 0;
    pthread_mutex_lock(&w->lock);
    int n = w->dn < max ? w->dn : max;
    for (int i = 0; i < n; i++) out[i] = w->done[i];
    for (int i = n; i < w->dn; i++) w->done[i - n] = w->done[i];
    w->dn -= n;
    pthread_mutex_unlock(&w->lock);
    return n;
}
