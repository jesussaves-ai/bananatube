/* The bottom screen's logic: the rail's geometry and selection, the keyboard, and what a button means. No
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
    /* ---- the rail ---- */
    Results r;
    fill(&r, 8);
    check_int(r.sel, 0);
    check(!results_move(&r, BK_LEFT));                 /* already the first */
    check(!results_move(&r, BK_UP));
    check(results_move(&r, BK_RIGHT));
    check_int(r.sel, 1);
    check(results_move(&r, BK_RIGHT));
    check_int(r.sel, 2);
    check(results_move(&r, BK_LEFT));
    check_int(r.sel, 1);
    check(results_move(&r, BK_DOWN));                  /* a page forward */
    check_int(r.sel, 4);
    check(results_move(&r, BK_UP));
    check_int(r.sel, 1);
    r.sel = 6;
    check(results_move(&r, BK_DOWN));                  /* 8 cards: the page clamps to the last */
    check_int(r.sel, 7);
    check(!results_move(&r, BK_RIGHT));
    fill(&r, 1);                                       /* one card: nothing to move to */
    check(!results_move(&r, BK_LEFT));
    check(!results_move(&r, BK_RIGHT));
    check(!results_move(&r, BK_DOWN));
    fill(&r, 0);                                       /* and none at all */
    check(!results_move(&r, BK_RIGHT));

    /* the selected card is wider and centred; its neighbours sit a gap away on either side */
    fill(&r, 8);
    r.sel = 3;
    int bx, by, bw, bh;
    results_card_box(&r, 3, 8, 88, SCREEN_W - 16, &bx, &by, &bw, &bh);
    check_int(bw, RAIL_CARD_W * 3 / 2);
    check_int(bx, 8 + (SCREEN_W - 16 - bw) / 2);
    check_int(by, 88);
    check_int(bh, RAIL_CARD_H);
    int bx2, bw2;
    results_card_box(&r, 4, 8, 88, SCREEN_W - 16, &bx2, &by, &bw2, &bh);
    check_int(bx2, bx + bw + RAIL_GAP);
    check_int(bw2, RAIL_CARD_W);
    results_card_box(&r, 2, 8, 88, SCREEN_W - 16, &bx2, &by, &bw2, &bh);
    check_int(bx2 + bw2 + RAIL_GAP, bx);

    /* the screen holds three cards and a peek, and moving brings the next one in */
    r.sel = 0;
    int vis[RESULTS_MAX];
    int n = results_visible(&r, 8, SCREEN_W - 16, vis, RESULTS_MAX);
    check(n >= 3);
    check(n <= 5);
    check_int(vis[0], 0);
    r.sel = 7;
    n = results_visible(&r, 8, SCREEN_W - 16, vis, RESULTS_MAX);
    int found = 0;
    for (int i = 0; i < n; i++) if (vis[i] == r.sel) found = 1;
    check(found);                                      /* the selection is always among them */

    /* a picture arrives for a video and its card finds it */
    results_set_thumb(&r, "vid00000003", 7);
    check_int(r.tex_slot[3], 7);
    results_set_thumb(&r, "not-here", 9);              /* no such video: nothing changes */
    check_int(r.tex_slot[3], 7);

    /* previous/next step one result, not a page, and stop at the ends */
    fill(&r, 6);
    r.sel = 0;
    check(!results_step(&r, -1));
    check(results_step(&r, 1));
    check_int(r.sel, 1);
    r.sel = 5;
    check(!results_step(&r, 1));
    check(results_step(&r, -1));
    check_int(r.sel, 4);

    /* a very long title stays inside its card and is capped at two lines: the whole card is drawn inside
     * its own box, and the one wrapped op is the title. The view is one card wide, so the card under test is
     * the only one on screen. */
    fill(&r, 4);
    memset(r.item[1].title, 'x', sizeof r.item[1].title - 1);
    r.item[1].title[sizeof r.item[1].title - 1] = 0;
    r.sel = 1;
    UiTheme th = ui_theme();
    int rw = RAIL_CARD_W * 3 / 2;
    DrawList one;
    dl_reset(&one);
    results_draw(&one, &r, &th, NULL, 0, 8, 88, rw, 392);
    results_card_box(&r, 1, 8, 88, rw, &bx, &by, &bw, &bh);
    int wrapped = 0;
    for (int i = 0; i < one.n; i++) {
        const DrawOp *o = &one.op[i];
        check(o->x >= bx && o->y >= by && o->x + o->w <= bx + bw && o->y + o->h <= by + bh);
        if (o->kind == OP_TEXT && o->wrap) { wrapped++; check_int(o->max_lines, 2); }
    }
    check_int(wrapped, 1);

    /* a move onto a card that has no picture yet: the new card's whole box is damaged, so the placeholder is
     * repainted and the card that lost the focus loses its highlight */
    fill(&r, 4);
    r.sel = 0;
    DrawList moved, was;          /* `a`/`b2` would collide with names already in this file */
    dl_reset(&moved);
    results_draw(&moved, &r, &th, NULL, 0, 8, 88, SCREEN_W - 16, 392);
    r.sel = 1;
    dl_reset(&was);
    results_draw(&was, &r, &th, NULL, 0, 8, 88, SCREEN_W - 16, 392);
    Damage own, missed, dmg;
    dl_diff(&moved, &was, &own);
    damage_reset(&missed);
    damage_reset(&dmg);
    damage_next(&own, &missed, &dmg, &missed);
    results_card_box(&r, 1, 8, 88, SCREEN_W - 16, &bx, &by, &bw, &bh);
    check(damage_touches(&dmg, bx, by, bw, bh));
    results_card_box(&r, 0, 8, 88, SCREEN_W - 16, &bx, &by, &bw, &bh);
    check(damage_touches(&dmg, bx, by, bw, bh));       /* and the card that lost it */

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
    /* the cursor stays inside the rail */
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
    check(SCREEN_H - kb_height() - RAIL_CARD_H > 0);     /* and a row of cards still fits above it */

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

    /* results arrive and the rail can be walked; a move may need pictures */
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
    check(st.view_h >= RAIL_CARD_H);
    ui_key(&st, BK_X);
    check(st.view_h >= RAIL_CARD_H);

    /* A D-pad move in the keyboard view lights a different key, so the frame has to be redrawn. The main
     * loop waits 300 ms before redrawing a frame nothing asked for, and that wait was the keyboard's D-pad
     * lag on the handheld: the cursor only moved when the clock ticked. */
    ui_init(&st);
    ui_key(&st, BK_X);                                   /* the keyboard */
    st.dirty = false;
    check_int(ui_key(&st, BK_RIGHT), UI_NONE);
    check(st.dirty);

    /* ---- a frame's draw list is that frame's, and nothing else's ----
     * ui_draw clears the list it is handed. The app used to reset it once outside its main loop, so every
     * frame appended another copy of the whole UI: the count grew by a screenful per frame until the list hit
     * DRAW_MAX, and past that dl_push() dropped the tail -- the screen stopped updating, which on the handheld
     * was a keyboard that froze after one letter and a 250 ms full-screen repaint every frame before that.
     * The lists here are deliberately not cleared first: ui_draw has to do it. */
    ui_init(&st);
    DrawList d1, d2;
    memset(&d1, 0x5a, sizeof d1);
    ui_draw_top(&st, &d1, NULL, 0);
    memset(&d2, 0x5a, sizeof d2);
    ui_draw_bot(&st, &d2, NULL, 0);
    check(d1.n > 0);
    check(d2.n > 0);
    check_int(d1.dropped, 0);
    check_int(d2.dropped, 0);
    /* each of them clears what it is given, and neither grows: the content is on the top panel and the
     * controls on the bottom, so a second frame of each must be exactly the first */
    DrawList t1, t2, b1, b2;
    memset(&t1, 0x5a, sizeof t1);
    ui_draw_top(&st, &t1, NULL, 0);
    memset(&t2, 0x5a, sizeof t2);
    ui_draw_top(&st, &t2, NULL, 0);
    check_int(t2.n, t1.n);
    memset(&b1, 0x5a, sizeof b1);
    ui_draw_bot(&st, &b1, NULL, 0);
    memset(&b2, 0x5a, sizeof b2);
    ui_draw_bot(&st, &b2, NULL, 0);
    check_int(b2.n, b1.n);
    /* and with the keyboard up, where the bottom panel has the most draws it ever has */
    ui_key(&st, BK_SELECT);
    memset(&b1, 0x5a, sizeof b1);
    ui_draw_bot(&st, &b1, NULL, 0);
    memset(&b2, 0x5a, sizeof b2);
    ui_draw_bot(&st, &b2, NULL, 0);
    check_int(b2.n, b1.n);
    check_int(b1.dropped, 0);

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
    ui_set_playing(&st, "dQw4w9WgXcQ", "A video", "A channel", 754);
    check_str(st.playing.id, "dQw4w9WgXcQ");
    check_str(st.playing.title, "A video");
    check_int(st.playing.duration_sec, 754);
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

    /* ---- the header ---- */
    /* The mark can be missing -- a corrupt or absent PNG -- and the wordmark must still say both halves: the
     * picture is what was lost, not the "Tube". */
    ui_init(&st);
    ui_set_logo(&st, -1, 40);
    DrawList hn;
    ui_draw_top(&st, &hn, NULL, 0);
    int nb = 0, nt = 0;
    for (int i = 0; i < hn.n; i++) {
        if (hn.op[i].kind != OP_TEXT) continue;
        if (strcmp(hn.op[i].text, "Banana") == 0) nb++;
        if (strcmp(hn.op[i].text, "Tube") == 0) nt++;
    }
    check_int(nb, 1);
    check_int(nt, 1);

    ui_init(&st);
    ui_set_battery(&st, 67, true);
    ui_set_logo(&st, 0, 40);   /* a slot, not an address: a draw op names a picture by its index */
    DrawList h1;
    ui_draw_top(&st, &h1, NULL, 0);
    int images = 0, bananas = 0, tubes = 0, percents = 0;
    for (int i = 0; i < h1.n; i++) {
        if (h1.op[i].kind == OP_IMAGE) images++;
        if (h1.op[i].kind != OP_TEXT) continue;
        if (strcmp(h1.op[i].text, "Banana") == 0) bananas++;
        if (strcmp(h1.op[i].text, "Tube") == 0) tubes++;
        if (strcmp(h1.op[i].text, "67%") == 0) percents++;
    }
    check_int(images, 1);                              /* the brand mark, once */
    check_int(bananas, 1);
    check_int(tubes, 1);
    check_int(percents, 1);
    for (int i = 0; i < h1.n; i++) {
        if (h1.op[i].kind != OP_TEXT) continue;
        if (strcmp(h1.op[i].text, "Banana") == 0) check_int((int)h1.op[i].color, (int)st.theme.accent);
        if (strcmp(h1.op[i].text, "Tube") == 0)   check_int((int)h1.op[i].color, (int)st.theme.ink);
    }

    /* nobody set a mark and there is no battery: the wordmark alone, and no percentage -- not "0%" */
    ui_init(&st);
    ui_draw_top(&st, &h1, NULL, 0);
    images = percents = bananas = 0;
    for (int i = 0; i < h1.n; i++) {
        if (h1.op[i].kind == OP_IMAGE) images++;
        if (h1.op[i].kind != OP_TEXT) continue;
        if (strcmp(h1.op[i].text, "Banana") == 0) bananas++;
        if (strchr(h1.op[i].text, '%') != NULL) percents++;
    }
    check_int(images, 0);                              /* no mark: nothing is drawn for it, not a black box */
    check_int(bananas, 1);                             /* the wordmark is there whether or not the mark is */
    check_int(percents, 0);

    /* ---- the bottom bar ----
     * The hint row is centred, and a row of chips whose widths follow their words cannot be laid out from a
     * fixed left edge and still come out even. The first chip has to start exactly where a row measured with
     * the same helper would start. */
    ui_init(&st);
    ui_set_playing(&st, "id", "Title", "Channel", 100);
    st.status[0] = 0;
    DrawList barops;
    ui_draw_bot(&st, &barops, NULL, 0);
    int bartop = SCREEN_H - 30, barfirst = -1;
    for (int i = 0; i < barops.n; i++) {
        const DrawOp *o = &barops.op[i];
        if (o->kind == OP_RECT && o->y > bartop) { barfirst = o->x; break; }
    }
    static const char *chips[5] = { "START", "\xE2\x86\x90\xE2\x86\x92", "\xE2\x86\x91\xE2\x86\x93", "X", "B" };
    static const char *words[5] = { "pause", "seek", "volume", "quality", "stop" };
    int bartotal = 0;
    for (int i = 0; i < 5; i++) bartotal += widget_hint_text(NULL, &st.theme, 0, 0, chips[i], words[i], i == 0) + 16;
    bartotal -= 16;
    check_int(barfirst, (SCREEN_W - bartotal) / 2);

    /* the D-pad's chips are not blank: seek and volume are the D-pad, and its face is two arrows */
    int bararrows = 0;
    for (int i = 0; i < barops.n; i++)
        if (barops.op[i].kind == OP_TEXT && strstr(barops.op[i].text, "\xE2\x86") != NULL) bararrows++;
    check_int(bararrows, 2);

    /* the transport row lights the button the last tap landed on */
    ui_init(&st);
    ui_set_playing(&st, "id", "Title", "Channel", 100);
    ui_set_transport(&st, TP_QUALITY);
    check_int(st.tp_sel, TP_QUALITY);
    DrawList tpops;
    ui_draw_bot(&st, &tpops, NULL, 0);
    int tpx, tpw;
    ui_transport_box(4, &tpx, &tpw);
    int tprimms = 0;
    for (int i = 0; i < tpops.n; i++) {
        const DrawOp *o = &tpops.op[i];
        if (o->kind == OP_RECT && o->x == tpx && o->y == TP_Y && o->w == tpw && o->color == st.theme.accent) tprimms++;
    }
    check_int(tprimms, 1);

    test_done();
}
