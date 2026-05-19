/* compositor/window_registry.c — /dev/win/<id>/ namespace (Plan 9 style)
 *
 * Creates and maintains registry entries for each window.
 * Uses NEXS registry API (nexs_registry.h).
 */

/* Resolved via Makefile: -I base-nexs/registry/include -I base-nexs/core/include */
#include "nexs_registry.h"
#include "nexs_value.h"

#include "ghal_compositor.h"

#include <stdio.h>
#include <string.h>

/* ── Registry helpers ───────────────────────────────────────── */

/* Write an integer to a registry path, creating it if needed */
void winreg_set_int(const char *path, int val) {
    reg_set(path, VAL_INT(val), RK_READ | RK_WRITE);
}

/* Write a string to a registry path */
void winreg_set_str(const char *path, const char *s) {
    reg_set(path, VAL_STR(s), RK_READ | RK_WRITE);
}

/* Store a raw pointer as an integer value in the registry */
static void winreg_set_ptr(const char *path, void *ptr) {
    reg_set(path, VAL_INT((int64_t)(uintptr_t)ptr), RK_READ);
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

