/* innertube.h -- the parse half of the InnerTube backend, so it can be tested from a canned reply. */
#ifndef BANANATUBE_INNERTUBE_H
#define BANANATUBE_INNERTUBE_H

#include "net/backend.h"

/* Walks the reply for every "videoRenderer" -- YouTube moves them around between builds, so the tree is
 * searched rather than addressed by path. Returns how many results it wrote. */
int innertube_parse_search(const char *json, SearchResult *out, int max);

/* streamingData.adaptiveFormats + .formats, and whether anything usable came back. */
int innertube_parse_formats(const char *json, Format *out, int max, ResolveStatus *status);

int innertube_parse_video(const char *json, const char *id, VideoDetail *out);

#endif /* BANANATUBE_INNERTUBE_H */
