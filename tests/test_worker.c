/* The thumbnail worker: a real JPEG, decoded off the main thread, into the slot it was asked for.
 *
 * The fetch is injected, so this needs no network: the point being tested is the queue, the "do not start the
 * same download twice" rule, the decode, and that a failure frees the slot for a retry. */
#include "net/worker.h"
#include "test.h"

#include <jpeglib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool make_jpeg(int w, int h, uint8_t **out, size_t *n)
{
    struct jpeg_compress_struct c;
    struct jpeg_error_mgr e;
    c.err = jpeg_std_error(&e);
    jpeg_create_compress(&c);

    unsigned char *buf = NULL;
    unsigned long size = 0;
    jpeg_mem_dest(&c, &buf, &size);
    c.image_width = (JDIMENSION)w;
    c.image_height = (JDIMENSION)h;
    c.input_components = 3;
    c.in_color_space = JCS_RGB;
    jpeg_set_defaults(&c);
    jpeg_set_quality(&c, 60, TRUE);
    jpeg_start_compress(&c, TRUE);

    uint8_t *row = malloc((size_t)w * 3);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            row[x * 3 + 0] = (uint8_t)(x * 255 / (w ? w : 1));
            row[x * 3 + 1] = (uint8_t)(y * 255 / (h ? h : 1));
            row[x * 3 + 2] = 128;
        }
        JSAMPROW rows[1] = { row };
        jpeg_write_scanlines(&c, rows, 1);
    }
    free(row);
    jpeg_finish_compress(&c);
    jpeg_destroy_compress(&c);
    *out = buf;
    *n = size;
    return size > 0;
}

typedef struct {
    uint8_t *jpeg;
    size_t   n;
    int      calls;
    int      fail_for;                 /* fail every fetch after this many */
} Fake;

static bool fake_fetch(const char *video_id, uint8_t **data, size_t *len, void *user)
{
    Fake *f = user;
    f->calls++;
    if (f->fail_for >= 0 && f->calls > f->fail_for) return false;
    if (strcmp(video_id, "nosuchvideo") == 0) return false;
    *data = malloc(f->n);
    if (!*data) return false;
    memcpy(*data, f->jpeg, f->n);
    *len = f->n;
    return true;
}

/* Wait for the workers to finish (they are threads; the test must not race them). */
static int wait_done(ThumbWorker *w, ThumbDone *out, int max, int want)
{
    for (int tries = 0; tries < 200; tries++) {
        int n = thumb_worker_drain(w, out, max);
        if (n >= want) return n;
        usleep(10000);
    }
    return thumb_worker_drain(w, out, max);
}

int main(void)
{
    /* a 480x360 source, like an hqdefault fallback: the decode should halve it, not take it whole */
    uint8_t *small = NULL, *big = NULL;
    size_t small_n = 0, big_n = 0;
    check(make_jpeg(320, 180, &small, &small_n));
    check(make_jpeg(480, 360, &big, &big_n));

    uint8_t *pixels = NULL;
    int w = 0, h = 0;

    /* a 320x180 source is decoded at its own size: it is already what a tile wants */
    check(thumb_decode(small, small_n, &pixels, &w, &h, THUMB_DRAW_W, THUMB_DRAW_H));
    check_int(w, 320);
    check_int(h, 180);
    free(pixels);

    /* a 480x360 source is decoded at 1/2 (240x180): libjpeg's own scaling, which is what keeps the decode
     * cheap on an A55. It is still bigger than the 208x117 a tile draws, so nothing is upscaled after. */
    check(thumb_decode(big, big_n, &pixels, &w, &h, THUMB_DRAW_W, THUMB_DRAW_H));
    check_int(w, 240);
    check_int(h, 180);
    check(w >= THUMB_DRAW_W && h >= THUMB_DRAW_H);
    free(pixels);

    /* rubbish is refused, not crashed on: this is a truncated download or a 404 page */
    pixels = (uint8_t *)0x1;
    w = h = 7;
    check(!thumb_decode((const uint8_t *)"not a jpeg at all", 17, &pixels, &w, &h, THUMB_W, THUMB_H));
    check(pixels == NULL);
    check_int(w, 0);

    /* ---- the worker ---- */
    ThumbWorker *worker = thumb_worker_start(2);
    check(worker != NULL);
    if (!worker) { free(small); free(big); test_done(); }

    Fake fake = { .jpeg = small, .n = small_n, .calls = 0, .fail_for = -1 };
    thumb_worker_set_fetch(worker, fake_fetch, &fake);

    check(thumb_worker_request(worker, 3, "dQw4w9WgXcQ"));
    check(!thumb_worker_request(worker, 3, "dQw4w9WgXcQ"));   /* the same video is not fetched twice */
    check(thumb_worker_pending(worker));

    ThumbDone done[DONE_MAX];
    int n = wait_done(worker, done, DONE_MAX, 1);
    check_int(n, 1);
    check_int(done[0].slot, 3);
    check_str(done[0].video_id, "dQw4w9WgXcQ");
    check(done[0].pixels != NULL);
    check_int(done[0].w, 320);
    free(done[0].pixels);

    /* once it is done, the same video can be asked for again (the grid scrolled back to it) */
    check(thumb_worker_request(worker, 4, "dQw4w9WgXcQ"));
    n = wait_done(worker, done, DONE_MAX, 1);
    check_int(n, 1);
    check_int(done[0].slot, 4);
    free(done[0].pixels);

    /* a fetch that fails reports an empty picture, so the caller can free the slot and try later */
    fake.fail_for = fake.calls;
    check(thumb_worker_request(worker, 5, "anothervideo"));
    n = wait_done(worker, done, DONE_MAX, 1);
    check_int(n, 1);
    check(done[0].pixels == NULL);
    check_int(done[0].w, 0);

    thumb_worker_stop(worker);
    free(small);
    free(big);

    test_done();
}
