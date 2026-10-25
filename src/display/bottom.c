/* bottom.c */
#include "display/bottom.h"
#include "display/render.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

bool bottom_init(Bottom *b, int fd, const DrmPanel *p)
{
    memset(b, 0, sizeof *b);
    b->w = p->w;
    b->h = p->h;
    b->front = 0;
    damage_reset(&b->missed[0]);
    damage_reset(&b->missed[1]);
    b->canvas = calloc((size_t)p->w * p->h, sizeof(uint32_t));
    if (!b->canvas) return false;
    for (int i = 0; i < 2; i++)
        if (!dumb_alloc(fd, p, &b->buf[i])) return false;
    return true;
}

void bottom_invalidate(Bottom *b)
{
    b->have_prev = false;
}

void bottom_set_font(Bottom *b, FontCtx *f)
{
    b->font = f;
}

void bottom_fini(Bottom *b, int fd)
{
    for (int i = 0; i < 2; i++) dumb_free(fd, &b->buf[i]);
    free(b->canvas);
    b->canvas = NULL;
}

/* ---- putting a frame on the panel --------------------------------------------------------------------- */

void bottom_present(Bottom *b, int fd, const DrmPanel *p, const DrawList *dl,
                    const BottomTex *tex, int tex_n, float scale)
{
    Damage own, dmg;
    long painted = 0;
    int back = b->front ^ 1;

    dl_diff(&b->prev, dl, &own);           /* what this frame changed */

    /* What has to be repainted: this frame's own changes, plus whatever the buffer we are drawing into missed
     * while it was hidden behind the other one (a flip alternates them, so each is missing the frame it sat
     * out). It is repainted from this frame's canvas, which is whole.
     *
     * `own` and `dmg` are deliberately separate: storing the *amplified* set back as "what this buffer missed"
     * fed the damage back into itself, and because more than MAX_DAMAGE rectangles collapse to one bounding
     * box, that collapse then repeated every frame -- the whole panel repainted, both panels, for ever. */
    damage_next(&own, &b->missed[back], &dmg, &b->missed[back]);

    if (b->have_prev && damage_empty(&dmg)) {
        b->last_pixels = 0;
        return;                                          /* nothing changed: the panel keeps its pixels */
    }

    if (scale <= 0) scale = 1.0f;                        /* 1.0 on the RG DS, 1.6 on the RG DS Plus */

    /* Repaint only the damaged rectangles, every op that touches them clipped to them. */
    for (int i = 0; i < dmg.n; i++) {
        int dx = render_scale(dmg.r[i].x, scale), dy = render_scale(dmg.r[i].y, scale);
        int dw = render_scale(dmg.r[i].w, scale), dh = render_scale(dmg.r[i].h, scale);
        if (dx < 0) { dw += dx; dx = 0; }
        if (dy < 0) { dh += dy; dy = 0; }
        if (dx + dw > b->w) dw = b->w - dx;
        if (dy + dh > b->h) dh = b->h - dy;
        if (dw <= 0 || dh <= 0) continue;

        RenderCanvas rc = { b->canvas, b->w, b->h, b->font };
        fill_rect_black(&rc, dx, dy, dw, dh);
        render_ops(b->canvas, b->w, b->h, b->font, dl, tex, tex_n, scale, dx, dy, dw, dh);
        painted += (long)dw * dh;
    }

    b->last_damage = dmg.n;
    b->last_draws = dl->n;
    b->last_pixels = painted;

    /* Copy the damaged rectangles into the spare scanout buffer and put it up. */
    DumbBuf *dst = &b->buf[back];
    for (int i = 0; i < dmg.n; i++) {
        int dx = render_scale(dmg.r[i].x, scale), dy = render_scale(dmg.r[i].y, scale);
        int dw = render_scale(dmg.r[i].w, scale), dh = render_scale(dmg.r[i].h, scale);
        if (dx < 0) { dw += dx; dx = 0; }
        if (dy < 0) { dh += dy; dy = 0; }
        if (dx + dw > b->w) dw = b->w - dx;
        if (dy + dh > b->h) dh = b->h - dy;
        if (dw <= 0 || dh <= 0) continue;
        for (int yy = 0; yy < dh; yy++)
            memcpy(dst->map + (size_t)(dy + yy) * dst->pitch + (size_t)dx * 4,
                   b->canvas + (size_t)(dy + yy) * b->w + dx, (size_t)dw * 4);
    }

    /* A page flip when one can be queued -- the CRTC switches at the next vblank instead of being
     * reprogrammed, which is the difference between a frame costing microseconds and costing a modeset. A
     * flip already in flight, or a lost master, falls back to setting the CRTC directly: never skip the frame,
     * because the frame is already drawn and the app believes it is on screen. */
    if (b->pending > 0 || !drm_page_flip(fd, p, dst, &b->pending)) {
        drm_set_crtc(fd, p, dst);
        drm_wait_vblank(fd, p);
    }

    /* The buffer going behind the flip misses *this frame's* changes -- and only those. */
    b->missed[b->front] = own;
    b->front = back;
    b->prev = *dl;
    b->have_prev = true;
}
