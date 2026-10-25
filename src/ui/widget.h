/* widget.h -- the little pieces the two views share. */
#ifndef BANANATUBE_WIDGET_H
#define BANANATUBE_WIDGET_H

#include "display/drawlist.h"
#include "input/keys.h"
#include "ui/theme.h"

#include <stdbool.h>

/* A rounded panel with the palette's body colour and a hairline top highlight, as the menu's cards are. */
void widget_panel(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, int radius);

/* A sunken field: what the search bar looks like empty. */
void widget_well(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, int radius);

/* A pill with a key chip and its word: "A Play". Returns the width it used. */
int  widget_hint(DrawList *dl, const UiTheme *t, int x, int y, BananaKey key, const char *label, bool hot);

/* The button hints along the bottom of a view. `keys`/`labels` are parallel, `n` of them. */
void widget_hints_row(DrawList *dl, const UiTheme *t, const char *title);

/* A horizontal progress track (the search spinner, a video's position). `frac` is 0..1. */
void widget_bar(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, float frac, bool sunken);

/* A spinner: `phase` is 0..1 round the circle. */
void widget_spinner(DrawList *dl, const UiTheme *t, int cx, int cy, int radius, float phase);

/* Text with the palette's colours and the sizes this app uses, so the two views agree. */
void widget_title(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, const char *text, HAlign align);
void widget_label(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, const char *text, HAlign align);
void widget_small(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, const char *text, HAlign align);
void widget_mono(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, const char *text, HAlign align);

#endif /* BANANATUBE_WIDGET_H */
