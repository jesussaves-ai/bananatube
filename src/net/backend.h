/* backend.h -- what the app asks of YouTube, and what it gets back.
 *
 * Two sources behind one interface (conf.backend chooses, "auto" tries them in order):
 *   invidious.c   an Invidious/Piped instance: one GET for a search, one for a video
 *   innertube.c   YouTube's own /youtubei/v1 API, the way the Android VR and web clients do it
 *
 * Neither spawns anything. yt-dlp is the resolver's last resort and runs once, at the moment a video is
 * picked (net/resolver.c), never for a search.
 */
#ifndef BANANATUBE_BACKEND_H
#define BANANATUBE_BACKEND_H

#include "conf.h"

#include <stdbool.h>

#define VIDEO_ID_LEN 16
#define TITLE_LEN    160
#define CHANNEL_LEN  80
#define URL_LEN      1024
#define DESC_LEN     384

typedef struct {
    char      id[VIDEO_ID_LEN];
    char      title[TITLE_LEN];
    char      channel[CHANNEL_LEN];
    int       duration_sec;        /* -1 unknown */
    long long views;               /* -1 unknown */
    bool      live;
} SearchResult;

typedef struct {
    char      id[VIDEO_ID_LEN];
    char      title[TITLE_LEN];
    char      channel[CHANNEL_LEN];
    char      description[DESC_LEN];
    int       duration_sec;
    long long views, likes;
    char      published[32];
} VideoDetail;

/* One entry of a DASH/adaptive format list, as either backend hands it over. */
typedef struct {
    char url[URL_LEN];
    char mime[24];                 /* "video/mp4", "video/webm", "audio/mp4" */
    char vcodec[24];               /* "avc1.64001f", "vp09.00.31.08", "av01.0.08M.08" */
    char acodec[24];               /* "mp4a.40.2", or empty for a video-only stream */
    int  width, height, fps;
    int  bitrate_kbps;
    int  audio_kbps;
    bool video, audio;
    bool has_url;                  /* false: it came as a signatureCipher we will not solve */
} Format;

typedef struct {
    char url[URL_LEN];             /* the stream libmpv opens */
    char audio_url[URL_LEN];       /* a separate audio stream (DASH), or empty when url carries both */
    int  width, height, fps;
    int  audio_kbps;
    char vcodec[24];
    bool muxed;
} StreamUrl;

typedef enum {
    RESOLVE_OK = 0,
    RESOLVE_NO_NETWORK,            /* curl could not reach anything */
    RESOLVE_INSTANCE_FAILED,       /* the instance answered, but with an error or nothing usable */
    RESOLVE_NO_SUITABLE_FORMAT,    /* only AV1, or nothing at or under the quality cap */
    RESOLVE_NEEDS_CIPHER,          /* the only URLs came as signatureCipher */
    RESOLVE_ALL_FAILED             /* every path was tried */
} ResolveStatus;

const char *resolve_status_text(ResolveStatus s);

/* A backend's three calls. search returns how many results it wrote (0 on failure); detail and resolve
 * return 0 on success. */
typedef struct {
    const char *name;
    int (*search)(const Conf *c, const char *query, SearchResult *out, int max);
    int (*detail)(const Conf *c, const char *id, VideoDetail *out);
    ResolveStatus (*streams)(const Conf *c, const char *id, Format *out, int max);
} Backend;

extern const Backend backend_invidious;
extern const Backend backend_innertube;

/* ---- the format rules, shared by both backends and tested on their own ---------------------------------
 * The RK3568's video unit decodes H.264 and VP9; AV1 is software-only and would peg all four A55s at 480p,
 * so an av01 format is never chosen. Prefer H.264 (cheapest at the VPU's clock), then VP9, then the lowest
 * bitrate of whichever is allowed. */
#define FORMAT_MAX_FPS 60
int select_video_format(const Format *f, int n, int max_height, int max_fps);
int select_audio_format(const Format *f, int n, bool allow_webm);  /* the highest-bitrate audio-only stream */

/* True for a codec string the handheld's video unit can decode. */
bool codec_hardware_decodable(const char *vcodec);

#endif /* BANANATUBE_BACKEND_H */
