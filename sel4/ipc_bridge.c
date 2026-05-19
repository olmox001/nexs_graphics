/* sel4/ipc_bridge.c — Microkit ↔ NEXS IPC translation layer
 *
 * Translates NEXS sendmessage/receivemessage (targeting /dev/win/<id>/)
 * to Microkit channel IPC under the hood.
 */

#ifdef NEXS_SEL4

#include "../include/ghal_ipc.h"
#include <microkit.h>

#include <string.h>
#include <stdint.h>

#define CH_COMPOSITOR    1   /* default channel to compositor PD */
#define CH_VSYNC_TIMER   3

/* ── nexs_sendmessage — NEXS IPC send abstraction ────────────── */

int nexs_sendmessage(const char *dst_path, const void *msg, size_t len) {
    if (!dst_path || !msg) return -1;

    /* Route based on destination registry path */
    microkit_channel ch = CH_COMPOSITOR;

    if (strncmp(dst_path, "/dev/win/", 9) == 0 ||
        strncmp(dst_path, "/dev/compositor/", 16) == 0) {
        ch = CH_COMPOSITOR;
    }

    /* Pack msg into Microkit message registers */
    const GHalIpcMessage *ipc_msg = (const GHalIpcMessage *)msg;
    microkit_mr_set(0, ipc_msg->msg_type);
    microkit_mr_set(1, ipc_msg->payload_len);

    /* For small payloads (<= 4 words), pack inline */
    uint32_t words = (ipc_msg->payload_len + 3) / 4;
    if (words > 10) words = 10;
    for (uint32_t i = 0; i < words; i++) {
        uint32_t v = 0;
        memcpy(&v, ipc_msg->payload + i * 4, 4);
        microkit_mr_set(2 + i, v);
    }

    microkit_msginfo info = microkit_msginfo_new(0, 2 + words);
    microkit_send(ch, info);
    (void)len;
    return 0;
}

/* ── nexs_receivemessage — NEXS IPC recv abstraction ─────────── */

int nexs_receivemessage(const char *src_path, void *msg, size_t len) {
    if (!src_path || !msg) return -1;

    microkit_channel ch = CH_COMPOSITOR;
    microkit_msginfo info = microkit_recv(ch, NULL);
    (void)info;

    GHalIpcMessage *ipc_msg = (GHalIpcMessage *)msg;
    ipc_msg->msg_type    = (uint32_t)microkit_mr_get(0);
    ipc_msg->payload_len = (uint32_t)microkit_mr_get(1);
    uint32_t words = (ipc_msg->payload_len + 3) / 4;
    if (words > 10) words = 10;
    for (uint32_t i = 0; i < words && i * 4 < sizeof(ipc_msg->payload); i++) {
        uint32_t v = (uint32_t)microkit_mr_get(2 + i);
        memcpy(ipc_msg->payload + i * 4, &v, 4);
    }

    (void)len;
    return 1;
}

/* ── VSYNC wait — block on timer channel ─────────────────────── */

void microkit_ipc_wait_vsync(void) {
    microkit_recv(CH_VSYNC_TIMER, NULL);
}

/* ── IPC send/recv wrappers (used by ghal_ipc.h API) ────────── */

int ghal_ipc_send(const char *dst_path, const GHalIpcMessage *msg) {
    return nexs_sendmessage(dst_path, msg, sizeof(*msg));
}

int ghal_ipc_recv(GHalIpcMessage *msg, int timeout_ms) {
    (void)timeout_ms;
    return nexs_receivemessage("/dev/compositor/inbox", msg, sizeof(*msg));
}

#endif /* NEXS_SEL4 */
