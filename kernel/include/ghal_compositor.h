/* include/ghal_compositor.h — Unified Compositor Interface
 *
 * Identical on Host, Baremetal, seL4.
 * Communication is IPC (NEXS protocol) on all platforms.
 *
 * Compositor runs as: thread (Host), task (Baremetal), PD (seL4).
 */

#ifndef GHAL_COMPOSITOR_H
#define GHAL_COMPOSITOR_H
#pragma once

#include "ghal.h"
#include "ghal_ipc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GCOMP_MAX_WINDOWS  64
#define GCOMP_IPC_TIMEOUT  1000  /* ms */

typedef struct {
    GHalWindow  wins[GCOMP_MAX_WINDOWS];
    int         count;
    uint32_t    focused_id;
    uint32_t    last_vsync_us;
    uint32_t    damage_flags[GCOMP_MAX_WINDOWS];  /* dirty per window */
    uint32_t    next_win_id;
    uint32_t    frame_count;
} GCompositor;

/* Core compositor (identical on all platforms) */
int  gcomp_init(GCompositor *c);
int  gcomp_tick(GCompositor *c);    /* damage + composite + vsync + ipc */
void gcomp_shutdown(GCompositor *c);

/* Window registry */
int  gcomp_win_register(GCompositor *c, GHalWindow *w);
void gcomp_win_unregister(GCompositor *c, uint32_t id);
void gcomp_publish_registry(GHalWindow *w);   /* → /dev/win/<id>/ */
void gcomp_update_registry(GHalWindow *w);    /* sync fields back */
GHalWindow *gcomp_find_win(GCompositor *c, uint32_t id);

/* IPC event dispatch (all platforms) */
int  gcomp_poll_ipc_event(GCompositor *c, GHalIpcMessage *msg);
int  gcomp_route_event(GCompositor *c, const GHalIpcMessage *msg);

/* Input dispatch */
void gcomp_send_input_to_window(uint32_t win_id,
                                 const GHalInputEvent *event);

/* Damage helpers */
void gcomp_mark_damage(GCompositor *c, uint32_t win_id);
void gcomp_clear_damage(GCompositor *c, uint32_t win_id);

#ifdef __cplusplus
}
#endif

#endif /* GHAL_COMPOSITOR_H */
