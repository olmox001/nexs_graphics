/* host/ghal_host.c — Platform dispatcher + public GHAL API
 *
 * Auto-detects backend from environment/compile-time defines.
 * On macOS: g_ghal_driver is set by ghal_macos.m constructor.
 * On Linux: g_ghal_driver is set by ghal_x11.c constructor.
 *
 * This file implements the thin public API wrappers that delegate
 * to g_ghal_driver function pointers.
 */

#include "../include/ghal.h"
#include "../include/ghal_compositor.h"

#include "nexs_registry.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Singleton — set by platform backend constructor */
GHalDriver *g_ghal_driver = NULL;

#ifdef __APPLE__
extern GHalDriver s_macos_driver;
#elif !defined(NEXS_BAREMETAL)
extern GHalDriver s_x11_driver;
#endif

/* Shared compositor state (hosted: single process) */
static GCompositor s_compositor;
static int         s_compositor_ready = 0;

/* ── ghal_init ──────────────────────────────────────────────── */

int ghal_init(void) {
    if (!g_ghal_driver) {
#ifdef __APPLE__
        g_ghal_driver = &s_macos_driver;
#elif !defined(NEXS_BAREMETAL)
        g_ghal_driver = &s_x11_driver;
#endif
    }

    if (!g_ghal_driver) {
        fprintf(stderr, "GHAL: no driver registered\n");
        return -1;
    }

    /* Initialize backend */
    if (g_ghal_driver->init) {
        int rc = g_ghal_driver->init();
        if (rc != 0) return rc;
    }

    /* Publish /dev/gfx/ entries */
    uint32_t dw = 0, dh = 0;
    if (g_ghal_driver->get_display_size)
        g_ghal_driver->get_display_size(&dw, &dh);

    reg_set("/dev/gfx/backend",   val_str(g_ghal_driver->name), RK_READ);
    reg_set("/dev/gfx/width",     val_int((int64_t)dw),         RK_READ);
    reg_set("/dev/gfx/height",    val_int((int64_t)dh),         RK_READ);
    reg_set("/dev/gfx/vsync_hz",  val_int((int64_t)(g_ghal_driver->vsync_hz
                                           ? g_ghal_driver->vsync_hz : 60)), RK_READ);
    reg_set("/dev/gfx/format",    val_str("BGRA8"),              RK_READ);
    reg_set("/dev/gfx/gpu_vendor",val_str("unknown"),            RK_READ);

    /* Initialize compositor */
    gcomp_init(&s_compositor);
    s_compositor_ready = 1;

    return 0;
}

void ghal_shutdown(void) {
    if (s_compositor_ready) {
        gcomp_shutdown(&s_compositor);
        s_compositor_ready = 0;
    }
    if (g_ghal_driver && g_ghal_driver->shutdown)
        g_ghal_driver->shutdown();
}

void ghal_get_display_size(uint32_t *w, uint32_t *h) {
    if (g_ghal_driver && g_ghal_driver->get_display_size)
        g_ghal_driver->get_display_size(w, h);
    else { if (w) *w = 0; if (h) *h = 0; }
}

/* ── Window API ──────────────────────────────────────────────── */

int ghal_win_open(GHalWindow *w, const char *title,
                  uint32_t width, uint32_t height) {
    if (!w) return -1;
    memset(w, 0, sizeof(*w));
    strncpy(w->title, title, sizeof(w->title) - 1);
    w->width  = width;
    w->height = height;
    w->visible = true;

    if (!s_compositor_ready) return -1;
    return gcomp_win_register(&s_compositor, w);
}

void ghal_win_close(GHalWindow *w) {
    if (!w || !s_compositor_ready) return;
    gcomp_win_unregister(&s_compositor, w->id);
}

void ghal_win_set_title(GHalWindow *w, const char *title) {
    if (!w || !title) return;
    strncpy(w->title, title, sizeof(w->title) - 1);
    if (g_ghal_driver && g_ghal_driver->win_set_title)
        g_ghal_driver->win_set_title(w, title);
    gcomp_update_registry(w);
}

void ghal_win_resize(GHalWindow *w, uint32_t width, uint32_t height) {
    if (!w) return;
    w->width  = width;
    w->height = height;
    if (g_ghal_driver && g_ghal_driver->win_resize)
        g_ghal_driver->win_resize(w, width, height);
    gcomp_update_registry(w);
}

/* ── Surface API ─────────────────────────────────────────────── */

int ghal_surface_create(GHalSurface *s, uint32_t w, uint32_t h,
                         uint32_t fmt) {
    if (!s || !g_ghal_driver) return -1;
    memset(s, 0, sizeof(*s));
    s->width  = w;
    s->height = h;
    s->format = fmt;
    if (g_ghal_driver->surface_create)
        return g_ghal_driver->surface_create(s, w, h, fmt);
    /* Software fallback: malloc pixels */
    s->stride     = w * 4;
    s->pixel_size = (size_t)s->stride * h;
    s->pixels     = malloc(s->pixel_size);
    return s->pixels ? 0 : -1;
}

void ghal_surface_destroy(GHalSurface *s) {
    if (!s) return;
    if (g_ghal_driver && g_ghal_driver->surface_destroy) {
        g_ghal_driver->surface_destroy(s);
    } else {
        free(s->pixels);
        s->pixels = NULL;
    }
}

int ghal_surface_lock(GHalSurface *s) {
    if (!s) return -1;
    if (g_ghal_driver && g_ghal_driver->surface_lock)
        return g_ghal_driver->surface_lock(s);
    return 0;
}

void ghal_surface_unlock(GHalSurface *s) {
    if (!s) return;
    if (g_ghal_driver && g_ghal_driver->surface_unlock)
        g_ghal_driver->surface_unlock(s);
}

void ghal_surface_present(GHalWindow *w, GHalSurface *s) {
    if (!w || !s) return;
    if (g_ghal_driver && g_ghal_driver->surface_present)
        g_ghal_driver->surface_present(w, s);
    gcomp_mark_damage(&s_compositor, w->id);
}

/* ── GPU API ─────────────────────────────────────────────────── */

int ghal_gpu_begin_frame(GHalWindow *w) {
    if (!w || !g_ghal_driver || !g_ghal_driver->gpu_begin_frame) return 0;
    return g_ghal_driver->gpu_begin_frame(w);
}

int ghal_gpu_submit(const void *cmdbuf, size_t len) {
    if (!g_ghal_driver || !g_ghal_driver->gpu_submit) return 0;
    return g_ghal_driver->gpu_submit(cmdbuf, len);
}

int ghal_gpu_end_frame(GHalWindow *w) {
    if (!w || !g_ghal_driver || !g_ghal_driver->gpu_end_frame) return 0;
    return g_ghal_driver->gpu_end_frame(w);
}

uint32_t ghal_vsync(void) {
    if (s_compositor_ready) {
        gcomp_tick(&s_compositor);
        return s_compositor.last_vsync_us;
    }
    if (g_ghal_driver && g_ghal_driver->vsync)
        return g_ghal_driver->vsync();
    return 16667;  /* default 60 Hz */
}

void ghal_win_close_by_id(uint32_t id) {
    if (s_compositor_ready) {
        gcomp_win_unregister(&s_compositor, id);
    }
}

int ghal_has_active_windows(void) {
    return s_compositor_ready && s_compositor.count > 0;
}

