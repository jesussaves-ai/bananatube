# Architecture

Why BananaTube is built the way it is, and what each part is allowed to cost. For the original proposal this was
developed from, see the plan alongside it; this file is the design as it stands.

## 1. Where it sits in BananaOS

`banana-tube` is a standalone binary that the menu starts the way it starts a game (`ui/src/main.cpp`'s launch,
the same path `run-bananads` takes):

1. banana-ui closes the DRM device, the input devices and the sound device, sets the system's clock profile
   (`banana-perf`), then `fork`s and `exec`s `/usr/libexec/banana/run-bananatube` in its own session (`setsid`).
2. The parent writes the child's pid to `/run/banana/game.pid` and waits (`waitpid`).
3. banana-tube opens `/dev/dri/card0` and takes the DRM master, which was left free.
4. On exit it closes the device and drops the master; banana-ui's `waitpid` returns, it reopens the panels and the
   input and sound devices, records the play, and the menu comes back.

bananad does not start it: it reads `/run/banana/game.pid` to `SIGSTOP`/`SIGCONT` the process group around a
sleep and `SIGTERM` it on a shutdown, as it does for any game (`system/bananad/bananad.c`).

The contract with the launcher is the one BananaDS gets, so `docs/EMULATORS.md`'s names are reused:
`BANANA_TOP` (the top panel's connector, `DSI-2` on both handhelds) and `BANANA_TOUCH` (part of the bottom
touchscreen's sysfs path, used to tell two touch-capable devices apart).

Sleep is bananad's, unchanged: it `SIGSTOP`s the process group before the clocks drop and `SIGCONT`s it on wake,
and banana-tube redraws on `SIGUSR1`. Lid-close audio-only (Phase 4) is a flag the app keeps for itself: on
`SW_LID` it drops the video track and keeps the sound, and restores the picture when the lid opens.

## 2. Display

BananaOS has no compositor, no Wayland and no windowing library. banana-ui opens both DRM/KMS connectors itself
(`ui/src/kms.cpp`), and BananaDS drives the panels through `libdrm` with Panfrost for the GPU. BananaTube takes
the same path, with one panel per technique:

| | Top panel | Bottom panel |
|---|---|---|
| Carries | **the content**: the search field, the results grid, the video | **the controls**: the keyboard, the transport row, what is selected |
| Drawn by | the CPU (dumb buffers) while browsing, the GPU through `libmpv` while playing | the CPU (dumb buffers) |
| Surface | `display/bottom.c`'s canvas, or a `GBM` surface + `EGL` context (`display/gbm_egl.c`) | DRM dumb buffers (`display/bottom.c`) |
| Why | it is the main screen — the one the menu is on — so what you are looking at belongs there | it is the screen with the touchscreen, so every control belongs there |

That split came from the handheld: the first version put the whole client on the bottom panel, which reads as the
app running on the wrong screen, and left the top panel showing a placeholder so dark it looked switched off.

The top panel therefore has two ways of being driven, and they never run at the same time: a dumb buffer made by
`bottom_present()` while the app is being browsed, and mpv's frames through EGL once a video starts. Switching
between them invalidates the damage diff (`bottom_invalidate()`), or the panel keeps the last video frame under
the results — the diff would compare the grid against mpv's frame and conclude that nothing had changed.

The bottom panel is the arrangement banana-ui uses, which is what keeps an idle frame cheap:

- the frame is a **flat list of draws** (`display/draw.h`) — rectangles, text, pictures, each with the clip in
  force when it was pushed;
- the next frame is **diffed against the last one** (`dl_diff`): a draw that came, went, moved or changed
  contributes its old and new boxes to a small damage list, and the two lists are walked with a bounded
  look-ahead so an insertion does not repaint everything after it;
- only the damaged rectangles are repainted (every draw that touches them, clipped to them), composed in
  **cached memory**;
- only those rectangles are `memcpy`d into the **uncached scanout buffer**, and the page flips on vblank.

At most eight separate rectangles are tracked; past that the list collapses to one bounding rectangle, because a
1.2 MB repaint costs less than the bookkeeping.

**The panel sizes.** A theme is 640x480 units whatever the panel is: the RG DS Plus's 1024x768 sheets are that
grid scaled by 1.6, and the software renderer scales its geometry and its text the same way. The mode is not
hard-coded: `drm_find_panels()` reads it from the connector (the kernel command line's `video=DSI-2:...` first,
then the panel's preferred mode), so `banana-panel-tune`'s rewritten device tree is what is used.

**Never wait for a vblank when presenting.** banana-ui's `present()` queues a page flip and moves on, skips a
frame while a flip is in flight, and its fallback (a lost master, a refused flip) is a direct `drmModeSetCrtc`
with *no* wait. The first version of `bottom_present()` did all three wrong: it waited on `drmWaitVBlank` after
the fallback (a blocking call until the CRTC scans out, which is where a 450 ms "laggy" frame went), and it
forgot to raise the pending counter on a successful flip, so two flips could queue and the second cancelled the
first. The pace is the pending counter, not the kernel's vblank event.

**Two DRM devices, not one** — which the first run on the handheld made clear. `card0` is the display
controller (the `rockchip` driver, which owns the connectors); the Mali GPU is a separate card (`panfrost`, with
no display at all). Mesa bridges them with **kmsro**: a render-only GPU driver paired with a display-only KMS
device, so a GBM device made from `card0` hands back buffers the display can scan out. `display/gbm_egl.c`
therefore tries `card0` first, and the GPU's own `/dev/dri/renderD*` only if the driver refuses, in which case
the buffers reach the display through a dma-buf (`add_fb()` does it the dma-buf way for both, because that is
correct for both).

And the GBM surface has to be created in the format the **chosen EGL config** asks for
(`EGL_NATIVE_VISUAL_ID`), not in a format picked before looking: a surface in XRGB8888 against a config whose
visual is ARGB8888 is an `EGL_BAD_MATCH` out of `eglCreateWindowSurface`, which reads in the log as "no EGL on
the top panel". The first version picked XRGB8888 up front and did exactly that on the device. The init path
now also logs every config the driver offers and `eglGetError()` at each step, because the failure is a
driver's opinion and the log is the only place to read it on a handheld.

## 3. Input

Read straight from `/dev/input/event*` with `poll()` — `evdev` lets several readers share a device, which is how
bananad, banana-ui and this all see the buttons at once.

The codes are the RG DS's (`buildroot/board/rgds/dts/rk3568-anbernic-rg-ds.dts`), and the mapping is banana-ui's
(`ui/src/input.cpp`) so a button means what a BananaOS user expects:

| Button | Code | | Button | Code |
|---|---|---|---|---|
| D-pad | `BTN_DPAD_UP/DOWN/LEFT/RIGHT` | | A | `BTN_EAST` |
| B | `BTN_SOUTH` | | X | `BTN_NORTH` |
| Y | `BTN_WEST` | | L1/R1 | `BTN_TL`/`BTN_TR` |
| L2/R2 | `BTN_TL2`/`BTN_TR2` | | START/SELECT | `BTN_START`/`BTN_SELECT` |
| HOME | `BTN_MODE` | | Lid | `SW_LID` |

A is `BTN_EAST` and B is `BTN_SOUTH` — the Nintendo layout, and the one thing that differs from a PC pad. A USB
pad's hat (`ABS_HAT0X/Y`) is reported as a D-pad press. L2/R2 are their own keys here, where banana-ui folds them
into L/R: the player seeks with them.

The touchscreen is found by capability (multitouch, or a single-touch with `BTN_TOUCH`, or an absolute pointer),
and `BANANA_TOUCH` picks the bottom one when two devices qualify. Its axis ranges come from `EVIOCGABS` and are
mapped to screen units once, so a tap is in the same coordinates as the draws.

## 4. The backends

Two sources behind one interface (`net/backend.h`), chosen by `bananatube.conf` (`backend = auto` tries them in
order):

**Tier 1 — search, metadata, thumbnails: one HTTP request, no process.**

- **Invidious/Piped** (`net/invidious.c`): `GET /api/v1/search?q=…&type=video` and `GET /api/v1/videos/<id>`. One
  GET, plain URLs, no cipher, no session. A public instance that is blocked or rate-limited is the risk, which is
  what the order is for.
- **InnerTube** (`net/innertube.c`): `POST /youtubei/v1/search` with the Android VR client's context. YouTube
  moves its renderers between builds, so the reply is *walked* for every `videoRenderer` rather than addressed by
  path.

Neither spawns anything. A search is ~1.5 MB of peak allocation and 200–400 ms on the handheld's Wi-Fi.

**Tier 2 — a stream URL, once, when a video is picked.**

1. The configured instance's `adaptiveFormats`/`formatStreams` (or Piped's `videoStreams`/`audioStreams`), or
2. InnerTube's `streamingData`, or
3. **a single `yt-dlp -g` run** — only here, only once per video, with its output captured and the process gone
   (and its ~70 MB freed) before libmpv is asked to open the stream.

A reply whose only URLs come back as a `signatureCipher` is reported as `RESOLVE_NEEDS_CIPHER` rather than solved:
shipping a JavaScript interpreter and chasing YouTube's weekly `n`-param rotation is not worth it on this device.

Authentication, when it is added, is a `cookies.txt` in `banana/` handed to curl for the requests that need it —
never parsed into memory and never logged.

### The format rules (`select_video_format`)

The RK3568's video unit decodes H.264, H.265, VP8 and VP9. **It has no AV1 decoder**, and software AV1 at 480p
uses every A55 core, so `av01` is refused outright — even when it is the only format at or under the cap, in which
case the answer is "no suitable format" rather than a video that stutters.

In order of preference:

1. H.264 (`avc1`/`avc3`) — the VPU's cheapest path and the widest support;
2. VP9 (`vp09`/`vp9`), then H.265, then VP8, then an absent codec string (ranked last, allowed);
3. within a codec: the **largest height at or under the cap**, then the lowest bitrate;
4. never above `conf.quality` (480p by default — the panel's own height) and never above 60 fps;
5. audio is the highest-bitrate audio-only stream, so a DASH video and its audio are two URLs libmpv plays
   together; a muxed `formats`/`formatStreams` entry needs no second stream.

## 5. The bottom screen's UI (Phase 2)

Four views, one of which is drawn at a time: the **results grid** (the search field, three columns of tiles,
the button hints), the **keyboard** over the grid, **playback**, the **watched list**, and the app's own
**settings** screen. `ui/view.c` owns the state and answers one question per button — "type this", "search",
"play", "quit" — which is what keeps `main.c` free of screen logic and the whole thing testable without a
display.

The drawing is the family of helpers in `ui/widget.c` (a card, a well, a hint chip, a bar, a spinner) plus the
grid in `ui/results.c` and the keyboard in `input/keyboard.c`, all pushing DrawOps. **A tile's picture is not a
texture upload**: the bottom panel is drawn by the CPU, so a decoded thumbnail is an RGB565 buffer in ordinary
memory and the renderer scales it into the frame. That removes a whole GPU path the plan had assumed, and it is
why the cache's budget is 12 pictures × ~90 KB rather than VRAM.

The pictures themselves come from `net/thumbworker.c`: two niced threads fetching with libcurl and decoding
with libjpeg (scaled 1/2, 1/4 or 1/8 during the decode), and a queue the main loop drains. A video is asked for
once — the grid asks every frame and the worker remembers what is queued or in flight — and a picture that
fails frees its slot to be tried again. What draws the grid decides what to fetch, so scrolling fills in behind
itself.

The renderer (`display/render.c`) is deliberately **not** in the DRM half: it draws into any ARGB32 buffer, so
the tests render at both panel sizes, and `bananatube --shot` renders the client to a picture on a machine with
no panels at all (which is how the screenshots in the README are made, and what a CI run can diff).

**The settings screen** (`ui/settings.c`) is the file's own rows, in the app: backend, quality, lid audio, the
watched list's size, the picture cache, the search timeout, the decoder and the instance URL. Changing one calls
`conf_set()` on the app's `Conf` and reports `UI_SETTINGS_SAVE`; `main.c` writes `bananatube.conf` and applies
what takes effect at once (the history's cap, the picture cache), and the rest is read when the next search or
video starts, so nothing has to be torn down mid-play. The instance URL is the one typed row, so it opens the
same keyboard the query uses (`kb_draw_as`, with "save" on its key instead of "search"). The list is on the
bottom panel — the one with the touchscreen — and what the selected row does on the top one, the way BananaOS's
own Settings is laid out; a tap selects a row and a tap on the selected row changes it. **START** on the results
opens it, which is the key the menu uses for its own Settings.

## 6. Playback (Phase 3)

libmpv renders into the EGL surface `display/gbm_egl.c` creates, through `mpv_render_context` — the same surface
the placeholder used to paint. `player/player.c` owns the handle and the options; `player/osd.c` is the overlay.

**The main loop stays in charge.** mpv's update callback writes a byte to a self-pipe whose read end is in the
same `poll()` as the panels, the buttons and the workers, so a frame is drawn when mpv has one and the bottom
screen keeps responding in between. Nothing about the video is allowed to block the UI thread.

The options that matter on a 1 GB device, every one of them set before `mpv_initialize` and checked back in
`tests/test_player.c`:

| | |
|---|---|
| `hwdec` | the RK3568's video unit. `v4l2m2m-copy` is the kernel's stateless decoder and needs no Rockchip userspace — and it is the one that works here, because BananaOS's ffmpeg is built with h264/hevc/vp9 v4l2m2m and no Rockchip MPP (so `rkmpp-copy` has no decoder to bind to). `no` is software. `bananatube.conf` chooses, and the launcher writes the handheld's value |
| `vd-lavc-dr=yes` | decoded frames go straight to the GPU: no CPU copy of every frame |
| `demuxer-max-bytes=16MiB` | mpv's default is 150 MB, which on this device is an OOM rather than a stutter |
| `demuxer-max-back-bytes=4MiB` | the rewind buffer |
| `cache=no`, `cache-on-disk=no` | stream it; nothing is written to the card |
| `ytdl=no` | mpv must never spawn its own yt-dlp: the URL was resolved in `net/resolver.c` |
| `config=no`, `load-scripts=no` | nothing on the card may quietly undo any of the above |
| `vo=libmpv`, `osc=no`, `input-default-bindings=no` | we own the surface, the input and the overlay |
| `video-sync=display-resample`, `video-output-levels=limited` | the panels are 60 Hz and limited-range |
| `keep-open=no`, `save-position-on-quit=no` | a session is not state; Phase 4 owns history |

**The overlay** is a DrawList like the bottom screen's, rendered into an ARGB buffer by the same
`display/render.c`, uploaded once as a texture (`display/gles_blit.c`) and drawn over the video. It shows the
resolution being decoded, the position and duration, a spinner while mpv is reading ahead, and the frames it
had to drop — and it is only re-uploaded when what it says changes, so an idle overlay costs nothing.

**Two things only the device can settle**, both cheap to change:

1. **Whether the kernel's decoder holds up.** `v4l2m2m-copy` against software (`hwdec = no`) at 480p60 and
   720p60, with `player_dropped()` as the measure. The overlay shows the count while it happens. The plan had
   `rkmpp-copy` as the alternative, and the built image settled that before any hardware did: BananaOS's ffmpeg
   is configured with the kernel's h264/hevc/vp9 v4l2m2m decoders and with no Rockchip MPP, so `rkmpp-copy` has
   no decoder to bind to. Turning it on would mean packaging the MPP library and adding `--enable-rkmpp` to
   ffmpeg — worth doing only if the kernel's decoder is shown to fall short.
2. **The overlay's orientation.** mpv is told `MPV_RENDER_PARAM_FLIP_Y=1` and the blitter flips its own
   coordinates in the vertex shader to match; if the bar comes out upside-down on the panel, one of the two is
   the wrong way round.

## 7. The player, history and the lid (Phase 4)

**The transport row** is seven buttons across the bottom screen (previous, volume down, pause, volume up, the
quality, next, stop). `ui_transport_box()` is the one place its geometry lives, so a tap and the drawing cannot
disagree — they did once, for the result tiles, and `tests/test_ui.c` now checks both.

**Seeking** is relative (`LEFT`/`RIGHT` or `L2`/`R2`, ±10 s), **volume** is `UP`/`DOWN` (mpv's own volume,
which bananad's mixer then rides), **previous/next** walk the result list and start that video, and **X** cycles
the quality: 360p → 480p → 720p → 360p. Changing quality means re-resolving at the new size and reloading at the
same position, which is the only honest way to do it mid-stream; it is also written to `bananatube.conf`, so the
choice is what the next video gets.

**The watched list** is `banana/bananatube-history.json`: most recent first, each with where it got to, plus the
last search so a restart is not a blank screen. How many are kept is `history_size` in `bananatube.conf` (50 to
begin with), clamped to `HISTORY_MAX` because the file is an array — a value the file cannot hold is clamped
rather than obeyed, and the entries that no longer fit fall off the end. JSON rather than SQLite because the data is
a few kilobytes, cJSON is already here, and a file that can be read on a computer is worth more on a handheld
than a query language. It is written on a pause, a stop, a switch and an exit, and every 30 s while playing, so
pulling the battery loses at most half a minute. `Y` on the results screen opens it; `A` carries on from
where it stopped.

**The lid.** banana-tube reads `SW_LID` itself (evdev). When the lid shuts and `audio_only_on_lid` is on, mpv is
told `vid=no`: the video unit stops working, the sound carries on, and the top panel is left alone (bananad has
already turned the backlights off). Opening it puts the picture back.

That interacts with a setting on the other side of the system, and the two must agree: **Settings ▸ Sleep &
power ▸ Closing the lid**. Set to *Sleep* (the default), bananad suspends the device when the lid shuts — and a
suspended SoC plays nothing, so the audio stops there and the sound comes back from where it left off on waking.
Set to *Nothing*, the device stays awake and this is what happens instead. Both are reasonable; they are just
different, and the plan's "audio with the lid shut" only means anything with the second.

## 8. Memory

| | |
|---|---|
| libmpv + ffmpeg (no cache) | 35–45 MB |
| Decoded frames (hardware DMABUFs) | 8–12 MB |
| Result pictures, 12 slots at up to 320x180 RGB565 (plus the ones being fetched) | ~1.5 MB — ordinary memory, not VRAM |
| The overlay's texture, and the OSD buffer | ~0.3 MB |
| EGL/GBM surfaces, top panel (2x 1024x768) | 6–8 MB |
| Bottom panel dumb buffers (2x 640x480) | 3 MB |
| Draw lists, glyph cache, curl buffers | 3–4 MB |
| **Peak during 720p playback** | **~75–90 MB** |
| **Idle, browsing** | **~30–35 MB** |

The 1 GB machine runs the kernel and the caches in ~95 MB, and bananad reaped banana-ui before starting this, so
there is no overlap with the menu's 23.5 MB. The `yt-dlp` fallback adds ~70 MB for the one second it runs, while
the process is still at ~30 MB, and is gone before libmpv allocates.

The thumbnail cache is a fixed number of slots (`cache_size`, 12 by default) with an LRU: a grid keeps about a
screenful resident, and the least recently drawn slot is the one reused. Pictures are decoded by libjpeg with
`scale_denom` set to 1/2, 1/4 or 1/8, so the scaling happens during the decode instead of on the CPU afterwards.

## 9. Phases

| | |
|---|---|
| **1. Both panels** | DRM, GBM/EGL, dumb buffers, the damage-diffing renderer, evdev, FreeType, the handover. **Done** — see the README's status table. |
| **2. The bottom screen** | **Done.** The search field, the keyboard, the results grid, and the background thumbnail fetcher and decoder (`net/thumbworker.c`), blitted by the CPU renderer. |
| **3. Video on the top screen** | **Done.** `player/player.c` and `player/osd.c` render through `mpv_render_context` into the existing EGL surface, with the caps in force; the bottom screen becomes the player's (pause, seek, stop). The decoder measurement is the one thing left for the device. |
| **4. Transport and persistence** | **Done.** The transport row, the quality switch, the watched list (JSON, not SQLite: a few kilobytes do not need a database), and audio-only with the lid shut. |
| **The app's settings** | **Done.** `ui/settings.c`: the rows `bananatube.conf` holds, changed in the app (START on the results) and saved as they change, so the card and the menu's own Settings are never needed to configure it. |

Phase 1 was first because it is the risky one: it touches the same display path banana-ui and BananaDS use, and it
has to coexist with bananad's handover. With both panels lighting up from one process, everything after it is an
addition to a working skeleton.
