/* The watched list: what carries on from where, and what survives a restart.
 *
 * The file is JSON on the card, so this tests the round trip as well as the bookkeeping: a card written by one
 * run has to read back in the next, and a card with a broken file must not lose the app's place in it. */
#include "history.h"
#include "test.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *tmp_path(char *buf, size_t cap, const char *name)
{
    snprintf(buf, cap, "/tmp/bananatube-hist-%d-%s", (int)getpid(), name);
    return buf;
}

int main(void)
{
    History h;
    history_clear(&h);
    check_int(h.count, 0);
    check_int((int)sizeof h.query, 96);

    /* opening a video puts it at the front and remembers what it is */
    history_touch(&h, "aaaaaaaaaaa", "First video", "A channel", 754);
    check_int(h.count, 1);
    check_str(h.item[0].id, "aaaaaaaaaaa");
    check_str(h.item[0].title, "First video");
    check_int(h.item[0].duration_sec, 754);
    check(h.item[0].watched_at > 0);

    history_touch(&h, "bbbbbbbbbbb", "Second video", "B channel", 300);
    check_int(h.count, 2);
    check_str(h.item[0].id, "bbbbbbbbbbb");        /* most recent first */

    /* opening one again moves it to the front without duplicating it */
    history_touch(&h, "aaaaaaaaaaa", "First video", "A channel", 754);
    check_int(h.count, 2);
    check_str(h.item[0].id, "aaaaaaaaaaa");
    check_str(h.item[1].id, "bbbbbbbbbbb");

    /* where it got to */
    history_progress(&h, "aaaaaaaaaaa", 123.5);
    const HistoryItem *it = history_find(&h, "aaaaaaaaaaa");
    check(it != NULL);
    if (it) check(it->position > 123.0 && it->position < 124.0);
    check(history_find(&h, "ccccccccccc") == NULL);
    history_progress(&h, "ccccccccccc", 10);       /* not in the list: nothing happens */
    check_int(h.count, 2);

    /* the list the view shows: most recent first */
    int order[HISTORY_MAX];
    check_int(history_order(&h, order, HISTORY_MAX), 2);
    check_int(order[0], 0);
    check_int(order[1], 1);

    /* ---- the file ---- */
    char path[256];
    tmp_path(path, sizeof path, "round-trip.json");
    unlink(path);

    str_copy(h.query, sizeof h.query, "banana theme");
    check(history_save(&h, path));
    check(access(path, R_OK) == 0);

    History back;
    check(history_load(&back, path));
    check_int(back.count, 2);
    check_str(back.query, "banana theme");
    check_str(back.item[0].id, "aaaaaaaaaaa");
    check_str(back.item[1].id, "bbbbbbbbbbb");
    check_str(back.item[1].title, "Second video");
    check_int(back.item[1].duration_sec, 300);
    check(back.item[0].position > 123.0 && back.item[0].position < 124.0);
    check(back.item[0].watched_at == h.item[0].watched_at);
    unlink(path);

    /* a card with no file yet: an empty history, not an error */
    History fresh;
    check(!history_load(&fresh, "/tmp/bananatube-no-such-history-file"));
    check_int(fresh.count, 0);

    /* a file that is not JSON: an empty history, and nothing crashes */
    tmp_path(path, sizeof path, "broken.json");
    FILE *f = fopen(path, "w");
    check(f != NULL);
    if (f) { fputs("{ this is not json at all ", f); fclose(f); }
    check(!history_load(&fresh, path));
    check_int(fresh.count, 0);
    unlink(path);

    /* history_size from the settings: it is obeyed, clamped, and falls back to the array's size (the file is
     * an array, so a bigger number cannot be honoured) */
    History capped;
    history_clear(&capped);
    check_int(capped.cap, HISTORY_MAX);
    history_set_cap(&capped, 3);
    check_int(capped.cap, 3);
    for (int i = 0; i < 5; i++) {
        char id[VIDEO_ID_LEN];
        snprintf(id, sizeof id, "cap%08d", i);
        history_touch(&capped, id, "Capped", "A channel", 60);
    }
    check_int(capped.count, 3);
    check_str(capped.item[0].id, "cap00000004");
    check_str(capped.item[2].id, "cap00000002");       /* the first two fell off */
    check_int(history_order(&capped, order, HISTORY_MAX), 3);
    /* a smaller cap takes effect at once, from the end the list grows */
    history_set_cap(&capped, 1);
    check_int(capped.count, 1);
    check_str(capped.item[0].id, "cap00000004");
    /* nonsense in the file must not lose the list or crash it */
    history_set_cap(&capped, 0);
    check_int(capped.cap, 1);
    history_set_cap(&capped, 100000);
    check_int(capped.cap, HISTORY_MAX);
    /* and the file only holds what the cap says */
    tmp_path(path, sizeof path, "capped.json");
    unlink(path);
    history_set_cap(&capped, 2);
    history_touch(&capped, "cap00000009", "Capped", "A channel", 60);
    history_touch(&capped, "cap00000010", "Capped", "A channel", 60);
    check(history_save(&capped, path));
    History back2;
    check(history_load(&back2, path));
    check_int(back2.count, 2);
    check_str(back2.item[0].id, "cap00000010");
    unlink(path);

    /* the list is capped: the oldest falls off the end */
    History big;
    history_clear(&big);
    for (int i = 0; i < HISTORY_MAX + 10; i++) {
        char id[VIDEO_ID_LEN];
        snprintf(id, sizeof id, "vid%08d", i);
        history_touch(&big, id, "Many", "A channel", 60);
    }
    check_int(big.count, HISTORY_MAX);
    check_str(big.item[0].id, "vid00000059");       /* the newest is still first */
    check(history_find(&big, "vid00000000") == NULL);

    test_done();
}
