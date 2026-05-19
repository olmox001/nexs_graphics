/* host/linux_x11/ghal_egl.h — EGL context helpers (Linux X11 only) */

#ifndef GHAL_EGL_H
#define GHAL_EGL_H
#pragma once

#include "../../include/ghal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize EGL (called by x11_init after xcb_connect) */
int  ghal_egl_init(void *xcb_connection);
void ghal_egl_shutdown(void);

/* Create/destroy EGL surface for an xcb_window_t */
int  ghal_egl_create_surface(uint32_t xcb_win, GHalWindow *w);
void ghal_egl_destroy_surface(GHalWindow *w);

/* Blit CPU surface pixels to EGL surface and swap */
void ghal_egl_present(GHalWindow *w, GHalSurface *s);

#ifdef __cplusplus
}
#endif

#endif /* GHAL_EGL_H */
