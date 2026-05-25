/* compositor/ipc_dispatcher.c — IPC send/recv abstraction
 *
 * Maps NEXS sendmessage/receivemessage to the compositor's IPC queue.
 * Same interface on Host (pipe/thread), Baremetal (shared memory),
 * and seL4 (Microkit channels — see sel4/ipc_bridge.c).
 */

#include "ghal_ipc.h"
#include "ghal_compositor.h"

#include <string.h>
#include <stdio.h>

/* Local in-process IPC queue (used on Host and Baremetal single-AS) */
#define IPC_QUEUE_MAX 256

static GHalIpcMessage s_ipc_queue[IPC_QUEUE_MAX];
static int            s_ipc_head = 0;
static int            s_ipc_tail = 0;

/* ── ghal_ipc_send — push message to compositor ────────────── */

int ghal_ipc_send(const char *dst_path, const GHalIpcMessage *msg) {
    if (!dst_path || !msg) return -1;

    int next = (s_ipc_tail + 1) % IPC_QUEUE_MAX;
    if (next == s_ipc_head) return -1;  /* queue full */

    s_ipc_queue[s_ipc_tail] = *msg;
    s_ipc_tail = next;
    return 0;
}

/* ── ghal_ipc_recv — pop next message (non-blocking when timeout=0) ── */

int ghal_ipc_recv(GHalIpcMessage *msg, int timeout_ms) {
    (void)timeout_ms;  /* non-blocking in local queue */
    if (s_ipc_head == s_ipc_tail) return 0;  /* empty */

    *msg = s_ipc_queue[s_ipc_head];
    s_ipc_head = (s_ipc_head + 1) % IPC_QUEUE_MAX;
    return 1;
}

/* ── gcomp_poll_ipc_event — used by compositor tick ────────── */

int gcomp_poll_ipc_event(GCompositor *c, GHalIpcMessage *msg) {
    (void)c;
    return ghal_ipc_recv(msg, 0);
}
