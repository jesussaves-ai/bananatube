/* The thumbnail cache: 12 slots, and the least recently drawn one is the one reused. */
#include "net/thumbnail.h"
#include "test.h"

int main(void)
{
    ThumbCache tc;
    thumb_init(&tc, 3);
    check_int(thumb_count(&tc), 0);

    /* three videos take the three slots */
    check_int(thumb_slot_for(&tc, "aaaaaaaaaaa"), 0);
    check_int(thumb_slot_for(&tc, "bbbbbbbbbbb"), 1);
    check_int(thumb_slot_for(&tc, "ccccccccccc"), 2);
    check_int(thumb_count(&tc), 3);

    /* the same video is not loaded twice */
    check_int(thumb_slot_for(&tc, "aaaaaaaaaaa"), 0);
    check_int(thumb_count(&tc), 3);

    /* looking one up marks it used */
    check_int(thumb_touch(&tc, "bbbbbbbbbbb"), 1);
    check_int(thumb_touch(&tc, "zzzzzzzzzzz"), -1);

    /* the cache is full: the least recently drawn goes -- "ccccccccccc", never touched since it was added */
    check_int(thumb_slot_for(&tc, "ddddddddddd"), 2);
    check_str(thumb_id_at(&tc, 2), "ddddddddddd");
    check_int(thumb_touch(&tc, "ccccccccccc"), -1);
    check_int(thumb_count(&tc), 3);

    /* a failed load frees its slot for a retry */
    thumb_forget(&tc, 2);
    check_int(thumb_count(&tc), 2);
    check_int(thumb_touch(&tc, "ddddddddddd"), -1);
    check_int(thumb_slot_for(&tc, "eeeeeeeeeee"), 2);

    /* the cap is clamped to the table */
    ThumbCache big;
    thumb_init(&big, 9999);
    check_int(big.cap, THUMB_MAX_SLOTS);
    thumb_init(&big, 0);
    check_int(thumb_slot_for(&big, "aaaaaaaaaaa"), -1);

    /* the URL is the small one: 320x180 is the size we want decoded on this SoC, with the big one to fall back on */
    char u[160];
    thumb_url("dQw4w9WgXcQ", THUMB_SMALL, u, sizeof u);
    check(str_contains(u, "dQw4w9WgXcQ"));
    check(str_ends(u, "mqdefault.jpg"));
    thumb_url("dQw4w9WgXcQ", THUMB_LARGE, u, sizeof u);
    check(str_ends(u, "hqdefault.jpg"));

    test_done();
}
