/* host/linux_x11/ghal_x11.c — Linux X11 backend (xcb, dlopen)
 *
 * Zero static link-time dependencies on libxcb.
 * All symbols loaded via dlopen at runtime.
 * constructor priority 150 (same as macOS; only one is compiled per platform).
 */

#ifndef __APPLE__

#include "ghal.h"
#include "ghal_egl.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── xcb types (from xcb/xcb.h — copied minimally for dlopen) ── */
typedef uint32_t xcb_window_t;
typedef uint32_t xcb_colormap_t;
typedef uint32_t xcb_visualid_t;
typedef struct { int fd; } *xcb_connection_t;
typedef struct { uint8_t response_type; uint8_t pad[3]; uint32_t sequence;
                 uint32_t pad0[5]; } xcb_generic_event_t;

#define XCB_COPY_FROM_PARENT    0
#define XCB_WINDOW_CLASS_INPUT_OUTPUT 1
#define XCB_CW_BACK_PIXEL       2
#define XCB_CW_EVENT_MASK       2048
#define XCB_EVENT_MASK_KEY_PRESS   1
#define XCB_EVENT_MASK_KEY_RELEASE 2
#define XCB_EVENT_MASK_POINTER_MOTION 64
#define XCB_KEY_PRESS   2
#define XCB_KEY_RELEASE 3
#define XCB_MOTION_NOTIFY 6

/* ── dlopen function pointers ───────────────────────────────── */
static void *s_xcb_lib = NULL;

typedef xcb_connection_t *(*pfn_xcb_connect)(const char *displayname, int *screenp);
typedef void (*pfn_xcb_disconnect)(xcb_connection_t *c);
typedef int  (*pfn_xcb_connection_has_error)(xcb_connection_t *c);
typedef xcb_window_t (*pfn_xcb_generate_id)(xcb_connection_t *c);
typedef void (*pfn_xcb_create_window)(xcb_connection_t *, uint8_t, xcb_window_t,
             xcb_window_t, int16_t, int16_t, uint16_t, uint16_t, uint16_t,
             uint16_t, xcb_visualid_t, uint32_t, const uint32_t *);
typedef void (*pfn_xcb_map_window)(xcb_connection_t *, xcb_window_t);
typedef void (*pfn_xcb_destroy_window)(xcb_connection_t *, xcb_window_t);
typedef int  (*pfn_xcb_flush)(xcb_connection_t *);
typedef xcb_generic_event_t *(*pfn_xcb_poll_for_event)(xcb_connection_t *);

static pfn_xcb_connect             x_connect;
static pfn_xcb_disconnect          x_disconnect;
static pfn_xcb_connection_has_error x_has_error;
static pfn_xcb_generate_id         x_generate_id;
static pfn_xcb_create_window       x_create_window;
static pfn_xcb_map_window          x_map_window;
static pfn_xcb_destroy_window      x_destroy_window;
static pfn_xcb_flush               x_flush;
static pfn_xcb_poll_for_event      x_poll_event;

#define DLSYM(lib, name, type) \
    name = (type)dlsym(lib, #name); \
    if (!name) { fprintf(stderr, "GHAL/x11: missing symbol %s\n", #name); return -1; }

/* ── Connection state ───────────────────────────────────────── */
static xcb_connection_t *g_xcb = NULL;
static xcb_window_t      g_root = 0;

/* ── x11_init ────────────────────────────────────────────────── */

static int x11_init(void) {
    s_xcb_lib = dlopen("libxcb.so.1", RTLD_LAZY);
    if (!s_xcb_lib) s_xcb_lib = dlopen("libxcb.so", RTLD_LAZY);
    if (!s_xcb_lib) {
        fprintf(stderr, "GHAL/x11: cannot load libxcb: %s\n", dlerror());
        return -1;
    }

    DLSYM(s_xcb_lib, x_connect,       pfn_xcb_connect);
    DLSYM(s_xcb_lib, x_disconnect,    pfn_xcb_disconnect);
    DLSYM(s_xcb_lib, x_has_error,     pfn_xcb_connection_has_error);
    DLSYM(s_xcb_lib, x_generate_id,   pfn_xcb_generate_id);
    DLSYM(s_xcb_lib, x_create_window, pfn_xcb_create_window);
    DLSYM(s_xcb_lib, x_map_window,    pfn_xcb_map_window);
    DLSYM(s_xcb_lib, x_destroy_window, pfn_xcb_destroy_window);
    DLSYM(s_xcb_lib, x_flush,         pfn_xcb_flush);
    DLSYM(s_xcb_lib, x_poll_event,    pfn_xcb_poll_for_event);

    int screen;
    g_xcb = x_connect(NULL, &screen);
    if (x_has_error(g_xcb)) {
        fprintf(stderr, "GHAL/x11: xcb_connect failed\n");
        return -1;
    }

    /* Initialize EGL */
    return ghal_egl_init(g_xcb);
}

static void x11_shutdown(void) {
    ghal_egl_shutdown();
    if (g_xcb) {
        x_disconnect(g_xcb);
        g_xcb = NULL;
    }
    if (s_xcb_lib) {
        dlclose(s_xcb_lib);
        s_xcb_lib = NULL;
    }
}

static void x11_get_display_size(uint32_t *w, uint32_t *h) {
    if (w) *w = 1920;
    if (h) *h = 1080;  /* TODO: query screen from xcb_setup */
}

static uint32_t x11_preferred_format(void) {
    return GHAL_FMT_BGRA8;
}

/* ── Window management ───────────────────────────────────────── */

static int x11_win_open(GHalWindow *w) {
    xcb_window_t win = x_generate_id(g_xcb);
    uint32_t mask  = XCB_CW_EVENT_MASK;
    uint32_t vals[1] = {
        XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
        XCB_EVENT_MASK_POINTER_MOTION
    };

    x_create_window(g_xcb, XCB_COPY_FROM_PARENT, win, g_root,
                    0, 0, (uint16_t)w->width, (uint16_t)w->height,
                    0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    XCB_COPY_FROM_PARENT, mask, vals);
    x_map_window(g_xcb, win);
    x_flush(g_xcb);

    w->native_handle = (uintptr_t)win;

    /* Create EGL surface for this window */
    return ghal_egl_create_surface(win, w);
}

static void x11_win_close(GHalWindow *w) {
    if (!w->native_handle) return;
    ghal_egl_destroy_surface(w);
    x_destroy_window(g_xcb, (xcb_window_t)w->native_handle);
    x_flush(g_xcb);
    w->native_handle = 0;
}

static void x11_win_set_title(GHalWindow *w, const char *title) {
    /* xcb_change_property for WM_NAME — simplified: no-op stub */
    (void)w; (void)title;
}

static void x11_win_resize(GHalWindow *w, uint32_t nw, uint32_t nh) {
    w->width  = nw;
    w->height = nh;
    /* xcb_configure_window — simplified stub */
}

/* ── Surface management ──────────────────────────────────────── */

static int x11_surface_create(GHalSurface *s, uint32_t w, uint32_t h,
                               uint32_t fmt) {
    (void)fmt;
    s->stride     = w * 4;
    s->pixel_size = (size_t)s->stride * h;
    s->pixels     = malloc(s->pixel_size);
    return s->pixels ? 0 : -1;
}

static void x11_surface_destroy(GHalSurface *s) {
    free(s->pixels);
    s->pixels = NULL;
}

static int x11_surface_lock(GHalSurface *s) {
    (void)s;
    return 0;
}

static void x11_surface_unlock(GHalSurface *s) {
    s->gpu_dirty = true;
}

static void x11_surface_present(GHalWindow *w, GHalSurface *s) {
    ghal_egl_present(w, s);
}

/* ── VSYNC ───────────────────────────────────────────────────── */

static uint32_t x11_vsync(void) {
    /* EGL SwapInterval already handles vsync if available */
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 16667000 };
    nanosleep(&ts, NULL);
    return 16667;
}

/* ── Input polling ───────────────────────────────────────────── */

static int x11_poll_input(GHalInputEvent *event) {
    xcb_generic_event_t *ev = x_poll_event(g_xcb);
    if (!ev) return 0;

    memset(event, 0, sizeof(*event));
    uint8_t type = ev->response_type & 0x7F;

    if (type == XCB_KEY_PRESS) {
        event->type = GHAL_INPUT_KEY_DOWN;
        /* keycode at offset 10 in xcb_key_press_event_t */
        event->key.keycode = ((uint8_t *)ev)[10];
    } else if (type == XCB_KEY_RELEASE) {
        event->type = GHAL_INPUT_KEY_UP;
        event->key.keycode = ((uint8_t *)ev)[10];
    } else if (type == XCB_MOTION_NOTIFY) {
        event->type = GHAL_INPUT_MOUSE_MOVE;
        /* event_x/y at offsets 18/20 */
        event->mouse.x = (int32_t)*((int16_t *)((uint8_t *)ev + 18));
        event->mouse.y = (int32_t)*((int16_t *)((uint8_t *)ev + 20));
    }

    free(ev);
    return (event->type != 0) ? 1 : 0;
}

/* ── Driver registration ─────────────────────────────────────── */

GHalDriver s_x11_driver = {
    .name             = "x11-egl",
    .init             = x11_init,
    .shutdown         = x11_shutdown,
    .get_display_size = x11_get_display_size,
    .preferred_format = x11_preferred_format,
    .win_open         = x11_win_open,
    .win_close        = x11_win_close,
    .win_set_title    = x11_win_set_title,
    .win_resize       = x11_win_resize,
    .surface_create   = x11_surface_create,
    .surface_destroy  = x11_surface_destroy,
    .surface_lock     = x11_surface_lock,
    .surface_unlock   = x11_surface_unlock,
    .surface_present  = x11_surface_present,
    .gpu_begin_frame  = NULL,
    .gpu_submit       = NULL,
    .gpu_end_frame    = NULL,
    .vsync            = x11_vsync,
    .vsync_hz         = 60,
    .poll_input       = x11_poll_input,
};

__attribute__((constructor(150)))
static void register_x11_ghal(void) {
    g_ghal_driver = &s_x11_driver;
}

#endif /* !__APPLE__ */
