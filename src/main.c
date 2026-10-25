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
#include "history.h"
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
/* Which of the two panels a shot is of. The app draws the same view onto whichever one it belongs to, so a
 * picture has to say which -- and "both" is the pair stacked, the way the two screens sit on the handheld. */
typedef enum { SHOT_VIEW = 0, SHOT_TOP, SHOT_BOTTOM, SHOT_BOTH } ShotPanel;

static int shot(const char *path, bool keyboard, bool with_results, bool plus, bool playing, bool show_history,
                ShotPanel panel);
static FontCtx *shot_fonts(void);
static bool save_panels(const char *path, AppState *ui, FontCtx *fnt, BottomTex *pics, bool plus, ShotPanel panel);

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

/* The history sits beside the settings, on the card. */
static void history_path(const char *conf_path, char *out, size_t cap)
{
    str_copy(out, cap, conf_path);
    char *slash = strrchr(out, '/');
    if (slash) slash[1] = '\0';
    else       out[0] = '\0';
    strncat(out, "bananatube-history.json", cap - strlen(out) - 1);
}

#ifdef BANANATUBE_MPV
/* Resolve a video and put it on the top panel. `resume_at` is where the history says it got to. */
static bool start_video(AppState *ui, const Conf *conf, Player *player, History *hist,
                        const char *id, const char *title, const char *channel, double resume_at)
{
    if (!player || !id || !*id) return false;

    StreamUrl s;
    ResolveStatus rs = resolver_resolve(conf, id, &s);
    if (rs != RESOLVE_OK) {
        ui_set_status(ui, resolve_status_text(rs));
        return false;
    }

    player_set_start(player, resume_at);
    if (!player_open(player, &s)) {
        ui_set_status(ui, player_error(player));
        return false;
    }

    history_touch(hist, id, title, channel, s.height > 0 ? 0 : -1);
    ui_set_playing(ui, title, channel);
    ui->playing.quality = (int)conf->quality;
    ui->playing.volume = player_volume(player);
    ui->playing.video_on = true;
    ui->dirty = true;
    return true;
}

/* Where the video is, into the history. `write_file` also puts it on the card (a pause, a stop, an exit);
 * otherwise it only marks the state, and the file is written every half minute. */
static void save_progress(AppState *ui, Player *player, History *hist, const char *conf_path, bool write_file)
{
    if (!player || !ui->playing.active || !hist) return;
    if (ui->results.count == 0) return;
    const SearchResult *r = &ui->results.item[ui->results.sel];
    history_progress(hist, r->id, player_position(player));
    if (write_file) {
        char path[512];
        history_path(conf_path, path, sizeof path);
        history_save(hist, path);
    }
}

/* X while a video plays: 360p -> 480p -> 720p, and back. It re-resolves at the new size and carries on from
 * where it was, which is the only honest way to change quality mid-stream. */
static bool switch_quality(AppState *ui, Conf *conf, Player *player, History *hist, const char *conf_path)
{
    switch ((int)conf->quality) {
    case QUALITY_360: conf->quality = QUALITY_480; break;
    case QUALITY_480: conf->quality = QUALITY_720; break;
    default:          conf->quality = QUALITY_360; break;
    }
    conf_save(conf, conf_path);

    if (!player || !ui->playing.active || ui->results.count == 0) {
        ui->playing.quality = (int)conf->quality;
        ui->dirty = true;
        return true;
    }
    const SearchResult *r = &ui->results.item[ui->results.sel];
    double at = player_position(player);
    save_progress(ui, player, hist, conf_path, true);
    return start_video(ui, conf, player, hist, r->id, r->title, r->channel, at);
}
#endif

/* What this program costs and what the device has left. On a 1 GB handheld a frame that takes 200 ms may
 * have nothing to do with drawing: the log has to be able to say which, or the next guess is just as blind. */
static void log_memory(const char *when)
{
    long rss = 0, avail = 0;
    FILE *f = fopen("/proc/self/status", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f))
            if (sscanf(line, "VmRSS: %ld kB", &rss) == 1) break;
        fclose(f);
    }
    f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f))
            if (sscanf(line, "MemAvailable: %ld kB", &avail) == 1) break;
        fclose(f);
    }
    log_line("memory (%s): bananatube %ld kB resident, %ld kB left on the device", when, rss, avail);
}


/* ---- testing without the handheld -----------------------------------------------------------------------
 * The real client with no DRM, no EGL, no decoder and no input devices: the same AppState, the same ui_key,
 * the same draw lists, the same backends over the network. What it cannot test is the panels, the GPU and the
 * video unit -- everything else about the app is here, and it is the only way to iterate on a UI bug without
 * carrying a card to a handheld and back.
 *
 *   bananatube --search "banana theme" [--out DIR] [--plus] [--frames]
 *   bananatube --resolve <videoId>
 *   bananatube --keys "select,down,A,start" [--query "banana theme"] [--out DIR] [--plus]
 */
static const char *env_conf_path(void)
{
    const char *p = getenv("BANANATUBE_CONF");
    return (p && *p) ? p : "bananatube.conf";
}

static const char *arg_value(int argc, char **argv, const char *name)
{
    for (int i = 2; i < argc - 1; i++)
        if (strcmp(argv[i], name) == 0) return argv[i + 1];
    return NULL;
}

static const char *action_name(UiAction a)
{
    switch (a) {
    case UI_NONE:           return "-";
    case UI_PLAY:           return "play";
    case UI_SEARCH:         return "search";
    case UI_STOP:           return "stop";
    case UI_PAUSE:          return "pause";
    case UI_SEEK_BACK:      return "seek back";
    case UI_SEEK_FORWARD:   return "seek forward";
    case UI_VOLUME_UP:      return "volume up";
    case UI_VOLUME_DOWN:    return "volume down";
    case UI_QUALITY:        return "quality";
    case UI_NEXT_VIDEO:     return "next";
    case UI_PREV_VIDEO:     return "previous";
    case UI_THUMBS:         return "thumbs";
    case UI_SAVE_PROGRESS:  return "save progress";
    case UI_HISTORY:        return "history";
    case UI_QUIT:           return "quit";
    default:                return "?";
    }
}

static const char *view_name(View v)
{
    switch (v) {
    case VIEW_RESULTS:  return "results";
    case VIEW_KEYBOARD: return "keyboard";
    case VIEW_PLAYING:  return "playing";
    case VIEW_HISTORY:  return "history";
    default:            return "?";
    }
}

/* A search over the real network, printed as a table. Exit status: 0 with results, 1 with none, 2 when
 * nothing could be reached -- so a script can tell the three apart. */
static int cli_search(int argc, char **argv)
{
    const char *query = argv[2];
    const char *out = arg_value(argc, argv, "--out");
    bool plus = false, frames = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--plus") == 0) plus = true;
        if (strcmp(argv[i], "--frames") == 0) frames = true;
    }

    Conf conf;
    const char *conf_path = env_conf_path();
    if (!conf_load(&conf, conf_path)) log_line("no settings at %s: using the defaults", conf_path);
    log_line("backend = %s, instance = %s", conf_backend_name(conf.backend), conf.instance[0] ? conf.instance : "(built-in list)");

    SearchResult found[SEARCH_RESULTS_MAX];
    char message[160] = "";
    int64_t t0 = now_ms();
    int n = backend_search_auto(&conf, query, found, SEARCH_RESULTS_MAX, message, sizeof message);
    int64_t ms = now_ms() - t0;

    if (n < 0) { printf("search \"%s\": FAILED after %lld ms -- %s\n", query, (long long)ms, message); return 2; }
    printf("search \"%s\": %d results in %lld ms\n", query, n, (long long)ms);
    for (int i = 0; i < n; i++) {
        char dur[16], views[24];
        fmt_duration(found[i].duration_sec, dur, sizeof dur);
        fmt_views(found[i].views, views, sizeof views);
        printf("  %2d  %-11s %8s  %-8s  %s  [%s]\n", i + 1, found[i].id, dur, views,
               found[i].title, found[i].channel);
    }
    if (n == 0) printf("  (%s)\n", message);
    if (!frames) return n > 0 ? 0 : 1;

    AppState ui;
    ui_init(&ui);
    ui.theme = ui_theme();
    str_copy(ui.query, sizeof ui.query, query);
    ui_set_results(&ui, found, n > 0 ? n : 0, n > 0 ? "" : message);

    /* The pictures the handheld would have: a few seconds of the real worker thread, off the real network. */
    static BottomTex pics[THUMB_MAX_SLOTS];
    ThumbCache cache;
    thumb_init(&cache, conf.cache_size);
    ThumbWorker *worker = thumb_worker_start(3);
    if (worker) {
        request_visible_thumbs(&ui, &cache, worker);
        for (int i = 0; i < 40; i++) {
            struct timespec ts = { 0, 100 * 1000 * 1000 };
            nanosleep(&ts, NULL);
            drain_thumbs(&ui, &cache, worker, pics, THUMB_MAX_SLOTS);
        }
        thumb_worker_stop(worker);
    }
    FontCtx *fnt = shot_fonts();
    char path[600];
    snprintf(path, sizeof path, "%s/search.ppm", out ? out : "/tmp");
    save_panels(path, &ui, fnt, pics, plus, SHOT_BOTH);
    font_free(fnt);
    tex_free_all(pics, THUMB_MAX_SLOTS);
    return n > 0 ? 0 : 1;
}

/* One video's stream URL, resolved the way pressing A does. This is the half of playing that the UI cannot
 * show: if this fails, no player would have worked either, and the log here says which part failed. */
static int cli_resolve(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: bananatube --resolve <videoId>\n"); return 2; }
    Conf conf;
    if (!conf_load(&conf, env_conf_path())) log_line("no settings: using the defaults");

    StreamUrl s;
    int64_t t0 = now_ms();
    ResolveStatus rs = resolver_resolve(&conf, argv[2], &s);
    int64_t ms = now_ms() - t0;

    printf("resolve %s: %s after %lld ms\n", argv[2], resolve_status_text(rs), (long long)ms);
    if (rs != RESOLVE_OK) return 2;
    printf("  %dx%d @%d fps, %s, %s\n", s.width, s.height, s.fps,
           s.vcodec[0] ? s.vcodec : "?", s.muxed ? "one stream" : "video + audio");
    printf("  url: %.72s%s\n", s.url, strlen(s.url) > 72 ? "..." : "");
    if (s.audio_url[0]) printf("  audio: %.72s...\n", s.audio_url);
    return 0;
}

/* Drives the real UI with a script of key names -- the same words the log prints -- and writes a frame after
 * every step. This is --shot for a sequence: it catches the state machine, the layout, the draw lists and the
 * actions, on a machine with no panels. */
static int cli_keys(int argc, char **argv)
{
    const char *script = argv[2];
    const char *out = arg_value(argc, argv, "--out");
    const char *query = arg_value(argc, argv, "--query");
    bool plus = false;
    for (int i = 2; i < argc; i++) if (strcmp(argv[i], "--plus") == 0) plus = true;

    Conf conf;
    const char *conf_path = env_conf_path();
    if (!conf_load(&conf, conf_path)) log_line("no settings at %s: using the defaults", conf_path);

    AppState ui;
    ui_init(&ui);
    ui.theme = ui_theme();
    static BottomTex pics[THUMB_MAX_SLOTS];

    if (query && *query) {
        SearchResult found[SEARCH_RESULTS_MAX];
        char message[160] = "";
        int n = backend_search_auto(&conf, query, found, SEARCH_RESULTS_MAX, message, sizeof message);
        str_copy(ui.query, sizeof ui.query, query);
        ui_set_results(&ui, found, n > 0 ? n : 0, n > 0 ? "" : message);
        printf("query \"%s\": %d results%s\n", query, n > 0 ? n : 0, n < 0 ? " (nothing reachable)" : "");
    }

    FontCtx *fnt = shot_fonts();
    char path[600];
    snprintf(path, sizeof path, "%s/00-start.ppm", out ? out : "/tmp");
    save_panels(path, &ui, fnt, pics, plus, SHOT_BOTH);

    char buf[512];
    str_copy(buf, sizeof buf, script);
    int step = 0;
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        while (*tok == ' ') tok++;
        BananaKey k = banana_key_from_name(tok);
        step++;
        if (k == BK_NONE) { printf("  %2d. \"%s\" is not a key name\n", step, tok); continue; }

        UiAction a = ui_key(&ui, k);
        printf("  %2d. %-8s -> %-14s view %s, %d results, sel %d, history %d\n", step, key_name(k),
               action_name(a), view_name(ui.view), ui.results.count, ui.results.sel,
               ui.history ? ui.history->count : 0);

        if (a == UI_SEARCH) {
            SearchResult found[SEARCH_RESULTS_MAX];
            char message[160] = "";
            int n = backend_search_auto(&conf, ui.query, found, SEARCH_RESULTS_MAX, message, sizeof message);
            ui_set_results(&ui, found, n > 0 ? n : 0, n > 0 ? "" : message);
            printf("        search \"%s\": %d%s\n", ui.query, n > 0 ? n : 0,
                   n < 0 ? " (nothing reachable)" : message);
        } else if (a == UI_PLAY) {
            /* what pressing A does past the UI: the resolve. No player here (that is the GPU and the panels). */
            if (ui.view == VIEW_HISTORY && ui.history && ui.history_sel < ui.history->count) {
                const HistoryItem *it = &ui.history->item[ui.history_sel];
                StreamUrl st;
                ResolveStatus rs = resolver_resolve(&conf, it->id, &st);
                printf("        \"%s\" -> %s\n", it->title, resolve_status_text(rs));
            } else if (ui.results.count) {
                const SearchResult *r = &ui.results.item[ui.results.sel];
                StreamUrl st;
                ResolveStatus rs = resolver_resolve(&conf, r->id, &st);
                printf("        \"%s\" -> %s (%dx%d %s)\n", r->title, resolve_status_text(rs),
                       st.width, st.height, st.vcodec);
                if (rs == RESOLVE_OK) ui_set_playing(&ui, r->title, r->channel);
            }
        } else if (a == UI_QUIT) {
            printf("        quit\n");
            break;
        } else if (a == UI_THUMBS) {
            printf("        (thumbnails would be fetched by the worker here)\n");
        }

        snprintf(path, sizeof path, "%s/%02d-%s.ppm", out ? out : "/tmp", step, key_name(k));
        save_panels(path, &ui, fnt, pics, plus, SHOT_BOTH);
        ui.dirty = false;
    }
    font_free(fnt);
    return 0;
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

    /* the modes that need no panels at all (docs/TESTING.md, "testing without the handheld") */
    if (argc > 1 && strcmp(argv[1], "--search") == 0)  return argc > 2 ? cli_search(argc, argv) : 2;
    if (argc > 1 && strcmp(argv[1], "--resolve") == 0) return cli_resolve(argc, argv);
    if (argc > 1 && strcmp(argv[1], "--keys") == 0)    return argc > 2 ? cli_keys(argc, argv) : 2;

    if (argc > 1 && strcmp(argv[1], "--shot") == 0) {
        const char *out = argc > 2 ? argv[2] : "/tmp/bananatube.ppm";
        bool kb = false, empty = false, plus = false, playing = false, history = false;
        ShotPanel panel = SHOT_VIEW;                 /* whichever panel carries this view's content */
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "keyboard") == 0) kb = true;
            else if (strcmp(argv[i], "empty") == 0) empty = true;
            else if (strcmp(argv[i], "plus") == 0) plus = true;
            else if (strcmp(argv[i], "playing") == 0) playing = true;
            else if (strcmp(argv[i], "history") == 0) history = true;
            else if (strcmp(argv[i], "top") == 0) panel = SHOT_TOP;
            else if (strcmp(argv[i], "bottom") == 0) panel = SHOT_BOTTOM;
            else if (strcmp(argv[i], "both") == 0) panel = SHOT_BOTH;
        }
        return shot(out, kb, !empty, plus, playing, history, panel);
    }

    Conf conf;
    if (!conf_load(&conf, conf_path))
        log_line("no settings at %s: using the defaults", conf_path);

    static History history;
    char hist_path[512];
    history_path(conf_path, hist_path, sizeof hist_path);
    if (history_load(&history, hist_path))
        log_line("history: %d entries from %s", history.count, hist_path);
    history_set_cap(&history, conf.history_size);      /* history_size in bananatube.conf */
    /* A search already typed on the command line: run-bananatube banana theme. The menu passes the entry it
     * found instead -- this app's marker file -- and that is not a query (util.c's arg_is_query). */
    const char *initial = NULL;
    for (int i = 1; i < argc && !initial; i++)
        if (arg_is_query(argv[i])) initial = argv[i];

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
    /* EGL on the top panel is for the video. Without it -- a machine with no GPU, QEMU's virtio device, Mesa
     * built for another SoC -- the client still browses: the top panel falls back to the same CPU path as the
     * bottom one (which is where the search field and the results live anyway), and only the video is lost.
     * Exiting here is what made the whole app untestable anywhere but the handheld. */
    bool have_egl = gbm_egl_init(fd, &top, &egl);
    if (!have_egl)
        log_line("no EGL on the top panel: the video cannot be shown, everything else runs");

    /* Both panels are drawn by the CPU here: the top one carries the app's content (the results, or, once a
     * video plays, mpv's frames through EGL), the bottom one the controls. Two of the same thing, because the
     * top panel is a dumb buffer while browsing and an EGL surface while playing, and only one of them owns
     * the CRTC at a time. */
    Bottom top_ui, bot_ui;
    if (!bottom_init(&top_ui, fd, &top) || !bottom_init(&bot_ui, fd, &bot)) {
        log_line("no screen buffers");
        drm_close(fd);
        return 1;
    }

    FontCtx *fnt = font_init(first_font("BANANATUBE_FONTS", TTF_TITLE, "banana/fonts/Inter-Regular.ttf"),
                             first_font("BANANATUBE_FONTS", TTF_BOLD, "banana/fonts/Inter-SemiBold.ttf"),
                             first_font("BANANATUBE_FONTS", TTF_PIXEL, "rnds/fonts/PixelifySans-Medium.ttf"));
    if (!fnt) log_line("no fonts: the bottom screen will have no text");
    bottom_set_font(&top_ui, fnt);
    bottom_set_font(&bot_ui, fnt);

#ifdef BANANATUBE_MPV
    log_memory("at startup");

    /* The player renders into the top panel's EGL context, which gbm_egl_init left current on this thread. */
    Player *player = NULL;
    if (have_egl) {
        PlayerGl pgl = {
            .egl_display = egl.egl_display,
            .egl_context = egl.egl_context,
            .egl_surface = egl.egl_surface,
            .get_proc_address = (void *(*)(const char *))eglGetProcAddress,
            .width = top.w,
            .height = top.h,
        };
        player = player_create(conf.hwdec, 200);
        if (player && !player_attach_render(player, &pgl)) {
            log_line("no render context: the top screen will stay dark");
            player_destroy(player);
            player = NULL;
        }
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
    ui_set_history(&ui, &history);
    if (initial && *initial) {
        str_copy(ui.query, sizeof ui.query, initial);
        ui.view = VIEW_RESULTS;
        ui.searching = true;
    } else if (history.query[0]) {
        /* come back to the last search, so a restart is not a blank screen */
        str_copy(ui.query, sizeof ui.query, history.query);
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
    DrawList dl_top, dl_bot;        /* ui_draw_top/_bot clear the list they are given */

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

        /* A tap becomes the same key presses a button makes, so one place decides what a key means -- and so
         * the actions those keys raise actually run. The old path called ui_key() for the keyboard's SEARCH key
         * and threw the UI_SEARCH it returned away, which is why tapping Search did nothing at all.
         *
         * The keyboard's letters are not keys, and DELETE and the layer switch keep their own handling (they
         * raise nothing), so only SUBMIT and CLOSE become key presses here. */
        if (touch.down && ui.view == VIEW_KEYBOARD) {
            char ch[8] = "";
            size_t room = sizeof ui.query - strlen(ui.query) - 1;
            BananaKey tapped = BK_NONE;
            switch (kb_tap(&ui.kb, touch.x, touch.y, ch, sizeof ch)) {
            case KB_ACTION_CHAR:   if (room >= strlen(ch)) strncat(ui.query, ch, room); ui.dirty = true; break;
            case KB_ACTION_SPACE:  if (room >= 1) strncat(ui.query, " ", room); ui.dirty = true; break;
            case KB_ACTION_DELETE: ui_key(&ui, BK_B); break;      /* its own path: whole characters, not bytes */
            case KB_ACTION_LAYER:  ui_key(&ui, BK_X); break;
            case KB_ACTION_SUBMIT: tapped = BK_START; break;
            case KB_ACTION_CLOSE:  tapped = BK_SELECT; break;
            default: break;
            }
            if (tapped != BK_NONE && nk < INPUT_EVENTS_MAX) {
                keys[nk].key = tapped; keys[nk].down = true; keys[nk].repeat = false;
                nk++;
            }
        } else if (touch.down && ui.view == VIEW_PLAYING) {
            /* The transport row is on the panel with the touchscreen, which is the whole reason the controls
             * are down there -- and until now it was drawn but not actually reachable by finger. */
            BananaKey tapped = BK_NONE;
            switch (ui_transport_hit(touch.x, touch.y)) {
            case TP_PREV:     tapped = BK_L1;    break;
            case TP_NEXT:     tapped = BK_R1;    break;
            case TP_PAUSE:    tapped = BK_START; break;
            case TP_VOL_DOWN: tapped = BK_DOWN;  break;
            case TP_VOL_UP:   tapped = BK_UP;    break;
            case TP_QUALITY:  tapped = BK_X;     break;
            case TP_STOP:     tapped = BK_B;     break;
            default: break;
            }
            if (tapped != BK_NONE && nk < INPUT_EVENTS_MAX) {
                keys[nk].key = tapped; keys[nk].down = true; keys[nk].repeat = false;
                nk++;
            }
        }

        for (int i = 0; i < nk && !g_quit; i++) {
            if (!keys[i].down) continue;
            log_line("key %s", key_name(keys[i].key));
            switch (ui_key(&ui, keys[i].key)) {
            case UI_QUIT: g_quit = 1; break;
            case UI_SEARCH:
                if (!search || !search_job_begin(search, ui.query)) {
                    ui_set_status(&ui, "A search is already running.");
                    break;
                }
                str_copy(history.query, sizeof history.query, ui.query);
                history_save(&history, hist_path);
                break;
            case UI_PLAY: {
                ui_set_status(&ui, "Resolving\xE2\x80\xA6");
                /* Resolving is a blocking call on this thread, so the frames that say "Resolving..." are drawn
                 * before it starts: otherwise the last picture stays on the panels for the second or two it
                 * takes and the app looks frozen, which is how a slow resolve first read from the handheld. */
                ui_draw_top(&ui, &dl_top, pics, THUMB_MAX_SLOTS);
                ui_draw_bot(&ui, &dl_bot, pics, THUMB_MAX_SLOTS);
                bottom_present(&top_ui, fd, &top, &dl_top, pics, THUMB_MAX_SLOTS, (float)top.w / SCREEN_W);
                bottom_present(&bot_ui, fd, &bot, &dl_bot, pics, THUMB_MAX_SLOTS, (float)bot.w / SCREEN_W);
                ui.dirty = false;
#ifdef BANANATUBE_MPV
                if (ui.view == VIEW_HISTORY) {
                    if (ui.history_sel >= 0 && ui.history_sel < history.count) {
                        const HistoryItem *it = &history.item[ui.history_sel];
                        /* carrying on where it stopped: what the watched list is for */
                        if (start_video(&ui, &conf, player, &history, it->id, it->title, it->channel, it->position))
                            break;
                    }
                } else if (ui.results.count > 0) {
                    const SearchResult *r = &ui.results.item[ui.results.sel];
                    const HistoryItem *seen = history_find(&history, r->id);
                    if (start_video(&ui, &conf, player, &history, r->id, r->title, r->channel,
                                    seen ? seen->position : 0))
                        break;
                }
                ui_set_status(&ui, player ? "Could not play that." : "No player: build with libmpv.");
#else
                ui_set_status(&ui, "No player: build with libmpv.");
#endif
                break;
            }
#ifdef BANANATUBE_MPV
            case UI_STOP:
                save_progress(&ui, player, &history, conf_path, true);
                if (player) player_close(player);
                ui_set_playing_stopped(&ui, "");
                break;
            case UI_PAUSE:
                if (player) player_toggle_pause(player);
                save_progress(&ui, player, &history, conf_path, true);
                ui.dirty = true;
                break;
            case UI_SEEK_BACK:    if (player) player_seek(player, -10.0); break;
            case UI_SEEK_FORWARD: if (player) player_seek(player,  10.0); break;
            case UI_VOLUME_UP:
                if (player) { player_set_volume(player, player_volume(player) + 5); ui_set_full_state(&ui, player_volume(player), ui.playing.quality, ui.playing.video_on, player_dropped(player)); }
                break;
            case UI_VOLUME_DOWN:
                if (player) { player_set_volume(player, player_volume(player) - 5); ui_set_full_state(&ui, player_volume(player), ui.playing.quality, ui.playing.video_on, player_dropped(player)); }
                break;
            case UI_QUALITY:
                if (!switch_quality(&ui, &conf, player, &history, conf_path))
                    ui_set_status(&ui, "Could not change quality.");
                ui.dirty = true;
                break;
            case UI_PREV_VIDEO:
            case UI_NEXT_VIDEO:
                if (ui.results.count > 0) {
                    bool next = (ui_key(&ui, BK_NONE) == UI_NONE);
                    (void)next;
                    if (ui.results.sel + 1 < ui.results.count && ui.playing.active) ui.results.sel++;
                    const SearchResult *r = &ui.results.item[ui.results.sel];
                    save_progress(&ui, player, &history, conf_path, true);
                    start_video(&ui, &conf, player, &history, r->id, r->title, r->channel, 0);
                }
                break;
            case UI_SAVE_PROGRESS:
                save_progress(&ui, player, &history, conf_path, true);
                break;
#endif
            case UI_THUMBS:
                request_visible_thumbs(&ui, &cache, thumbs);
                break;
            case UI_NONE:
            default:
                break;
            }
        }

#ifdef BANANATUBE_MPV
        /* The lid: with the sound carrying on and the picture off, the video unit stops working with it (which
         * is what an album with the panels shut wants). bananad still owns going to sleep: if Settings has the
         * lid set to Sleep, it suspends the device and the sound goes with it, which is the user's choice. */
        static bool lid_was_closed = false;
        if (in.lid_closed != lid_was_closed) {
            lid_was_closed = in.lid_closed;
            if (player && ui.playing.active && conf.audio_only_on_lid) {
                player_set_video(player, !in.lid_closed);
                ui_set_full_state(&ui, player_volume(player), ui.playing.quality, !in.lid_closed,
                                  player_dropped(player));
                save_progress(&ui, player, &history, conf_path, true);
                log_line("lid %s: video %s", in.lid_closed ? "shut" : "open", in.lid_closed ? "off" : "on");
            }
        }

        /* every half minute while playing, so a pull of the battery loses at most that */
        static int64_t last_save = 0;
        int64_t tick = now_ms();
        if (player && ui.playing.active && tick - last_save > 30000) {
            last_save = tick;
            save_progress(&ui, player, &history, conf_path, true);
        }
#endif

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
        static int64_t last_mem = 0;
        if (now - last_mem > 60000) { last_mem = now; log_memory("a minute in"); }
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
            int64_t t0 = now_ms();
            ui_draw_top(&ui, &dl_top, pics, THUMB_MAX_SLOTS);
            ui_draw_bot(&ui, &dl_bot, pics, THUMB_MAX_SLOTS);
            int64_t t1 = now_ms();

            bool video = false;
#ifdef BANANATUBE_MPV
            video = player && player_active(player);
#endif
            /* Who owns the top panel changed (the video started or stopped), so its damage diff is worthless:
             * the panel would otherwise keep mpv's last frame under the results, or the results under the
             * video, because the list it compares against is the one from the other mode. */
            static bool top_was_video = false;
            if (video != top_was_video) {
                bottom_invalidate(&top_ui);
                top_was_video = video;
                log_line("top panel: %s", video ? "the video" : "the screen");
            }
            if (!video)
                bottom_present(&top_ui, fd, &top, &dl_top, pics, THUMB_MAX_SLOTS, (float)top.w / SCREEN_W);
            int64_t t2 = now_ms();
            bottom_present(&bot_ui, fd, &bot, &dl_bot, pics, THUMB_MAX_SLOTS, (float)bot.w / SCREEN_W);
            int64_t t3 = now_ms();

            /* A frame that does not fit drops its tail draws. It looks like a half-drawn screen, and it is
             * worth a line rather than silence. */
            static int last_dropped = 0;
            int dropped = dl_top.dropped + dl_bot.dropped;
            if (dropped != last_dropped) {
                last_dropped = dropped;
                log_line("frame: %d draws did not fit in %d (DRAW_MAX): the tail of the frame was dropped",
                         dropped, DRAW_MAX);
            }

            /* A frame that takes a noticeable time, with what it did: how much was drawn, how many separate
             * rectangles were repainted and how many pixels that was (the copy into the uncached scanout
             * buffer is the only slow memory in the path, so the pixel count is the number that matters). */
            int64_t t4 = now_ms();
            if (t4 - t0 > 30)
                log_line("frame: %lld ms (lists %lld, top %lld, bottom %lld) -- %d+%d draws, "
                         "%d+%d damage rects, %ld+%ld px repainted of %d+%d", (long long)(t4 - t0),
                         (long long)(t1 - t0), (long long)(t2 - t1), (long long)(t3 - t2),
                         dl_top.n, dl_bot.n, top_ui.last_damage, bot_ui.last_damage,
                         top_ui.last_pixels, bot_ui.last_pixels, top.w * top.h, bot.w * bot.h);

            dirty = false;
        }
    }

    log_line("quitting");
#ifdef BANANATUBE_MPV
    save_progress(&ui, player, &history, conf_path, true);
    if (player) { player_destroy(player); player = NULL; }
    osd_free(&osd);
    blit_fini();
#endif
    history_save(&history, hist_path);
    tex_free_all(pics, THUMB_MAX_SLOTS);
    thumb_worker_stop(thumbs);
    search_job_stop(search);
    input_close(&in);
    bottom_fini(&top_ui, fd);
    bottom_fini(&bot_ui, fd);
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

/* The fonts the client uses, for a render with no panels. */
static FontCtx *shot_fonts(void)
{
    FontCtx *fnt = font_init(first_font("BANANATUBE_FONTS", TTF_TITLE, "banana/fonts/Inter-Regular.ttf"),
                             first_font("BANANATUBE_FONTS", TTF_BOLD, "banana/fonts/Inter-SemiBold.ttf"),
                             first_font("BANANATUBE_FONTS", TTF_PIXEL, "rnds/fonts/PixelifySans-Medium.ttf"));
    if (!fnt) log_line("shot: no fonts, the picture will have no text");
    return fnt;
}

/* Renders whichever panel(s) were asked for into a PPM. Both of them stacked in one file is the way the two
 * screens sit on the handheld, which is how the README's pictures are made. */
static bool save_panels(const char *path, AppState *ui, FontCtx *fnt, BottomTex *pics, bool plus, ShotPanel panel)
{
    DrawList dl_top, dl_bot;
    ui_draw_top(ui, &dl_top, pics, THUMB_MAX_SLOTS);
    ui_draw_bot(ui, &dl_bot, pics, THUMB_MAX_SLOTS);

    bool want_top = (panel == SHOT_TOP || panel == SHOT_BOTH ||
                     (panel == SHOT_VIEW && ui->view != VIEW_KEYBOARD && ui->view != VIEW_PLAYING));
    bool want_bot = (panel == SHOT_BOTTOM || panel == SHOT_BOTH || !want_top);

    if (getenv("BANANATUBE_DUMP")) {
        const char *kind[] = { "rect", "text", "image" };
        for (int i = 0; i < dl_top.n; i++) {
            const DrawOp *op = &dl_top.op[i];
            if (op->kind == OP_IMAGE || (op->kind == OP_TEXT && i < 30))
                printf("top %3d %-5s box %4d,%4d %3dx%3d image %2d fit %d text \"%s\"\n",
                       i, kind[op->kind], op->x, op->y, op->w, op->h, op->image, op->fit, op->text);
        }
    }

    /* the panel to draw for: 640x480 on the RG DS, the Plus's 1024x768 at 1.6 with "plus" */
    int w = SCREEN_W, h = SCREEN_H;
    float scale = 1.0f;
    if (plus) { w = 1024; h = 768; scale = 1.6f; }
    int stack = (want_top && want_bot) ? 2 : 1;
    uint32_t *canvas = calloc((size_t)w * h * stack, sizeof(uint32_t));
    if (!canvas) return false;
    if (want_top) render_full(canvas, w, h, fnt, &dl_top, pics, THUMB_MAX_SLOTS, scale);
    if (want_bot) render_full(canvas + (size_t)w * h * (want_top ? 1 : 0), w, h, fnt, &dl_bot,
                              pics, THUMB_MAX_SLOTS, scale);

    FILE *f = fopen(path, "wb");
    if (!f) { log_line("shot: cannot write %s", path); free(canvas); return false; }
    fprintf(f, "P6\n%d %d\n255\n", w, h * stack);
    for (int i = 0; i < w * h * stack; i++) {
        uint8_t px[3] = { (uint8_t)(canvas[i] >> 16), (uint8_t)(canvas[i] >> 8), (uint8_t)canvas[i] };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    log_line("shot: %s (%d+%d draws)", path, dl_top.n, dl_bot.n);
    free(canvas);
    return true;
}

static int shot(const char *path, bool keyboard, bool with_results, bool plus, bool playing, bool show_history,
                ShotPanel panel)
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

    static History shot_history;
    if (show_history) {
        history_clear(&shot_history);
        const char *t[5] = { "Making a theme for BananaOS, start to finish", "BananaDS runs Golden Sun: Dark Dawn",
                             "RetroArch shaders on a 1 GB handheld", "The RG DS Plus, one year on",
                             "Why there is no browser in BananaOS" };
        const char *c[5] = { "Banana Dev", "ROCKNIX", "Banana Dev", "Retro Game Corps", "Banana Dev" };
        for (int i = 0; i < 5; i++) {
            char id[VIDEO_ID_LEN];
            snprintf(id, sizeof id, "hist%07d", i);
            history_touch(&shot_history, id, t[i], c[i], 60 * (9 + i * 6) + 30);
        }
        history_progress(&shot_history, "hist0000000", 60 * 4 + 12);
        history_progress(&shot_history, "hist0000001", 60 * 12 + 3);
        history_progress(&shot_history, "hist0000002", 60 * 20 + 45);
        ui_set_history(&ui, &shot_history);
        ui.view = VIEW_HISTORY;
        ui.history_sel = 1;
    }

    if (playing) {
        ui_set_playing(&ui, "Making a theme for BananaOS, start to finish", "Banana Dev");
        ui_set_playing_state(&ui, 4 * 60 + 12, 12 * 60 + 34, false, false, 854, 480);
        ui_set_full_state(&ui, 70, 480, true, 0);
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

    FontCtx *fnt = shot_fonts();
    bool ok = save_panels(path, &ui, fnt, pics, plus, panel);
    font_free(fnt);
    return ok ? 0 : 1;
}
