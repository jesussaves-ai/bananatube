/* gles_blit.h -- one textured quad, for the top screen's overlay.
 *
 * The video is drawn by libmpv into the EGL surface; the overlay (the resolution badge, the position, the
 * buffering spinner) is drawn by the same DrawList renderer the bottom screen uses, rasterised into an ARGB
 * buffer and uploaded as a texture. A handful of triangles instead of a second plane or a compositor.
 */
#ifndef BANANATUBE_GLES_BLIT_H
#define BANANATUBE_GLES_BLIT_H

#include <stdbool.h>
#include <stdint.h>

/* Compiles the program. Needs a current EGL context. */
bool blit_init(void);
void blit_fini(void);

/* Uploads (or re-uploads) an ARGB32 buffer as a texture. Returns its id, or 0. */
unsigned blit_upload(unsigned tex, const uint32_t *argb, int w, int h);
void     blit_free(unsigned tex);

/* Draws the texture into the current framebuffer, filling the rectangle (x, y, w, h) of a `dst_w x dst_h`
 * viewport. Alpha-blended, so a transparent pixel lets the video through. */
void blit_draw(unsigned tex, int tex_w, int tex_h, int x, int y, int w, int h, int dst_w, int dst_h);

#endif /* BANANATUBE_GLES_BLIT_H */
