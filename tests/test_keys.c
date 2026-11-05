/* The button mapping: one wrong here and the wrong button plays a video. */
#include "input/keys.h"
#include "test.h"

#include <linux/input-event-codes.h>

int main(void)
{
    /* the RG DS's Nintendo layout: A is east, B is south (banana-ui's ui/src/input.cpp) */
    check(key_from_code(BTN_EAST) == BK_A);
    check(key_from_code(BTN_SOUTH) == BK_B);
    check(key_from_code(BTN_NORTH) == BK_X);
    check(key_from_code(BTN_WEST) == BK_Y);

    /* the D-pad, and the arrow keys a USB keyboard would send */
    check(key_from_code(BTN_DPAD_UP) == BK_UP);
    check(key_from_code(BTN_DPAD_DOWN) == BK_DOWN);
    check(key_from_code(BTN_DPAD_LEFT) == BK_LEFT);
    check(key_from_code(BTN_DPAD_RIGHT) == BK_RIGHT);
    check(key_from_code(KEY_UP) == BK_UP);
    check(key_from_code(KEY_LEFT) == BK_LEFT);

    /* the shoulders: L2/R2 are their own keys (the player seeks with them), where banana-ui folds them in */
    check(key_from_code(BTN_TL) == BK_L1);
    check(key_from_code(BTN_TR) == BK_R1);
    check(key_from_code(BTN_TL2) == BK_L2);
    check(key_from_code(BTN_TR2) == BK_R2);

    check(key_from_code(BTN_START) == BK_START);
    check(key_from_code(BTN_SELECT) == BK_SELECT);
    check(key_from_code(BTN_MODE) == BK_HOME);

    /* the volume rocker is bananad's; we recognise it only so it is not mistaken for a button */
    check(key_from_code(KEY_VOLUMEUP) == BK_VOLUME_UP);
    check(key_from_code(BTN_Z) == BK_NONE);
    check(key_from_code(0xffff) == BK_NONE);

    check_str(key_hint_label(BK_A), "A");
    check_str(key_hint_label(BK_SELECT), "SELECT");
    /* The D-pad has a face too. A hint chip for it used to be an empty pill, which reads as a button that
     * is not there -- and on the playing screen the D-pad is seek and volume. */
    check_str(key_hint_label(BK_LEFT), "\xE2\x86\x90");
    check_str(key_hint_label(BK_UP), "\xE2\x86\x91");
    check_str(key_hint_label(BK_RIGHT), "\xE2\x86\x92");
    check_str(key_hint_label(BK_DOWN), "\xE2\x86\x93");
    check_str(key_name(BK_L2), "L2");

    test_done();
}
