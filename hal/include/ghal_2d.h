/* include/ghal_2d.h — 2D drawing API (software, CPU-side) */

#ifndef GHAL_2D_H
#define GHAL_2D_H
#pragma once

#include "ghal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Clip rectangle applied to all draw ops */
void draw2d_set_clip(GHalSurface *s,
                     int32_t x, int32_t y, int32_t w, int32_t h);
void draw2d_reset_clip(GHalSurface *s);

/* Fill solid rectangle */
void draw2d_fill_rect(GHalSurface *s,
                      int32_t x, int32_t y, int32_t w, int32_t h,
                      uint32_t color);

/* Bresenham line */
void draw2d_line(GHalSurface *s,
                 int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                 uint32_t color);

/* Clear surface with solid color */
void draw2d_clear(GHalSurface *s, uint32_t color);

/* Blit src → dst at (dx, dy), no alpha blending */
void draw2d_blit(GHalSurface *dst, const GHalSurface *src,
                 int32_t dx, int32_t dy);

/* Alpha-blend src → dst */
void draw2d_blit_alpha(GHalSurface *dst, const GHalSurface *src,
                       int32_t dx, int32_t dy);

/* Render a NUL-terminated UTF-8 string using the built-in 8×16 font */
void draw2d_text(GHalSurface *s,
                 int32_t x, int32_t y,
                 const char *str, uint32_t color);

#ifdef __cplusplus
}
#endif

#endif /* GHAL_2D_H */
