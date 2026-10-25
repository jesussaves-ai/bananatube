/* invidious.h -- the parts of the Invidious/Piped backend that can be exercised without a network.
 *
 * The fetch functions are in the Backend vtable (net/backend.h); these parse a body that has already been
 * fetched, which is what the tests feed from tests/data.
 */
#ifndef BANANATUBE_INVIDIOUS_H
#define BANANATUBE_INVIDIOUS_H

#include "net/backend.h"

/* The instance to use: conf's if it names one, else the built-in list. NULL when `which` is past the end. */
const char *invidious_instance(const Conf *c, int which);

/* A search body: Invidious's bare array, or Piped's {"items":[...]}. Returns how many results it wrote. */
int invidious_parse_search(const char *json, SearchResult *out, int max);

/* A video body (/api/v1/videos/<id>, or Piped's /streams/<id>): the formats it offers, and whether any of
 * them came as a signatureCipher rather than a URL. */
int invidious_parse_formats(const char *json, Format *out, int max, ResolveStatus *status);

int invidious_parse_video(const char *json, const char *id, VideoDetail *out);

#endif /* BANANATUBE_INVIDIOUS_H */
