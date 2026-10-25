/* thumbnail.h -- the video grid's pictures, kept to a fixed number of small textures.
 *
 * Twenty unoptimised thumbnails decoded into the heap would fragment it and spike memory on a 1 GB device.
 * So: a fixed number of slots (conf.cache_size, 12 by default), an LRU that hands back the slot to reuse,
 * and RGB565 textures at 320x180 (150 KiB each -- 12 of them is 1.8 MB).
 *
 * The bookkeeping and the decode are here; uploading a decoded picture to a GLES texture is the display
 * layer's job (the upload callback), so this file has no opinion about the GPU and the tests can run
 * headless.
 */
#ifndef BANANATUBE_THUMBNAIL_H
#define BANANATUBE_THUMBNAIL_H

#include "net/backend.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define THUMB_MAX_SLOTS 48
/* The cache's target picture size, and the most the decoder ever produces. */
#define THUMB_W 320
#define THUMB_H 180
/* What a tile in the grid actually draws (194x109) plus a margin: decoding to at least this, and no more, is
 * what lets libjpeg halve an hqdefault fallback (480x360 -> 240x180) instead of decoding it whole. */
#define THUMB_DRAW_W 208
#define THUMB_DRAW_H 117

typedef struct {
    int     cap;                                  /* slots in use cap (<= THUMB_MAX_SLOTS) */
    char    id[THUMB_MAX_SLOTS][VIDEO_ID_LEN];    /* "" = free */
    int64_t used[THUMB_MAX_SLOTS];                /* the tick it was last drawn at */
    int64_t tick;
    int     resident;
} ThumbCache;

void thumb_init(ThumbCache *tc, int cap);

/* The slot already holding `id`, marking it as just used, or -1. */
int  thumb_touch(ThumbCache *tc, const char *id);

/* The slot `id` should be loaded into: its own if it has one, a free slot if there is one, else the
 * least-recently-used slot (whose old contents are gone). The id is recorded straight away, so a second
 * request for the same video does not start a second download. -1 only when cap is 0. */
int  thumb_slot_for(ThumbCache *tc, const char *id);

/* The load failed: the slot goes back to free so it can be retried. */
void thumb_forget(ThumbCache *tc, int slot);

int  thumb_count(const ThumbCache *tc);
const char *thumb_id_at(const ThumbCache *tc, int slot);

/* Where the picture for a video lives. SMALL is mqdefault (320x180): the size we want, so the handheld
 * decodes less. LARGE is hqdefault (480x360), which is the fallback -- some videos have only that one. */
typedef enum { THUMB_SMALL = 0, THUMB_LARGE } ThumbSize;
size_t thumb_url(const char *video_id, ThumbSize size, char *out, size_t cap);

/* Decodes a JPEG into RGB565. libjpeg scales it down by 1/2, 1/4 or 1/8 -- the cheapest scaling there is,
 * and it happens during the decode -- while the result is still at least max_w x max_h, so the picture is
 * never upscaled afterwards and never bigger than it needs to be for where it is drawn. *out is malloc'd by
 * the caller's free(). Returns false and logs when the picture cannot be read. */
bool thumb_decode(const uint8_t *data, size_t n, uint8_t **out, int *w, int *h, int max_w, int max_h);

#endif /* BANANATUBE_THUMBNAIL_H */
