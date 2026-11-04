/* resolvejob.c */
#include "net/http.h"
#include "net/resolvejob.h"
#include "net/resolver.h"
#include "util.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct ResolveJob {
    pthread_t       thread;
    pthread_mutex_t lock;
    pthread_cond_t  wake;

    bool            running;        /* a request is being worked on */
    bool            ready;          /* an answer is waiting to be taken */
    bool            quit;

    char            video_id[VIDEO_ID_LEN];
    Conf            conf;

    StreamUrl       out;
    ResolveStatus   status;
};

static void *resolve_main(void *arg)
{
    ResolveJob *j = arg;

    for (;;) {
        pthread_mutex_lock(&j->lock);
        while (!j->quit && !j->running) pthread_cond_wait(&j->wake, &j->lock);
        if (j->quit) { pthread_mutex_unlock(&j->lock); return NULL; }

        char id[VIDEO_ID_LEN];
        str_copy(id, sizeof id, j->video_id);
        Conf conf = j->conf;
        pthread_mutex_unlock(&j->lock);

        StreamUrl s;
        http_set_deadline_ms(now_ms() + 15000);
        ResolveStatus st = resolver_resolve(&conf, id, &s);
        http_set_deadline_ms(0);

        pthread_mutex_lock(&j->lock);
        j->out = s;
        j->status = st;
        j->running = false;
        j->ready = true;
        pthread_mutex_unlock(&j->lock);
    }
}

ResolveJob *resolve_job_start(void)
{
    ResolveJob *j = calloc(1, sizeof *j);
    if (!j) return NULL;
    pthread_mutex_init(&j->lock, NULL);
    pthread_cond_init(&j->wake, NULL);
    if (pthread_create(&j->thread, NULL, resolve_main, j) != 0) {
        free(j);
        return NULL;
    }
    return j;
}

/* Wait for the worker, but never longer than the caller can afford. */
static bool join_within(pthread_t t, long ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += ms * 1000000L;
    ts.tv_sec += ts.tv_nsec / 1000000000L;
    ts.tv_nsec %= 1000000000L;
    return pthread_timedjoin_np(t, NULL, &ts) == 0;
}

void resolve_job_stop(ResolveJob *j)
{
    if (!j) return;
    pthread_mutex_lock(&j->lock);
    j->quit = true;
    pthread_cond_signal(&j->wake);
    pthread_mutex_unlock(&j->lock);
    if (!join_within(j->thread, 250)) {
        /* shortcut: a worker still inside a network timeout is detached and its job is leaked until the
         * process exits; upgrade to a cancellable transfer if this ever becomes common. */
        pthread_detach(j->thread);
        return;
    }
    pthread_mutex_destroy(&j->lock);
    pthread_cond_destroy(&j->wake);
    free(j);
}

bool resolve_job_begin(ResolveJob *j, const Conf *c, const char *video_id)
{
    if (!j || !video_id || !*video_id) return false;
    pthread_mutex_lock(&j->lock);
    if (j->running) { pthread_mutex_unlock(&j->lock); return false; }
    str_copy(j->video_id, sizeof j->video_id, video_id);
    j->conf = *c;
    j->running = true;
    j->ready = false;
    pthread_cond_signal(&j->wake);
    pthread_mutex_unlock(&j->lock);
    return true;
}

bool resolve_job_busy(ResolveJob *j)
{
    bool b;
    if (!j) return false;
    pthread_mutex_lock(&j->lock);
    b = j->running;
    pthread_mutex_unlock(&j->lock);
    return b;
}

bool resolve_job_take(ResolveJob *j, StreamUrl *out, ResolveStatus *status)
{
    bool got = false;
    if (!j) return false;
    pthread_mutex_lock(&j->lock);
    if (j->ready) {
        if (out) *out = j->out;
        if (status) *status = j->status;
        j->ready = false;
        got = true;
    }
    pthread_mutex_unlock(&j->lock);
    return got;
}
