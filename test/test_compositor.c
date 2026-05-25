/* test/test_compositor.c — Unit tests for compositor registry + IPC dispatcher
 *
 * Tests: window register/unregister, IPC message routing.
 * Stubs out registry and driver calls; no display needed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef NEXS_API
#define NEXS_API
#endif

/* Stub GHalDriver / GHalSurface / GHalWindow inline */
typedef unsigned long uintptr_t;
typedef unsigned long size_t;

typedef struct GHalSurface {
    uint32_t id, width, height, stride, format;
    void *pixels;
    size_t pixel_size;
    uintptr_t gpu_handle;
    bool gpu_dirty;
} GHalSurface;

typedef struct GHalWindow {
    uint32_t id;
    char title[128];
    uint32_t width, height;
    GHalSurface *surface;
    bool visible;
    uint32_t zorder;
    char reg_path[64];
    uintptr_t native_handle;
} GHalWindow;

typedef struct {
    uint32_t type;
    uint32_t win_id;
    union {
        struct { uint32_t keycode; uint32_t modifiers; } key;
        struct { int32_t x, y; uint32_t buttons; } mouse;
    };
} GHalInputEvent;

typedef struct {
    const char *name;
    int  (*init)(void);
    void (*shutdown)(void);
    void (*get_display_size)(uint32_t *w, uint32_t *h);
    uint32_t (*preferred_format)(void);
    int  (*win_open)(GHalWindow *w);
    void (*win_close)(GHalWindow *w);
    void (*win_set_title)(GHalWindow *w, const char *title);
    void (*win_resize)(GHalWindow *w, uint32_t nw, uint32_t nh);
    int  (*surface_create)(GHalSurface *s, uint32_t w, uint32_t h, uint32_t fmt);
    void (*surface_destroy)(GHalSurface *s);
    int  (*surface_lock)(GHalSurface *s);
    void (*surface_unlock)(GHalSurface *s);
    void (*surface_present)(GHalWindow *w, GHalSurface *s);
    int  (*gpu_begin_frame)(GHalWindow *w);
    int  (*gpu_submit)(const void *cmdbuf, size_t len);
    int  (*gpu_end_frame)(GHalWindow *w);
    uint32_t (*vsync)(void);
    uint32_t vsync_hz;
    int  (*poll_input)(GHalInputEvent *event);
} GHalDriver;

#define GHAL_IPC_WIN_OPEN       1
#define GHAL_IPC_WIN_CLOSE      2
#define GHAL_IPC_WIN_RESIZE     3
#define GHAL_IPC_DRAW_RECT      4
#define GHAL_IPC_SURFACE_PRESENT 9

typedef struct {
    uint32_t msg_type;
    uint32_t payload_len;
    uint8_t  payload[512];
} GHalIpcMessage;

typedef struct {
    char title[128];
    uint32_t width, height;
} GHalIpcWinOpen;

typedef struct {
    uint32_t win_id;
    uint32_t width, height;
} GHalIpcWinResize;

typedef struct {
    uint32_t win_id;
    int32_t x, y, w, h;
    uint32_t color;
} GHalIpcDrawRect;

#define GCOMP_MAX_WINDOWS 64
typedef struct {
    GHalWindow wins[GCOMP_MAX_WINDOWS];
    int count;
    uint32_t focused_id;
    uint32_t last_vsync_us;
    uint32_t damage_flags[GCOMP_MAX_WINDOWS];
    uint32_t next_win_id;
    uint32_t frame_count;
} GCompositor;

/* Stubs for registry and driver */
GHalDriver *g_ghal_driver = NULL;
static int g_win_open_calls = 0;
static int g_win_close_calls = 0;
static int g_present_calls = 0;
static char g_reg_entries[64][2][256];
static int  g_reg_count = 0;

/* Minimal Value stub — compositor only checks .type == TYPE_STR */
#define TYPE_NIL 0
#define TYPE_STR 5
typedef struct { int type; void *data; } Value;

/* Registry stubs */
static int reg_set_impl(const char *path) {
    if (g_reg_count < 64)
        strncpy(g_reg_entries[g_reg_count++][0], path, 255);
    return 0;
}
int reg_delete(const char *path) { (void)path; return 0; }
Value reg_get(const char *path) { (void)path; Value v = {TYPE_NIL, NULL}; return v; }
void val_free(Value *v) { (void)v; }
void winreg_set_int(const char *p, int val) { (void)val; reg_set_impl(p); }
void winreg_set_str(const char *p, const char *s) { (void)s; reg_set_impl(p); }
void gcomp_publish_registry(GHalWindow *w) { (void)w; }
void gcomp_update_registry(GHalWindow *w) { (void)w; }

/* Surface + layout stubs needed by gcomp_tick */
int  ghal_surface_lock(GHalSurface *s)   { (void)s; return 0; }
void ghal_surface_unlock(GHalSurface *s) { (void)s; }
void ghal_layout_render(GHalWindow *w)   { (void)w; }

static int stub_win_open(GHalWindow *w) { (void)w; g_win_open_calls++; return 0; }
static void stub_win_close(GHalWindow *w) { (void)w; g_win_close_calls++; }
static void stub_present(GHalWindow *w, GHalSurface *s) { (void)w; (void)s; g_present_calls++; }
static uint32_t stub_vsync(void) { return 16667; }

static GHalDriver s_stub_driver = {
    .name = "stub",
    .win_open = stub_win_open,
    .win_close = stub_win_close,
    .surface_present = stub_present,
    .vsync = stub_vsync,
};

/* Import the functions under test */
int  gcomp_init(GCompositor *c);
int  gcomp_tick(GCompositor *c);
void gcomp_shutdown(GCompositor *c);
int  gcomp_win_register(GCompositor *c, GHalWindow *w);
void gcomp_win_unregister(GCompositor *c, uint32_t id);
GHalWindow *gcomp_find_win(GCompositor *c, uint32_t id);
int  gcomp_route_event(GCompositor *c, const GHalIpcMessage *msg);
void gcomp_mark_damage(GCompositor *c, uint32_t id);
void gcomp_clear_damage(GCompositor *c, uint32_t id);
void gcomp_publish_registry(GHalWindow *w);
void gcomp_update_registry(GHalWindow *w);
void gcomp_send_input_to_window(uint32_t id, const GHalInputEvent *ev);
int  gcomp_poll_ipc_event(GCompositor *c, GHalIpcMessage *msg);
int  ghal_ipc_send(const char *path, const GHalIpcMessage *msg);
int  ghal_ipc_recv(GHalIpcMessage *msg, int timeout_ms);

/* draw2d stub */
void draw2d_fill_rect(GHalSurface *s, int32_t x, int32_t y,
                      int32_t w, int32_t h, uint32_t c) {
    (void)s;(void)x;(void)y;(void)w;(void)h;(void)c;
}

/* ── Test helpers ────────────────────────────────────────────── */
static int g_pass = 0, g_fail = 0;
#define EXPECT(cond, name) do { \
    if (cond) { printf("  PASS: %s\n", name); g_pass++; } \
    else { printf("  FAIL: %s\n", name); g_fail++; } \
} while(0)

/* ── Tests ────────────────────────────────────────────────────── */

static void test_compositor_init(void) {
    printf("test_compositor_init:\n");
    GCompositor c;
    int rc = gcomp_init(&c);
    EXPECT(rc == 0, "gcomp_init returns 0");
    EXPECT(c.count == 0, "no windows initially");
    EXPECT(c.next_win_id == 1, "first ID is 1");
}

static void test_win_register(void) {
    printf("test_win_register:\n");
    g_ghal_driver = &s_stub_driver;
    g_win_open_calls = 0;

    GCompositor c;
    gcomp_init(&c);

    GHalWindow w = {0};
    strncpy(w.title, "Test Window", sizeof(w.title));
    w.width = 320; w.height = 240;

    int rc = gcomp_win_register(&c, &w);
    EXPECT(rc == 0, "register returns 0");
    EXPECT(c.count == 1, "count becomes 1");
    EXPECT(w.id != 0, "window gets non-zero ID");
    EXPECT(g_win_open_calls == 1, "backend win_open called");
    EXPECT(gcomp_find_win(&c, w.id) != NULL, "find_win works");
}

static void test_win_unregister(void) {
    printf("test_win_unregister:\n");
    g_ghal_driver = &s_stub_driver;
    g_win_close_calls = 0;

    GCompositor c;
    gcomp_init(&c);

    GHalWindow w = {0};
    strncpy(w.title, "Temp", sizeof(w.title));
    w.width = 100; w.height = 100;
    gcomp_win_register(&c, &w);
    uint32_t id = w.id;

    gcomp_win_unregister(&c, id);
    EXPECT(c.count == 0, "count back to 0");
    EXPECT(gcomp_find_win(&c, id) == NULL, "find_win returns NULL");
    EXPECT(g_win_close_calls == 1, "backend win_close called");
}

static void test_two_windows(void) {
    printf("test_two_windows:\n");
    g_ghal_driver = &s_stub_driver;

    GCompositor c;
    gcomp_init(&c);

    GHalWindow w1 = {0}, w2 = {0};
    strncpy(w1.title, "Win1", sizeof(w1.title));
    strncpy(w2.title, "Win2", sizeof(w2.title));
    w1.width = w2.width = 640;
    w1.height = w2.height = 480;

    gcomp_win_register(&c, &w1);
    gcomp_win_register(&c, &w2);
    EXPECT(c.count == 2, "two windows registered");
    EXPECT(w1.id != w2.id, "different IDs");

    gcomp_win_unregister(&c, w1.id);
    EXPECT(c.count == 1, "one window after unregister");
    EXPECT(gcomp_find_win(&c, w2.id) != NULL, "w2 still findable");
}

static void test_damage_tracking(void) {
    printf("test_damage_tracking:\n");
    g_ghal_driver = &s_stub_driver;

    GCompositor c;
    gcomp_init(&c);

    GHalWindow w = {0};
    strncpy(w.title, "Damage", sizeof(w.title));
    w.width = 200; w.height = 200;
    gcomp_win_register(&c, &w);

    EXPECT(c.damage_flags[0] == 0, "no damage initially");
    gcomp_mark_damage(&c, w.id);
    EXPECT(c.damage_flags[0] == 1, "damage marked");
    gcomp_clear_damage(&c, w.id);
    EXPECT(c.damage_flags[0] == 0, "damage cleared");
}

static void test_ipc_queue(void) {
    printf("test_ipc_queue:\n");
    GHalIpcMessage msg = { .msg_type = GHAL_IPC_WIN_OPEN, .payload_len = 4 };
    memcpy(msg.payload, "TEST", 4);

    int rc = ghal_ipc_send("/dev/compositor/inbox", &msg);
    EXPECT(rc == 0, "ipc_send returns 0");

    GHalIpcMessage recv = {0};
    GCompositor c; gcomp_init(&c);
    int got = gcomp_poll_ipc_event(&c, &recv);
    EXPECT(got == 1, "poll returns 1 message");
    EXPECT(recv.msg_type == GHAL_IPC_WIN_OPEN, "message type preserved");
}

int main(void) {
    printf("=== GHAL compositor unit tests ===\n");
    test_compositor_init();
    test_win_register();
    test_win_unregister();
    test_two_windows();
    test_damage_tracking();
    test_ipc_queue();
    printf("\nResult: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail > 0) ? 1 : 0;
}
