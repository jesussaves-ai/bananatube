/* osd.h -- the top panel's overlay: the resolution badge, the position, the buffering spinner.
 *
 * Drawn with the same renderer as the bottom screen (a DrawList -> an ARGB buffer) and uploaded as one
 * texture over the video. Only re-uploaded when what it says changes, so an idle overlay costs nothing.
 */
#ifndef BANANATUBE_OSD_H
#define BANANATUBE_OSD_H

#include "display/render.h"
#include "player/player.h"
#include "ui/theme.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    unsigned  tex;          /* the GL texture, 0 before the first upload */
    int       w, h;         /* the buffer's size: the panel's width, and the bar's height */
    uint32_t *pixels;
    uint32_t  hash;         /* what is drawn, so the same overlay is not uploaded twice */
    bool      blank;        /* nothing to show: the texture is transparent */
} Osd;

bool osd_init(Osd *o, int panel_w, float scale);
void osd_free(Osd *o);

/* Updates the bar from the player's state and, when it changed, re-uploads the texture. `title` may be NULL
 * when nothing is playing: then the bar is left empty and the video shows through. */
void osd_update(Osd *o, FontCtx *fnt, const Player *p, const char *title, const char *status,
                bool paused, float spin);

/* Draws it over whatever is in the framebuffer, at the bottom of a `panel_w x panel_h` viewport. */
void osd_draw(const Osd *o, int panel_w, int panel_h);

#endif /* BANANATUBE_OSD_H */
