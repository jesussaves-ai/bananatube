/* backend.c -- the one place that decides which backend answers, and in what order.
 *
 * It used to live inside searchjob.c's worker thread, which meant a search could only be run from there: the
 * command line's --search, and the headless sessions in main.c, had no way to ask the same question. Now they
 * all call this, so what the handheld does and what the tests do cannot drift apart.
 *
 * The rule is the same as it always was: conf.backend chooses one, "auto" tries the Invidious instances and
 * then YouTube's own InnerTube -- never either of them twice.
 */
#include "net/backend.h"
#include "util.h"

#include <stdio.h>
#include <string.h>

int backend_search_auto(const Conf *c, const char *query, SearchResult *out, int max,
                        char *message, size_t message_cap)
{
    const Backend *order[2];
    int nb = 0;
    int n = 0;
    int answered = 0;

    if (message && message_cap) message[0] = '\0';

    if (c->backend == BACKEND_INNERTUBE)      order[nb++] = &backend_innertube;
    else if (c->backend == BACKEND_INVIDIOUS) order[nb++] = &backend_invidious;
    else { order[0] = &backend_invidious; order[1] = &backend_innertube; nb = 2; }

    /* A backend that could not be reached (-1) is not a backend that found nothing (0): the first is "your
     * Wi-Fi is off", the second is "nothing matched". */
    for (int b = 0; b < nb && n == 0; b++) {
        int got = order[b]->search(c, query, out, max);
        if (got < 0) {
            log_line("search: %s could not be reached", order[b]->name);
            if (message && message_cap)
                snprintf(message, message_cap, "Could not reach YouTube. Is Wi-Fi on?");
            continue;
        }
        answered = 1;
        n = got;
        if (got == 0 && message && message_cap)
            snprintf(message, message_cap, "%s found nothing for that", order[b]->name);
    }
    if (n == 0 && message && message_cap)
        snprintf(message, message_cap, answered ? "Nothing found for that."
                                                : "Could not reach YouTube. Is Wi-Fi on?");
    return n > 0 ? n : (answered ? 0 : -1);
}
