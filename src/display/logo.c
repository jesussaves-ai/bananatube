/* logo.c -- the header's brand mark.
 *
 * libpng's simplified API does the decoding, then the RGBA is composited over the bar colour and packed into
 * the RGB565 the renderer's pictures use, so the rest of the tree treats the mark like any other picture.
 * Every failure path leaves `out` empty, because the header draws nothing rather than a placeholder.
 */
#include "display/logo.h"
#include "util.h"

#include <png.h>
#include <stdlib.h>
#include <string.h>

bool logo_load(BottomTex *out, const char *path, uint32_t back)
{
    out->pixels = NULL;
    out->w = 0;
    out->h = 0;

    png_image img;
    memset(&img, 0, sizeof img);
    img.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&img, path)) {
        log_line("logo: %s: %s", path, img.message);
        return false;
    }
    img.format = PNG_FORMAT_RGBA;

    uint8_t *rgba = malloc(PNG_IMAGE_SIZE(img));
    if (!rgba) {
        log_line("logo: %s: no memory for %ux%u", path, img.width, img.height);
        png_image_free(&img);
        return false;
    }
    if (!png_image_finish_read(&img, NULL, rgba, 0, NULL)) {
        log_line("logo: %s: %s", path, img.message);
        free(rgba);
        png_image_free(&img);
        return false;
    }

    int w = (int)img.width, h = (int)img.height;
    uint8_t *px = malloc((size_t)w * h * 2);
    if (!px) {
        log_line("logo: %s: no memory for %dx%d", path, w, h);
        free(rgba);
        png_image_free(&img);
        return false;
    }

    int br = (int)((back >> 16) & 0xff), bg = (int)((back >> 8) & 0xff), bb = (int)(back & 0xff);
    for (int i = 0; i < w * h; i++) {
        int a = rgba[i * 4 + 3];
        int r = (rgba[i * 4 + 0] * a + br * (255 - a) + 127) / 255;
        int g = (rgba[i * 4 + 1] * a + bg * (255 - a) + 127) / 255;
        int b = (rgba[i * 4 + 2] * a + bb * (255 - a) + 127) / 255;
        uint16_t c = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        px[i * 2]     = (uint8_t)(c >> 8);
        px[i * 2 + 1] = (uint8_t)(c & 0xff);
    }
    free(rgba);
    png_image_free(&img);

    out->pixels = px;
    out->w = w;
    out->h = h;
    return true;
}
