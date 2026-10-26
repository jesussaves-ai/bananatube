/* settings.h -- the app's own settings screen.
 *
 * The same rows banana/bananatube.conf holds, changed with the D-pad and saved to the file the moment a value
 * changes, so the client never needs the card out or the menu's Settings to be configured. One list on the
 * bottom panel (the screen with the controls) and what the selected row does on the top one -- the layout
 * BananaOS's own Settings uses, and the same place the app puts everything a finger has to reach.
 *
 * The view is plain data and the drawing happens once per frame from it, so settings_key() and the geometry
 * can be tested without a display, exactly as ui/view.c is (tests/test_settings.c drives this file).
 */
#ifndef BANANATUBE_SETTINGS_H
#define BANANATUBE_SETTINGS_H

#include "conf.h"
#include "display/drawlist.h"
#include "input/keyboard.h"
#include "input/keys.h"
#include "ui/theme.h"

#include <stdbool.h>

/* What a button did here. The caller saves the file on SETTINGS_CHANGED (main.c), which is the only place
 * that owns the Conf's lifetime and the live effects a change has. */
typedef enum {
    SETTINGS_NONE = 0,
    SETTINGS_MOVED,          /* the selection or a value changed on screen: redraw */
    SETTINGS_CHANGED,        /* a value changed: bananatube.conf wants saving and applying */
    SETTINGS_CLOSE,          /* B or START: back to the results */
} SettingsResult;

typedef struct {
    Conf    *conf;               /* the app's settings, read and written in place */
    int      sel, top;           /* the selected row, and the first row drawn (all of them fit today) */
    bool     editing;            /* the instance row is open for typing (the keyboard is up) */
    Keyboard kb;
    char     entry[256];         /* what is being typed for the instance */
    char     changed_key[24];    /* the key the last change touched, so main.c can apply it live */
} SettingsView;

/* How many rows there are (the table is fixed). */
int settings_rows(void);

/* Point the view at the app's settings and start at the first row. */
void settings_open(SettingsView *sv, Conf *conf);

SettingsResult settings_key(SettingsView *sv, BananaKey key);

/* The row a tap at (x, y) landed on, or -1. Device/screen units, as the bottom panel draws them. */
int settings_hit(const SettingsView *sv, int x, int y);

void settings_draw_top(SettingsView *sv, DrawList *dl, const UiTheme *t);
void settings_draw_bot(SettingsView *sv, DrawList *dl, const UiTheme *t);

#endif /* BANANATUBE_SETTINGS_H */
