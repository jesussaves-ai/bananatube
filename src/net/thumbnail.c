/* thumbnail.c */
#include "net/thumbnail.h"
#include "util.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jpeglib.h>

void thumb_init(ThumbCache *tc, int cap)
{
    memset(tc, 0, sizeof *tc);
    if (cap > THUMB_MAX_SLOTS) cap = THUMB_MAX_SLOTS;
    if (cap < 0) cap = 0;
    tc->cap = cap;
}

int thumb_count(const ThumbCache *tc)
{
    return tc->resident;
}

const char *thumb_id_at(const ThumbCache *tc, int slot)
{
    if (slot < 0 || slot >= THUMB_MAX_SLOTS) return "";
    return tc->id[slot];
}

int thumb_touch(ThumbCache *tc, const char *id)
{
    if (!tc || !id || !*id) return -1;
    for (int i = 0; i < tc->cap; i++) {
        if (tc->id[i][0] && strcmp(tc->id[i], id) == 0) {
            tc->used[i] = ++tc->tick;
            return i;
        }
    }
    return -1;
}

int thumb_slot_for(ThumbCache *tc, const char *id)
{
    if (!tc || tc->cap <= 0 || !id || !*id) return -1;

    int have = thumb_touch(tc, id);
    if (have >= 0) return have;

    for (int i = 0; i < tc->cap; i++) {
        if (!tc->id[i][0]) {                       /* free: take it */
            str_copy(tc->id[i], sizeof tc->id[i], id);
            tc->used[i] = ++tc->tick;
            tc->resident++;
            return i;
        }
    }

    /* Full: the one drawn longest ago goes. The grid keeps about a screenful resident, so this is the
     * eviction that costs least. */
    int lru = 0;
    for (int i = 1; i < tc->cap; i++)
        if (tc->used[i] < tc->used[lru]) lru = i;

    str_copy(tc->id[lru], sizeof tc->id[lru], id);
    tc->used[lru] = ++tc->tick;
    return lru;
}

void thumb_forget(ThumbCache *tc, int slot)
{
    if (!tc || slot < 0 || slot >= THUMB_MAX_SLOTS) return;
    if (tc->id[slot][0]) {
        tc->id[slot][0] = '\0';
        if (tc->resident > 0) tc->resident--;
    }
}

const char *thumb_url(const char *video_id)
{
    /* One static buffer: the caller copies it out immediately. */
    static char url[160];
    snprintf(url, sizeof url, "https://i.ytimg.com/vi/%s/mqdefault.jpg", video_id ? video_id : "");
    return url;
}

/* ---- JPEG -> RGB565 ------------------------------------------------------------------------------------ */

/* libjpeg longjmps out of a decode that goes wrong (a truncated download, a 404 page saved as .jpg). Left
 * to its default handler it exits the process, so a missing thumbnail would take the whole app with it.
 * This one jumps back to thumb_decode's setjmp and reports the message. */
typedef struct {
    struct jpeg_error_mgr pub;
    jmp_buf               jump;
    char                  message[JMSG_LENGTH_MAX];
} ThumbError;

static void thumb_error_exit(j_common_ptr cinfo)
{
    ThumbError *e = (ThumbError *)cinfo->err;
    (*cinfo->err->format_message)(cinfo, e->message);
    longjmp(e->jump, 1);
}

bool thumb_decode(const uint8_t *data, size_t n, uint8_t **out, int *w, int *h, int max_w, int max_h)
{
    if (out) *out = NULL;
    if (w) *w = 0;
    if (h) *h = 0;
    if (!data || n < 4) return false;

    struct jpeg_decompress_struct cinfo;
    ThumbError jerr;
    uint8_t *rgb = NULL;
    uint8_t *buf = NULL;
    int iw = 0, ih = 0;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = thumb_error_exit;
    if (setjmp(jerr.jump)) {
        /* Anything libjpeg called fatal lands here: undo what we own and say so. */
        free(rgb);
        free(buf);
        jpeg_destroy_decompress(&cinfo);
        log_line("thumbnail: %s", jerr.message);
        return false;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, (unsigned char *)data, (unsigned long)n);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    cinfo.out_color_space = JCS_RGB;
    /* The cheapest way to a small picture: ask libjpeg for 1/2, 1/4 or 1/8 while it decodes, rather than
     * decoding 480x360 on an A55 and scaling it afterwards. */
    cinfo.scale_num = 1;
    cinfo.scale_denom = 1;
    for (int d = 2; d <= 8; d *= 2)
        if ((int)cinfo.image_width / d >= max_w && (int)cinfo.image_height / d >= max_h)
            cinfo.scale_denom = d;

    if (!jpeg_start_decompress(&cinfo)) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    iw = (int)cinfo.output_width;
    ih = (int)cinfo.output_height;
    if (iw <= 0 || ih <= 0) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    rgb = malloc((size_t)iw * ih * 3);
    if (!rgb) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    while (cinfo.output_scanline < cinfo.output_height) {
        uint8_t *row = rgb + (size_t)cinfo.output_scanline * (size_t)iw * 3;
        JSAMPROW rows[1] = { row };
        if (jpeg_read_scanlines(&cinfo, rows, 1) == 0) break;
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    buf = malloc((size_t)iw * (size_t)ih * 2);
    if (!buf) { free(rgb); return false; }

    for (size_t i = 0, p = 0; i < (size_t)iw * (size_t)ih; i++, p += 3) {
        uint16_t c = (uint16_t)(((rgb[p] & 0xf8) << 8) | ((rgb[p + 1] & 0xfc) << 3) | (rgb[p + 2] >> 3));
        buf[i * 2]     = (uint8_t)(c >> 8);
        buf[i * 2 + 1] = (uint8_t)(c & 0xff);
    }
    free(rgb);

    if (out) *out = buf; else free(buf);
    if (w) *w = iw;
    if (h) *h = ih;
    return true;
}
