# Testing on the handheld

What to look for on a real RG DS and RG DS Plus, in the order it makes sense to do it. Everything here is
something the host tests cannot check: the panels, the decoder, the touch mapping, the handover, battery.

## Before you start

- **A card with the build.** Settings ▸ Display & sound ▸ Theme is not involved; this is an app.
- **SSH on** (Settings ▸ Network ▸ SSH, keys from `banana/ssh/authorized_keys`) so you can read the log and the
  memory numbers without taking the card out.
- **`banana/logs/bananatube.log`** is where everything the client says goes (the launcher writes it; the last run
  is kept as `.1`). `tail -f` it over SSH while you poke at the thing.
- **`sleep_debug = yes`** in `banana/bananad.conf` before the sleep tests: it adds the kernel's per-device resume
  times to `banana/logs/sleep-devices.log`, slowest first.

### Getting it launched

The client is started the way a game is — the menu lets go of the panels and runs the launcher. So the menu needs
a tile for it:

1. The Buildroot package (`buildroot/package/bananatube/`) installs the binary, `run-bananatube` and the app
   folder.
2. Add the entry from `system/systems.conf.snippet` to `/etc/banana/systems.conf` (or `banana/systems.conf` on
   the card), then restart the menu. That is the only change outside the package.

Then **YouTube** appears on the shelf; opening it hands the panels over.

## 1. It starts, and comes back

| | |
|---|---|
| Both panels light up? | The top one is the video's; the bottom one is the client. Neither should show the menu behind it. |
| Any error on the way in? | `grep -E 'panels\|EGL\|player\|egl' banana/logs/bananatube.log` — "panels: top DSI-2 … bottom DSI-…" then "egl: …" then "player: libmpv …". |
| Does the menu come back? | **HOME** (or F1 over a serial console) quits. The panels should go back to the menu within a second or two, with nothing left of the client on screen. |

If the menu does not come back, `pidof banana-ui` on SSH says whether it is there; init respawns it, so a black
panel with a live `banana-ui` is a DRM handover problem worth a log.

## 2. The bottom screen

| | |
|---|---|
| The layout | A header, the search field, the results, and the button hints along the bottom. Nothing overlapping, nothing clipped at the edges. |
| The **Plus**'s 1.6 scaling | The same layout, larger, with no soft edges and no draw in the wrong place: this is where a forgotten `scale` shows up (it did, once). |
| **Touch** | Tap a result: it should select, and tapping it again should play it. Tap a keyboard key: the letter should appear. If taps land somewhere other than your finger, that is the touchscreen's axis mapping — note which way it is off. |
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
| **The decoder** | `hwdec = v4l2m2m-copy` is what the launcher writes. Play a 480p and a 720p video and watch the overlay's drop count. **Anything other than 0 means try `rkmpp-copy`** in `banana/bananatable.conf` (or `BANANATUBE_HWDEC=rkmpp-copy`). |
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
| It fills in | Play something for a few seconds, stop, press SELECT: the video should be there with roughly the
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

- `banana/logs/bananatube.log` (and `.1` if it restarted)
- `banana/bananatube.conf`
- `banana/logs/bananad-sleep.log` (or `sleep.log`) for anything about the lid
- the overlay's drop count if it is a stutter
- which of the two panels, and which panel size, if it is a drawing problem — the RG DS and the Plus scale
  differently, and that is where scaling bugs hide

## Known unknowns, in order of how likely they are to need a change

1. **The overlay's orientation** (`FLIP_Y` against the blitter's own flip). One line.
2. **The decoder**: `v4l2m2m-copy` versus `rkmpp-copy`, and whether 720p60 holds. A setting already.
3. **The lid against bananad's sleep**: whether "audio only with the lid shut" is what people want, or whether
   the two settings should be tied together.
4. **The quality switch re-resolving**: it restarts the stream at the same position, which is honest but not
   seamless. If it is jarring, the alternative is to let mpv pick a different track without reopening — more
   work, and it does not work for a DASH video whose audio is a separate stream.
