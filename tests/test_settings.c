/* The app's own settings screen: the rows, what a button does to them, and that a change reaches the Conf.
 * No display: the view is plain data (ui/settings.c), so this is the part a screenshot would only make you
 * squint at. */
#include "ui/settings.h"
#include "ui/view.h"
#include "test.h"

#include <string.h>

int main(void)
{
    /* the table is the file's keys, one row each */
    check_int(settings_rows(), 8);

    Conf conf;
    conf_defaults(&conf);
    SettingsView sv;
    settings_open(&sv, &conf);
    check(sv.conf == &conf);
    check_int(sv.sel, 0);
    check(!sv.editing);

    /* the D-pad walks the list and stops at the ends (no wrap: the first and last rows are not neighbours) */
    check_int(settings_key(&sv, BK_UP), SETTINGS_NONE);
    check_int(sv.sel, 0);
    check_int(settings_key(&sv, BK_DOWN), SETTINGS_MOVED);
    check_int(sv.sel, 1);
    for (int i = 0; i < 20; i++) settings_key(&sv, BK_DOWN);
    check_int(sv.sel, settings_rows() - 1);
    check_int(settings_key(&sv, BK_DOWN), SETTINGS_NONE);

    /* the backend row: RIGHT steps forward and wraps, LEFT steps back, and the Conf follows */
    sv.sel = 0;
    check_str(conf_backend_name(conf.backend), "auto");
    check_int(settings_key(&sv, BK_RIGHT), SETTINGS_CHANGED);
    check_str(sv.changed_key, "backend");
    check_str(conf_backend_name(conf.backend), "invidious");
    settings_key(&sv, BK_RIGHT);
    check_str(conf_backend_name(conf.backend), "innertube");
    settings_key(&sv, BK_RIGHT);                     /* wraps round */
    check_str(conf_backend_name(conf.backend), "auto");
    settings_key(&sv, BK_LEFT);                      /* and back the other way */
    check_str(conf_backend_name(conf.backend), "innertube");
    /* A changes a choice row too (it is what a tap sends) */
    settings_key(&sv, BK_A);
    check_str(conf_backend_name(conf.backend), "auto");

    /* a value the list does not have (edited on the card) still cycles to a real one */
    conf.hwdec[0] = '\0';
    str_copy(conf.hwdec, sizeof conf.hwdec, "rkmpp-copy");
    sv.sel = 6;
    check_int(settings_key(&sv, BK_RIGHT), SETTINGS_CHANGED);
    check_str(sv.changed_key, "hwdec");
    check(strcmp(conf.hwdec, "rkmpp-copy") != 0);

    /* the lid row is a two-way choice: it toggles whichever way the D-pad goes */
    sv.sel = 2;
    bool was_lid = conf.audio_only_on_lid;
    settings_key(&sv, BK_RIGHT);
    check(conf.audio_only_on_lid != was_lid);
    settings_key(&sv, BK_LEFT);
    check(conf.audio_only_on_lid == was_lid);

    /* the numbers land in the Conf as numbers (the defaults are 50, 12 and 6000) */
    sv.sel = 3;                                      /* history_size */
    check_int(conf.history_size, 50);
    settings_key(&sv, BK_RIGHT);
    check_int(conf.history_size, 100);
    settings_key(&sv, BK_LEFT);
    check_int(conf.history_size, 50);
    sv.sel = 4;                                      /* cache_size */
    check_int(conf.cache_size, 12);
    settings_key(&sv, BK_RIGHT);
    check_int(conf.cache_size, 18);
    sv.sel = 5;                                      /* search_timeout_ms */
    check_int(conf.search_timeout_ms, 6000);
    settings_key(&sv, BK_RIGHT);
    check_int(conf.search_timeout_ms, 10000);

    /* the instance row is typed, not cycled: LEFT/RIGHT leave it alone */
    sv.sel = 7;
    str_copy(conf.instance, sizeof conf.instance, "");
    check_int(settings_key(&sv, BK_RIGHT), SETTINGS_NONE);
    check_str(conf.instance, "");
    /* A opens the keyboard, prefilled with what is there */
    check_int(settings_key(&sv, BK_A), SETTINGS_MOVED);
    check(sv.editing);
    str_copy(sv.entry, sizeof sv.entry, "https://inv.nadeko.net");
    check_int(settings_key(&sv, BK_START), SETTINGS_CHANGED);   /* START saves it */
    check(!sv.editing);
    check_str(sv.changed_key, "instance");
    check_str(conf.instance, "https://inv.nadeko.net");

    /* SELECT puts the keyboard away and keeps the old value */
    settings_key(&sv, BK_A);
    check(sv.editing);
    str_copy(sv.entry, sizeof sv.entry, "https://wrong.example");
    check_int(settings_key(&sv, BK_SELECT), SETTINGS_MOVED);
    check(!sv.editing);
    check_str(conf.instance, "https://inv.nadeko.net");

    /* B deletes a character while typing, and does not close the view */
    settings_key(&sv, BK_A);
    str_copy(sv.entry, sizeof sv.entry, "abc");
    settings_key(&sv, BK_B);
    check_str(sv.entry, "ab");
    /* ...and B closes the view when the list has it */
    sv.editing = false;
    check_int(settings_key(&sv, BK_B), SETTINGS_CLOSE);
    check_int(settings_key(&sv, BK_START), SETTINGS_CLOSE);

    /* a tap finds the row it is on, and nothing in the hint strip or the margins */
    sv.sel = 0; sv.top = 0;
    check_int(settings_hit(&sv, SCREEN_W / 2, 20), 0);
    check_int(settings_hit(&sv, SCREEN_W / 2, 20 + 48 * 3), 3);
    check_int(settings_hit(&sv, SCREEN_W / 2, SCREEN_H - 5), -1);    /* the hint row */
    check_int(settings_hit(&sv, 4, 20), -1);                         /* the left margin */

    /* the two frames draw, and neither overflows the draw list (the caller resets it, as ui_draw_* do) */
    UiTheme th = ui_theme();
    DrawList dl;
    memset(&dl, 0x5a, sizeof dl);
    dl_reset(&dl);
    settings_draw_top(&sv, &dl, &th);
    check(dl.n > 0);
    check_int(dl.dropped, 0);
    settings_open(&sv, &conf);
    memset(&dl, 0x5a, sizeof dl);
    dl_reset(&dl);
    settings_draw_bot(&sv, &dl, &th);
    check(dl.n > 0);
    check_int(dl.dropped, 0);
    /* and with the keyboard up for the instance */
    sv.sel = 7;
    settings_key(&sv, BK_A);
    memset(&dl, 0x5a, sizeof dl);
    dl_reset(&dl);
    settings_draw_bot(&sv, &dl, &th);
    check(dl.n > 0);
    check_int(dl.dropped, 0);

    test_done();
}
