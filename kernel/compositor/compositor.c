/* compositor/compositor.c — Unified Compositor
 *
 * Identical code on Host (thread), Baremetal (task), seL4 (PD).
 * Platform differences are isolated to GHalDriver callbacks.
 * No #ifdef, no platform-specific logic here.
 */

#include "ghal_compositor.h"
#include "ghal_surface.h"
#include "ghal_2d.h"
#include "include/compositor_internal.h"

/* Resolved via -I base-nexs/lang/include */
#include "nexs_value.h"
#include "nexs_registry.h"

#include <string.h>
#include <stdio.h>

/* Forward declarations (implemented in window_registry.c) */
extern void winreg_set_int(const char *path, int val);
extern void winreg_set_str(const char *path, const char *val);
extern void gcomp_publish_registry(GHalWindow *w);
extern void gcomp_update_registry(GHalWindow *w);
extern int  reg_delete(const char *path);

extern void ghal_layout_render(GHalWindow *w);

/* ── Compositor init/shutdown ───────────────────────────────── */

int gcomp_init(GCompositor *c) {
    if (!c) return -1;
    memset(c, 0, sizeof(*c));
    c->next_win_id = 1;

    /* Publish compositor status */
    winreg_set_int("/sys/compositor/running", 1);
    winreg_set_int("/sys/compositor/windows", 0);
    winreg_set_int("/sys/compositor/fps",     0);
    winreg_set_int("/sys/compositor/focused", 0);

    return 0;
}

void gcomp_shutdown(GCompositor *c) {
    if (!c) return;
    for (int i = 0; i < c->count; i++)
        gcomp_win_unregister(c, c->wins[i].id);
    winreg_set_int("/sys/compositor/running", 0);
}

/* ── Main tick — runs identically on all platforms ─────────── */

int gcomp_tick(GCompositor *c) {
    if (!c || !g_ghal_driver) return -1;

    /* 1. Composite dirty windows */
    for (int i = 0; i < c->count; i++) {
        if (c->damage_flags[i] && c->wins[i].surface) {
            GHalWindow *w = &c->wins[i];

            /* Check if window has a VFS HTML layout tree registered */
            char layout_tag_path[256];
            snprintf(layout_tag_path, sizeof(layout_tag_path), "/dev/win/%u/layout/tag", w->id);
            Value tag_val = reg_get(layout_tag_path);
            if (tag_val.type == TYPE_STR && tag_val.data) {
                ghal_surface_lock(w->surface);
                ghal_layout_render(w);
                ghal_surface_unlock(w->surface);
            }
            val_free(&tag_val);

            if (g_ghal_driver->surface_present)
                g_ghal_driver->surface_present(w, w->surface);
            c->damage_flags[i] = 0;
        }
    }

    /* 2. VSYNC — abstracted; implementation varies per backend */
    uint32_t us = 0;
    if (g_ghal_driver->vsync)
        us = g_ghal_driver->vsync();
    c->last_vsync_us = us;
    c->frame_count++;

    /* 3. FPS update every 60 frames */
    if ((c->frame_count % 60) == 0 && us > 0) {
        uint32_t fps = 1000000u / (us ? us : 16667u);
        winreg_set_int("/sys/compositor/fps", (int)fps);
    }

    /* 4. Dispatch pending IPC events */
    GHalIpcMessage msg;
    int dispatched = 0;
    while (gcomp_poll_ipc_event(c, &msg) == 1 && dispatched < 32) {
        gcomp_route_event(c, &msg);
        dispatched++;
    }

    /* 5. Poll input */
    if (g_ghal_driver->poll_input) {
        GHalInputEvent ev;
        while (g_ghal_driver->poll_input(&ev) == 1)
            gcomp_send_input_to_window(c->focused_id, &ev);
    }

    return 0;
}

/* ── Damage helpers ─────────────────────────────────────────── */

void gcomp_mark_damage(GCompositor *c, uint32_t win_id) {
    for (int i = 0; i < c->count; i++) {
        if (c->wins[i].id == win_id) {
            c->damage_flags[i] = 1;
            return;
        }
    }
}

void gcomp_clear_damage(GCompositor *c, uint32_t win_id) {
    for (int i = 0; i < c->count; i++) {
        if (c->wins[i].id == win_id) {
            c->damage_flags[i] = 0;
            return;
        }
    }
}

/* ── Window lookup ──────────────────────────────────────────── */

GHalWindow *gcomp_find_win(GCompositor *c, uint32_t id) {
    for (int i = 0; i < c->count; i++)
        if (c->wins[i].id == id) return &c->wins[i];
    return NULL;
}

/* ── Input dispatch ─────────────────────────────────────────── */

void gcomp_send_input_to_window(uint32_t win_id,
                                 const GHalInputEvent *event) {
    if (!event) return;

    char path[80];
    snprintf(path, sizeof(path), "/dev/win/%u/inbox", win_id);

    /* Encode event as simple string and push to IPC queue */
    char buf[64];
    if (event->type == GHAL_INPUT_KEY_DOWN || event->type == GHAL_INPUT_KEY_UP) {
        snprintf(buf, sizeof(buf), "%s:%u",
                 event->type == GHAL_INPUT_KEY_DOWN ? "key_down" : "key_up",
                 event->key.keycode);
    } else if (event->type == GHAL_INPUT_MOUSE_MOVE) {
        snprintf(buf, sizeof(buf), "mouse_move:%d,%d",
                 event->mouse.x, event->mouse.y);
    } else {
        snprintf(buf, sizeof(buf), "mouse_btn:%u", event->mouse.buttons);
    }

    /* Publish to registry inbox key */
    winreg_set_str(path, buf);
}

/* ── Window registration ────────────────────────────────────── */

int gcomp_win_register(GCompositor *c, GHalWindow *w) {
    if (!c || !w || c->count >= GCOMP_MAX_WINDOWS) return -1;

    w->id = c->next_win_id++;
    snprintf(w->reg_path, sizeof(w->reg_path), "/dev/win/%u", w->id);

    if (g_ghal_driver && g_ghal_driver->win_open) {
        if (g_ghal_driver->win_open(w) != 0) return -1;
    }

    c->wins[c->count] = *w;
    c->damage_flags[c->count] = 0;
    c->count++;

    gcomp_publish_registry(&c->wins[c->count - 1]);
    winreg_set_int("/sys/compositor/windows", c->count);
    c->focused_id = w->id;
    winreg_set_int("/sys/compositor/focused", (int)w->id);
    return 0;
}

void gcomp_win_unregister(GCompositor *c, uint32_t id) {
    if (!c) return;
    for (int i = 0; i < c->count; i++) {
        if (c->wins[i].id != id) continue;

        if (g_ghal_driver && g_ghal_driver->win_close)
            g_ghal_driver->win_close(&c->wins[i]);

        char p[80];
        snprintf(p, sizeof(p), "/dev/win/%u", id);
        reg_delete(p);

        int last = c->count - 1;
        if (i != last) {
            c->wins[i]         = c->wins[last];
            c->damage_flags[i] = c->damage_flags[last];
        }
        c->count--;
        winreg_set_int("/sys/compositor/windows", c->count);

        if (c->focused_id == id) {
            c->focused_id = (c->count > 0) ? c->wins[0].id : 0;
            winreg_set_int("/sys/compositor/focused", (int)c->focused_id);
        }
        return;
    }
}

/* ── IPC event routing ──────────────────────────────────────── */

int gcomp_route_event(GCompositor *c, const GHalIpcMessage *msg) {
    if (!c || !msg) return -1;

    switch (msg->msg_type) {
    case GHAL_IPC_WIN_OPEN: {
        const GHalIpcWinOpen *p = (const GHalIpcWinOpen *)msg->payload;
        GHalWindow w = {0};
        strncpy(w.title, p->title, sizeof(w.title) - 1);
        w.width  = p->width;
        w.height = p->height;
        gcomp_win_register(c, &w);
        break;
    }
    case GHAL_IPC_WIN_CLOSE: {
        uint32_t id;
        memcpy(&id, msg->payload, sizeof(id));
        gcomp_win_unregister(c, id);
        break;
    }
    case GHAL_IPC_WIN_RESIZE: {
        const GHalIpcWinResize *p = (const GHalIpcWinResize *)msg->payload;
        GHalWindow *w = gcomp_find_win(c, p->win_id);
        if (w && g_ghal_driver->win_resize)
            g_ghal_driver->win_resize(w, p->width, p->height);
        break;
    }
    case GHAL_IPC_DRAW_RECT: {
        const GHalIpcDrawRect *p = (const GHalIpcDrawRect *)msg->payload;
        GHalWindow *w = gcomp_find_win(c, p->win_id);
        if (w && w->surface) {
            draw2d_fill_rect(w->surface, p->x, p->y, p->w, p->h, p->color);
            gcomp_mark_damage(c, p->win_id);
        }
        break;
    }
    case GHAL_IPC_SURFACE_PRESENT: {
        uint32_t id;
        memcpy(&id, msg->payload, sizeof(id));
        GHalWindow *w = gcomp_find_win(c, id);
        if (w && w->surface)
            gcomp_mark_damage(c, id);
        break;
    }
    default:
        break;
    }
    return 0;
}
