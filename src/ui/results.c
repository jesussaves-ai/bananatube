/* results.c -- the rail of results: where each card is, which are on screen, and drawing them. */
#include "ui/results.h"
#include "ui/widget.h"
#include "util.h"

#include <stdio.h>
#include <string.h>

#define CARD_PAD     4
#define CARD_TITLE_H 32
#define CARD_META_H  16

void results_clear(Results *r)
{
    memset(r, 0, sizeof *r);
    for (int i = 0; i < RESULTS_MAX; i++) r->tex_slot[i] = IMAGE_NONE;
    r->sel = 0;
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

int results_card_w(const Results *r, int i)
{
    return i == r->sel ? RAIL_CARD_W * 3 / 2 : RAIL_CARD_W;
}

void results_card_box(const Results *r, int i, int x, int y, int w, int *bx, int *by, int *bw, int *bh)
{
    /* The selection is centred and every other card is placed from it, so a card's position depends on where
     * the selection is -- there is no scroll offset and nothing to keep. Walk out from the selection in
     * whichever direction i is. */
    int cur = x + (w - results_card_w(r, r->sel)) / 2;
    if (i < r->sel) {
        for (int k = r->sel - 1; k >= i; k--) cur -= RAIL_GAP + results_card_w(r, k);
    } else {
        for (int k = r->sel; k < i; k++) cur += RAIL_GAP + results_card_w(r, k);
    }
    if (bx) *bx = cur;
    if (by) *by = y;
    if (bw) *bw = results_card_w(r, i);
    if (bh) *bh = RAIL_CARD_H;
}

int results_visible(const Results *r, int x, int w, int *out, int max)
{
    int n = 0;
    for (int i = 0; i < r->count; i++) {
        int bx, bw;
        results_card_box(r, i, x, 0, w, &bx, NULL, &bw, NULL);
        if (bx + bw <= x || bx >= x + w) continue;      /* off to one side of the screen */
        if (n < max) out[n++] = i;
    }
    return n;
}

bool results_move(Results *r, BananaKey key)
{
    if (r->count == 0) return false;
    int before = r->sel;
    switch (key) {
    case BK_LEFT:  r->sel -= 1; break;
    case BK_RIGHT: r->sel += 1; break;
    case BK_UP:    r->sel -= RAIL_PAGE; break;
    case BK_DOWN:  r->sel += RAIL_PAGE; break;
    default:       return false;
    }
    if (r->sel < 0) r->sel = 0;
    if (r->sel >= r->count) r->sel = r->count - 1;
    return r->sel != before;
}

bool results_step(Results *r, int delta)
{
    if (!r || r->count <= 0) return false;
    int want = r->sel + delta;
    if (want < 0) want = 0;
    if (want >= r->count) want = r->count - 1;
    if (want == r->sel) return false;
    r->sel = want;
    return true;
}

void results_draw(DrawList *dl, const Results *r, const UiTheme *t, const BottomTex *tex, int tex_n,
                  int x, int y, int w, int h)
{
    if (r->count == 0) return;

    int vis[RESULTS_MAX];
    int n = results_visible(r, x, w, vis, RESULTS_MAX);

    /* the row is clipped to its viewport, so a card leaving the screen slides out of it */
    dl_push_clip(dl, x, y, w, h);
    for (int k = 0; k < n; k++) {
        int i = vis[k];
        const SearchResult *s = &r->item[i];
        bool on = (i == r->sel);
        int bx, by, bw, bh;
        results_card_box(r, i, x, y, w, &bx, &by, &bw, &bh);

        /* Each card is clipped to itself. A channel name that did not fit used to run past the card's edge,
         * be cut mid-glyph, and land inside the neighbour. */
        dl_push_clip(dl, bx, by, bw, bh);

        if (on) {
            /* The focused card is the lighter one inside a 2px accent rim. (It used to get a 3px accent bar
             * across its top edge as well, which read as a tab stuck onto the card.) */
            dl_rect_rounded(dl, bx, by, bw, bh, W_RADIUS_CARD, t->accent);
            dl_rect_rounded(dl, bx + 2, by + 2, bw - 4, bh - 4, W_RADIUS_CARD - 2, t->panel_hi);
        } else {
            widget_panel(dl, t, bx, by, bw, bh, W_RADIUS_CARD);
        }

        /* The picture takes all the height the text does not: worth it, because the cards are sized for the
         * focused one and a narrower card's 16:9 picture left a dead band under its title. */
        int iw = bw - CARD_PAD * 2;
        int ih = bh - CARD_PAD * 2 - CARD_TITLE_H - CARD_META_H - 8;
        int slot = r->tex_slot[i];
        int has = (slot >= 0 && slot < tex_n && tex && tex[slot].pixels) ? 1 : 0;
        dl_image(dl, bx + CARD_PAD, by + CARD_PAD, iw, ih, has ? slot : IMAGE_NONE,
                 FIT_COVER, has ? t->panel : t->bg);

        if (!has) {
            widget_tiny(dl, t, bx + CARD_PAD, by + CARD_PAD, iw, ih, "loading...", ALIGN_CENTER);
        }

        /* the duration on the picture's corner, as every video site does it */
        if (s->live) {
            dl_rect_rounded(dl, bx + bw - CARD_PAD - 38, by + CARD_PAD + ih - 18, 38, 16, W_RADIUS_CHIP, t->warn);
            dl_text(dl, bx + bw - CARD_PAD - 38, by + CARD_PAD + ih - 18, 38, 16, "LIVE", FONT_UI_BOLD,
                    W_SIZE_SMALL, t->ink, ALIGN_CENTER, VALIGN_MIDDLE);
        } else if (s->duration_sec >= 0) {
            char d[16];
            fmt_duration(s->duration_sec, d, sizeof d);
            dl_rect_rounded(dl, bx + bw - CARD_PAD - 44, by + CARD_PAD + ih - 18, 44, 16, W_RADIUS_CHIP, 0xc0000000);
            dl_text(dl, bx + bw - CARD_PAD - 44, by + CARD_PAD + ih - 18, 44, 16, d, FONT_UI, W_SIZE_SMALL,
                    t->ink, ALIGN_CENTER, VALIGN_MIDDLE);
        }

        dl_text_wrapped(dl, bx + CARD_PAD + 2, by + CARD_PAD + ih + CARD_PAD, bw - CARD_PAD * 2 - 4,
                        CARD_TITLE_H, s->title, FONT_UI_BOLD, W_SIZE_TITLE, t->ink,
                        ALIGN_LEFT, VALIGN_TOP, 2, 15);

        char views[24];
        fmt_views(s->views, views, sizeof views);
        char meta[128];
        if (views[0]) snprintf(meta, sizeof meta, "%s \xC2\xB7 %s views", s->channel, views);
        else          snprintf(meta, sizeof meta, "%s", s->channel);
        widget_tiny(dl, t, bx + CARD_PAD + 2, by + bh - CARD_PAD - CARD_META_H, bw - CARD_PAD * 2 - 4,
                    CARD_META_H, meta, ALIGN_LEFT);
        dl_pop_clip(dl);
    }
    dl_pop_clip(dl);
}
