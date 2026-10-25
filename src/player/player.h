/* player.h -- libmpv, rendering into the top panel's EGL surface.
 *
 * The options are the whole point of this file: mpv's defaults would cache 150 MB of stream on a machine with
 * 1 GB of RAM, spawn its own yt-dlp (we resolved the URL ourselves), and read a stray mpv.conf that could
 * undo all of it. Every one of them is set here, and tests/test_player.c checks they took.
 *
 * What plays is decided by net/resolver.c: a video URL, and for DASH a separate audio one.
 */
#ifndef BANANATUBE_PLAYER_H
#define BANANATUBE_PLAYER_H

#include "net/backend.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct Player Player;

/* Where the video goes: the EGL display and context gbm_egl.c made for the top panel. */
typedef struct {
    void *egl_display;
    void *egl_context;
    void *egl_surface;
    void *(*get_proc_address)(const char *);
    int   width, height;          /* the panel's pixels */
} PlayerGl;

/* mpv and its options. No GPU involved: what tests/test_player.c checks. */
Player *player_create(const char *hwdec, int audio_buffer_ms);
/* Point the render API at the top panel's EGL context, which must be current on this thread. */
bool    player_attach_render(Player *p, const PlayerGl *gl);
void    player_destroy(Player *p);

bool    player_open(Player *p, const StreamUrl *s);
void    player_close(Player *p);
bool    player_active(const Player *p);          /* a file is loaded (not necessarily playing) */

/* A byte arrives on this fd when mpv has a frame: put it in poll() rather than polling the player. */
int     player_wake_fd(const Player *p);
void    player_wake_drain(Player *p);

/* mpv asked for a frame (or the state changed): draw the surface and present it. */
bool    player_frame_ready(Player *p);
void    player_render(Player *p);
void    player_tick(Player *p);                  /* drain mpv's events: errors, end of file */

void    player_set_paused(Player *p, bool paused);
bool    player_paused(const Player *p);
void    player_toggle_pause(Player *p);
void    player_seek(Player *p, double seconds);  /* by seconds, relative */
void    player_set_volume(Player *p, int percent);

double  player_position(const Player *p);        /* seconds, 0 when unknown */
double  player_duration(const Player *p);
bool    player_buffering(const Player *p);       /* reading ahead: what the spinner is for */
bool    player_eof(const Player *p);
int     player_width(const Player *p);           /* the picture's size, for the badge */
int     player_height(const Player *p);
int     player_dropped(const Player *p);         /* frames the renderer dropped (a stutter to explain) */
const char *player_error(const Player *p);       /* why the last open failed, or "" */

/* The mpv handle, for tests/test_player.c to read the options back. Nothing in the app needs it. */
struct mpv_handle *player_mpv_handle(const Player *p);

#endif /* BANANATUBE_PLAYER_H */
