/* resolver.h -- a video id to something libmpv can open.
 *
 * The order, and the only place a process may be spawned, is in net/resolver.c. Phase 3 calls this from the
 * UI when a result is chosen.
 */
#ifndef BANANATUBE_RESOLVER_H
#define BANANATUBE_RESOLVER_H

#include "net/backend.h"

/* Fills *out with a playable stream (and, for DASH, its audio). Blocking: 1-3 s on an instance, longer on the
 * yt-dlp fallback, which is what the "Starting…" spinner on the bottom screen is for. */
ResolveStatus resolver_resolve(const Conf *c, const char *video_id, StreamUrl *out);

#endif /* BANANATUBE_RESOLVER_H */
