/* Parsing the two backends' replies, from files: no network, and the shapes are the ones the tests assert. */
#include "net/invidious.h"
#include "net/innertube.h"
#include "test.h"

int main(void)
{
    SearchResult rs[16];
    Format fmts[64];

    /* ---- Invidious search ---- */
    char *json = test_read("invidious_search.json");
    check(json != NULL);
    int n = invidious_parse_search(json, rs, 16);
    check_int(n, 2);                                  /* the channel entry is not a video */
    check_str(rs[0].id, "dQw4w9WgXcQ");
    check_str(rs[0].title, "Making a theme for BananaOS");
    check_str(rs[0].channel, "Banana Dev");
    check_int(rs[0].duration_sec, 754);
    check_int(rs[0].views, 1234567);
    check(!rs[0].live);
    check_str(rs[1].id, "abc12345678");
    check_int(rs[1].duration_sec, 3700);

    /* ---- Invidious formats ---- */
    json = test_read("invidious_videos.json");
    ResolveStatus st;
    int nf = invidious_parse_formats(json, fmts, 64, &st);
    check_int(st, RESOLVE_OK);
    check(nf > 0);
    /* adaptive video, audio-only, ciphered, muxed */
    int nvid = 0, naud = 0, nciphered = 0, nmuxed = 0;
    for (int i = 0; i < nf; i++) {
        if (!fmts[i].has_url) nciphered++;
        else if (fmts[i].audio && !fmts[i].video) naud++;
        else if (fmts[i].video && fmts[i].acodec[0]) nmuxed++;
        else if (fmts[i].video) nvid++;
    }
    check_int(nvid, 6);          /* av1 x2, avc1 x3 (1080p included: the cap is applied when one is chosen), vp9 x1 */
    check_int(naud, 2);
    check_int(nciphered, 1);
    check_int(nmuxed, 1);

    /* and the rules pick the 480p H.264 out of them */
    int pick = select_video_format(fmts, nf, 480, FORMAT_MAX_FPS);
    check(pick >= 0);
    check_int(fmts[pick].height, 480);
    check(str_starts(fmts[pick].vcodec, "avc1"));

    /* ---- InnerTube search ---- */
    json = test_read("innertube_search.json");
    n = innertube_parse_search(json, rs, 16);
    check_int(n, 2);
    check_str(rs[0].id, "vidOne00001");
    check_str(rs[0].title, "First result");            /* two runs joined */
    check_str(rs[0].channel, "Channel One");
    check_int(rs[0].duration_sec, 12 * 60 + 34);
    check_int(rs[0].views, 1234);
    check_str(rs[1].title, "Second result");           /* simpleText */
    check_int(rs[1].duration_sec, 3723);
    check(rs[1].live);

    /* ---- InnerTube player ---- */
    /* ---- InnerTube, as the API actually answers ----
     * The Android VR client returns compactVideoRenderer for a search, not videoRenderer. The fixture above
     * was hand-written with the name the parser wanted, so nothing caught the difference and the app read
     * zero results out of a 200 response containing nineteen videos. This one is a real response, captured
     * with the app's own payload and pruned to three items; only the human-readable text was replaced. */
    json = test_read("innertube_search_compact.json");
    n = innertube_parse_search(json, rs, 16);
    check_int(n, 3);
    check_str(rs[0].id, "0OmRrFD8zJk");
    check_str(rs[0].title, "Result 1");
    check_str(rs[0].channel, "Channel 1");
    check_int(rs[0].duration_sec, 3 * 60 + 16);
    check(rs[0].views > 139000000 && rs[0].views < 140000000);
    check_str(rs[1].title, "Result 2");
    check_str(rs[2].title, "Result 3");
    check_int(rs[2].duration_sec, 18);
    check(!rs[0].live);
    /* a truncated list is still a list */
    check_int(innertube_parse_search(json, rs, 2), 2);

    json = test_read("innertube_player.json");
    VideoDetail d;
    check_int(innertube_parse_video(json, "vidOne00001", &d), 0);
    check_str(d.title, "First result");
    check_str(d.channel, "Channel One");
    check_int(d.duration_sec, 754);
    check_int(d.views, 1234567);

    nf = innertube_parse_formats(json, fmts, 64, &st);
    check_int(st, RESOLVE_OK);
    /* The only 480p entry there is AV1 (ciphered, too), so at a 480 cap there is nothing the VPU can decode,
     * and the resolver says so rather than falling back to software. */
    check_int(select_video_format(fmts, nf, 480, FORMAT_MAX_FPS), -1);
    pick = select_video_format(fmts, nf, 720, FORMAT_MAX_FPS);
    check(pick >= 0);
    check_int(fmts[pick].height, 720);
    check(str_starts(fmts[pick].vcodec, "avc1"));        /* H.264 over the VP9 at the same size */

    test_done();
}
