/* test/test_draw2d.c — Unit tests for lib/draw2d.c + lib/font.c
 *
 * Pure software test — no display, no platform deps.
 * Compile: cc test_draw2d.c lib/draw2d.c lib/font.c -I include -I vendor/include -o test_draw2d
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Provide minimal NEXS_API stub before including GHAL headers */
#define NEXS_API

/* Stub out the nexs_hal.h include chain for standalone compilation */
#define NEXS_HAL_H
#define HAL_INTERNAL_H
#define NEXS_MMU_H

/* Now include ghal types directly */
typedef struct GHalSurface {
    uint32_t    id;
    uint32_t    width, height;
    uint32_t    stride;
    uint32_t    format;
    void       *pixels;
    size_t      pixel_size;
    uintptr_t   gpu_handle;
    bool        gpu_dirty;
} GHalSurface;

typedef struct GHalWindow {
    uint32_t id;
    char     title[128];
    uint32_t width, height;
    GHalSurface *surface;
    bool     visible;
    uint32_t zorder;
    char     reg_path[64];
    uintptr_t native_handle;
} GHalWindow;

#define GHAL_FMT_RGBA8 0
#define GHAL_FMT_BGRA8 1
#define GHAL_RGBA(r,g,b,a) \
    (((uint32_t)(r)<<24)|((uint32_t)(g)<<16)|((uint32_t)(b)<<8)|(a))

/* Forward declarations for draw2d functions */
void draw2d_clear(GHalSurface *s, uint32_t color);
void draw2d_fill_rect(GHalSurface *s, int32_t x, int32_t y,
                      int32_t w, int32_t h, uint32_t color);
void draw2d_line(GHalSurface *s, int32_t x0, int32_t y0,
                 int32_t x1, int32_t y1, uint32_t color);
void draw2d_blit(GHalSurface *dst, const GHalSurface *src,
                 int32_t dx, int32_t dy);
void draw2d_text(GHalSurface *s, int32_t x, int32_t y,
                 const char *str, uint32_t color);
void draw2d_set_clip(GHalSurface *s, int32_t x, int32_t y,
                     int32_t w, int32_t h);
void draw2d_reset_clip(GHalSurface *s);

/* ── Test helpers ─────────────────────────────────────────────── */
static int g_pass = 0, g_fail = 0;

#define EXPECT_EQ(a, b, name) do { \
    uint32_t _a = (uint32_t)(a), _b = (uint32_t)(b); \
    if (_a == _b) { printf("  PASS: %s\n", name); g_pass++; } \
    else { printf("  FAIL: %s  expected=0x%08X got=0x%08X\n", \
                  name, _b, _a); g_fail++; } \
} while(0)

static GHalSurface make_surf(uint32_t w, uint32_t h) {
    GHalSurface s;
    memset(&s, 0, sizeof(s));
    s.width      = w;
    s.height     = h;
    s.stride     = w * 4;
    s.pixel_size = (size_t)s.stride * h;
    s.pixels     = calloc(1, s.pixel_size);
    s.format     = GHAL_FMT_BGRA8;
    return s;
}

static void free_surf(GHalSurface *s) {
    free(s->pixels);
    s->pixels = NULL;
}

static uint32_t get_pixel(GHalSurface *s, uint32_t x, uint32_t y) {
    if (!s->pixels || x >= s->width || y >= s->height) return 0;
    const uint32_t *row =
        (const uint32_t *)((const uint8_t *)s->pixels + y * s->stride);
    return row[x];
}

/* ── Tests ───────────────────────────────────────────────────── */

static void test_clear(void) {
    printf("test_clear:\n");
    GHalSurface s = make_surf(64, 64);
    uint32_t color = GHAL_RGBA(0xFF, 0x00, 0x00, 0xFF);
    draw2d_clear(&s, color);
    EXPECT_EQ(get_pixel(&s,  0,  0), color, "top-left pixel");
    EXPECT_EQ(get_pixel(&s, 63, 63), color, "bottom-right pixel");
    EXPECT_EQ(get_pixel(&s, 32, 32), color, "center pixel");
    free_surf(&s);
}

static void test_fill_rect(void) {
    printf("test_fill_rect:\n");
    GHalSurface s   = make_surf(64, 64);
    uint32_t bg     = GHAL_RGBA(0x00, 0x00, 0x00, 0xFF);
    uint32_t fill   = GHAL_RGBA(0x00, 0xFF, 0x00, 0xFF);
    draw2d_clear(&s, bg);
    draw2d_fill_rect(&s, 10, 10, 20, 20, fill);
    EXPECT_EQ(get_pixel(&s, 10, 10), fill, "rect top-left");
    EXPECT_EQ(get_pixel(&s, 29, 29), fill, "rect bottom-right");
    EXPECT_EQ(get_pixel(&s, 20, 20), fill, "rect center");
    EXPECT_EQ(get_pixel(&s,  9, 10), bg,   "outside left");
    EXPECT_EQ(get_pixel(&s, 30, 10), bg,   "outside right");
    EXPECT_EQ(get_pixel(&s, 10,  9), bg,   "outside top");
    EXPECT_EQ(get_pixel(&s, 10, 30), bg,   "outside bottom");
    free_surf(&s);
}

static void test_clip(void) {
    printf("test_clip:\n");
    GHalSurface s = make_surf(64, 64);
    uint32_t bg   = GHAL_RGBA(0x11, 0x11, 0x11, 0xFF);
    uint32_t fill = GHAL_RGBA(0xFF, 0xFF, 0x00, 0xFF);
    draw2d_clear(&s, bg);
    draw2d_set_clip(&s, 16, 16, 32, 32);
    draw2d_fill_rect(&s, 0, 0, 64, 64, fill);
    draw2d_reset_clip(&s);
    EXPECT_EQ(get_pixel(&s, 16, 16), fill, "clip inside top-left");
    EXPECT_EQ(get_pixel(&s, 47, 47), fill, "clip inside bottom-right");
    EXPECT_EQ(get_pixel(&s,  0,  0), bg,   "clip outside top-left");
    EXPECT_EQ(get_pixel(&s, 63,  0), bg,   "clip outside top-right");
    free_surf(&s);
}

static void test_line(void) {
    printf("test_line:\n");
    GHalSurface s = make_surf(64, 64);
    uint32_t bg   = GHAL_RGBA(0x00, 0x00, 0x00, 0xFF);
    uint32_t lc   = GHAL_RGBA(0xFF, 0xFF, 0xFF, 0xFF);
    draw2d_clear(&s, bg);
    draw2d_line(&s, 0, 0, 63, 63, lc);
    EXPECT_EQ(get_pixel(&s,  0,  0), lc, "line start");
    EXPECT_EQ(get_pixel(&s, 63, 63), lc, "line end");
    EXPECT_EQ(get_pixel(&s, 32, 32), lc, "line midpoint");
    free_surf(&s);
}

static void test_blit(void) {
    printf("test_blit:\n");
    GHalSurface src = make_surf(16, 16);
    GHalSurface dst = make_surf(64, 64);
    uint32_t bg     = GHAL_RGBA(0x00, 0x00, 0x00, 0xFF);
    uint32_t srcf   = GHAL_RGBA(0xAA, 0xBB, 0xCC, 0xFF);
    draw2d_clear(&dst, bg);
    draw2d_clear(&src, srcf);
    draw2d_blit(&dst, &src, 8, 8);
    EXPECT_EQ(get_pixel(&dst,  8,  8), srcf, "blit top-left");
    EXPECT_EQ(get_pixel(&dst, 23, 23), srcf, "blit bottom-right");
    EXPECT_EQ(get_pixel(&dst,  7,  8), bg,   "before blit left");
    EXPECT_EQ(get_pixel(&dst, 24,  8), bg,   "after blit right");
    free_surf(&src);
    free_surf(&dst);
}

static void test_oob_safety(void) {
    printf("test_oob_safety (no crash):\n");
    GHalSurface s = make_surf(32, 32);
    uint32_t fill = GHAL_RGBA(0xFF, 0xFF, 0xFF, 0xFF);
    draw2d_fill_rect(&s, -10, -10, 100, 100, fill);
    draw2d_line(&s, -5, -5, 100, 100, 0xFF0000FF);
    EXPECT_EQ(get_pixel(&s, 31,  0), fill, "top-right not on diagonal");
    EXPECT_EQ(get_pixel(&s,  0, 31), fill, "bottom-left not on diagonal");
    free_surf(&s);
}

int main(void) {
    printf("=== GHAL draw2d unit tests ===\n");
    test_clear();
    test_fill_rect();
    test_clip();
    test_line();
    test_blit();
    test_oob_safety();
    printf("\nResult: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail > 0) ? 1 : 0;
}
