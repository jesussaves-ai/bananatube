/* view.h -- what the bottom screen is showing, and everything the UI needs to draw it.
 *
 * The state is plain data and the drawing happens once per frame from it, so ui_key() and the geometry can be
 * tested without a display, and main.c only has to own the workers and the panels.
 */
#ifndef BANANATUBE_VIEW_H
#define BANANATUBE_VIEW_H

#include "display/drawlist.h"
#include "input/keyboard.h"
#include "net/backend.h"
#include "net/thumbnail.h"
#include "ui/results.h"
#include "ui/theme.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    VIEW_RESULTS = 0,        /* the grid, with the search bar above it */
    VIEW_KEYBOARD,           /* the keyboard over the grid, while a query is typed */
    VIEW_PLAYING,            /* a video is on the top panel: this screen says what and controls it */
} View;

/* What the top panel is doing, as the bottom screen needs to show it. */
typedef struct {
    char   title[TITLE_LEN];
    char   channel[CHANNEL_LEN];
    double position, duration;
    int    width, height;
    bool   paused, buffering, active;
    char   status[96];       /* why it stopped, or "" */
} Playing;

#define QUERY_MAX 96

/* What the app should do about a key: the UI decides what a button means, main.c does it. */
typedef enum {
    UI_NONE = 0,
    UI_SEARCH,               /* START on the keyboard: run st->query */
    UI_PLAY,                 /* A on a result: play the selected video */
    UI_QUIT,                 /* HOME */
    UI_THUMBS,               /* the visible set changed: fetch what is missing */
    UI_STOP,                 /* B while playing: back to the results */
    UI_PAUSE,                /* START while playing */
    UI_SEEK_BACK,            /* L2 */
    UI_SEEK_FORWARD,         /* R2 */
} UiAction;

typedef struct {
    View     view;
    UiTheme  theme;

    char     query[QUERY_MAX];
    bool     searching;          /* a search is in flight */
    char     status[128];        /* what the last search said, or why it failed */

    Results  results;
    Keyboard kb;
    Playing  playing;

    int      view_x, view_y, view_w, view_h;   /* where the grid is drawn (the keyboard shrinks it) */
    float    spin;                             /* the spinner's phase, turned by main.c while a search runs */
    bool     dirty;
} AppState;

void      ui_init(AppState *st);
UiAction  ui_key(AppState *st, BananaKey key);
void      ui_draw(AppState *st, DrawList *dl, const BottomTex *tex, int tex_n);
/* main.c tells the UI what happened, so it can say so on screen. */
void      ui_set_results(AppState *st, const SearchResult *items, int n, const char *message);
void      ui_set_status(AppState *st, const char *message);
/* The video started: the bottom screen becomes the player's. */
void      ui_set_playing(AppState *st, const char *title, const char *channel);
void      ui_set_playing_state(AppState *st, double position, double duration, bool paused, bool buffering,
                               int width, int height);
void      ui_set_playing_stopped(AppState *st, const char *why);

#endif /* BANANATUBE_VIEW_H */
