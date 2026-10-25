/* theme.c -- the menu's own dark palette (ui/rnds/RndsUI.cpp's darkPal in BananaLinux). */
#include "ui/theme.h"

UiTheme ui_theme(void)
{
    UiTheme t;
    t.bg        = 0xff17161c;      /* screenBg */
    t.bar       = 0xff2e2b33;      /* screenBand: the header strip */
    t.panel     = 0xff121117;      /* chipBody: a card sitting on the background */
    t.panel_hi  = 0xff3a3540;      /* panelBand: a raised edge */
    t.ink       = 0xfff1ede2;
    t.muted     = 0xffa39d8e;
    t.faint     = 0xff8a8374;      /* line */
    t.accent    = 0xfff0b429;      /* the menu's default accent is yellow */
    t.accent_ink= 0xff17161c;
    t.hot       = 0xffffcf5c;      /* accent band */
    t.warn      = 0xffff3b4a;
    t.good      = 0xff6fbf73;
    return t;
}
