/* render.c -- the software renderer for the bottom screen. See render.h. */
#include "display/render.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

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

static void fill_rect(RenderCanvas *b, int x, int y, int w, int h, int radius, uint32_t color,
                      int cx, int cy, int cw, int ch);

void fill_rect_black(RenderCanvas *rc, int x, int y, int w, int h)
{
    fill_rect(rc, x, y, w, h, 0, 0xff000000u, x, y, w, h);
}

static void fill_rect(RenderCanvas *b, int x, int y, int w, int h, int radius, uint32_t color,
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

int render_scale(int v, float k) { return (int)((float)v * k + (v < 0 ? -0.5f : 0.5f)); }

static void draw_image(RenderCanvas *b, const DrawOp *op, const BottomTex *tex, int tex_n,
                       int cx, int cy, int cw, int ch, float scale)
{
    const BottomTex *t = (tex && op->image >= 0 && op->image < tex_n) ? &tex[op->image] : NULL;
    if (!t || !t->pixels || t->w <= 0 || t->h <= 0) {
        /* No picture (yet): the op's placeholder colour. The box is scaled like every other draw -- passing it
         * unscaled painted an empty tile over its neighbour, which only showed on the Plus's 1.6. */
        fill_rect(b, render_scale(op->x, scale), render_scale(op->y, scale), render_scale(op->w, scale), render_scale(op->h, scale),
                  0, op->color, cx, cy, cw, ch);
        return;
    }

    /* The panel is the 640x480 grid scaled up on the Plus, so a picture is scaled the same way. */
    int bx = render_scale(op->x, scale), by = render_scale(op->y, scale), bw = render_scale(op->w, scale), bh = render_scale(op->h, scale);

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

static void draw_op(RenderCanvas *b, const DrawOp *op, FontCtx *fnt, const BottomTex *tex, int tex_n,
                    int cx, int cy, int cw, int ch, float scale)
{
    int x = render_scale(op->x, scale), y = render_scale(op->y, scale), w = render_scale(op->w, scale), h = render_scale(op->h, scale);

    switch (op->kind) {
    case OP_RECT:
        fill_rect(b, x, y, w, h, render_scale(op->radius, scale), op->color, cx, cy, cw, ch);
        if (op->border > 0) {
            int t = render_scale(op->border, scale) < 1 ? 1 : render_scale(op->border, scale);
            fill_rect(b, x, y, w, t, 0, op->border_color, cx, cy, cw, ch);
            fill_rect(b, x, y + h - t, w, t, 0, op->border_color, cx, cy, cw, ch);
            fill_rect(b, x, y, t, h, 0, op->border_color, cx, cy, cw, ch);
            fill_rect(b, x + w - t, y, t, h, 0, op->border_color, cx, cy, cw, ch);
        }
        break;
    case OP_TEXT:
        if (fnt)
            font_draw_box(fnt, b->canvas, b->w, b->h, cx, cy, cw, ch, x, y, w, h, op->text,
                          op->font, render_scale(op->size, scale) < 1 ? 1 : render_scale(op->size, scale),
                          op->color, op->align, op->valign,
                          op->wrap, op->max_lines, render_scale(op->line_height, scale));
        break;
    case OP_IMAGE:
        draw_image(b, op, tex, tex_n, cx, cy, cw, ch, scale);
        break;
    }
}

/* ---- the whole frame, with no panel under it ---------------------------------------------------------- */



/* ---- entry points ------------------------------------------------------------------------------------- */

void render_ops(uint32_t *canvas, int w, int h, FontCtx *fnt, const DrawList *dl,
                const BottomTex *tex, int tex_n, float scale,
                int cx, int cy, int cw, int ch)
{
    if (!canvas || !dl) return;
    if (scale <= 0) scale = 1.0f;
    RenderCanvas rc = { canvas, w, h, fnt };
    for (int k = 0; k < dl->n; k++)
        draw_op(&rc, &dl->op[k], fnt, tex, tex_n, cx, cy, cw, ch, scale);
}

void render_full(uint32_t *canvas, int w, int h, FontCtx *fnt, const DrawList *dl,
                 const BottomTex *tex, int tex_n, float scale)
{
    if (!canvas) return;
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++) canvas[(size_t)yy * w + xx] = 0xff000000u;
    render_ops(canvas, w, h, fnt, dl, tex, tex_n, scale, 0, 0, w, h);
}
