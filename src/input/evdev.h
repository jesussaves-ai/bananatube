/* evdev.h -- the buttons, the touchscreen and the lid, read straight from /dev/input.
 *
 * No SDL, no libinput: bananad and banana-ui already read these devices this way (system/bananad/bananad.c,
 * ui/src/input.cpp), and evdev lets several readers share a device.
 */
#ifndef BANANATUBE_EVDEV_H
#define BANANATUBE_EVDEV_H

#include "display/draw.h"
#include "input/keys.h"

#include <poll.h>
#include <stdbool.h>

#define INPUT_MAX_DEV 16
#define INPUT_EVENTS_MAX 32

typedef struct {
    BananaKey key;
    bool      down;        /* false: released */
    bool      repeat;      /* the kernel's own autorepeat (a held direction) */
} KeyEvent;

typedef struct {
    /* A touch-down *edge* in this batch of events, not "a finger is on the glass": it is cleared at the start
     * of every dispatch and set when a release or a new touch arrives. A flag that stayed true meant the
     * keyboard's tap was re-run for every later event -- including every button press -- so the last key typed
     * itself again and again and delete looked broken. */
    bool down;
    int  x, y;             /* screen units (0..639, 0..479), after the axis range is mapped; kept between reads */
} TouchEvent;

typedef struct {
    int  fd[INPUT_MAX_DEV];
    int  n;
    bool is_touch[INPUT_MAX_DEV];
    bool is_gamepad[INPUT_MAX_DEV];

    /* the touch device's axis ranges, so a raw value becomes a screen coordinate */
    int  tx_min, tx_max, ty_min, ty_max;
    int  tx_slot_x, tx_slot_y;    /* the last multitouch slot's raw values */

    bool lid_closed;              /* SW_LID */
} InputCtx;

/* Opens every /dev/input/event* it can. `touch_hint` comes from BANANA_TOUCH (part of the bottom panel's
 * touchscreen sysfs path); it is used to pick which of two touch-capable devices is the bottom screen. */
void input_init(InputCtx *in, const char *touch_hint);
void input_close(InputCtx *in);

/* Adds this ctx's fds to the poll set. Returns how many were added. */
int  input_pollfds(InputCtx *in, struct pollfd *pfds, int max);

/* Reads whichever fds are ready. Fills keys/touch and returns how many key events were produced. */
int  input_dispatch(InputCtx *in, struct pollfd *pfds, KeyEvent *keys, int key_max, TouchEvent *touch);

#endif /* BANANATUBE_EVDEV_H */
