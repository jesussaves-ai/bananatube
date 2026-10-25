/* history.c */
#include "history.h"
#include "net/http.h"          /* the cJSON helpers */
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void history_clear(History *h)
{
    memset(h, 0, sizeof *h);
}

bool history_load(History *h, const char *path)
{
    history_clear(h);
    if (!path) return false;

    size_t n = 0;
    char *text = file_read(path, &n);
    if (!text) return false;                       /* first run: an empty history is not an error */

    cJSON *root = json_parse(text, n, "history");
    free(text);
    if (!root) return false;

    json_str(root, "query", h->query, sizeof h->query);
    const cJSON *items = json_at(root, "items");
    const cJSON *o = NULL;
    cJSON_ArrayForEach(o, items) {
        if (h->count >= HISTORY_MAX) break;
        if (!cJSON_IsObject(o)) continue;
        HistoryItem *it = &h->item[h->count];
        json_str(o, "id", it->id, sizeof it->id);
        if (!it->id[0]) continue;
        json_str(o, "title", it->title, sizeof it->title);
        json_str(o, "channel", it->channel, sizeof it->channel);
        it->duration_sec = json_int(o, "duration", -1);
        it->position = json_num(o, "position", 0);
        it->watched_at = (int64_t)json_ll(o, "at", 0);
        h->count++;
    }
    cJSON_Delete(root);
    return true;
}

bool history_save(const History *h, const char *path)
{
    if (!h || !path) return false;

    cJSON *root = cJSON_CreateObject();
    if (!root) return false;
    cJSON_AddStringToObject(root, "query", h->query);
    cJSON *items = cJSON_AddArrayToObject(root, "items");
    for (int i = 0; i < h->count; i++) {
        const HistoryItem *it = &h->item[i];
        cJSON *o = cJSON_CreateObject();
        if (!o) break;
        cJSON_AddStringToObject(o, "id", it->id);
        cJSON_AddStringToObject(o, "title", it->title);
        cJSON_AddStringToObject(o, "channel", it->channel);
        cJSON_AddNumberToObject(o, "duration", it->duration_sec);
        cJSON_AddNumberToObject(o, "position", it->position);
        cJSON_AddNumberToObject(o, "at", (double)it->watched_at);
        cJSON_AddItemToArray(items, o);
    }

    char *text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text) return false;

    /* Rename over the old file: a card pulled mid-write then has the old history, not half of one. */
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s.new", path);
    FILE *f = fopen(tmp, "w");
    if (!f) { free(text); return false; }
    fputs(text, f);
    fclose(f);
    free(text);

    if (rename(tmp, path) != 0) { remove(tmp); return false; }
    return true;
}

static int index_of(const History *h, const char *id)
{
    for (int i = 0; i < h->count; i++)
        if (strcmp(h->item[i].id, id) == 0) return i;
    return -1;
}

static void move_to_front(History *h, int at)
{
    if (at <= 0) return;
    HistoryItem it = h->item[at];
    for (int i = at; i > 0; i--) h->item[i] = h->item[i - 1];
    h->item[0] = it;
}

void history_touch(History *h, const char *id, const char *title, const char *channel, int duration_sec)
{
    if (!h || !id || !*id) return;

    int at = index_of(h, id);
    if (at < 0) {
        if (h->count >= HISTORY_MAX) h->count = HISTORY_MAX - 1;   /* the oldest falls off the end */
        at = h->count++;
        memset(&h->item[at], 0, sizeof h->item[at]);
        str_copy(h->item[at].id, sizeof h->item[at].id, id);
    }
    str_copy(h->item[at].title, sizeof h->item[at].title, title ? title : "");
    str_copy(h->item[at].channel, sizeof h->item[at].channel, channel ? channel : "");
    if (duration_sec > 0) h->item[at].duration_sec = duration_sec;
    h->item[at].watched_at = (int64_t)time(NULL);
    move_to_front(h, at);
}

void history_progress(History *h, const char *id, double position)
{
    int at = index_of(h, id);
    if (at < 0 || position < 0) return;
    h->item[at].position = position;
}

const HistoryItem *history_find(const History *h, const char *id)
{
    int at = index_of(h, id);
    return at < 0 ? NULL : &h->item[at];
}

int history_order(const History *h, int *out, int max)
{
    /* The list is kept most-recent-first already, so this is a copy with the ones that never played dropped. */
    int n = 0;
    for (int i = 0; i < h->count && n < max; i++) {
        if (h->item[i].watched_at == 0) continue;
        out[n++] = i;
    }
    return n;
}
