/* theme.h -- the palette the two views draw with.
 *
 * The menu's own dark/light palettes and named accent colours (ui/rnds/RndsUI.cpp in BananaLinux), so the
 * client looks like the thing it was started from. An unknown theme/accent falls back to dark/yellow.
 */
#ifndef BANANATUBE_THEME_H
#define BANANATUBE_THEME_H

#include <stdint.h>

typedef struct {
    uint32_t bg, bar, panel, panel_hi, ink, muted, faint, accent, accent_ink, hot, warn, good;
    /* The brand's own colour. The mark beside the wordmark is always the banana yellow, so the wordmark's
     * first half is too, whatever the menu's accent is set to. */
    uint32_t brand, brand_ink;
} UiTheme;

UiTheme ui_theme(void);
UiTheme ui_theme_with(const char *theme, const char *accent);

#endif /* BANANATUBE_THEME_H */
