/* drawlist.h -- a frame's draws, and the damage between two frames. */
#ifndef BANANATUBE_DRAWLIST_H
#define BANANATUBE_DRAWLIST_H

#include "display/draw.h"

#define DRAW_MAX 512

typedef struct {
    DrawOp op[DRAW_MAX];
    int    n;
    int    dropped;            /* how many draws were refused because the list was full (0 in a sane frame) */
    /* the clip stack: push_clip narrows it, pop_clip widens it back. Every op stores the resolved clip
     * that was in force when it was pushed. */
    int    cx, cy, cw, ch;
    int    scx[8], scy[8], scw[8], sch[8];
    int    depth;
} DrawList;

void dl_reset(DrawList *dl);                       /* clears the ops and the clip to the whole screen */
void dl_push_clip(DrawList *dl, int x, int y, int w, int h);
void dl_pop_clip(DrawList *dl);

/* Appends an op with the current clip resolved. Returns a pointer to the stored op (to fill in), or NULL
 * when the list is full -- a frame that overflows DRAW_MAX drops the draws past the end rather than
 * growing a buffer nobody needs on a 1 GB device. */
DrawOp *dl_push(DrawList *dl, OpKind kind);

/* Convenience forms for the three kinds. */
DrawOp *dl_rect(DrawList *dl, int x, int y, int w, int h, uint32_t color);
DrawOp *dl_rect_rounded(DrawList *dl, int x, int y, int w, int h, int radius, uint32_t color);
DrawOp *dl_rect_outlined(DrawList *dl, int x, int y, int w, int h, int radius,
                         uint32_t color, int border, uint32_t border_color);
DrawOp *dl_text(DrawList *dl, int x, int y, int w, int h, const char *text, FontId font, int size,
                uint32_t color, HAlign align, VAlign valign);
/* Text that breaks at spaces into at most `max_lines` lines: a video title. */
DrawOp *dl_text_wrapped(DrawList *dl, int x, int y, int w, int h, const char *text, FontId font, int size,
                        uint32_t color, HAlign align, VAlign valign, int max_lines, int line_height);
DrawOp *dl_image(DrawList *dl, int x, int y, int w, int h, int image, Fit fit, uint32_t placeholder);

/* ---- the damage between two frames -------------------------------------------------------------------
 * Walks the two lists together. An op that is identical in the same place costs nothing; an op that came,
 * went, moved or changed contributes its old and new boxes. */
void dl_diff(const DrawList *prev, const DrawList *cur, Damage *out);

#endif /* BANANATUBE_DRAWLIST_H */
