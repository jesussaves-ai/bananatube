/* keyboard.c -- the on-screen keyboard. */
#include "input/keyboard.h"
#include "ui/theme.h"
#include "util.h"

#include <string.h>

/* Four rows of ten, in three layers. Row 0 carries the digits, so a query with letters and numbers needs no
 * switch; X walks the layers (lowercase, capitals, symbols), as X sets capitals on the menu's keyboard. */
static const char *LETTERS[KB_LAYERS][KB_ROWS] = {
    { "1234567890", "qwertyuiop", "asdfghjkl;", "zxcvbnm,./" },
    { "!@#$%^&*()", "QWERTYUIOP", "ASDFGHJKL:", "ZXCVBNM<>?" },
    { "`~-_=+[]{}", "()<>\"'\\|/", ".,;:!?\xC2\xBF\xC2\xA1", "\xC2\xA3\xE2\x82\xAC\xC2\xA2\xC2\xA5*+~^" },
};
static const char *WIDE[KB_LAYERS][KB_WIDE] = {
    { "shift", "space", "del", "search" },
    { "abc",   "space", "del", "search" },
    { "abc",   "space", "del", "search" },
};

/* Sized so that one row of result tiles (161 px) still fits above it: the point of typing a search is to see
 * what comes back. */
#define KB_PAD 6
#define KB_GAP 4
#define KEY_H 34
#define WIDE_H 30

void kb_reset(Keyboard *k)
{
    k->row = 1;                 /* on the letters, where a search starts */
    k->col = 4;
    k->wide = 0;
    k->layer = 0;
}

const char *kb_label(const Keyboard *k, int row, int col)
{
    if (row < 0 || col < 0) return "";
    if (row < KB_ROWS) {
        if (col >= KB_COLS) return "";
        const char *s = LETTERS[k->layer][row];
        size_t n = strlen(s);
        /* one character, which may be multi-byte: find its byte length */
        size_t at = 0;
        for (int i = 0; i < col && at < n; i++) {
            unsigned char c = (unsigned char)s[at];
            at += (c >= 0xf0) ? 4 : (c >= 0xe0) ? 3 : (c >= 0xc0) ? 2 : 1;
        }
        static char buf[8];
        size_t len = 1;
        unsigned char c = (unsigned char)s[at];
        if (c >= 0xf0) len = 4; else if (c >= 0xe0) len = 3; else if (c >= 0xc0) len = 2;
        if (at + len > n) len = n - at;
        if (len >= sizeof buf) len = sizeof buf - 1;
        memcpy(buf, s + at, len);
        buf[len] = '\0';
        return buf;
    }
    if (col >= KB_WIDE) return "";
    return WIDE[k->layer][col];
}

static int wide_for_col(int col) { return clampi(col * KB_WIDE / KB_COLS, 0, KB_WIDE - 1); }
static int col_for_wide(int wide) { return clampi(wide * KB_COLS / KB_WIDE, 0, KB_COLS - 1); }

bool kb_move(Keyboard *k, BananaKey key)
{
    int ox = k->row, oc = k->col, ow = k->wide;
    switch (key) {
    case BK_LEFT:
        if (k->row < KB_ROWS) { if (k->col > 0) k->col--; }
        else if (k->wide > 0) k->wide--;
        break;
    case BK_RIGHT:
        if (k->row < KB_ROWS) { if (k->col < KB_COLS - 1) k->col++; }
        else if (k->wide < KB_WIDE - 1) k->wide++;
        break;
    case BK_UP:
        if (k->row == KB_ROWS) { k->row = KB_ROWS - 1; k->col = col_for_wide(k->wide); }
        else if (k->row > 0) k->row--;
        break;
    case BK_DOWN:
        if (k->row == KB_ROWS - 1) { k->wide = wide_for_col(k->col); k->row = KB_ROWS; }
        else if (k->row < KB_ROWS - 1) k->row++;
        break;
    default:
        return false;
    }
    return k->row != ox || k->col != oc || k->wide != ow;
}

/* A wide key as an action. */
static KbAction wide_action(int layer, int wide, char *out, size_t cap)
{
    (void)out; (void)cap;
    switch (wide) {
    case 0: return KB_ACTION_LAYER;
    case 1: return KB_ACTION_SPACE;
    case 2: return KB_ACTION_DELETE;
    case 3: return KB_ACTION_SUBMIT;
    default: return KB_ACTION_NONE;
    }
    (void)layer;
}

KbAction kb_press(Keyboard *k, BananaKey key, char *out, size_t out_cap)
{
    if (out && out_cap) out[0] = '\0';
    switch (key) {
    case BK_LEFT: case BK_RIGHT: case BK_UP: case BK_DOWN:
        kb_move(k, key);
        return KB_ACTION_NONE;
    case BK_A:
        if (k->row == KB_ROWS) return wide_action(k->layer, k->wide, out, out_cap);
        str_copy(out, out_cap, kb_label(k, k->row, k->col));
        return out && out[0] ? KB_ACTION_CHAR : KB_ACTION_NONE;
    case BK_B:      return KB_ACTION_DELETE;
    case BK_X:      k->layer = (k->layer + 1) % KB_LAYERS; return KB_ACTION_LAYER;
    case BK_Y:      return KB_ACTION_SPACE;
    case BK_START:  return KB_ACTION_SUBMIT;
    case BK_SELECT: return KB_ACTION_CLOSE;
    default:        return KB_ACTION_NONE;
    }
}

int kb_height(void)
{
    return KB_ROWS * (KEY_H + KB_GAP) + WIDE_H + KB_PAD * 2;   /* the keys' own height, without the growth */
}

void kb_layout(int x, int y, int w, int h, int *key_w, int *key_h, int *wide_h)
{
    (void)y;
    int kk = (w - KB_PAD * 2 - KB_GAP * (KB_COLS - 1)) / KB_COLS;
    int kh = KEY_H, wh = WIDE_H;

    /* With the panel to itself the keys grow to use it: a bigger key is a bigger touch target, and a keyboard
     * filling half its panel looks like something failed to draw. The drawing and the hit test both come
     * through here, so they cannot disagree about where a key is. */
    if (h > 0) {
        int rows_h = KB_ROWS * KEY_H + WIDE_H + KB_GAP * KB_ROWS + KB_PAD * 2;
        int extra = (h - rows_h) / (KB_ROWS + 1);
        if (extra > 0) { kh += extra; wh += extra; }
    }
    if (key_w) *key_w = kk;
    if (key_h) *key_h = kh;
    if (wide_h) *wide_h = wh;
}

KbAction kb_tap(Keyboard *k, int x, int y, char *out, size_t out_cap)
{
    int key_w, key_h, wide_h;
    kb_layout(KB_BOX_X, KB_BOX_Y, KB_BOX_W, KB_BOX_H, &key_w, &key_h, &wide_h);  /* the box kb_draw is given */
    int left = KB_BOX_X + KB_PAD;
    int top = KB_BOX_Y + KB_BOX_H - (KB_ROWS * (key_h + KB_GAP) + wide_h + KB_PAD);
    (void)x;

    int rel = y - top;
    if (rel < 0) return KB_ACTION_NONE;
    if (rel < KB_ROWS * (key_h + KB_GAP)) {
        int row = rel / (key_h + KB_GAP);
        int col = (x - left) / (key_w + KB_GAP);
        if (col < 0 || col >= KB_COLS) return KB_ACTION_NONE;
        if ((x - left) % (key_w + KB_GAP) > key_w) return KB_ACTION_NONE;
        k->row = row; k->col = col;
        return kb_press(k, BK_A, out, out_cap);
    }
    /* the wide row: four keys of equal width */
    int wrow_top = KB_ROWS * (key_h + KB_GAP);
    if (rel >= wrow_top && rel < wrow_top + wide_h) {
        int ww = (SCREEN_W - KB_PAD * 2 - KB_GAP * (KB_WIDE - 1)) / KB_WIDE;
        int wide = (x - left) / (ww + KB_GAP);
        if (wide < 0 || wide >= KB_WIDE) return KB_ACTION_NONE;
        k->row = KB_ROWS; k->wide = wide;
        return kb_press(k, BK_A, out, out_cap);
    }
    return KB_ACTION_NONE;
}

void kb_draw(DrawList *dl, const Keyboard *k, const UiTheme *t, int x, int y, int w, int h)
{
    int key_w, key_h, wide_h;
    kb_layout(x, y, w, h, &key_w, &key_h, &wide_h);
    int total = KB_ROWS * (key_h + KB_GAP) + wide_h + KB_PAD * 2;
    int top = y + h - total;
    if (top < y) top = y;

    dl_rect(dl, x, top, w, h - (top - y), t->bg);
    int left = x + KB_PAD;

    for (int r = 0; r < KB_ROWS; r++) {
        for (int c = 0; c < KB_COLS; c++) {
            const char *label = kb_label(k, r, c);
            if (!*label || strcmp(label, " ") == 0) continue;
            int kx = left + c * (key_w + KB_GAP);
            int ky = top + KB_PAD + r * (key_h + KB_GAP);
            bool on = (k->row == r && k->col == c);
            dl_rect_rounded(dl, kx, ky, key_w, key_h, 7, on ? t->accent : t->panel);
            dl_text(dl, kx, ky, key_w, key_h, label, FONT_UI, 19,
                    on ? t->accent_ink : t->ink, ALIGN_CENTER, VALIGN_MIDDLE);
        }
    }

    int ww = (w - KB_PAD * 2 - KB_GAP * (KB_WIDE - 1)) / KB_WIDE;
    int wy = top + KB_PAD + KB_ROWS * (key_h + KB_GAP);
    for (int i = 0; i < KB_WIDE; i++) {
        int kx = left + i * (ww + KB_GAP);
        bool on = (k->row == KB_ROWS && k->wide == i);
        bool go = (i == 3);                       /* the search key: the one that does something */
        dl_rect_rounded(dl, kx, wy, ww, wide_h, 7, (on || go) ? t->accent : t->panel);
        dl_text(dl, kx, wy, ww, wide_h, WIDE[k->layer][i], FONT_UI, (on || go) ? 15 : 14,
                (on || go) ? t->accent_ink : t->ink, ALIGN_CENTER, VALIGN_MIDDLE);
    }
}
