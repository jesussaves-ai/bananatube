/* keyboard.h -- the on-screen keyboard, over the results.
 *
 * Four rows of ten keys and a row of wide keys, in three layers (lowercase, capitals, symbols), driven by the
 * D-pad and A or by tapping a key. It is the same shape as the menu's Wi-Fi keyboard, so it feels like the
 * rest of BananaOS.
 */
#ifndef BANANATUBE_KEYBOARD_H
#define BANANATUBE_KEYBOARD_H

#include "display/drawlist.h"
#include "input/keys.h"
#include "ui/theme.h"

#include <stdbool.h>
#include <stddef.h>

#define KB_COLS   10
#define KB_ROWS   4
#define KB_WIDE   4                 /* Shift, Space, Del, Done */
#define KB_LAYERS 3

typedef enum {
    KB_ACTION_NONE = 0,
    KB_ACTION_CHAR,          /* `out` holds the character typed */
    KB_ACTION_SPACE,
    KB_ACTION_DELETE,
    KB_ACTION_LAYER,         /* X: lowercase -> capitals -> symbols -> lowercase */
    KB_ACTION_SUBMIT,        /* START: search for what is typed */
    KB_ACTION_CLOSE,         /* SELECT: put the keyboard away */
} KbAction;

typedef struct {
    int  row;                /* 0..KB_ROWS-1 are the letter rows, KB_ROWS is the wide row */
    int  col;
    int  wide;               /* which wide key, when row == KB_ROWS */
    int  layer;
} Keyboard;

void      kb_reset(Keyboard *k);

/* Moves the cursor. Returns true when it moved (so the caller can mark the frame dirty). */
bool      kb_move(Keyboard *k, BananaKey key);

/* A = the key under the cursor, B = delete, X = layer, Y = space, START = submit, SELECT = close. */
KbAction  kb_press(Keyboard *k, BananaKey key, char *out, size_t out_cap);

/* What a key says: "q", "Shift", "Del", "Done", or "" for a cell that has no key. */
const char *kb_label(const Keyboard *k, int row, int col);

/* The key under a tap at (x, y) in screen units, or KB_ACTION_NONE. */
KbAction  kb_tap(Keyboard *k, int x, int y, char *out, size_t out_cap);

/* Where the keyboard is drawn, given the area it has. */
void      kb_layout(int x, int y, int w, int h, int *key_w, int *key_h, int *wide_h);

/* How tall it is, whatever the width: the results above it are given what is left over. */
int       kb_height(void);

/* The keyboard's box: the bottom panel, above the hint row. kb_draw draws into it and kb_tap hit-tests in it
 * -- one definition, because a keyboard whose hit test and drawing disagree types the wrong letter. It used to
 * bottom-align itself in the whole 640x480 screen whatever box it was given, which left most of the panel
 * empty once the keyboard had a panel to itself. */
#define KB_BOX_X 0
#define KB_BOX_Y 0
#define KB_BOX_W SCREEN_W
#define KB_BOX_H (SCREEN_H - 30)          /* 30: the hint row at the bottom (view.c's HINTS_H) */

void      kb_draw(DrawList *dl, const Keyboard *k, const UiTheme *t, int x, int y, int w, int h);

#endif /* BANANATUBE_KEYBOARD_H */
