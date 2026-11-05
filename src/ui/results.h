/* results.h -- the results a search found, and where each one sits on the screen.
 *
 * One row of cards, the focused one wider, pushed left and right. The row lives on the top panel, which has
 * no touchscreen, so there is no hit-test and nothing to scroll: a card's box follows from where the
 * selection is, which is why the box asks for the whole Results.
 */
#ifndef BANANATUBE_RESULTS_H
#define BANANATUBE_RESULTS_H

#include "display/drawlist.h"
#include "net/backend.h"
#include "net/thumbnail.h"
#include "ui/theme.h"
#include "input/keys.h"

#include <stdbool.h>

#define RESULTS_MAX SEARCH_RESULTS_MAX

#define RAIL_CARD_W 150
#define RAIL_CARD_H 184          /* the widest card's 16:9 picture, a two-line title and the meta line */
#define RAIL_GAP    12
#define RAIL_PAGE   3            /* a page is one card more than the screen holds */

typedef struct {
    SearchResult item[RESULTS_MAX];
    int          tex_slot[RESULTS_MAX];   /* the picture slot, or IMAGE_NONE */
    int          count;
    int          sel;
} Results;

void results_clear(Results *r);
int  results_append(Results *r, const SearchResult *items, int n);

/* The focused card is half again as wide as the rest. */
int  results_card_w(const Results *r, int i);

/* Where card i sits: the selection is centred, the cards before it go out to its left and the ones after it
 * to its right, RAIL_GAP apart. */
void results_card_box(const Results *r, int i, int x, int y, int w, int *bx, int *by, int *bw, int *bh);

/* The cards whose box meets [x, x+w), in order, up to max of them. */
int  results_visible(const Results *r, int x, int w, int *out, int max);

/* LEFT and RIGHT one card, UP and DOWN a page. False when the selection did not move. */
bool results_move(Results *r, BananaKey key);
bool results_step(Results *r, int delta);

void results_set_thumb(Results *r, const char *video_id, int slot);

void results_draw(DrawList *dl, const Results *r, const UiTheme *t, const BottomTex *tex, int tex_n,
                  int x, int y, int w, int h);

#endif /* BANANATUBE_RESULTS_H */
