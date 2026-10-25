/* font.h -- FreeType, for the text on the bottom screen.
 *
 * A glyph is rasterised once per (font, size, character) and kept as an 8-bit alpha bitmap, then blended into
 * the frame. On a 640x480 screen with a handful of sizes that is a few hundred small bitmaps in all.
 */
#ifndef BANANATUBE_FONT_H
#define BANANATUBE_FONT_H

#include "display/draw.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct FontCtx FontCtx;

/* Any path may be NULL: a missing font falls back to whichever of the others loaded. Returns NULL when none
 * of them did, which the caller treats as "no text on screen" rather than a reason to fail. */
FontCtx *font_init(const char *regular, const char *bold, const char *mono);
void     font_free(FontCtx *f);

int font_line_height(FontCtx *f, FontId id, int size);
int font_text_width(FontCtx *f, FontId id, int size, const char *utf8);

/* Draws one line, clipped, into an ARGB32 buffer whose stride is `dst_w` pixels. `color` is 0xAARRGGBB.
 * `align`/`valign` place the line inside (x, y, w, h) -- the same box the DrawOp carried. */
void font_draw(FontCtx *f, uint32_t *dst, int dst_w, int dst_h,
               int clip_x, int clip_y, int clip_w, int clip_h,
               int x, int y, int w, int h, const char *utf8,
               FontId id, int size, uint32_t color, HAlign align, VAlign valign);

#endif /* BANANATUBE_FONT_H */
