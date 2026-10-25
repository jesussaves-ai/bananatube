/* bottom.h -- the screen the CPU draws.
 *
 * banana-ui's arrangement, which is what keeps an idle frame at well under a millisecond on these SoCs:
 * compose into a cached buffer, keep the last frame's draw list, and on the next frame repaint only the
 * rectangles of the draws that came, went or moved -- then copy just those rectangles into the uncached
 * scanout buffer (the only slow memory in the path) and flip.
 */
#ifndef BANANATUBE_BOTTOM_H
#define BANANATUBE_BOTTOM_H

#include "display/drawlist.h"
#include "display/drm.h"
#include "font.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    DumbBuf   buf[2];
    int       page;
    uint32_t *canvas;          /* cached memory: the composed frame */
    int       w, h;
    DrawList  prev;            /* the last frame, for the damage diff */
    FontCtx  *font;            /* the text renderer (font.c); may be NULL, then text is skipped */
    bool      have_prev;
    /* what the last frame cost, for the log and the benchmark */
    int       last_damage, last_draws;
    long      last_pixels;     /* how many pixels were repainted and copied to the panel */
} Bottom;

bool bottom_init(Bottom *b, int fd, const DrmPanel *p);
void bottom_set_font(Bottom *b, FontCtx *f);
void bottom_fini(Bottom *b, int fd);
/* Forget the last frame, so the next present repaints all of it. For a panel that has been showing something
 * else in between -- the video, or another view -- where the diff against the last draw list would otherwise
 * say "nothing changed" and leave the old picture underneath. */
void bottom_invalidate(Bottom *b);

/* Draws `dl` and puts it on the panel. `tex` (may be NULL) is the thumbnail table the OP_IMAGE draws read;
 * a slot with no pixels draws the op's placeholder colour.
 *
 * `scale` is the panel's size over the 640x480 the UI is designed in: 1 on the RG DS (and under QEMU), 1.6 on
 * the RG DS Plus. Geometry and text are multiplied by it, so the same draws fill either panel. */
void bottom_present(Bottom *b, int fd, const DrmPanel *p, const DrawList *dl,
                    const BottomTex *tex, int tex_n, float scale);


#endif /* BANANATUBE_BOTTOM_H */
