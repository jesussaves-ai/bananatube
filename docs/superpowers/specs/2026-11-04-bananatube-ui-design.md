# BananaTube UI: the polish pass, the header, and the selection lag

Date: 2026-11-04
Repo: `bananatube` (client) — built and shipped by `BananaLinux`

## Intent, and why

The client works. It resolves a video, plays it through the handheld's decoder, and makes
sound. What it does not do is look or feel like it belongs next to the rest of BananaOS.
The owner's words: *"the UI looks like a first grader made it"*. Separately, moving the
selection is laggy — *"its still a very slow UI thats the main issue the videos are fine"*.

So this is two jobs that touch the same code:

1. Make every screen look deliberate, in the menu's own visual language.
2. Make moving the selection cheap enough to feel immediate.

## Goals

- One type scale, one spacing rhythm, one corner-radius set and one rule for when the accent
  appears, shared by every screen: results, the on-screen keyboard, the video detail panel,
  history and settings.
- The results screen becomes the horizontal rail the owner picked (direction B of three
  mockups), with the focused card growing.
- The header becomes: the banana mark, then **Banana** in the accent and **Tube** in the ink,
  on the left; battery and clock on the right.
- Moving the selection repaints only the pixels that changed, and costs a fraction of today's
  frame.

## Non-goals

- No new screens or features. No home feed, no channels, no playlists, no recommendations.
- No change to the playback path — the visionOS client, `audio-add` and the render context stay
  exactly as they are.
- No user-facing theme editor or extra layouts. The existing dark/light plus six named accents
  stays as it is.
- No new dependency beyond libpng, which is already in the image, and no new asset.

## Anti-goals

- Prettier but slower. If the polish pass adds per-frame work, it is wrong.
- A rewrite of the draw layer. The DrawList, the damage diff and the software renderer stay.

## Constraints

- RK3566, 1 GB RAM, no GPU compositor: the frame is rasterised by the CPU into a dumb buffer and
  flipped. Every pixel drawn is CPU work on a Cortex-A55.
- The UI is authored in 640x480 screen units and scaled (1.6 on the RG DS Plus). All layout
  numbers in this document are screen units.
- `DRAW_MAX` is 512 ops per frame; a frame that overflows drops its tail.
- The damage diff drives what gets repainted, and it treats an op's own box `(x, y, w, h)` as the
  bound of what that op draws (`dl_diff` → `damage_add`). `op_bounds()` exists but is unused.
- Match the menu: background `#17161c` (dark) / `#f3efe6` (light), ink `#f1ede2`, muted `#a39d8e`,
  faint `#8a8374`, accent from the six named ones (`yellow #f0b429` by default), Pixelify Sans
  already loaded as `FONT_MONO`.
- The client lives in `~/bananatube`; `BananaLinux` owns the Buildroot package and the pin.

## Approaches considered — the results screen

Three were mocked up in the browser and set side by side with today's screen.

- **A — Same grid, properly built.** Keep three columns, fix the craft. Cheapest, instantly
  recognisable, but keeps eight small targets and seven thumbnails per frame.
- **B — TV rail.** One horizontal row, the focused card wider, push left and right. Three large
  targets, and only three pictures to draw a frame instead of seven.
- **C — Reading list.** One result per row, thumbnail left, full-row focus bar. Most legible and
  least to draw, but only four results on screen.

**B was picked.** A was the safe choice and C the most legible, but B is the one that both reads
as a deliberate design and cuts the per-frame picture work, which is where the time goes.

## The work

### 1. A shared chrome pass — `src/ui/widget.c`, `src/ui/theme.c`

Every text op in the UI already goes through the `widget_*` helpers, so the type scale is one
file. Establish and apply:

- A four-step scale, in screen units: display 19 / title 14 / body 12 / small 11, each with one
  colour role (ink for display and title, muted for body, faint for small). Today the sizes are
  ad hoc per call site.
- One radius set: 8 for cards and fields, 4 for chips and badges, half-height for pills.
- One spacing rhythm: 6 / 10 / 16 / 24.
- The accent appears in exactly three places per screen: the focused item, the primary action
  chip, and one status or progress element. Today it also tints section headings and the clock,
  which is what makes the screen read as noisy rather than calm.
- Panels keep the menu's hairline (`widget_panel` already draws it).

This is the part that reaches every screen, and it is the cheapest part.

### 2. The rail — `src/ui/view.c`, `src/ui/results.c`

- Drawing: one row of cards, the focused one 1.5x wide, the rest clipped at the screen edge so it
  is obvious there is more.
- Selection: left and right move along the row; up and down are no-ops inside the results (or
  page the row by a screenful — a decision for the builder, both are defensible).
- The row scrolls so the focused card is fully visible, in the same spirit as the existing
  `results_scroll_into_view`, but horizontal.
- **No touch.** The results live on the top panel, which has no touchscreen, and the app's touch
  branch only handles the keyboard, the playing screen and settings. There is no tap-to-focus or
  tap-to-play rule in the app today (`results_hit()` is called only from `tests/test_ui.c`), so
  the rail is D-pad only and there is nothing to preserve.
- The bottom panel keeps showing the focused video's title, channel, duration and picture.

### 3. The brand mark — new, small

- The asset is `/usr/share/banana/banana/banana-48.png` (48x48 RGBA), installed by the menu's
  `ui/Makefile`. Loaded **once at startup** with libpng, which the image already carries.
- The renderer stores pixels opaquely into RGB565 and `BottomTex` has no alpha channel, so the
  RGBA is **composited onto the header bar's colour while decoding** (the theme's `bar`). The
  theme can change, so the mark is re-baked when it does — one 48x48 decode, not a frame's work.
- The mark gets its **own `BottomTex`, outside the thumbnail cache's `THUMB_MAX_SLOTS` range**,
  because `thumb_slot_for()` / `tex_store()` evict LRU thumbnails and would eventually drop it.
- If the file is missing or fails to decode: the wordmark alone. That needs a loaded flag —
  `dl_image` with a texture that has no pixels fills the placeholder colour, which is not
  "nothing".
- The wordmark is two `dl_text` calls in `FONT_UI_BOLD` (Inter SemiBold): `Banana` in the accent,
  then `Tube` in the ink. The offset needs `font_text_width`, which needs the `FontCtx` — that
  lives in `main.c`, and the draw-list model defers all measurement to render time — so the offset
  is measured once after `font_init()` and handed to the UI, not computed per frame.
- Pixelify (`FONT_MONO`) stays as the clock's face and nothing else.

### 4. The battery — small

- Scan `/sys/class/power_supply` exactly as the menu's `ui/src/library.cpp` does: `capacity` comes
  from the supply whose `type` is `Battery`, and "on the cable" is **any** supply with
  `online == 1`. `status` alone is not enough — the rk817 gauge reports `Discharging` while it is
  charging, which is why the menu does not trust it. Cache the result ~2 s rather than reading it
  per frame.
- Draw the percentage and a small fill bar. While on the cable, the fill takes the accent.

### 5. The lag — `src/display/render.c`

`render_ops()` calls `draw_op()` for every op in the list, with no rejection of ops that fall
outside the rectangle being repainted:

```c
for (int k = 0; k < dl->n; k++) draw_op(&rc, &dl->op[k], fnt, tex, tex_n, cx, cy, cw, ch, scale);
```

Each kind clips internally, but only *after* the work: `font_draw_box` runs `wrap_text` and
measures the whole string through the glyph cache, and `draw_image` computes its source mapping —
for every op, on every frame, whether or not one pixel of it lands in the damaged rectangle. That
is why a selection move that repaints 288 pixels still cost ~30 ms on the device.

The fix is an early-out at the top of the loop:

```c
/* Outside the rectangle being repainted: nothing this op draws can land in it, and the op's box is
 * the same bound the damage diff already trusts as the whole of what the op draws. */
```

The box must be scaled with `render_scale(op->x, scale)` before comparing, because the clip rect
is in device pixels and the op is in screen units.

**Why this is safe:** `dl_diff` → `damage_add(out, op->x, op->y, op->w, op->h)` already uses this
exact unpadded box as the region an op occupies. If an op could draw outside its box, the damage
diff would already leave stale pixels behind on every change, and it does not. Every text op in
the tree is created through `widget_*` with a generous `h` and `VALIGN_MIDDLE`, so text sits well
inside its box.

Verification has to be a **partial repaint**, not the existing bench: `--bench` renders the whole
panel (`render_full`), so the early-out would skip nothing and the numbers would not move. Add a
selection-move case to `cli_bench` that repaints a small rectangle, and measure that on the host
and on the device. The device number is the one that counts.

The other half of a selection move is the bottom panel redrawing the focused video's picture. If
the measurement shows that blit dominates, the plan needs a second change — leave the picture
until the focus settles — and the early-out alone will not reach the goal.

## What's left to the builder

- The exact rail dimensions, and whether the row shows three cards or three and a peek.
- The header's exact heights and the icon size, from the mockups rather than from these numbers.
- Whether up/down page the rail or do nothing.
- Whether the keyboard, settings and history screens need structural changes or only the shared
  chrome pass — the mockups only covered the results screen.
- The light theme values for the new pieces (the mockups were dark only).
