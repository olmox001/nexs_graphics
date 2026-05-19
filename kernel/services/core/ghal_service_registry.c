/* services/core/ghal_service_registry.c — Service module linked-list registry
 *
 * Maintains the singly-linked list of GhalServiceModule descriptors.
 * ghal_service_init_all() walks the list and:
 *   1. Creates the module's /dev/<prefix>/ key in the NEXS registry.
 *   2. Calls the module's init() callback.
 *   3. Registers each GhalBuiltinDef with fn_register_builtin_sig().
 *
 * No platform #ifdef here.  The module itself chooses what its init()
 * does; the registry just orchestrates the sequencing.
 */

#include "../../include/ghal_service.h"

/* Full NEXS headers — resolved via Makefile -I flags */
#include "nexs_fn.h"
#include "nexs_registry.h"
#include "nexs_value.h"

#include <stdio.h>
#include <string.h>

/* ── Module list ──────────────────────────────────────────────── */

static GhalServiceModule *s_head = NULL;
static int                s_count = 0;
static int                s_initialised = 0;

void ghal_service_register(GhalServiceModule *mod) {
    if (!mod) return;
    mod->_next = s_head;
    s_head     = mod;
    s_count++;
}

GhalServiceModule *ghal_service_head(void) { return s_head; }

GhalServiceModule *ghal_service_find(const char *name) {
    for (GhalServiceModule *m = s_head; m; m = m->_next)
        if (name && strcmp(m->name, name) == 0) return m;
    return NULL;
}

/* ── Init all modules ─────────────────────────────────────────── */

void ghal_service_init_all(void) {
    if (s_initialised) return;
    s_initialised = 1;

    /* Walk in registration order (last registered = first in list).
     * Reverse to preserve declaration order — build a small array. */
    GhalServiceModule *arr[64];
    int n = 0;
    for (GhalServiceModule *m = s_head; m && n < 64; m = m->_next)
        arr[n++] = m;

    for (int i = n - 1; i >= 0; i--) {
        GhalServiceModule *m = arr[i];

        /* Create /dev/<prefix>/ namespace in NEXS registry */
        if (m->reg_prefix && *m->reg_prefix)
            reg_mkpath(m->reg_prefix, RK_READ | RK_WRITE);

        /* Module-specific init */
        if (m->init) m->init();

        /* Register each builtin into the NEXS function table */
        for (int j = 0; j < m->n_builtins; j++) {
            const GhalBuiltinDef *b = &m->builtins[j];
            fn_register_builtin_sig(b->name, b->fn, b->sig);
        }

        /* Publish module metadata in registry */
        char path[128];
        snprintf(path, sizeof(path), "%s/__version__", m->reg_prefix);
        reg_set(path, VAL_STR(m->version), RK_READ);
        snprintf(path, sizeof(path), "%s/__n_builtins__", m->reg_prefix);
        reg_set(path, VAL_INT(m->n_builtins), RK_READ);
    }
}

/* ── Shutdown all modules (reverse init order) ────────────────── */

void ghal_service_shutdown_all(void) {
    GhalServiceModule *arr[64];
    int n = 0;
    for (GhalServiceModule *m = s_head; m && n < 64; m = m->_next)
        arr[n++] = m;
    for (int i = 0; i < n; i++) {
        if (arr[i]->shutdown) arr[i]->shutdown();
    }
    s_initialised = 0;
}
