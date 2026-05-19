/* sel4/ghal_sel4.c — GHalDriver stubs for seL4
 *
 * All operations forward to compositor_pd via IPC.
 * Follows the same pattern as base-nexs/hal/sel4/hal_sel4.c.
 * Compiled with: -DNEXS_SEL4
 */

#ifdef NEXS_SEL4

#include "../include/ghal.h"
#include "../include/ghal_ipc.h"

#include <string.h>

/* ── IPC wrapper (maps NEXS protocol → Microkit channels) ─────
 * See sel4/ipc_bridge.c for implementation. */
extern int nexs_sendmessage(const char *dst, const void *msg, size_t len);

static int sel4_init(void) {
    /* Compositor PD is already running; just signal ready */
    return 0;
}

static void sel4_shutdown(void) {}

static void sel4_get_display_size(uint32_t *w, uint32_t *h) {
    /* Query compositor via IPC */
    GHalIpcMessage msg = {
        .msg_type = GHAL_IPC_WIN_OPEN,  /* reuse as query */
        .payload_len = 0,
    };
    nexs_sendmessage("/dev/compositor/query", &msg, sizeof(msg));
    /* Response populates /dev/gfx/width and /dev/gfx/height */
    if (w) *w = 1024;
    if (h) *h = 768;
}

static uint32_t sel4_preferred_format(void) {
    return GHAL_FMT_BGRA8;
}

/* Window open: IPC to compositor PD */
static int sel4_win_open(GHalWindow *w) {
    GHalIpcMessage msg;
    msg.msg_type    = GHAL_IPC_WIN_OPEN;
    GHalIpcWinOpen *payload = (GHalIpcWinOpen *)msg.payload;
    strncpy(payload->title, w->title, sizeof(payload->title) - 1);
    payload->width  = w->width;
    payload->height = w->height;
    msg.payload_len = sizeof(GHalIpcWinOpen);
    return nexs_sendmessage("/dev/compositor/inbox", &msg, sizeof(msg));
}

static void sel4_win_close(GHalWindow *w) {
    GHalIpcMessage msg;
    msg.msg_type    = GHAL_IPC_WIN_CLOSE;
    msg.payload_len = sizeof(uint32_t);
    memcpy(msg.payload, &w->id, sizeof(uint32_t));
    nexs_sendmessage("/dev/compositor/inbox", &msg, sizeof(msg));
}

static void sel4_win_set_title(GHalWindow *w, const char *title) {
    GHalIpcMessage msg;
    msg.msg_type    = GHAL_IPC_WIN_TITLE;
    msg.payload_len = (uint32_t)(strlen(title) + 5);
    memcpy(msg.payload, &w->id, 4);
    strncpy((char *)msg.payload + 4, title, sizeof(msg.payload) - 5);
    nexs_sendmessage("/dev/compositor/inbox", &msg, sizeof(msg));
}

static void sel4_win_resize(GHalWindow *w, uint32_t nw, uint32_t nh) {
    GHalIpcMessage msg;
    msg.msg_type = GHAL_IPC_WIN_RESIZE;
    GHalIpcWinResize *p = (GHalIpcWinResize *)msg.payload;
    p->win_id = w->id;
    p->width  = nw;
    p->height = nh;
    msg.payload_len = sizeof(GHalIpcWinResize);
    nexs_sendmessage("/dev/compositor/inbox", &msg, sizeof(msg));
}

/* Surface: shared memory region (allocated by surface_pd) */
static int sel4_surface_create(GHalSurface *s, uint32_t w, uint32_t h,
                                uint32_t fmt) {
    (void)fmt;
    /* Request shared MR from surface_pd */
    /* In full impl: seL4_CNode_Copy + seL4_Map; stub here */
    s->stride     = w * 4;
    s->pixel_size = (size_t)s->stride * h;
    s->pixels     = NULL;  /* MR mapped by surface_pd grant */
    s->gpu_dirty  = 0;
    return 0;
}

static void sel4_surface_destroy(GHalSurface *s) {
    /* Release shared MR back to surface_pd */
    s->pixels = NULL;
}

static int sel4_surface_lock(GHalSurface *s) {
    (void)s;
    /* Acquire write token (seL4 notification) */
    return 0;
}

static void sel4_surface_unlock(GHalSurface *s) {
    s->gpu_dirty = 1;
}

static void sel4_surface_present(GHalWindow *w, GHalSurface *s) {
    GHalIpcMessage msg;
    msg.msg_type    = GHAL_IPC_SURFACE_PRESENT;
    msg.payload_len = sizeof(uint32_t);
    memcpy(msg.payload, &w->id, sizeof(uint32_t));
    nexs_sendmessage("/dev/compositor/inbox", &msg, sizeof(msg));
    s->gpu_dirty = 0;
}

/* VSYNC: wait for CH_VSYNC_TIMER notification from compositor */
static uint32_t sel4_vsync(void) {
    extern void microkit_ipc_wait_vsync(void);
    microkit_ipc_wait_vsync();
    return 16667;
}

/* Input: read from compositor input notification channel */
static int sel4_poll_input(GHalInputEvent *event) {
    (void)event;
    return 0;  /* Phase 7 stub */
}

static GHalDriver s_sel4_driver = {
    .name             = "sel4-ipc",
    .init             = sel4_init,
    .shutdown         = sel4_shutdown,
    .get_display_size = sel4_get_display_size,
    .preferred_format = sel4_preferred_format,
    .win_open         = sel4_win_open,
    .win_close        = sel4_win_close,
    .win_set_title    = sel4_win_set_title,
    .win_resize       = sel4_win_resize,
    .surface_create   = sel4_surface_create,
    .surface_destroy  = sel4_surface_destroy,
    .surface_lock     = sel4_surface_lock,
    .surface_unlock   = sel4_surface_unlock,
    .surface_present  = sel4_surface_present,
    .gpu_begin_frame  = NULL,
    .gpu_submit       = NULL,
    .gpu_end_frame    = NULL,
    .vsync            = sel4_vsync,
    .vsync_hz         = 60,
    .poll_input       = sel4_poll_input,
};

__attribute__((constructor(150)))
static void register_sel4_ghal(void) {
    g_ghal_driver = &s_sel4_driver;
}

#endif /* NEXS_SEL4 */
