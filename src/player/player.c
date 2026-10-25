/* player.c -- libmpv on the top panel. */
#include "player/player.h"
#include "util.h"

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <mpv/client.h>
#include <mpv/render_gl.h>

struct Player {
    mpv_handle        *mpv;
    mpv_render_context *rc;
    PlayerGl           gl;

    int     wake[2];                /* a byte when mpv has a frame: what wakes the main loop's poll() */
    bool    active;                 /* a file is loaded */
    bool    eof;
    bool    frame;                  /* mpv wants a frame drawn */
    bool    buffering;
    double  position, duration;
    int     width, height;
    int     dropped;
    char    error[160];
};

static void *get_proc(void *ctx, const char *name)
{
    Player *p = ctx;
    return p->gl.get_proc_address ? p->gl.get_proc_address(name) : NULL;
}

/* mpv has a new frame: draw it from the main loop, never from this callback. */
static void on_mpv_render(void *ctx)
{
    Player *p = ctx;
    p->frame = true;
    if (p->wake[1] >= 0) {
        ssize_t n = write(p->wake[1], "f", 1);       /* the loop's poll() watches the read end */
        (void)n;
    }
}

static bool set(mpv_handle *m, const char *name, const char *value)
{
    int r = mpv_set_option_string(m, name, value);
    if (r < 0) log_line("player: %s = %s: %s", name, value, mpv_error_string(r));
    return r >= 0;
}

Player *player_create(const char *hwdec, int audio_buffer_ms)
{
    Player *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    p->dropped = 0;
    p->error[0] = '\0';
    p->wake[0] = p->wake[1] = -1;
    if (pipe(p->wake) == 0) {
        fcntl(p->wake[0], F_SETFL, O_NONBLOCK);
        fcntl(p->wake[1], F_SETFL, O_NONBLOCK);
    }

    p->mpv = mpv_create();
    if (!p->mpv) { free(p); return NULL; }

    /* Nothing from a config file or the environment may change the budget below. */
    set(p->mpv, "config", "no");
    set(p->mpv, "load-scripts", "no");
    set(p->mpv, "terminal", "yes");
    set(p->mpv, "msg-level", "all=warn");

    /* The render API: we own the surface (the panel) and the loop. */
    set(p->mpv, "vo", "libmpv");
    set(p->mpv, "idle", "yes");
    set(p->mpv, "force-window", "no");
    set(p->mpv, "osc", "no");
    set(p->mpv, "osd-level", "0");
    set(p->mpv, "input-default-bindings", "no");
    set(p->mpv, "input-vo-keyboard", "no");
    set(p->mpv, "cursor-autohide", "no");

    /* Memory. mpv's default demuxer cache is 150 MB: on a 1 GB handheld that is an OOM, not a stutter. */
    set(p->mpv, "demuxer-max-bytes", "16MiB");
    set(p->mpv, "demuxer-max-back-bytes", "4MiB");
    set(p->mpv, "cache", "no");
    set(p->mpv, "cache-on-disk", "no");
    set(p->mpv, "ytdl", "no");               /* we resolved the URL: mpv must not spawn one of its own */
    set(p->mpv, "vd-lavc-dr", "yes");        /* decoded frames straight to the GPU, no CPU copy */
    set(p->mpv, "hwdec", (hwdec && *hwdec) ? hwdec : "auto-safe");

    /* What looks right on these panels. */
    set(p->mpv, "video-sync", "display-resample");
    set(p->mpv, "video-output-levels", "limited");
    set(p->mpv, "keepaspect", "yes");
    set(p->mpv, "interpolation", "no");      /* an A55 has no room for it, and the panels are 60 Hz */
    set(p->mpv, "audio-channels", "stereo");
    set(p->mpv, "audio-buffer", "0.2");      /* bananad's ALSA route is 48 kHz stereo */

    /* Nothing about this session is worth keeping: Phase 4 owns history, and a resume file would be one more
     * thing writing to the card on a device that is meant to be pulled. */
    set(p->mpv, "save-position-on-quit", "no");
    set(p->mpv, "keep-open", "no");

    char buf[32];
    snprintf(buf, sizeof buf, "%.3f", (audio_buffer_ms > 0 ? audio_buffer_ms : 200) / 1000.0);
    set(p->mpv, "audio-buffer", buf);

    if (mpv_initialize(p->mpv) < 0) {
        log_line("player: mpv_initialize failed");
        mpv_terminate_destroy(p->mpv);
        if (p->wake[0] >= 0) close(p->wake[0]);
        if (p->wake[1] >= 0) close(p->wake[1]);
        free(p);
        return NULL;
    }
    log_line("player: libmpv %lu ready (hwdec %s)", (unsigned long)mpv_client_api_version(),
             hwdec && *hwdec ? hwdec : "auto-safe");
    return p;
}

bool player_attach_render(Player *p, const PlayerGl *gl)
{
    if (!p || !gl) return false;
    p->gl = *gl;

    /* EGL on GBM: the context is current on this thread, so the render API takes that one and needs no
     * display handle (that is what the X11/Wayland params are for). */
    mpv_opengl_init_params ip = { .get_proc_address = get_proc, .get_proc_address_ctx = p };
    int adv = 1;
    mpv_render_param params[] = {
        { MPV_RENDER_PARAM_API_TYPE, (void *)MPV_RENDER_API_TYPE_OPENGL },
        { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &ip },
        { MPV_RENDER_PARAM_ADVANCED_CONTROL, &adv },
        { 0, NULL },
    };
    if (mpv_render_context_create(&p->rc, p->mpv, params) < 0) {
        log_line("player: no render context (is an EGL context current?)");
        return false;
    }
    mpv_render_context_set_update_callback(p->rc, on_mpv_render, p);
    log_line("player: rendering into %dx%d", gl->width, gl->height);
    return true;
}

void player_destroy(Player *p)
{
    if (!p) return;
    if (p->rc) { mpv_render_context_free(p->rc); p->rc = NULL; }
    if (p->mpv) mpv_terminate_destroy(p->mpv);
    if (p->wake[0] >= 0) close(p->wake[0]);
    if (p->wake[1] >= 0) close(p->wake[1]);
    free(p);
}

mpv_handle *player_mpv_handle(const Player *p)
{
    return p ? p->mpv : NULL;
}

int player_wake_fd(const Player *p)
{
    return p ? p->wake[0] : -1;
}

void player_wake_drain(Player *p)
{
    if (!p || p->wake[0] < 0) return;
    char buf[64];
    while (read(p->wake[0], buf, sizeof buf) > 0) { }
}

static double get_double(Player *p, const char *name)
{
    double d = 0;
    if (mpv_get_property(p->mpv, name, MPV_FORMAT_DOUBLE, &d) < 0) return 0;
    return d;
}

static int get_int(Player *p, const char *name)
{
    int64_t v = 0;
    if (mpv_get_property(p->mpv, name, MPV_FORMAT_INT64, &v) < 0) return 0;
    return (int)v;
}

bool player_open(Player *p, const StreamUrl *s)
{
    if (!p || !s || !s->url[0]) return false;
    player_close(p);

    /* DASH: the video and its sound are two URLs. mpv plays them together, synced on their timestamps. */
    if (s->audio_url[0]) mpv_set_option_string(p->mpv, "audio-file", s->audio_url);
    else                 mpv_set_option_string(p->mpv, "audio-file", "");

    const char *cmd[] = { "loadfile", s->url, "replace", NULL };
    int r = mpv_command(p->mpv, cmd);
    if (r < 0) {
        snprintf(p->error, sizeof p->error, "%s", mpv_error_string(r));
        log_line("player: loadfile: %s", p->error);
        return false;
    }
    p->active = true;
    p->eof = false;
    p->error[0] = '\0';
    p->buffering = true;
    p->width = s->width;
    p->height = s->height;
    log_line("player: opening %dx%d %s%s", s->width, s->height, s->vcodec,
             s->audio_url[0] ? " + separate audio" : "");
    return true;
}

void player_close(Player *p)
{
    if (!p) return;
    const char *cmd[] = { "stop", NULL };
    mpv_command(p->mpv, cmd);
    p->active = false;
    p->eof = false;
    p->buffering = false;
    p->position = p->duration = 0;
    p->frame = true;                 /* the surface needs clearing back to the placeholder */
}

bool player_active(const Player *p) { return p && p->active; }

void player_tick(Player *p)
{
    if (!p) return;
    for (;;) {
        mpv_event *e = mpv_wait_event(p->mpv, 0);
        if (!e || e->event_id == MPV_EVENT_NONE) break;
        switch (e->event_id) {
        case MPV_EVENT_END_FILE: {
            mpv_event_end_file *f = e->data;
            p->active = false;
            if (f && f->reason == MPV_END_FILE_REASON_ERROR) {
                const char *r = mpv_error_string(f->error);
                snprintf(p->error, sizeof p->error, "%s", r ? r : "playback failed");
                log_line("player: %s", p->error);
            } else {
                p->eof = true;
                log_line("player: end of file");
            }
            p->frame = true;
            break;
        }
        case MPV_EVENT_FILE_LOADED:
            log_line("player: playing");
            break;
        case MPV_EVENT_LOG_MESSAGE: {
            mpv_event_log_message *m = e->data;
            if (m && m->level && strncmp(m->level, "error", 5) == 0)
                log_line("mpv: %s%s", m->prefix ? m->prefix : "", m->text ? m->text : "");
            break;
        }
        default:
            break;
        }
    }

    if (p->active) {
        p->position = get_double(p, "time-pos");
        p->duration = get_double(p, "duration");
        p->buffering = get_int(p, "paused-for-cache") != 0 || get_double(p, "demuxer-cache-duration") < 0.5;
        int w = get_int(p, "video-params/w"), h = get_int(p, "video-params/h");
        if (w > 0 && h > 0) { p->width = w; p->height = h; }
        int d = get_int(p, "frame-drop-count");
        if (d > 0) p->dropped = d;
    }
}

bool player_frame_ready(Player *p)
{
    if (!p) return false;
    bool f = p->frame;
    p->frame = false;
    return f;
}

void player_render(Player *p)
{
    if (!p || !p->rc) return;
    if (!mpv_render_context_update(p->rc)) return;      /* nothing new: keep the last frame */

    mpv_opengl_fbo fbo = { .fbo = 0, .w = p->gl.width, .h = p->gl.height, .internal_format = 0 };
    int flip = 1;
    mpv_render_param params[] = {
        { MPV_RENDER_PARAM_OPENGL_FBO, &fbo },
        { MPV_RENDER_PARAM_FLIP_Y, &flip },
        { 0, NULL },
    };
    mpv_render_context_render(p->rc, params);
}

void player_set_paused(Player *p, bool paused)
{
    if (!p) return;
    int f = paused ? 1 : 0;
    mpv_set_property(p->mpv, "pause", MPV_FORMAT_FLAG, &f);
}

bool player_paused(const Player *p)
{
    if (!p) return false;
    int f = 0;
    mpv_get_property(p->mpv, "pause", MPV_FORMAT_FLAG, &f);
    return f != 0;
}

void player_toggle_pause(Player *p)
{
    player_set_paused(p, !player_paused(p));
}

void player_seek(Player *p, double seconds)
{
    if (!p) return;
    char v[32];
    snprintf(v, sizeof v, "%+.1f", seconds);            /* "+10.0" is relative in mpv */
    const char *cmd[] = { "seek", v, "relative", NULL };
    mpv_command(p->mpv, cmd);
}

void player_set_volume(Player *p, int percent)
{
    if (!p) return;
    double v = percent;
    mpv_set_property(p->mpv, "volume", MPV_FORMAT_DOUBLE, &v);
}

double player_position(const Player *p) { return p ? p->position : 0; }
double player_duration(const Player *p) { return p ? p->duration : 0; }
bool   player_buffering(const Player *p) { return p ? p->buffering : false; }
bool   player_eof(const Player *p) { return p ? p->eof : false; }
int    player_width(const Player *p) { return p ? p->width : 0; }
int    player_height(const Player *p) { return p ? p->height : 0; }
int    player_dropped(const Player *p) { return p ? p->dropped : 0; }
const char *player_error(const Player *p) { return p ? p->error : ""; }
