/* The header's brand mark: a PNG decoded into the same RGB565 the pictures are, with its transparency
 * composited over the bar it sits on. A missing or corrupt file must be a plain "no mark", never a crash and
 * never a black box. */
#include "display/logo.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

int main(void)
{
    BottomTex mark;
    memset(&mark, 0, sizeof mark);

    check(logo_load(&mark, TEST_DATA_DIR "/banana-48.png", 0x2e2b33));
    check_int(mark.w, 48);
    check_int(mark.h, 48);
    check(mark.pixels != NULL);

    /* the mark is three yellow bananas on nothing: a yellow pixel must come out yellow, and a transparent
     * one must come out as the bar colour we passed */
    int yellow = 0, bar = 0;
    for (int i = 0; i < mark.w * mark.h; i++) {
        uint16_t c = (uint16_t)((mark.pixels[i * 2] << 8) | mark.pixels[i * 2 + 1]);
        int r = (c >> 11) & 0x1f, g = (c >> 5) & 0x3f, b = c & 0x1f;
        if (r > 20 && g > 20 && b < 12) yellow++;
        /* 0x2e2b33 as RGB565: r 0x2e>>3 = 5, g 0x2b>>2 = 10, b 0x33>>3 = 6 */
        if (r == 0x05 && g == 0x0a && b == 0x06) bar++;
    }
    check(yellow > 100);                                    /* the bananas */
    check(bar > 100);                                       /* where the PNG was transparent */
    free((void *)mark.pixels);

    /* a broken file, and one that is not there at all */
    check(!logo_load(&mark, TEST_DATA_DIR "/banana-broken.png", 0x2e2b33));
    check(mark.pixels == NULL);
    check(!logo_load(&mark, TEST_DATA_DIR "/no-such-file.png", 0x2e2b33));
    check(mark.pixels == NULL);

    test_done();
}
