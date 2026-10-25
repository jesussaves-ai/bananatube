/* The stream rules: the RK3568's video unit decodes H.264 and VP9, never AV1. Getting this wrong is a video
 * that plays at two frames a second with all four cores pegged. */
#include "net/backend.h"
#include "test.h"

#include <string.h>

static Format fmt(const char *codec, const char *mime, int h, int fps, int kbps, const char *url)
{
    Format f;
    memset(&f, 0, sizeof f);
    strncpy(f.vcodec, codec, sizeof f.vcodec - 1);
    strncpy(f.mime, mime, sizeof f.mime - 1);
    f.height = h;
    f.fps = fps;
    f.bitrate_kbps = kbps;
    f.video = true;
    f.has_url = url && *url;
    if (url) strncpy(f.url, url, sizeof f.url - 1);
    return f;
}

static Format audio(const char *mime, int kbps, const char *url)
{
    Format f;
    memset(&f, 0, sizeof f);
    strncpy(f.mime, mime, sizeof f.mime - 1);
    strncpy(f.acodec, "mp4a.40.2", sizeof f.acodec - 1);
    f.audio_kbps = kbps;
    f.audio = true;
    f.has_url = true;
    strncpy(f.url, url, sizeof f.url - 1);
    return f;
}

int main(void)
{
    /* AV1 is not decodable, whatever its size */
    check(!codec_hardware_decodable("av01.0.05M.08"));
    check(codec_hardware_decodable("avc1.4d401f"));
    check(codec_hardware_decodable("vp09.00.31.08"));
    check(codec_hardware_decodable("vp9"));
    check(!codec_hardware_decodable("something-else"));

    Format f[8];
    f[0] = fmt("av01.0.05M.08", "video/mp4", 480, 30, 400, "https://x/av1-480.mp4");
    f[1] = fmt("avc1.4d401f", "video/mp4", 720, 30, 1500, "https://x/avc1-720.mp4");
    f[2] = fmt("avc1.4d401f", "video/mp4", 480, 30, 600, "https://x/avc1-480.mp4");
    f[3] = fmt("vp9", "video/webm", 480, 30, 500, "https://x/vp9-480.webm");
    f[4] = fmt("avc1.640028", "video/mp4", 1080, 30, 3000, "https://x/avc1-1080.mp4");
    f[5] = fmt("avc1.4d401f", "video/mp4", 720, 60, 1600, "https://x/avc1-720-60.mp4");
    f[6] = fmt("avc1.4d401f", "video/mp4", 720, 30, 0, "");            /* no URL: a signatureCipher */
    f[7] = fmt("", "video/mp4", 480, 30, 100, "https://x/nocodec.mp4"); /* an instance that omits the codec */

    /* 480p cap: H.264 at 480 wins over VP9 at 480 (the VPU's cheaper path), and AV1 is ignored */
    check_int(select_video_format(f, 8, 480, 60), 2);
    /* 720p cap: the 720p H.264 */
    check_int(select_video_format(f, 8, 720, 60), 1);
    /* 60 fps is allowed; above it is not */
    check_int(select_video_format(f, 8, 720, 30), 1);
    check_int(select_video_format(f, 6, 720, 30), 1);       /* index 5 (60 fps) is out, index 6 has no URL */
    /* no cap at all: still never AV1, so the 1080p H.264 */
    check_int(select_video_format(f, 8, 0, 60), 4);

    /* only AV1 offered: nothing suitable, not a wrong choice */
    Format only_av1[1];
    only_av1[0] = fmt("av01.0.05M.08", "video/mp4", 480, 30, 400, "https://x/a.mp4");
    check_int(select_video_format(only_av1, 1, 720, 60), -1);

    /* a ciphered entry is never chosen, even when it is the only one of its codec */
    Format ciphered[1];
    ciphered[0] = fmt("avc1.4d401f", "video/mp4", 480, 30, 600, "");
    check_int(select_video_format(ciphered, 1, 720, 60), -1);

    /* audio: the highest bitrate audio-only stream; webm only when allowed */
    Format a[3];
    a[0] = audio("audio/mp4", 128, "https://x/a128.m4a");
    a[1] = audio("audio/webm", 160, "https://x/a160.webm");
    a[2] = fmt("avc1.4d401f", "video/mp4", 480, 30, 600, "https://x/v.mp4");   /* a muxed entry: not audio-only */
    a[2].acodec[0] = 'm';
    check_int(select_audio_format(a, 3, true), 1);
    check_int(select_audio_format(a, 3, false), 0);

    check_str(resolve_status_text(RESOLVE_OK), "ready");
    check(str_contains(resolve_status_text(RESOLVE_NO_SUITABLE_FORMAT), "decode"));

    test_done();
}
