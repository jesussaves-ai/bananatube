/* The bottom screen's logic: the grid's geometry and selection, the keyboard, and what a button means. No
 * display, no fonts -- these are the parts that are wrong in a way a screenshot makes you squint at. */
#include "input/keyboard.h"
#include "ui/results.h"
#include "ui/view.h"
#include "test.h"

#include <string.h>

static void fill(Results *r, int n)
{
    SearchResult item[RESULTS_MAX];
    for (int i = 0; i < n && i < RESULTS_MAX; i++) {
        memset(&item[i], 0, sizeof item[i]);
        snprintf(item[i].id, sizeof item[i].id, "vid%08d", i);
        snprintf(item[i].title, sizeof item[i].title, "Video %d", i);
        item[i].duration_sec = 60 + i;
        item[i].views = 1000 * (i + 1);
    }
    results_clear(r);
    results_append(r, item, n);
}

int main(void)
{
    /* ---- the grid ---- */
    Results r;
    fill(&r, 8);
    check_int(results_rows(&r), 3);
    check_int(r.sel, 0);
    check(!results_move(&r, BK_LEFT));                 /* already at the left of its row */
    check(results_move(&r, BK_RIGHT));
    check_int(r.sel, 1);
    check(results_move(&r, BK_DOWN));
    check_int(r.sel, 4);                               /* one whole row */
    check(results_move(&r, BK_UP));
    check_int(r.sel, 1);
    check(!results_move(&r, BK_UP));                   /* the top row: nothing above */
    /* the last row: right does not run off the end */
    r.sel = 5;
    check(results_move(&r, BK_DOWN));
    check_int(r.sel, 7);                               /* 8 videos: the last row is short, so DOWN lands on the last */
    check(!results_move(&r, BK_RIGHT));
    check(!results_move(&r, BK_DOWN));                 /* nothing below the last row */

    /* two rows fit in 348; the selection scrolls them into view */
    check_int(results_visible_rows(348), 2);
    r.sel = 7;
    results_scroll_into_view(&r, 348);
    check(r.top_row >= 1);
    int vis[RESULTS_MAX];
    int n = results_visible(&r, 348, vis, RESULTS_MAX);
    check_int(n, 5);                                    /* two rows of 8 videos: 3 + the 2 the last row has */
    int found = 0;
    for (int i = 0; i < n; i++) if (vis[i] == r.sel) found = 1;
    check(found);                                       /* the selection is always among them */
    r.sel = 0;
    results_scroll_into_view(&r, 348);
    check_int(r.top_row, 0);

    /* an empty list has nothing to move */
    fill(&r, 0);
    check(!results_move(&r, BK_DOWN));
    check_int(results_visible(&r, 348, vis, RESULTS_MAX), 0);

    /* the tiles' boxes line up in three columns and step by a row */
    int x, y, w, h;
    results_tile_box(0, 0, 8, 88, SCREEN_W - 16, &x, &y, &w, &h);
    check_int(y, 88);
    check_int(h, TILE_H);
    int x2, y2;
    results_tile_box(1, 0, 8, 88, SCREEN_W - 16, &x2, &y2, &w, &h);
    check_int(x2 - x, TILE_W + TILE_GAP);
    check_int(y2, 88);
    results_tile_box(3, 0, 8, 88, SCREEN_W - 16, &x2, &y2, &w, &h);
    check_int(x2, x);                                   /* the next row starts at the same x */
    check_int(y2, 88 + TILE_H + TILE_GAP);

    /* a tap on a tile finds it */
    fill(&r, 6);
    r.view_h = 348;
    results_tile_box(2, 0, 8, 88, SCREEN_W - 16, &x, &y, &w, &h);
    check_int(results_hit(&r, 348, 8, 88, SCREEN_W - 16, x + 5, y + 5), 2);
    check_int(results_hit(&r, 348, 8, 88, SCREEN_W - 16, x + 5, y - 40), -1);
    check_int(results_hit(&r, 348, 8, 88, SCREEN_W - 16, 4, y + 5), -1);      /* left of the first tile */
    check_int(results_hit(&r, 348, 8, 88, SCREEN_W - 16, x - 40, y + 5), 1);   /* and x-40 is on the tile before */

    /* a picture arrives for a video and its tile finds it */
    results_set_thumb(&r, "vid00000003", 7);
    check_int(r.tex_slot[3], 7);
    results_set_thumb(&r, "not-here", 9);               /* no such video: nothing changes */
    check_int(r.tex_slot[3], 7);

    /* ---- the keyboard ---- */
    Keyboard k;
    kb_reset(&k);
    check_int(k.layer, 0);
    check_str(kb_label(&k, 1, 0), "q");
    check_str(kb_label(&k, 1, 4), "t");
    check_str(kb_label(&k, 0, 3), "4");                 /* the digits are on the top row */
    check_str(kb_label(&k, KB_ROWS, 0), "shift");
    check_str(kb_label(&k, KB_ROWS, 3), "search");

    /* the D-pad moves, and down from the letters lands on the wide row */
    int before = k.row;
    check(kb_move(&k, BK_RIGHT));
    check_int(k.col, 5);
    check(kb_move(&k, BK_DOWN));
    check(kb_move(&k, BK_DOWN));
    check(kb_move(&k, BK_DOWN));
    check_int(k.row, KB_ROWS);
    check(k.wide >= 0 && k.wide < KB_WIDE);
    check(kb_move(&k, BK_UP));
    check_int(k.row, KB_ROWS - 1);
    check(before == 1);
    /* the cursor stays inside the grid */
    for (int i = 0; i < 40; i++) kb_move(&k, BK_LEFT);
    check_int(k.col, 0);
    for (int i = 0; i < 40; i++) kb_move(&k, BK_RIGHT);
    check_int(k.col, KB_COLS - 1);

    /* A types the letter the cursor is on, and the layers change what the letter is */
    k.row = 1; k.col = 0; k.layer = 0;
    char ch[8] = "";
    check_int(kb_press(&k, BK_A, ch, sizeof ch), KB_ACTION_CHAR);
    check_str(ch, "q");
    kb_press(&k, BK_X, ch, sizeof ch);
    check_int(k.layer, 1);
    check_int(kb_press(&k, BK_A, ch, sizeof ch), KB_ACTION_CHAR);
    check_str(ch, "Q");
    check(kb_press(&k, BK_A, ch, sizeof ch) == KB_ACTION_CHAR);
    kb_press(&k, BK_X, ch, sizeof ch);
    check_int(k.layer, 2);                              /* symbols */
    kb_press(&k, BK_X, ch, sizeof ch);
    check_int(k.layer, 0);                              /* and round again */

    /* the wide keys */
    k.row = KB_ROWS; k.wide = 1;
    check_int(kb_press(&k, BK_A, ch, sizeof ch), KB_ACTION_SPACE);
    k.wide = 2;
    check_int(kb_press(&k, BK_A, ch, sizeof ch), KB_ACTION_DELETE);
    k.wide = 3;
    check_int(kb_press(&k, BK_A, ch, sizeof ch), KB_ACTION_SUBMIT);
    k.wide = 0;
    check_int(kb_press(&k, BK_A, ch, sizeof ch), KB_ACTION_LAYER);

    /* the buttons the menu's keyboard uses */
    check_int(kb_press(&k, BK_B, ch, sizeof ch), KB_ACTION_DELETE);
    check_int(kb_press(&k, BK_Y, ch, sizeof ch), KB_ACTION_SPACE);
    check_int(kb_press(&k, BK_START, ch, sizeof ch), KB_ACTION_SUBMIT);
    check_int(kb_press(&k, BK_SELECT, ch, sizeof ch), KB_ACTION_CLOSE);
    check_int(kb_press(&k, BK_L1, ch, sizeof ch), KB_ACTION_NONE);

    /* the height the results are given what is left of */
    check(kb_height() > 0);
    check(kb_height() < SCREEN_H / 2);                   /* it leaves more than half the screen to the results */
    check(SCREEN_H - kb_height() - TILE_H > 0);          /* and a row of tiles still fits above it */

    /* ---- what a button means ---- */
    AppState st;
    ui_init(&st);
    check_int(st.view, VIEW_RESULTS);
    check_int(st.results.count, 0);

    /* SELECT opens the keyboard -- the key the on-screen hints and the README name -- and X does too, since
     * the keyboard itself uses X for capitals */
    check_int(ui_key(&st, BK_SELECT), UI_NONE);
    check_int(st.view, VIEW_KEYBOARD);
    check_int(ui_key(&st, BK_START), UI_NONE);           /* nothing typed yet */
    str_copy(st.query, sizeof st.query, "banana");
    check_int(ui_key(&st, BK_START), UI_SEARCH);
    check_int(st.view, VIEW_RESULTS);                    /* the keyboard gets out of the way */
    check(st.searching);

    /* results arrive and the grid can be walked; a move may need pictures */
    fill(&st.results, 6);
    st.searching = false;
    check_int(ui_key(&st, BK_RIGHT), UI_THUMBS);
    check_int(st.results.sel, 1);
    check_int(ui_key(&st, BK_A), UI_PLAY);

    /* the watched list is on Y, and with nothing watched at all it leaves the view alone */
    check_int(ui_key(&st, BK_Y), UI_NONE);
    check_int(st.view, VIEW_RESULTS);

    /* B clears the results -- and with an empty list it does nothing at all. It used to quit there, which is
     * a "back"/"cancel" press in the state the app starts in: on the handheld that read as a crash. HOME is
     * the only key that leaves, and it leaves from every view. */
    check_int(ui_key(&st, BK_B), UI_NONE);
    check_int(st.results.count, 0);
    check_int(ui_key(&st, BK_B), UI_NONE);
    check_int(st.view, VIEW_RESULTS);
    check_int(ui_key(&st, BK_HOME), UI_QUIT);

    /* typing in the keyboard view edits the query, and DELETE survives a multi-byte character */
    ui_init(&st);
    ui_key(&st, BK_X);
    str_copy(st.query, sizeof st.query, "ab\xC3\xA9");
    ui_key(&st, BK_B);                                   /* deletes the whole é, not half of it */
    check_str(st.query, "ab");

    /* the views get a height that fits what they draw */
    ui_init(&st);
    check(st.view_h >= TILE_H);
    ui_key(&st, BK_X);
    check(st.view_h >= TILE_H);

    /* ---- the transport row ---- */
    /* the buttons tile the width: no gap at the ends, and every pixel of the row belongs to one of them */
    int px, pw, last_x = 0, last_w = 0;
    ui_transport_box(0, &px, &pw);
    check_int(px, 10);
    check(pw > 40);                                   /* wide enough to read a word in */
    ui_transport_box(TP_COUNT - 1, &last_x, &last_w);
    check_int(last_x + last_w, SCREEN_W - 10);
    for (int i = 1; i < TP_COUNT; i++) {
        int x, w;
        ui_transport_box(i, &x, &w);
        int prev_x, prev_w;
        ui_transport_box(i - 1, &prev_x, &prev_w);
        check(x > prev_x);
        check_int(x - (prev_x + prev_w), 6);          /* the gap between them */
    }
    /* a tap finds the button it is on, and nothing above the row */
    ui_transport_box(2, &px, &pw);
    check_int(ui_transport_hit(px + pw / 2, TP_Y + TP_H / 2), TP_PAUSE);
    check_int(ui_transport_hit(px + pw / 2, TP_Y - 5), TP_NONE);
    check_int(ui_transport_hit(px + pw / 2, TP_Y + TP_H), TP_NONE);
    ui_transport_box(5, &px, &pw);
    check_int(ui_transport_hit(px + 1, TP_Y + 1), TP_NEXT);
    ui_transport_box(6, &px, &pw);
    check_int(ui_transport_hit(px + 1, TP_Y + 1), TP_STOP);
    ui_transport_box(0, &px, &pw);
    check_int(ui_transport_hit(px + 1, TP_Y + 1), TP_PREV);

    /* ---- what the buttons mean while a video plays ---- */
    ui_init(&st);
    str_copy(st.playing.title, sizeof st.playing.title, "A video");
    st.view = VIEW_PLAYING;
    check_int(ui_key(&st, BK_LEFT), UI_SEEK_BACK);
    check_int(ui_key(&st, BK_RIGHT), UI_SEEK_FORWARD);
    check_int(ui_key(&st, BK_L2), UI_SEEK_BACK);
    check_int(ui_key(&st, BK_R2), UI_SEEK_FORWARD);
    check_int(ui_key(&st, BK_UP), UI_VOLUME_UP);
    check_int(ui_key(&st, BK_DOWN), UI_VOLUME_DOWN);
    check_int(ui_key(&st, BK_X), UI_QUALITY);
    check_int(ui_key(&st, BK_L1), UI_PREV_VIDEO);
    check_int(ui_key(&st, BK_R1), UI_NEXT_VIDEO);
    check_int(ui_key(&st, BK_START), UI_PAUSE);
    check_int(ui_key(&st, BK_B), UI_STOP);
    check_int(ui_key(&st, BK_HOME), UI_QUIT);
    /* stopping goes back to the results */
    ui_set_playing_stopped(&st, "Stopped.");
    check_int(st.view, VIEW_RESULTS);
    check_str(st.playing.status, "Stopped.");

    /* ---- the small formatters ---- */
    char buf[32];
    fmt_views(-1, buf, sizeof buf);
    check_str(buf, "");
    fmt_views(987, buf, sizeof buf);
    check_str(buf, "987");
    fmt_views(12345, buf, sizeof buf);
    check_str(buf, "12K");
    fmt_views(3456789, buf, sizeof buf);
    check_str(buf, "3.4M");
    fmt_duration(754, buf, sizeof buf);
    check_str(buf, "12:34");
    fmt_duration(3600 + 754, buf, sizeof buf);
    check_str(buf, "1:12:34");
    fmt_duration(-1, buf, sizeof buf);
    check_str(buf, "--:--");

    test_done();
}
