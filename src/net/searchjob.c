/* searchjob.c -- the search on a thread of its own.
 *
 * A search is 200-400 ms on the handheld's Wi-Fi. Done on the main thread it would freeze the keyboard
 * mid-keystroke and the spinner mid-turn; done here, the frame keeps coming and the UI shows a spinner until
 * the answer lands in the result slot below.
 */
#include "net/worker.h"
#include "util.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEARCHQ_MAX SEARCH_RESULTS_MAX
#define QUERY_MAX_LOCAL 128

struct SearchJob {
    Conf            conf;
    pthread_t       thread;
    pthread_mutex_t lock;
    pthread_cond_t  wake;
    bool            quit;
    bool            running;      /* a search is in flight */
    bool            ready;        /* a finished search is waiting to be taken */
    char            query[QUERY_MAX_LOCAL];
    char            message[160];
    SearchResult    results[SEARCHQ_MAX];
    int             count;
};

static void *search_main(void *arg)
{
    SearchJob *j = arg;

    for (;;) {
        pthread_mutex_lock(&j->lock);
        while (!j->quit && !j->running) pthread_cond_wait(&j->wake, &j->lock);
        if (j->quit) { pthread_mutex_unlock(&j->lock); return NULL; }
        char query[QUERY_MAX_LOCAL];
        str_copy(query, sizeof query, j->query);
        Conf conf = j->conf;
        pthread_mutex_unlock(&j->lock);

        SearchResult found[SEARCHQ_MAX];
        int n = 0;
        char message[160] = "";

        /* backend = auto: try the instance, then InnerTube. Never both twice. */
        const Backend *order[2];
        int nb = 0;
        if (conf.backend == BACKEND_INNERTUBE) { order[nb++] = &backend_innertube; }
        else if (conf.backend == BACKEND_INVIDIOUS) { order[nb++] = &backend_invidious; }
        else { order[0] = &backend_invidious; order[1] = &backend_innertube; nb = 2; }

        for (int b = 0; b < nb && n == 0; b++) {
            n = order[b]->search(&conf, query, found, SEARCHQ_MAX);
            if (n == 0) snprintf(message, sizeof message, "%s did not answer", order[b]->name);
        }
        if (n == 0 && !message[0]) str_copy(message, sizeof message, "No results. Is Wi-Fi on?");

        pthread_mutex_lock(&j->lock);
        j->count = n;
        if (n > 0) memcpy(j->results, found, sizeof(SearchResult) * (size_t)n);
        str_copy(j->message, sizeof j->message, message);
        j->running = false;
        j->ready = true;
        pthread_mutex_unlock(&j->lock);
    }
}

SearchJob *search_job_start(const Conf *c)
{
    SearchJob *j = calloc(1, sizeof *j);
    if (!j) return NULL;
    j->conf = *c;
    pthread_mutex_init(&j->lock, NULL);
    pthread_cond_init(&j->wake, NULL);
    if (pthread_create(&j->thread, NULL, search_main, j) != 0) {
        free(j);
        return NULL;
    }
    return j;
}

void search_job_stop(SearchJob *j)
{
    if (!j) return;
    pthread_mutex_lock(&j->lock);
    j->quit = true;
    pthread_cond_broadcast(&j->wake);
    pthread_mutex_unlock(&j->lock);
    pthread_join(j->thread, NULL);
    pthread_mutex_destroy(&j->lock);
    pthread_cond_destroy(&j->wake);
    free(j);
}

bool search_job_begin(SearchJob *j, const char *query)
{
    if (!j || !query || !*query) return false;
    pthread_mutex_lock(&j->lock);
    if (j->running || j->ready) { pthread_mutex_unlock(&j->lock); return false; }
    str_copy(j->query, sizeof j->query, query);
    j->running = true;
    pthread_cond_broadcast(&j->wake);
    pthread_mutex_unlock(&j->lock);
    return true;
}

bool search_job_running(SearchJob *j)
{
    if (!j) return false;
    pthread_mutex_lock(&j->lock);
    bool r = j->running;
    pthread_mutex_unlock(&j->lock);
    return r;
}

bool search_job_take(SearchJob *j, SearchResult *results, int max, int *count,
                     char *message, size_t message_cap)
{
    if (!j) return false;
    pthread_mutex_lock(&j->lock);
    if (!j->ready) { pthread_mutex_unlock(&j->lock); return false; }
    int n = j->count > max ? max : j->count;
    if (n > 0 && results) memcpy(results, j->results, sizeof(SearchResult) * (size_t)n);
    if (count) *count = n;
    if (message && message_cap) str_copy(message, message_cap, j->message);
    j->ready = false;
    pthread_mutex_unlock(&j->lock);
    return true;
}
