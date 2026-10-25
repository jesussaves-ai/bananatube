/* The player's configuration, which is the dangerous part of it.
 *
 * mpv's defaults are for a desktop: a 150 MB demuxer cache, a yt-dlp of its own, a config file that can
 * override everything. On a 1 GB handheld those are an OOM and a 5-second delay. This checks the caps are
 * actually in force -- no GPU, no video, no network.
 */
#include "player/player.h"
#include "test.h"

#include <mpv/client.h>

static bool mpv_str(mpv_handle *m, const char *name, char *out, size_t cap)
{
    char *s = NULL;
    if (mpv_get_property(m, name, MPV_FORMAT_STRING, &s) < 0 || !s) return false;
    str_copy(out, cap, s);
    mpv_free(s);
    return true;
}

static void check_str_prop(mpv_handle *m, const char *name, const char *want)
{
    char got[128] = "";
    if (!mpv_str(m, name, got, sizeof got)) { t_fail++; printf("  FAIL: no property %s\n", name); return; }
    check_str(got, want);
}

static void check_int_prop(mpv_handle *m, const char *name, long long want)
{
    int64_t got = -1;
    if (mpv_get_property(m, name, MPV_FORMAT_INT64, &got) < 0) { t_fail++; printf("  FAIL: no property %s\n", name); return; }
    check_int(got, want);
}

/* A yes/no option. mpv reads these back as MPV_FORMAT_FLAG; asking for INT64 fails for some of them. */
static void check_flag_prop(mpv_handle *m, const char *name, bool want)
{
    int got = -1;
    if (mpv_get_property(m, name, MPV_FORMAT_FLAG, &got) < 0) { t_fail++; printf("  FAIL: no flag %s\n", name); return; }
    check_int(got ? 1 : 0, want ? 1 : 0);
}

/* The player's mpv handle is private; the test drives its own with the same options through the public API,
 * which is what player_create sets up. To reach the real one, player.h exposes the handle for exactly this. */
mpv_handle *player_mpv_handle(const Player *p);

int main(void)
{
    Player *p = player_create("v4l2m2m-copy", 200);
    check(p != NULL);
    if (!p) test_done();

    mpv_handle *m = player_mpv_handle(p);
    check(m != NULL);
    if (!m) { player_destroy(p); test_done(); }

    /* ---- memory: the whole point ---- */
    check_int_prop(m, "demuxer-max-bytes", 16 * 1024 * 1024);
    check_int_prop(m, "demuxer-max-back-bytes", 4 * 1024 * 1024);
    check_str_prop(m, "cache", "no");
    check_flag_prop(m, "ytdl", false);          /* we resolved the URL: no second yt-dlp */

    /* ---- the display path ---- */
    check_str_prop(m, "vo", "libmpv");
    check_str_prop(m, "hwdec", "v4l2m2m-copy");
    check_flag_prop(m, "vd-lavc-dr", true);
    check_str_prop(m, "video-sync", "display-resample");
    check_str_prop(m, "video-output-levels", "limited");

    /* ---- nothing may override the above ---- */
    check_flag_prop(m, "config", false);
    check_flag_prop(m, "load-scripts", false);

    /* ---- and it does not do the things a player normally does for itself ---- */
    check_str_prop(m, "osc", "no");
    check_flag_prop(m, "input-default-bindings", false);
    check_flag_prop(m, "save-position-on-quit", false);
    check_flag_prop(m, "keep-open", false);

    /* the audio buffer is the 200 ms the launcher's ALSA route wants, not mpv's 1 s default */
    double ab = 0;
    check(mpv_get_property(m, "audio-buffer", MPV_FORMAT_DOUBLE, &ab) >= 0);
    check(ab > 0.0 && ab < 0.5);

    /* the self-pipe the main loop polls is real */
    check(player_wake_fd(p) >= 0);

    /* nothing is loaded to begin with */
    check(!player_active(p));
    check_int(player_width(p), 0);
    check_str(player_error(p), "");

    /* an empty stream is refused rather than handed to mpv */
    StreamUrl empty;
    memset(&empty, 0, sizeof empty);
    check(!player_open(p, &empty));

    /* a stream with a URL is accepted (it will fail to connect, which is fine: no network here) */
    StreamUrl s;
    memset(&s, 0, sizeof s);
    str_copy(s.url, sizeof s.url, "https://127.0.0.1:1/nonexistent");
    str_copy(s.vcodec, sizeof s.vcodec, "avc1");
    s.width = 640; s.height = 480; s.muxed = true;
    check(player_open(p, &s));
    check(player_active(p));
    /* the URL is handed to mpv, so the geometry the resolver found is what the UI shows at once */
    check_int(player_width(p), 640);
    check_int(player_height(p), 480);

    player_tick(p);            /* drains whatever mpv has said so far */

    player_destroy(p);
    test_done();
}
