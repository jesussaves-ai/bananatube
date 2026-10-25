# BananaTube

A native dual-screen YouTube client for [BananaOS](https://github.com/JorreFog/BananaLinux) on the Anbernic RG DS
and RG DS Plus (Rockchip RK3568, 1 GB of RAM, two 640x480 panels).

The video goes on the top panel, hardware-decoded. The search, the results and the transport controls are on the
bottom one, drawn by the CPU straight into DRM buffers. There is **no browser, no WebView, no Electron, no Qt, no
SDL** and — the point of the whole thing — **no `yt-dlp` on the search path**: searching is one HTTP request
parsed with cJSON, because spawning a Python interpreter on a Cortex-A55 costs ~70 MB and 3–5 seconds.

## Status

Phase 1 (this repository's first commit) — the skeleton, built and tested:

| | |
|---|---|
| ✅ | Both panels from one process: `GBM`/`EGL` on the top, DRM dumb buffers on the bottom |
| ✅ | DRM master taken from bananad and dropped on exit, so the menu comes back |
| ✅ | Buttons, touchscreen and lid read from `/dev/input` (`BTN_EAST` is A, `BTN_SOUTH` is B: the RG DS's layout) |
| ✅ | The software screen: draw lists, damage diffing, `memcpy` blits, rounded boxes, text through FreeType |
| ✅ | The backend core: Invidious/Piped and InnerTube, the stream-format rules, the thumbnail cache, settings |
| ✅ | `buildroot/package/bananatube` and the launcher `system/run-bananatube` |
| ⏳ | Phase 2: the search screen, the virtual keyboard, the results grid, thumbnail upload |
| ⏳ | Phase 3: libmpv rendering into the EGL surface, `rkmpp`/`v4l2-request` hwdecode, the OSD |
| ⏳ | Phase 4: transport and scrubbing, history, lid-close audio-only |

Phase 1 run on the handheld lights both panels: the top one cycles through a colour, and the bottom one shows the
clock, the buttons as they are pressed and the tap coordinates. That is enough to prove the risky part — one
process driving one panel through the GPU and the other through the CPU, with the display handover working.

## Build

Needs `cmake`, a C11 compiler, and the dev packages for `libdrm`, `egl`, `glesv2`, `gbm`, `freetype2`, `libjpeg`,
`libcurl` and `cJSON` (on Arch: `libdrm mesa freetype2 libjpeg-turbo curl cjson`).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

For the handheld it is built by Buildroot; see `buildroot/package/bananatube/`.

## Test

```sh
sh tests/run.sh          # configures, builds and runs every host test
```

The tests need no DRM device, no GPU and no network. They cover the parts where being wrong is expensive and
invisible: the damage diff (a wrong one smears or freezes the bottom screen), the stream-format rules (choosing
AV1, which the RK3568's video unit cannot decode, pegs all four cores), the two backends' replies, the button
mapping and the settings file.

## Run

On a handheld, bananad starts it through the launcher:

```sh
/usr/libexec/banana/run-bananatube
```

On a computer, the display half needs two DSI connectors, so it will stop at "no DRM device with two connected
panels" — which is itself the useful test that the DRM walk is right:

```sh
BANANA_TOP=DSI-2 ./build/bananatube
```

## How it is put together

```
src/
  main.c                  the loop: poll the display and the input, draw a frame, flip
  util.c conf.c font.c
  display/
    draw.h drawlist.c     a frame as a list of draws, and the damage between two frames
    drm.c                 the two panels, dumb buffers, CRTCs, vblank
    gbm_egl.c             the top panel's GBM surface and EGL context (where libmpv will draw)
    bottom.c              the software renderer for the bottom panel
  input/
    keys.c                evdev codes -> A/B/X/Y/L/R/START/SELECT/HOME
    evdev.c               the gamepad, the touchscreen and the lid
  net/
    backend.h             what the app asks of YouTube; the format rules
    http.c                libcurl, capped replies, cJSON helpers
    invidious.c           an Invidious/Piped instance (search, video, formats)
    innertube.c           YouTube's own /youtubei/v1 API (Android VR client: no cipher)
    resolver.c            id -> playable URL, and the last-resort yt-dlp run
    thumbnail.c           a fixed-size LRU of small RGB565 textures, and a JPEG decoder
  player/                 (Phase 3) libmpv: one surface, the RAM caps, the OSD
  ui/                     (Phase 2) the search screen, the keyboard, the results grid
tests/                    the host tests
buildroot/package/        the Buildroot package
system/run-bananatube     the launcher bananad starts
docs/ARCHITECTURE.md      the design, and why each choice was made
```

## The two decisions worth knowing

**No SDL, no Wayland, no compositor.** BananaOS has none of them: `banana-ui` opens both DRM/KMS connectors
itself, and BananaDS drives the panels through `libdrm` with Panfrost for the GPU. This follows the same path —
EGL on GBM for the panel libmpv draws to, dumb buffers for the panel the CPU draws. A windowing library would
add a dependency and an abstraction the rest of the system does not have, and buy nothing.

**The backends are tried in order, and nothing is spawned to search.** An Invidious/Piped instance first (one GET,
plain URLs, no cipher), YouTube's InnerTube API next (the Android VR client still answers without a
`signatureCipher`), and only if both fail does a single, tightly scoped `yt-dlp -g` run — once per video, at the
moment it is played, with its ~70 MB freed before libmpv opens the stream.

## License

GPL-3.0-or-later — the same as BananaOS. See [LICENSE](LICENSE).
