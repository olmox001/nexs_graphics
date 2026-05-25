/* include/ghal_image.h — Image load/save/scale service
 *
 * Layered above GHalSurface: converts files ↔ surfaces using stb_image /
 * stb_image_write / stb_image_resize2.  No platform deps — pure C.
 *
 * All surfaces produced by ghal_image_load are BGRA8, malloc-backed, and
 * must be released with ghal_surface_destroy().
 */

#ifndef GHAL_IMAGE_H
#define GHAL_IMAGE_H
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

/* ── Load ────────────────────────────────────────────────────── */

/* Load any stb_image-supported format (PNG, JPEG, BMP, TGA, GIF…)
 * into a newly allocated BGRA8 GHalSurface.
 * Returns 0 on success, -1 on failure (file not found or unsupported). */
NEXS_API int ghal_image_load(const char *path, GHalSurface *out);

/* Same but from an in-memory buffer (e.g. resource embedded in binary). */
NEXS_API int ghal_image_load_mem(const void *buf, int len, GHalSurface *out);

/* ── Save ────────────────────────────────────────────────────── */

typedef enum {
    GHAL_IMG_PNG = 0,
    GHAL_IMG_BMP,
    GHAL_IMG_TGA,
    GHAL_IMG_JPG,   /* quality 90 */
} GhalImgFormat;

/* Save a GHalSurface to disk.  Surface must be BGRA8 or RGBA8. */
NEXS_API int ghal_image_save(const char *path, GhalImgFormat fmt,
                              const GHalSurface *s);

/* ── Scale ───────────────────────────────────────────────────── */

/* Resize src into dst (must be pre-allocated with correct dimensions).
 * Uses stb_image_resize2 RGBA channel ordering. */
NEXS_API int ghal_surface_scale(const GHalSurface *src, GHalSurface *dst);

/* Create a new surface scaled to (w, h) from src.
 * Caller must free with ghal_surface_destroy(). */
NEXS_API int ghal_surface_scale_new(const GHalSurface *src,
                                    uint32_t w, uint32_t h,
                                    GHalSurface *out);

#ifdef __cplusplus
}
#endif
#endif /* GHAL_IMAGE_H */
