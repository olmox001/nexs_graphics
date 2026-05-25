/* baremetal/compositor_bare.c — Baremetal compositor entry point
 *
 * On baremetal, the compositor runs as the main task (no threads).
 * Uses the SAME gcomp_tick() from compositor/compositor.c — no changes.
 * Only difference: main loop is here instead of a hosted thread.
 */

#ifdef NEXS_BAREMETAL

#include "ghal_compositor.h"
#include "ghal.h"

/* Single compositor state for baremetal */
static GCompositor g_compositor = {0};

/* Entry point called from baremetal kernel after nexs_hal_init() */
void ghal_baremetal_main(void) {
    if (ghal_init() != 0) {
        /* If no driver, halt */
        extern void nexs_hal_halt(void) __attribute__((noreturn));
        nexs_hal_halt();
    }

    /* Run the compositor tick loop forever */
    while (1) {
        gcomp_tick(&g_compositor);
        /* No sleep: vsync() in gcomp_tick handles timing */
    }
}

#endif /* NEXS_BAREMETAL */
