/* include/ghal_font_ttf.h — TrueType font rendering service
 *
 * Wraps stb_truetype + stb_rect_pack to provide a simple "load font, draw
 * text into GHalSurface" API.  The baked atlas is a GHalSurface (A8 or
 * RGBA8) so it can be uploaded to a GHalSurface and composited by draw2d.
 *
 * Usage:
 *   GhalFontTTF *f = ghal_font_ttf_load("vera.ttf", 18.0f);
 *   ghal_font_ttf_draw(surf, f, 10, 10, "Hello!", 0xFFFFFFFF);
 *   ghal_font_ttf_free(f);
 */

#ifndef GHAL_FONT_TTF_H
#define GHAL_FONT_TTF_H
#pragma once

#ifndef NEXS_HAL_H
#include "nexs_hal.h"
#endif
#ifndef NEXS_API
#define NEXS_API
#endif

#include "ghal.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Atlas size — enough for printable ASCII at up to ~48 pt */
#define GHAL_TTF_ATLAS_W 512
#define GHAL_TTF_ATLAS_H 512
#define GHAL_TTF_FIRST_CHAR 32
#define GHAL_TTF_NUM_CHARS  96   /* 0x20–0x7F */

/* Opaque font handle */
typedef struct GhalFontTTF GhalFontTTF;

/* ── Lifecycle ────────────────────────────────────────────────── */

/* Load a TTF/OTF from disk and bake an atlas at the given pixel height.
 * Returns NULL on failure. */
NEXS_API GhalFontTTF *ghal_font_ttf_load(const char *path, float size_px);

/* Load from an in-memory buffer (e.g. embedded font). */
NEXS_API GhalFontTTF *ghal_font_ttf_load_mem(const void *ttf_buf,
                                              int ttf_len, float size_px);

/* Free all resources. */
NEXS_API void ghal_font_ttf_free(GhalFontTTF *f);

/* ── Metrics ──────────────────────────────────────────────────── */

/* Pixel width of a string (for layout). */
NEXS_API int ghal_font_ttf_measure(const GhalFontTTF *f, const char *str);

/* Ascent (pixels above baseline). */
NEXS_API int ghal_font_ttf_ascent(const GhalFontTTF *f);

/* Line height (ascent + descent + line gap). */
NEXS_API int ghal_font_ttf_line_height(const GhalFontTTF *f);

/* ── Rendering ────────────────────────────────────────────────── */

/* Draw a UTF-8 string into a BGRA8 surface at (x, y).
 * color is GHAL_RGBA(r, g, b, a) — alpha is respected. */
NEXS_API void ghal_font_ttf_draw(GHalSurface *dst, const GhalFontTTF *f,
                                  int x, int y, const char *str,
                                  uint32_t color);

/* Draw with word-wrap at max_width pixels.  Returns final y position. */
NEXS_API int  ghal_font_ttf_draw_wrapped(GHalSurface *dst,
                                          const GhalFontTTF *f,
                                          int x, int y, int max_width,
                                          const char *str, uint32_t color);

/* ── Atlas access (advanced / GPU upload) ─────────────────────── */

/* Read-only view of the baked alpha atlas (8-bit greyscale).
 * width = height = GHAL_TTF_ATLAS_W/H */
NEXS_API const uint8_t *ghal_font_ttf_atlas(const GhalFontTTF *f,
                                             int *out_w, int *out_h);

#ifdef __cplusplus
}
#endif
#endif /* GHAL_FONT_TTF_H */
