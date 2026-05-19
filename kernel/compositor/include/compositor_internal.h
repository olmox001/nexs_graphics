/* compositor/include/compositor_internal.h — internal compositor state */

#ifndef COMPOSITOR_INTERNAL_H
#define COMPOSITOR_INTERNAL_H
#pragma once

#include "../../include/ghal_compositor.h"

/* Max pending IPC messages in local queue */
#define COMP_IPC_QUEUE_DEPTH 256

/* Per-window compositor state (extends GHalWindow) */
typedef struct {
    GHalWindow  win;
    uint32_t    damage_x, damage_y;
    uint32_t    damage_w, damage_h;
    int         pending_close;
} CompWindow;

/* Internal compositor context */
typedef struct {
    CompWindow  cwin[GCOMP_MAX_WINDOWS];
    int         count;
    uint32_t    focused_id;
    uint32_t    last_vsync_us;
    uint32_t    frame_count;
    uint32_t    next_win_id;

    /* IPC ring buffer */
    GHalIpcMessage ipc_queue[COMP_IPC_QUEUE_DEPTH];
    int            ipc_head, ipc_tail;
} CompState;

/* Internal helpers (not public) */
CompWindow *comp_find_cwin(CompState *s, uint32_t id);
void        comp_composite_window(CompState *s, CompWindow *cw);
void        comp_update_fps_reg(CompState *s);

#endif /* COMPOSITOR_INTERNAL_H */
