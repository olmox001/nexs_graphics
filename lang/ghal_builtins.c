/* lang/ghal_builtins.c — NEXS graphics builtin functions
 *
 * Maps NEXS Value arguments to GHAL API calls.
 * Registered via fn_register_builtin_sig() in ghal_fn_table.c.
 */

#include "../include/ghal.h"
#include "../include/ghal_2d.h"
#include "../include/ghal_compositor.h"

/* Resolved via -I base-nexs/lang/include */
#include "nexs_fn.h"
#include "nexs_value.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Global window table for builtin-managed windows */
#define GHAL_BLT_MAX_WINS 64
static GHalWindow  s_wins[GHAL_BLT_MAX_WINS];
static int         s_win_used[GHAL_BLT_MAX_WINS];

static GHalWindow *alloc_win(void) {
    for (int i = 0; i < GHAL_BLT_MAX_WINS; i++) {
        if (!s_win_used[i]) {
            s_win_used[i] = 1;
            memset(&s_wins[i], 0, sizeof(s_wins[i]));
            return &s_wins[i];
        }
    }
    return NULL;
}

static GHalWindow *find_win(uint32_t id) {
    for (int i = 0; i < GHAL_BLT_MAX_WINS; i++) {
        if (s_win_used[i] && s_wins[i].id == id)
            return &s_wins[i];
    }
    return NULL;
}

static void release_win(uint32_t id) {
    for (int i = 0; i < GHAL_BLT_MAX_WINS; i++) {
        if (s_win_used[i] && s_wins[i].id == id) {
            s_win_used[i] = 0;
            return;
        }
    }
}

/* ── win_open(title str, w int, h int) → int ───────────────── */

static Value bi_win_open(Value *args, int n) {
    if (n < 3)
        return val_err(1, "win_open: need title w h");

    char        title_buf[256];
    val_to_str(&args[0], title_buf, sizeof(title_buf));
    const char *title = title_buf;
    uint32_t    w     = (uint32_t)args[1].ival;
    uint32_t    h     = (uint32_t)args[2].ival;

    GHalWindow *win = alloc_win();
    if (!win) return val_err(-1, "win_open: no free window slots");

    int rc = ghal_win_open(win, title, w, h);
    if (rc != 0) {
        release_win(win->id);
        return val_err(rc, "win_open failed");
    }
    return val_int((int64_t)win->id);
}

/* ── win_close(id int) → int ────────────────────────────────── */

static Value bi_win_close(Value *args, int n) {
    if (n < 1) return val_err(1, "win_close: need id");
    uint32_t id = (uint32_t)args[0].ival;
    GHalWindow *w = find_win(id);
    if (!w) return val_err(-1, "win_close: window not found");
    ghal_win_close(w);
    release_win(id);
    return val_int(0);
}

/* ── draw_rect(win int, x int, y int, w int, h int, color int) → int ── */

static Value bi_draw_rect(Value *args, int n) {
    if (n < 6) return val_err(1, "draw_rect: need win x y w h color");

    uint32_t id    = (uint32_t)args[0].ival;
    int32_t  x     = (int32_t)args[1].ival;
    int32_t  y     = (int32_t)args[2].ival;
    int32_t  rw    = (int32_t)args[3].ival;
    int32_t  rh    = (int32_t)args[4].ival;
    uint32_t color = (uint32_t)args[5].ival;

    GHalWindow *w = find_win(id);
    if (!w || !w->surface) return val_err(-1, "draw_rect: invalid window");

    ghal_surface_lock(w->surface);
    draw2d_fill_rect(w->surface, x, y, rw, rh, color);
    ghal_surface_unlock(w->surface);
    return val_int(0);
}

/* ── surface_blit(win int) → int  (present) ─────────────────── */

static Value bi_surface_blit(Value *args, int n) {
    if (n < 1) return val_err(1, "surface_blit: need win");
    uint32_t id = (uint32_t)args[0].ival;
    GHalWindow *w = find_win(id);
    if (!w || !w->surface) return val_err(-1, "surface_blit: invalid window");
    ghal_surface_present(w, w->surface);
    return val_int(0);
}

/* ── draw_text(win int, x int, y int, text str, color int) → int ── */

static Value bi_draw_text(Value *args, int n) {
    if (n < 5) return val_err(1, "draw_text: need win x y text color");
    uint32_t    id    = (uint32_t)args[0].ival;
    int32_t     x     = (int32_t)args[1].ival;
    int32_t     y     = (int32_t)args[2].ival;
    char        text_buf[512];
    val_to_str(&args[3], text_buf, sizeof(text_buf));
    const char *text  = text_buf;
    uint32_t    color = (uint32_t)args[4].ival;

    GHalWindow *w = find_win(id);
    if (!w || !w->surface) return val_err(-1, "draw_text: invalid window");
    ghal_surface_lock(w->surface);
    draw2d_text(w->surface, x, y, text, color);
    ghal_surface_unlock(w->surface);
    return val_int(0);
}

/* ── draw_clear(win int, color int) → int ───────────────────── */

static Value bi_draw_clear(Value *args, int n) {
    if (n < 2) return val_err(1, "draw_clear: need win color");
    uint32_t id    = (uint32_t)args[0].ival;
    uint32_t color = (uint32_t)args[1].ival;
    GHalWindow *w = find_win(id);
    if (!w || !w->surface) return val_err(-1, "draw_clear: invalid window");
    ghal_surface_lock(w->surface);
    draw2d_clear(w->surface, color);
    ghal_surface_unlock(w->surface);
    return val_int(0);
}

/* ── gl_clear(r f, g f, b f, a f) → int ─────────────────────── */

static Value bi_gl_clear(Value *args, int n) {
    if (n < 4) return val_err(1, "gl_clear: need r g b a");
    /* Convert float 0-1 to 0-255 */
    uint32_t r = (uint32_t)(args[0].fval * 255.0);
    uint32_t g = (uint32_t)(args[1].fval * 255.0);
    uint32_t b = (uint32_t)(args[2].fval * 255.0);
    uint32_t a = (uint32_t)(args[3].fval * 255.0);
    uint32_t color = GHAL_RGBA(r, g, b, a);
    /* Clear all active windows */
    for (int i = 0; i < GHAL_BLT_MAX_WINS; i++) {
        if (s_win_used[i] && s_wins[i].surface) {
            ghal_surface_lock(s_wins[i].surface);
            draw2d_clear(s_wins[i].surface, color);
            ghal_surface_unlock(s_wins[i].surface);
        }
    }
    return val_int(0);
}

/* ── compute_run(kernel str) → int  (stub) ───────────────────── */

static Value bi_compute_run(Value *args, int n) {
    if (n < 1) return val_err(1, "compute_run: need kernel");
    char kernel_buf[256];
    val_to_str(&args[0], kernel_buf, sizeof(kernel_buf));
    (void)kernel_buf;
    /* Phase 8: dispatch to ghal_compute */
    return val_int(0);
}

/* ── vsync() → int ───────────────────────────────────────────── */

static Value bi_vsync(Value *args, int n) {
    (void)args; (void)n;
    ghal_vsync();
    return val_int(0);
}

/* ── Registration ─────────────────────────────────────────────── */

void ghal_builtins_register(void) {
    fn_register_builtin_sig("win_open",
        bi_win_open,    "win_open(title str, w int, h int) -> int");
    fn_register_builtin_sig("win_close",
        bi_win_close,   "win_close(id int) -> int");
    fn_register_builtin_sig("draw_rect",
        bi_draw_rect,   "draw_rect(win int, x int, y int, w int, h int, color int) -> int");
    fn_register_builtin_sig("surface_blit",
        bi_surface_blit,"surface_blit(win int) -> int");
    fn_register_builtin_sig("draw_text",
        bi_draw_text,   "draw_text(win int, x int, y int, text str, color int) -> int");
    fn_register_builtin_sig("draw_clear",
        bi_draw_clear,  "draw_clear(win int, color int) -> int");
    fn_register_builtin_sig("gl_clear",
        bi_gl_clear,    "gl_clear(r float, g float, b float, a float) -> int");
    fn_register_builtin_sig("compute_run",
        bi_compute_run, "compute_run(kernel str) -> int");
    fn_register_builtin_sig("vsync",
        bi_vsync,       "vsync() -> int");
}
