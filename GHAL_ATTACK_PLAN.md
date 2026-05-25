# GHAL — Graphics Hardware Abstraction Layer
## Architectural Attack Plan — Unified Baremetal Support

> Progetto separato che usa NEXS come gitsubmodule.
> Nessun file in `nexs/` viene mai modificato.
> Tutte le override stanno in `reimplementation/` con lo stesso path-mirror.

### **UNIFIED ARCHITECTURE: Single Compositor + IPC + Abstract VSYNC**

> Il compositor è UNO SOLO. Host, Baremetal, seL4 hanno identica logica compositor.
> IPC (NEXS protocol) è il meccanismo primario di comunicazione su TUTTI gli stack.
> VSYNC è astratto al livello HAL (callback nel GHalDriver, non device-specific).
> GPU acceleration è trasparente (hardware se disponibile, software fallback).

---

## 1. Analisi della HAL NEXS esistente

### Pattern HAL attuale (ciò che ripetiamo per la grafica)

```
nexs_hal.h         → interfaccia pubblica piatta (8 funzioni)
hal_internal.h     → HalDriver struct + g_hal_driver singleton
nexs_hal_module.h  → modulo probed/registered → /hal/modules/<name>/
nexs_hal_bc.h      → HALB bytecode VM (file-path virtual device)
hal_hosted.c       → stubs libc (constructor registra g_hal_driver)
hal_sel4.c         → weak stubs + seL4 halt
hal/amd64/ arm64/ riscv64/  → implementazioni arch
```

**Principi che eredita GHAL :**
- Ogni risorsa grafica è un file nel registry: `/dev/win/<id>/`, `/dev/gfx/`
- **Cross-process SOLO via IPC NEXS** (sendmessage/receivemessage) — su TUTTI gli stack
- Il compositor è un **servizio** nel registry (thread/process/PD) — IDENTICO su Host/Baremetal/seL4
- Un GHalDriver per piattaforma, registrato come constructor
- Bytecode GALB (Graphics ABI Layer Bytecode) — estensione di HALB
- **VSYNC è callback astratto nel HAL**, non device-specific (timer/interrupt/native)
- **GPU acceleration è trasparente**: HW accelerated se disponibile, software fallback always
- Zero dipendenze esterne non-POSIX nel layer C; le librerie host sono dlopen'd

---

## 2. Unified Architecture Comparison

### Host vs Baremetal vs seL4: Same Compositor, Different Backend

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                          COMPOSITOR (Identical Code)                            │
│                         gcomp_tick() / gcomp_route_event()                      │
│                              IPC dispatch                                        │
│                              Damage tracking                                     │
│                              Z-order compositing                                │
└─────────────────────────────────────────────────────────────────────────────────┘
       ↓ ghal_driver→vsync()      ↓ ghal_driver→surface_present()
       ↓ ghal_driver→poll_input()
       
┌──────────────────────┬──────────────────────┬──────────────────────┐
│  HOST (macOS/Linux)  │   BAREMETAL (QEMU)   │  seL4 (Microkernel)  │
├──────────────────────┼──────────────────────┼──────────────────────┤
│ Compositor: Thread   │ Compositor: Task     │ Compositor: PD       │
│ IPC: NEXS native     │ IPC: same-addr-space │ IPC: Microkit CH     │
│ VSYNC: native callback  │ VSYNC: timer       │ VSYNC: timer         │
│ Window: NSWindow/Xcb │ Window: registry     │ Window: registry     │
│ GPU: Metal/GL (opt)  │ GPU: VirtIO (opt)    │ GPU: none (MVP)      │
│ Surface: GPU buffer  │ Surface: RAM buffer  │ Surface: shared MR   │
└──────────────────────┴──────────────────────┴──────────────────────┘
```

**Code reuse across platforms:**
- ✅ `compositor/compositor.c` — same on all platforms (100% identical)
- ✅ `compositor/ipc_dispatcher.c` — same on all platforms (just calls platform's IPC)
- ✅ `lib/draw2d.c` — same on all platforms (no platform deps)
- ✅ `bc/ghal_bc.c` — same on all platforms (no platform deps)
- ✅ `lang/ghal_builtins.c` — same on all platforms (calls HAL)

**Platform-specific only at Layer 1 (HAL):**
- `host/macos/ghal_macos.m` — NSWindow/Metal impl
- `host/linux_x11/ghal_x11.c` — X11/EGL impl
- `baremetal/virtio_gpu.c` — VirtIO GPU impl
- `baremetal/ghal_linear_fb.c` — Linear framebuffer impl
- `sel4/compositor_pd.c` — seL4 PD wrapper (calls compositor code)

---

```
ghal/
├── .gitmodules                         # nexs → olmox001/base-nexs
├── nexs/                               # git submodule (READ-ONLY)
├── reimplementation/                   # shadow mirror di nexs/ — intercettato dal compiler
│   └── hal/
│       └── hal_hosted.c               # +window bootstrap su macos/linux
├── Makefile
├── include/
│   ├── ghal.h                         # GHalDriver + GHalSurface (top-level)
│   ├── ghal_compositor.h              # window registry API
│   ├── ghal_surface.h                 # framebuffer / texture surface
│   ├── ghal_2d.h                      # draw2d: rect, line, blit, text
│   ├── ghal_3d.h                      # GL state machine compat
│   ├── ghal_compute.h                 # OpenCL-style compute (CL ABI)
│   └── ghal_bc.h                      # GALB bytecode opcodes + state
├── compositor/
│   ├── compositor.c                   # event loop + damage tracking
│   ├── window_registry.c              # /dev/win/ namespace, Plan9 style
│   └── include/
│       └── compositor_internal.h
├── host/
│   ├── ghal_host.c                    # auto-detect: macos vs linux x11
│   ├── macos/
│   │   ├── ghal_macos.m               # NSWindow + CAMetalLayer (ObjC)
│   │   └── ghal_metal.m               # MTLCommandBuffer, MTLTexture
│   └── linux_x11/
│       ├── ghal_x11.c                 # xcb + xkb
│       └── ghal_egl.c                 # EGL + GLESv2/GL3 via mesa
├── baremetal/
│   ├── virtio_gpu.c                   # virtio-gpu v2 driver (QEMU virgl)
│   ├── drm_stub.c                     # DRM/KMS minimal (modesetting)
│   └── compositor_bare.c             # software compositor + damage
├── sel4/
│   ├── compositor_pd.c               # compositor come PD seL4
│   ├── surface_pd.c                  # surface allocator PD
│   └── ghal_sel4.c                   # GHalDriver stub per seL4
├── bc/
│   ├── ghal_bc.c                     # GALB VM executor
│   └── ghal_asm.c                    # assembler helpers
├── lib/
│   ├── draw2d.c                      # implementazione 2D
│   ├── draw3d.c                      # OpenGL-compatible state machine
│   ├── font.c                        # bitmap font renderer (8x16 builtin)
│   └── compute.c                     # OpenCL-style kernel dispatch
├── lang/
│   ├── ghal_builtins.c               # NEXS builtins grafici
│   └── ghal_fn_table.c               # registrazione fn_register_builtin_sig
└── example/
    ├── hello_window.nx
    ├── triangle.nx
    └── compositor_demo.nx
```

### Regola di intercettazione compiler

Il Makefile antepone `-I reimplementation/` a `-I nexs/`. Il compiler
trova sempre la versione local-override prima dell'originale:

```makefile
CFLAGS += -I reimplementation -I nexs/hal/include -I nexs/include \
          -I nexs/core/include -I nexs/kernel/include
```

---

## 3. Unified Compositor Architecture (all platforms)

```
┌──────────────────────────────────────────────────────────────────┐
│  NEXS Scripts (.nx)  — builtins: win_open, draw_rect, gl_*       │  LIVELLO 4: Linguaggio
├──────────────────────────────────────────────────────────────────┤
│  GALB Bytecode VM  — opcodes grafici sopra HALB                  │  LIVELLO 3: Bytecode ABI
├──────────────────────────────────────────────────────────────────┤
│  COMPOSITOR SERVICE (Unified Logic)                              │  LIVELLO 2: Compositor
│  /dev/win/<id>/  (registry)                                      │
│  IPC dispatch (NEXS protocol)                                    │
│  damage tracking + Z-order + input routing                       │
│  ↓ IPC (sendmessage/receivemessage) ↓                            │
├──────────────────────────────────────────────────────────────────┤
│  GHalDriver  ←  GHalModule registry (/hal/gfx/modules/<name>/)  │  LIVELLO 1: HAL grafica
│                                                                   │
│  ┌─────────────────────────────────────────────────────────────┐ │
│  │ Unified Callbacks:                                          │ │
│  │  - surface_create/destroy/lock/unlock/present               │ │
│  │  - win_open/close/set_title/resize                          │ │
│  │  - gpu_begin/submit/end_frame (optional)                    │ │
│  │  - vsync() ← ABSTRACT (native/timer/interrupt)              │ │
│  │  - poll_input()                                             │ │
│  └─────────────────────────────────────────────────────────────┘ │
├──────────────────────┬──────────────────┬───────────────────────┤
│  HOST macOS (Metal)  │ HOST Linux X11   │  BAREMETAL/seL4       │  LIVELLO 0: Backend
│  Cocoa/Metal/CALayer │ xcb/EGL/GL       │  virtio-gpu/DRM/Linear │
│  VSYNC: CADisplayLink│ VSYNC: SwapInterval│ VSYNC: timer/interrupt │
│  (native)           │ (native)          │ (abstracted)          │
└────────────────────┴──────────────────┴───────────────────────┘
```

**Key Insight: COMPOSITOR IS IDENTICAL EVERYWHERE**

```c
/* compositor/compositor.c — runs on ALL platforms identically */
void gcomp_tick(GCompositor *c) {
    /* Damage tracking */
    for (int i = 0; i < c->count; i++) {
        if (c->wins[i].damage) {
            /* Composite dirty window into framebuffer */
            /* (via GHalDriver abstraction — same code everywhere) */
        }
    }
    /* Flip framebuffer */
    ghal_vsync();  /* Abstracted callback — not device-specific */
    
    /* Dispatch IPC events */
    while (gcomp_poll_ipc_event(c, &event)) {
        gcomp_route_event(c, &event);
    }
}
```

**IPC is the PRIMARY communication mechanism:**
- Host: compositor runs as thread/process, apps communicate via IPC NEXS protocol
- Baremetal: compositor runs as main task, apps communicate via IPC within single address space
- seL4: compositor runs as PD, apps communicate via Microkit channels (which wrap IPC)

**GPU acceleration is transparent:**
- If `ghal_driver->gpu_begin_frame` is non-NULL and hardware available → use GPU
- Otherwise → software fallback (draw2d on CPU)

---

## 4. Core C Interfaces (Unified)

### 4.0 `include/ghal_ipc.h` — NEXS IPC Protocol (new)

```c
/* include/ghal_ipc.h
 * 
 * Standard NEXS IPC protocol for graphics.
 * Used on ALL platforms (Host, Baremetal, seL4).
 * Via sendmessage/receivemessage abstraction.
 */

#define GHAL_IPC_WIN_OPEN       1
#define GHAL_IPC_WIN_CLOSE      2
#define GHAL_IPC_WIN_RESIZE     3
#define GHAL_IPC_DRAW_RECT      4
#define GHAL_IPC_DRAW_BLIT      5
#define GHAL_IPC_SURFACE_CREATE 6
#define GHAL_IPC_SURFACE_LOCK   7
#define GHAL_IPC_INPUT_EVENT    8

typedef struct {
    uint32_t msg_type;       /* GHAL_IPC_* */
    uint32_t payload_len;
    uint8_t  payload[512];   /* Variable-length payload */
} GHalIpcMessage;

/* IPC helpers (abstract over sendmessage/receivemessage) */
int ghal_ipc_send(const char *dst_path, const GHalIpcMessage *msg);
int ghal_ipc_recv(GHalIpcMessage *msg, int timeout_ms);
```

### 4.1 `include/ghal.h` — GHalDriver (updated: vsync abstraction)

```c
/* include/ghal.h */
#ifndef GHAL_H
#define GHAL_H
#pragma once
#include "nexs/hal/include/nexs_hal.h"   /* eredita NexsMemMap, NEXS_API */
#include <stdint.h>
#include <stddef.h>

/* ── Surface (framebuffer handle) ────────────────────────── */
typedef struct {
    uint32_t  id;
    uint32_t  width, height;
    uint32_t  stride;        /* bytes per row */
    uint32_t  format;        /* GHAL_FMT_* */
    void     *pixels;        /* NULL se GPU-only */
    uintptr_t gpu_handle;    /* opaque per il backend */
} GHalSurface;

#define GHAL_FMT_RGBA8  0
#define GHAL_FMT_BGRA8  1
#define GHAL_FMT_RGB565 2

/* ── Window handle ───────────────────────────────────────── */
typedef struct {
    uint32_t  id;
    char      title[128];
    uint32_t  width, height;
    GHalSurface *surface;
    char      reg_path[64];  /* /dev/win/<id> */
} GHalWindow;

/* ── Backend driver struct (unified across all platforms) ────── */
typedef struct {
    const char *name;          /* "metal", "x11-egl", "virtio-gpu" */

    /* lifecycle */
    int  (*init)(void);
    void (*shutdown)(void);

    /* display info */
    void (*get_display_size)(uint32_t *w, uint32_t *h);
    uint32_t (*preferred_format)(void);

    /* window management */
    int  (*win_open)(GHalWindow *w);
    void (*win_close)(GHalWindow *w);
    void (*win_set_title)(GHalWindow *w, const char *title);
    void (*win_resize)(GHalWindow *w, uint32_t nw, uint32_t nh);

    /* surface operations */
    int  (*surface_create)(GHalSurface *s, uint32_t w, uint32_t h, uint32_t fmt);
    void (*surface_destroy)(GHalSurface *s);
    int  (*surface_lock)(GHalSurface *s);    /* CPU access */
    void (*surface_unlock)(GHalSurface *s);
    void (*surface_present)(GHalWindow *w, GHalSurface *s); /* flip/blit */

    /* GPU command submission (optional; NULL = software only) */
    int  (*gpu_begin_frame)(GHalWindow *w);
    int  (*gpu_submit)(const void *cmdbuf, size_t len);
    int  (*gpu_end_frame)(GHalWindow *w);

    /* VSYNC ABSTRACTION (unified timing)
     * Called by compositor to sync with display refresh.
     * Implementation is backend-specific:
     *   - Host macOS: CADisplayLink callback or sleep(16ms)
     *   - Host Linux: GLX/EGL SwapInterval or timer
     *   - Baremetal: timer interrupt (vsync_hz configurable)
     * Returns: time until next VSYNC in microseconds (for scheduling)
     */
    uint32_t (*vsync)(void);
    
    /* VSYNC configuration (backend-specific) */
    uint32_t vsync_hz;       /* Display refresh rate (60, 120, etc.) */

    /* Input polling (optional; NULL = no input) */
    int  (*poll_input)(GHalInputEvent *event);
    
} GHalDriver;

/* Singleton — registrato da ogni backend via __attribute__((constructor)) */
extern GHalDriver *g_ghal_driver;

/* ... rest of public API ... */
#endif /* GHAL_H */
```

### 4.2 `include/ghal_compositor.h` — Unified Compositor (all platforms)

```c
/* include/ghal_compositor.h
 * 
 * Compositor runs identically on Host, Baremetal, seL4.
 * Communication is via IPC (NEXS protocol) everywhere.
 */

#ifndef GHAL_COMPOSITOR_H
#define GHAL_COMPOSITOR_H
#pragma once
#include "ghal.h"
#include "ghal_ipc.h"

#define GCOMP_MAX_WINDOWS 64
#define GCOMP_IPC_TIMEOUT 1000  /* ms */

typedef struct {
    GHalWindow  wins[GCOMP_MAX_WINDOWS];
    int         count;
    uint32_t    focused_id;
    uint32_t    last_vsync_time;    /* microseconds */
    uint32_t    damage_flags[GCOMP_MAX_WINDOWS];  /* dirty regions */
} GCompositor;

/* Core compositor API (identical everywhere) */
int  gcomp_init(GCompositor *c);
int  gcomp_tick(GCompositor *c);      /* Main event loop: damage, composite, vsync, ipc */
void gcomp_shutdown(GCompositor *c);

/* Window registry (via IPC) */
int  gcomp_win_register(GCompositor *c, GHalWindow *w);
void gcomp_win_unregister(GCompositor *c, uint32_t id);
void gcomp_publish_registry(GHalWindow *w); /* → /dev/win/<id>/ */

/* IPC event dispatch (all platforms use this) */
int  gcomp_poll_ipc_event(GCompositor *c, GHalIpcMessage *msg);
int  gcomp_route_event(GCompositor *c, const GHalIpcMessage *msg);

/* Input dispatch */
void gcomp_send_input_to_window(uint32_t win_id, const GHalInputEvent *event);

#endif
```

### 4.3 `include/ghal_bc.h` — GALB bytecode (estensione HALB)

```c
/* include/ghal_bc.h */
/*
 * GALB — Graphics ABI Layer Bytecode
 * Estende HALB (nexs_hal_bc.h) con opcode grafici.
 * Magic: "GALB" v1.
 *
 * Tutti gli opcode HALB 0x00-0xFF restano validi.
 * Opcode grafici usano range 0x40-0x7F (non usati da HALB).
 *
 * Virtual device paths grafici:
 *   /dev/fb          → framebuffer raw (read/write pixel data)
 *   /dev/win/<id>    → finestra (ctl per resize/title/show/hide)
 *   /dev/gpu         → GPU command stream
 *   /dev/compositor  → notifiche compositor
 */

#define GALB_MAGIC      "GALB"
#define GALB_VERSION    1

typedef enum {
    /* Surface ops */
    GALO_SURF_CREATE  = 0x40,   /* width height fmt → surf_reg  */
    GALO_SURF_DESTROY = 0x41,   /* surf_reg                     */
    GALO_SURF_LOCK    = 0x42,   /* surf_reg → ptr               */
    GALO_SURF_UNLOCK  = 0x43,   /* surf_reg                     */
    GALO_SURF_PRESENT = 0x44,   /* win_reg surf_reg             */

    /* Window ops */
    GALO_WIN_OPEN     = 0x50,   /* title w h → win_reg          */
    GALO_WIN_CLOSE    = 0x51,   /* win_reg                      */
    GALO_WIN_TITLE    = 0x52,   /* win_reg title_str            */
    GALO_WIN_RESIZE   = 0x53,   /* win_reg w h                  */
    GALO_WIN_SHOW     = 0x54,   /* win_reg visible:u8           */

    /* 2D draw ops (software, su surface locked) */
    GALO_DRAW_RECT    = 0x60,   /* surf_reg x y w h color       */
    GALO_DRAW_LINE    = 0x61,   /* surf_reg x0 y0 x1 y1 color   */
    GALO_DRAW_BLIT    = 0x62,   /* dst_reg src_reg dx dy        */
    GALO_DRAW_TEXT    = 0x63,   /* surf_reg x y str color       */
    GALO_DRAW_CLEAR   = 0x64,   /* surf_reg color               */

    /* GPU command stream */
    GALO_GPU_BEGIN    = 0x70,   /* win_reg                      */
    GALO_GPU_SUBMIT   = 0x71,   /* data_str len                 */
    GALO_GPU_END      = 0x72,   /* win_reg                      */
    GALO_GPU_VSYNC    = 0x73,   /* wait for vblank              */
} GalOpcode;

/* Colore packed RGBA8 */
#define GHAL_RGBA(r,g,b,a) (((uint32_t)(r)<<24)|((uint32_t)(g)<<16)|((uint32_t)(b)<<8)|(a))
```

---

## 5. Backend: HOST macOS (MacBook Pro i9 2019)

**GPU disponibili:** Intel UHD 630 + AMD Radeon Pro 5500M
**API target:** Metal (via CAMetalLayer), OpenCL 1.2 (framework macOS)
**Finestre:** NSWindow + NSView (Objective-C in ghal_macos.m)

```objc
/* host/macos/ghal_macos.m — sketch */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

static int macos_win_open(GHalWindow *w) {
    NSRect frame = NSMakeRect(0, 0, w->width, w->height);
    NSWindow *nsw = [[NSWindow alloc]
        initWithContentRect:frame
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable
        backing:NSBackingStoreBuffered
        defer:NO];
    [nsw setTitle:[NSString stringWithUTF8String:w->title]];
    
    /* CAMetalLayer per accelerazione GPU */
    CAMetalLayer *layer = [CAMetalLayer layer];
    layer.device = MTLCreateSystemDefaultDevice();
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    [nsw.contentView setLayer:layer];
    [nsw.contentView setWantsLayer:YES];
    [nsw makeKeyAndOrderFront:nil];
    
    w->gpu_handle = (uintptr_t)CFBridgingRetain(nsw);
    gcomp_publish_registry(w);  /* → /dev/win/<id>/ */
    return 0;
}

static void macos_surface_present(GHalWindow *w, GHalSurface *s) {
    /* blit s->pixels → metal texture → CAMetalLayer drawable */
    ...
}

static GHalDriver s_macos_driver = {
    .name            = "metal",
    .init            = macos_init,
    .shutdown        = macos_shutdown,
    .win_open        = macos_win_open,
    .win_close       = macos_win_close,
    .surface_create  = macos_surface_create,
    .surface_present = macos_surface_present,
    .gpu_begin_frame = macos_gpu_begin,
    .gpu_submit      = macos_gpu_submit,
    .gpu_end_frame   = macos_gpu_end,
    .vsync           = macos_vsync,
};

__attribute__((constructor))
static void register_macos_ghal(void) { g_ghal_driver = &s_macos_driver; }
```

---

## 6. Backend: HOST Linux X11

**Stack:** xcb + EGL (mesa) + GLESv2 o GL3
**dlopen:** `libxcb.so`, `libEGL.so`, `libGL.so` — zero link statici

```c
/* host/linux_x11/ghal_x11.c — sketch */
#include <xcb/xcb.h>   /* header portatile POSIX */

static xcb_connection_t *g_xcb;
static xcb_window_t      g_root;

static int x11_init(void) {
    g_xcb = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(g_xcb)) return -1;
    /* pubblica /dev/gfx/backend = "x11-egl" */
    reg_set("/dev/gfx/backend", val_str("x11-egl"), RK_READ);
    return 0;
}

static int x11_win_open(GHalWindow *w) {
    xcb_window_t win = xcb_generate_id(g_xcb);
    xcb_create_window(g_xcb, XCB_COPY_FROM_PARENT, win, g_root,
        0, 0, w->width, w->height, 0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0, NULL);
    xcb_map_window(g_xcb, win);
    xcb_flush(g_xcb);
    w->gpu_handle = (uintptr_t)win;
    gcomp_publish_registry(w);
    return 0;
}
```

---

## 7. Backend: BAREMETAL — Unified Compositor + VirtIO Pure

**Key: Identical compositor code, unified IPC, abstracted VSYNC**

```
BAREMETAL ARCHITECTURE (Unified with Host):

App Process ──IPC (NEXS protocol)──┐
                                    ↓
         Compositor Service ─── GHalDriver
                 │                   │
                 └─ damage tracking ──┴─ VirtIO GPU / Linear FB
                 └─ Z-order compositing
                 └─ VSYNC timing (via timer interrupt)
                 └─ Input routing
```

**VirtIO GPU Pure** (no Mesa bloat):

```c
/* baremetal/virtio_gpu.c — minimal VirtIO driver */

static int virtio_gpu_init(void) {
    /* PCI probe for vendor 0x1af4, device 0x1050 */
    /* Map BAR0 for MMIO */
    /* Initialize control and cursor virtqueues */
    /* Query display info: GET_DISPLAY_INFO */
}

static int virtio_gpu_create_resource(uint32_t id, uint32_t fmt,
                                      uint32_t w, uint32_t h) {
    /* RESOURCE_CREATE_2D: allocate GPU resource */
    /* ATTACH_BACKING: assign host pages to resource */
}

static int virtio_gpu_transfer_to_host(uint32_t resource_id, 
                                       uint32_t x, uint32_t y, 
                                       uint32_t w, uint32_t h) {
    /* TRANSFER_TO_HOST_2D: GPU reads from host memory */
}

static int virtio_gpu_flush(uint32_t resource_id) {
    /* RESOURCE_FLUSH: notify GPU to display */
}

static GHalDriver s_virtio_driver = {
    .name = "virtio-gpu",
    .init = virtio_gpu_init,
    .get_display_size = virtio_gpu_get_display_size,
    .surface_create = virtio_gpu_surface_create,    /* allocate GPU resource */
    .surface_lock = virtio_gpu_surface_lock,        /* shadow CPU buffer */
    .surface_unlock = virtio_gpu_surface_unlock,    /* TRANSFER_TO_HOST + FLUSH */
    .surface_present = virtio_gpu_surface_present,  /* SET_SCANOUT + RESOURCE_FLUSH */
    .vsync = virtio_gpu_vsync,                      /* timer-based; configurable Hz */
    .gpu_begin_frame = virtio_gpu_begin_frame,      /* optional */
    .gpu_submit = virtio_gpu_submit,                /* optional: direct virgl cmdbuf */
    .poll_input = virtio_gpu_poll_input,            /* PS/2 / input handler */
};
```

**Fallback: Linear Framebuffer** (if VirtIO not available):

```c
/* baremetal/ghal_linear_fb.c — software rendering fallback */

static int linear_fb_init(void) {
    /* Discover framebuffer address (bootloader or BIOS) */
    /* Set up timer for VSYNC interrupts */
}

static GHalDriver s_linear_driver = {
    .name = "linear-fb",
    .surface_present = linear_fb_surface_present,  /* memcpy directly to VRAM */
    .vsync = linear_fb_vsync,                      /* timer-based VSYNC */
    .gpu_begin_frame = NULL,                        /* No GPU; software only */
};
```

**VSYNC Abstraction** (unified across all backends):

```c
/* VSYNC is NOT device-specific; it's a timing callback */

/* On Host macOS: native CADisplayLink or sleep(16.67ms) */
uint32_t macos_vsync(void) {
    /* Wait for CADisplayLink callback or timer */
    return 16667;  /* microseconds until next vsync */
}

/* On Baremetal: timer interrupt handler */
uint32_t timer_vsync(void) {
    /* Waits for timer interrupt (configurable Hz) */
    return (1000000 / vsync_hz);  /* microseconds until next vsync */
}

/* Compositor uses same interface everywhere */
void gcomp_tick(GCompositor *c) {
    /* Composite dirty regions */
    ghal_composite(c);
    
    /* Wait for VSYNC (implementation varies, but interface is same) */
    uint32_t us_until_next = ghal_driver->vsync();
    
    /* Schedule next frame */
    /* (on Host: callback triggers, on Baremetal: interrupt triggers) */
}
```

---

## 8. seL4: Compositor as Protected Domain (Unified Logic)

**Key: Same compositor code, but runs in isolated PD. IPC via Microkit channels.**

```
nexs_root PD ─ Microkit CH ─ compositor_pd (identical code!)
     ↑                              ↑
     └─ IPC NEXS wrapper ──────────┴─ GHalDriver
                                   └─ damage tracking
                                   └─ Z-order
     app PD ─────→ win_open() IPC ────→ compositor_pd
                                       ↓ surface_pd (allocator)
```

```c
/* sel4/compositor_pd.c — SAME compositor code as Host/Baremetal */
#include "ghal_compositor.h"
#include <microkit.h>

static GCompositor g_compositor = {0};

void notified(microkit_channel ch) {
    if (ch == CH_APP_WIN_REQUEST) {
        /* Receive IPC message via Microkit */
        GHalIpcMessage msg;
        microkit_ipc_recv(mr_app_to_comp, &msg);  /* wrapper over microkit_msginfo_recv */
        
        /* Route message through unified compositor logic */
        gcomp_route_event(&g_compositor, &msg);
        
        /* Send response back */
        microkit_ipc_send(CH_COMP_TO_APP, ...);
    }
    
    if (ch == CH_VSYNC_TIMER) {
        /* VSYNC interrupt: drive compositor tick */
        gcomp_tick(&g_compositor);
        microkit_notify(CH_COMP_TO_APPS);  /* wake all waiting apps */
    }
}

int main(void) {
    gcomp_init(&g_compositor);
    microkit_notify(CH_INIT_READY);
    return 0;
}
```

**IPC Bridge: Microkit channels wrap NEXS protocol**

```c
/* sel4/ipc_bridge.c — translate between Microkit and NEXS IPC */

/* On seL4, apps use NEXS sendmessage/receivemessage,
   which get translated to Microkit IPC under the hood */

int nexs_sendmessage(const char *dst_path, const void *msg, size_t len) {
    /* If dst_path is /dev/win/<id>/inbox */
    /* Route to appropriate Microkit channel */
    if (strncmp(dst_path, "/dev/win/", 9) == 0) {
        return microkit_ipc_send(CH_COMP_TO_APP, msg, len);
    }
}

int nexs_receivemessage(const char *src_path, void *msg, size_t len) {
    /* Block on Microkit channel until message arrives */
    return microkit_ipc_recv(src_path_to_channel(src_path), msg, len);
}
```

---

## 9. NEXS Language Integration — builtins grafici

**File:** `lang/ghal_builtins.c`

```c
/* win_open(title w h) → win_id */
static Value bi_win_open(Value *args, int n) {
    if (n < 3) return val_err(1, "win_open: need title w h");
    GHalWindow w = {0};
    strncpy(w.title, val_to_str(&args[0]), sizeof(w.title)-1);
    w.width  = (uint32_t)args[1].ival;
    w.height = (uint32_t)args[2].ival;
    int rc = ghal_win_open(&w, w.title, w.width, w.height);
    if (rc != 0) return val_err(rc, "win_open failed");
    return val_int(w.id);
}

/* draw_rect(win_id x y w h color) → 0 */
static Value bi_draw_rect(Value *args, int n) { ... }

/* surface_blit(win_id) → 0  (present) */
static Value bi_surface_blit(Value *args, int n) { ... }

/* gl_clear(r g b a) */
/* gl_triangle(x0 y0 x1 y1 x2 y2 color) */
/* compute_run(kernel_str args...) */

void ghal_register_builtins(void) {
    fn_register_builtin_sig("win_open",     bi_win_open,
        "win_open(title str, w int, h int) → int");
    fn_register_builtin_sig("win_close",    bi_win_close,
        "win_close(id int) → int");
    fn_register_builtin_sig("draw_rect",    bi_draw_rect,
        "draw_rect(win int, x int, y int, w int, h int, color int) → int");
    fn_register_builtin_sig("surface_blit", bi_surface_blit,
        "surface_blit(win int) → int");
    fn_register_builtin_sig("gl_clear",     bi_gl_clear,
        "gl_clear(r f, g f, b f, a f) → int");
    fn_register_builtin_sig("compute_run",  bi_compute_run,
        "compute_run(kernel str) → int");
}
```

**Esempio NEXS:** `example/hello_window.nx`

```nexs
# hello_window.nx
win = win_open("Hello GHAL" 800 600)
if win < 0 { out "error opening window"; halt }

# Colore RGBA packed: 0xFF4488FF = rosa
draw_rect(win 100 100 200 150 0xFF4488FF)
draw_rect(win 50  50  700 500 0x222222FF)
surface_blit(win)

sleep(3000)
win_close(win)
```

---

## 10. Registry namespace completo

```
/dev/win/                    → window directory
/dev/win/<id>/title          → str
/dev/win/<id>/width          → int
/dev/win/<id>/height         → int
/dev/win/<id>/visible        → int
/dev/win/<id>/zorder         → int   (stack order)
/dev/win/<id>/surface        → ptr   (GHalSurface*)
/dev/win/<id>/inbox          → IPC   (eventi: "key:A", "resize:800x600")
/dev/win/<id>/damage         → int   (1 = needs repaint)

/dev/gfx/backend             → str   ("metal"|"x11-egl"|"virtio-gpu"|"softpipe")
/dev/gfx/width               → int
/dev/gfx/height              → int
/dev/gfx/format              → str   ("BGRA8")
/dev/gfx/vsync_hz            → int
/dev/gfx/gpu_vendor          → str

/hal/gfx/modules/<name>/type   → str  (HAL_MOD_GFX = 7, nuovo tipo)
/hal/gfx/modules/<name>/state  → str  ("active"|"absent"|"error")

/sys/compositor/running      → int
/sys/compositor/focused      → int   (win id)
/sys/compositor/windows      → int   (count)
/sys/compositor/fps          → int   (misurato)
```

---

## 11. Unified Implementation Phases (Single Compositor Everywhere)

### Phase 0 — Repo skeleton + Unified Interfaces (1 giorno)
- [ ] `git init ghal && git submodule add <nexs-url> nexs`
- [ ] Struttura cartelle, Makefile con override intercept
- [ ] `include/ghal.h` — GHalDriver (unified, abstract VSYNC)
- [ ] `include/ghal_ipc.h` — NEXS IPC message protocol (new)
- [ ] `include/ghal_bc.h` — GALB opcodes
- [ ] `include/ghal_compositor.h` — unified compositor interface

### Phase 1 — Core HAL + Unified Compositor + IPC (3 giorni)
- [ ] **`compositor/compositor.c`** — SINGLE implementation, works everywhere
  - Damage tracking, Z-order, composite, VSYNC dispatch
  - IPC event routing (works on Host/Baremetal/seL4)
  - NOT platform-specific
- [ ] `compositor/window_registry.c` — /dev/win/<id>/ CRUD (platform-agnostic)
- [ ] `compositor/ipc_dispatcher.c` — route IPC messages (unified)
- [ ] `ghal_init()` — detect backend, initialize compositor
- [ ] `g_ghal_driver` singleton registration
- [ ] **IPC abstraction layer** (sendmessage/receivemessage wrappers)

**Key: Compositor code is 100% portable. Test on Host first.**

### Phase 2 — Host Backend (macOS / Linux) (2-3 giorni)
- [ ] `host/ghal_host.c` — auto-detect platform, dispatch to backend
- [ ] **macOS only:**
  - `host/macos/ghal_macos.m` — NSWindow, CAMetalLayer
  - VSYNC: CADisplayLink or sleep(16ms)
- [ ] **Linux X11 only:**
  - `host/linux_x11/ghal_x11.c` — xcb window, event loop
  - VSYNC: GLX SwapInterval or timer
- [ ] **Test:** Run same compositor code on both platforms (different VSYNC only)

### Phase 3 — Software Rendering + GALB VM (2 giorni)
- [ ] `lib/draw2d.c` — Bresenham, blit, fill (platform-independent)
- [ ] `lib/font.c` — 8×16 bitmap font (platform-independent)
- [ ] `bc/ghal_bc.c` — GALB bytecode executor (platform-independent)
- [ ] **Test:** Draw shapes, render bytecode (no GPU needed)

### Phase 4 — NEXS Builtins (1 giorno)
- [ ] `lang/ghal_builtins.c` — win_open, draw_rect, surface_blit, etc.
- [ ] Language integration (call compositor via IPC from scripts)
- [ ] **Test:** NEXS scripts run on Host, control compositor via IPC

### Phase 5 — Baremetal: Unified Compositor + VirtIO (3 giorni)
**This is where unified architecture shines:**
- [ ] **Use SAME compositor code from Phase 1** (no changes!)
- [ ] `baremetal/virtio_gpu.c` — VirtIO GPU driver (minimal)
- [ ] `baremetal/ghal_linear_fb.c` — Linear framebuffer fallback
- [ ] `baremetal/ipc_local.c` — IPC within same address space
- [ ] VSYNC: timer interrupt handler (configurable Hz)
- [ ] **Test:** QEMU with same NEXS scripts, compositor logic identical to Host

### Phase 6 — seL4 Integration (2-3 giorni)
**Again, SAME compositor code:**
- [ ] **Use SAME compositor code from Phase 1** (no changes!)
- [ ] `sel4/compositor_pd.c` — run compositor in protected domain
- [ ] `sel4/ipc_bridge.c` — Microkit ↔ NEXS IPC translation
- [ ] `sel4/surface_pd.c` — shared memory allocator
- [ ] **Test:** seL4 system with identical compositor and NEXS scripts

### Phase 7 — GPU Acceleration (Optional, future)
- [ ] `lib/draw3d.c` — OpenGL-compatible state machine
- [ ] `lib/compute.c` — OpenCL-style compute
- [ ] GPU paths in `host/macos/ghal_metal.m`, `baremetal/virtio_gpu.c`, etc.
- [ ] **Key: GPU is optional acceleration, not required**

---

## Key Insight: Compositor Code Reuse

```c
/* This code runs IDENTICALLY on Host, Baremetal, seL4 */

void gcomp_tick(GCompositor *c) {
    /* Damage tracking — same everywhere */
    for (int i = 0; i < c->count; i++) {
        if (c->wins[i].damage) {
            /* Composite window to framebuffer */
            /* Via GHalDriver (abstracted) — different on each platform */
        }
    }
    
    /* VSYNC — abstracted callback, works everywhere */
    uint32_t us_until_next = ghal_driver->vsync();
    
    /* IPC dispatch — identical interface everywhere */
    GHalIpcMessage msg;
    while (gcomp_poll_ipc_event(c, &msg) == 1) {
        gcomp_route_event(c, &msg);
    }
}
```

**No #ifdef, no platform-specific logic in compositor.**
**Platform differences isolated to GHalDriver implementations.**

---

## 12. Header portatili da scaricare (no reinvenzione)

```bash
# Standard POSIX — già presenti su sistema
# xcb (X11):
apt install libxcb-dev    # oppure brew install libxcb
# EGL:
apt install libegl-dev
# virtio-gpu (header kernel Linux — basta l'header, no linking):
# linux/virtio_gpu.h   copiato in baremetal/include/
# OpenCL (macOS già ce l'ha):
# /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/.../OpenCL/
# Mesa GL headers (solo header, per compatibilità ABI):
apt install libgl-dev
```

**Approccio:** tutti gli header vengono copiati in `vendor/include/`
e non richiediamo presenza sul sistema di build del target. Solo il
compile host (macOS/Linux) ha bisogno delle lib host.

---

## 13. Il `reimplementation/` pattern in dettaglio

```
reimplementation/
└── hal/
    └── hal_hosted.c    ← override di nexs/hal/hal_hosted.c

    # Aggiunge: ghal_init() bootstrap automatico su hosted
    # Quando nexs runtime parte su macOS, l'__attribute__((constructor))
    # di reimplementation/hal/hal_hosted.c viene chiamato PRIMA
    # di quello di nexs/hal/hal_hosted.c grazie all'ordine -I
    # → g_ghal_driver = &s_macos_driver viene settato prima di nexs_hal_init()
```

Makefile rule:

```makefile
# ghal/Makefile
NEXS_SRCS := $(shell find nexs -name '*.c' ! -path 'nexs/hal/hal_hosted.c')
REIMPL     := reimplementation/hal/hal_hosted.c
GHAL_SRCS  := compositor/compositor.c compositor/window_registry.c \
              host/ghal_host.c lib/draw2d.c lib/font.c \
              bc/ghal_bc.c lang/ghal_builtins.c

CFLAGS := -I reimplementation -I nexs/hal/include -I nexs/include \
          -I nexs/core/include -I include -I vendor/include

# Su macOS aggiunge il backend ObjC
ifeq ($(shell uname),Darwin)
  GHAL_SRCS += host/macos/ghal_macos.m host/macos/ghal_metal.m
  LDFLAGS   += -framework Cocoa -framework Metal -framework QuartzCore
endif
```

---

## 14. Note hardware — MacBook Pro Intel i9 2019

| Componente | Valore | Rilevanza GHAL |
|---|---|---|
| CPU | Intel Core i9-9880H (8c/16t) | RISC-V/amd64 emulation in QEMU |
| GPU integrata | Intel UHD Graphics 630 | OpenGL 4.6, Metal, OpenCL 1.2 |
| GPU discreta | AMD Radeon Pro 5500M 4GB | Metal, OpenCL 1.2, non attiva su ext display |
| RAM | 16-32 GB DDR4 | QEMU può avere 8GB per virtio-gpu |
| macOS | Ventura/Sonoma | Metal 3, CoreAnimation, XPC |

**QEMU virgl su macBook:**
```bash
# host: macOS con Metal
qemu-system-x86_64 \
  -device virtio-gpu-gl,virgl=on \
  -display sdl,gl=on \           # usa Metal via MoltenVK o CGL
  -m 512M -cpu host \
  -kernel build/baremetal-amd64/nexs.elf
```

Il path: `ghal baremetal → virtio-gpu cmd → QEMU virgl → Metal host → Intel UHD`
Questo ci dà accelerazione GPU reale anche su baremetal in QEMU.

---

## 15. Prossimo step immediato

**Strategia**: Compositor first, backends second. Compositor code is identical everywhere.

```bash
# Phase 0: Setup
mkdir ghal && cd ghal
git init
echo '[submodule "nexs"]
    path = nexs
    url = https://github.com/olmox001/base-nexs.git
    branch = dev-stable' > .gitmodules
git submodule add https://github.com/olmox001/base-nexs.git nexs

# Directories
mkdir -p include compositor/{include,ipc} lang bc lib
mkdir -p host/{macos,linux_x11} baremetal sel4
mkdir -p reimplementation/hal vendor/include test
```

**Ordine stretto (Critical Path):**

1. **Phase 0**: Header setup (`ghal.h`, `ghal_ipc.h`, `ghal_compositor.h`)
2. **Phase 1**: Unified compositor (single implementation)
   - **Write compositor code ONCE**
   - Test on Host (macOS) with thread-based IPC
   - No platform-specific code yet
3. **Phase 2**: Host backend (to validate compositor on real OS)
   - macOS: CAMetalLayer (test compositor with native window)
   - Linux: X11 (port same compositor binary)
4. **Phase 3**: Software rendering (no GPU required)
   - draw2d + GALB VM
5. **Phase 4**: NEXS builtins (scripts control compositor)
6. **Phase 5**: Baremetal (reuse EXACT same compositor code!)
   - VirtIO driver + compositor (no compositor rewrite)
7. **Phase 6**: seL4 (again, reuse EXACT same compositor code!)
   - Compositor PD + IPC bridge

**Why this works:**
- Compositor logic is platform-agnostic (handles events, composes windows, calls HAL)
- IPC protocol is the same on all platforms (NEXS sendmessage/receivemessage)
- VSYNC is abstract (callback in GHalDriver)
- GHalDriver implementations differ, but compositor calls them identically

**No code duplication. Single source of truth.**

---

## 16. GPU Acceleration Strategy

GPU is **optional and transparent**:

```c
/* In compositor or application */
if (ghal_driver->gpu_begin_frame != NULL && hardware_supports_gpu) {
    /* GPU path */
    ghal_driver->gpu_begin_frame(win);
    ghal_driver->gpu_submit(cmdbuf, len);
    ghal_driver->gpu_end_frame(win);
} else {
    /* Software fallback (always available) */
    draw2d_render(surf);
    ghal_driver->surface_present(win, surf);
}
```

**Per platform:**
- **Host macOS**: Metal (hardware accelerated, optional)
- **Host Linux**: Mesa/EGL (hardware or software, optional)
- **Baremetal**: VirtIO compute + GPU (optional), or software
- **seL4**: Software rendering (MVP), compute as separate PD (future)

No mandatory GPU dependency anywhere.
