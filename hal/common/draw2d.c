/* lib/draw2d.c — Software 2D renderer (platform-independent)
 *
 * All drawing operates on a GHalSurface with CPU-accessible pixels.
 * No platform dependencies; testable entirely in hosted mode.
 */

#include "ghal.h"
#include "ghal_2d.h"
#include "ghal_surface.h"
#include "font_data.h"

#include <string.h>
#include <stdlib.h>

/* ── Clip state (per surface; simplified: one global clip) ─────
 * A full implementation would embed clip in GHalSurface.
 * For testability, use a static context here.              */

typedef struct {
    int32_t x, y, w, h;
    int     active;
} ClipRect;

static ClipRect s_clip = {0};

void draw2d_set_clip(GHalSurface *s, int32_t x, int32_t y,
                     int32_t w, int32_t h) {
    (void)s;
    s_clip.x = x; s_clip.y = y;
    s_clip.w = w; s_clip.h = h;
    s_clip.active = 1;
}

void draw2d_reset_clip(GHalSurface *s) {
    (void)s;
    s_clip.active = 0;
}

/* ── Internal: clamp rect to surface bounds (and clip) ─────── */
static int clip_rect(const GHalSurface *s,
                     int32_t *x, int32_t *y, int32_t *w, int32_t *h) {
    int32_t x0 = *x, y0 = *y, x1 = x0 + *w, y1 = y0 + *h;

    /* Clip to surface */
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int32_t)s->width)  x1 = (int32_t)s->width;
    if (y1 > (int32_t)s->height) y1 = (int32_t)s->height;

    /* Clip to active clip rect */
    if (s_clip.active) {
        if (x0 < s_clip.x) x0 = s_clip.x;
        if (y0 < s_clip.y) y0 = s_clip.y;
        if (x1 > s_clip.x + s_clip.w) x1 = s_clip.x + s_clip.w;
        if (y1 > s_clip.y + s_clip.h) y1 = s_clip.y + s_clip.h;
    }

    *x = x0; *y = y0; *w = x1 - x0; *h = y1 - y0;
    return (*w > 0 && *h > 0) ? 1 : 0;
}

/* ── draw2d_clear ────────────────────────────────────────────── */

void draw2d_clear(GHalSurface *s, uint32_t color) {
    if (!s || !s->pixels) return;
    uint32_t *px = (uint32_t *)s->pixels;
    size_t    n  = (size_t)s->width * s->height;
    for (size_t i = 0; i < n; i++) px[i] = color;
}

/* ── draw2d_fill_rect ────────────────────────────────────────── */

void draw2d_fill_rect(GHalSurface *s, int32_t x, int32_t y,
                      int32_t w, int32_t h, uint32_t color) {
    if (!s || !s->pixels) return;
    if (!clip_rect(s, &x, &y, &w, &h)) return;

    for (int32_t row = y; row < y + h; row++) {
        uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + row * s->stride);
        for (int32_t col = x; col < x + w; col++)
            line[col] = color;
    }
}

/* ── draw2d_line — Bresenham ─────────────────────────────────── */

void draw2d_line(GHalSurface *s, int32_t x0, int32_t y0,
                 int32_t x1, int32_t y1, uint32_t color) {
    if (!s || !s->pixels) return;

    int32_t dx = abs(x1 - x0), dy = abs(y1 - y0);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx - dy;

    while (1) {
        if (x0 >= 0 && y0 >= 0 &&
            (uint32_t)x0 < s->width && (uint32_t)y0 < s->height) {
            uint32_t *line =
                (uint32_t *)((uint8_t *)s->pixels + y0 * s->stride);
            line[x0] = color;
        }
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

/* ── draw2d_blit — simple copy (no alpha) ───────────────────── */

void draw2d_blit(GHalSurface *dst, const GHalSurface *src,
                 int32_t dx, int32_t dy) {
    if (!dst || !src || !dst->pixels || !src->pixels) return;

    int32_t sw = (int32_t)src->width;
    int32_t sh = (int32_t)src->height;

    for (int32_t sy = 0; sy < sh; sy++) {
        int32_t dy2 = dy + sy;
        if (dy2 < 0 || dy2 >= (int32_t)dst->height) continue;
        const uint32_t *srow =
            (const uint32_t *)((const uint8_t *)src->pixels + sy * src->stride);
        uint32_t       *drow =
            (uint32_t *)((uint8_t *)dst->pixels + dy2 * dst->stride);

        for (int32_t sx2 = 0; sx2 < sw; sx2++) {
            int32_t dx2 = dx + sx2;
            if (dx2 < 0 || dx2 >= (int32_t)dst->width) continue;
            drow[dx2] = srow[sx2];
        }
    }
}

/* ── draw2d_blit_alpha — alpha compositing (Porter-Duff over) ── */

void draw2d_blit_alpha(GHalSurface *dst, const GHalSurface *src,
                       int32_t dx, int32_t dy) {
    if (!dst || !src || !dst->pixels || !src->pixels) return;

    int32_t sw = (int32_t)src->width;
    int32_t sh = (int32_t)src->height;

    for (int32_t sy = 0; sy < sh; sy++) {
        int32_t dy2 = dy + sy;
        if (dy2 < 0 || dy2 >= (int32_t)dst->height) continue;
        const uint32_t *srow =
            (const uint32_t *)((const uint8_t *)src->pixels + sy * src->stride);
        uint32_t       *drow =
            (uint32_t *)((uint8_t *)dst->pixels + dy2 * dst->stride);

        for (int32_t sx2 = 0; sx2 < sw; sx2++) {
            int32_t dx2 = dx + sx2;
            if (dx2 < 0 || dx2 >= (int32_t)dst->width) continue;

            uint32_t sc = srow[sx2];
            uint32_t a  = sc & 0xFF;  /* RGBA8: alpha in low byte */
            if (a == 0xFF) {
                drow[dx2] = sc;
            } else if (a > 0) {
                uint32_t dc = drow[dx2];
                uint32_t sr = (sc >> 24) & 0xFF;
                uint32_t sg = (sc >> 16) & 0xFF;
                uint32_t sb = (sc >>  8) & 0xFF;
                uint32_t dr = (dc >> 24) & 0xFF;
                uint32_t dg = (dc >> 16) & 0xFF;
                uint32_t db = (dc >>  8) & 0xFF;
                uint32_t ia = 0xFF - a;
                uint32_t nr = (sr * a + dr * ia) / 0xFF;
                uint32_t ng = (sg * a + dg * ia) / 0xFF;
                uint32_t nb = (sb * a + db * ia) / 0xFF;
                drow[dx2] = (nr << 24) | (ng << 16) | (nb << 8) | 0xFF;
            }
        }
    }
}

/* ── draw2d_text — 8×16 bitmap font ─────────────────────────── */

extern const uint8_t g_font_8x16[256][16];  /* defined in font.c */

void draw2d_text(GHalSurface *s, int32_t x, int32_t y,
                 const char *str, uint32_t color) {
    if (!s || !s->pixels || !str) return;

    int32_t cx = x;
    for (const char *p = str; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '\n') { cx = x; y += 16; continue; }

        const uint8_t *glyph = g_font_8x16[ch];
        for (int row = 0; row < 16; row++) {
            int32_t py = y + row;
            if (py < 0 || py >= (int32_t)s->height) continue;
            uint32_t *line =
                (uint32_t *)((uint8_t *)s->pixels + py * s->stride);
            for (int col = 0; col < 8; col++) {
                int32_t px2 = cx + col;
                if (px2 < 0 || px2 >= (int32_t)s->width) continue;
                if (glyph[row] & (0x80 >> col))
                    line[px2] = color;
            }
        }
        cx += 8;
    }
}
