# GHAL — Graphics Hardware Abstraction Layer
## Architectural Design & Unified Attack Plan — Unified Baremetal Support

This document defines the core architecture, interfaces, and integration details for the **NEXS Graphics Subsystem (nexs_graphics)**. It establishes the **Graphics Hardware Abstraction Layer (GHAL)** and the **Graphics ABI Layer Bytecode (GALB)** to provide absolute platform parity across Host (macOS, Linux), Baremetal (amd64, arm64, riscv64), and microkernel (seL4) environments.

---

## 1. Core Architectural Pillars

Based on our design rules, we enforce two critical architectural paradigms:

### A. Uniform C, C++, OpenGL, and CUDA Support
Applications must be able to compile and run using standard C and C++ toolchains, targeting industry-standard graphics (OpenGL/Mesa) and general-purpose compute (CUDA/OpenCL-style) APIs uniformly across all environments:
1.  **C/C++ Standard Integration**: Public GHAL APIs are fully exposed through clean, linkable C linkage (`extern "C"`), making them directly importable by standard compilers.
2.  **OpenGL/Mesa State Machine**: The standard graphics library is modeled after the OpenGL state machine. Apps use standardized vertex, index, texture, and shader buffers.
3.  **CUDA/Compute Pipeline**: Parallel compute kernels (OpenCL/CUDA compatible C ABI) run uniformly. They map to Host pipelines (Metal Compute on macOS, EGL/GL Compute on Linux) and virtualized pipelines (VirGL or software compute queues on Baremetal/seL4).

### B. No Redundant Software 2D Stack Replication
We strictly avoid writing basic software 2D draw libraries and duplicating custom compositors on CPU for every single platform. Instead, we structure a clean, highly optimized pipeline:

```
┌────────────────────────────────────────────────────────┐
│               UNIFIED WINDOWS & COMPOSITOR             │
│   (Manages textures/surfaces produced by std libs)    │
├────────────────────────────────────────────────────────┤
│          STANDARD GRAPHICS LIBRARIES (OpenGL/Mesa)      │
│   (State machine, shader compilers, render pipelines)  │
├────────────────────────────────────────────────────────┤
│          NEXS OPTIMIZED HARDWARE HAL (GHalDriver)      │
│   (Uniform display, native VSYNC, hardware GPU hooks)  │
└────────────────────────────────────────────────────────┘
```

This ensures:
*   The Compositor composites **completed textures** (surfaces) produced directly by standard graphics libraries (OpenGL/Mesa) or GPU contexts.
*   We leverage the optimized NEXS HAL (`base-nexs` core structures) as the hardware baseline, using it to route framebuffers and events with optimal performance rather than custom slow pixel copies.

---

## 2. Submodule Purity & The `reimplementation/` Pattern

This project is organized as an independent repository that treats the main NEXS operating system as a read-only Git submodule.

> ⚠️ **MANDATORY RULES FOR GHAL**:
> 1. **Purity**: No files inside the `base-nexs/` submodule are ever modified.
> 2. **Shadow Override**: All customization, graphics additions, or overrides to the core operating system go inside the `reimplementation/` directory, mirroring the exact paths of the original files.
> 3. **Compilation Priority**: The build system is configured to search the `reimplementation/` paths first.

### Compiler Intercept Rule
The `Makefile` prepends `-I reimplementation/` to the include search path, forcing the compiler to find our customized headers and implementations before the clean submodule originals:

```makefile
CFLAGS += -I reimplementation -I base-nexs/hal/include -I base-nexs/include \
          -I base-nexs/core/include -I base-nexs/kernel/include
```

---

## 3. Platform Comparison Matrix

### Same Compositor, Different Platform Backend

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                          COMPOSITOR (Identical Code)                            │
│                         gcomp_tick() / gcomp_route_event()                      │
│                              IPC dispatch                                        │
│                              Damage tracking                                     │
│                              Z-order compositing                                │
└─────────────────────────────────────────────────────────────────────────────────┘
       ↓ ghal_driver→vsync()      ↓ ghal_driver→surface_present()
       
┌──────────────────────┬──────────────────────┬──────────────────────┐
│  HOST (macOS/Linux)  │   BAREMETAL (QEMU)   │  seL4 (Microkernel)  │
├──────────────────────┼──────────────────────┼──────────────────────┤
│ Compositor: Thread   │ Compositor: Task     │ Compositor: PD       │
│ IPC: NEXS native     │ IPC: same-addr-space │ IPC: Microkit CH     │
│ VSYNC: native callback│ VSYNC: timer        │ VSYNC: timer         │
│ Window: NSWindow/Xcb │ Window: registry     │ Window: registry     │
│ GPU: Metal/GL (opt)  │ GPU: VirtIO (opt)    │ GPU: none (MVP)      │
│ Surface: GPU buffer  │ Surface: RAM buffer  │ Surface: shared MR   │
└──────────────────────┴──────────────────────┴──────────────────────┘
```

### Code Reuse Map
*   **✅ 100% Shared Portable Code**:
    *   `compositor/compositor.c` — Event routing, layer composition, and damage rendering.
    *   `compositor/window_registry.c` — Virtual `/dev/win/` namespace provider (Plan 9 format).
    *   `lib/draw2d.c` — Optimized vector rendering, blitting, and pixel manipulation.
    *   `bc/ghal_bc.c` — GALB bytecode interpreter and state execution.
    *   `lang/ghal_builtins.c` — Language-level bindings and builtin evaluation.
*   **🔧 Target-Specific HAL Drivers (Layer 1)**:
    *   `host/macos/ghal_macos.m` — Cocoa `NSWindow` + `CAMetalLayer` backend (Objective-C).
    *   `host/linux_x11/ghal_x11.c` — `xcb` X11 window loop.
    *   `host/linux_x11/ghal_egl.c` — EGL context mapping on Mesa graphics.
    *   `baremetal/virtio_gpu.c` — Direct VirtIO-GPU hardware driver.
    *   `baremetal/ghal_linear_fb.c` — Direct linear pixel buffer mapping.
    *   `sel4/compositor_pd.c` — seL4 Protection Domain entrypoint wrapping the compositor core.

---

## 4. GHAL Directory Structure

```
nexs_graphics/
├── .gitmodules                         # base-nexs → olmox001/base-nexs
├── base-nexs/                          # Clean Git Submodule (READ-ONLY)
├── reimplementation/                   # Custom Override Layer (mirrors base-nexs)
│   └── hal/
│       └── hal_hosted.c               # Automatic windowing bootstrap on macOS/Linux
├── Makefile
├── include/
│   ├── ghal.h                         # Core GHalDriver, GHalSurface, and GHalWindow
│   ├── ghal_compositor.h              # Compositor interface, layer management
│   ├── ghal_surface.h                 # Framebuffers, texture formats
│   ├── ghal_2d.h                      # Vector drawing (lines, rects, text)
│   ├── ghal_3d.h                      # OpenGL/Mesa-compatible state definitions
│   ├── ghal_compute.h                 # OpenCL/CUDA-compatible compute C ABI
│   ├── ghal_ipc.h                     # Graphics NEXS IPC protocol structures
│   └── ghal_bc.h                      # GALB bytecode VM definitions
├── compositor/
│   ├── compositor.c                   # Frame tick, damage composition, and input routing
│   ├── window_registry.c              # Plan 9 virtual /dev/win/ namespace logic
│   └── include/
│       └── compositor_internal.h
├── host/
│   ├── ghal_host.c                    # Platform dispatcher (macOS vs Linux X11)
│   ├── macos/
│   │   ├── ghal_macos.m               # NSWindow wrapper + CAMetalLayer (ObjC)
│   │   └── ghal_metal.m               # MTLCommandBuffer + MTLTexture render backend
│   └── linux_x11/
│       ├── ghal_x11.c                 # Minimal xcb + xkb integration
│       └── ghal_egl.c                 # EGL context + GLESv2/GL3 binding via Mesa
├── baremetal/
│   ├── virtio_gpu.c                   # Minimal VirtIO-GPU v2 driver (VirGL compatible)
│   ├── drm_stub.c                     # KMS modesetting compatibility interface
│   └── compositor_bare.c             # Baremetal software fallback setup
├── sel4/
│   ├── compositor_pd.c               # seL4 Protection Domain wrapper
│   ├── surface_pd.c                  # Shared Memory allocator PD
│   └── ghal_sel4.c                   # Target driver stubs
├── bc/
│   ├── ghal_bc.c                     # GALB bytecode runner
│   └── ghal_asm.c                    # Runtime assembler tools
├── lib/
│   ├── draw2d.c                      # Software rendering library
│   ├── draw3d.c                      # OpenGL-compatible render pipeline / state machine
│   ├── font.c                        # Standard built-in 8x16 bitmap font engine
│   └── compute.c                     # OpenCL/CUDA-style C compute dispatch
├── lang/
│   ├── ghal_builtins.c               # NEXS graphics interpreter builtins
│   └── ghal_fn_table.c               # Builtin routing table registration
├── vendor/
│   └── include/                       # Portable downloaded POSIX headers (xcb, egl, virtio)
└── example/
    ├── hello_window.nx                # Basic graphics script
    ├── triangle.nx                    # Vector rasterization sample
    └── compositor_demo.nx             # Multiple overlapping windows demo
```

---

## 5. Core Technical Specifications & C Interfaces

### 5.1 GHAL IPC Protocol (`include/ghal_ipc.h`)
Used universally across all platforms. Packages drawing and window operations over standard NEXS `sendmessage`/`receivemessage` boundaries.

```c
#ifndef GHAL_IPC_H
#define GHAL_IPC_H

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
    uint8_t  payload[512];   /* Variable payload size */
} GHalIpcMessage;

/* IPC abstractions mapping to NEXS core send/recv */
int ghal_ipc_send(const char *dst_path, const GHalIpcMessage *msg);
int ghal_ipc_recv(GHalIpcMessage *msg, int timeout_ms);

#endif /* GHAL_IPC_H */
```

### 5.2 Core GHAL Objects & Unified Driver (`include/ghal.h`)

```c
#ifndef GHAL_H
#define GHAL_H
#pragma once

#include "base-nexs/hal/include/nexs_hal.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Pixel format constants */
#define GHAL_FMT_RGBA8      0
#define GHAL_FMT_BGRA8      1
#define GHAL_FMT_RGB565     2
#define GHAL_FMT_XRGB8      3

/* Macro: pack color to RGBA8 */
#define GHAL_RGBA(r, g, b, a) \
    (((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | \
     ((uint32_t)(b) << 8) | (a))

/* GHalSurface: unified memory map wrapper */
typedef struct {
    uint32_t    id;              /* Unique surface ID */
    uint32_t    width, height;   /* Dimensions in pixels */
    uint32_t    stride;          /* Bytes per row */
    uint32_t    format;          /* GHAL_FMT_* */
    
    void       *pixels;          /* CPU-accessible pixel data (NULL if GPU-only) */
    size_t      pixel_size;      /* Total size in bytes */
    
    uintptr_t   gpu_handle;      /* Opaque GPU resource reference */
    bool        gpu_dirty;       /* CPU modified, GPU synchronization flag */
} GHalSurface;

/* GHalWindow: viewport metadata */
typedef struct {
    uint32_t      id;             /* Unique window ID */
    char          title[128];     /* Window title string */
    uint32_t      width, height;  /* Window size in pixels */
    
    GHalSurface  *surface;        /* Associated drawing surface */
    
    bool          visible;        /* Window visibility state */
    uint32_t      zorder;         /* Stack position */
    
    char          reg_path[64];   /* Registry path: /dev/win/<id> */
    uintptr_t     native_handle;  /* Native window handle reference */
} GHalWindow;

/* GHalDriver: Core abstract target interface */
typedef struct {
    const char *name;          /* "metal", "x11-egl", "virtio-gpu", "softpipe" */
    
    /* Lifecycle */
    int  (*init)(void);
    void (*shutdown)(void);
    
    /* Display Queries */
    void (*get_display_size)(uint32_t *width, uint32_t *height);
    uint32_t (*preferred_format)(void);
    
    /* Window Ops (Host only) */
    int  (*win_open)(GHalWindow *w);
    void (*win_close)(GHalWindow *w);
    void (*win_set_title)(GHalWindow *w, const char *title);
    void (*win_resize)(GHalWindow *w, uint32_t new_width, uint32_t new_height);
    
    /* Surface Ops */
    int  (*surface_create)(GHalSurface *s, uint32_t w, uint32_t h, uint32_t fmt);
    void (*surface_destroy)(GHalSurface *s);
    int  (*surface_lock)(GHalSurface *s);      /* CPU lock */
    void (*surface_unlock)(GHalSurface *s);    /* CPU unlock */
    void (*surface_present)(GHalWindow *w, GHalSurface *s);  /* Flush rendering to viewport */
    
    /* GPU Commands Submission */
    int  (*gpu_begin_frame)(GHalWindow *w);
    void (*gpu_submit)(const void *cmdbuf, size_t len);
    void (*gpu_end_frame)(GHalWindow *w);
    
    /* Display Synchronization */
    void (*vsync)(void);   /* Waits for vertical blanking interval */
} GHalDriver;

extern GHalDriver *g_ghal_driver;

/* Public API */
NEXS_API int ghal_init(void);
NEXS_API void ghal_shutdown(void);
NEXS_API void ghal_get_display_size(uint32_t *width, uint32_t *height);

NEXS_API int ghal_win_open(GHalWindow *w, const char *title, uint32_t width, uint32_t height);
NEXS_API void ghal_win_close(GHalWindow *w);
NEXS_API void ghal_win_set_title(GHalWindow *w, const char *title);
NEXS_API void ghal_win_resize(GHalWindow *w, uint32_t width, uint32_t height);

NEXS_API int ghal_surface_create(GHalSurface *s, uint32_t w, uint32_t h, uint32_t fmt);
NEXS_API void ghal_surface_destroy(GHalSurface *s);
NEXS_API int ghal_surface_lock(GHalSurface *s);
NEXS_API void ghal_surface_unlock(GHalSurface *s);
NEXS_API void ghal_surface_present(GHalWindow *w, GHalSurface *s);

NEXS_API int ghal_gpu_begin_frame(GHalWindow *w);
NEXS_API void ghal_gpu_submit(const void *cmdbuf, size_t len);
NEXS_API void ghal_gpu_end_frame(GHalWindow *w);
NEXS_API void ghal_vsync(void);

#endif /* GHAL_H */
```

### 5.3 GHAL Compositor Specifications (`include/ghal_compositor.h`)
The identical compositor code handles window bounds, damage regions, and schedules flushes.

```c
#ifndef GHAL_COMPOSITOR_H
#define GHAL_COMPOSITOR_H
#pragma once

#include "ghal.h"
#include "ghal_ipc.h"

#define GCOMP_MAX_WINDOWS 64

typedef struct {
    GHalWindow  wins[GCOMP_MAX_WINDOWS];
    int         count;
    uint32_t    focused_id;
    uint32_t    last_vsync_time;
    uint32_t    damage_flags[GCOMP_MAX_WINDOWS];
} GCompositor;

int  gcomp_init(GCompositor *c);
int  gcomp_tick(GCompositor *c); /* Main event and drawing loop */
void gcomp_shutdown(GCompositor *c);

int  gcomp_win_register(GCompositor *c, GHalWindow *w);
void gcomp_win_unregister(GCompositor *c, uint32_t id);
void gcomp_publish_registry(GHalWindow *w);

int  gcomp_poll_ipc_event(GCompositor *c, GHalIpcMessage *msg);
int  gcomp_route_event(GCompositor *c, const GHalIpcMessage *msg);
void gcomp_send_input_to_window(uint32_t win_id, const GHalInputEvent *event);

#endif /* GHAL_COMPOSITOR_H */
```

### 5.4 GALB Bytecode Machine Specification (`include/ghal_bc.h`)
Extends NEXS's HALB interpreter. All standard HALB opcodes (0x00–0x3F) remain active. Graphical instructions utilize the non-overlapping `0x40`–`0x7F` address range.

```c
#ifndef GHAL_BC_H
#define GHAL_BC_H

#define GALB_MAGIC      "GALB"
#define GALB_VERSION    1

typedef enum {
    /* Surface Alloc & Presentation */
    GALO_SURF_CREATE  = 0x40,   /* width height fmt → surf_reg  */
    GALO_SURF_DESTROY = 0x41,   /* surf_reg                     */
    GALO_SURF_LOCK    = 0x42,   /* surf_reg → ptr               */
    GALO_SURF_UNLOCK  = 0x43,   /* surf_reg                     */
    GALO_SURF_PRESENT = 0x44,   /* win_reg surf_reg             */

    /* Window Controls */
    GALO_WIN_OPEN     = 0x50,   /* title w h → win_reg          */
    GALO_WIN_CLOSE    = 0x51,   /* win_reg                      */
    GALO_WIN_TITLE    = 0x52,   /* win_reg title_str            */
    GALO_WIN_RESIZE   = 0x53,   /* win_reg w h                  */
    GALO_WIN_SHOW     = 0x54,   /* win_reg visible_flag:u8      */

    /* 2D Software Graphics primitives (Draw on locked surface CPU buffer) */
    GALO_DRAW_RECT    = 0x60,   /* surf_reg x y w h color_packed*/
    GALO_DRAW_LINE    = 0x61,   /* surf_reg x0 y0 x1 y1 color   */
    GALO_DRAW_BLIT    = 0x62,   /* dst_reg src_reg dx dy        */
    GALO_DRAW_TEXT    = 0x63,   /* surf_reg x y str color       */
    GALO_DRAW_CLEAR   = 0x64,   /* surf_reg color               */

    /* Direct GPU Pipelines (Optional) */
    GALO_GPU_BEGIN    = 0x70,   /* win_reg                      */
    GALO_GPU_SUBMIT   = 0x71,   /* data_str len                 */
    GALO_GPU_END      = 0x72,   /* win_reg                      */
    GALO_GPU_VSYNC    = 0x73,   /* wait for display vblank      */
} GalOpcode;

#endif /* GHAL_BC_H */
```

---

## 6. Target Hardware Specifics & Acceleration Options

### 6.1 Host Target Details
*   **Target Machine**: MacBook Pro 16-inch 2019
    *   **CPU**: Intel Core i9-9880H (8 Cores, 16 Threads)
    *   **GPUs**: Integrated Intel UHD Graphics 630 + Discrete AMD Radeon Pro 5500M (4GB GDDR6 VRAM)
*   **macOS Backend**:
    *   Creates native Cocoa UI loops with Objective-C (`host/macos/ghal_macos.m`).
    *   Draws directly using GPU-backed `CAMetalLayer` rendering.
    *   VSYNC is driven natively by `CADisplayLink` (refresh ticks matches screen refresh).
*   **Linux Backend**:
    *   Uses native X11 (`xcb`) connection mapping.
    *   Connects Mesa hardware acceleration using the EGL state context.
    *   Forces VSYNC timing using OpenGL EGL `eglSwapInterval`.

### 6.2 Baremetal Virtualization & VirtIO-GPU Target
On raw hardware, GHAL acts as the direct VirtIO-GPU device controller inside the virtual system:
1.  **Probe**: Scans the virtual PCI bus for Vendor ID `0x1af4` and Device ID `0x1050`.
2.  **MMIO Map**: Assigns memory access to BAR0 for VirtIO control registers.
3.  **Command Execution**: Sends standard GPU resource requests:
    *   `VIRTIO_GPU_CMD_RESOURCE_CREATE_2D`: Setup the back-buffer object.
    *   `VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING`: Bind allocated system RAM pages.
    *   `VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D`: Signal the GPU to fetch the drawn buffer.
    *   `VIRTIO_GPU_CMD_RESOURCE_FLUSH`: Request update on the host screen.
4.  **VirGL Accelerated Path (Optional)**: Emits direct OpenGL-like command buffers through QEMU to be computed on the MacBook's physical Intel UHD/Radeon Pro GPU, delivering hardware-accelerated baremetal rendering.
    *   *QEMU activation:*
        ```bash
        qemu-system-x86_64 -device virtio-gpu-gl,virgl=on -display sdl,gl=on -m 512M -kernel build/baremetal-amd64/nexs.elf
        ```

### 6.3 seL4 Microkernel Target
*   **Protection Domains**: The software Compositor and G-HAL run securely within their own isolated Protection Domain (PD) called `compositor_pd`.
*   **IPC Interception**: Under the hood, standard `sendmessage`/`receivemessage` calls targeting `/dev/win/` are translated by the seL4 microkernel layer (`sel4/ipc_bridge.c`) directly to high-speed **Microkit Channels**.
*   **Memory Allocator**: A separate `surface_pd` service dynamically maps shared physical framebuffers (Shared Memory Regions) between client applications and the compositor, ensuring zero-copy display updates.

---

## 7. GHAL Virtual Registry Tree Specifications
To maintain Plan 9 system conventions, all graphical devices are mounted directly in the virtual file namespace:

```
/dev/win/                    → Directory listing active window handles
/dev/win/<id>/title          → Window Title [Read/Write string]
/dev/win/<id>/width          → Viewport width [Read-only integer]
/dev/win/<id>/height         → Viewport height [Read-only integer]
/dev/win/<id>/visible        → Viewport visibility state [Read/Write integer: 1 or 0]
/dev/win/<id>/zorder         → Layer depth value [Read/Write integer]
/dev/win/<id>/surface        → Pointer reference to GHalSurface [Read-only]
/dev/win/<id>/inbox          → Event communication buffer (routes "key:A", "mouse:x,y")
/dev/win/<id>/damage         → Repaint signal [Read/Write integer: 1 if dirty]

/dev/gfx/backend             → Active graphics pipeline [Read-only string: e.g., "metal", "virtio-gpu"]
/dev/gfx/width               → Display horizontal resolution [Read-only]
/dev/gfx/height              → Display vertical resolution [Read-only]
/dev/gfx/format              → Active display format [Read-only string: "BGRA8"]
/dev/gfx/vsync_hz            → Target frame refresh frequency [Read-only]
/dev/gfx/gpu_vendor          → Detected GPU vendor name [Read-only]

/hal/gfx/modules/<name>/type   → Device type identification [Read-only: HAL_MOD_GFX = 7]
/hal/gfx/modules/<name>/state  → Driver operation status [Read-only: "active", "absent", "error"]

/sys/compositor/running      → Compositor daemon status [Read-only: 1 or 0]
/sys/compositor/focused      → Current active window identifier [Read/Write]
/sys/compositor/windows      → Count of active window registers [Read-only]
/sys/compositor/fps          → Measured system composition speed [Read-only]
```

---

## 8. Unified Implementation Attack Plan

### Phase 0: Repository Skeleton
*   Configure the repository skeleton and place standard headers in `include/` and `vendor/include/`.
*   Initialize GHAL structs and driver singleton hooks.
*   Configure the `Makefile` with the `-I reimplementation/` override priorities.
*   **Validation**: Submodule purity checks.

### Phase 1: Core HAL + Compositor Registry
*   Implement `ghal_init()` detecting platform and loading drivers.
*   Implement the compositor state and registry namespace operations `/dev/win/<id>/`.
*   Validate multi-window setups.

### Phase 2: Host macOS Backend
*   Implement Cocoa/Metal windows (`NSWindow` + `CAMetalLayer`) in `host/macos/ghal_macos.m`.
*   Establish Metal texture buffering, uploads, and queue fences.
*   Enable CADisplayLink VSYNC loop timing.

### Phase 3: Host Linux X11 Backend
*   Build Linux X11 mapping (`host/linux_x11/ghal_x11.c`) with dynamic `dlopen` of XCB.
*   Configure dynamic EGL context bindings with Mesa fallback contexts.

### Phase 4: draw2d + font + GALB VM
*   Establish optimized drawing rasterizers (lines, rects, bitmap fonts) in `lib/draw2d.c` and `lib/font.c`.
*   Develop the GALB stack bytecode interpreter.

### Phase 5: NEXS Language Integration
*   Map graphics builtins (`win_open`, `draw_rect`, `surface_blit`) in `lang/ghal_builtins.c` to bind the graphics pipeline directly to standard C, C++, and NEXS interpreter evaluation.
*   Validate end-to-end hello_window.nx script ticks.

### Phase 6: Baremetal virtio-gpu Driver
*   Establish the VirtIO-GPU driver (`baremetal/virtio_gpu.c`) loading BAR registers under baremetal PCI maps.
*   Coordinate double-buffered screen refreshes and VirGL hardware-assisted commands.

### Phase 7: seL4 Protection Domains
*   Deploy `sel4/compositor_pd.c` to wrap the compositor core within a dedicated seL4 Protection Domain.
*   Translate system IPC routing directly to high-speed seL4 Microkit channels.
*   Build `sel4/surface_pd.c` to manage shared-memory window allocations.

### Phase 8: draw3d + compute (Unified Advanced Graphics)
*   Deploy standard C, C++, OpenGL, and CUDA-like compute bindings uniformly across all host, baremetal, and microkernel platforms.
*   Establish full OpenGL/Mesa-compatible 3D vertex shading state machine (`lib/draw3d.c`).
*   Implement universal OpenCL/CUDA compute dispatching pipelines (`lib/compute.c`) running over the CPU or mapped hardware compute queues.
