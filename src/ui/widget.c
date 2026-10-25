/* widget.c -- the shared pieces. Sizes are in screen units (the panel is 640x480 whatever its pixels). */
#include "ui/widget.h"
#include "util.h"

#include <math.h>
#include <string.h>

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

int widget_hint(DrawList *dl, const UiTheme *t, int x, int y, BananaKey key, const char *label, bool hot)
{
    const char *k = key_hint_label(key);
    int kw = 14 + 7 * (int)strlen(k);                 /* a chip as wide as its letters need */
    dl_rect_rounded(dl, x, y, kw, 18, 4, hot ? t->accent : t->panel_hi);
    dl_text(dl, x, y, kw, 18, k, FONT_UI_BOLD, 11, hot ? t->accent_ink : t->ink, ALIGN_CENTER, VALIGN_MIDDLE);
    dl_text(dl, x + kw + 5, y, 200, 18, label, FONT_UI, 12, hot ? t->ink : t->muted, ALIGN_LEFT, VALIGN_MIDDLE);
    return kw + 5 + 7 * (int)strlen(label);
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

void widget_title(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, const char *text, HAlign align)
{
    dl_text(dl, x, y, w, h, text, FONT_UI_BOLD, 19, t->ink, align, VALIGN_MIDDLE);
}

void widget_label(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, const char *text, HAlign align)
{
    dl_text(dl, x, y, w, h, text, FONT_UI, 14, t->ink, align, VALIGN_MIDDLE);
}

void widget_small(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, const char *text, HAlign align)
{
    dl_text(dl, x, y, w, h, text, FONT_UI, 12, t->muted, align, VALIGN_MIDDLE);
}

void widget_mono(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, const char *text, HAlign align)
{
    dl_text(dl, x, y, w, h, text, FONT_MONO, 14, t->accent, align, VALIGN_MIDDLE);
}
