/* bottom.c */
#include "display/bottom.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

bool bottom_init(Bottom *b, int fd, const DrmPanel *p)
{
    memset(b, 0, sizeof *b);
    b->w = p->w;
    b->h = p->h;
    b->page = 0;
    b->canvas = calloc((size_t)p->w * p->h, sizeof(uint32_t));
    if (!b->canvas) return false;
    for (int i = 0; i < 2; i++)
        if (!dumb_alloc(fd, p, &b->buf[i])) return false;
    return true;
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

/* ---- the software renderer: one op at a time, clipped ------------------------------------------------- */

static void blend_px(uint32_t *canvas, int w, int h, int x, int y, uint32_t color)
{
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    uint32_t a = (color >> 24) & 0xff;
    if (a == 0) a = 0xff;
    uint32_t *p = &canvas[(size_t)y * w + x];
    if (a == 0xff) { *p = 0xff000000u | (color & 0x00ffffffu); return; }
    uint32_t o = *p;
    uint32_t r = (((color >> 16) & 0xff) * a + ((o >> 16) & 0xff) * (255 - a)) / 255;
    uint32_t g = (((color >> 8) & 0xff) * a + ((o >> 8) & 0xff) * (255 - a)) / 255;
    uint32_t b = ((color & 0xff) * a + (o & 0xff) * (255 - a)) / 255;
    *p = 0xff000000u | (r << 16) | (g << 8) | b;
}

/* Rounded corners are drawn by skipping the pixels outside the circle at each corner: no antialiasing, which
 * is what the look wants anyway on a 1x panel. */
static void fill_rect(Bottom *b, int x, int y, int w, int h, int radius, uint32_t color,
                      int cx, int cy, int cw, int ch)
{
    int x0 = x < cx ? cx : x, y0 = y < cy ? cy : y;
    int x1 = x + w > cx + cw ? cx + cw : x + w;
    int y1 = y + h > cy + ch ? cy + ch : y + h;
    if (x1 <= x0 || y1 <= y0) return;

    if (radius <= 0) {
        for (int yy = y0; yy < y1; yy++)
            for (int xx = x0; xx < x1; xx++) blend_px(b->canvas, b->w, b->h, xx, yy, color);
        return;
    }
    if (radius > (x1 - x0) / 2) radius = (x1 - x0) / 2;
    if (radius > (y1 - y0) / 2) radius = (y1 - y0) / 2;

    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
            int dx = 0, dy = 0;
            if (xx < x + radius) dx = x + radius - xx;
            else if (xx >= x + w - radius) dx = xx - (x + w - radius - 1);
            if (yy < y + radius) dy = y + radius - yy;
            else if (yy >= y + h - radius) dy = yy - (y + h - radius - 1);
            if (dx && dy && dx * dx + dy * dy > radius * radius) continue;   /* outside the corner */
            blend_px(b->canvas, b->w, b->h, xx, yy, color);
        }
    }
}

static void draw_image(Bottom *b, const DrawOp *op, const BottomTex *tex, int tex_n,
                       int cx, int cy, int cw, int ch, int scale)
{
    const BottomTex *t = (tex && op->image >= 0 && op->image < tex_n) ? &tex[op->image] : NULL;
    if (!t || !t->pixels || t->w <= 0 || t->h <= 0) {
        fill_rect(b, op->x, op->y, op->w, op->h, 0, op->color, cx, cy, cw, ch);
        return;
    }

    /* The panel is the theme's 640x480 grid scaled up on the Plus, so a picture is scaled the same way. */
    int bx = op->x * scale, by = op->y * scale, bw = op->w * scale, bh = op->h * scale;

    int dw = bw, dh = bh;
    if (op->fit == FIT_CONTAIN || op->fit == FIT_PIXEL) {
        /* whole multiples for pixel art, otherwise fit inside; both keep the aspect */
        dw = t->w;
        dh = t->h;
        int sx = bw / t->w, sy = bh / t->h;
        int s = sx < sy ? sx : sy;
        if (s < 1) {
            dw = bw;
            dh = t->h * bw / (t->w ? t->w : 1);
        } else if (op->fit == FIT_PIXEL) { dw = t->w * s; dh = t->h * s; }
        else { dw = t->w * s; dh = t->h * s; }
    } else if (op->fit == FIT_COVER) {
        dw = bw; dh = bh;
    }

    int ox = bx + (bw - dw) / 2, oy = by + (bh - dh) / 2;
    for (int yy = 0; yy < dh; yy++) {
        int dy = oy + yy;
        if (dy < cy || dy >= cy + ch || dy < 0 || dy >= b->h) continue;
        int sy = (op->fit == FIT_COVER) ? yy * t->h / (dh ? dh : 1) : yy * t->h / (dh ? dh : 1);
        if (sy >= t->h) sy = t->h - 1;
        for (int xx = 0; xx < dw; xx++) {
            int dx = ox + xx;
            if (dx < cx || dx >= cx + cw || dx < 0 || dx >= b->w) continue;
            int sx = xx * t->w / (dw ? dw : 1);
            if (sx >= t->w) sx = t->w - 1;
            const uint8_t *p = t->pixels + ((size_t)sy * t->w + sx) * 2;
            uint16_t c = (uint16_t)((p[0] << 8) | p[1]);
            uint32_t rgb = 0xff000000u | (((c >> 11) & 0x1f) << 19) | (((c >> 5) & 0x3f) << 10) | ((c & 0x1f) << 3);
            blend_px(b->canvas, b->w, b->h, dx, dy, rgb);
        }
    }
}

static void draw_op(Bottom *b, const DrawOp *op, FontCtx *fnt, const BottomTex *tex, int tex_n,
                    int cx, int cy, int cw, int ch, int scale)
{
    int x = op->x * scale, y = op->y * scale, w = op->w * scale, h = op->h * scale;

    switch (op->kind) {
    case OP_RECT:
        fill_rect(b, x, y, w, h, op->radius * scale, op->color, cx, cy, cw, ch);
        if (op->border > 0) {
            int t = op->border * scale;
            fill_rect(b, x, y, w, t, 0, op->border_color, cx, cy, cw, ch);
            fill_rect(b, x, y + h - t, w, t, 0, op->border_color, cx, cy, cw, ch);
            fill_rect(b, x, y, t, h, 0, op->border_color, cx, cy, cw, ch);
            fill_rect(b, x + w - t, y, t, h, 0, op->border_color, cx, cy, cw, ch);
        }
        break;
    case OP_TEXT:
        if (fnt)
            font_draw(fnt, b->canvas, b->w, b->h, cx, cy, cw, ch, x, y, w, h, op->text,
                      op->font, op->size * scale, op->color, op->align, op->valign);
        break;
    case OP_IMAGE:
        draw_image(b, op, tex, tex_n, cx, cy, cw, ch, scale);
        break;
    }
}

/* ---- putting a frame on the panel --------------------------------------------------------------------- */

void bottom_present(Bottom *b, int fd, const DrmPanel *p, const DrawList *dl,
                    const BottomTex *tex, int tex_n)
{
    Damage dmg;
    dl_diff(&b->prev, dl, &dmg);
    if (b->have_prev && damage_empty(&dmg)) return;      /* nothing changed: the panel keeps its pixels */

    int scale = b->w / SCREEN_W;                          /* 1 on the RG DS, 1 on QEMU, 1 on the Plus's DPI */
    if (scale < 1) scale = 1;

    /* Repaint only the damaged rectangles, every op that touches them clipped to them. */
    for (int i = 0; i < dmg.n; i++) {
        int dx = dmg.r[i].x * scale, dy = dmg.r[i].y * scale;
        int dw = dmg.r[i].w * scale, dh = dmg.r[i].h * scale;
        if (dx < 0) { dw += dx; dx = 0; }
        if (dy < 0) { dh += dy; dy = 0; }
        if (dx + dw > b->w) dw = b->w - dx;
        if (dy + dh > b->h) dh = b->h - dy;
        if (dw <= 0 || dh <= 0) continue;

        fill_rect(b, dx, dy, dw, dh, 0, 0xff000000u, dx, dy, dw, dh);   /* the screen's own black */
        for (int k = 0; k < dl->n; k++) {
            const DrawOp *op = &dl->op[k];
            int ox = op->x * scale, oy = op->y * scale, ow = op->w * scale, oh = op->h * scale;
            if (ox >= dx + dw || oy >= dy + dh || ox + ow <= dx || oy + oh <= dy) continue;
            draw_op(b, op, b->font, tex, tex_n, dx, dy, dw, dh, scale);
        }
    }

    b->last_damage = dmg.n;
    b->last_draws = dl->n;

    /* Copy the damaged rectangles into the spare scanout buffer and flip to it. */
    DumbBuf *dst = &b->buf[(b->page + 1) % 2];
    for (int i = 0; i < dmg.n; i++) {
        int dx = dmg.r[i].x * scale, dy = dmg.r[i].y * scale;
        int dw = dmg.r[i].w * scale, dh = dmg.r[i].h * scale;
        if (dx < 0) { dw += dx; dx = 0; }
        if (dy < 0) { dh += dy; dy = 0; }
        if (dx + dw > b->w) dw = b->w - dx;
        if (dy + dh > b->h) dh = b->h - dy;
        if (dw <= 0 || dh <= 0) continue;
        for (int yy = 0; yy < dh; yy++)
            memcpy(dst->map + (size_t)(dy + yy) * dst->pitch + (size_t)dx * 4,
                   b->canvas + (size_t)(dy + yy) * b->w + dx, (size_t)dw * 4);
    }

    drm_set_crtc(fd, p, dst);
    drm_wait_vblank(fd, p);
    b->page = (b->page + 1) % 2;
    b->prev = *dl;
    b->have_prev = true;
}
