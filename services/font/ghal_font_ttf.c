/* services/font/ghal_font_ttf.c — TrueType font rendering service
 *
 * Implements ghal_font_ttf.h using stb_truetype + stb_rect_pack.
 * Bakes an 8-bit alpha atlas at load time; renders glyphs into
 * BGRA8 GHalSurfaces with per-pixel alpha blending.
 */

#define STB_RECT_PACK_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#define NEXS_API
#ifndef NEXS_HAL_H
#define NEXS_HAL_H
#endif

#include "stb_rect_pack.h"
#include "stb_truetype.h"

#include "../../include/ghal_font_ttf.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── GhalFontTTF internals ─────────────────────────────────────── */

struct GhalFontTTF {
    stbtt_fontinfo       info;
    stbtt_packedchar     chars[GHAL_TTF_NUM_CHARS];

    uint8_t             *atlas;     /* GHAL_TTF_ATLAS_W × GHAL_TTF_ATLAS_H, A8 */
    uint8_t             *ttf_buf;   /* malloc'd copy of the TTF file */

    float                scale;     /* stbtt scale factor for size_px */
    float                size_px;
    int                  ascent;
    int                  descent;
    int                  line_gap;
};

/* ── Lifecycle ────────────────────────────────────────────────── */

static GhalFontTTF *bake(uint8_t *ttf_buf, float size_px) {
    GhalFontTTF *f = calloc(1, sizeof(*f));
    if (!f) { free(ttf_buf); return NULL; }
    f->ttf_buf = ttf_buf;
    f->size_px = size_px;

    if (!stbtt_InitFont(&f->info, ttf_buf, 0)) {
        free(ttf_buf); free(f); return NULL;
    }

    /* Metrics */
    f->scale = stbtt_ScaleForPixelHeight(&f->info, size_px);
    int asc, desc, lgap;
    stbtt_GetFontVMetrics(&f->info, &asc, &desc, &lgap);
    f->ascent   = (int)(asc  * f->scale + 0.5f);
    f->descent  = (int)(desc * f->scale - 0.5f);
    f->line_gap = (int)(lgap * f->scale + 0.5f);

    /* Bake atlas */
    f->atlas = calloc(1, GHAL_TTF_ATLAS_W * GHAL_TTF_ATLAS_H);
    if (!f->atlas) { free(ttf_buf); free(f); return NULL; }

    stbtt_pack_context pc;
    stbtt_PackBegin(&pc, f->atlas,
                    GHAL_TTF_ATLAS_W, GHAL_TTF_ATLAS_H, 0, 1, NULL);
    stbtt_PackSetOversampling(&pc, 2, 2);
    stbtt_PackFontRange(&pc, ttf_buf, 0, size_px,
                        GHAL_TTF_FIRST_CHAR, GHAL_TTF_NUM_CHARS, f->chars);
    stbtt_PackEnd(&pc);
    return f;
}

GhalFontTTF *ghal_font_ttf_load(const char *path, float size_px) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(fp); return NULL; }
    fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    return bake(buf, size_px);
}

GhalFontTTF *ghal_font_ttf_load_mem(const void *ttf_buf, int ttf_len,
                                     float size_px) {
    uint8_t *buf = malloc((size_t)ttf_len);
    if (!buf) return NULL;
    memcpy(buf, ttf_buf, (size_t)ttf_len);
    return bake(buf, size_px);
}

void ghal_font_ttf_free(GhalFontTTF *f) {
    if (!f) return;
    free(f->atlas);
    free(f->ttf_buf);
    free(f);
}

/* ── Metrics ──────────────────────────────────────────────────── */

int ghal_font_ttf_ascent(const GhalFontTTF *f) {
    return f ? f->ascent : 0;
}

int ghal_font_ttf_line_height(const GhalFontTTF *f) {
    if (!f) return 0;
    return f->ascent - f->descent + f->line_gap;
}

int ghal_font_ttf_measure(const GhalFontTTF *f, const char *str) {
    if (!f || !str) return 0;
    float x = 0;
    for (const char *p = str; *p; p++) {
        int c = (unsigned char)*p - GHAL_TTF_FIRST_CHAR;
        if (c < 0 || c >= GHAL_TTF_NUM_CHARS) continue;
        x += f->chars[c].xadvance;
    }
    return (int)(x + 0.5f);
}

/* ── Rendering ────────────────────────────────────────────────── */

/* Porter-Duff over: blend glyph alpha with solid color onto BGRA8 dst */
static void blend_glyph(GHalSurface *dst,
                         const uint8_t *atlas,
                         int ax, int ay, int aw, int ah,
                         int dx, int dy,
                         uint32_t color) {
    uint8_t cr = (color >> 24) & 0xFF;
    uint8_t cg = (color >> 16) & 0xFF;
    uint8_t cb = (color >>  8) & 0xFF;
    uint8_t ca = (color      ) & 0xFF;

    for (int row = 0; row < ah; row++) {
        int py = dy + row;
        if (py < 0 || (uint32_t)py >= dst->height) continue;

        for (int col = 0; col < aw; col++) {
            int px = dx + col;
            if (px < 0 || (uint32_t)px >= dst->width) continue;

            uint8_t alpha = atlas[(ay + row) * GHAL_TTF_ATLAS_W + (ax + col)];
            if (alpha == 0) continue;

            /* Multiply glyph color alpha by font color alpha */
            uint32_t a = (uint32_t)alpha * ca / 255;
            uint32_t ia = 255 - a;

            uint8_t *d = (uint8_t *)dst->pixels
                         + py * dst->stride + px * 4;
            d[0] = (uint8_t)((cb * a + d[0] * ia) / 255);  /* B */
            d[1] = (uint8_t)((cg * a + d[1] * ia) / 255);  /* G */
            d[2] = (uint8_t)((cr * a + d[2] * ia) / 255);  /* R */
            d[3] = (uint8_t)(a + d[3] * ia / 255);          /* A */
        }
    }
}

void ghal_font_ttf_draw(GHalSurface *dst, const GhalFontTTF *f,
                         int x, int y, const char *str, uint32_t color) {
    if (!dst || !dst->pixels || !f || !str) return;

    float cx = (float)x;
    float cy = (float)(y + f->ascent);

    for (const char *p = str; *p; p++) {
        int c = (unsigned char)*p - GHAL_TTF_FIRST_CHAR;
        if (c < 0 || c >= GHAL_TTF_NUM_CHARS) { cx += f->size_px * 0.3f; continue; }

        const stbtt_packedchar *pc = &f->chars[c];

        int dx = (int)(cx + pc->xoff  + 0.5f);
        int dy = (int)(cy + pc->yoff  + 0.5f);
        int aw = pc->x1 - pc->x0;
        int ah = pc->y1 - pc->y0;

        blend_glyph(dst, f->atlas,
                    pc->x0, pc->y0, aw, ah,
                    dx, dy, color);

        cx += pc->xadvance;
    }
}

int ghal_font_ttf_draw_wrapped(GHalSurface *dst, const GhalFontTTF *f,
                                int x, int y, int max_width,
                                const char *str, uint32_t color) {
    if (!dst || !f || !str) return y;

    int lh   = ghal_font_ttf_line_height(f);
    int cx   = x;
    int cy   = y;
    const char *word_start = str;
    const char *p          = str;

    while (*p) {
        /* Find end of next word */
        const char *ws = p;
        while (*p && *p != ' ' && *p != '\n') p++;

        /* Measure word */
        char tmp[256];
        int wlen = (int)(p - ws);
        if (wlen >= (int)sizeof(tmp)) wlen = (int)sizeof(tmp) - 1;
        memcpy(tmp, ws, (size_t)wlen);
        tmp[wlen] = '\0';
        int ww = ghal_font_ttf_measure(f, tmp);

        if (cx != x && cx + ww > x + max_width) {
            cx  = x;
            cy += lh;
        }
        ghal_font_ttf_draw(dst, f, cx, cy, tmp, color);
        cx += ww;

        if (*p == '\n') { cx = x; cy += lh; p++; }
        else if (*p == ' ') { cx += (int)(f->size_px * 0.25f); p++; }
        (void)word_start;
    }
    return cy + lh;
}

/* ── Atlas access ────────────────────────────────────────────── */

const uint8_t *ghal_font_ttf_atlas(const GhalFontTTF *f, int *w, int *h) {
    if (!f) return NULL;
    if (w) *w = GHAL_TTF_ATLAS_W;
    if (h) *h = GHAL_TTF_ATLAS_H;
    return f->atlas;
}
