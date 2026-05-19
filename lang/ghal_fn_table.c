/* lang/ghal_fn_table.c — Register GHAL builtins in NEXS function table
 *
 * Called once at runtime startup (from reimplementation/hal/hal_hosted.c
 * or from the seL4/baremetal init path).
 *
 * ghal_builtins_register() pushes the core graphics builtins (win_open,
 * draw_rect, vsync, …) into the fn table.  ghal_service_init_all() then
 * walks the GhalServiceModule linked list built by constructor(300)
 * registrations and pushes every service's builtins.
 */

#include "nexs_fn.h"
#include "../include/ghal_service.h"

/* Declared in ghal_builtins.c — registers the core bi_* functions */
extern void ghal_builtins_register(void);

void ghal_register_builtins(void) {
    /* Core graphics builtins (window, draw, vsync, …) */
    ghal_builtins_register();

    /* Initialize all self-registered service modules (image, font, …).
     * Each module was registered by its constructor(300) before main(). */
    ghal_service_init_all();
}
