/* include/ghal_service.h — GHAL Service Module Protocol
 *
 * A GhalServiceModule is a self-contained unit that:
 *   1. Declares a set of BuiltinFn functions with NEXS Value in/out.
 *   2. Registers itself at startup via __attribute__((constructor(300))).
 *   3. Publishes its resource namespace into the NEXS registry (/dev/<prefix>/).
 *
 * This lets each service file be dropped in independently — no central
 * list, no #ifdef.  ghal_service_init_all() is called once from the
 * GHAL bootstrap (constructor priority 250) and walks the linked list
 * that was built during static init.
 *
 * Compatibility:
 *   .nx scripts  — see builtin via fn_register_builtin_sig, call by name
 *   NEXS registry — /dev/<prefix>/<id>/ keys published per resource
 *   POSIX hosted  — VFS bridge routes to fopen/fread
 *   NEXS VFS      — VFS bridge routes to nexs_open/nexs_read
 *   seL4 PD       — VFS bridge routes to IPC to fs PD
 */

#ifndef GHAL_SERVICE_H
#define GHAL_SERVICE_H
#pragma once

#ifndef NEXS_HAL_H
#include "nexs_hal.h"
#endif
#ifndef NEXS_API
#define NEXS_API
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Types from nexs_value / nexs_fn ─────────────────────────── */

#include "nexs_value.h"
typedef Value (*BuiltinFn)(Value *args, int n_args);

/* ── Builtin descriptor ───────────────────────────────────────── */

typedef struct {
    const char *name;   /* .nx function name, e.g. "img_load"           */
    const char *sig;    /* human-readable sig for docs/introspect        */
    BuiltinFn   fn;     /* C implementation: Value fn(Value*, int)       */
} GhalBuiltinDef;

/* ── Service module descriptor ────────────────────────────────── */

typedef struct GhalServiceModule {
    const char              *name;        /* "image", "font", …           */
    const char              *version;     /* "1.0"                        */
    const char              *reg_prefix;  /* "/dev/img", "/dev/font", …   */
    void                   (*init)(void); /* called by ghal_service_init_all */
    void                   (*shutdown)(void);
    const GhalBuiltinDef    *builtins;
    int                      n_builtins;
    struct GhalServiceModule *_next;      /* linked-list — set by registry */
} GhalServiceModule;

/* ── Service registry API ─────────────────────────────────────── */

/* Register a module — call from __attribute__((constructor(300))). */
NEXS_API void ghal_service_register(GhalServiceModule *mod);

/* Call init() on all modules and push builtins into the NEXS fn table.
 * Called once from the GHAL bootstrap (constructor 250 or ghal_init). */
NEXS_API void ghal_service_init_all(void);

/* Teardown: call shutdown() on all modules in reverse order. */
NEXS_API void ghal_service_shutdown_all(void);

/* Lookup by name (NULL if not found). */
NEXS_API GhalServiceModule *ghal_service_find(const char *name);

/* Iterate: returns first module; follow ->_next for the rest. */
NEXS_API GhalServiceModule *ghal_service_head(void);

/* ── Convenience macro for module registration ────────────────── */

/* Place this in the module .c file after defining `s_mod`:
 *
 *   GHAL_SERVICE_REGISTER(s_mod)
 *
 * It expands to a constructor that calls ghal_service_register(). */
#define GHAL_SERVICE_REGISTER(mod_var) \
    __attribute__((constructor(300))) \
    static void _ghal_svc_register_##mod_var(void) { \
        ghal_service_register(&(mod_var)); \
    }

#ifdef __cplusplus
}
#endif
#endif /* GHAL_SERVICE_H */
