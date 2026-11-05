/* widget.h -- the little pieces the two views share. */
#ifndef BANANATUBE_WIDGET_H
#define BANANATUBE_WIDGET_H

#include "display/drawlist.h"
#include "input/keys.h"
#include "ui/theme.h"

#include <stdbool.h>

/* The UI's vocabulary, so the screens agree. Sizes are in screen units (the panel is 640x480 whatever its
 * pixels). Four steps, one colour role each. */
#define W_SIZE_DISPLAY 19
#define W_SIZE_TITLE   14
#define W_SIZE_BODY    12
#define W_SIZE_SMALL   11

/* One radius set: cards and fields, then chips and badges. A pill is half its height and is spelled that way
 * at the call site. */
#define W_RADIUS_CARD  8
#define W_RADIUS_CHIP  4

/* One spacing rhythm. */
#define W_SPACE_XS     6
#define W_SPACE_S      10
#define W_SPACE_M      16
#define W_SPACE_L      24

/* The accent rule: three places per screen -- the focused item, the primary action chip, and one status or
 * progress element. Everything else is ink, muted or faint. More than that is what made the screens noisy. */

/* A rounded panel with the palette's body colour and a hairline top highlight, as the menu's cards are. */
void widget_panel(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, int radius);

/* A sunken field: what the search bar looks like empty. */
void widget_well(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, int radius);

/* A pill with a chip and its word: "A Play". Returns the width it used. The chip's face can be given rather
 * than taken from a key, for a button that is a pair of arrows. Pass a NULL list to measure it and draw
 * nothing: a row whose chip widths follow their words cannot be centred without measuring first. */
int  widget_hint_text(DrawList *dl, const UiTheme *t, int x, int y, const char *chip, const char *label, bool hot);
int  widget_hint(DrawList *dl, const UiTheme *t, int x, int y, BananaKey key, const char *label, bool hot);

/* The button hints along the bottom of a view. `keys`/`labels` are parallel, `n` of them. */
void widget_hints_row(DrawList *dl, const UiTheme *t, const char *title);

/* A horizontal progress track (the search spinner, a video's position). `frac` is 0..1. */
void widget_bar(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, float frac, bool sunken);

/* A spinner: `phase` is 0..1 round the circle. */
void widget_spinner(DrawList *dl, const UiTheme *t, int cx, int cy, int radius, float phase);

/* What the header shows on the right and as the brand: the mark (NULL for none), where the wordmark's second
 * half starts (0 for none), and the battery (-1 for no percentage). */
/* What the header shows: the brand mark's slot in the picture array (-1 for none), where the wordmark's
 * second half starts (0 for none), the battery (-1 for no percentage) and whether it is on the cable. A
 * screen that wants its own name where the brand goes sets title instead: Settings is not the brand. */
typedef struct {
    int         logo_image;
    int         tube_x;
    int         battery;
    bool        charging;
    const char *title;
} HeaderInfo;

/* The header strip every view shares: the bar, the brand and the wordmark on the left, the battery and the
 * clock on the right. */
void widget_header(DrawList *dl, const UiTheme *t, const HeaderInfo *info);

/* Text with the palette's colours and the sizes this app uses, so the two views agree. */
void widget_title(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, const char *text, HAlign align);
void widget_label(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, const char *text, HAlign align);
void widget_small(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, const char *text, HAlign align);
void widget_mono(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, const char *text, HAlign align);

/* The fourth step: the smallest line on a card and in a hint bar. */
void widget_tiny(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, const char *text, HAlign align);

#endif /* BANANATUBE_WIDGET_H */
