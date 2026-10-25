/* drawlist.c -- building a frame, and working out what changed since the last one. */
#include "display/drawlist.h"
#include "util.h"

#include <string.h>

/* ---- damage ------------------------------------------------------------------------------------------ */

void damage_reset(Damage *d)
{
    d->n = 0;
}

bool damage_empty(const Damage *d)
{
    return d->n == 0;
}

/* A rectangle swallowed by one already in the list adds nothing. */
static bool damage_covered(const Damage *d, int x, int y, int w, int h)
{
    for (int i = 0; i < d->n; i++) {
        const int ax = d->r[i].x, ay = d->r[i].y;
        if (x >= ax && y >= ay && x + w <= ax + d->r[i].w && y + h <= ay + d->r[i].h) return true;
    }
    return false;
}

static bool rects_intersect(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

void damage_add(Damage *d, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    if (damage_covered(d, x, y, w, h)) return;

    /* Merge with anything it touches, growing in place, then retry: merging can make one rectangle cover
     * another, and the list is tiny. */
    for (int i = 0; i < d->n; i++) {
        int *ax = &d->r[i].x, *ay = &d->r[i].y, *aw = &d->r[i].w, *ah = &d->r[i].h;
        if (!rects_intersect(x, y, w, h, *ax, *ay, *aw, *ah)) continue;
        int x0 = x < *ax ? x : *ax, y0 = y < *ay ? y : *ay;
        int x1 = x + w > *ax + *aw ? x + w : *ax + *aw;
        int y1 = y + h > *ay + *ah ? y + h : *ay + *ah;
        d->r[i].x = x0; d->r[i].y = y0; d->r[i].w = x1 - x0; d->r[i].h = y1 - y0;
        if (damage_covered(d, x0, y0, d->r[i].w, d->r[i].h)) return;
    }

    if (d->n < MAX_DAMAGE) {
        d->r[d->n].x = x; d->r[d->n].y = y; d->r[d->n].w = w; d->r[d->n].h = h;
        d->n++;
        return;
    }
    /* More separate places than we care to track. Clear one bounding rectangle instead: on the bottom
     * screen a full repaint is a 1.2 MB memcpy, so this is the cheaper mistake. */
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    for (int i = 0; i < d->n; i++) {
        if (d->r[i].x < x0) x0 = d->r[i].x;
        if (d->r[i].y < y0) y0 = d->r[i].y;
        if (d->r[i].x + d->r[i].w > x1) x1 = d->r[i].x + d->r[i].w;
        if (d->r[i].y + d->r[i].h > y1) y1 = d->r[i].y + d->r[i].h;
    }
    d->n = 1;
    d->r[0].x = x0; d->r[0].y = y0; d->r[0].w = x1 - x0; d->r[0].h = y1 - y0;
}

void damage_bounds(const Damage *d, int *x, int *y, int *w, int *h)
{
    if (d->n == 0) { *x = *y = 0; *w = *h = 0; return; }
    int x0 = d->r[0].x, y0 = d->r[0].y, x1 = x0 + d->r[0].w, y1 = y0 + d->r[0].h;
    for (int i = 1; i < d->n; i++) {
        if (d->r[i].x < x0) x0 = d->r[i].x;
        if (d->r[i].y < y0) y0 = d->r[i].y;
        if (d->r[i].x + d->r[i].w > x1) x1 = d->r[i].x + d->r[i].w;
        if (d->r[i].y + d->r[i].h > y1) y1 = d->r[i].y + d->r[i].h;
    }
    *x = x0; *y = y0; *w = x1 - x0; *h = y1 - y0;
}

bool damage_touches(const Damage *d, int x, int y, int w, int h)
{
    for (int i = 0; i < d->n; i++)
        if (rects_intersect(x, y, w, h, d->r[i].x, d->r[i].y, d->r[i].w, d->r[i].h)) return true;
    return false;
}

/* ---- ops --------------------------------------------------------------------------------------------- */

void op_bounds(const DrawOp *op, int pad, int *x, int *y, int *w, int *h)
{
    *x = op->x - pad;
    *y = op->y - pad;
    *w = op->w + pad * 2;
    *h = op->h + pad * 2;
}

bool op_equal(const DrawOp *a, const DrawOp *b)
{
    if (a->kind != b->kind) return false;
    if (a->x != b->x || a->y != b->y || a->w != b->w || a->h != b->h) return false;
    if (a->clip_x != b->clip_x || a->clip_y != b->clip_y ||
        a->clip_w != b->clip_w || a->clip_h != b->clip_h) return false;
    if (a->color != b->color) return false;

    switch (a->kind) {
    case OP_RECT:
        return a->radius == b->radius && a->border == b->border && a->border_color == b->border_color;
    case OP_TEXT:
        return a->font == b->font && a->size == b->size && a->align == b->align &&
               a->valign == b->valign && strcmp(a->text, b->text) == 0;
    case OP_IMAGE:
        return a->image == b->image && a->fit == b->fit && a->tint == b->tint;
    }
    return false;
}

/* ---- the list ---------------------------------------------------------------------------------------- */

void dl_reset(DrawList *dl)
{
    dl->n = 0;
    dl->depth = 0;
    dl->cx = 0; dl->cy = 0; dl->cw = SCREEN_W; dl->ch = SCREEN_H;
}

void dl_push_clip(DrawList *dl, int x, int y, int w, int h)
{
    if (dl->depth < (int)(sizeof dl->scx / sizeof dl->scx[0])) {
        dl->scx[dl->depth] = dl->cx; dl->scy[dl->depth] = dl->cy;
        dl->scw[dl->depth] = dl->cw; dl->sch[dl->depth] = dl->ch;
        dl->depth++;
    }
    /* the intersection of the current clip and the new one */
    int x0 = x > dl->cx ? x : dl->cx, y0 = y > dl->cy ? y : dl->cy;
    int x1 = x + w < dl->cx + dl->cw ? x + w : dl->cx + dl->cw;
    int y1 = y + h < dl->cy + dl->ch ? y + h : dl->cy + dl->ch;
    dl->cx = x0; dl->cy = y0;
    dl->cw = x1 > x0 ? x1 - x0 : 0;
    dl->ch = y1 > y0 ? y1 - y0 : 0;
}

void dl_pop_clip(DrawList *dl)
{
    if (dl->depth <= 0) return;
    dl->depth--;
    dl->cx = dl->scx[dl->depth]; dl->cy = dl->scy[dl->depth];
    dl->cw = dl->scw[dl->depth]; dl->ch = dl->sch[dl->depth];
}

DrawOp *dl_push(DrawList *dl, OpKind kind)
{
    if (dl->n >= DRAW_MAX) return NULL;
    DrawOp *op = &dl->op[dl->n++];
    memset(op, 0, sizeof *op);
    op->kind = kind;
    op->clip_x = dl->cx; op->clip_y = dl->cy; op->clip_w = dl->cw; op->clip_h = dl->ch;
    op->tint = 0xffffffffu;
    op->image = IMAGE_NONE;
    op->fit = FIT_COVER;
    return op;
}

DrawOp *dl_rect(DrawList *dl, int x, int y, int w, int h, uint32_t color)
{
    DrawOp *op = dl_push(dl, OP_RECT);
    if (!op) return NULL;
    op->x = x; op->y = y; op->w = w; op->h = h;
    op->color = color;
    return op;
}

DrawOp *dl_rect_rounded(DrawList *dl, int x, int y, int w, int h, int radius, uint32_t color)
{
    DrawOp *op = dl_rect(dl, x, y, w, h, color);
    if (op) op->radius = radius;
    return op;
}

DrawOp *dl_rect_outlined(DrawList *dl, int x, int y, int w, int h, int radius,
                         uint32_t color, int border, uint32_t border_color)
{
    DrawOp *op = dl_rect_rounded(dl, x, y, w, h, radius, color);
    if (op) { op->border = border; op->border_color = border_color; }
    return op;
}

DrawOp *dl_text(DrawList *dl, int x, int y, int w, int h, const char *text, FontId font, int size,
                uint32_t color, HAlign align, VAlign valign)
{
    DrawOp *op = dl_push(dl, OP_TEXT);
    if (!op) return NULL;
    op->x = x; op->y = y; op->w = w; op->h = h;
    op->font = font; op->size = size;
    op->color = color;
    op->align = align; op->valign = valign;
    str_copy(op->text, sizeof op->text, text);
    return op;
}

DrawOp *dl_image(DrawList *dl, int x, int y, int w, int h, int image, Fit fit, uint32_t placeholder)
{
    DrawOp *op = dl_push(dl, OP_IMAGE);
    if (!op) return NULL;
    op->x = x; op->y = y; op->w = w; op->h = h;
    op->image = image; op->fit = fit;
    op->color = placeholder;      /* what to paint when the texture is not there yet */
    return op;
}

/* ---- the damage between two frames ------------------------------------------------------------------- */

#define LOOKAHEAD 3

static void damage_op(Damage *out, const DrawOp *op)
{
    damage_add(out, op->x, op->y, op->w, op->h);
}

void dl_diff(const DrawList *prev, const DrawList *cur, Damage *out)
{
    damage_reset(out);
    if (!prev || prev->n == 0) {
        for (int k = 0; k < cur->n; k++) damage_op(out, &cur->op[k]);
        return;
    }

    int i = 0, j = 0;
    while (i < prev->n && j < cur->n) {
        if (op_equal(&prev->op[i], &cur->op[j])) { i++; j++; continue; }

        /* Resynchronise: the nearest pair within LOOKAHEAD that matches again says whether an op came, went
         * or merely changed. */
        int best = 2 * LOOKAHEAD + 1, ba = -1, bb = -1;
        for (int a = 0; a <= LOOKAHEAD && i + a < prev->n; a++) {
            for (int b = 0; b <= LOOKAHEAD && j + b < cur->n; b++) {
                if (a == 0 && b == 0) continue;
                if (op_equal(&prev->op[i + a], &cur->op[j + b]) && a + b < best) {
                    best = a + b; ba = a; bb = b;
                }
            }
        }
        if (ba < 0) break;                       /* no resync in reach: the tails below are all damage */

        for (int k = 0; k < ba; k++) damage_op(out, &prev->op[i + k]);
        for (int k = 0; k < bb; k++) damage_op(out, &cur->op[j + k]);
        i += ba; j += bb;
    }

    for (int k = i; k < prev->n; k++) damage_op(out, &prev->op[k]);
    for (int k = j; k < cur->n; k++) damage_op(out, &cur->op[k]);
}
