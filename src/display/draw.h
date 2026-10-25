/* draw.h -- what the bottom screen is made of.
 *
 * The bottom screen is drawn the way banana-ui draws the menu (ui/src/kms.cpp, ui/src/soft.cpp): a frame is
 * a flat list of draws, compared with the last frame's list draw by draw, and only the rectangles of the
 * draws that came, went or moved are cleared and redrawn. There is no layout engine and no scene graph --
 * each view pushes its ops in order and knows its own bounds.
 */
#ifndef BANANATUBE_DRAW_H
#define BANANATUBE_DRAW_H

#include <stdbool.h>
#include <stdint.h>

/* screen units: the panel is 640x480 whatever its pixel size (the RG DS Plus scales by 1.6) */
#define SCREEN_W 640
#define SCREEN_H 480

typedef enum { OP_RECT = 0, OP_TEXT, OP_IMAGE } OpKind;

typedef enum { ALIGN_LEFT = 0, ALIGN_CENTER, ALIGN_RIGHT } HAlign;
typedef enum { VALIGN_TOP = 0, VALIGN_MIDDLE, VALIGN_BOTTOM } VAlign;

/* How a picture fills its box -- the same names the theme format uses (docs/THEMES.md). */
typedef enum { FIT_COVER = 0, FIT_CONTAIN, FIT_FILL, FIT_PIXEL } Fit;

typedef enum { FONT_UI = 0, FONT_UI_BOLD, FONT_MONO } FontId;

/* An image is a slot in the texture table (net/thumbnail.c fills them). -1 is "nothing to draw yet", which
 * draws the op's placeholder colour instead. */
#define IMAGE_NONE (-1)

typedef struct {
    OpKind   kind;
    int      x, y, w, h;          /* the box, in screen units */
    int      clip_x, clip_y, clip_w, clip_h;   /* resolved when pushed */

    uint32_t color;               /* OP_RECT: the fill. OP_TEXT: the text colour. */
    int      radius;              /* OP_RECT: corner radius (0 = square, h/2 = a pill) */
    int      border;              /* OP_RECT: border width, drawn inside (0 = none) */
    uint32_t border_color;

    FontId   font;                /* OP_TEXT */
    int      size;
    HAlign   align;
    VAlign   valign;
    char     text[80];            /* already truncated: the op does not carry a pointer */

    int      image;               /* OP_IMAGE: the texture slot */
    Fit      fit;
    uint32_t tint;                /* OP_IMAGE: multiplied into the picture (0xffffffff = as-is) */
} DrawOp;

/* Damage is a handful of rectangles to clear. If the frame changed more than MAX_DAMAGE places apart,
 * one bounding rectangle is used instead (cheaper to repaint than to keep splitting). */
#define MAX_DAMAGE 8
typedef struct {
    int n;
    struct { int x, y, w, h; } r[MAX_DAMAGE];
} Damage;

void   damage_reset(Damage *d);
void   damage_add(Damage *d, int x, int y, int w, int h);
bool   damage_empty(const Damage *d);
/* The union of every rectangle, or an empty rectangle when there is none. */
void   damage_bounds(const Damage *d, int *x, int *y, int *w, int *h);
/* True when any part of (x,y,w,h) is inside the damage (so a draw can skip itself). */
bool   damage_touches(const Damage *d, int x, int y, int w, int h);

/* The op's box grown by `pad` (a shadow, an outline). */
void   op_bounds(const DrawOp *op, int pad, int *x, int *y, int *w, int *h);

/* Two ops draw the same pixels in the same place: the damage diff's unit of comparison. */
bool   op_equal(const DrawOp *a, const DrawOp *b);

#endif /* BANANATUBE_DRAW_H */
