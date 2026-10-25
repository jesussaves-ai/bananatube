/* main.c -- BananaTube.
 *
 * The bottom panel is the client: a search field, an on-screen keyboard and a grid of results with their
 * pictures. The top one is where the video plays; Phase 3 puts libmpv on the EGL surface that is already set
 * up here, so for now it shows that there is no video yet.
 *
 * banana-ui hands the DRM master over by exiting; bananad starts us and waits. HOME quits, and bananad brings
 * the menu back -- the handover BananaDS uses.
 */
#include "conf.h"
#include "display/render.h"
#include "display/bottom.h"
#include "display/drm.h"
#include "display/gbm_egl.h"
#include "font.h"
#include "input/evdev.h"
#include "net/resolver.h"
#include "net/worker.h"
#include "ui/view.h"
#ifdef BANANATUBE_MPV
#include "display/gles_blit.h"
#include "player/player.h"
#include "player/osd.h"
#endif
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

/* The fonts BananaOS ships (ui/Makefile installs them under /usr/share/banana). Inter for the titles, which
 * are small and want to stay readable; Pixelify Sans for the clock, so it looks like the menu. */
#define TTF_TITLE "/usr/share/banana/banana/fonts/Inter-Regular.ttf"
#define TTF_BOLD  "/usr/share/banana/banana/fonts/Inter-SemiBold.ttf"
#define TTF_PIXEL "/usr/share/banana/rnds/fonts/PixelifySans-Medium.ttf"

/* On the handheld the fonts are where BananaOS installs them. On a computer (the --shot path, a test) they are
 * in a checkout's ui/assets, so BANANATUBE_FONTS names that directory. */
static const char *first_font(const char *env_name, const char *device, const char *local)
{
    const char *dir = getenv("BANANATUBE_FONTS");
    static char buf[512];
    if (dir && *dir) {
        snprintf(buf, sizeof buf, "%s/%s", dir, local);
        if (access(buf, R_OK) == 0) return buf;
    }
    if (access(device, R_OK) == 0) return device;
    return device;                       /* missing: font_init falls back to whichever loaded */
}

#define CONF_PATH_DEFAULT "/data/banana/bananatube.conf"

/* the headless picture of the bottom screen, defined at the end of this file */
static int shot(const char *path, bool keyboard, bool with_results, bool plus, bool playing);

/* ---- the pictures ---------------------------------------------------------------------------------------
 * One slot per thumbnail, in ordinary memory: the bottom panel is drawn by the CPU, so a decoded picture is a
 * buffer the renderer blits, not a texture to upload. 12 slots at 320x180 RGB565 is 1.4 MB. */
static void tex_store(BottomTex *slots, int n, int slot, uint8_t *pixels, int w, int h)
{
    if (slot < 0 || slot >= n) { free(pixels); return; }
    free((void *)slots[slot].pixels);
    slots[slot].pixels = pixels;
    slots[slot].w = w;
    slots[slot].h = h;
}

static void tex_free_all(BottomTex *slots, int n)
{
    for (int i = 0; i < n; i++) {
        free((void *)slots[i].pixels);
        slots[i].pixels = NULL;
        slots[i].w = slots[i].h = 0;
    }
}

/* Ask for the picture of every tile on screen that has not got one: what draws the grid is what decides what
 * to fetch, so scrolling fills in behind itself. */
static void request_visible_thumbs(AppState *st, ThumbCache *cache, ThumbWorker *worker)
{
    int vis[RESULTS_MAX];
    int n = results_visible(&st->results, st->view_h, vis, RESULTS_MAX);
    for (int k = 0; k < n; k++) {
        int i = vis[k];
        const SearchResult *s = &st->results.item[i];
        if (!s->id[0] || st->results.tex_slot[i] != IMAGE_NONE) continue;

        int have = thumb_touch(cache, s->id);
        if (have >= 0) { st->results.tex_slot[i] = have; continue; }

        int slot = thumb_slot_for(cache, s->id);        /* reserves (and may evict) before the fetch */
        if (slot < 0) continue;
        if (!thumb_worker_request(worker, slot, s->id)) {
            thumb_forget(cache, slot);                  /* already queued: the flag came from the worker */
            continue;
        }
    }
}

/* Take finished pictures: keep their slot, drop the tile they displaced, and mark the frame dirty. */
static void drain_thumbs(AppState *st, ThumbCache *cache, ThumbWorker *worker, BottomTex *slots, int nslots)
{
    ThumbDone done[DONE_MAX];
    int n = thumb_worker_drain(worker, done, (int)(sizeof done / sizeof done[0]));
    for (int i = 0; i < n; i++) {
        int slot = done[i].slot;
        if (!done[i].pixels) {                          /* no picture: let the slot be tried again */
            thumb_forget(cache, slot);
            continue;
        }
        for (int k = 0; k < st->results.count; k++)
            if (st->results.tex_slot[k] == slot && strcmp(st->results.item[k].id, done[i].video_id) != 0)
                st->results.tex_slot[k] = IMAGE_NONE;
        tex_store(slots, nslots, slot, done[i].pixels, done[i].w, done[i].h);
        results_set_thumb(&st->results, done[i].video_id, slot);
        st->dirty = true;
    }
}

int main(int argc, char **argv)
{
    signal(SIGUSR1, on_usr1);
    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);

    const char *top_name = getenv("BANANA_TOP");
    if (!top_name || !*top_name) top_name = "DSI-2";
    const char *touch_hint = getenv("BANANA_TOUCH");
    const char *conf_path = getenv("BANANATUBE_CONF");
    if (!conf_path || !*conf_path) conf_path = CONF_PATH_DEFAULT;

    log_line("starting: top=%s touch=%s", top_name, touch_hint ? touch_hint : "(auto)");

    if (argc > 1 && strcmp(argv[1], "--shot") == 0) {
        const char *out = argc > 2 ? argv[2] : "/tmp/bananatube.ppm";
        bool kb = false, empty = false, plus = false, playing = false;
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "keyboard") == 0) kb = true;
            else if (strcmp(argv[i], "empty") == 0) empty = true;
            else if (strcmp(argv[i], "plus") == 0) plus = true;
            else if (strcmp(argv[i], "playing") == 0) playing = true;
        }
        return shot(out, kb, !empty, plus, playing);
    }

    Conf conf;
    if (!conf_load(&conf, conf_path))
        log_line("no settings at %s: using the defaults", conf_path);
    /* a query on the command line is what the menu passes when it starts us for a search already typed */
    const char *initial = argc > 1 ? argv[1] : NULL;

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

    FontCtx *fnt = font_init(first_font("BANANATUBE_FONTS", TTF_TITLE, "banana/fonts/Inter-Regular.ttf"),
                             first_font("BANANATUBE_FONTS", TTF_BOLD, "banana/fonts/Inter-SemiBold.ttf"),
                             first_font("BANANATUBE_FONTS", TTF_PIXEL, "rnds/fonts/PixelifySans-Medium.ttf"));
    if (!fnt) log_line("no fonts: the bottom screen will have no text");
    bottom_set_font(&bottom, fnt);

#ifdef BANANATUBE_MPV
    /* The player renders into the top panel's EGL context, which gbm_egl_init left current on this thread. */
    PlayerGl pgl = {
        .egl_display = egl.egl_display,
        .egl_context = egl.egl_context,
        .egl_surface = egl.egl_surface,
        .get_proc_address = (void *(*)(const char *))eglGetProcAddress,
        .width = top.w,
        .height = top.h,
    };
    Player *player = player_create(conf.hwdec, 200);
    if (player && !player_attach_render(player, &pgl)) {
        log_line("no render context: the top screen will stay dark");
        player_destroy(player);
        player = NULL;
    }
    if (!player) log_line("no player: the top screen will stay dark");
    Osd osd;
    if (!osd_init(&osd, top.w, (float)top.w / SCREEN_W)) log_line("no OSD buffer");
    if (player) blit_init();
#endif

    InputCtx in;
    input_init(&in, touch_hint);

    AppState ui;
    ui_init(&ui);
    if (initial && *initial) {
        str_copy(ui.query, sizeof ui.query, initial);
        ui.view = VIEW_RESULTS;
        ui.searching = true;
    }

    SearchJob *search = search_job_start(&conf);
    ThumbWorker *thumbs = thumb_worker_start(2);
    ThumbCache cache;
    thumb_init(&cache, conf.cache_size);
    static BottomTex pics[THUMB_MAX_SLOTS];
    if (initial && *initial && search) search_job_begin(search, ui.query);

    struct pollfd pfds[1 + INPUT_MAX_DEV];
    KeyEvent keys[INPUT_EVENTS_MAX];
    TouchEvent touch = { 0 };
    DrawList dl;
    dl_reset(&dl);

    int64_t last_spin = 0;
    int64_t last_clock = 0;
    bool dirty = true;

    while (!g_quit) {
        int nfds = 0;
        pfds[nfds].fd = fd; pfds[nfds].events = POLLIN; nfds++;
        nfds += input_pollfds(&in, pfds + 1, INPUT_MAX_DEV);
#ifdef BANANATUBE_MPV
        int wake_at = -1;
        if (player) { wake_at = nfds; pfds[nfds].fd = player_wake_fd(player); pfds[nfds].events = POLLIN; nfds++; }
#endif

        /* A playing video sets the pace: mpv's pipe wakes us with each frame. */
        bool playing = ui.playing.active;
        int timeout = playing ? 1000 : ((ui.searching || thumb_worker_pending(thumbs)) ? 100 : (dirty ? 0 : 300));
        int rc = poll(pfds, (nfds_t)nfds, timeout);
        if (rc < 0 && errno != EINTR) { log_line("poll: %s", strerror(errno)); break; }
#ifdef BANANATUBE_MPV
        if (wake_at >= 0 && (pfds[wake_at].revents & POLLIN)) player_wake_drain(player);
#endif

        int nk = input_dispatch(&in, pfds + 1, keys, INPUT_EVENTS_MAX, &touch);

        /* a tap: the keyboard's keys, or a tile */
        if (touch.down && ui.view == VIEW_KEYBOARD) {
            char ch[8] = "";
            size_t room = sizeof ui.query - strlen(ui.query) - 1;
            switch (kb_tap(&ui.kb, touch.x, touch.y, ch, sizeof ch)) {
            case KB_ACTION_CHAR:   if (room >= strlen(ch)) strncat(ui.query, ch, room); ui.dirty = true; break;
            case KB_ACTION_SPACE:  if (room >= 1) strncat(ui.query, " ", room); ui.dirty = true; break;
            case KB_ACTION_DELETE: ui_key(&ui, BK_B); break;
            case KB_ACTION_LAYER:  ui_key(&ui, BK_X); break;
            case KB_ACTION_SUBMIT: ui_key(&ui, BK_START); break;
            case KB_ACTION_CLOSE:  ui_key(&ui, BK_SELECT); break;
            default:               ui.dirty = true; break;
            }
        } else if (touch.down && ui.view == VIEW_RESULTS) {
            int hit = results_hit(&ui.results, ui.view_h, ui.view_x, ui.view_y, ui.view_w, touch.x, touch.y);
            if (hit >= 0) {
                while (ui.results.sel != hit && results_move(&ui.results, hit > ui.results.sel ? BK_RIGHT : BK_LEFT)) {}
                /* a tap on the selected tile opens it, as tapping a list item twice does in the menu */
                if (hit == ui.results.sel) { ui.dirty = true; ui_key(&ui, BK_A); }
                results_scroll_into_view(&ui.results, ui.view_h);
            }
        }

        for (int i = 0; i < nk && !g_quit; i++) {
            if (!keys[i].down) continue;
            log_line("key %s", key_name(keys[i].key));
            switch (ui_key(&ui, keys[i].key)) {
            case UI_QUIT: g_quit = 1; break;
            case UI_SEARCH:
                if (!search || !search_job_begin(search, ui.query))
                    ui_set_status(&ui, "A search is already running.");
                break;
            case UI_PLAY: {
                const SearchResult *r = &ui.results.item[ui.results.sel];
                ui_set_status(&ui, "Resolving\xE2\x80\xA6");
                StreamUrl s;
                ResolveStatus rs = resolver_resolve(&conf, r->id, &s);
#ifdef BANANATUBE_MPV
                if (rs == RESOLVE_OK && player && player_open(player, &s)) {
                    ui_set_playing(&ui, r->title, r->channel);
                    ui.dirty = true;
                    break;
                }
                if (rs == RESOLVE_OK && !player) rs = RESOLVE_ALL_FAILED;
#endif
                ui_set_status(&ui, resolve_status_text(rs));
                break;
            }
#ifdef BANANATUBE_MPV
            case UI_STOP:
                if (player) player_close(player);
                ui_set_playing_stopped(&ui, "");
                break;
            case UI_PAUSE:
                if (player) player_toggle_pause(player);
                ui.dirty = true;
                break;
            case UI_SEEK_BACK:    if (player) player_seek(player, -10.0); break;
            case UI_SEEK_FORWARD: if (player) player_seek(player,  10.0); break;
#endif
            case UI_THUMBS:
                request_visible_thumbs(&ui, &cache, thumbs);
                break;
            case UI_NONE:
            default:
                break;
            }
        }

        /* the workers' answers */
        SearchResult found[RESULTS_MAX];
        int nfound = 0;
        char message[160];
        if (search && search_job_take(search, found, RESULTS_MAX, &nfound, message, sizeof message)) {
            ui_set_results(&ui, found, nfound, nfound ? "" : message);
            request_visible_thumbs(&ui, &cache, thumbs);
        }
        drain_thumbs(&ui, &cache, thumbs, pics, THUMB_MAX_SLOTS);

        if (g_redraw) { dirty = true; g_redraw = 0; }

        int64_t now = now_ms();
        if (now - last_clock > 1000) { last_clock = now; dirty = true; }
        if (ui.searching && now - last_spin > 100) {         /* the spinner keeps turning */
            last_spin = now;
            ui.spin += 1.0f / 8.0f;
            dirty = true;
        }

        /* The video keeps its own clock: render whenever mpv says there is a frame, whatever the UI is doing. */
#ifdef BANANATUBE_MPV
        if (player) {
            player_tick(player);
            if (player_active(player)) {
                ui_set_playing_state(&ui, player_position(player), player_duration(player),
                                     player_paused(player), player_buffering(player),
                                     player_width(player), player_height(player));
                if (player_eof(player)) {
                    player_close(player);
                    ui_set_playing_stopped(&ui, "Finished.");
                }
            } else if (ui.playing.active) {
                /* it stopped under us: an error, or the stream ended */
                const char *why = player_error(player);
                ui_set_playing_stopped(&ui, why && *why ? why : "Stopped.");
            }
            if (player_frame_ready(player) && player_active(player)) {
                player_render(player);

                float ospin = (float)((now / 120) % 8) / 8.0f;
                const SearchResult *r = ui.results.count ? &ui.results.item[ui.results.sel] : NULL;
                osd_update(&osd, fnt, player, r ? r->title : NULL,
                           player_error(player), player_paused(player), ospin);
                osd_draw(&osd, top.w, top.h);

                gbm_egl_swap(fd, &egl);
                dirty = true;                 /* the bottom screen may be showing the position */
            }
        }
#endif

        if (dirty) {
            ui_draw(&ui, &dl, pics, THUMB_MAX_SLOTS);
            bottom_present(&bottom, fd, &bot, &dl, pics, THUMB_MAX_SLOTS, (float)bot.w / SCREEN_W);

            /* With no video, the top panel is a dark placeholder: a slow pulse, so it is obvious which panel is
             * which and that the app is alive. */
#ifdef BANANATUBE_MPV
            if (!(player && player_active(player)))
#endif
            {
                float p = (float)((now / 40) % 60) / 60.0f;
                float k = 0.045f + 0.015f * p;
                glClearColor(k * 0.9f, k, k * 1.25f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                gbm_egl_swap(fd, &egl);
            }

            dirty = false;
        }
    }

    log_line("quitting");
#ifdef BANANATUBE_MPV
    if (player) { player_destroy(player); player = NULL; }
    osd_free(&osd);
    blit_fini();
#endif
    tex_free_all(pics, THUMB_MAX_SLOTS);
    thumb_worker_stop(thumbs);
    search_job_stop(search);
    input_close(&in);
    bottom_fini(&bottom, fd);
    gbm_egl_fini(&egl);
    font_free(fnt);
    drmDropMaster(fd);
    drm_close(fd);
    return 0;
}

/* ---- a headless picture of the bottom screen -------------------------------------------------------------
 * `bananatube --shot <file.ppm> [results|empty|keyboard] [plus]` renders the client into a PPM with no DRM,
 * no EGL and no panels (plus: the RG DS Plus's 1024x768 at 1.6),
 * with a made-up set of results. It is how the layout is looked at on a machine that has none, and what a CI
 * run can diff. The pictures are flat colours: no network is involved. */
static int shot(const char *path, bool keyboard, bool with_results, bool plus, bool playing)
{
    AppState ui;
    ui_init(&ui);
    ui.theme = ui_theme();
    ui.view = keyboard ? VIEW_KEYBOARD : VIEW_RESULTS;
    ui.spin = 0.4f;
    if (with_results && !playing) str_copy(ui.query, sizeof ui.query, "banana theme");

    static BottomTex pics[THUMB_MAX_SLOTS];
    static uint8_t pic_a[THUMB_W * THUMB_H * 2], pic_b[THUMB_W * THUMB_H * 2];
    for (int i = 0; i < THUMB_W * THUMB_H; i++) {
        uint16_t a = (uint16_t)(((i * 7) % 32) << 11 | (12 << 5) | 6);
        uint16_t c = (uint16_t)((6 << 11) | (((i * 5) % 64) << 5) | 20);
        pic_a[i * 2] = (uint8_t)(a >> 8); pic_a[i * 2 + 1] = (uint8_t)(a & 0xff);
        pic_b[i * 2] = (uint8_t)(c >> 8); pic_b[i * 2 + 1] = (uint8_t)(c & 0xff);
    }
    pics[0].pixels = pic_a; pics[0].w = THUMB_W; pics[0].h = THUMB_H;
    pics[1].pixels = pic_b; pics[1].w = THUMB_W; pics[1].h = THUMB_H;

    if (playing) {
        ui_set_playing(&ui, "Making a theme for BananaOS, start to finish", "Banana Dev");
        ui_set_playing_state(&ui, 4 * 60 + 12, 12 * 60 + 34, false, false, 854, 480);
        ui.playing.buffering = false;
    } else if (with_results) {
        SearchResult r[8];
        memset(r, 0, sizeof r);
        const char *titles[8] = {
            "Making a theme for BananaOS, start to finish",
            "BananaDS runs Golden Sun: Dark Dawn",
            "The RG DS Plus, one year on",
            "RetroArch shaders on a 1 GB handheld",
            "How the two panels get their picture",
            "A tour of the theme editor",
            "Why there is no browser in BananaOS",
            "Booting from a card in 3.9 seconds",
        };
        const char *chans[8] = { "Banana Dev", "ROCKNIX", "Retro Game Corps", "Banana Dev",
                                 "Banana Dev", "Majirefy", "Banana Dev", "Banana Dev" };
        for (int i = 0; i < 8; i++) {
            str_copy(r[i].id, sizeof r[i].id, i == 0 ? "dQw4w9WgXcQ" : (i == 1 ? "bananaDS001" : "vid0000000"));
            if (i > 1) snprintf(r[i].id, sizeof r[i].id, "vid%08d", i);
            str_copy(r[i].title, sizeof r[i].title, titles[i]);
            str_copy(r[i].channel, sizeof r[i].channel, chans[i]);
            r[i].duration_sec = 60 * (4 + i * 7) + 13;
            r[i].views = (i == 3) ? -1 : (long long)(1234 * (i + 1)) * 37;
            r[i].live = (i == 6);
        }
        ui_set_results(&ui, r, 8, "");
        ui.results.tex_slot[0] = 0;
        ui.results.tex_slot[1] = 1;
        ui.results.sel = 3;
        results_scroll_into_view(&ui.results, ui.view_h);
    }

    DrawList dl;
    dl_reset(&dl);
    ui_draw(&ui, &dl, pics, THUMB_MAX_SLOTS);

    if (getenv("BANANATUBE_DUMP")) {
        const char *kind[] = { "rect", "text", "image" };
        for (int i = 0; i < dl.n; i++) {
            const DrawOp *op = &dl.op[i];
            if (op->kind == OP_IMAGE || (op->kind == OP_TEXT && i < 30))
                printf("op %3d %-5s box %4d,%4d %3dx%3d image %2d fit %d text \"%s\"\n",
                       i, kind[op->kind], op->x, op->y, op->w, op->h, op->image, op->fit, op->text);
        }
    }

    FontCtx *fnt = font_init(first_font("BANANATUBE_FONTS", TTF_TITLE, "banana/fonts/Inter-Regular.ttf"),
                             first_font("BANANATUBE_FONTS", TTF_BOLD, "banana/fonts/Inter-SemiBold.ttf"),
                             first_font("BANANATUBE_FONTS", TTF_PIXEL, "rnds/fonts/PixelifySans-Medium.ttf"));
    if (!fnt) log_line("shot: no fonts, the picture will have no text");

    /* the panel to draw for: 640x480 on the RG DS, the Plus's 1024x768 at 1.6 with "plus" */
    int w = SCREEN_W, h = SCREEN_H;
    float scale = 1.0f;
    if (plus) { w = 1024; h = 768; scale = 1.6f; }
    uint32_t *canvas = calloc((size_t)w * h, sizeof(uint32_t));
    if (!canvas) { font_free(fnt); return 1; }
    render_full(canvas, w, h, fnt, &dl, pics, THUMB_MAX_SLOTS, scale);

    FILE *f = fopen(path, "wb");
    if (!f) { log_line("shot: cannot write %s", path); free(canvas); font_free(fnt); return 1; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint8_t px[3] = { (uint8_t)(canvas[i] >> 16), (uint8_t)(canvas[i] >> 8), (uint8_t)canvas[i] };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    log_line("shot: %s (%d draws)", path, dl.n);
    free(canvas);
    font_free(fnt);
    return 0;
}
