/* osd.c */
#include "player/osd.h"
#include "display/gles_blit.h"
#include "ui/widget.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OSD_H 40          /* the bar's height in the UI's units */

bool osd_init(Osd *o, int panel_w, float scale)
{
    memset(o, 0, sizeof *o);
    o->h = (int)(OSD_H * scale + 0.5f);
    o->w = panel_w;
    o->pixels = calloc((size_t)o->w * o->h, sizeof(uint32_t));
    o->blank = true;
    return o->pixels != NULL;
}

void osd_free(Osd *o)
{
    if (!o) return;
    blit_free(o->tex);
    free(o->pixels);
    memset(o, 0, sizeof *o);
}

/* Everything the bar shows, folded into one number: the same overlay is not uploaded again. */
static uint32_t state_hash(const Player *p, const char *title, const char *status, bool paused, float spin)
{
    uint32_t h = 2166136261u;
    const char *s = title ? title : "";
    while (*s) { h = (h ^ (unsigned char)*s++) * 16777619u; }
    s = status ? status : "";
    while (*s) { h = (h ^ (unsigned char)*s++) * 16777619u; }
    h = (h ^ (uint32_t)player_position(p)) * 16777619u;      /* whole seconds: it ticks, it does not blur */
    h = (h ^ (uint32_t)player_duration(p)) * 16777619u;
    h = (h ^ (uint32_t)(paused ? 1 : 0)) * 16777619u;
    h = (h ^ (uint32_t)(player_buffering(p) ? 2 : 0)) * 16777619u;
    h = (h ^ (uint32_t)(spin * 100.0f)) * 16777619u;
    h = (h ^ (uint32_t)player_width(p)) * 16777619u;
    return h;
}

void osd_update(Osd *o, FontCtx *fnt, const Player *p, const char *title, const char *status,
                bool paused, float spin)
{
    if (!o || !o->pixels) return;

    bool show = (title && *title) || (status && *status);
    uint32_t h = show ? state_hash(p, title, status, paused, spin) : 0;
    if (h == o->hash && o->blank == !show) return;
    o->hash = h;
    o->blank = !show;

    if (!show) {
        /* Nothing to say: upload a transparent texture so the bar disappears without a second code path. */
        memset(o->pixels, 0, (size_t)o->w * o->h * sizeof(uint32_t));
        o->tex = blit_upload(o->tex, o->pixels, o->w, o->h);
        return;
    }

    float scale = (float)o->h / OSD_H;
    UiTheme t = ui_theme();

    DrawList dl;
    dl_reset(&dl);
    int W = (int)(o->w / scale + 0.5f);                  /* the bar in the UI's units */

    dl_rect(&dl, 0, 0, W, OSD_H, 0xe0101218);            /* a dark ribbon, mostly opaque */

    /* left: the resolution the VPU is decoding, and the frames it dropped */
    char badge[48];
    int pw = player_width(p), ph = player_height(p);
    if (pw > 0 && ph > 0) snprintf(badge, sizeof badge, "%dp", ph);
    else                  snprintf(badge, sizeof badge, "%s", "loading");
    dl_rect_rounded(&dl, 10, 8, 56, 24, 5, 0xfff0b429);
    dl_text(&dl, 10, 8, 56, 24, badge, FONT_UI_BOLD, 13, 0xff17161c, ALIGN_CENTER, VALIGN_MIDDLE);

    int dropped = player_dropped(p);
    if (dropped > 0) {
        char d[32];
        snprintf(d, sizeof d, "%d dropped", dropped);
        dl_text(&dl, 74, 8, 120, 24, d, FONT_UI, 11, 0xffff8a80, ALIGN_LEFT, VALIGN_MIDDLE);
    }

    /* centre: the title, over the position */
    char times[48];
    char a[16], b[16];
    fmt_duration((int)player_position(p), a, sizeof a);
    fmt_duration((int)player_duration(p), b, sizeof b);
    snprintf(times, sizeof times, "%s / %s", a, b);
    if (title && *title) dl_text(&dl, 150, 4, W - 300, 18, title, FONT_UI_BOLD, 13, 0xfff1ede2, ALIGN_CENTER, VALIGN_MIDDLE);
    dl_text(&dl, 150, 22, W - 300, 14, times, FONT_MONO, 12, 0xffa39d8e, ALIGN_CENTER, VALIGN_MIDDLE);

    /* right: what it is doing */
    if (paused)   dl_text(&dl, W - 130, 8, 120, 24, "paused", FONT_UI_BOLD, 13, 0xfff0b429, ALIGN_RIGHT, VALIGN_MIDDLE);
    else if (player_buffering(p))
        widget_spinner(&dl, &t, W - 30, 20, 8, spin);
    if (status && *status) dl_text(&dl, W - 130, 8, 120, 24, status, FONT_UI, 12, 0xffff8a80, ALIGN_RIGHT, VALIGN_MIDDLE);

    /* and a progress line along the very bottom */
    double dur = player_duration(p);
    float frac = dur > 0 ? (float)(player_position(p) / dur) : 0.0f;
    if (frac > 0) widget_bar(&dl, &t, 0, OSD_H - 3, W, 3, frac, false);

    render_full(o->pixels, o->w, o->h, fnt, &dl, NULL, 0, scale);
    o->tex = blit_upload(o->tex, o->pixels, o->w, o->h);
}

void osd_draw(const Osd *o, int panel_w, int panel_h)
{
    if (!o || !o->tex) return;
    blit_draw(o->tex, o->w, o->h, 0, panel_h - o->h, o->w, o->h, panel_w, panel_h);
}
