/* keys.h -- the handheld's buttons, as the rest of the app means them.
 *
 * The mapping is banana-ui's (ui/src/input.cpp), so a button does what a BananaOS user expects: A is BTN_EAST
 * and B is BTN_SOUTH (the RG DS's Nintendo layout), which is the one thing that differs from a PC pad.
 */
#ifndef BANANATUBE_KEYS_H
#define BANANATUBE_KEYS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BK_NONE = 0,
    BK_UP, BK_DOWN, BK_LEFT, BK_RIGHT,
    BK_A, BK_B, BK_X, BK_Y,
    BK_L1, BK_R1, BK_L2, BK_R2,
    BK_START, BK_SELECT, BK_HOME,
    BK_VOLUME_UP, BK_VOLUME_DOWN
} BananaKey;

/* The app's action for one evdev code, or BK_NONE. `code` is a KEY_/BTN_ value. */
BananaKey key_from_code(uint16_t code);

/* A short name for logs ("A", "D-pad up", "L2"). */
const char *key_name(BananaKey k);
/* The other direction, for a script that names its keys the way the log does ("down", "A", "select"). Returns
 * BK_NONE for a name that is not one. */
BananaKey   banana_key_from_name(const char *name);

/* A hint chip's letter for the button hints row: "A", "B", "L", "R", "START", "SELECT". */
const char *key_hint_label(BananaKey k);

#endif /* BANANATUBE_KEYS_H */
