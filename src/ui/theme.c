/* theme.c -- the menu's own palettes (ui/rnds/RndsUI.cpp in BananaLinux). */
#include "ui/theme.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

static const struct { const char *name; uint32_t rgb; } ACCENTS[] = {
    { "yellow", 0xf0b429 },
    { "blue",   0x4a9fe0 },
    { "red",    0xe8574d },
    { "green",  0x56bf6b },
    { "purple", 0xa07ae6 },
    { "pink",   0xec6fa8 },
};

static float channel_lum(uint32_t v)
{
    float x = (float)v / 255.0f;
    return x <= 0.03928f ? x / 12.92f : powf((x + 0.055f) / 1.055f, 2.4f);
}

static float rel_lum(uint32_t c)
{
    return 0.2126f * channel_lum((c >> 16) & 0xff) +
           0.7152f * channel_lum((c >> 8) & 0xff) +
           0.0722f * channel_lum(c & 0xff);
}

static uint32_t ink_on(uint32_t rgb)
{
    float a = rel_lum(rgb);
    float dark = (a + 0.05f) / (rel_lum(0x17161c) + 0.05f);
    float light = (rel_lum(0xffffff) + 0.05f) / (a + 0.05f);
    return dark >= light ? 0x17161c : 0xffffff;
}

UiTheme ui_theme_with(const char *theme, const char *accent)
{
    bool light = theme && strcmp(theme, "light") == 0;
    uint32_t a = 0xf0b429;
    for (size_t i = 0; i < sizeof ACCENTS / sizeof ACCENTS[0]; i++)
        if (accent && strcmp(accent, ACCENTS[i].name) == 0) { a = ACCENTS[i].rgb; break; }

    UiTheme t;
    t.bg        = light ? 0xfff3efe6 : 0xff17161c;
    t.bar       = light ? 0xfffdfcf9 : 0xff2e2b33;
    t.panel     = light ? 0xfff6f2ea : 0xff121117;
    t.panel_hi  = light ? 0xffffffff : 0xff3a3540;
    t.ink       = light ? 0xff2a2620 : 0xfff1ede2;
    t.muted     = light ? 0xff7a7262 : 0xffa39d8e;
    t.faint     = light ? 0xffa69d8c : 0xff8a8374;
    t.accent    = 0xff000000u | a;
    t.accent_ink= ink_on(a);
    t.hot       = light ? t.accent : 0xffffcf5c;
    t.warn      = 0xffff3b4a;
    t.good      = 0xff6fbf73;
    return t;
}

UiTheme ui_theme(void)
{
    return ui_theme_with("dark", "yellow");
}
