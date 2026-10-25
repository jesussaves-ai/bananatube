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

/* Where each transport button is. One place, so a tap and the drawing cannot disagree (they did, once, for the
 * result tiles: tests/test_ui.c checks both now). */
void ui_transport_box(int i, int *x, int *w)
{
    int margin = 10, gap = 6;
    int total = SCREEN_W - margin * 2;
    int spaces = total - gap * (TP_COUNT - 1);
    int each = spaces / TP_COUNT;
    int extra = spaces - each * TP_COUNT;      /* the remainder: the first button takes it, so the row is
                                                * flush at both ends rather than 3 px short of one */
    if (x) *x = margin + (i == 0 ? 0 : extra + each + gap + (i - 1) * (each + gap));
    if (w) *w = each + (i == 0 ? extra : 0);
}

/* Which button a tap at (px, py) landed on, or TP_NONE. */
TransportButton ui_transport_hit(int px, int py)
{
    static const TransportButton order[TP_COUNT] = { TP_PREV, TP_VOL_DOWN, TP_PAUSE, TP_VOL_UP,
                                                     TP_QUALITY, TP_NEXT, TP_STOP };
    if (py < TP_Y || py >= TP_Y + TP_H) return TP_NONE;
    for (int i = 0; i < TP_COUNT; i++) {
        int x, w;
        ui_transport_box(i, &x, &w);
        if (px >= x && px < x + w) return order[i];
    }
    return TP_NONE;
}

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

void ui_set_playing(AppState *st, const char *title, const char *channel)
{
    st->view = VIEW_PLAYING;
    str_copy(st->playing.title, sizeof st->playing.title, title ? title : "");
    str_copy(st->playing.channel, sizeof st->playing.channel, channel ? channel : "");
    st->playing.position = st->playing.duration = 0;
    st->playing.paused = false;
    st->playing.buffering = true;
    st->playing.active = true;
    st->playing.status[0] = '\0';
    layout(st);
    st->dirty = true;
}

void ui_set_playing_state(AppState *st, double position, double duration, bool paused, bool buffering,
                          int width, int height)
{
    Playing *pl = &st->playing;
    if (position != pl->position || paused != pl->paused || buffering != pl->buffering ||
        width != pl->width || height != pl->height) st->dirty = true;
    pl->position = position;
    pl->duration = duration;
    pl->paused = paused;
    pl->buffering = buffering;
    if (width > 0) pl->width = width;
    if (height > 0) pl->height = height;
}

void ui_set_full_state(AppState *st, int volume, int quality, bool video_on, int dropped)
{
    Playing *pl = &st->playing;
    if (pl->volume != volume || pl->quality != quality || pl->video_on != video_on || pl->dropped != dropped)
        st->dirty = true;
    pl->volume = volume;
    pl->quality = quality;
    pl->video_on = video_on;
    pl->dropped = dropped;
}

void ui_set_history(AppState *st, const History *h)
{
    st->history = h;
    st->dirty = true;
}

void ui_set_playing_stopped(AppState *st, const char *why)
{
    st->playing.active = false;
    str_copy(st->playing.status, sizeof st->playing.status, why ? why : "");
    st->view = VIEW_RESULTS;
    layout(st);
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

    if (st->view == VIEW_PLAYING) {
        switch (key) {
        case BK_B:      return UI_STOP;
        case BK_START:  return UI_PAUSE;
        case BK_A:      return UI_PAUSE;
        case BK_LEFT:   return UI_SEEK_BACK;
        case BK_RIGHT:  return UI_SEEK_FORWARD;
        case BK_L2:     return UI_SEEK_BACK;
        case BK_R2:     return UI_SEEK_FORWARD;
        case BK_UP:     return UI_VOLUME_UP;
        case BK_DOWN:   return UI_VOLUME_DOWN;
        case BK_X:      return UI_QUALITY;
        case BK_L1:     return UI_PREV_VIDEO;
        case BK_R1:     return UI_NEXT_VIDEO;
        default:        return UI_NONE;      /* SELECT is for searching, not for this */
        }
    }

    if (st->view == VIEW_HISTORY) {
        switch (key) {
        case BK_UP:   case BK_LEFT:  case BK_L1:
            st->history_sel = st->history_sel > 0 ? st->history_sel - 1 : 0;
            st->dirty = true;
            return UI_NONE;
        case BK_DOWN: case BK_RIGHT: case BK_R1: case BK_L2: case BK_R2: {
            int n = st->history ? history_order(st->history, NULL, 0) : 0;
            if (st->history_sel + 1 < (int)st->history->count) st->history_sel++;
            (void)n;
            st->dirty = true;
            return UI_NONE;
        }
        case BK_A:      return UI_PLAY;
        case BK_B:      st->view = VIEW_RESULTS; layout(st); st->dirty = true; return UI_NONE;
        case BK_SELECT: st->view = VIEW_RESULTS; layout(st); st->dirty = true; return UI_NONE;
        default:        return UI_NONE;
        }
    }

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
                /* D-pad movement and a key the keyboard has no use for changed nothing on screen, so they
                 * must not mark the frame dirty either: that rebuilds the whole draw list for nothing. */
                return UI_NONE;
            }
            return UI_NONE;
        }
    }

    /* the results view */
    switch (key) {
    case BK_SELECT:                          /* search: SELECT is the key the hints and the README name */
    case BK_X:                               /* and X as well, since the keyboard uses it for capitals */
        st->view = VIEW_KEYBOARD;
        kb_reset(&st->kb);
        layout(st);
        st->dirty = true;
        return UI_NONE;
    case BK_Y:                               /* the watched list (SELECT searched before it existed) */
        if (st->history && st->history->count > 0) {
            st->view = VIEW_HISTORY;
            st->history_sel = 0;
            layout(st);
            st->dirty = true;
        }
        return UI_NONE;
    case BK_A:
        if (st->results.count) return UI_PLAY;
        st->view = VIEW_KEYBOARD;                 /* nothing to play: go and type */
        st->dirty = true;
        return UI_NONE;
    case BK_B:
        /* Only ever clears. It used to quit when the list was empty, which is the state the app starts in and
         * the state after a search that found nothing: pressing back there ended the app, which is
         * indistinguishable from a crash. HOME is the only key that quits. */
        if (st->results.count) {
            results_clear(&st->results);
            ui_set_status(st, "SELECT searches");
            st->dirty = true;
        }
        return UI_NONE;
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

    if (st->view == VIEW_PLAYING) {
        x += widget_hint(dl, t, x, y, BK_START, st->playing.paused ? "resume" : "pause", true) + 12;
        x += widget_hint(dl, t, x, y, BK_LEFT, "seek", false) + 12;
        x += widget_hint(dl, t, x, y, BK_UP, "volume", false) + 12;
        x += widget_hint(dl, t, x, y, BK_X, "quality", false) + 12;
        x += widget_hint(dl, t, x, y, BK_B, "stop", true) + 12;
    } else if (st->view == VIEW_HISTORY) {
        x += widget_hint(dl, t, x, y, BK_A, "carry on", true) + 12;
        x += widget_hint(dl, t, x, y, BK_B, "back", false) + 12;
        x += widget_hint(dl, t, x, y, BK_HOME, "quit", false) + 12;
    } else if (st->view == VIEW_KEYBOARD) {
        x += widget_hint(dl, t, x, y, BK_A, "type", true) + 12;
        x += widget_hint(dl, t, x, y, BK_X, "capitals", false) + 12;
        x += widget_hint(dl, t, x, y, BK_Y, "space", false) + 12;
        x += widget_hint(dl, t, x, y, BK_B, "delete", false) + 12;
        x += widget_hint(dl, t, x, y, BK_START, "search", true) + 12;
    } else {
        x += widget_hint(dl, t, x, y, BK_A, "play", true) + 12;
        x += widget_hint(dl, t, x, y, BK_SELECT, "search", true) + 12;
        x += widget_hint(dl, t, x, y, BK_Y, "watched", false) + 12;
        x += widget_hint(dl, t, x, y, BK_B, "clear", false) + 12;
    }
    /* the status only where there is room for it: the keyboard's hints fill the row, and a message drawn
     * over them reads as neither */
    if (st->status[0] && st->view == VIEW_RESULTS)
        widget_small(dl, t, x + 8, y, SCREEN_W - x - 18, 18, st->status, ALIGN_RIGHT);
}

static void draw_playing(AppState *st, DrawList *dl)
{
    const UiTheme *t = &st->theme;
    const Playing *pl = &st->playing;

    dl_text_wrapped(dl, 16, 58, SCREEN_W - 32, 52, pl->title[0] ? pl->title : "Playing", FONT_UI_BOLD, 20,
                    t->ink, ALIGN_LEFT, VALIGN_TOP, 2, 24);
    widget_small(dl, t, 16, 114, SCREEN_W - 200, 18, pl->channel, ALIGN_LEFT);
    if (pl->dropped > 0) {
        char d[32];
        snprintf(d, sizeof d, "%d frames dropped", pl->dropped);
        dl_text(dl, SCREEN_W - 240, 114, 224, 18, d, FONT_UI, 11, t->warn, ALIGN_RIGHT, VALIGN_MIDDLE);
    }

    /* what the video unit is decoding, and how loud it is */
    char badge[64];
    if (!pl->video_on)                   snprintf(badge, sizeof badge, "%s", "audio only");
    else if (pl->width > 0 && pl->height > 0) snprintf(badge, sizeof badge, "%dx%d", pl->width, pl->height);
    else                                 snprintf(badge, sizeof badge, "%s", "starting");
    dl_rect_rounded(dl, 16, 140, 96, 24, 6, pl->video_on ? t->accent : t->faint);
    dl_text(dl, 16, 140, 96, 24, badge, FONT_UI_BOLD, 13,
            pl->video_on ? t->accent_ink : t->ink, ALIGN_CENTER, VALIGN_MIDDLE);
    if (pl->buffering && !pl->paused && pl->video_on) widget_spinner(dl, t, 126, 152, 8, st->spin);

    char vol[32];
    snprintf(vol, sizeof vol, "volume %d%%", pl->volume);
    widget_small(dl, t, SCREEN_W - 200, 140, 184, 24, vol, ALIGN_RIGHT);

    /* where we are */
    char a[16], b[16], times[48];
    fmt_duration((int)pl->position, a, sizeof a);
    fmt_duration((int)pl->duration, b, sizeof b);
    snprintf(times, sizeof times, "%s / %s", a, b);
    float frac = pl->duration > 0 ? (float)(pl->position / pl->duration) : 0.0f;
    widget_bar(dl, t, 16, 178, SCREEN_W - 32, 10, frac, true);
    widget_mono(dl, t, 16, 190, SCREEN_W - 32, 20, times, ALIGN_CENTER);

    /* the transport row: tapped, or driven by the buttons */
    static const TransportButton order[TP_COUNT] = { TP_PREV, TP_VOL_DOWN, TP_PAUSE, TP_VOL_UP,
                                                     TP_QUALITY, TP_NEXT, TP_STOP };
    for (int i = 0; i < TP_COUNT; i++) {
        int x, w;
        ui_transport_box(i, &x, &w);
        const char *label = "";
        bool hot = false;
        char q[16];
        switch (order[i]) {
        case TP_PREV:     label = "|<";  break;
        case TP_NEXT:     label = ">|";  break;
        case TP_VOL_DOWN: label = "vol -"; break;
        case TP_VOL_UP:   label = "vol +"; break;
        case TP_PAUSE:    label = pl->paused ? "> play" : "|| pause"; hot = true; break;
        case TP_QUALITY:  snprintf(q, sizeof q, "%dp", pl->quality); label = q; break;
        case TP_STOP:     label = "[] stop"; break;
        default: break;
        }
        dl_rect_rounded(dl, x, TP_Y, w, TP_H, 8, hot ? t->accent : t->panel);
        dl_text(dl, x, TP_Y, w, TP_H, label, FONT_UI_BOLD, 13,
                hot ? t->accent_ink : t->ink, ALIGN_CENTER, VALIGN_MIDDLE);
    }

    /* a paused video says so; so does a stopped one, with the reason */
    if (pl->paused)
        widget_label(dl, t, 0, 296, SCREEN_W, 22, "Paused", ALIGN_CENTER);
    if (pl->status[0])
        dl_text_wrapped(dl, 40, 294, SCREEN_W - 80, 44, pl->status, FONT_UI, 15, t->warn,
                        ALIGN_CENTER, VALIGN_TOP, 2, 20);
    if (!pl->video_on)
        widget_small(dl, t, 0, 320, SCREEN_W, 20, "The lid is shut: the sound carries on without the picture.",
                     ALIGN_CENTER);
}

/* The watched list: what the history file holds, most recent first. */
static void draw_history(AppState *st, DrawList *dl)
{
    const UiTheme *t = &st->theme;
    const History *h = st->history;

    if (!h || h->count == 0) {
        widget_label(dl, t, 0, 220, SCREEN_W, 24, "Nothing watched yet", ALIGN_CENTER);
        widget_small(dl, t, 0, 250, SCREEN_W, 20, "A video you have played turns up here, where it stopped.",
                     ALIGN_CENTER);
        return;
    }

    int row_h = 62, top = 92, rows = (SCREEN_H - 92 - 40) / row_h;
    if (st->history_sel < st->history_scroll) st->history_scroll = st->history_sel;
    if (st->history_sel >= st->history_scroll + rows) st->history_scroll = st->history_sel - rows + 1;
    if (st->history_scroll < 0) st->history_scroll = 0;

    for (int r = 0; r < rows; r++) {
        int at = st->history_scroll + r;
        if (at >= h->count) break;
        const HistoryItem *it = &h->item[at];
        bool on = (at == st->history_sel);
        int y = top + r * row_h;

        dl_rect_rounded(dl, 12, y, SCREEN_W - 24, row_h - 8, 8, on ? t->panel_hi : t->panel);
        if (on) dl_rect_rounded(dl, 12, y, 3, row_h - 8, 2, t->accent);

        dl_text_wrapped(dl, 24, y + 6, SCREEN_W - 190, 30, it->title, FONT_UI_BOLD, 14, t->ink,
                        ALIGN_LEFT, VALIGN_TOP, 1, 18);
        char line[160];
        char pos[16], dur[16];
        fmt_duration((int)it->position, pos, sizeof pos);
        fmt_duration(it->duration_sec, dur, sizeof dur);
        snprintf(line, sizeof line, "%s \xC2\xB7 %s of %s", it->channel, pos, dur);
        widget_small(dl, t, 24, y + 28, SCREEN_W - 200, 20, line, ALIGN_LEFT);

        /* how far in it got */
        float frac = it->duration_sec > 0 ? (float)(it->position / it->duration_sec) : 0.0f;
        widget_bar(dl, t, SCREEN_W - 150, y + 26, 126, 6, frac, true);
    }
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

    if (st->view == VIEW_PLAYING) {
        draw_playing(st, dl);
        draw_hints(st, dl);
        return;
    }
    if (st->view == VIEW_HISTORY) {
        draw_history(st, dl);
        draw_hints(st, dl);
        return;
    }

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
