/* settings.c -- the app's own settings screen (see ui/settings.h).
 *
 * One fixed table of rows, each the key bananatube.conf uses, what it is called, what it does, and the values
 * it takes. A row with no values is typed with the keyboard (the instance URL). Changing a value calls
 * conf_set() on the app's Conf and reports SETTINGS_CHANGED; the caller saves the file, so this file never
 * touches the disk and the tests can drive it with a Conf on the stack.
 */
#include "ui/settings.h"
#include "ui/widget.h"
#include "util.h"

#include <stdio.h>
#include <string.h>

#define ROW_H    48
#define LIST_Y   10
#define HINTS_H  30

typedef struct {
    const char *key;             /* bananatube.conf's key */
    const char *label;           /* what the row says */
    const char *help;            /* the top panel's description */
    const char *names[6];        /* shown */
    const char *values[6];       /* stored */
    int         n;               /* how many of each; 0: typed with the keyboard */
    bool        text;            /* the instance URL: a keyboard, not a cycle */
} Row;

static const Row ROWS[] = {
    { "backend", "Source",
      "Where a search and a stream come from. Auto tries your Invidious instance first, then YouTube's own "
      "InnerTube API. Invidious hands over plain URLs, the low-memory path.",
      { "Auto", "Invidious", "InnerTube" }, { "auto", "invidious", "innertube" }, 3, false },
    { "quality", "Quality",
      "The height asked of the stream. 480p is the panel's own height; 720p is sharper and costs more; 360p "
      "is the lightest. X while a video plays changes it there too.",
      { "360p", "480p", "720p" }, { "360p", "480p", "720p" }, 3, false },
    { "audio_only_on_lid", "Lid audio",
      "On: closing the lid keeps the sound playing and drops the video, for an album. Only matters when "
      "BananaOS's Settings > Closing the lid is set to \"Does nothing\".",
      { "On", "Off" }, { "yes", "no" }, 2, false },
    { "history_size", "Watched list",
      "How many videos the watched list keeps (Y on the results). A value larger than the file can hold is "
      "clamped, and the oldest fall off the end.",
      { "10", "25", "50", "100" }, { "10", "25", "50", "100" }, 4, false },
    { "cache_size", "Picture cache",
      "How many result pictures are held in memory: 12 is about 1.5 MB. More pictures scroll more smoothly; "
      "fewer leave more RAM to the video decoder.",
      { "6", "12", "18", "24" }, { "6", "12", "18", "24" }, 4, false },
    { "search_timeout_ms", "Search timeout",
      "How long a backend is given to answer before the next one is tried. The handheld's Wi-Fi is slow to "
      "start, so a short timeout makes a slow instance look broken.",
      { "3 s", "6 s", "10 s", "15 s" }, { "3000", "6000", "10000", "15000" }, 4, false },
    { "hwdec", "Decoder",
      "The RK3568's video unit. Auto lets mpv pick (the kernel's v4l2m2m decoder); Hardware forces it; "
      "Software uses none. Takes effect the next time a video starts.",
      { "Auto", "Hardware", "Software" }, { "auto-safe", "v4l2m2m-copy", "no" }, 3, false },
    { "instance", "Instance",
      "An Invidious or Piped base URL, e.g. https://inv.nadeko.net. Empty uses the built-in list of public "
      "instances, tried in order.",
      { "" }, { "" }, 0, true },
};
#define ROW_N ((int)(sizeof ROWS / sizeof ROWS[0]))

int settings_rows(void) { return ROW_N; }

void settings_open(SettingsView *sv, Conf *conf)
{
    sv->conf = conf;
    sv->sel = sv->top = 0;
    sv->editing = false;
    sv->changed_key[0] = '\0';
    kb_reset(&sv->kb);
}

/* The value the Conf holds for a row, as the file would write it. */
static void row_raw(const SettingsView *sv, int r, char *out, size_t cap)
{
    const Conf *c = sv->conf;
    const char *k = ROWS[r].key;
    if (!strcmp(k, "backend"))                str_copy(out, cap, conf_backend_name(c->backend));
    else if (!strcmp(k, "quality"))           str_copy(out, cap, conf_quality_name(c->quality));
    else if (!strcmp(k, "audio_only_on_lid")) str_copy(out, cap, c->audio_only_on_lid ? "yes" : "no");
    else if (!strcmp(k, "history_size"))      snprintf(out, cap, "%d", c->history_size);
    else if (!strcmp(k, "cache_size"))        snprintf(out, cap, "%d", c->cache_size);
    else if (!strcmp(k, "search_timeout_ms")) snprintf(out, cap, "%d", c->search_timeout_ms);
    else if (!strcmp(k, "hwdec"))             str_copy(out, cap, c->hwdec);
    else if (!strcmp(k, "instance"))          str_copy(out, cap, c->instance);
    else out[0] = '\0';
}

/* Which of the row's values the Conf holds, or -1 when it holds one the list does not have (edited on the
 * card, e.g. hwdec = rkmpp-copy). */
static int row_index(const SettingsView *sv, int r)
{
    const Row *row = &ROWS[r];
    if (row->n <= 0) return -1;
    char cur[128];
    row_raw(sv, r, cur, sizeof cur);
    for (int i = 0; i < row->n; i++) if (strcmp(cur, row->values[i]) == 0) return i;
    return -1;
}

/* What the row shows for its value: the name of the matching choice, the raw value for one the list does not
 * have, and a placeholder for an empty instance. */
static void row_text(const SettingsView *sv, int r, char *out, size_t cap)
{
    const Row *row = &ROWS[r];
    if (row->n <= 0) {
        row_raw(sv, r, out, cap);
        if (!out[0]) str_copy(out, cap, "(built-in list)");
        return;
    }
    int at = row_index(sv, r);
    if (at >= 0) str_copy(out, cap, row->names[at]);
    else row_raw(sv, r, out, cap);
}

static void entry_add(SettingsView *sv, const char *text)
{
    size_t n = strlen(sv->entry), add = strlen(text);
    if (n + add + 1 > sizeof sv->entry) return;
    memcpy(sv->entry + n, text, add + 1);
}

static void entry_backspace(SettingsView *sv)
{
    size_t n = strlen(sv->entry);
    if (!n) return;
    n--;
    while (n > 0 && ((unsigned char)sv->entry[n] & 0xc0) == 0x80) n--;   /* never leave half a character */
    sv->entry[n] = '\0';
}

static SettingsResult change(SettingsView *sv, int dir)
{
    const Row *row = &ROWS[sv->sel];
    if (row->n <= 0) return SETTINGS_NONE;              /* a text row has nothing to cycle */
    int at = row_index(sv, sv->sel);
    int next;
    if (at < 0)            next = dir > 0 ? 0 : row->n - 1;   /* a value not in the list: step to an end */
    else if (row->n == 1)  return SETTINGS_NONE;
    else                   next = (at + dir + row->n) % row->n;
    if (at >= 0 && next == at) return SETTINGS_NONE;
    conf_set(sv->conf, row->key, row->values[next]);
    str_copy(sv->changed_key, sizeof sv->changed_key, row->key);
    return SETTINGS_CHANGED;
}

SettingsResult settings_key(SettingsView *sv, BananaKey key)
{
    if (sv->editing) {
        char ch[8] = "";
        switch (kb_press(&sv->kb, key, ch, sizeof ch)) {
        case KB_ACTION_CHAR:    entry_add(sv, ch); return SETTINGS_MOVED;
        case KB_ACTION_SPACE:   entry_add(sv, " "); return SETTINGS_MOVED;
        case KB_ACTION_DELETE:  entry_backspace(sv); return SETTINGS_MOVED;
        case KB_ACTION_LAYER:   return SETTINGS_MOVED;
        case KB_ACTION_SUBMIT:                       /* START: save the URL (an empty one is the built-in list) */
            sv->editing = false;
            conf_set(sv->conf, "instance", sv->entry);
            str_copy(sv->changed_key, sizeof sv->changed_key, "instance");
            return SETTINGS_CHANGED;
        case KB_ACTION_CLOSE:                        /* SELECT: put the keyboard away, keep the old value */
            sv->editing = false;
            return SETTINGS_MOVED;
        default:
            return SETTINGS_NONE;
        }
    }

    switch (key) {
    case BK_UP:    if (sv->sel > 0) { sv->sel--; return SETTINGS_MOVED; } return SETTINGS_NONE;
    case BK_DOWN:  if (sv->sel < ROW_N - 1) { sv->sel++; return SETTINGS_MOVED; } return SETTINGS_NONE;
    case BK_LEFT:  case BK_L1: return change(sv, -1);
    case BK_RIGHT: case BK_R1: return change(sv, +1);
    case BK_A:
        if (ROWS[sv->sel].text) {
            /* the instance URL: the keyboard, prefilled with what is there now */
            sv->editing = true;
            kb_reset(&sv->kb);
            str_copy(sv->entry, sizeof sv->entry, sv->conf->instance);
            return SETTINGS_MOVED;
        }
        return change(sv, +1);
    case BK_B: case BK_START: return SETTINGS_CLOSE;
    default: return SETTINGS_NONE;
    }
}

int settings_hit(const SettingsView *sv, int x, int y)
{
    if (sv->editing) return -1;
    if (x < 10 || x >= SCREEN_W - 10) return -1;
    if (y < LIST_Y || y >= SCREEN_H - HINTS_H) return -1;
    int i = sv->top + (y - LIST_Y) / ROW_H;
    return (i >= 0 && i < ROW_N) ? i : -1;
}

void settings_draw_top(SettingsView *sv, DrawList *dl, const UiTheme *t)
{
    widget_header(dl, t, "Settings");

    if (sv->editing) {
        widget_label(dl, t, 20, 58, SCREEN_W - 40, 24, "Instance", ALIGN_LEFT);
        widget_small(dl, t, 20, 82, SCREEN_W - 40, 40,
                     "An Invidious or Piped base URL, e.g. https://inv.nadeko.net. Empty uses the built-in list.",
                     ALIGN_LEFT);
        widget_well(dl, t, 20, 140, SCREEN_W - 40, 40, 8);
        char probe[sizeof sv->entry + 2];
        snprintf(probe, sizeof probe, "%s|", sv->entry);
        widget_label(dl, t, 32, 140, SCREEN_W - 64, 40, sv->entry[0] ? probe : "|", ALIGN_LEFT);
        widget_small(dl, t, 20, 192, SCREEN_W - 40, 20, "START saves it, SELECT puts the keyboard away.", ALIGN_LEFT);
        return;
    }

    const Row *row = &ROWS[sv->sel];
    widget_title(dl, t, 20, 60, SCREEN_W - 40, 34, row->label, ALIGN_LEFT);

    /* the value, large, in the accent: it is what the D-pad is about to change */
    char v[64];
    row_text(sv, sv->sel, v, sizeof v);
    widget_label(dl, t, 20, 96, SCREEN_W - 40, 26, v, ALIGN_LEFT);

    dl_text_wrapped(dl, 20, 132, SCREEN_W - 40, SCREEN_H - 180, row->help, FONT_UI, 15, t->muted,
                    ALIGN_LEFT, VALIGN_TOP, 9, 22);
}

void settings_draw_bot(SettingsView *sv, DrawList *dl, const UiTheme *t)
{
    if (sv->editing) {
        kb_draw_as(dl, &sv->kb, t, KB_BOX_X, KB_BOX_Y, KB_BOX_W, KB_BOX_H, "save");
        return;
    }

    int visible = (SCREEN_H - LIST_Y - HINTS_H) / ROW_H;
    if (visible < 1) visible = 1;
    if (sv->sel < sv->top) sv->top = sv->sel;
    if (sv->sel >= sv->top + visible) sv->top = sv->sel - visible + 1;
    if (sv->top > ROW_N - visible) sv->top = ROW_N - visible;
    if (sv->top < 0) sv->top = 0;

    for (int i = sv->top; i < ROW_N && i < sv->top + visible; i++) {
        int y = LIST_Y + (i - sv->top) * ROW_H;
        bool on = (i == sv->sel);
        dl_rect_rounded(dl, 10, y, SCREEN_W - 20, ROW_H - 8, 8, on ? t->panel_hi : t->panel);
        if (on) dl_rect_rounded(dl, 10, y, 3, ROW_H - 8, 2, t->accent);

        widget_label(dl, t, 22, y, SCREEN_W - 240, ROW_H - 8, ROWS[i].label, ALIGN_LEFT);
        char v[64];
        row_text(sv, i, v, sizeof v);
        widget_label(dl, t, SCREEN_W - 220, y, 190, ROW_H - 8, v, ALIGN_RIGHT);
        if (on && !ROWS[i].text) {
            dl_text(dl, SCREEN_W - 238, y, 16, ROW_H - 8, "<", FONT_UI_BOLD, 15, t->accent, ALIGN_CENTER, VALIGN_MIDDLE);
            dl_text(dl, SCREEN_W - 26, y, 16, ROW_H - 8, ">", FONT_UI_BOLD, 15, t->accent, ALIGN_CENTER, VALIGN_MIDDLE);
        }
    }
}
