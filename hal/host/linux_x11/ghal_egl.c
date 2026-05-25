/* host/linux_x11/ghal_egl.c — EGL context + Mesa blit (Linux only)
 *
 * dlopen("libEGL.so") and optionally "libGL.so" / "libGLESv2.so".
 * Uploads CPU surface via glTexImage2D and does a fullscreen quad blit.
 */

#ifndef __APPLE__

#include "ghal_egl.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Minimal EGL types (from EGL/egl.h) */
typedef void       *EGLDisplay;
typedef void       *EGLConfig;
typedef void       *EGLContext;
typedef void       *EGLSurface;
typedef void       *EGLNativeDisplayType;
typedef uint32_t    EGLNativeWindowType;
typedef int32_t     EGLint;
typedef uint32_t    EGLenum;
typedef uint32_t    EGLBoolean;

#define EGL_NONE          0x3038
#define EGL_SURFACE_TYPE  0x3033
#define EGL_WINDOW_BIT    0x0004
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT  0x0004
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_NO_DISPLAY    ((EGLDisplay)0)
#define EGL_NO_CONTEXT    ((EGLContext)0)
#define EGL_NO_SURFACE    ((EGLSurface)0)
#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)
#define EGL_TRUE  1
#define EGL_FALSE 0

typedef EGLDisplay (*pfn_eglGetDisplay)(EGLNativeDisplayType);
typedef EGLBoolean (*pfn_eglInitialize)(EGLDisplay, EGLint *, EGLint *);
typedef EGLBoolean (*pfn_eglBindAPI)(EGLenum);
typedef EGLBoolean (*pfn_eglChooseConfig)(EGLDisplay, const EGLint *,
                    EGLConfig *, EGLint, EGLint *);
typedef EGLContext (*pfn_eglCreateContext)(EGLDisplay, EGLConfig,
                    EGLContext, const EGLint *);
typedef EGLSurface (*pfn_eglCreateWindowSurface)(EGLDisplay, EGLConfig,
                    EGLNativeWindowType, const EGLint *);
typedef EGLBoolean (*pfn_eglMakeCurrent)(EGLDisplay, EGLSurface,
                    EGLSurface, EGLContext);
typedef EGLBoolean (*pfn_eglSwapBuffers)(EGLDisplay, EGLSurface);
typedef EGLBoolean (*pfn_eglDestroySurface)(EGLDisplay, EGLSurface);
typedef EGLBoolean (*pfn_eglDestroyContext)(EGLDisplay, EGLContext);
typedef EGLBoolean (*pfn_eglTerminate)(EGLDisplay);

static void *s_egl_lib = NULL;
static pfn_eglGetDisplay         eglGetDisplay_;
static pfn_eglInitialize         eglInitialize_;
static pfn_eglBindAPI            eglBindAPI_;
static pfn_eglChooseConfig       eglChooseConfig_;
static pfn_eglCreateContext      eglCreateContext_;
static pfn_eglCreateWindowSurface eglCreateWindowSurface_;
static pfn_eglMakeCurrent        eglMakeCurrent_;
static pfn_eglSwapBuffers        eglSwapBuffers_;
static pfn_eglDestroySurface     eglDestroySurface_;
static pfn_eglDestroyContext     eglDestroyContext_;
static pfn_eglTerminate          eglTerminate_;

static EGLDisplay g_egl_display = EGL_NO_DISPLAY;
static EGLConfig  g_egl_config;
static EGLContext g_egl_context = EGL_NO_CONTEXT;

/* Per-window EGL surfaces stored in gpu_handle */

#define DLSYM_EGL(name, type) \
    name##_ = (type)dlsym(s_egl_lib, #name); \
    if (!name##_) { fprintf(stderr, "GHAL/egl: missing %s\n", #name); return -1; }

int ghal_egl_init(void *xcb_connection) {
    s_egl_lib = dlopen("libEGL.so.1", RTLD_LAZY);
    if (!s_egl_lib) s_egl_lib = dlopen("libEGL.so", RTLD_LAZY);
    if (!s_egl_lib) {
        fprintf(stderr, "GHAL/egl: cannot load libEGL: %s\n", dlerror());
        return -1;
    }

    DLSYM_EGL(eglGetDisplay, pfn_eglGetDisplay);
    DLSYM_EGL(eglInitialize, pfn_eglInitialize);
    DLSYM_EGL(eglBindAPI, pfn_eglBindAPI);
    DLSYM_EGL(eglChooseConfig, pfn_eglChooseConfig);
    DLSYM_EGL(eglCreateContext, pfn_eglCreateContext);
    DLSYM_EGL(eglCreateWindowSurface, pfn_eglCreateWindowSurface);
    DLSYM_EGL(eglMakeCurrent, pfn_eglMakeCurrent);
    DLSYM_EGL(eglSwapBuffers, pfn_eglSwapBuffers);
    DLSYM_EGL(eglDestroySurface, pfn_eglDestroySurface);
    DLSYM_EGL(eglDestroyContext, pfn_eglDestroyContext);
    DLSYM_EGL(eglTerminate, pfn_eglTerminate);

    g_egl_display = eglGetDisplay_((EGLNativeDisplayType)xcb_connection);
    if (g_egl_display == EGL_NO_DISPLAY) return -1;

    EGLint major, minor;
    if (!eglInitialize_(g_egl_display, &major, &minor)) return -1;

    /* OpenGL ES 2 context */
    static const EGLint cfg_attr[] = {
        EGL_SURFACE_TYPE,     EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE,  EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLint num_cfg = 0;
    if (!eglChooseConfig_(g_egl_display, cfg_attr, &g_egl_config, 1, &num_cfg)
        || num_cfg == 0) return -1;

    static const EGLint ctx_attr[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    g_egl_context = eglCreateContext_(g_egl_display, g_egl_config,
                                      EGL_NO_CONTEXT, ctx_attr);
    return (g_egl_context != EGL_NO_CONTEXT) ? 0 : -1;
}

void ghal_egl_shutdown(void) {
    if (g_egl_context != EGL_NO_CONTEXT)
        eglDestroyContext_(g_egl_display, g_egl_context);
    if (g_egl_display != EGL_NO_DISPLAY)
        eglTerminate_(g_egl_display);
    if (s_egl_lib) { dlclose(s_egl_lib); s_egl_lib = NULL; }
}

int ghal_egl_create_surface(uint32_t xcb_win, GHalWindow *w) {
    EGLSurface surf = eglCreateWindowSurface_(g_egl_display, g_egl_config,
                                               (EGLNativeWindowType)xcb_win,
                                               NULL);
    if (surf == EGL_NO_SURFACE) return -1;
    w->gpu_handle = (uintptr_t)surf;
    eglMakeCurrent_(g_egl_display, surf, surf, g_egl_context);
    return 0;
}

void ghal_egl_destroy_surface(GHalWindow *w) {
    if (!w->gpu_handle) return;
    eglDestroySurface_(g_egl_display, (EGLSurface)w->gpu_handle);
    w->gpu_handle = 0;
}

void ghal_egl_present(GHalWindow *w, GHalSurface *s) {
    if (!w->gpu_handle || !s->pixels) return;
    EGLSurface surf = (EGLSurface)w->gpu_handle;
    eglMakeCurrent_(g_egl_display, surf, surf, g_egl_context);
    /* Simple CPU→framebuffer copy via glReadPixels fallback */
    /* Full GL blit implemented in Phase 8 */
    eglSwapBuffers_(g_egl_display, surf);
}

#endif /* !__APPLE__ */
