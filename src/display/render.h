/* render.h -- the software renderer for the bottom screen, with no DRM in it.
 *
 * The panels are 640x480 on the RG DS and 1024x768 on the Plus, and the UI is designed in the smaller grid:
 * every draw is multiplied by `scale` (1.0 or 1.6) here. This half is device-independent on purpose, so it
 * can be rendered into a buffer and checked -- a misplaced draw is invisible on one panel size and obvious on
 * the other, which is exactly the kind of bug that costs a wasted flash.
 */
#ifndef BANANATUBE_RENDER_H
#define BANANATUBE_RENDER_H

#include "display/drawlist.h"
#include "font.h"

/* What the renderer draws into: an ARGB32 buffer and, optionally, the text renderer. */
typedef struct {
    uint32_t *canvas;
    int       w, h;
    FontCtx  *font;
} RenderCanvas;

void fill_rect_black(RenderCanvas *rc, int x, int y, int w, int h);

/* A length or a position from the UI's 640x480 grid in panel pixels: 1.0 on the RG DS, 1.6 on the Plus. */
int  render_scale(int v, float scale);

/* Draws `dl` into an ARGB32 buffer (stride `w` pixels), every op clipped to (cx, cy, cw, ch) -- the region a
 * damaged rectangle covers, in panel pixels. `fnt` may be NULL, then text is skipped. */
void render_ops(uint32_t *canvas, int w, int h, FontCtx *fnt, const DrawList *dl,
                const BottomTex *tex, int tex_n, float scale,
                int cx, int cy, int cw, int ch);

/* The whole list, cleared first: what a headless picture of the screen needs. */
void render_full(uint32_t *canvas, int w, int h, FontCtx *fnt, const DrawList *dl,
                 const BottomTex *tex, int tex_n, float scale);

#endif /* BANANATUBE_RENDER_H */
