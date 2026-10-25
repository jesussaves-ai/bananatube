/* theme.h -- the palette the two views draw with.
 *
 * Fixed for now, matching the menu's own dark palette (ui/rnds/RndsUI.cpp in BananaLinux) so the client looks
 * like the thing it was started from. A layout theme's `colors` would slot in here (Phase 4).
 */
#ifndef BANANATUBE_THEME_H
#define BANANATUBE_THEME_H

#include <stdint.h>

typedef struct {
    uint32_t bg, bar, panel, panel_hi, ink, muted, faint, accent, accent_ink, hot, warn, good;
} UiTheme;

UiTheme ui_theme(void);

#endif /* BANANATUBE_THEME_H */
