/* compositor/window_registry.c — /dev/win/<id>/ namespace (Plan 9 style)
 *
 * Creates and maintains registry entries for each window.
 * Uses NEXS registry API (nexs_registry.h).
 */

/* Resolved via Makefile: -I base-nexs/registry/include -I base-nexs/core/include */
#include "nexs_registry.h"
#include "nexs_value.h"

#include "../include/ghal_compositor.h"

#include <stdio.h>
#include <string.h>

/* ── Registry helpers ───────────────────────────────────────── */

/* Write an integer to a registry path, creating it if needed */
void winreg_set_int(const char *path, int val) {
    Value v;
    v.type = VAL_INT;
    v.ival = val;
    reg_set(path, v, RK_READ | RK_WRITE);
}

/* Write a string to a registry path */
void winreg_set_str(const char *path, const char *s) {
    Value v = val_str(s);
    reg_set(path, v, RK_READ | RK_WRITE);
}

/* Write a pointer to a registry path */
static void winreg_set_ptr(const char *path, void *ptr) {
    Value v;
    v.type = VAL_PTR;
    v.ptr  = ptr;
    reg_set(path, v, RK_READ);
}

/* ── Window registration ────────────────────────────────────── */

void gcomp_publish_registry(GHalWindow *w) {
    char p[128];

#define WPTH(sub) (snprintf(p, sizeof(p), "%s/" sub, w->reg_path), p)

    winreg_set_str(WPTH("title"),   w->title);
    winreg_set_int(WPTH("width"),   (int)w->width);
    winreg_set_int(WPTH("height"),  (int)w->height);
    winreg_set_int(WPTH("visible"), w->visible ? 1 : 0);
    winreg_set_int(WPTH("zorder"),  (int)w->zorder);
    winreg_set_ptr(WPTH("surface"), w->surface);
    winreg_set_int(WPTH("damage"),  0);
    winreg_set_str(WPTH("inbox"),   "");

#undef WPTH
}

void gcomp_update_registry(GHalWindow *w) {
    char p[128];

#define WPTH(sub) (snprintf(p, sizeof(p), "%s/" sub, w->reg_path), p)
    winreg_set_str(WPTH("title"),   w->title);
    winreg_set_int(WPTH("width"),   (int)w->width);
    winreg_set_int(WPTH("height"),  (int)w->height);
    winreg_set_int(WPTH("visible"), w->visible ? 1 : 0);
    winreg_set_int(WPTH("zorder"),  (int)w->zorder);
    winreg_set_ptr(WPTH("surface"), w->surface);
#undef WPTH
}

int gcomp_win_register(GCompositor *c, GHalWindow *w) {
    if (!c || !w || c->count >= GCOMP_MAX_WINDOWS) return -1;

    /* Assign ID */
    w->id = c->next_win_id++;
    snprintf(w->reg_path, sizeof(w->reg_path), "/dev/win/%u", w->id);

    /* Ask backend to create the native window */
    if (g_ghal_driver && g_ghal_driver->win_open) {
        if (g_ghal_driver->win_open(w) != 0) return -1;
    }

    /* Store in compositor table */
    c->wins[c->count] = *w;
    c->damage_flags[c->count] = 0;
    c->count++;

    /* Publish to registry */
    gcomp_publish_registry(&c->wins[c->count - 1]);

    /* Update window count */
    winreg_set_int("/sys/compositor/windows", c->count);

    /* Focus new window */
    c->focused_id = w->id;
    winreg_set_int("/sys/compositor/focused", (int)w->id);

    return 0;
}

void gcomp_win_unregister(GCompositor *c, uint32_t id) {
    if (!c) return;
    for (int i = 0; i < c->count; i++) {
        if (c->wins[i].id != id) continue;

        /* Close native window */
        if (g_ghal_driver && g_ghal_driver->win_close)
            g_ghal_driver->win_close(&c->wins[i]);

        /* Remove registry entries */
        char p[80];
        snprintf(p, sizeof(p), "/dev/win/%u", id);
        reg_delete(p);  /* deletes subtree recursively */

        /* Compact the window table */
        int last = c->count - 1;
        if (i != last) {
            c->wins[i]        = c->wins[last];
            c->damage_flags[i] = c->damage_flags[last];
        }
        c->count--;

        winreg_set_int("/sys/compositor/windows", c->count);

        /* Update focus */
        if (c->focused_id == id) {
            c->focused_id = (c->count > 0) ? c->wins[0].id : 0;
            winreg_set_int("/sys/compositor/focused", (int)c->focused_id);
        }
        return;
    }
}
