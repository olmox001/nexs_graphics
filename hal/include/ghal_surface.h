/* include/ghal_surface.h — Surface format and helper definitions */

#ifndef GHAL_SURFACE_H
#define GHAL_SURFACE_H
#pragma once

#include "ghal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bytes per pixel for a given format */
static inline uint32_t ghal_fmt_bpp(uint32_t fmt) {
    switch (fmt) {
    case GHAL_FMT_RGB565: return 2;
    case GHAL_FMT_RGBA8:
    case GHAL_FMT_BGRA8:
    case GHAL_FMT_XRGB8:
    default:              return 4;
    }
}

/* Minimum stride for width+format (no extra padding) */
static inline uint32_t ghal_fmt_stride(uint32_t w, uint32_t fmt) {
    return w * ghal_fmt_bpp(fmt);
}

/* Total pixel buffer size */
static inline size_t ghal_surface_byte_size(uint32_t w, uint32_t h,
                                             uint32_t fmt) {
    return (size_t)ghal_fmt_stride(w, fmt) * h;
}

/* Write a pixel at (x,y) — format must be RGBA8 or BGRA8 */
static inline void ghal_surface_put_pixel(GHalSurface *s,
                                           uint32_t x, uint32_t y,
                                           uint32_t color) {
    if (!s->pixels || x >= s->width || y >= s->height) return;
    uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + y * s->stride);
    row[x] = color;
}

/* Read a pixel at (x,y) */
static inline uint32_t ghal_surface_get_pixel(const GHalSurface *s,
                                               uint32_t x, uint32_t y) {
    if (!s->pixels || x >= s->width || y >= s->height) return 0;
    const uint32_t *row =
        (const uint32_t *)((const uint8_t *)s->pixels + y * s->stride);
    return row[x];
}

#ifdef __cplusplus
}
#endif

#endif /* GHAL_SURFACE_H */
