/* The draw list and the damage diff: the bottom screen only repaints what changed, so a wrong diff shows up
 * as a smeared or frozen screen on the device. */
#include "display/drawlist.h"
#include "test.h"

int main(void)
{
    DrawList a, b;
    Damage d;

    /* an empty list damages nothing */
    dl_reset(&a);
    dl_reset(&b);
    dl_diff(&a, &b, &d);
    check(damage_empty(&d));

    /* an unchanged frame damages nothing */
    dl_reset(&a);
    dl_rect(&a, 10, 10, 100, 50, 0xff112233);
    dl_text(&a, 10, 70, 200, 20, "hello", FONT_UI, 16, 0xffffffff, ALIGN_LEFT, VALIGN_MIDDLE);
    dl_reset(&b);
    dl_rect(&b, 10, 10, 100, 50, 0xff112233);
    dl_text(&b, 10, 70, 200, 20, "hello", FONT_UI, 16, 0xffffffff, ALIGN_LEFT, VALIGN_MIDDLE);
    dl_diff(&a, &b, &d);
    check(damage_empty(&d));

    /* a changed colour damages that box (and only it) */
    dl_reset(&b);
    dl_rect(&b, 10, 10, 100, 50, 0xffaabbcc);
    dl_text(&b, 10, 70, 200, 20, "hello", FONT_UI, 16, 0xffffffff, ALIGN_LEFT, VALIGN_MIDDLE);
    dl_diff(&a, &b, &d);
    check_int(d.n, 1);
    check_int(d.r[0].x, 10);
    check_int(d.r[0].y, 10);
    check_int(d.r[0].w, 100);
    check_int(d.r[0].h, 50);

    /* the same text at a different size is a different draw */
    dl_reset(&b);
    dl_rect(&b, 10, 10, 100, 50, 0xff112233);
    dl_text(&b, 10, 70, 200, 20, "hello", FONT_UI, 20, 0xffffffff, ALIGN_LEFT, VALIGN_MIDDLE);
    dl_diff(&a, &b, &d);
    check(!damage_empty(&d));

    /* an op that appeared damages its box */
    dl_reset(&b);
    dl_rect(&b, 10, 10, 100, 50, 0xff112233);
    dl_text(&b, 10, 70, 200, 20, "hello", FONT_UI, 16, 0xffffffff, ALIGN_LEFT, VALIGN_MIDDLE);
    dl_rect(&b, 300, 300, 40, 40, 0xff00ff00);
    dl_diff(&a, &b, &d);
    check(!damage_empty(&d));
    check(damage_touches(&d, 300, 300, 40, 40));
    check(!damage_touches(&d, 0, 400, 10, 10));

    /* an op that went damages where it was */
    dl_reset(&b);
    dl_rect(&b, 10, 10, 100, 50, 0xff112233);
    dl_diff(&a, &b, &d);
    check(damage_touches(&d, 10, 70, 200, 20));

    /* touch rectangles merge, so a few changes stay a few rectangles */
    damage_reset(&d);
    damage_add(&d, 0, 0, 10, 10);
    damage_add(&d, 5, 5, 10, 10);
    check_int(d.n, 1);
    check_int(d.r[0].x, 0);
    check_int(d.r[0].w, 15);
    damage_add(&d, 100, 100, 10, 10);
    check_int(d.n, 2);
    /* one swallowed by another adds nothing */
    damage_add(&d, 2, 2, 4, 4);
    check_int(d.n, 2);

    /* More separate places than we track: rather than growing a buffer nobody needs, the list collapses to
     * one bounding rectangle. (It fills up again from there, which is the right trade: one big repaint now,
     * small ones after.) */
    damage_reset(&d);
    for (int i = 0; i < MAX_DAMAGE; i++) damage_add(&d, i * 40, i * 40, 8, 8);
    check_int(d.n, MAX_DAMAGE);
    damage_add(&d, MAX_DAMAGE * 40, MAX_DAMAGE * 40, 8, 8);
    check_int(d.n, 1);
    check(d.r[0].w >= MAX_DAMAGE * 40 + 8);

    /* damage_bounds covers everything in the list */
    damage_reset(&d);
    damage_add(&d, 10, 20, 30, 40);
    damage_add(&d, 100, 5, 10, 10);
    int x, y, w, h;
    damage_bounds(&d, &x, &y, &w, &h);
    check_int(x, 10);
    check_int(y, 5);
    check_int(w, 100);
    check_int(h, 55);

    /* clips: an op is cut to the box in force when it was pushed */
    dl_reset(&a);
    dl_push_clip(&a, 0, 100, 640, 50);
    dl_rect(&a, 0, 0, 640, 480, 0xff000000);
    check_int(a.n, 1);
    check_int(a.op[0].clip_y, 100);
    check_int(a.op[0].clip_h, 50);
    dl_pop_clip(&a);
    dl_rect(&a, 0, 0, 10, 10, 0xff000000);
    check_int(a.op[1].clip_y, 0);
    check_int(a.op[1].clip_h, 480);

    /* an empty clip stays empty rather than going negative */
    dl_reset(&a);
    dl_push_clip(&a, 0, 0, 10, 10);
    dl_push_clip(&a, 100, 100, 10, 10);
    check_int(a.cw, 0);
    check_int(a.ch, 0);

    /* two ops in the same place with the same look are the same draw */
    DrawOp p, q;
    memset(&p, 0, sizeof p);
    memset(&q, 0, sizeof q);
    p.kind = q.kind = OP_RECT;
    p.x = q.x = 5; p.y = q.y = 6; p.w = q.w = 7; p.h = q.h = 8;
    p.color = q.color = 0xff000000;
    check(op_equal(&p, &q));
    q.color = 0xff111111;
    check(!op_equal(&p, &q));

    test_done();
}
