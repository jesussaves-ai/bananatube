/* results.c -- the grid of results: where each tile is, which are on screen, and drawing them. */
#include "ui/results.h"
#include "ui/widget.h"
#include "util.h"

#include <stdio.h>
#include <string.h>

#define TILE_THUMB_H 109   /* the picture inside a tile: 194x109 is 16:9 */
#define TITLE_Y 113
#define TITLE_H 32
#define META_Y 145
#define META_H 16

void results_clear(Results *r)
{
    memset(r, 0, sizeof *r);
    for (int i = 0; i < RESULTS_MAX; i++) r->tex_slot[i] = IMAGE_NONE;
    r->sel = 0;
    r->top_row = 0;
}

int results_append(Results *r, const SearchResult *items, int n)
{
    int added = 0;
    for (int i = 0; i < n && r->count < RESULTS_MAX; i++, added++) {
        r->item[r->count] = items[i];
        r->tex_slot[r->count] = IMAGE_NONE;
        r->count++;
    }
    return added;
}

void results_set_thumb(Results *r, const char *video_id, int slot)
{
    for (int i = 0; i < r->count; i++)
        if (strcmp(r->item[i].id, video_id) == 0) { r->tex_slot[i] = slot; return; }
}

int results_rows(const Results *r)
{
    return (r->count + GRID_COLS - 1) / GRID_COLS;
}

int results_visible_rows(int view_h)
{
    int step = TILE_H + TILE_GAP;
    int rows = (view_h + TILE_GAP) / step;
    return rows < 1 ? 1 : rows;
}

bool results_move(Results *r, BananaKey key)
{
    if (r->count == 0) return false;
    int before = r->sel;
    switch (key) {
    case BK_LEFT:  if (r->sel % GRID_COLS > 0) r->sel--; break;
    case BK_RIGHT: if (r->sel % GRID_COLS < GRID_COLS - 1 && r->sel + 1 < r->count) r->sel++; break;
    case BK_UP:    if (r->sel - GRID_COLS >= 0) r->sel -= GRID_COLS; break;
    case BK_DOWN: {
        if (r->sel + GRID_COLS < r->count) r->sel += GRID_COLS;
        else if (r->sel / GRID_COLS + 1 < results_rows(r)) r->sel = r->count - 1;  /* the last row is short */
        else return false;
        break;
    }
    default:       return false;
    }
    return r->sel != before;
}

void results_scroll_into_view(Results *r, int view_h)
{
    r->view_h = view_h;
    int rows = results_visible_rows(view_h);
    int sel_row = r->sel / GRID_COLS;
    if (sel_row < r->top_row) r->top_row = sel_row;
    if (sel_row >= r->top_row + rows) r->top_row = sel_row - rows + 1;
    int max_top = results_rows(r) - rows;
    if (max_top < 0) max_top = 0;
    if (r->top_row > max_top) r->top_row = max_top;
    if (r->top_row < 0) r->top_row = 0;
}

void results_tile_box(int i, int top_row, int x, int y, int w, int *bx, int *by, int *bw, int *bh)
{
    int row = i / GRID_COLS, col = i % GRID_COLS;
    /* the columns share the width out evenly, so a wider screen does not leave a gap at the right */
    int cols_total = GRID_COLS * TILE_W + (GRID_COLS - 1) * TILE_GAP;
    int left = x + (w - cols_total) / 2;
    if (bx) *bx = left + col * (TILE_W + TILE_GAP);
    if (by) *by = y + (row - top_row) * (TILE_H + TILE_GAP);
    if (bw) *bw = TILE_W;
    if (bh) *bh = TILE_H;
}

int results_visible(const Results *r, int view_h, int *out, int max)
{
    int rows = results_visible_rows(view_h);
    int n = 0;
    for (int row = r->top_row; row < r->top_row + rows; row++) {
        for (int col = 0; col < GRID_COLS; col++) {
            int i = row * GRID_COLS + col;
            if (i >= r->count) break;
            if (n < max) out[n++] = i;
        }
    }
    return n;
}

int results_hit(const Results *r, int view_h, int x, int y, int w, int px, int py)
{
    int vis[RESULTS_MAX];
    int n = results_visible(r, view_h, vis, RESULTS_MAX);
    for (int k = 0; k < n; k++) {
        int bx, by, bw, bh;
        results_tile_box(vis[k], r->top_row, x, y, w, &bx, &by, &bw, &bh);
        if (px >= bx && px < bx + bw && py >= by && py < by + bh) return vis[k];
    }
    return -1;
}

void results_draw(DrawList *dl, const Results *r, const UiTheme *t, const BottomTex *tex, int tex_n,
                  int x, int y, int w, int h)
{
    if (r->count == 0) return;

    int vis[RESULTS_MAX];
    int n = results_visible(r, h, vis, RESULTS_MAX);

    /* scrolled so a tile slides out from under the header rather than over it */
    dl_push_clip(dl, x, y, w, h);
    for (int k = 0; k < n; k++) {
        int i = vis[k];
        const SearchResult *s = &r->item[i];
        bool on = (i == r->sel);
        int bx, by, bw, bh;
        results_tile_box(i, r->top_row, x, y, w, &bx, &by, &bw, &bh);

        dl_rect_rounded(dl, bx, by, bw, bh, 8, on ? t->panel_hi : t->panel);
        if (on) dl_rect_rounded(dl, bx, by, bw, 3, 2, t->accent);          /* the selection's rim */

        int slot = r->tex_slot[i];
        int has = (slot >= 0 && slot < tex_n && tex && tex[slot].pixels) ? 1 : 0;
        dl_image(dl, bx + 4, by + 4, bw - 8, TILE_THUMB_H, has ? slot : IMAGE_NONE,
                 FIT_COVER, has ? t->panel : t->bg);

        if (!has) {
            widget_small(dl, t, bx + 4, by + 4, bw - 8, TILE_THUMB_H, "loading...", ALIGN_CENTER);
        }

        /* the duration on the picture's corner, as every video site does it */
        if (s->live) {
            dl_rect_rounded(dl, bx + bw - 46, by + TILE_THUMB_H - 14, 38, 16, 4, t->warn);
            dl_text(dl, bx + bw - 46, by + TILE_THUMB_H - 14, 38, 16, "LIVE", FONT_UI_BOLD, 11,
                    t->ink, ALIGN_CENTER, VALIGN_MIDDLE);
        } else if (s->duration_sec >= 0) {
            char d[16];
            fmt_duration(s->duration_sec, d, sizeof d);
            dl_rect_rounded(dl, bx + bw - 52, by + TILE_THUMB_H - 14, 44, 16, 4, 0xc0000000);
            dl_text(dl, bx + bw - 52, by + TILE_THUMB_H - 14, 44, 16, d, FONT_UI, 11,
                    t->ink, ALIGN_CENTER, VALIGN_MIDDLE);
        }

        dl_text_wrapped(dl, bx + 6, by + TITLE_Y, bw - 12, TITLE_H, s->title, FONT_UI_BOLD, 13,
                        on ? t->ink : t->ink, ALIGN_LEFT, VALIGN_TOP, 2, 15);

        char views[24];
        fmt_views(s->views, views, sizeof views);
        char meta[128];
        if (views[0]) snprintf(meta, sizeof meta, "%s\xC2\xB7 %s views", s->channel, views);
        else          snprintf(meta, sizeof meta, "%s", s->channel);
        widget_small(dl, t, bx + 6, by + META_Y, bw - 12, META_H, meta, ALIGN_LEFT);
    }
    dl_pop_clip(dl);
}
