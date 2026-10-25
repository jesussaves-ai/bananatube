/* resolvejob.h -- resolving a video's stream URL, off the thread that draws.
 *
 * The resolve is the slowest thing the client does: a few HTTP calls, and yt-dlp's one run when the backends
 * cannot answer. On the main thread that is a second or two of frozen panels, which is how it was first
 * reported from the handheld ("it freezes when I try to start a video"). Same shape as the search job: a thread
 * that does the work, and the loop picks the answer up when it is ready.
 *
 * The loop watches it by polling (resolve_job_busy), so nothing has to wake it: a resolve is a second, and the
 * main loop already wakes a few times a second for the spinner.
 */
#ifndef BANANATUBE_RESOLVEJOB_H
#define BANANATUBE_RESOLVEJOB_H

#include "net/backend.h"

#include <stdbool.h>

typedef struct ResolveJob ResolveJob;

ResolveJob *resolve_job_start(void);
void        resolve_job_stop(ResolveJob *j);

/* Asks for one video. Ignored while another resolve is still running. */
bool        resolve_job_begin(ResolveJob *j, const Conf *c, const char *video_id);

/* True while a resolve is in flight (the loop shortens its wait on this). */
bool        resolve_job_busy(ResolveJob *j);

/* Takes the answer once: true when one was waiting, and then `out`/`status` are filled in. */
bool        resolve_job_take(ResolveJob *j, StreamUrl *out, ResolveStatus *status);

#endif /* BANANATUBE_RESOLVEJOB_H */
