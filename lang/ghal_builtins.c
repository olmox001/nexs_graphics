/* lang/ghal_builtins.c — NEXS graphics builtin functions
 *
 * Maps NEXS Value arguments to GHAL API calls.
 * Registered via fn_register_builtin_sig() in ghal_fn_table.c.
 */

#include "../include/ghal.h"
#include "../include/ghal_2d.h"
#include "../include/ghal_compositor.h"
#include "../include/ghal_bc.h"

/* Resolved via -I base-nexs/lang/include */
#include "nexs_fn.h"
#include "nexs_value.h"
#include "nexs_registry.h"
#include "nexs_eval.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

extern const char *nexs_embedded_lookup(const char *name);
extern EvalCtx *nexs_g_eval_ctx;
extern int nexs_exec(EvalCtx *ctx, const char *path);

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

    /* Allocate and attach a software surface for CPU-side drawing */
    GHalSurface *surf = (GHalSurface *)malloc(sizeof(GHalSurface));
    if (surf) {
        if (ghal_surface_create(surf, w, h, GHAL_FMT_BGRA8) != 0) {
            free(surf);
            surf = NULL;
        }
    }
    win->surface = surf;

    return val_int((int64_t)win->id);
}

/* ── win_close(id int) → int ────────────────────────────────── */

static Value bi_win_close(Value *args, int n) {
    if (n < 1) return val_err(1, "win_close: need id");
    uint32_t id = (uint32_t)args[0].ival;
    GHalWindow *w = find_win(id);
    if (!w) return val_err(-1, "win_close: window not found");
    if (w->surface) {
        ghal_surface_destroy(w->surface);
        free(w->surface);
        w->surface = NULL;
    }
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

/* ── Custom Extension-Aware File Resolution for exec & load ── */

static char *read_file_content(const char *path, int *out_len) {
    char normalized[256];
    const char *p = path;
    if (p[0] == '/') p++;

    /* Resolve relative paths via VFS CWD */
    Value cwd_val = reg_get("/proc/1/vfs_cwd");
    if (path[0] != '/' && cwd_val.type == TYPE_STR && cwd_val.data &&
        ((char *)cwd_val.data)[0] != '\0') {
        snprintf(normalized, sizeof(normalized), "%s/%s", (char *)cwd_val.data, path);
    } else {
        strncpy(normalized, p, sizeof(normalized) - 1);
        normalized[sizeof(normalized) - 1] = '\0';
    }
    val_free(&cwd_val);

    /* 1. Registry VFS */
    char reg_vfs_path[256];
    snprintf(reg_vfs_path, sizeof(reg_vfs_path), "/sys/vfs/files/%s", normalized);
    Value vfs_val = reg_get(reg_vfs_path);
    if (vfs_val.type == TYPE_STR && vfs_val.data) {
        const char *vfs_content = (char *)vfs_val.data;
        if (strncmp(vfs_content, "BUNDLED", 7) == 0) {
            const char *lookup_path = normalized;
            if (strncmp(vfs_content, "BUNDLED:", 8) == 0) {
                lookup_path = vfs_content + 8;
            }
            const char *emb = nexs_embedded_lookup(lookup_path);
            if (emb) {
                char *res = strdup(emb);
                if (out_len) *out_len = (int)strlen(res);
                val_free(&vfs_val);
                return res;
            }
        } else {
            char *res = strdup(vfs_content);
            if (out_len) *out_len = (int)strlen(res);
            val_free(&vfs_val);
            return res;
        }
    }
    val_free(&vfs_val);

    /* 2. Embedded table */
    const char *emb = nexs_embedded_lookup(normalized);
    if (!emb && p != path) {
        emb = nexs_embedded_lookup(p);
    }
    if (emb) {
        char *res = strdup(emb);
        if (out_len) *out_len = (int)strlen(res);
        return res;
    }

    /* 3. Hosted File System */
    FILE *f = fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz >= 0) {
            char *res = malloc((size_t)sz + 1);
            if (res) {
                size_t rd = fread(res, 1, (size_t)sz, f);
                res[rd] = '\0';
                if (out_len) *out_len = (int)rd;
                fclose(f);
                return res;
            }
        }
        fclose(f);
    }
    return NULL;
}

static Value bi_exec(Value *args, int n) {
    if (n < 1) return val_err(4, "exec: requires a path");
    if (args[0].type != TYPE_STR || !args[0].data)
        return val_err(4, "exec: argument must be a string");

    const char *path = (char *)args[0].data;
    size_t len = strlen(path);

    if (len > 5 && strcmp(path + len - 5, ".g.nx") == 0) {
        int content_len = 0;
        char *src = read_file_content(path, &content_len);
        if (!src) {
            char err_buf[256];
            snprintf(err_buf, sizeof(err_buf), "cannot open '%s': No such file or directory", path);
            return val_err(99, err_buf);
        }

        uint32_t code_len = 0;
        uint8_t *bytecode = ghal_compile_g_nx(src, &code_len);
        free(src);

        if (!bytecode || code_len == 0) {
            char err_buf[256];
            snprintf(err_buf, sizeof(err_buf), "compilation of '%s' failed", path);
            return val_err(99, err_buf);
        }

        GalbVM vm;
        if (galb_vm_init(&vm, bytecode, code_len) != 0) {
            free(bytecode);
            return val_err(99, "VM initialization failed");
        }

        int rc = galb_vm_run(&vm);
        free(bytecode);
        return val_int(rc);
    } else {
        EvalCtx *ctx_to_use = nexs_g_eval_ctx;
        EvalCtx inner_ctx;
        if (!ctx_to_use) {
            eval_ctx_init(&inner_ctx);
            ctx_to_use = &inner_ctx;
        }
        return val_int(nexs_exec(ctx_to_use, path));
    }
}

/* ── HTML/CSS VFS Layout Builtins ───────────────────────────── */

extern void ghal_html_parse(int win_id, const char *html_str);
extern void ghal_html_dump(int win_id, char *out_buf, size_t max_len);

static Value bi_ui_parse_html(Value *args, int n) {
    if (n < 2) return val_err(1, "ui_parse_html: need win html");
    uint32_t win_id = (uint32_t)args[0].ival;
    if (args[1].type != TYPE_STR || !args[1].data)
        return val_err(2, "ui_parse_html: html must be a string");

    ghal_html_parse((int)win_id, (char *)args[1].data);
    return val_int(0);
}

static Value bi_ui_dump_html(Value *args, int n) {
    if (n < 1) return val_err(1, "ui_dump_html: need win");
    uint32_t win_id = (uint32_t)args[0].ival;

    static char dump_buf[16384];
    dump_buf[0] = '\0';
    ghal_html_dump((int)win_id, dump_buf, sizeof(dump_buf));
    return val_str(dump_buf);
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
    fn_register_builtin_sig("ui_parse_html",
        bi_ui_parse_html, "ui_parse_html(win int, html str) -> int");
    fn_register_builtin_sig("ui_dump_html",
        bi_ui_dump_html,  "ui_dump_html(win int) -> str");

    /* Override exec and load in the base NEXS system */
    fn_register_builtin_sig("exec",
        bi_exec,        "exec(path str) -> int");
    fn_register_builtin_sig("load",
        bi_exec,        "load(path str) -> int");

    /* Register GHAL functions under /sys in VFS registry */
    {
        static const char *names[] = {
            "win_open", "win_close", "draw_rect", "surface_blit",
            "draw_text", "draw_clear", "gl_clear", "compute_run", "vsync",
            "ui_parse_html", "ui_dump_html", "exec", "load"
        };
        char path[80];
        for (int i = 0; i < (int)(sizeof(names)/sizeof(names[0])); i++) {
            NexsFnDef *def = fn_lookup(names[i]);
            if (def) {
                int idx = (int)(def - g_fn_table);
                snprintf(path, sizeof(path), "/sys/%s", names[i]);
                reg_set(path, val_fn_idx(idx), RK_READ | RK_EXEC);
            }
        }
    }
}
