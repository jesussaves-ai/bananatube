# Testing on the handheld

What to look for on a real RG DS and RG DS Plus, in the order it makes sense to do it. Everything here is
something the host tests cannot check: the panels, the decoder, the touch mapping, the handover, battery.

## Before you start — the search needs Wi-Fi

**Join a network first** (Settings ▸ Network). Without one, every search fails: the first run on the handheld
had no `banana/wifi.conf` at all, so both backends failed and the log said `invidious: ... Error` and
`innertube: ... Error`. The app now says "Could not reach YouTube. Is Wi-Fi on?" and logs which instance or
endpoint it could not reach.

- **A card with the build.** Settings ▸ Display & sound ▸ Theme is not involved; this is an app.
- **SSH on** (Settings ▸ Network ▸ SSH, keys from `banana/ssh/authorized_keys`) so you can read the log and the
  memory numbers without taking the card out.
- **`banana/logs/bananatube.log`** is where everything the client says goes (the launcher writes it; the run
  before it is kept as `bananatube.1.log`). `tail -f` it over SSH while you poke at the thing.
- **`sleep_debug = yes`** in `banana/bananad.conf` before the sleep tests: it adds the kernel's per-device resume
  times to `banana/logs/sleep-devices.log`, slowest first.

### Getting it launched

The client is started the way a game is — the menu lets go of the panels and runs the launcher. So the menu needs
a tile for it:

1. The Buildroot package (`buildroot/package/bananatube/`) installs the binary, `run-bananatube` and the app
   folder.
2. Add the entry from `system/systems.conf.snippet` to `/etc/banana/systems.conf`, then restart the menu.
   **If the card has `/data/banana/systems.conf`, the menu reads that instead** (`banana-ui-session` sets
   `BANANA_SYSTEMS` to it) — put the section in the card's file in that case.
3. Add the line from `system/systems.cfg.snippet` to the theme's `rnds/systems.cfg` (the theme in the image is
   `/usr/share/banana/rnds/systems.cfg`). This is what gives YouTube a tile of its own, where **A starts it**;
   without it the tile falls back to the `*` icon and opens as a folder holding one game.

Then **YouTube** appears on the shelf and A hands the panels over. If a theme of your own is selected, its
`systems.cfg` needs the same line — the `[app]` marker lives in the theme, not in the menu's list.

## 1. It starts, and comes back

| | |
|---|---|
| Both panels light up? | The top one is the video's; the bottom one is the client. Neither should show the menu behind it. |
| Any error on the way in? | `grep -E 'panels\|EGL\|player\|egl' banana/logs/bananatube.log` — "panels: top DSI-2 … bottom DSI-…" then "egl: …" then "player: libmpv …". |
| Does the menu come back? | **HOME** (or F1 over a serial console) quits. The panels should go back to the menu within a second or two, with nothing left of the client on screen. |

If the menu does not come back, `pidof banana-ui` on SSH says whether it is there; init respawns it, so a black
panel with a live `banana-ui` is a DRM handover problem worth a log.

## 2. The two screens

**The top panel carries the content and the bottom one the controls**: the search field, the results grid and the
video are on the top screen; the keyboard, the transport row and what is selected are on the bottom one, which is
the screen with the touchscreen.

| | |
|---|---|
| The layout | Top: a header, the search field, the results grid. Bottom: the keyboard while typing, the transport row while playing, the selected video's details otherwise, and the button hints along its bottom. Nothing overlapping, nothing clipped at the edges. |
| The **Plus**'s 1.6 scaling | The same layout, larger, with no soft edges and no draw in the wrong place: this is where a forgotten `scale` shows up (it did, once). |
| **Touch** | Tap a keyboard key: the letter should appear, and tapping **search** should run the search (it used to do nothing: the tap path threw away the action the key raised). While a video plays, the transport row should respond to a tap. The top panel has no touchscreen, so the grid is D-pad only. If taps land somewhere other than your finger, note which way they are off. |
| The keyboard | D-pad to move, A to type, B to delete, X capitals, Y space, START to search, SELECT to put it away. Touch should work for all of it. |

## 3. Searching

| | |
|---|---|
| The first search | SELECT ▸ type ▸ START. **Under 400 ms** on the handheld's Wi-Fi is the target; the log line is `invidious: …` or `innertube: …`. |
| Which backend answered | The log says. `backend = auto` tries your Invidious instance first and falls back to InnerTube. |
| An instance that is down | Set `instance = https://10.255.255.1` in `banana/bananatube.conf`: the first search should still work (via InnerTube), just slower. |
| Wi-Fi off | Settings ▸ Network ▸ Wi-Fi off, then search: it should say so rather than hang. The message is on the results screen. |
| Many results | A search with 30+ results should scroll smoothly; only the tiles on screen should have pictures at first. |
| Nothing found | A nonsense query should say "Nothing found", not sit on a spinner. |

## 4. Thumbnails

| | |
|---|---|
| They arrive | "loading…" tiles should fill in over a second or two, in the order they came back. |
| Scrolling | Scroll fast through 30 results and back: no stutter, no tile stuck on "loading…", no picture on the wrong tile. |
| The cache | Scroll to the bottom and back to the top: the first tiles should already have their pictures (12 are kept). |
| No leak | `grep VmRSS /proc/$(pidof bananatube)/status` before and after a hard scroll: it should come back to within a megabyte or so. |

## 5. Playback — the two things I could not test

| | |
|---|---|
| **The decoder** | `hwdec = v4l2m2m-copy` is what the launcher writes, and the only hardware decoder this image has: BananaOS's ffmpeg is built with the kernel's h264/hevc/vp9 v4l2m2m and with no Rockchip MPP, so **`rkmpp-copy` would fail to start — do not set it**. Play a 480p and a 720p video and watch the overlay's drop count. Non-zero means set `hwdec = no` in `banana/bananatube.conf` and compare (software decode, but no copy-back overhead), and tell me which was better. |
| **The overlay's orientation** | The bar should be at the bottom of the video, the right way up. If it is upside-down or at the top, that is the `FLIP_Y` flag against the blitter's flip — tell me which and it is a one-line fix. |
| Hardware decode at all? | The log has mpv's own line; `dmesg \| grep -i -E 'rkvdec\|v4l2\|mpp'` should show the decoder being opened. If mpv falls back to software the drop count will be loud about it. |
| The picture | Full screen, letterboxed (16:9 in a 4:3 panel), no tearing, no green or smeared frames. |
| Sound | In sync with the picture. Volume keys still change the mixer (bananad owns those); the on-screen volume is mpv's own on top. |
| The badge | It should read the size the resolver picked (480p by default, from `quality`), and match what mpv is decoding. |
| A video that will not play | An age-restricted or region-locked one: the message should be readable, not a hang. |

## 6. Transport

| | |
|---|---|
| Pause | START, and the `‖ pause` button. The picture freezes; the OSD says "paused". |
| Seek | LEFT/RIGHT and L2/R2 seek 10 s. The overlay's position should jump and the picture follow within a moment. |
| Volume | UP/DOWN, and the two volume buttons. The number on screen moves, the sound changes. |
| Quality | X, and the quality button: 360 → 480 → 720 → 360. The video should restart at the new size **from where it was**, and the badge should change. |
| Prev / next | L1/R1 and the two buttons: the next result plays. |
| Stop | B: back to the results, with the watched list updated. |

## 7. The watched list

| | |
|---|---|
| It fills in | Play something for a few seconds, stop, press Y: the video should be there with roughly the
right position. |
| It carries on | Open it from the list: it should resume where it stopped, not from the beginning. |
| It survives a restart | Exit the client (HOME), open it again: the list is still there, and the last search is
still in the field. |
| The file | `cat banana/bananatube-history.json` — readable, and the positions make sense. |
| The cap | After more than 50 videos the oldest should fall off. |

## 8. The lid, and sleep

**These two settings have to agree, and this is where to find out.**

| | |
|---|---|
| Settings ▸ Sleep & power ▸ **Closing the lid = Nothing** | Close the lid while a video plays: the sound should keep going, the panels go dark (bananad's backlights), and the overlay's badge should say "audio only". Open it: the picture comes back and carries on from the same place. |
| Settings ▸ Sleep & power ▸ **Closing the lid = Sleeps** (the default) | Close the lid: bananad suspends the device, so the sound stops. That is expected, not a bug — the plan's audio-only mode only means anything with the other setting. Worth confirming the sound resumes from where it was when you open it. |
| A short power press | Sleeps. With a video playing, wake it: the picture should come back within a fraction of a second (bananad's `SIGCONT` plus a redraw), and `sleep.log` should have the line. |
| Leave it playing and let it sleep | After a wake, the video should still be playing (bananad `SIGSTOP`s the process group rather than killing it). |

## 9. What it costs

| | |
|---|---|
| Memory, browsing | `grep VmRSS /proc/$(pidof bananatube)/status` — **~35 MB** is the target. |
| Memory, 720p playing | **Under 95 MB** is the target; the whole system under 180 MB. |
| The demuxer cap took | It should plateau rather than creep: watch VmRSS over ten minutes of playback. This is what `tests/test_player.c` checks in the options, and this is it in practice. |
| Idle CPU | Between frames and with nothing playing, `top` should show it nearly idle. While playing, the A55s should have room to spare (the VPU is doing the work). |
| Battery | A night of standby, and an hour of video, against what ROCKNIX gives. Worth writing down. |

## If something is wrong

Send me:

- `banana/logs/bananatube.log` (and `bananatube.1.log`, the run before it)
- `banana/bananatube.conf`
- `banana/logs/sleep.log` for anything about the lid
- the overlay's drop count if it is a stutter
- which of the two panels, and which panel size, if it is a drawing problem — the RG DS and the Plus scale
  differently, and that is where scaling bugs hide

## Testing without the handheld

Most bugs never needed the card. The client has three modes that run the *real* code with no DRM, no EGL, no
decoder and no input devices — the same `AppState`, the same `ui_key`, the same draw lists, the same backends
over the real network — and write pictures you can look at. What they cannot test is the panels, the GPU and
the video unit.

```sh
# how long a frame takes to paint and what it spends it on, by kind of draw
bananatube --bench results 50 [plus] [--query "banana theme"]

# a real search: a table of results, and both panels drawn as the handheld would have them
bananatube --search "banana theme" --out /tmp/bt --frames

# one video's stream URL, resolved exactly as pressing A does — the half of playing that has no UI
bananatube --resolve GbOFoVTa93E

# drive the UI with a script of key names, one frame per step
bananatube --keys "select,down,A,start,right,A" --query "banana theme" --out /tmp/bt
```

`--keys` prints what each key did, what the view, the selection and the history became, and — for the keys that
need the network — what the search returned and what pressing A resolved to:

```
   3. right    -> thumbs    view results, 20 results, sel 2, history 0
   4. A        -> play      view results, 20 results, sel 2, history 0
        "Foreboding Fantasy - Top Banana Chase Theme" -> ready (640x360 avc1.4D401E)
```

Key names are the ones the log prints: `up down left right A B X Y L1 R1 L2 R2 start select home`.

Both `--search --frames` and `--keys` write PPMs — `--show` them, or `magick x.ppm x.png`. A frame is both
panels stacked, the way the two screens sit. Add `--plus` for the RG DS Plus's 1024x768.

Two things are worth knowing about it:

- **It cannot show the video.** The top panel is drawn by libmpv through EGL, and there is no GPU in these
  modes; `--resolve` is what tells you the stream side is right.
- **The real client runs without EGL now.** If `gbm_egl_init()` fails — QEMU's virtio device, a machine whose
  Mesa is built for another SoC — the app logs it, browses normally on the CPU, and only the video is lost. It
  used to exit, which is what made it untestable anywhere but the handheld.

## What the third run found (all four of these were real)

| | |
|---|---|
| **Videos resolved and then 403'd** | `ffmpeg: HTTP error 403 Forbidden` on the googlevideo URL. `URL_LEN` was 1024, so the buffers held 1023 characters, and every stream URL for this client is **1102–1130**: the signature was cut off. 2048 now, and both parsers log it if a URL ever again does not fit. Checked from the desktop: the same URL fetches **206**. |
| **The keyboard typed several letters and would not delete** | `TouchEvent.down` was sticky: set on a touch and never cleared, so **every later event — including every button press — re-ran the keyboard tap at the last finger position**. B added a letter and then deleted it, which reads as delete doing nothing. It is an edge now (set per read, cleared at the start of every dispatch, cleared by a lift). |
| **Typing repeated itself** | the kernel's autorepeat arrives as `value == 2` and the app treated it as a fresh press. Only the D-pad repeats now, which is what banana-ui does. |
| **The UI took 185–500 ms a frame, and 1.5 s on a transition** | a full `drmModeSetCrtc` on **every frame**, on both panels. banana-ui uses **`drmModePageFlip`** after the first frame; the same now, with the double-buffer "missed damage" bookkeeping a flip needs (the hidden buffer is missing the frames it sat out) and `drm_handle_events()` in the poll loop, without which the flip counter never comes down. Falls back to a modeset when a flip cannot be queued, so the worst case is the old behaviour. |

The last one is the only fix here that cannot be checked on a desktop — the display belongs to the desktop's
compositor — so it wants a device test. Everything else was verified from here: the stream URL fetches 206, the
keyboard and repeat logic is unit-tested, and the render cost is measured with `--bench`.

## What the first run on the hardware found

Recorded because none of it is visible from a desktop, and each one cost a build:

1. **EGL never came up.** `gbm_egl_init` created the GBM surface in `XRGB8888` *before* choosing an EGL config,
   so when the config it then picked had a different `EGL_NATIVE_VISUAL_ID` the two disagreed and
   `eglCreateWindowSurface` returned `EGL_BAD_MATCH` — which reads as "no EGL on the top panel". The RK3568
   also has **two DRM devices** (the `rockchip` display controller and a separate `panfrost` GPU card), which
   Mesa bridges with kmsro. See `docs/ARCHITECTURE.md` §2.
2. **SELECT did not search.** It opened the watched list, and with nothing watched yet it did *nothing at all*,
   while every word of the app's own interface and the README said SELECT searches. The keyboard is on SELECT
   again; the watched list moved to Y.
3. **B quit the app.** From the results screen with an empty list — the state the app starts in, and the state
   after a search that found nothing — `B` returned `UI_QUIT`. A back/cancel press there ended the program,
   which on a handheld is indistinguishable from a crash, and is how it was first reported. B now only ever
   clears; HOME is the only key that leaves. `tests/test_ui.c` had been asserting the old behaviour.
4. **The idle top panel looked dead.** With nothing playing it was a placeholder whose brightness was about
   11/255 — a working panel that reads as switched off — so the app looked like it was only on the bottom
   screen. It is gone: the top panel now carries the app.
5. **A frame's draw list was never cleared.** `dl_reset()` ran once outside the main loop, so every frame
   appended another copy of the whole UI: the list grew by a screenful each frame until it hit `DRAW_MAX`, at
   which point the draws that no longer fitted were dropped and the screen stopped updating. That was the
   250 ms-a-frame full-screen repaint *and* the "freezes after one letter": by then the keyboard's draws were
   past the end of the list. The instrumentation added for exactly this (`frame: ... draws, ... damage rects,
   ... px repainted`) showed it in one run. `ui_draw_top`/`ui_draw_bot` now clear the list they are given, and
   `tests/test_ui.c` draws twice and requires the same count.
6. **InnerTube answered with nothing.** A search came back HTTP 200 with nineteen videos in it and the app read
   zero, because the Android VR client returns `compactVideoRenderer` and the parser only looked for
   `videoRenderer`. The fixture it was tested against had been hand-written with the name the parser wanted, so
   nothing caught it; there is now a fixture captured from the real API.
7. **Only the first Invidious instance was ever tried**, and the list was mostly dead (checked against the live
   API: one answers, one rate-limits, one is behind a wall, one is down). All of them are tried in order now,
   with the working one first and the others as fallbacks.
8. **YouTube bot-checks the player endpoint.** A search resolved (the `compactVideoRenderer` fix below), the
   video did not: `innertube: Sign in to confirm you're not a bot`, then the yt-dlp last resort (which is not in
   the image), then "Could not play this video". Measured against the live API, **1 of 8** videos resolved.
   Every InnerTube reply carries a `visitorData`, the refusal carries a usable one, and sending it back is the
   whole difference: **8 of 8** resolved with it. `player_request()` in `net/innertube.c` keeps it, sends it,
   and asks once more with the value from a refusal. `--resolve` is how it was found and how it is checked.
9. **The layout.** The results were on the bottom panel, which read as the app being on the wrong screen — the
   content goes on the top panel now and the controls on the bottom one, which is also where the touchscreen is.
   The transport row had been drawn but was never actually tappable (`ui_transport_hit` was only used by its own
   test), and the keyboard was bottom-aligned inside the whole screen whatever box it was given, which left most
   of the bottom panel empty.

## Known unknowns, in order of how likely they are to need a change

1. **The overlay's orientation** (`FLIP_Y` against the blitter's own flip). One line.
2. **The decoder at 720p60**: whether the kernel's `v4l2m2m-copy` holds, or software is better for some
   codec. A setting already (`rkmpp-copy` is not: no MPP in the image — see `docs/ARCHITECTURE.md` §6).
3. **The lid against bananad's sleep**: whether "audio only with the lid shut" is what people want, or whether
   the two settings should be tied together.
4. **The quality switch re-resolving**: it restarts the stream at the same position, which is honest but not
   seamless. If it is jarring, the alternative is to let mpv pick a different track without reopening — more
   work, and it does not work for a DASH video whose audio is a separate stream.
