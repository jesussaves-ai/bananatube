/* view.h -- what the bottom screen is showing, and everything the UI needs to draw it.
 *
 * The state is plain data and the drawing happens once per frame from it, so ui_key() and the geometry can be
 * tested without a display, and main.c only has to own the workers and the panels.
 */
#ifndef BANANATUBE_VIEW_H
#define BANANATUBE_VIEW_H

#include "display/drawlist.h"
#include "input/keyboard.h"
#include "history.h"
#include "net/backend.h"
#include "net/thumbnail.h"
#include "ui/results.h"
#include "ui/settings.h"
#include "ui/theme.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    VIEW_RESULTS = 0,        /* the grid, with the search bar above it */
    VIEW_KEYBOARD,           /* the keyboard over the grid, while a query is typed */
    VIEW_PLAYING,            /* a video is on the top panel: this screen says what and controls it */
    VIEW_HISTORY,            /* what has been watched, to carry on with one */
    VIEW_SETTINGS,           /* the app's own settings: backend, quality, decoder, ... */
} View;

/* What the top panel is doing, as the bottom screen needs to show it. */
typedef struct {
    char   title[TITLE_LEN];
    char   channel[CHANNEL_LEN];
    double position, duration;
    int    width, height;
    bool   paused, buffering, active;
    bool   video_on;         /* off while the lid is shut: the sound carries on without it */
    int    volume;           /* 0..130, what X/Y and the on-screen buttons move */
    int    quality;          /* the height being asked for: 360, 480 or 720 */
    int    dropped;
    char   status[96];       /* why it stopped, or "" */
} Playing;

/* The transport row's buttons, so a tap can find them. */
typedef enum {
    TP_NONE = 0, TP_PREV, TP_PAUSE, TP_NEXT, TP_VOL_DOWN, TP_VOL_UP, TP_QUALITY, TP_STOP,
} TransportButton;

/* The row's geometry: seven buttons across the screen at TP_Y. One place, so a tap and the drawing cannot
 * disagree -- they did once, for the result tiles. */
#define TP_COUNT 7
#define TP_Y     244
#define TP_H     42
void          ui_transport_box(int i, int *x, int *w);
TransportButton ui_transport_hit(int px, int py);

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
    UI_SEEK_BACK,            /* L2 or LEFT while playing */
    UI_SEEK_FORWARD,         /* R2 or RIGHT */
    UI_VOLUME_UP,            /* UP */
    UI_VOLUME_DOWN,          /* DOWN */
    UI_QUALITY,              /* X: 360p -> 480p -> 720p, re-resolved */
    UI_NEXT_VIDEO,           /* R1 */
    UI_PREV_VIDEO,           /* L1 */
    UI_SAVE_PROGRESS,        /* the history wants writing */
    UI_HISTORY,              /* SELECT on the results: the watched list */
    UI_OPEN_SETTINGS,        /* START on the results: the app's own settings */
    UI_SETTINGS_SAVE,        /* a setting changed: bananatube.conf wants saving and applying */
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
    SettingsView settings;       /* the app's own settings screen (VIEW_SETTINGS) */
    const History *history;      /* the list the history view shows; owned by main.c */
    int      history_sel;
    int      history_scroll;

    int      view_x, view_y, view_w, view_h;   /* where the grid is drawn (the keyboard shrinks it) */
    float    spin;                             /* the spinner's phase, turned by main.c while a search runs */
    bool     dirty;
} AppState;

void      ui_init(AppState *st);
UiAction  ui_key(AppState *st, BananaKey key);
/* Two panels, two lists. The top one carries the content (the search field, the results, the video); the
 * bottom one carries the controls (the keyboard, the transport row, and what is selected). Each clears the
 * list it is given. */
void      ui_draw_top(AppState *st, DrawList *dl, const BottomTex *tex, int tex_n);
void      ui_draw_bot(AppState *st, DrawList *dl, const BottomTex *tex, int tex_n);
/* main.c tells the UI what happened, so it can say so on screen. */
void      ui_set_results(AppState *st, const SearchResult *items, int n, const char *message);
void      ui_set_status(AppState *st, const char *message);
/* The video started: the bottom screen becomes the player's. */
void      ui_set_playing(AppState *st, const char *title, const char *channel);
void      ui_set_playing_state(AppState *st, double position, double duration, bool paused, bool buffering,
                               int width, int height);
void      ui_set_full_state(AppState *st, int volume, int quality, bool video_on, int dropped);
void      ui_set_history(AppState *st, const History *h);
void      ui_set_playing_stopped(AppState *st, const char *why);
/* Open the app's own settings (START on the results). The Conf is main.c's; the view changes it in place and
 * reports UI_SETTINGS_SAVE when it does, so main.c can write the file and apply the change. */
void      ui_open_settings(AppState *st, Conf *conf);

#endif /* BANANATUBE_VIEW_H */
