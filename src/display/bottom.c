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
    /* A page flip in flight: the CRTC is already switching to another buffer and the event has not arrived
     * yet. Programming it now would cancel the flip -- the next frame's dl_diff will still be the cumulative
     * change (prev is not advanced), and a skipped frame is one the user cannot see anyway. banana-ui's
     * kms.cpp does exactly this: its present() returns false when its own pending counter is non-zero. */
    if (b->pending > 0) {
        static int warned = 0;
        if (!warned) { log_line("bottom: %s -- skipping a frame: a flip is in flight", p->name); warned = 1; }
        b->last_pixels = 0;
        b->last_draws = dl->n;
        return;
    }

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
     * reprogrammed. The fallback is a direct modeset, as banana-ui's present() does when
     * drmModePageFlip returns an error -- and like banana-ui's fallback, it never waits for a vblank.
     * drmWaitVBlank blocks until the CRTC scans out, which on a busy or disabled CRTC meant hundreds of
     * milliseconds (that is what "the UI is still laggy, 450 ms a frame" was -- the log's own numbers
     * showed the painting took tens of milliseconds and the rest was the block).
     *
     * The counter is ours to raise: the handler in drm.c drops it when the flip completes, and the check at
     * the top of this function skips a frame while it is up -- the two of them pass a token between them,
     * and an increment that is missing there means two flips queued at once, which cancels the first. */
    static int first_flip = -1, first_mode = -1;
    if (drm_page_flip(fd, p, dst, &b->pending)) {
        b->pending++;
        if (first_flip) { log_line("bottom: %s -- page flips", p->name); first_flip = 0; }
    } else {
        drm_set_crtc(fd, p, dst);
        if (first_mode) { log_line("bottom: %s -- flips refused, a modeset each frame", p->name); first_mode = 0; }
    }

    /* The buffer going behind the flip misses *this frame's* changes -- and only those. */
    b->missed[b->front] = own;
    b->front = back;
    b->prev = *dl;
    b->have_prev = true;
}
