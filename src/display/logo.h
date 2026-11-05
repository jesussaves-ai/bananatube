/* logo.h -- the header's brand mark, decoded from a PNG into the same pictures the thumbnails are.
 *
 * A PNG has an alpha channel and the renderer's pictures do not, so the mark is composited onto the bar it
 * will sit on while it is decoded. One 48x48 decode at startup.
 */
#ifndef BANANATUBE_LOGO_H
#define BANANATUBE_LOGO_H

#include "display/draw.h"

#include <stdbool.h>
#include <stdint.h>

/* Decodes `path` into `out` (RGB565, high byte first). `back` is the bar colour as 0xRRGGBB, which the PNG's
 * transparent pixels become. False and a log line when it cannot be read or decoded. On success `out->pixels`
 * is malloc'd; free() it. On failure `out` is left with pixels == NULL. */
bool logo_load(BottomTex *out, const char *path, uint32_t back);

#endif /* BANANATUBE_LOGO_H */
