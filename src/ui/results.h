/* results.h -- the grid of search results: the data, where the selection is, and where the tiles go.
 *
 * All geometry: given a viewport it says which tiles are on screen and where each one is, so the drawing and
 * the tests agree about it.
 */
#ifndef BANANATUBE_RESULTS_H
#define BANANATUBE_RESULTS_H

#include "display/drawlist.h"
#include "input/keys.h"
#include "net/backend.h"
#include "net/thumbnail.h"
#include "ui/theme.h"

#include <stdbool.h>

#define RESULTS_MAX  SEARCH_RESULTS_MAX
#define GRID_COLS    3
#define TILE_W       194
#define TILE_H       161
#define TILE_GAP     10

typedef struct {
    SearchResult item[RESULTS_MAX];
    int          tex_slot[RESULTS_MAX];   /* the picture's slot in the caller's table, or IMAGE_NONE */
    int          count;
    int          sel;          /* the selected result */
    int          top_row;      /* the first row drawn: the selection is scrolled into view */
    int          view_h;       /* the height the grid last drew into */
} Results;

void results_clear(Results *r);
int  results_append(Results *r, const SearchResult *items, int n);

int  results_rows(const Results *r);
int  results_visible_rows(int view_h);
/* Moves the selection with the D-pad: across a row, up and down whole rows, without wrapping off the ends. */
bool results_move(Results *r, BananaKey key);
/* The row the selection is on, scrolled to be one of the visible ones. */
void results_scroll_into_view(Results *r, int view_h);

/* The tiles that are on screen, in draw order. Fills `out` with their indices and returns how many. */
int  results_visible(const Results *r, int view_h, int *out, int max);

/* The box of the tile at index `i`, in screen units, for the viewport (x, y, w). */
void results_tile_box(int i, int top_row, int x, int y, int w, int *bx, int *by, int *bw, int *bh);
/* The tile under a tap, or -1. The viewport is the same (x, y, w, view_h) the grid was drawn with. */
int  results_hit(const Results *r, int view_h, int x, int y, int w, int px, int py);

/* main.c says which picture slot a video's thumbnail landed in. */
void results_set_thumb(Results *r, const char *video_id, int slot);

void results_draw(DrawList *dl, const Results *r, const UiTheme *t, const BottomTex *tex, int tex_n,
                  int x, int y, int w, int h);

#endif /* BANANATUBE_RESULTS_H */
