/* resolvejob.c */
#include "net/resolvejob.h"
#include "net/resolver.h"
#include "util.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

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
        ResolveStatus st = resolver_resolve(&conf, id, &s);

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

void resolve_job_stop(ResolveJob *j)
{
    if (!j) return;
    pthread_mutex_lock(&j->lock);
    j->quit = true;
    pthread_cond_signal(&j->wake);
    pthread_mutex_unlock(&j->lock);
    pthread_join(j->thread, NULL);
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
