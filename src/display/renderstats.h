/* renderstats.h -- where a frame's painting time goes, by kind of draw (rect, text, image).
 *
 * The frame log says how long a panel took; this says what it was doing. It is what turned "the UI is slow"
 * into a number: on the RG DS Plus a keypress that repaints two tiles costs ~185 ms, and this is the split. */
#ifndef BANANATUBE_RENDERSTATS_H
#define BANANATUBE_RENDERSTATS_H

/* Microseconds since an arbitrary point: the clock the drawing timings use. */
double render_now_us(void);

void render_stats_reset(void);
/* ms[3] and calls[3], indexed by OpKind: 0 rect, 1 text, 2 image. */
void render_stats(double *ms, long *calls);

#endif /* BANANATUBE_RENDERSTATS_H */
