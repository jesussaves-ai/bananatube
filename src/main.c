/* main.c -- BananaTube.
 *
 * Phase 1: both panels from one process. The top one is driven through GBM/EGL (the surface libmpv will draw
 * into); the bottom one is drawn by the CPU into DRM dumb buffers, the way banana-ui draws the menu. The
 * buttons and the touchscreen are read from /dev/input, and the lid from SW_LID.
 *
 * banana-ui hands the DRM master over by exiting; bananad starts us and waits. Pressing HOME (or F1 on
 * QEMU) quits, and bananad brings the menu back -- the same handover BananaDS uses.
 */
#include "conf.h"
#include "display/bottom.h"
#include "display/drm.h"
#include "display/gbm_egl.h"
#include "font.h"
#include "input/evdev.h"
#include "util.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

static volatile sig_atomic_t g_redraw = 0;
static volatile sig_atomic_t g_quit = 0;

static void on_usr1(int sig) { (void)sig; g_redraw = 1; }
static void on_term(int sig) { (void)sig; g_quit = 1; }

/* The fonts BananaOS ships (ui/Makefile installs them under /usr/share/banana). Inter for titles, which are
 * small and want to stay readable; Pixelify Sans for the clock and the status line, so it looks like the
 * menu. Any of them missing falls back to whichever loaded. */
#define TTF_TITLE "/usr/share/banana/banana/fonts/Inter-Regular.ttf"
#define TTF_BOLD  "/usr/share/banana/banana/fonts/Inter-SemiBold.ttf"
#define TTF_PIXEL "/usr/share/banana/rnds/fonts/PixelifySans-Medium.ttf"

/* ---- Phase 1's bottom screen: a clock, the buttons pressed, and what the panels are ------------------ */

typedef struct {
    char lines[8][80];
    int  n;
    int  tick;                 /* the last second drawn, so the clock only repaints when it changes */
} Phase1UI;

static void ui_push(Phase1UI *ui, const char *fmt, ...)
{
    if (ui->n >= (int)(sizeof ui->lines / sizeof ui->lines[0])) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ui->lines[ui->n], sizeof ui->lines[0], fmt, ap);
    va_end(ap);
    ui->n++;
}

static void ui_build(Phase1UI *ui, const DrawList *dl, const TouchEvent *touch,
                     const DrmPanel *top, const DrmPanel *bot, int frame_ms)
{
    dl_reset((DrawList *)dl);                 /* Phase 1 rebuilds the whole frame each second */

    DrawList *d = (DrawList *)dl;
    dl_rect(d, 0, 0, SCREEN_W, SCREEN_H, 0xff10131c);
    dl_rect(d, 0, 0, SCREEN_W, 44, 0xff1b2030);
    dl_text(d, 16, 0, 400, 44, "BananaTube", FONT_UI_BOLD, 22, 0xfff1ede2, ALIGN_LEFT, VALIGN_MIDDLE);

    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char clock[16];
    snprintf(clock, sizeof clock, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    dl_text(d, SCREEN_W - 140, 0, 124, 44, clock, FONT_MONO, 22, 0xfff0b429, ALIGN_RIGHT, VALIGN_MIDDLE);

    int y = 70;
    for (int i = 0; i < ui->n; i++) {
        dl_text(d, 20, y, SCREEN_W - 40, 24, ui->lines[i], FONT_UI, 16, 0xffc9c4b8, ALIGN_LEFT, VALIGN_MIDDLE);
        y += 24;
    }

    char foot[128];
    snprintf(foot, sizeof foot, "top %s %dx%d · bottom %s %dx%d · frame %d ms",
             top->name, top->w, top->h, bot->name, bot->w, bot->h, frame_ms);
    dl_text(d, 20, SCREEN_H - 40, SCREEN_W - 40, 20, foot, FONT_MONO, 13, 0xff7d7767, ALIGN_LEFT, VALIGN_MIDDLE);

    if (touch->down) {
        char tap[64];
        snprintf(tap, sizeof tap, "touch %d,%d", touch->x, touch->y);
        dl_text(d, 20, SCREEN_H - 64, SCREEN_W - 40, 20, tap, FONT_UI, 15, 0xfff0b429, ALIGN_LEFT, VALIGN_MIDDLE);
    }

    dl_text(d, SCREEN_W - 400, SCREEN_H - 40, 380, 20,
            "HOME quits · D-pad, A, B, Start, Select are logged", FONT_UI, 13, 0xff7d7767, ALIGN_RIGHT, VALIGN_MIDDLE);
}

int main(void)
{
    signal(SIGUSR1, on_usr1);          /* bananad after a wake: draw a frame, do not re-open anything */
    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);

    const char *top_name = getenv("BANANA_TOP");
    if (!top_name || !*top_name) top_name = "DSI-2";     /* the launcher's default on both handhelds */
    const char *touch_hint = getenv("BANANA_TOUCH");

    log_line("starting: top=%s touch=%s", top_name, touch_hint ? touch_hint : "(auto)");

    int fd = drm_open();
    if (fd < 0) { log_line("no DRM device: %s", strerror(errno)); return 1; }
    if (drmSetMaster(fd)) log_line("drmSetMaster: %s (continuing)", strerror(errno));

    DrmPanel top, bot;
    const char *err = NULL;
    if (!drm_find_panels(fd, top_name, &top, &bot, &err)) {
        log_line("%s", err ? err : "no panels");
        drm_close(fd);
        return 1;
    }

    GbmEgl egl;
    if (!gbm_egl_init(fd, &top, &egl)) { log_line("no EGL on the top panel"); drm_close(fd); return 1; }

    Bottom bottom;
    if (!bottom_init(&bottom, fd, &bot)) { log_line("no bottom-screen buffers"); drm_close(fd); return 1; }

    FontCtx *fnt = font_init(TTF_TITLE, TTF_BOLD, TTF_PIXEL);
    if (!fnt) log_line("no fonts: the bottom screen will have no text");
    bottom_set_font(&bottom, fnt);

    /* Light both panels. The top one's connectors are set here once; every later frame only moves its
     * framebuffer (gbm_egl_swap), which is what a flip is. */
    dl_reset(&bottom.prev);

    InputCtx in;
    input_init(&in, touch_hint);

    Phase1UI ui = { 0 };
    ui_push(&ui, "Phase 1: both panels from one process");
    ui_push(&ui, "top: GBM + EGL (the surface libmpv will use)");
    ui_push(&ui, "bottom: CPU into DRM dumb buffers");
    ui_push(&ui, "");
    ui_push(&ui, "Buttons pressed appear here.");
    ui_push(&ui, "start is bananas, cd / and make it");

    struct pollfd pfds[1 + INPUT_MAX_DEV];
    KeyEvent keys[INPUT_EVENTS_MAX];
    TouchEvent touch = { 0 };
    DrawList dl;
    dl_reset(&dl);

    int frame = 0;
    bool dirty = true;
    int64_t last_clock = 0;

    while (!g_quit) {
        int nfds = 0;
        pfds[nfds].fd = fd; pfds[nfds].events = POLLIN; nfds++;
        nfds += input_pollfds(&in, pfds + 1, INPUT_MAX_DEV);

        int timeout = dirty ? 0 : 200;
        int rc = poll(pfds, (nfds_t)nfds, timeout);
        if (rc < 0 && errno != EINTR) { log_line("poll: %s", strerror(errno)); break; }

        int64_t t0 = now_ms();
        int nk = input_dispatch(&in, pfds + 1, keys, INPUT_EVENTS_MAX, &touch);

        for (int i = 0; i < nk; i++) {
            if (!keys[i].down) continue;
            log_line("key %s", key_name(keys[i].key));
            if (keys[i].key == BK_HOME) { g_quit = 1; break; }
            if (!keys[i].repeat) {
                ui.n = 6;                                   /* keep the fixed lines, drop the old presses */
                ui_push(&ui, "pressed %s", key_name(keys[i].key));
                dirty = true;
            }
        }

        if (g_redraw) { dirty = true; g_redraw = 0; }
        int64_t now = now_ms();
        if (now - last_clock > 1000) { last_clock = now; dirty = true; }

        if (dirty) {
            if (touch.down) dirty = true;                   /* keep the tap readout on screen */
            ui_build(&ui, &dl, &touch, &top, &bot, (int)(now - t0));
            bottom_present(&bottom, fd, &bot, &dl, NULL, 0);

            /* Top screen: a colour that cycles, so it is obvious which panel is which. Phase 3 replaces this
             * with libmpv rendering into the same surface. */
            float p = (float)(frame % 180) / 60.0f;
            float r = p < 1 ? 1.0f - p : 0.0f;
            float g = (p >= 1 && p < 2) ? p - 1.0f : (p >= 2 ? 3.0f - p : 0.0f);
            float b = p < 1 ? p : (p >= 2 ? p - 2.0f : 0.0f);
            glClearColor(r * 0.35f, g * 0.35f, b * 0.35f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            gbm_egl_swap(fd, &egl);

            dirty = false;
            frame++;
        }
    }

    log_line("quitting");
    input_close(&in);
    bottom_fini(&bottom, fd);
    gbm_egl_fini(&egl);
    font_free(fnt);
    drmDropMaster(fd);
    drm_close(fd);
    return 0;
}
