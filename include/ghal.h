/* include/ghal.h — Graphics Hardware Abstraction Layer
 *
 * Unified graphics interface spanning Host (macOS/Linux), Baremetal,
 * and seL4 protection domains. Follows NEXS HAL patterns.
 *
 * Key structures:
 *   GHalSurface   — framebuffer/texture handle
 *   GHalWindow    — window handle with registry path
 *   GHalDriver    — platform driver singleton (registered at init)
 *   GHalInputEvent — unified input event
 *
 * Registry paths (/dev/win/, /dev/gfx/) expose graphics resources
 * for cross-process IPC access.
 */

#ifndef GHAL_H
#define GHAL_H
#pragma once

#include "base-nexs/hal/include/nexs_hal.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Pixel format constants ─────────────────────────────────── */
#define GHAL_FMT_RGBA8  0
#define GHAL_FMT_BGRA8  1
#define GHAL_FMT_RGB565 2
#define GHAL_FMT_XRGB8  3

#define GHAL_RGBA(r,g,b,a) \
    (((uint32_t)(r)<<24)|((uint32_t)(g)<<16)|((uint32_t)(b)<<8)|(a))

/* ── GHalSurface — framebuffer/texture handle ───────────────── */
typedef struct GHalSurface {
    uint32_t    id;
    uint32_t    width, height;
    uint32_t    stride;        /* bytes per row */
    uint32_t    format;        /* GHAL_FMT_* */
    void       *pixels;        /* CPU-accessible data; NULL if GPU-only */
    size_t      pixel_size;    /* total allocated bytes */
    uintptr_t   gpu_handle;    /* opaque GPU resource reference */
    bool        gpu_dirty;     /* CPU modified, GPU sync needed */
} GHalSurface;

/* ── GHalWindow — window handle ────────────────────────────── */
typedef struct GHalWindow {
    uint32_t      id;
    char          title[128];
    uint32_t      width, height;
    GHalSurface  *surface;
    bool          visible;
    uint32_t      zorder;
    char          reg_path[64];    /* /dev/win/<id> */
    uintptr_t     native_handle;   /* platform-specific handle */
} GHalWindow;

/* ── GHalInputEvent — unified input ─────────────────────────── */
#define GHAL_INPUT_KEY_DOWN   1
#define GHAL_INPUT_KEY_UP     2
#define GHAL_INPUT_MOUSE_MOVE 3
#define GHAL_INPUT_MOUSE_BTN  4

typedef struct {
    uint32_t type;       /* GHAL_INPUT_* */
    uint32_t win_id;     /* target window */
    union {
        struct { uint32_t keycode; uint32_t modifiers; } key;
        struct { int32_t x, y; uint32_t buttons; }      mouse;
    };
} GHalInputEvent;

/* ── GHalDriver — backend singleton interface ───────────────── */
typedef struct {
    const char *name;   /* "metal", "x11-egl", "virtio-gpu", "softpipe" */

    /* lifecycle */
    int  (*init)(void);
    void (*shutdown)(void);

    /* display query */
    void     (*get_display_size)(uint32_t *w, uint32_t *h);
    uint32_t (*preferred_format)(void);

    /* window management (NULL on baremetal) */
    int  (*win_open)(GHalWindow *w);
    void (*win_close)(GHalWindow *w);
    void (*win_set_title)(GHalWindow *w, const char *title);
    void (*win_resize)(GHalWindow *w, uint32_t nw, uint32_t nh);

    /* surface operations */
    int  (*surface_create)(GHalSurface *s, uint32_t w, uint32_t h, uint32_t fmt);
    void (*surface_destroy)(GHalSurface *s);
    int  (*surface_lock)(GHalSurface *s);
    void (*surface_unlock)(GHalSurface *s);
    void (*surface_present)(GHalWindow *w, GHalSurface *s);

    /* GPU command submission (NULL = software only) */
    int  (*gpu_begin_frame)(GHalWindow *w);
    int  (*gpu_submit)(const void *cmdbuf, size_t len);
    int  (*gpu_end_frame)(GHalWindow *w);

    /* VSYNC — returns microseconds until next vblank */
    uint32_t (*vsync)(void);
    uint32_t  vsync_hz;

    /* input polling (NULL = no input) */
    int  (*poll_input)(GHalInputEvent *event);

} GHalDriver;

/* Singleton registered by each platform via __attribute__((constructor)) */
extern GHalDriver *g_ghal_driver;

/* ── Public API ──────────────────────────────────────────────── */
NEXS_API int  ghal_init(void);
NEXS_API void ghal_shutdown(void);
NEXS_API void ghal_get_display_size(uint32_t *w, uint32_t *h);

NEXS_API int  ghal_win_open(GHalWindow *w, const char *title,
                             uint32_t width, uint32_t height);
NEXS_API void ghal_win_close(GHalWindow *w);
NEXS_API void ghal_win_set_title(GHalWindow *w, const char *title);
NEXS_API void ghal_win_resize(GHalWindow *w, uint32_t width, uint32_t height);

NEXS_API int  ghal_surface_create(GHalSurface *s, uint32_t w, uint32_t h,
                                   uint32_t fmt);
NEXS_API void ghal_surface_destroy(GHalSurface *s);
NEXS_API int  ghal_surface_lock(GHalSurface *s);
NEXS_API void ghal_surface_unlock(GHalSurface *s);
NEXS_API void ghal_surface_present(GHalWindow *w, GHalSurface *s);

NEXS_API int  ghal_gpu_begin_frame(GHalWindow *w);
NEXS_API int  ghal_gpu_submit(const void *cmdbuf, size_t len);
NEXS_API int  ghal_gpu_end_frame(GHalWindow *w);
NEXS_API uint32_t ghal_vsync(void);

#ifdef __cplusplus
}
#endif

#endif /* GHAL_H */
