/* The software renderer, at both panel sizes.
 *
 * The panels are 640x480 on the RG DS and 1024x768 on the Plus, where every draw is multiplied by 1.6. A draw
 * that forgets to scale lands in the wrong place -- invisible on one panel size, an empty tile painted over
 * its neighbour on the other. That was a real bug: the placeholder branch of an image passed its box
 * unscaled. This test is what catches it next time. */
#include "display/render.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

#define BG    0xff17161c
#define PLACE 0xff00ff00
#define PICT  0xff0000ff

static uint32_t at(uint32_t *c, int w, int x, int y) { return c[(size_t)y * w + x]; }

int main(void)
{
    static uint8_t px[8 * 8 * 2];
    memset(px, 0x11, sizeof px);
    BottomTex tex[1] = { { px, 8, 8 } };

    /* A draw list whose two image ops are spaced so that "scaled" and "unscaled" cannot be confused. */
    DrawList dl;
    dl_reset(&dl);
    dl_rect(&dl, 10, 10, 60, 40, 0xffffffff);
    dl_image(&dl, 200, 10, 60, 40, IMAGE_NONE, FIT_COVER, PLACE);   /* no picture yet: the placeholder colour */
    dl_image(&dl, 10, 100, 60, 40, 0, FIT_COVER, PLACE);            /* a picture */

    int w = 640, h = 480;
    uint32_t *c = calloc((size_t)w * h, 4);
    check(c != NULL);
    if (!c) test_done();

    /* ---- scale 1.0: the RG DS ---- */
    render_full(c, w, h, NULL, &dl, tex, 1, 1.0f);
    check_int(at(c, w, 40, 30), 0xffffffff);            /* the rectangle */
    check_int(at(c, w, 230, 30), PLACE);                /* the placeholder is where it says */
    check_int(at(c, w, 40, 120) != 0, 1);               /* the picture drew something */
    check_int(at(c, w, 400, 300), 0xff000000);          /* and the background is black, not the fill */

    /* ---- scale 1.6: the RG DS Plus ---- */
    w = 1024; h = 768;
    free(c);
    c = calloc((size_t)w * h, 4);
    check(c != NULL);
    if (!c) test_done();
    render_full(c, w, h, NULL, &dl, tex, 1, 1.6f);

    /* the rectangle: 16,16 .. 111,79 */
    check_int(at(c, w, 60, 45), 0xffffffff);
    /* the placeholder: 320,16 .. 415,79 -- and NOT at its unscaled box (200..259, 10..49) */
    check_int(at(c, w, 360, 45), PLACE);
    check_int(at(c, w, 230, 30), 0xff000000);
    /* the picture: 16,160 .. 111,223 */
    check_int(at(c, w, 60, 190) != 0, 1);
    check_int(at(c, w, 60, 100), 0xff000000);           /* its unscaled box is empty */
    /* and nothing was drawn between the two rows */
    check_int(at(c, w, 60, 130), 0xff000000);

    /* render_ops clips to the rectangle it is given: the damaged-rectangle path */
    for (int i = 0; i < w * h; i++) c[i] = 0xff000000;
    render_ops(c, w, h, NULL, &dl, tex, 1, 1.6f, 16, 16, 96, 64);   /* the first rectangle only */
    check_int(at(c, w, 60, 45), 0xffffffff);
    check_int(at(c, w, 360, 45), 0xff000000);           /* the placeholder was outside the clip */
    check_int(at(c, w, 60, 190), 0xff000000);           /* and so was the picture */

    free(c);
    test_done();
}
