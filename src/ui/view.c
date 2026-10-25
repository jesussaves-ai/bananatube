/* view.c -- the bottom screen: the results view, the keyboard over it, and what a button means. */
#include "ui/view.h"
#include "ui/widget.h"
#include "util.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define HEADER_H   44
#define SEARCH_Y   48
#define SEARCH_H   34
#define GRID_Y     88
#define HINTS_H    30

static void layout(AppState *st)
{
    st->view_x = 8;
    st->view_y = GRID_Y;
    st->view_w = SCREEN_W - 16;
    int bottom = SCREEN_H - HINTS_H;
    if (st->view == VIEW_KEYBOARD) bottom -= kb_height();     /* one row of tiles still fits */
    st->view_h = bottom - GRID_Y;
    if (st->view_h < TILE_H) st->view_h = TILE_H;
    st->results.view_h = st->view_h;
}

void ui_init(AppState *st)
{
    memset(st, 0, sizeof *st);
    st->theme = ui_theme();
    st->view = VIEW_RESULTS;
    results_clear(&st->results);
    kb_reset(&st->kb);
    str_copy(st->status, sizeof st->status, "SELECT searches");
    layout(st);
    st->dirty = true;
}

void ui_set_status(AppState *st, const char *message)
{
    str_copy(st->status, sizeof st->status, message ? message : "");
    st->searching = false;
    st->dirty = true;
}

void ui_set_results(AppState *st, const SearchResult *items, int n, const char *message)
{
    results_clear(&st->results);
    if (n > 0) results_append(&st->results, items, n);
    results_scroll_into_view(&st->results, st->view_h);
    st->searching = false;
    ui_set_status(st, message ? message : (n > 0 ? "" : "Nothing found."));
    if (!message || !*message) {
        char m[64];
        snprintf(m, sizeof m, "%d result%s", n, n == 1 ? "" : "s");
        if (n == 0) str_copy(st->status, sizeof st->status, m);
        else        snprintf(st->status, sizeof st->status, "%s \xC2\xB7 A plays \xC2\xB7 SELECT searches again", m);
    }
    st->dirty = true;
}

static void query_backspace(AppState *st)
{
    size_t n = strlen(st->query);
    if (!n) return;
    /* do not leave half a multi-byte character behind */
    n--;
    while (n > 0 && ((unsigned char)st->query[n] & 0xc0) == 0x80) n--;
    st->query[n] = '\0';
    st->dirty = true;
}

static void query_add(AppState *st, const char *text)
{
    size_t n = strlen(st->query), add = strlen(text);
    if (n + add + 1 > sizeof st->query) return;
    memcpy(st->query + n, text, add + 1);
    st->dirty = true;
}

UiAction ui_key(AppState *st, BananaKey key)
{
    if (key == BK_HOME) return UI_QUIT;

    if (st->view == VIEW_KEYBOARD) {
        char ch[8] = "";
        KbAction a = kb_press(&st->kb, key, ch, sizeof ch);
        switch (a) {
        case KB_ACTION_CHAR:    query_add(st, ch); return UI_NONE;
        case KB_ACTION_SPACE:   query_add(st, " "); return UI_NONE;
        case KB_ACTION_DELETE:  query_backspace(st); return UI_NONE;
        case KB_ACTION_LAYER:
            st->dirty = true;              /* kb_press has already changed the layer */
            return UI_NONE;
        case KB_ACTION_SUBMIT:
            if (!st->query[0]) { ui_set_status(st, "Type something first."); return UI_NONE; }
            st->view = VIEW_RESULTS;              /* the keyboard goes: the results want the room */
            st->searching = true;
            str_copy(st->status, sizeof st->status, "Searching\xE2\x80\xA6");
            layout(st);
            st->dirty = true;
            return UI_SEARCH;
        case KB_ACTION_CLOSE:
            st->view = VIEW_RESULTS;
            layout(st);
            st->dirty = true;
            return UI_NONE;
        default:
            if (a == KB_ACTION_NONE) {
                /* the D-pad only moves the cursor: nothing on screen changed unless it did */
                st->dirty = true;
                return UI_NONE;
            }
            return UI_NONE;
        }
    }

    /* the results view */
    switch (key) {
    case BK_SELECT:
        st->view = VIEW_KEYBOARD;
        kb_reset(&st->kb);
        layout(st);
        st->dirty = true;
        return UI_NONE;
    case BK_A:
        if (st->results.count) return UI_PLAY;
        st->view = VIEW_KEYBOARD;                 /* nothing to play: go and type */
        st->dirty = true;
        return UI_NONE;
    case BK_B:
        if (st->results.count) {
            results_clear(&st->results);
            ui_set_status(st, "SELECT searches");
            return UI_NONE;
        }
        return UI_QUIT;
    case BK_LEFT: case BK_RIGHT: case BK_UP: case BK_DOWN: {
        if (!results_move(&st->results, key)) return UI_NONE;
        results_scroll_into_view(&st->results, st->view_h);
        st->dirty = true;
        return UI_THUMBS;                         /* the visible set may have changed */
    }
    default:
        return UI_NONE;
    }
}

/* ---- drawing ------------------------------------------------------------------------------------------ */

static void draw_header(AppState *st, DrawList *dl)
{
    const UiTheme *t = &st->theme;
    dl_rect(dl, 0, 0, SCREEN_W, HEADER_H, t->bar);
    widget_title(dl, t, 16, 0, 400, HEADER_H, "YouTube", ALIGN_LEFT);

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char clock[16];
    snprintf(clock, sizeof clock, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    widget_mono(dl, t, SCREEN_W - 140, 0, 124, HEADER_H, clock, ALIGN_RIGHT);
}

static void draw_search_bar(AppState *st, DrawList *dl)
{
    const UiTheme *t = &st->theme;
    int x = 12, w = SCREEN_W - 24;
    widget_well(dl, t, x, SEARCH_Y, w, SEARCH_H, 8);

    if (st->query[0]) widget_label(dl, t, x + 10, SEARCH_Y, w - 20, SEARCH_H, st->query, ALIGN_LEFT);
    else              widget_small(dl, t, x + 10, SEARCH_Y, w - 20, SEARCH_H, "Search YouTube", ALIGN_LEFT);

    if (st->view == VIEW_KEYBOARD) {
        /* a caret after the text, so it is obvious the field is being typed into */
        char probe[QUERY_MAX + 4];
        snprintf(probe, sizeof probe, "%s|", st->query);
        int cw = 9 * (int)strlen(probe);
        if (cw < w - 20) dl_rect(dl, x + 10 + cw - 8, SEARCH_Y + 8, 2, SEARCH_H - 16, t->accent);
    }
    if (st->searching) widget_spinner(dl, t, x + w - 18, SEARCH_Y + SEARCH_H / 2, 7, st->spin);
}

static void draw_hints(AppState *st, DrawList *dl)
{
    const UiTheme *t = &st->theme;
    dl_rect(dl, 0, SCREEN_H - HINTS_H, SCREEN_W, HINTS_H, t->bar);
    int x = 10;
    int y = SCREEN_H - HINTS_H + (HINTS_H - 18) / 2;

    if (st->view == VIEW_KEYBOARD) {
        x += widget_hint(dl, t, x, y, BK_A, "type", true) + 12;
        x += widget_hint(dl, t, x, y, BK_X, "capitals", false) + 12;
        x += widget_hint(dl, t, x, y, BK_Y, "space", false) + 12;
        x += widget_hint(dl, t, x, y, BK_B, "delete", false) + 12;
        x += widget_hint(dl, t, x, y, BK_START, "search", true) + 12;
    } else {
        x += widget_hint(dl, t, x, y, BK_A, "play", true) + 12;
        x += widget_hint(dl, t, x, y, BK_SELECT, "search", true) + 12;
        x += widget_hint(dl, t, x, y, BK_B, "clear", false) + 12;
        x += widget_hint(dl, t, x, y, BK_HOME, "quit", false) + 12;
    }
    /* the status only where there is room for it: the keyboard's hints fill the row, and a message drawn
     * over them reads as neither */
    if (st->status[0] && st->view == VIEW_RESULTS)
        widget_small(dl, t, x + 8, y, SCREEN_W - x - 18, 18, st->status, ALIGN_RIGHT);
}

static void draw_empty(AppState *st, DrawList *dl)
{
    const UiTheme *t = &st->theme;
    int cy = st->view_y + st->view_h / 2;
    if (st->searching) {
        widget_spinner(dl, t, SCREEN_W / 2, cy - 10, 14, st->spin);
        widget_small(dl, t, 0, cy + 12, SCREEN_W, 20, "Searching\xE2\x80\xA6", ALIGN_CENTER);
        return;
    }
    widget_label(dl, t, 0, cy - 24, SCREEN_W, 24, "Press SELECT to search YouTube", ALIGN_CENTER);
    widget_small(dl, t, 0, cy + 4, SCREEN_W, 20,
                 "the on-screen keyboard works with the D-pad or by touch, as the menu's does", ALIGN_CENTER);
}

void ui_draw(AppState *st, DrawList *dl, const BottomTex *tex, int tex_n)
{
    const UiTheme *t = &st->theme;

    dl_rect(dl, 0, 0, SCREEN_W, SCREEN_H, t->bg);
    draw_header(st, dl);
    draw_search_bar(st, dl);

    if (st->results.count) {
        results_draw(dl, &st->results, t, tex, tex_n, st->view_x, st->view_y, st->view_w, st->view_h);
    } else {
        draw_empty(st, dl);
    }

    if (st->view == VIEW_KEYBOARD)
        kb_draw(dl, &st->kb, t, 0, GRID_Y, SCREEN_W, SCREEN_H - GRID_Y - HINTS_H);

    draw_hints(st, dl);
}
