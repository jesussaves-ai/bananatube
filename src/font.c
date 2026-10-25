/* font.c -- FreeType with a small glyph cache. */
#include "font.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#define GLYPH_CACHE 1024

typedef struct {
    FontId   id;
    int      size;
    uint32_t cp;
    int      w, h, bx, by, adv;
    uint8_t *bm;          /* w*h coverage, 8 bits a pixel */
} Glyph;

struct FontCtx {
    FT_Library lib;
    FT_Face    face[3];
    Glyph      glyph[GLYPH_CACHE];
    int        next;      /* round-robin replacement */
};

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

    for (int i = 0; i < GLYPH_CACHE; i++) {
        Glyph *g = &f->glyph[i];
        if (g->bm && g->id == id && g->size == size && g->cp == cp) return g;
    }

    Glyph *g = &f->glyph[f->next];
    f->next = (f->next + 1) % GLYPH_CACHE;

    if (FT_Set_Pixel_Sizes(f->face[id], 0, (FT_UInt)size)) return NULL;
    if (FT_Load_Char(f->face[id], cp, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL)) return NULL;

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
    return g;
}

int font_line_height(FontCtx *f, FontId id, int size)
{
    if (!f || !f->face[id]) return size;
    FT_Set_Pixel_Sizes(f->face[id], 0, (FT_UInt)(size < 1 ? 1 : size));
    return (int)(f->face[id]->size->metrics.height >> 6);
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

void font_draw(FontCtx *f, uint32_t *dst, int dst_w, int dst_h,
               int clip_x, int clip_y, int clip_w, int clip_h,
               int x, int y, int w, int h, const char *utf8,
               FontId id, int size, uint32_t color, HAlign align, VAlign valign)
{
    if (!f || !dst || !utf8 || !*utf8) return;

    int tw = font_text_width(f, id, size, utf8);
    int lh = font_line_height(f, id, size);
    int px = x;
    if (align == ALIGN_CENTER) px = x + (w - tw) / 2;
    else if (align == ALIGN_RIGHT) px = x + w - tw;
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

        for (int yy = 0; yy < g->h; yy++) {
            int dy = gy + yy;
            if (dy < cy0 || dy >= cy1 || dy < 0 || dy >= dst_h) continue;
            for (int xx = 0; xx < g->w; xx++) {
                int dx = gx + xx;
                if (dx < cx0 || dx >= cx1 || dx < 0 || dx >= dst_w) continue;
                uint32_t cov = g->bm[(size_t)yy * g->w + xx];
                if (!cov) continue;
                uint32_t alpha = (cov * a) / 255;
                uint32_t *p = &dst[(size_t)dy * dst_w + dx];
                uint32_t o = *p;
                uint32_t orr = (o >> 16) & 0xff, og = (o >> 8) & 0xff, ob = o & 0xff;
                uint32_t rr = (cr * alpha + orr * (255 - alpha)) / 255;
                uint32_t gg = (cg * alpha + og * (255 - alpha)) / 255;
                uint32_t bb = (cb * alpha + ob * (255 - alpha)) / 255;
                *p = 0xff000000u | (rr << 16) | (gg << 8) | bb;
            }
        }
    }
}
