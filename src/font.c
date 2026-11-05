/* font.c -- FreeType with a small glyph cache. */
#include "font.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#define GLYPH_CACHE 1024
/* A direct-mapped index from (font, size, codepoint) to a slot: the lookup was a linear scan of all 1024
 * slots, and every character of every string is looked up several times a frame (the text is measured for
 * its width, wrapped, and drawn). A collision only costs a reload, never a wrong glyph. */
#define GLYPH_INDEX 4096

typedef struct {
    FontId   id;
    int      size;
    uint32_t cp;
    bool     ok;          /* the slot holds this glyph. A space rasterises to no bitmap, so `bm == NULL`
                             cannot stand for "empty": testing it re-rasterised every space on every
                             lookup (measuring, wrapping and drawing all walk the same string). */
    int      w, h, bx, by, adv;
    uint8_t *bm;          /* w*h coverage, 8 bits a pixel */
} Glyph;

struct FontCtx {
    FT_Library lib;
    FT_Face    face[3];
    Glyph      glyph[GLYPH_CACHE];
    int        index[GLYPH_INDEX];   /* hash of the key -> slot + 1, or 0 */
    int        lh[3][201];           /* line height by face and size: FT_Set_Pixel_Sizes is not free and this
                                      * is asked for on every text draw */
    int        next;      /* round-robin replacement */
};

static uint32_t glyph_hash(FontId id, int size, uint32_t cp)
{
    uint32_t h = cp * 2654435761u;
    h ^= (uint32_t)size * 40503u;
    h ^= (uint32_t)id * 2246822519u;
    return h & (GLYPH_INDEX - 1);
}

static int utf8_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    if (!p || !*p) return -1;
    uint32_t c = *p++;
    int extra = 0;
    if (c >= 0xf0) { c &= 0x07; extra = 3; }
    else if (c >= 0xe0) { c &= 0x0f; extra = 2; }
    else if (c >= 0xc0) { c &= 0x1f; extra = 1; }
    else { *s = (const char *)p; return (int)c; }
    for (int i = 0; i < extra; i++) {
        if ((*p & 0xc0) != 0x80) break;
        c = (c << 6) | (*p++ & 0x3f);
    }
    *s = (const char *)p;
    return (int)c;
}

FontCtx *font_init(const char *regular, const char *bold, const char *mono)
{
    FontCtx *f = calloc(1, sizeof *f);
    if (!f) return NULL;
    if (FT_Init_FreeType(&f->lib)) { free(f); return NULL; }

    const char *path[3] = { regular, bold, mono };
    for (int i = 0; i < 3; i++) {
        if (!path[i]) continue;
        if (FT_New_Face(f->lib, path[i], 0, &f->face[i])) {
            log_line("font: could not load %s", path[i]);
            f->face[i] = NULL;
        }
    }
    /* A missing face uses whichever loaded, so a theme with only one font still draws everything. */
    for (int i = 0; i < 3; i++)
        if (!f->face[i])
            f->face[i] = f->face[0] ? f->face[0] : (f->face[1] ? f->face[1] : f->face[2]);
    if (!f->face[0]) { FT_Done_FreeType(f->lib); free(f); return NULL; }
    return f;
}

void font_free(FontCtx *f)
{
    if (!f) return;
    for (int i = 0; i < GLYPH_CACHE; i++) free(f->glyph[i].bm);
    /* The faces may alias one another: only the distinct pointers are done with. */
    FT_Face done[3] = { NULL, NULL, NULL };
    for (int i = 0; i < 3; i++) {
        if (!f->face[i]) continue;
        bool seen = false;
        for (int k = 0; k < 3; k++) if (done[k] == f->face[i]) seen = true;
        if (!seen) { FT_Done_Face(f->face[i]); done[i] = f->face[i]; }
    }
    FT_Done_FreeType(f->lib);
    for (int i = 0; i < GLYPH_CACHE; i++) f->glyph[i].bm = NULL;
    free(f);
}

static Glyph *glyph_get(FontCtx *f, FontId id, int size, uint32_t cp)
{
    if (!f || !f->face[id]) return NULL;
    if (size < 1) size = 1;
    if (size > 200) size = 200;

    uint32_t hash = glyph_hash(id, size, cp);
    int idx = f->index[hash];
    if (idx) {
        Glyph *g = &f->glyph[idx - 1];
        if (g->ok && g->id == id && g->size == size && g->cp == cp) return g;
    }

    if (FT_Set_Pixel_Sizes(f->face[id], 0, (FT_UInt)size)) return NULL;
    if (FT_Load_Char(f->face[id], cp, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL)) return NULL;

    Glyph *g = &f->glyph[f->next];
    int slot = f->next;
    f->next = (f->next + 1) % GLYPH_CACHE;

    /* the slot being reused leaves the index if it was what this key's hash pointed at */
    if (g->ok) {
        uint32_t oh = glyph_hash(g->id, g->size, g->cp);
        if (f->index[oh] == slot + 1) f->index[oh] = 0;
    }

    const FT_GlyphSlot s = f->face[id]->glyph;
    int w = (int)s->bitmap.width, h = (int)s->bitmap.rows;

    free(g->bm);
    g->bm = (w > 0 && h > 0) ? malloc((size_t)w * h) : NULL;
    if (w > 0 && h > 0 && g->bm)
        for (int y = 0; y < h; y++)
            memcpy(g->bm + (size_t)y * w, s->bitmap.buffer + (size_t)y * s->bitmap.pitch, (size_t)w);

    g->id = id; g->size = size; g->cp = cp;
    g->w = w; g->h = h;
    g->bx = s->bitmap_left;
    g->by = s->bitmap_top;
    g->adv = (int)(s->advance.x >> 6);
    g->ok = true;
    f->index[hash] = slot + 1;
    return g;
}

int font_line_height(FontCtx *f, FontId id, int size)
{
    if (!f || !f->face[id]) return size;
    if (size < 1) size = 1;
    if (size > 200) size = 200;
    if (f->lh[id][size]) return f->lh[id][size];
    FT_Set_Pixel_Sizes(f->face[id], 0, (FT_UInt)size);
    return f->lh[id][size] = (int)(f->face[id]->size->metrics.height >> 6);
}

int font_text_width(FontCtx *f, FontId id, int size, const char *utf8)
{
    if (!f || !utf8) return 0;
    int w = 0;
    const char *s = utf8;
    for (int cp; (cp = utf8_next(&s)) >= 0; ) {
        Glyph *g = glyph_get(f, id, size, (uint32_t)cp);
        w += g ? g->adv : size / 2;
    }
    return w;
}

/* Breaks `utf8` at spaces into lines no wider than `w`, at most `max_lines`, and reports how many it made.
 * The last line is ellipsised when the text does not fit. The width is measured as the line is walked: the
 * first version re-measured the whole prefix at every character (O(len^2) a line), and a line is wrapped
 * again every frame it is drawn, so a long title paid for its own length twice over. */
static int wrap_text(FontCtx *f, FontId id, int size, const char *utf8, int w, int max_lines,
                     char out[][192], int cap)
{
    if (!utf8) return 0;
    if (max_lines < 1) max_lines = 1;
    if (max_lines > cap) max_lines = cap;
    if (w <= 0) w = 1;

    int n = 0;
    const char *p = utf8;
    while (*p && n < max_lines) {
        /* the longest run of characters that fits (or, if the first one alone does not, that one anyway) */
        int taken = 0, last_space = -1, width = 0;
        const char *q = p;
        while (*q && *q != '\n') {
            if (*q == ' ') last_space = (int)(q - p);
            const char *next = q;
            int cp = utf8_next(&next);
            Glyph *g = glyph_get(f, id, size, (uint32_t)cp);
            int adv = g ? g->adv : size / 2;
            if (taken > 0 && width + adv > w) break;
            width += adv;
            taken = (int)(next - p);
            q = next;
            if (taken >= 191) break;
        }
        /* a word boundary is nicer than a chopped word, as long as it is not the whole line */
        int cut = taken;
        if (p[cut] && p[cut] != '\n' && last_space > 0 && last_space < taken) cut = last_space;
        /* Never stall: if even one character does not fit, take it anyway so p advances. */
        if (cut == 0 && p[0] && p[0] != '\n') {
            const char *r = p;
            utf8_next(&r);
            cut = (int)(r - p);
        }

        size_t out_n = (size_t)cut;
        const char *rest = p + cut;
        while (*rest == ' ') rest++;

        bool last = (n == max_lines - 1);
        bool more = (*rest != '\0');
        if (out_n >= 191) out_n = 191;
        memcpy(out[n], p, out_n);
        out[n][out_n] = '\0';
        if (last && more) {
            /* it does not all fit: end the last line with an ellipsis, trimming whole characters */
            int ell = font_text_width(f, id, size, "...");
            int fit = 0, fw = 0;
            const char *r = out[n];
            while (*r) {
                const char *next = r;
                int cp = utf8_next(&next);
                Glyph *g = glyph_get(f, id, size, (uint32_t)cp);
                int adv = g ? g->adv : size / 2;
                if (fw + adv + ell > w && fit > 0) break;
                fw += adv;
                fit = (int)(next - out[n]);
                r = next;
            }
            out[n][fit] = '\0';
            str_copy(out[n] + fit, sizeof out[n] - fit, "...");
        }
        n++;
        if (!more) break;
        p = rest;
    }
    /* one trailing newline run: the caller asked for lines, and an empty last line is not one */
    while (n > 1 && out[n - 1][0] == '\0') n--;
    return n;
}

int font_wrap_count(FontCtx *f, FontId id, int size, const char *utf8, int w, int max_lines)
{
    char lines[8][192];
    if (max_lines > 8) max_lines = 8;
    return wrap_text(f, id, size, utf8, w, max_lines, lines, 8);
}

void font_draw_box(FontCtx *f, uint32_t *dst, int dst_w, int dst_h,
                   int clip_x, int clip_y, int clip_w, int clip_h,
                   int x, int y, int w, int h, const char *utf8,
                   FontId id, int size, uint32_t color, HAlign align, VAlign valign,
                   bool wrap, int max_lines, int line_height)
{
    if (!f || !dst || !utf8 || !*utf8) return;

    char lines[8][192];
    int nl = 1;
    if (wrap) nl = wrap_text(f, id, size, utf8, w, max_lines > 0 ? max_lines : 8, lines, 8);
    else str_copy(lines[0], sizeof lines[0], utf8);

    int lh = line_height > 0 ? line_height : font_line_height(f, id, size);
    int block = lh * nl;
    int top = y;
    if (valign == VALIGN_MIDDLE) top = y + (h - block) / 2;
    else if (valign == VALIGN_BOTTOM) top = y + h - block;

    int font_lh = font_line_height(f, id, size);
    for (int i = 0; i < nl; i++) {
        int tw = (align == ALIGN_LEFT) ? w : font_text_width(f, id, size, lines[i]);
        int px = x;
        if (align == ALIGN_CENTER) px = x + (w - tw) / 2;
        else if (align == ALIGN_RIGHT) px = x + w - tw;
        int py = top + i * lh + font_lh;               /* the baseline */
        font_draw(f, dst, dst_w, dst_h, clip_x, clip_y, clip_w, clip_h,
                  px, py - font_lh, tw > w ? w : tw, lh, lines[i], id, size, color, ALIGN_LEFT, VALIGN_TOP);
    }
}

void font_draw(FontCtx *f, uint32_t *dst, int dst_w, int dst_h,
               int clip_x, int clip_y, int clip_w, int clip_h,
               int x, int y, int w, int h, const char *utf8,
               FontId id, int size, uint32_t color, HAlign align, VAlign valign)
{
    if (!f || !dst || !utf8 || !*utf8) return;

    int lh = font_line_height(f, id, size);
    int px = x;
    if (align != ALIGN_LEFT) {                         /* the width is only needed to place it */
        int tw = font_text_width(f, id, size, utf8);
        if (align == ALIGN_CENTER) px = x + (w - tw) / 2;
        else if (align == ALIGN_RIGHT) px = x + w - tw;
    }
    int py = y + lh;                                   /* the baseline */
    if (valign == VALIGN_MIDDLE) py = y + (h - lh) / 2 + lh;
    else if (valign == VALIGN_BOTTOM) py = y + h;

    uint32_t a = (color >> 24) & 0xff;
    uint32_t cr = (color >> 16) & 0xff, cg = (color >> 8) & 0xff, cb = color & 0xff;
    if (a == 0) a = 0xff;                              /* 0x00RRGGBB means "opaque" here */

    /* The clip is in screen units and the buffer is the panel's pixels, but they are the same grid on the
     * RG DS (640x480) and the Plus (1024x768 with a 1.6 scale applied by the caller's buffer size). */
    int cx0 = clip_x, cy0 = clip_y, cx1 = clip_x + clip_w, cy1 = clip_y + clip_h;

    const char *s = utf8;
    for (int cp; (cp = utf8_next(&s)) >= 0; ) {
        Glyph *g = glyph_get(f, id, size, (uint32_t)cp);
        if (!g) continue;
        int gx = px + g->bx, gy = py - g->by;
        px += g->adv;
        if (!g->bm) continue;

        /* The visible part of the glyph, clamped once: the old loop tested the clip and the buffer bounds on
         * every one of the glyph's pixels. */
        int x0 = gx < cx0 ? cx0 : gx, x1 = gx + g->w > cx1 ? cx1 : gx + g->w;
        int y0 = gy < cy0 ? cy0 : gy, y1 = gy + g->h > cy1 ? cy1 : gy + g->h;
        if (x0 < 0) x0 = 0;
        if (x1 > dst_w) x1 = dst_w;
        if (y0 < 0) y0 = 0;
        if (y1 > dst_h) y1 = dst_h;
        if (x0 >= x1 || y0 >= y1) continue;

        for (int dy = y0; dy < y1; dy++) {
            const uint8_t *brow = g->bm + (size_t)(dy - gy) * g->w;
            uint32_t *drow = dst + (size_t)dy * dst_w;
            for (int dx = x0; dx < x1; dx++) {
                uint32_t cov = brow[dx - gx];
                if (!cov) continue;
                uint32_t alpha = (cov * a) / 255;
                if (alpha == 255) { drow[dx] = 0xff000000u | (cr << 16) | (cg << 8) | cb; continue; }
                uint32_t o = drow[dx];
                uint32_t rr = (cr * alpha + ((o >> 16) & 0xff) * (255 - alpha)) / 255;
                uint32_t gg = (cg * alpha + ((o >> 8) & 0xff) * (255 - alpha)) / 255;
                uint32_t bb = (cb * alpha + (o & 0xff) * (255 - alpha)) / 255;
                drow[dx] = 0xff000000u | (rr << 16) | (gg << 8) | bb;
            }
        }
    }
}
