/* keys.c */
#include "input/keys.h"

#include <linux/input-event-codes.h>

BananaKey key_from_code(uint16_t code)
{
    switch (code) {
    /* the D-pad: ROCKNIX's joypad driver reports BTN_DPAD_*, a USB pad may report the arrow keys */
    case BTN_DPAD_UP:    case KEY_UP:    return BK_UP;
    case BTN_DPAD_DOWN:  case KEY_DOWN:  return BK_DOWN;
    case BTN_DPAD_LEFT:  case KEY_LEFT:  return BK_LEFT;
    case BTN_DPAD_RIGHT: case KEY_RIGHT: return BK_RIGHT;

    /* A is east on these handhelds (the Nintendo layout), B is south */
    case BTN_EAST:  case KEY_ENTER:                        return BK_A;
    case BTN_SOUTH: case KEY_BACKSPACE: case KEY_ESC:      return BK_B;
    case BTN_NORTH: case KEY_X:                            return BK_X;
    case BTN_WEST:  case KEY_Y:                            return BK_Y;

    /* the shoulders. banana-ui folds L2/R2 into L/R; here they are their own keys, because the player uses
     * them to seek rather than to page. */
    case BTN_TL:  case KEY_PAGEUP:   return BK_L1;
    case BTN_TR:  case KEY_PAGEDOWN: return BK_R1;
    case BTN_TL2:                    return BK_L2;
    case BTN_TR2:                    return BK_R2;

    case BTN_START:  case KEY_S:   return BK_START;
    case BTN_SELECT: case KEY_TAB: return BK_SELECT;
    case BTN_MODE:   case KEY_F1:  return BK_HOME;

    case KEY_VOLUMEUP:   return BK_VOLUME_UP;
    case KEY_VOLUMEDOWN: return BK_VOLUME_DOWN;
    default:             return BK_NONE;
    }
}

const char *key_name(BananaKey k)
{
    switch (k) {
    case BK_UP:    return "up";
    case BK_DOWN:  return "down";
    case BK_LEFT:  return "left";
    case BK_RIGHT: return "right";
    case BK_A:     return "A";
    case BK_B:     return "B";
    case BK_X:     return "X";
    case BK_Y:     return "Y";
    case BK_L1:    return "L1";
    case BK_R1:    return "R1";
    case BK_L2:    return "L2";
    case BK_R2:    return "R2";
    case BK_START: return "start";
    case BK_SELECT:return "select";
    case BK_HOME:  return "home";
    case BK_VOLUME_UP:   return "volume up";
    case BK_VOLUME_DOWN: return "volume down";
    default:       return "?";
    }
}

const char *key_hint_label(BananaKey k)
{
    switch (k) {
    case BK_A:      return "A";
    case BK_B:      return "B";
    case BK_X:      return "X";
    case BK_Y:      return "Y";
    case BK_L1:     return "L";
    case BK_R1:     return "R";
    case BK_L2:     return "L2";
    case BK_R2:     return "R2";
    case BK_START:  return "START";
    case BK_SELECT: return "SELECT";
    case BK_HOME:   return "HOME";
    default:        return "";
    }
}
