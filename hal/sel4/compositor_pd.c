/* sel4/compositor_pd.c — Compositor as seL4 Protected Domain
 *
 * Wraps the IDENTICAL gcomp_tick() from compositor/compositor.c.
 * Microkit IPC channels translate to NEXS IPC protocol.
 *
 * Channels (defined in system description .system file):
 *   CH_APP_WIN_REQUEST  — incoming window/draw IPC from apps
 *   CH_COMP_TO_APP      — responses back to apps
 *   CH_VSYNC_TIMER      — timer notification (triggers frame tick)
 *   CH_INIT_READY       — sent once after init
 */

#ifdef NEXS_SEL4

#include "ghal_compositor.h"
#include "ghal_ipc.h"
#include <microkit.h>

#define CH_APP_WIN_REQUEST  1
#define CH_COMP_TO_APP      2
#define CH_VSYNC_TIMER      3
#define CH_INIT_READY       4

static GCompositor g_compositor = {0};

/* Translate Microkit message registers → GHalIpcMessage */
static void microkit_mr_to_ipc(microkit_msginfo info,
                                 GHalIpcMessage *msg) {
    (void)info;
    /* In real impl: read microkit_mr(0..N) and reconstruct payload */
    msg->msg_type    = (uint32_t)microkit_mr_get(0);
    msg->payload_len = (uint32_t)microkit_mr_get(1);
    /* payload: read from shared memory region (MR) */
}

/* Handle Microkit notifications */
void notified(microkit_channel ch) {
    if (ch == CH_APP_WIN_REQUEST) {
        microkit_msginfo info = microkit_recv(CH_APP_WIN_REQUEST, NULL);
        GHalIpcMessage msg = {0};
        microkit_mr_to_ipc(info, &msg);
        gcomp_route_event(&g_compositor, &msg);

        /* Send acknowledgment */
        microkit_mr_set(0, 0);  /* status = OK */
        microkit_reply(microkit_msginfo_new(0, 1));
    }

    if (ch == CH_VSYNC_TIMER) {
        /* VSYNC interrupt: drive one compositor tick */
        gcomp_tick(&g_compositor);
        /* Wake all waiting app PDs */
        microkit_notify(CH_COMP_TO_APP);
    }
}

/* PD entry point */
void init(void) {
    gcomp_init(&g_compositor);
    microkit_notify(CH_INIT_READY);
}

#endif /* NEXS_SEL4 */
