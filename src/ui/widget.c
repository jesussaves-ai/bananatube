/* widget.c -- the shared pieces. Sizes are in screen units (the panel is 640x480 whatever its pixels). */
#include "ui/widget.h"
#include "util.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

void widget_panel(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, int radius)
{
    dl_rect_rounded(dl, x, y, w, h, radius, t->panel);
    dl_rect(dl, x + radius / 2, y, w - radius, 1, t->panel_hi);       /* the hairline the menu's cards have */
}

void widget_well(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, int radius)
{
    dl_rect_rounded(dl, x, y, w, h, radius, t->bg);
    dl_rect(dl, x + radius / 2, y, w - radius, 1, t->faint);
}

int widget_hint_text(DrawList *dl, const UiTheme *t, int x, int y, const char *chip, const char *label, bool hot)
{
    int kw = 14 + 7 * (int)strlen(chip);              /* a chip as wide as its letters need */
    if (kw < 30) kw = 30;                             /* ...but a one-letter key is not a sliver */
    if (dl) {
        dl_rect_rounded(dl, x, y, kw, 18, W_RADIUS_CHIP, hot ? t->accent : t->panel_hi);
        dl_text(dl, x, y, kw, 18, chip, FONT_UI_BOLD, W_SIZE_SMALL, hot ? t->accent_ink : t->ink, ALIGN_CENTER, VALIGN_MIDDLE);
        dl_text(dl, x + kw + 5, y, 200, 18, label, FONT_UI, W_SIZE_BODY, hot ? t->ink : t->muted, ALIGN_LEFT, VALIGN_MIDDLE);
    }
    return kw + 5 + 7 * (int)strlen(label);
}

int widget_hint(DrawList *dl, const UiTheme *t, int x, int y, BananaKey key, const char *label, bool hot)
{
    return widget_hint_text(dl, t, x, y, key_hint_label(key), label, hot);
}

void widget_hints_row(DrawList *dl, const UiTheme *t, const char *title)
{
    dl_rect(dl, 0, SCREEN_H - 30, SCREEN_W, 30, t->bar);
    (void)title;
}

void widget_bar(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, float frac, bool sunken)
{
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    dl_rect_rounded(dl, x, y, w, h, h / 2, sunken ? t->bg : t->panel);
    int fw = (int)((w - 4) * frac);
    if (fw > 0) dl_rect_rounded(dl, x + 2, y + 2, fw, h - 4, (h - 4) / 2, t->accent);
}

/* Eight small blocks round a circle: a spinner that reads as one at 24 px without a shader. */
void widget_spinner(DrawList *dl, const UiTheme *t, int cx, int cy, int radius, float phase)
{
    for (int i = 0; i < 8; i++) {
        float a = (float)(i * (360.0 / 8.0)) + phase * 360.0f;
        float rad = a * 3.14159265f / 180.0f;
        int px = cx + (int)(radius * cosf(rad));
        int py = cy + (int)(radius * sinf(rad));
        uint32_t c = (i >= 6 || i < 2) ? t->accent : t->faint;
        dl_rect_rounded(dl, px - 2, py - 2, 4, 4, 2, c);
    }
}

void widget_header(DrawList *dl, const UiTheme *t, const HeaderInfo *info)
{
    dl_rect(dl, 0, 0, SCREEN_W, 44, t->bar);

    /* A screen with its own name (Settings) is not the brand: it says so and stops there. */
    if (info && info->title) {
        widget_title(dl, t, 16, 0, 400, 44, info->title, ALIGN_LEFT);
    } else {
        /* The mark is a picture like any other: a draw op names it by its slot, and there is none unless
         * main.c decoded one and put it there. */
        int mark = (info && info->logo_image >= 0);
        int x = mark ? 44 : 16;
        if (mark) dl_image(dl, 12, 10, 24, 24, info->logo_image, FIT_CONTAIN, t->bar);
        dl_text(dl, x, 0, 400, 44, "Banana", FONT_UI_BOLD, W_SIZE_DISPLAY, t->brand,
                ALIGN_LEFT, VALIGN_MIDDLE);
        if (info && info->tube_x > 0)
            dl_text(dl, x + info->tube_x, 0, 400, 44, "Tube", FONT_UI_BOLD, W_SIZE_DISPLAY, t->ink,
                    ALIGN_LEFT, VALIGN_MIDDLE);
    }

    if (info && info->battery >= 0) {
        /* The number and its bar are one piece of furniture, stacked: the bar used to sit low and off to the
         * right of the number, which read as two unrelated things. */
        char pct[16];
        snprintf(pct, sizeof pct, "%d%%", info->battery);
        widget_mono(dl, t, SCREEN_W - 216, 4, 56, 24, pct, ALIGN_RIGHT);
        widget_bar(dl, t, SCREEN_W - 216, 30, 56, 5, (float)info->battery / 100.0f, true);
    }

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char clock[16];
    snprintf(clock, sizeof clock, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    widget_mono(dl, t, SCREEN_W - 140, 0, 124, 44, clock, ALIGN_RIGHT);
}

void widget_title(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, const char *text, HAlign align)
{
    dl_text(dl, x, y, w, h, text, FONT_UI_BOLD, W_SIZE_DISPLAY, t->ink, align, VALIGN_MIDDLE);
}

void widget_label(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, const char *text, HAlign align)
{
    dl_text(dl, x, y, w, h, text, FONT_UI, W_SIZE_TITLE, t->ink, align, VALIGN_MIDDLE);
}

void widget_small(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, const char *text, HAlign align)
{
    dl_text(dl, x, y, w, h, text, FONT_UI, W_SIZE_BODY, t->muted, align, VALIGN_MIDDLE);
}

void widget_mono(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, const char *text, HAlign align)
{
    dl_text(dl, x, y, w, h, text, FONT_MONO, 14, t->accent, align, VALIGN_MIDDLE);
}

/* The fourth step: the smallest line on a card and in a hint bar. */
void widget_tiny(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, const char *text, HAlign align)
{
    dl_text(dl, x, y, w, h, text, FONT_UI, W_SIZE_SMALL, t->faint, align, VALIGN_MIDDLE);
}
