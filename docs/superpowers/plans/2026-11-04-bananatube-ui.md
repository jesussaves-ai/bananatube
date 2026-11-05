# BananaTube UI: polish, header and the selection lag — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every BananaTube screen use one type scale, spacing and accent rule, give the results screen the rail and the header the owner picked, and make a selection move repaint only what changed.

**Architecture:** The UI is plain data (`AppState`, `Results`) plus one draw list per panel, rasterised by the CPU into a dumb buffer with only the damaged rectangles repainted. This work changes the draw vocabulary (`src/ui/widget.*`), the results geometry and drawing (`src/ui/results.*`), adds two small device readers (a PNG brand mark and the battery), and rejects ops in `render_ops()` that cannot land inside the rectangle being repainted.

**Tech Stack:** C11, CMake, the existing `bananatube_core` static library, libpng (already in the image) for the brand mark, the existing `tests/run.sh` (ctest) harness.

**Spec:** `docs/superpowers/specs/2026-11-04-bananatube-ui-design.md`

## Global Constraints

- No new dependency beyond libpng. No new asset.
- No change to the playback path: the visionOS/ANDROID_VR client split, `audio-add`, the render context and `player_*` stay exactly as they are.
- No new screens or features. No theme editor, no extra layouts.
- The DrawList, the damage diff and the software renderer stay. No GPU, no GLES in the UI path.
- UI units are the RG DS's 640x480; the Plus scales every draw by 1.6 in the renderer, not in the UI.
- `DRAW_MAX` is 512 ops per frame; a frame over 512 drops its tail (`dl.dropped`).
- The palette and its roles are `UiTheme` (`src/ui/theme.h`): `bg`, `bar`, `panel`, `panel_hi`, `ink`, `muted`, `faint`, `accent`, `accent_ink`, `hot`, `warn`, `good`. Default theme is dark/yellow; the theme and accent come from `Conf` once at startup (`main.c:815`).
- Pixelify Sans is `FONT_MONO` and is the clock's face only. Inter SemiBold is `FONT_UI_BOLD`.
- `sh tests/run.sh` must pass (16 tests today) after every task. Plain C11, 4-space indent, house comment style: say *why*, one line.
- The client lives in `~/bananatube`; `BananaLinux` owns the Buildroot package and the pin. Commit to `main` in `~/bananatube`.

## Review Focus

Five things the spec implies but no task's tests naturally cover. Each gets its test in the task that owns the code.

1. **A list of one (or none).** The rail must not draw a gap, crash, or let LEFT/RIGHT move off the single card.
2. **A long title or channel on the focused card.** The wider card must wrap and clip inside its box, and must not push the meta line out of the card.
3. **The brand mark missing or a corrupt PNG.** The header must show the wordmark alone — no crash and no placeholder box.
4. **No battery at all** (a `/sys/class/power_supply` with no `Battery` entry, or none at all). The header must draw no percentage rather than `0%`.
5. **A selection move while a thumbnail is still loading.** The placeholder is what changed, so the early-out must keep ops whose box is inside the rect and the damage rect must cover the placeholder, or the old focused card keeps its highlight.

---

### Task 1: A partial-repaint number in `cli_bench`

Every perf claim after this depends on being able to measure a *selection move*, and today `--bench` only measures a full repaint (`render_full` → `render_ops(..., 0, 0, w, h)`), which the early-out in Task 2 cannot affect at all.

**Files:**
- Modify: `src/main.c` (`cli_bench`, after the damage block that ends at line 508)

**Interfaces:**
- Consumes: `render_ops(canvas, w, h, fnt, dl, tex, tex_n, scale, cx, cy, cw, ch)` (`src/display/render.h`), `dl_diff`, `damage_next`, `damage_bounds` (`src/display/drawlist.h`), `ui_key(&ui, BK_RIGHT)`.
- Produces: nothing other tasks call. A printed line `  move:    ...` that Task 2 must change on the device.

- [ ] **Step 1: Add the measurement**

In `cli_bench`, after the existing damage block's closing `}`, add a block that measures one selection move the way the app performs it: draw a frame, move the selection, draw the next frame, diff the two lists to get the damage, and time only `render_ops` over that damage.

```c
    /* What one press of RIGHT costs. The app repaints only the damaged rectangles, and a selection move is
     * the frame the owner says is slow -- so this is the number to watch. The two draws differ by the
     * selection rim and the details panel, and both are inside the damage. */
    if (ui.results.count > 1) {
        ui.results.sel = 0;
        ui_draw_top(&ui, &dl_top, pics, THUMB_MAX_SLOTS);
        ui_draw_bot(&ui, &dl_bot, pics, THUMB_MAX_SLOTS);
        DrawList before_top = dl_top, before_bot = dl_bot;
        ui_key(&ui, BK_RIGHT);
        ui_draw_top(&ui, &dl_top, pics, THUMB_MAX_SLOTS);
        ui_draw_bot(&ui, &dl_bot, pics, THUMB_MAX_SLOTS);
        DrawList cur[2] = { dl_top, dl_bot }, old[2] = { before_top, before_bot };
        long px = 0;
        render_stats_reset();
        double t0 = render_now_us();
        for (int p = 0; p < 2; p++) {
            Damage own, missed, dmg;
            dl_diff(&old[p], &cur[p], &own);
            damage_reset(&missed);
            damage_reset(&dmg);
            damage_next(&own, &missed, &dmg, &missed);   /* one list, so nothing was missed */
            int x, y, dw, dh;
            damage_bounds(&dmg, &x, &y, &dw, &dh);
            px += (long)dw * dh;
            render_ops(canvas, w, h, fnt, &cur[p], pics, THUMB_MAX_SLOTS, scale,
                       render_scale(x, scale), render_scale(y, scale),
                       render_scale(dw, scale) < 1 ? 1 : render_scale(dw, scale),
                       render_scale(dh, scale) < 1 ? 1 : render_scale(dh, scale));
        }
        double mv = render_now_us() - t0;
        render_stats(ms, calls);
        printf("  move:    %6.2f ms   %ld px repainted, %ld rect draws, %ld text draws, %ld image draws\n",
               mv / 1000.0, px, calls[0], calls[1], calls[2]);
    }
```

(Note `DrawList` copies share the `op` array by value, which is what `cli_bench` already relies on in the damage block; keep that pattern.)

- [ ] **Step 2: Run it and read the number**

Run: `cd ~/bananatube && cmake --build build -j$(nproc) >/dev/null && cd build && BANANATUBE_FONTS=$HOME/BananaLinux/ui/assets ./bananatube --bench results plus 20 --query crab`
Expected: a `  move:` line appears with a non-zero pixel count and a few hundred draw calls. Record the number in the commit message — it is the "before".

- [ ] **Step 3: Commit**

```bash
git add src/main.c
git commit -m "bench: measure one selection move, not only a full repaint

--bench repaints everything on purpose, so it cannot show what the app actually
paints when the selection moves. This drives the same diff the app drives and
times render_ops over the damaged rectangles only."
```

---

### Task 2: `render_ops` rejects ops outside the repaint rectangle

**Files:**
- Modify: `src/display/render.c:192-201` (`render_ops`)
- Test: `tests/test_render.c`

**Interfaces:**
- Consumes: `render_scale(int v, float scale)`, `render_stats_reset()` / `render_stats(double *ms, long *calls)` (`src/display/render.h`).
- Produces: no signature change. `render_ops` keeps its exact prototype.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_render.c` before `test_done()`:

```c
    /* ---- an op that cannot land in the rectangle being repainted is not drawn ----
     * A selection move repaints a small rectangle; the frame's other 60-odd ops are still walked, and their
     * text is still measured, for every one of them. render_stats counts what was really drawn. */
    DrawList outside;
    dl_reset(&outside);
    for (int i = 0; i < 20; i++) dl_rect(&outside, 500, 10 + i, 20, 20, 0xffffffff);
    dl_rect(&outside, 5, 5, 10, 10, 0xffffffff);        /* the one inside the rectangle */
    render_stats_reset();
    render_ops(c, w, h, NULL, &outside, tex, 1, 1.0f, 0, 0, 32, 32);
    long calls[3];
    double ms[3];
    render_stats(ms, calls);
    check_int(calls[0], 1);                             /* the twenty outside it were not drawn */

    /* and at 1.6, where the rectangle is in panel pixels and the ops are in screen units */
    render_stats_reset();
    render_ops(c, 1024, 768, NULL, &outside, tex, 1, 1.6f, 0, 0, 52, 52);
    render_stats(ms, calls);
    check_int(calls[0], 1);
```

- [ ] **Step 2: Run it to make sure it fails**

Run: `cd ~/bananatube && cmake --build build -j$(nproc) >/dev/null && ./build/test_render`
Expected: FAIL — `calls[0]` is 21, not 1.

- [ ] **Step 3: Implement the early-out**

In `src/display/render.c`, replace the loop body of `render_ops`:

```c
    for (int k = 0; k < dl->n; k++) {
        const DrawOp *op = &dl->op[k];
        /* Nothing this op draws can land in the rectangle being repainted, so none of it is drawn: its text
         * is not measured and its picture is not walked. The op's own box is the bound that matters -- that
         * is the box dl_diff hands to damage_add, so if an op drew outside it the damage would already leave
         * the stale pixels behind. Scaled the same way draw_op scales it, because the rectangle is in panel
         * pixels and the op is in the UI's 640x480 units. */
        int bx = render_scale(op->x, scale), by = render_scale(op->y, scale);
        int bx2 = render_scale(op->x + op->w, scale), by2 = render_scale(op->y + op->h, scale);
        if (bx2 <= cx || by2 <= cy || bx >= cx + cw || by >= cy + ch) continue;
        draw_op(&rc, op, fnt, tex, tex_n, cx, cy, cw, ch, scale);
    }
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cd ~/bananatube && cmake --build build -j$(nproc) >/dev/null && sh tests/run.sh`
Expected: PASS, 100% of 16 tests (plus the new checks in `render`).

- [ ] **Step 5: Measure the move again**

Run: `cd ~/bananatube/build && BANANATUBE_FONTS=$HOME/BananaLinux/ui/assets ./bananatube --bench results plus 20 --query crab`
Expected: the `  move:` line's `rect draws` / `text draws` / `image draws` fall to only the ops inside the rectangle, and the ms falls with them. If it does not fall, stop and report — the cost is not in the op walk.

- [ ] **Step 6: Commit**

```c
git add src/display/render.c tests/test_render.c
git commit -m "render: do not draw the ops that cannot land in the repainted rectangle

A selection move repaints a small rectangle but paid for every op in the frame --
text measured, pictures walked -- because each kind clips only after its work."
```

---

### Task 3: The type, radius and spacing vocabulary

**Files:**
- Modify: `src/ui/widget.h`, `src/ui/widget.c`
- Modify: `src/ui/view.c`, `src/ui/settings.c` (replace the literal sizes/radii that match the vocabulary)
- Test: `tests/test_widget.c` (new)

**Interfaces:**
- Consumes: `dl_text(dl, x, y, w, h, text, FontId font, int size, uint32_t color, HAlign, VAlign)` (`src/display/drawlist.h`), `UiTheme` (`src/ui/theme.h`).
- Produces: the constants `W_SIZE_DISPLAY`, `W_SIZE_TITLE`, `W_SIZE_BODY`, `W_SIZE_SMALL`; `W_RADIUS_CARD`, `W_RADIUS_CHIP`; `W_SPACE_XS`, `W_SPACE_S`, `W_SPACE_M`, `W_SPACE_L`; and `void widget_tiny(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, const char *text, HAlign align)`. `widget_title`/`widget_label`/`widget_small`/`widget_mono` keep their exact prototypes.

The four steps, one colour role each: display 19 `FONT_UI_BOLD` `ink`; title 14 `FONT_UI` `ink`; body 12 `FONT_UI` `muted`; small 11 `FONT_UI` `faint`.

The accent rule, in a comment next to the constants: the accent appears in exactly three places per screen — the focused item, the primary action chip, and one status or progress element. Anything else that is accent today (a section heading, a hint chip that is not the primary action) becomes `ink` or `muted`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_widget.c`:

```c
/* The shared pieces: the sizes and colour roles every screen draws with. If these drift the screens stop
 * agreeing with each other, which is exactly what "it looks like a first grader made it" was. */
#include "ui/widget.h"
#include "test.h"

#include <string.h>

static const DrawOp *first_text(const DrawList *dl)
{
    for (int i = 0; i < dl->n; i++) if (dl->op[i].kind == OP_TEXT) return &dl->op[i];
    return NULL;
}

int main(void)
{
    UiTheme t = ui_theme();
    DrawList dl;

    dl_reset(&dl);
    widget_title(&dl, &t, 0, 0, 100, 20, "Title", ALIGN_LEFT);
    const DrawOp *op = first_text(&dl);
    check(op != NULL);
    check_int(op->size, W_SIZE_DISPLAY);
    check_int(op->font, FONT_UI_BOLD);
    check_int(op->color, t.ink);

    dl_reset(&dl);
    widget_label(&dl, &t, 0, 0, 100, 20, "Label", ALIGN_LEFT);
    op = first_text(&dl);
    check_int(op->size, W_SIZE_TITLE);
    check_int(op->font, FONT_UI);
    check_int(op->color, t.ink);

    dl_reset(&dl);
    widget_small(&dl, &t, 0, 0, 100, 20, "Body", ALIGN_LEFT);
    op = first_text(&dl);
    check_int(op->size, W_SIZE_BODY);
    check_int(op->color, t.muted);

    dl_reset(&dl);
    widget_tiny(&dl, &t, 0, 0, 100, 20, "Small", ALIGN_LEFT);
    op = first_text(&dl);
    check_int(op->size, W_SIZE_SMALL);
    check_int(op->color, t.faint);

    /* the four steps are four different sizes, in the order they are used */
    check(W_SIZE_DISPLAY > W_SIZE_TITLE && W_SIZE_TITLE > W_SIZE_BODY && W_SIZE_BODY > W_SIZE_SMALL);

    test_done();
}
```

- [ ] **Step 2: Add the test target and run it to make sure it fails**

In `CMakeLists.txt`, add `widget` to the test names loop:

```cmake
    foreach(t drawlist keys conf parse select lru ui worker render history args settings util http theme widget)
```

Run: `cd ~/bananatube && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build -j$(nproc)`
Expected: FAIL to compile — `widget_tiny` and `W_SIZE_*` are not defined.

- [ ] **Step 3: Add the vocabulary to `src/ui/widget.h`**

After the includes, before the first declaration:

```c
/* The UI's vocabulary, so the screens agree. Sizes are in screen units (the panel is 640x480 whatever its
 * pixels). Four steps, one colour role each. */
#define W_SIZE_DISPLAY 19
#define W_SIZE_TITLE   14
#define W_SIZE_BODY    12
#define W_SIZE_SMALL   11

/* One radius set: cards and fields, then chips and badges. A pill is half its height and is spelled that way
 * at the call site. */
#define W_RADIUS_CARD  8
#define W_RADIUS_CHIP  4

/* One spacing rhythm. */
#define W_SPACE_XS     6
#define W_SPACE_S      10
#define W_SPACE_M      16
#define W_SPACE_L      24

/* The accent rule: three places per screen -- the focused item, the primary action chip, and one status or
 * progress element. Everything else is ink, muted or faint. More than that is what made the screens noisy. */
```

Add the declaration after `widget_small`:

```c
/* The fourth step: the smallest line on a card and in a hint bar. */
void widget_tiny(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, const char *text, HAlign align);
```

- [ ] **Step 4: Use them in `src/ui/widget.c` and add `widget_tiny`**

Replace the literal sizes and radii in the existing functions with the constants (`widget_title` → `W_SIZE_DISPLAY`, `widget_label` → `W_SIZE_TITLE`, `widget_small` → `W_SIZE_BODY`, `widget_hint`'s chip radius 4 → `W_RADIUS_CHIP`, `widget_well`/`widget_panel` callers' 8 → `W_RADIUS_CARD`), and add:

```c
void widget_tiny(DrawList *dl, const UiTheme *t, int x, int y, int w, int h, const char *text, HAlign align)
{
    dl_text(dl, x, y, w, h, text, FONT_UI, W_SIZE_SMALL, t->faint, align, VALIGN_MIDDLE);
}
```

- [ ] **Step 5: Apply the vocabulary at the call sites**

In `src/ui/view.c` and `src/ui/settings.c`, replace every literal that is now a constant: text sizes 12/13/14/19/22 that mean "a body/title/display line", and radii 8 and 4. Leave numbers that mean something local (a bar's half-height, the 3px selection rim, the 10px picture radius).

`draw_details`'s title is `FONT_UI_BOLD 22` — that is `W_SIZE_DISPLAY` on the bottom panel (the panel's own display line), so use the constant. Its meta lines become `widget_tiny`.

- [ ] **Step 6: Run the tests**

Run: `cd ~/bananatube && sh tests/run.sh`
Expected: PASS, 100% — 17 tests now (`widget` added). `test_ui.c`'s draw-list counts compare a frame against the same frame, so size changes do not break them.

- [ ] **Step 7: Look at it**

Run: `cd ~/bananatube/build && BANANATUBE_FONTS=$HOME/BananaLinux/ui/assets ./bananatube --shot /tmp/opencode/shots/scale-results.ppm results plus && BANANATUBE_FONTS=$HOME/BananaLinux/ui/assets ./bananatube --shot /tmp/opencode/shots/scale-details.ppm results bottom plus` then `magick /tmp/opencode/shots/scale-results.ppm /tmp/opencode/shots/scale-results.png`
Expected: the screens are recognisably the same, with the meta lines one step smaller. If a line now overflows its box, widen the box rather than dropping the step.

- [ ] **Step 8: Commit**

```bash
git add src/ui/widget.h src/ui/widget.c src/ui/view.c src/ui/settings.c tests/test_widget.c CMakeLists.txt
git commit -m "ui: one type scale, one radius set and one spacing rhythm

Every screen drew its own sizes, which is what made them look like they came
from three different apps. They are named in widget.h now and pinned by a test."
```

---

### Task 4: The brand-mark loader

**Files:**
- Create: `src/display/logo.h`, `src/display/logo.c`
- Modify: `CMakeLists.txt` (add `src/display/logo.c` to `bananatube_core`; add libpng)
- Test: `tests/test_logo.c` (new)

**Interfaces:**
- Consumes: `BottomTex { const uint8_t *pixels; int w, h; }` (`src/display/draw.h`), the renderer's pixel order (`src/display/render.c` reads `uint16_t c = (p[0] << 8) | p[1]` and expands it as RGB565: red in bits 15-11, green 10-5, blue 4-0).
- Produces: `bool logo_load(BottomTex *out, const char *path, uint32_t back);` where `back` is the theme's `bar` colour as `0xRRGGBB`. Returns false and logs when the file cannot be read or decoded. On success `out->pixels` is heap-allocated (`free()` it) and holds `w*h*2` bytes in the RGB565 order above, with the PNG's alpha composited over `back`.

- [ ] **Step 1: Write the fixture and the failing test**

Copy the asset in and make a corrupt one:

```bash
cp ~/BananaLinux/ui/assets/banana/banana-48.png ~/bananatube/tests/data/banana-48.png
printf 'not a png at all\n' > ~/bananatube/tests/data/banana-broken.png
```

Create `tests/test_logo.c`:

```c
/* The header's brand mark: a PNG decoded into the same RGB565 the pictures are, with its transparency
 * composited over the bar it sits on. A missing or corrupt file must be a plain "no mark", never a crash and
 * never a black box. */
#include "display/logo.h"
#include "test.h"

#include <stdlib.h>

int main(void)
{
    BottomTex mark;
    memset(&mark, 0, sizeof mark);

    check(logo_load(&mark, TEST_DATA_DIR "/banana-48.png", 0x2e2b33));
    check_int(mark.w, 48);
    check_int(mark.h, 48);
    check(mark.pixels != NULL);

    /* the mark is three yellow bananas on nothing: a yellow pixel must come out yellow, and a transparent
     * one must come out as the bar colour we passed */
    int yellow = 0, bar = 0;
    for (int i = 0; i < mark.w * mark.h; i++) {
        uint16_t c = (uint16_t)((mark.pixels[i * 2] << 8) | mark.pixels[i * 2 + 1]);
        int r = (c >> 11) & 0x1f, g = (c >> 5) & 0x3f, b = c & 0x1f;
        if (r > 20 && g > 20 && b < 12) yellow++;
        if (r == 0x05 && g == 0x15 && b == 0x13) bar++;      /* 0x2e2b33 as RGB565 */
    }
    check(yellow > 100);                                    /* the bananas */
    check(bar > 100);                                       /* where the PNG was transparent */
    free((void *)mark.pixels);

    /* a broken file, and one that is not there at all */
    check(!logo_load(&mark, TEST_DATA_DIR "/banana-broken.png", 0x2e2b33));
    check(mark.pixels == NULL);
    check(!logo_load(&mark, TEST_DATA_DIR "/no-such-file.png", 0x2e2b33));
    check(mark.pixels == NULL);

    test_done();
}
```

- [ ] **Step 2: Wire the target and run it to make sure it fails**

In `CMakeLists.txt`: add `logo` to the test names loop, add `src/display/logo.c` to the `bananatube_core` source list, and add libpng next to the other `pkg_check_modules`:

```cmake
pkg_check_modules(PNG   REQUIRED libpng)
```
```cmake
    ${CURL_INCLUDE_DIRS} ${CJSON_INCLUDE_DIRS} ${JPEG_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS} ${PNG_INCLUDE_DIRS})
```
```cmake
    ${CURL_LIBRARIES} ${CJSON_LIBRARIES} ${JPEG_LIBRARIES} ${FREETYPE_LIBRARIES} ${PNG_LIBRARIES} pthread m)
```

Run: `cd ~/bananatube && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build -j$(nproc)`
Expected: FAIL to compile — `logo.h` does not exist.

- [ ] **Step 3: Write `src/display/logo.h` and `src/display/logo.c`**

`logo.h`:

```c
/* logo.h -- the header's brand mark, decoded from a PNG into the same pictures the thumbnails are.
 *
 * A PNG has an alpha channel and the renderer's pictures do not, so the mark is composited onto the bar it
 * will sit on while it is decoded. One 48x48 decode at startup.
 */
#ifndef BANANATUBE_LOGO_H
#define BANANATUBE_LOGO_H

#include "display/draw.h"

#include <stdbool.h>
#include <stdint.h>

/* Decodes `path` into `out` (RGB565, high byte first). `back` is the bar colour as 0xRRGGBB, which the PNG's
 * transparent pixels become. False and a log line when it cannot be read or decoded. On success `out->pixels`
 * is malloc'd; free() it. On failure `out` is left with pixels == NULL. */
bool logo_load(BottomTex *out, const char *path, uint32_t back);

#endif /* BANANATUBE_LOGO_H */
```

`logo.c`: use libpng's simplified API (`png_image_begin_read_from_file` / `png_image_finish_read` into an RGBA buffer, `PNG_FORMAT_RGBA`), then build the RGB565 buffer. Set `out->pixels = NULL` and `out->w = out->h = 0` on the first line, so every failure path satisfies the contract. Log with `log_line()` on failure (`#include "util.h"`).

- [ ] **Step 4: Run the test to verify it passes**

Run: `cd ~/bananatube && sh tests/run.sh`
Expected: PASS, 100% — 18 tests.

- [ ] **Step 5: Commit**

```bash
git add src/display/logo.h src/display/logo.c tests/test_logo.c tests/data/banana-48.png tests/data/banana-broken.png CMakeLists.txt
git commit -m "logo: the header's brand mark, decoded from a PNG at startup

The renderer's pictures have no alpha, so the mark is composited onto the bar
colour as it is decoded. A missing or corrupt file is just no mark."
```

---

### Task 5: The battery reader

**Files:**
- Create: `src/ui/battery.h`, `src/ui/battery.c`
- Modify: `CMakeLists.txt` (`bananatube_core` sources, test list)
- Test: `tests/test_battery.c` (new)
- Create: `tests/data/power_supply/{battery/{type,capacity,status},charger/{type,online}}`

**Interfaces:**
- Produces:
  - `typedef struct { int percent; bool charging; } Battery;` — `percent` is 100 for "unknown", and a `bool have` says whether there is a battery at all. Use `typedef struct { bool have; int percent; bool charging; } Battery;` so a machine with no battery draws nothing.
  - `void battery_read(Battery *b, const char *sysdir);` — reads `sysdir` now.
  - `bool battery_get(Battery *b, const char *sysdir, double now_s, double secs);` — calls `battery_read` when `secs` have passed since the last call, otherwise leaves `*b` alone; returns whether it re-read.

The rule, from the menu (`ui/src/library.cpp`) and `bananad`: the first supply whose `type` is `Battery` gives `capacity`; **any** supply with `online` == 1 means on the cable, because the rk817 gauge reports `Discharging` while charging.

- [ ] **Step 1: Write the fixtures and the failing test**

```bash
mkdir -p ~/bananatube/tests/data/power_supply/battery ~/bananatube/tests/data/power_supply/charger ~/bananatube/tests/data/power_supply_empty
printf 'Battery\n' > ~/bananatube/tests/data/power_supply/battery/type
printf '67\n'      > ~/bananatube/tests/data/power_supply/battery/capacity
printf 'Discharging\n' > ~/bananatube/tests/data/power_supply/battery/status
printf 'USB\n'     > ~/bananatube/tests/data/power_supply/charger/type
printf '1\n'       > ~/bananatube/tests/data/power_supply/charger/online
```

Create `tests/test_battery.c`:

```c
/* The header's battery: the percentage and whether it is on the cable. The gauge lies about `status` while
 * charging, so "on the cable" is any supply saying online -- the same rule the menu uses. */
#include "ui/battery.h"
#include "test.h"

int main(void)
{
    Battery b;

    /* the fixture: 67%, and the charger says online, so it is charging even though status says Discharging */
    memset(&b, 0, sizeof b);
    battery_read(&b, TEST_DATA_DIR "/power_supply");
    check(b.have);
    check_int(b.percent, 67);
    check(b.charging);

    /* nothing but the battery: still 67%, not charging */
    memset(&b, 0, sizeof b);
    battery_read(&b, TEST_DATA_DIR "/power_supply_empty");
    check(!b.have);
    check_int(b.percent, 100);
    check(!b.charging);

    /* the cache: the second call inside the window does not read again */
    b.have = false;
    check(battery_get(&b, TEST_DATA_DIR "/power_supply", 100.0, 2.0));
    check(b.have);
    b.percent = 1;                                     /* scribble on it */
    check(!battery_get(&b, TEST_DATA_DIR "/power_supply", 101.0, 2.0));
    check_int(b.percent, 1);                           /* it was left alone */
    check(battery_get(&b, TEST_DATA_DIR "/power_supply", 103.0, 2.0));
    check_int(b.percent, 67);

    test_done();
}
```

- [ ] **Step 2: Wire the target and run it to make sure it fails**

Add `battery` to the test names loop and `src/ui/battery.c` to the `bananatube_core` sources. Run: `cd ~/bananatube && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build -j$(nproc)`
Expected: FAIL to compile — `ui/battery.h` does not exist.

- [ ] **Step 3: Write `src/ui/battery.h` and `src/ui/battery.c`**

`battery.h`:

```c
/* battery.h -- what the header says about the battery.
 *
 * Reads /sys/class/power_supply the way the menu does: the first entry whose `type` is "Battery" holds the
 * percentage, and ANY entry with `online` == 1 means on the cable -- the rk817 gauge reports "Discharging"
 * while charging, so its own status is not to be trusted. No battery at all (a different board, or mains) is
 * have == false, and the header then draws no percentage.
 */
#ifndef BANANATUBE_BATTERY_H
#define BANANATUBE_BATTERY_H

#include <stdbool.h>

typedef struct {
    bool have;          /* there is a battery to report */
    int  percent;       /* 0..100, or 100 when unknown */
    bool charging;      /* on the cable */
} Battery;

/* Reads `sysdir` (<sys>/class/power_supply) now. Never fails: no battery is have == false. */
void battery_read(Battery *b, const char *sysdir);

/* The same, at most once every `secs` seconds: the header draws every frame and this is a few file reads.
 * Returns whether it re-read; `*b` is untouched when it did not. */
bool battery_get(Battery *b, const char *sysdir, double now_s, double secs);

#endif /* BANANATUBE_BATTERY_H */
```

`battery.c`: `opendir`/`readdir` the directory, skip `.`/`..`, read `type`, `capacity`, `online` from each entry with a small `read_int`/`read_line` helper, apply the rule above. Keep the last-read time in a `static double` for `battery_get`.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cd ~/bananatube && sh tests/run.sh`
Expected: PASS, 100% — 19 tests.

- [ ] **Step 5: Commit**

```bash
git add src/ui/battery.h src/ui/battery.c tests/test_battery.c tests/data/power_supply CMakeLists.txt
git commit -m "battery: the header's percentage and cable state, read as the menu reads it"
```

---

### Task 6: The header — brand mark, two-tone wordmark, battery

**Files:**
- Modify: `src/ui/view.h` (the `AppState` fields and two setters), `src/ui/view.c` (`draw_header`), `src/ui/widget.h`, `src/ui/widget.c` (`widget_header`)
- Modify: `src/main.c` (load the mark once, keep the battery fresh, hand both to the UI)
- Test: `tests/test_ui.c`

**Interfaces:**
- Consumes: `logo_load` (Task 4), `Battery`/`battery_get` (Task 5), `widget_tiny` and the constants (Task 3), `font_text_width(FontCtx *, FontId, const char *, int size)` (`src/font.h`), `dl_image` (`src/display/drawlist.h`).
- Produces:
  - `typedef struct { const BottomTex *logo; int tube_x; int battery; bool charging; } HeaderInfo;` in `widget.h` — `logo` NULL or `tube_x` 0 means no wordmark; `battery` < 0 means no percentage.
  - `void widget_header(DrawList *dl, const UiTheme *t, const HeaderInfo *info);` — the `title` parameter is **removed**; the header's left side is the brand and the wordmark now.
  - `void ui_set_logo(AppState *st, const BottomTex *logo, int tube_x);` and `void ui_set_battery(AppState *st, int percent, bool charging);` in `view.h`, plus the `AppState` fields `const BottomTex *logo; int wordmark_x; int battery; bool charging;`.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_ui.c` before `test_done()`:

```c
    /* ---- the header ---- */
    ui_init(&st);
    ui_set_battery(&st, 67, true);
    static uint8_t px[4 * 4 * 2];
    memset(px, 0x11, sizeof px);
    BottomTex mark = { px, 4, 4 };
    ui_set_logo(&st, &mark, 40);
    DrawList h1;
    ui_draw_top(&st, &h1, NULL, 0);
    int images = 0, bananas = 0, tubes = 0, percents = 0;
    for (int i = 0; i < h1.n; i++) {
        if (h1.op[i].kind == OP_IMAGE) images++;
        if (h1.op[i].kind != OP_TEXT) continue;
        if (strcmp(h1.op[i].text, "Banana") == 0) bananas++;
        if (strcmp(h1.op[i].text, "Tube") == 0) tubes++;
        if (strcmp(h1.op[i].text, "67%") == 0) percents++;
    }
    check_int(images, 1);                              /* the brand mark, once */
    check_int(bananas, 1);
    check_int(tubes, 1);
    check_int(percents, 1);
    for (int i = 0; i < h1.n; i++) {
        if (h1.op[i].kind != OP_TEXT) continue;
        if (strcmp(h1.op[i].text, "Banana") == 0) check_int((int)h1.op[i].color, (int)st.theme.accent);
        if (strcmp(h1.op[i].text, "Tube") == 0)   check_int((int)h1.op[i].color, (int)st.theme.ink);
    }

    /* nobody set a mark and there is no battery: the wordmark alone, and no percentage -- not "0%" */
    ui_init(&st);
    ui_draw_top(&st, &h1, NULL, 0);
    images = percents = bananas = 0;
    for (int i = 0; i < h1.n; i++) {
        if (h1.op[i].kind == OP_IMAGE) images++;
        if (h1.op[i].kind != OP_TEXT) continue;
        if (strcmp(h1.op[i].text, "Banana") == 0) bananas++;
        if (strchr(h1.op[i].text, '%') != NULL) percents++;
    }
    check_int(images, 0);                              /* no mark: nothing is drawn for it, not a black box */
    check_int(bananas, 1);                             /* the wordmark is there whether or not the mark is */
    check_int(percents, 0);

- [ ] **Step 2: Run it to make sure it fails**

Run: `cd ~/bananatube && cmake --build build -j$(nproc) >/dev/null && ./build/test_ui`
Expected: FAIL to compile — `ui_set_battery`, `ui_set_logo`, `widget_tiny` call sites.

- [ ] **Step 3: Change `widget_header`**

In `widget.h`, declare:

```c
/* What the header shows on the right and as the brand: the mark (NULL for none), where the wordmark's second
 * half starts (0 for none), and the battery (-1 for no percentage). */
typedef struct {
    const BottomTex *logo;
    int              tube_x;
    int              battery;
    bool             charging;
} HeaderInfo;

void widget_header(DrawList *dl, const UiTheme *t, const HeaderInfo *info);
```

In `widget.c`, `widget_header` becomes: the 44px `bar`; the mark via `dl_image` at x=12 (only when `info && info->logo && info->logo->pixels`) at 24x24, `FIT_CONTAIN`, placeholder `t->bar`; `Banana` in `t->accent` at x = (mark ? 44 : 16) via a `dl_text` at `W_SIZE_DISPLAY` `FONT_UI_BOLD`; `Tube` in `t->ink` at `(mark ? 44 : 16) + info->tube_x` when `info->tube_x > 0`; then the battery and the clock on the right. Keep the clock exactly as it is now (`widget_mono` at `SCREEN_W - 140`, width 124, `ALIGN_RIGHT`); put the battery percentage at `SCREEN_W - 196` with a small `widget_bar` under it, only when `info->battery >= 0`.

- [ ] **Step 4: Add the state and the setters**

In `view.h`'s `AppState`, after `history_scroll`:

```c
    const BottomTex *logo;       /* the brand mark, owned by main.c; NULL until it loads */
    int      wordmark_x;         /* where "Tube" starts: "Banana" measured once by main.c */
    int      battery;            /* 0..100, or -1 when there is none to report */
    bool     charging;
```

Declare after `ui_open_settings`:

```c
/* The header's brand mark and its two-tone wordmark. main.c owns the picture and measures the offset once
 * after font_init(), because the draw layer has no font context to measure with. */
void ui_set_logo(AppState *st, const BottomTex *logo, int tube_x);
/* The battery, as the header shows it: 0..100 and whether it is on the cable, or -1 for none. */
void ui_set_battery(AppState *st, int percent, bool charging);
```

In `view.c`, implement both as two-line assignments, and `draw_header` becomes:

```c
static void draw_header(AppState *st, DrawList *dl)
{
    HeaderInfo info = { st->logo, st->wordmark_x, st->battery, st->charging };
    widget_header(dl, &st->theme, &info);
}
```

- [ ] **Step 5: Wire `main.c`**

After `ui.theme = ui_theme_with(theme, accent);` (line ~815) and after the font is initialised:

```c
    /* The header's mark: decoded once, composited onto the bar it sits on, so its transparency is not a
     * black box. The wordmark's second half starts after the first, measured here because the draw layer has
     * no font to measure with. A missing file is no mark, not a failure. */
    static BottomTex logo;
    if (logo_load(&logo, LOGO_PATH, theme_bar_rgb(ui.theme))) {
        ui_set_logo(&ui, &logo, font_text_width(fnt, FONT_UI_BOLD, W_SIZE_DISPLAY, "Banana") + 6);
    }
```

with `#define LOGO_PATH "/usr/share/banana/banana/banana-48.png"` next to the other device paths at the top of `main.c`, a small `static uint32_t theme_bar_rgb(const UiTheme *t)` returning `t->bar & 0xffffff`, and the battery refreshed in the main loop just before `ui_draw_top`:

```c
        /* A few file reads, so twice a second rather than every frame. The static keeps the last value when
         * battery_get decides not to re-read. */
        static Battery bat = { false, 100, false };
        if (battery_get(&bat, "/sys/class/power_supply", now_ms() / 1000.0, 2.0))
            ui_set_battery(&ui, bat.have ? bat.percent : -1, bat.charging);
```

(`now_ms()` is the monotonic milliseconds the frame timing already uses.)

Also update the two other `widget_header` call sites if any exist — `grep -rn widget_header src/` must show only `view.c`.

- [ ] **Step 6: Run the tests and look at it**

Run: `cd ~/bananatube && sh tests/run.sh`
Expected: PASS, 100% — 19 tests.

Run: `cd ~/bananatube/build && BANANATUBE_FONTS=$HOME/BananaLinux/ui/assets ./bananatube --shot /tmp/opencode/shots/header.ppm results top plus && magick /tmp/opencode/shots/header.ppm /tmp/opencode/shots/header.png`
Expected: the scoreboard-free header from the mockup: bananas, `Banana` yellow, `Tube` white, the percentage and its bar, the clock. The mark's corners show the bar colour, not black.

- [ ] **Step 7: Commit**

```bash
git add src/ui/view.h src/ui/view.c src/ui/widget.h src/ui/widget.c src/main.c tests/test_ui.c
git commit -m "header: the banana mark, the two-tone wordmark and the battery

The header said 'YouTube' and the time. It now says whose app this is, in the
menu's own colours, and the percentage comes from the same sysfs rule the menu uses."
```

---

### Task 7: The rail

One row of cards, the focused one wider, push left and right to move. The viewport is the top panel, which has no touchscreen, so this is D-pad only and the hit-test the grid carried is dead code.

The scroll state disappears with the grid: with one row there is nothing to scroll, so `results_scroll_into_view`, `Results.top_row` and `results_rows` go, and every card's box is computed from the selection.

**Files:**
- Modify: `src/ui/results.h`, `src/ui/results.c`, `src/ui/view.c` (`ui_key`'s results branch)
- Test: `tests/test_ui.c`

**Interfaces:**
- Consumes: `DrawList`/`dl_text`/`dl_text_wrapped`/`dl_image`/`dl_push_clip`/`dl_pop_clip`, `widget_*` and the Task 3 constants.
- Produces, replacing the grid's four geometry calls:
  - `#define RAIL_CARD_W 150`, `#define RAIL_CARD_H 168`, `#define RAIL_GAP 12`, `#define RAIL_PAGE 3`
  - `int  results_card_w(const Results *r, int i);` — `RAIL_CARD_W * 3 / 2` for the selected card, else `RAIL_CARD_W`
  - `void results_card_box(const Results *r, int i, int x, int y, int w, int *bx, int *by, int *bw, int *bh);`
  - `int  results_visible(const Results *r, int x, int w, int *out, int max);` — the cards whose box meets `[x, x+w)`
  - removed: `results_rows`, `results_visible_rows`, `results_tile_box`, `results_hit`, `results_scroll_into_view`, and `Results.top_row`
  - `results_move(Results *r, BananaKey key)` now: `BK_LEFT` −1, `BK_RIGHT` +1, `BK_UP` −`RAIL_PAGE`, `BK_DOWN` +`RAIL_PAGE`, all clamped, `false` when nothing moved.

The box rule: the selected card is centred at `bx = x + (w - bw) / 2`; each card before it is placed to its left and each after it to its right, `RAIL_GAP` apart. A card's position therefore depends on where the selection is, which is why the box takes the whole `Results`.

- [ ] **Step 1: Write the failing test**

Replace the `/* ---- the grid ---- */` block in `tests/test_ui.c` (lines 26-92) with the rail's:

```c
    /* ---- the rail ---- */
    Results r;
    fill(&r, 8);
    check_int(r.sel, 0);
    check(!results_move(&r, BK_LEFT));                 /* already the first */
    check(!results_move(&r, BK_UP));
    check(results_move(&r, BK_RIGHT));
    check_int(r.sel, 1);
    check(results_move(&r, BK_RIGHT));
    check_int(r.sel, 2);
    check(results_move(&r, BK_LEFT));
    check_int(r.sel, 1);
    check(results_move(&r, BK_DOWN));                  /* a page forward */
    check_int(r.sel, 4);
    check(results_move(&r, BK_UP));
    check_int(r.sel, 1);
    r.sel = 6;
    check(results_move(&r, BK_DOWN));                  /* 8 cards: the page clamps to the last */
    check_int(r.sel, 7);
    check(!results_move(&r, BK_RIGHT));
    fill(&r, 1);                                       /* one card: nothing to move to */
    check(!results_move(&r, BK_LEFT));
    check(!results_move(&r, BK_RIGHT));
    check(!results_move(&r, BK_DOWN));
    fill(&r, 0);                                       /* and none at all */
    check(!results_move(&r, BK_RIGHT));

    /* the selected card is wider and centred; its neighbours sit a gap away on either side */
    fill(&r, 8);
    r.sel = 3;
    int bx, by, bw, bh;
    results_card_box(&r, 3, 8, 88, SCREEN_W - 16, &bx, &by, &bw, &bh);
    check_int(bw, RAIL_CARD_W * 3 / 2);
    check_int(bx, 8 + (SCREEN_W - 16 - bw) / 2);
    check_int(by, 88);
    check_int(bh, RAIL_CARD_H);
    int bx2, bw2;
    results_card_box(&r, 4, 8, 88, SCREEN_W - 16, &bx2, &by, &bw2, &bh);
    check_int(bx2, bx + bw + RAIL_GAP);
    check_int(bw2, RAIL_CARD_W);
    results_card_box(&r, 2, 8, 88, SCREEN_W - 16, &bx2, &by, &bw2, &bh);
    check_int(bx2 + bw2 + RAIL_GAP, bx);

    /* the screen holds three cards and a peek, and moving brings the next one in */
    r.sel = 0;
    int vis[RESULTS_MAX];
    int n = results_visible(&r, 8, SCREEN_W - 16, vis, RESULTS_MAX);
    check(n >= 3);
    check(n <= 5);
    check_int(vis[0], 0);
    r.sel = 7;
    n = results_visible(&r, 8, SCREEN_W - 16, vis, RESULTS_MAX);
    int found = 0;
    for (int i = 0; i < n; i++) if (vis[i] == r.sel) found = 1;
    check(found);                                      /* the selection is always among them */

    /* a picture arrives for a video and its card finds it */
    results_set_thumb(&r, "vid00000003", 7);
    check_int(r.tex_slot[3], 7);
    results_set_thumb(&r, "not-here", 9);              /* no such video: nothing changes */
    check_int(r.tex_slot[3], 7);

    /* previous/next step one result, not a page, and stop at the ends */
    fill(&r, 6);
    r.sel = 0;
    check(!results_step(&r, -1));
    check(results_step(&r, 1));
    check_int(r.sel, 1);
    r.sel = 5;
    check(!results_step(&r, 1));
    check(results_step(&r, -1));
    check_int(r.sel, 4);

    /* a very long title stays inside its card and is capped at two lines: the whole card is drawn inside
     * its own box, and the one wrapped op is the title */
    fill(&r, 4);
    memset(r.item[1].title, 'x', sizeof r.item[1].title - 1);
    r.item[1].title[sizeof r.item[1].title - 1] = 0;
    r.sel = 1;
    UiTheme th = ui_theme();
    DrawList one;
    dl_reset(&one);
    results_draw(&one, &r, &th, NULL, 0, 8, 88, SCREEN_W - 16, 392);
    results_card_box(&r, 1, 8, 88, SCREEN_W - 16, &bx, &by, &bw, &bh);
    int wrapped = 0;
    for (int i = 0; i < one.n; i++) {
        const DrawOp *o = &one.op[i];
        check(o->x >= bx && o->y >= by && o->x + o->w <= bx + bw && o->y + o->h <= by + bh);
        if (o->kind == OP_TEXT && o->wrap) { wrapped++; check_int(o->max_lines, 2); }
    }
    check_int(wrapped, 1);

    /* a move onto a card that has no picture yet: the new card's whole box is damaged, so the placeholder is
     * repainted and the card that lost the focus loses its highlight */
    fill(&r, 4);
    r.sel = 0;
    DrawList a, b2;
    dl_reset(&a);
    results_draw(&a, &r, &th, NULL, 0, 8, 88, SCREEN_W - 16, 392);
    r.sel = 1;
    dl_reset(&b2);
    results_draw(&b2, &r, &th, NULL, 0, 8, 88, SCREEN_W - 16, 392);
    Damage own, missed, dmg;
    dl_diff(&a, &b2, &own);
    damage_reset(&missed);
    damage_reset(&dmg);
    damage_next(&own, &missed, &dmg, &missed);
    results_card_box(&r, 1, 8, 88, SCREEN_W - 16, &bx, &by, &bw, &bh);
    check(damage_touches(&dmg, bx, by, bw, bh));
    results_card_box(&r, 0, 8, 88, SCREEN_W - 16, &bx, &by, &bw, &bh);
    check(damage_touches(&dmg, bx, by, bw, bh));       /* and the card that lost it */
```

Also remove the `results_hit` block (lines 79-86) and the `results_rows`/`results_visible_rows`/`results_tile_box` assertions.

- [ ] **Step 2: Run it to make sure it fails**

Run: `cd ~/bananatube && cmake --build build -j$(nproc) 2>&1 | head -20`
Expected: FAIL to compile — `RAIL_CARD_W`, `results_card_box` are not defined.

- [ ] **Step 3: Rewrite the geometry in `src/ui/results.h` / `results.c`**

Declare the five `RAIL_*` macros and the three functions above; delete `results_rows`, `results_visible_rows`, `results_tile_box`, `results_hit`, `results_scroll_into_view` and the `top_row` field. Keep `results_clear`, `results_append`, `results_set_thumb`, `results_step`, `results_move` (rewritten), `results_draw`.

`results_card_w` returns the wider width for `i == r->sel`. `results_card_box` centres the selected card and walks outwards from it in both directions. `results_visible` walks the same boxes and collects the indices whose `[bx, bx+bw)` meets `[x, x+w)`.

- [ ] **Step 4: Rewrite `results_draw`**

One clipped row: `dl_push_clip`, then for each visible index the card — `widget_panel` at `W_RADIUS_CARD` with `panel_hi` for the selected card and `panel` for the rest, the 3px accent rim on the selected card only, the picture via `dl_image` (`FIT_COVER`, the placeholder colour when there is no picture, `widget_tiny` "loading..." inside it), the duration badge, the title with `dl_text_wrapped` (`W_SIZE_TITLE`, two lines), and the channel/views line with `widget_tiny`. The picture's height is `bw * 9 / 16` so a wider card gets a wider picture.

- [ ] **Step 5: Update `ui_key`'s results branch**

In `view.c`, drop the `results_scroll_into_view` call:

```c
    case BK_LEFT: case BK_RIGHT: case BK_UP: case BK_DOWN: {
        if (!results_move(&st->results, key)) return UI_NONE;
        st->dirty = true;
        return UI_THUMBS;                         /* the visible set may have changed */
    }
```

- [ ] **Step 6: Run the tests**

Run: `cd ~/bananatube && sh tests/run.sh`
Expected: PASS, 100% — 19 tests.

- [ ] **Step 7: Look at it and move the selection**

Run:
```bash
cd ~/bananatube/build
BANANATUBE_FONTS=$HOME/BananaLinux/ui/assets ./bananatube --shot /tmp/opencode/shots/rail0.ppm results top plus
BANANATUBE_FONTS=$HOME/BananaLinux/ui/assets ./bananatube --keys RIGHT,RIGHT --query crab --out /tmp/opencode/shots/rail plus
magick /tmp/opencode/shots/rail0.ppm /tmp/opencode/shots/rail0.png
magick /tmp/opencode/shots/rail/02-RIGHT.ppm /tmp/opencode/shots/rail2.png
```
Expected: three cards and a peek, the focused one wider and centred; after two RIGHTs the focused card is the third and the row has shifted so the first is partly off the left edge. A long title wraps inside the card and the meta line stays inside it.

- [ ] **Step 8: Measure the move on the host**

Run: `cd ~/bananatube/build && BANANATUBE_FONTS=$HOME/BananaLinux/ui/assets ./bananatube --bench results plus 20 --query crab`
Expected: the `  move:` line is small — the rail draws three cards instead of a grid's six-plus, and Task 2's early-out drops everything outside the damage. Record the number in the commit message.

- [ ] **Step 9: Commit**

```bash
git add src/ui/results.h src/ui/results.c src/ui/view.c tests/test_ui.c
git commit -m "results: the rail, and no scroll state to keep

One row, the focused card wider, and every card's box computed from where the
selection is -- so the scroll offset, the row maths and the hit-test the top
panel could never receive all go."
```

- [ ] **Step 10: Ship it to the handheld**

The root filesystem is read-only squashfs, so the binary is bind-mounted over:

```bash
cd ~/BananaLinux && echo 'BANANATUBE_OVERRIDE_SRCDIR = /home/tom/bananatube' > output/rgds-plus/local.mk \
  && make -C output/rgds-plus bananatube-rebuild > /tmp/opencode/bt-ui.log 2>&1; echo EXIT=$?; rm -f output/rgds-plus/local.mk
ssh root@192.168.1.229 'kill -9 $(pidof bananatube) 2>/dev/null; sleep 1; umount /usr/bin/bananatube 2>/dev/null; echo prepped'
ssh root@192.168.1.229 'cat > /data/bin/bananatube.new' < output/rgds-plus/target/usr/bin/bananatube
ssh root@192.168.1.229 'chmod +x /data/bin/bananatube.new; cp /data/bin/bananatube.new /data/bin/bananatube; mount --bind /data/bin/bananatube /usr/bin/bananatube; md5sum /usr/bin/bananatube'
```

Then on the device: `BANANATUBE_FONTS=/usr/share/banana/banana bananatube --bench results plus 20 --query crab` for the device numbers, and open BananaTube from the menu and move the selection.

Expected: the header shows the mark and the percentage, the rail moves with the D-pad, and the `move:` number is a fraction of the pre-Task-2 one.
