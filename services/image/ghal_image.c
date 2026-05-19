/* services/image/ghal_image.c — Image load/save/scale service
 *
 * Implements ghal_image.h using stb_image, stb_image_write,
 * stb_image_resize2.  All stb implementations are compiled here
 * (single-file library pattern).
 */

/* stb single-file implementations */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
/* Route stb_image file I/O through the GHAL VFS bridge */
#define STBI_NO_STDIO

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ASSERT(x)

#define STB_IMAGE_RESIZE_IMPLEMENTATION

#define NEXS_API
#ifndef NEXS_HAL_H
#define NEXS_HAL_H
#endif

#include "stb_image.h"
#include "stb_image_write.h"
#include "stb_image_resize2.h"

#include "../../include/ghal_image.h"
#include "../core/ghal_vfs.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Internal helpers ─────────────────────────────────────────── */

/* stb_image always loads as RGBA.  We want BGRA8 to match GHalSurface. */
static void rgba_to_bgra(uint8_t *px, int n_pixels) {
    for (int i = 0; i < n_pixels; i++, px += 4) {
        uint8_t r = px[0], b = px[2];
        px[0] = b;
        px[2] = r;
    }
}

static void bgra_to_rgba(const uint8_t *src, uint8_t *dst, int n_pixels) {
    for (int i = 0; i < n_pixels; i++, src += 4, dst += 4) {
        dst[0] = src[2];  /* R */
        dst[1] = src[1];  /* G */
        dst[2] = src[0];  /* B */
        dst[3] = src[3];  /* A */
    }
}

static int fill_surface(GHalSurface *s, uint8_t *rgba,
                         uint32_t w, uint32_t h) {
    size_t sz = (size_t)w * h * 4;
    s->pixels = malloc(sz);
    if (!s->pixels) { stbi_image_free(rgba); return -1; }

    memcpy(s->pixels, rgba, sz);
    rgba_to_bgra((uint8_t *)s->pixels, (int)(w * h));
    stbi_image_free(rgba);

    s->width      = w;
    s->height     = h;
    s->stride     = w * 4;
    s->pixel_size = sz;
    s->format     = 1;  /* GHAL_FMT_BGRA8 */
    s->gpu_dirty  = 0;
    s->gpu_handle = 0;
    return 0;
}

/* ── Load ────────────────────────────────────────────────────── */

static stbi_io_callbacks s_vfs_cb = {
    ghal_vfs_stbi_read,
    ghal_vfs_stbi_skip,
    ghal_vfs_stbi_eof,
};

int ghal_image_load(const char *path, GHalSurface *out) {
    if (!path || !out) return -1;
    GhalVfsFile *vf = ghal_vfs_open(path);
    if (!vf) return -1;
    int w, h, ch;
    uint8_t *px = stbi_load_from_callbacks(&s_vfs_cb, vf, &w, &h, &ch, 4);
    ghal_vfs_close(vf);
    if (!px) return -1;
    return fill_surface(out, px, (uint32_t)w, (uint32_t)h);
}

int ghal_image_load_mem(const void *buf, int len, GHalSurface *out) {
    if (!buf || len <= 0 || !out) return -1;
    int w, h, ch;
    uint8_t *px = stbi_load_from_memory(
        (const stbi_uc *)buf, len, &w, &h, &ch, 4);
    if (!px) return -1;
    return fill_surface(out, px, (uint32_t)w, (uint32_t)h);
}

/* ── Save ────────────────────────────────────────────────────── */

int ghal_image_save(const char *path, GhalImgFormat fmt,
                    const GHalSurface *s) {
    if (!path || !s || !s->pixels) return -1;

    int w = (int)s->width, h = (int)s->height;
    int n_pixels = w * h;

    /* Convert BGRA → RGBA for stb_image_write */
    uint8_t *rgba = malloc((size_t)n_pixels * 4);
    if (!rgba) return -1;
    bgra_to_rgba((const uint8_t *)s->pixels, rgba, n_pixels);

    int rc = 0;
    switch (fmt) {
    case GHAL_IMG_PNG: rc = stbi_write_png(path, w, h, 4, rgba, w*4); break;
    case GHAL_IMG_BMP: rc = stbi_write_bmp(path, w, h, 4, rgba);      break;
    case GHAL_IMG_TGA: rc = stbi_write_tga(path, w, h, 4, rgba);      break;
    case GHAL_IMG_JPG: rc = stbi_write_jpg(path, w, h, 4, rgba, 90);  break;
    default: rc = 0;
    }
    free(rgba);
    return (rc != 0) ? 0 : -1;
}

/* ── Scale ───────────────────────────────────────────────────── */

int ghal_surface_scale(const GHalSurface *src, GHalSurface *dst) {
    if (!src || !dst || !src->pixels || !dst->pixels) return -1;

    /* stb_image_resize2 works in RGBA — swap channels in/out */
    int sw = (int)src->width, sh = (int)src->height;
    int dw = (int)dst->width, dh = (int)dst->height;
    int n_src = sw * sh, n_dst = dw * dh;

    uint8_t *src_rgba = malloc((size_t)n_src * 4);
    uint8_t *dst_rgba = malloc((size_t)n_dst * 4);
    if (!src_rgba || !dst_rgba) { free(src_rgba); free(dst_rgba); return -1; }

    bgra_to_rgba((const uint8_t *)src->pixels, src_rgba, n_src);

    stbir_resize_uint8_linear(src_rgba, sw, sh, sw * 4,
                               dst_rgba, dw, dh, dw * 4,
                               STBIR_RGBA);

    /* Convert result back to BGRA */
    uint8_t *dp = (uint8_t *)dst->pixels;
    const uint8_t *rp = dst_rgba;
    for (int i = 0; i < n_dst; i++, dp += 4, rp += 4) {
        dp[0] = rp[2]; dp[1] = rp[1]; dp[2] = rp[0]; dp[3] = rp[3];
    }

    free(src_rgba);
    free(dst_rgba);
    return 0;
}

int ghal_surface_scale_new(const GHalSurface *src,
                            uint32_t w, uint32_t h,
                            GHalSurface *out) {
    if (!src || !out || w == 0 || h == 0) return -1;

    size_t sz = (size_t)w * h * 4;
    out->pixels     = malloc(sz);
    out->width      = w;
    out->height     = h;
    out->stride     = w * 4;
    out->pixel_size = sz;
    out->format     = 1;  /* BGRA8 */
    out->gpu_dirty  = 0;
    out->gpu_handle = 0;

    if (!out->pixels) return -1;
    return ghal_surface_scale(src, out);
}
