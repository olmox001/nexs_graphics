/* include/ghal_ipc.h — NEXS IPC Protocol for graphics
 *
 * Used on ALL platforms: Host, Baremetal, seL4.
 * Packages window/surface operations over sendmessage/receivemessage.
 */

#ifndef GHAL_IPC_H
#define GHAL_IPC_H
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Message type codes ─────────────────────────────────────── */
#define GHAL_IPC_WIN_OPEN       1
#define GHAL_IPC_WIN_CLOSE      2
#define GHAL_IPC_WIN_RESIZE     3
#define GHAL_IPC_DRAW_RECT      4
#define GHAL_IPC_DRAW_BLIT      5
#define GHAL_IPC_SURFACE_CREATE 6
#define GHAL_IPC_SURFACE_LOCK   7
#define GHAL_IPC_INPUT_EVENT    8
#define GHAL_IPC_SURFACE_PRESENT 9
#define GHAL_IPC_WIN_TITLE      10

/* ── Unified IPC message ────────────────────────────────────── */
typedef struct {
    uint32_t msg_type;      /* GHAL_IPC_* */
    uint32_t payload_len;
    uint8_t  payload[512];  /* variable-length payload */
} GHalIpcMessage;

/* ── Typed payloads (cast from payload[]) ───────────────────── */
typedef struct {
    char     title[128];
    uint32_t width, height;
} GHalIpcWinOpen;

typedef struct {
    uint32_t win_id;
    uint32_t width, height;
} GHalIpcWinResize;

typedef struct {
    uint32_t win_id;
    int32_t  x, y, w, h;
    uint32_t color;
} GHalIpcDrawRect;

typedef struct {
    uint32_t surf_id;
    uint32_t width, height, fmt;
} GHalIpcSurfaceCreate;

/* ── IPC transport (abstract over send/recv) ────────────────── */
int ghal_ipc_send(const char *dst_path, const GHalIpcMessage *msg);
int ghal_ipc_recv(GHalIpcMessage *msg, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* GHAL_IPC_H */
