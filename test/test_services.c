/* test/test_services.c — Unit tests for stb-backed GHAL services
 *
 * Tests: image load-from-memory, save, scale, font metrics + render.
 * No disk I/O required for the core tests (uses in-memory PNG).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Pull in service headers — they transitively include ghal.h with the
 * NEXS_HAL_H guard so no nexs runtime is needed. */
#define NEXS_API

#include "../include/ghal_image.h"
#include "../include/ghal_font_ttf.h"

/* ── Test helpers ─────────────────────────────────────────────── */
static int g_pass = 0, g_fail = 0;
#define EXPECT(cond, name) do { \
    if (cond) { printf("  PASS: %s\n", name); g_pass++; } \
    else       { printf("  FAIL: %s\n", name); g_fail++; } \
} while(0)

static GHalSurface make_surf(uint32_t w, uint32_t h) {
    GHalSurface s = {0};
    s.width      = w; s.height = h;
    s.stride     = w * 4;
    s.pixel_size = (size_t)w * h * 4;
    s.pixels     = calloc(1, s.pixel_size);
    s.format     = GHAL_FMT_BGRA8;
    return s;
}
static void free_surf(GHalSurface *s) { free(s->pixels); s->pixels = NULL; }

static uint32_t px(const GHalSurface *s, int x, int y) {
    const uint32_t *row = (const uint32_t *)
        ((const uint8_t *)s->pixels + y * s->stride);
    return row[x];
}

/* ── Tiny 1×1 red PNG (valid stb_image-parseable) ────────────── */
static const uint8_t k_red_1x1_png[] = {
    0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A, /* PNG sig */
    0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52, /* IHDR len=13 */
    0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01, /* 1×1 */
    0x08,0x02,0x00,0x00,0x00,0x90,0x77,0x53, /* 8-bit RGB, CRC */
    0xDE,0x00,0x00,0x00,0x0C,0x49,0x44,0x41, /* IDAT len=12 */
    0x54,0x08,0xD7,0x63,0xF8,0xCF,0xC0,0x00, /* zlib(filter0,R=FF,G=0,B=0) */
    0x00,0x00,0x02,0x00,0x01,0xE2,0x21,0xBC, /* CRC */
    0x33,0x00,0x00,0x00,0x00,0x49,0x45,0x4E, /* IEND */
    0x44,0xAE,0x42,0x60,0x82              /* IEND CRC */
};

/* ── Image service tests ─────────────────────────────────────── */

static void test_image_load_mem(void) {
    printf("test_image_load_mem:\n");
    GHalSurface s = {0};
    int rc = ghal_image_load_mem(k_red_1x1_png, sizeof(k_red_1x1_png), &s);
    EXPECT(rc == 0,       "load returns 0");
    EXPECT(s.width  == 1, "width == 1");
    EXPECT(s.height == 1, "height == 1");
    EXPECT(s.pixels != NULL, "pixels allocated");
    /* stb decodes as RGBA then we swap → BGRA.  Red = B=0,G=0,R=0xFF → */
    if (s.pixels) {
        uint8_t *p = (uint8_t *)s.pixels;
        /* BGRA: B=0x00, G=0x00, R=0xFF, A=0xFF (stb adds alpha=255) */
        EXPECT(p[2] > 0x80, "red channel dominant");
        EXPECT(p[3] == 0xFF, "alpha == FF");
    }
    free_surf(&s);
}

static void test_image_save_and_reload(void) {
    printf("test_image_save_and_reload:\n");
    /* Create a 4×4 surface with a known pattern */
    GHalSurface src = make_surf(4, 4);
    uint32_t *pp = (uint32_t *)src.pixels;
    /* Use 0xFFFFFFFF — all bytes 0xFF regardless of channel ordering */
    for (int i = 0; i < 16; i++)
        pp[i] = 0xFFFFFFFF;

    const char *tmp = "/tmp/ghal_test_save.png";
    int rc = ghal_image_save(tmp, GHAL_IMG_PNG, &src);
    EXPECT(rc == 0, "save PNG returns 0");

    if (rc == 0) {
        GHalSurface dst = {0};
        rc = ghal_image_load(tmp, &dst);
        EXPECT(rc == 0,          "reload returns 0");
        EXPECT(dst.width  == 4,  "reloaded width == 4");
        EXPECT(dst.height == 4,  "reloaded height == 4");
        if (dst.pixels) {
            uint8_t *p = (uint8_t *)dst.pixels;
            EXPECT(p[0] == 0xFF && p[1] == 0xFF &&
                   p[2] == 0xFF && p[3] == 0xFF,
                   "white pixel survives round-trip");
        }
        free_surf(&dst);
    }
    free_surf(&src);
}

static void test_surface_scale(void) {
    printf("test_surface_scale:\n");
    GHalSurface src = make_surf(4, 4);
    /* Fill with solid blue */
    uint32_t *pp = (uint32_t *)src.pixels;
    for (int i = 0; i < 16; i++)
        pp[i] = GHAL_RGBA(0x00, 0x00, 0xFF, 0xFF);

    GHalSurface dst = {0};
    int rc = ghal_surface_scale_new(&src, 8, 8, &dst);
    EXPECT(rc == 0,        "scale_new returns 0");
    EXPECT(dst.width  == 8, "scaled width == 8");
    EXPECT(dst.height == 8, "scaled height == 8");
    if (dst.pixels) {
        uint8_t *p = (uint8_t *)dst.pixels;
        /* Blue channel (BGRA[0]) should be dominant */
        EXPECT(p[0] > 0xC0, "blue dominant in scaled surface");
    }
    free_surf(&src);
    free_surf(&dst);
}

/* ── Font service tests ──────────────────────────────────────── */

static void test_font_no_ttf(void) {
    printf("test_font_no_ttf (NULL path):\n");
    GhalFontTTF *f = ghal_font_ttf_load(NULL, 16.0f);
    EXPECT(f == NULL, "NULL path returns NULL");

    f = ghal_font_ttf_load("/nonexistent/font.ttf", 16.0f);
    EXPECT(f == NULL, "missing file returns NULL");
}

static void test_font_load_mem_invalid(void) {
    printf("test_font_load_mem_invalid:\n");
    const uint8_t garbage[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    GhalFontTTF *f = ghal_font_ttf_load_mem(garbage, sizeof(garbage), 16.0f);
    EXPECT(f == NULL, "invalid TTF buffer returns NULL");
}

static void test_font_draw_no_crash(void) {
    printf("test_font_draw_no_crash (bitmap font fallback):\n");
    /* Without a TTF file we just verify the surface remains intact */
    GHalSurface s = make_surf(128, 32);
    uint32_t bg = GHAL_RGBA(0, 0, 0, 0xFF);
    uint32_t *pp = (uint32_t *)s.pixels;
    for (int i = 0; i < 128*32; i++) pp[i] = bg;

    /* Draw with NULL font — should be a no-op, not a crash */
    ghal_font_ttf_draw(&s, NULL, 4, 4, "Hello", 0xFFFFFFFF);
    EXPECT(px(&s, 0, 0) == bg, "surface intact after NULL font draw");

    /* Draw into NULL surface — should be a no-op */
    GhalFontTTF *f = ghal_font_ttf_load("/nonexistent.ttf", 14.0f);
    ghal_font_ttf_draw(NULL, f, 0, 0, "x", 0xFFFFFFFF);
    EXPECT(1, "NULL surface draw no crash");
    ghal_font_ttf_free(f);  /* f == NULL is fine */

    free_surf(&s);
}

static void test_image_api_null_safety(void) {
    printf("test_image_api_null_safety:\n");
    GHalSurface dummy = {0};
    EXPECT(ghal_image_load(NULL, &dummy) == -1, "load NULL path == -1");
    EXPECT(ghal_image_load("/nonexistent/x.png", &dummy) == -1,
           "load missing file == -1");
    EXPECT(ghal_image_save(NULL, GHAL_IMG_PNG, &dummy) == -1,
           "save NULL path == -1");
    EXPECT(ghal_surface_scale(NULL, NULL) == -1, "scale NULL == -1");
}

/* ── main ────────────────────────────────────────────────────── */

int main(void) {
    printf("=== GHAL services unit tests ===\n");
    test_image_load_mem();
    test_image_save_and_reload();
    test_surface_scale();
    test_font_no_ttf();
    test_font_load_mem_invalid();
    test_font_draw_no_crash();
    test_image_api_null_safety();
    printf("\nResult: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail > 0) ? 1 : 0;
}
