/* baremetal/ghal_linear_fb.c — Linear framebuffer fallback driver
 *
 * Used when VirtIO-GPU is not available.
 * Framebuffer address provided by bootloader (multiboot2/UEFI GOP).
 * Software rendering only; VSYNC via timer interrupt.
 */

#ifdef NEXS_BAREMETAL

#include "ghal.h"

#include <string.h>

/* ── Framebuffer descriptor (filled by bootloader probe) ────── */
typedef struct {
    void    *base;
    uint32_t width;
    uint32_t height;
    uint32_t stride;    /* bytes per row */
    uint32_t bpp;       /* bits per pixel */
} LinearFb;

static LinearFb g_lfb = {0};

/* Platform hook: call this from boot/multiboot2.c after VESA/GOP probe */
void ghal_linear_fb_set_info(void *base, uint32_t w, uint32_t h,
                              uint32_t stride, uint32_t bpp) {
    g_lfb.base   = base;
    g_lfb.width  = w;
    g_lfb.height = h;
    g_lfb.stride = stride;
    g_lfb.bpp    = bpp;
}

/* ── linear_fb_init ─────────────────────────────────────────── */

static int linear_fb_init(void) {
    if (!g_lfb.base) {
        /* Try common UEFI GOP base (bootloader may map at 0xFD000000) */
        extern volatile uint32_t *g_uefi_framebuffer_base;
        g_lfb.base   = (void *)g_uefi_framebuffer_base;
        g_lfb.width  = 1024;
        g_lfb.height = 768;
        g_lfb.stride = 1024 * 4;
        g_lfb.bpp    = 32;
    }
    return g_lfb.base ? 0 : -1;
}

static void linear_fb_shutdown(void) {}

static void linear_fb_get_display_size(uint32_t *w, uint32_t *h) {
    if (w) *w = g_lfb.width;
    if (h) *h = g_lfb.height;
}

static uint32_t linear_fb_preferred_format(void) {
    return GHAL_FMT_BGRA8;
}

/* ── Surface operations ──────────────────────────────────────── */

static int linear_fb_surface_create(GHalSurface *s, uint32_t w,
                                     uint32_t h, uint32_t fmt) {
    (void)fmt;
    s->pixels     = g_lfb.base;  /* write directly to VRAM */
    s->stride     = g_lfb.stride;
    s->pixel_size = g_lfb.stride * h;
    s->gpu_dirty  = 0;
    return 0;
}

static void linear_fb_surface_destroy(GHalSurface *s) {
    s->pixels = NULL;
}

static int linear_fb_surface_lock(GHalSurface *s) {
    (void)s;
    return 0;
}

static void linear_fb_surface_unlock(GHalSurface *s) {
    (void)s;
    /* On linear FB, writes are visible immediately via cache coherency */
}

static void linear_fb_surface_present(GHalWindow *w, GHalSurface *s) {
    (void)w;
    if (s->pixels == g_lfb.base) return;  /* already in VRAM */
    /* Otherwise, blit shadow buffer → VRAM */
    uint32_t rows = s->height < g_lfb.height ? s->height : g_lfb.height;
    for (uint32_t row = 0; row < rows; row++) {
        const uint8_t *src = (const uint8_t *)s->pixels + row * s->stride;
        uint8_t       *dst = (uint8_t *)g_lfb.base + row * g_lfb.stride;
        uint32_t       nbytes = s->stride < g_lfb.stride ?
                                s->stride : g_lfb.stride;
        memcpy(dst, src, nbytes);
    }
}

/* ── VSYNC — platform timer ──────────────────────────────────── */

static uint32_t linear_fb_vsync(void) {
    extern void nexs_hal_sleep_us(uint32_t us);
    nexs_hal_sleep_us(16667);  /* ~60 Hz */
    return 16667;
}

/* ── Driver registration ─────────────────────────────────────── */

static GHalDriver s_linear_driver = {
    .name             = "linear-fb",
    .init             = linear_fb_init,
    .shutdown         = linear_fb_shutdown,
    .get_display_size = linear_fb_get_display_size,
    .preferred_format = linear_fb_preferred_format,
    .win_open         = NULL,
    .win_close        = NULL,
    .win_set_title    = NULL,
    .win_resize       = NULL,
    .surface_create   = linear_fb_surface_create,
    .surface_destroy  = linear_fb_surface_destroy,
    .surface_lock     = linear_fb_surface_lock,
    .surface_unlock   = linear_fb_surface_unlock,
    .surface_present  = linear_fb_surface_present,
    .gpu_begin_frame  = NULL,
    .gpu_submit       = NULL,
    .gpu_end_frame    = NULL,
    .vsync            = linear_fb_vsync,
    .vsync_hz         = 60,
    .poll_input       = NULL,
};

/* Registered at lower priority than virtio_gpu (which is 150).
 * If VirtIO init fails, the system can fall back to this driver
 * by swapping g_ghal_driver at runtime. */
__attribute__((constructor(145)))
static void register_linear_fb_ghal(void) {
    g_ghal_driver = &s_linear_driver;  /* overridden by virtio at 150 */
}

#endif /* NEXS_BAREMETAL */
