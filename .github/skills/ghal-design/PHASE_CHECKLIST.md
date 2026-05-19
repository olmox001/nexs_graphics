# GHAL Implementation Checklist

Project: **[Project Name]**  
Start Date: **[YYYY-MM-DD]**  
Team Lead: **[Name]**  

---

## Phase 0: Repository Skeleton
**Duration:** 1 day  
**Status:** [ ] Not Started [ ] In Progress [ ] Complete  
**Owner:** [Name]

### Tasks
- [ ] Initialize repo: `git init && git submodule add <nexs-url> nexs`
- [ ] Create folder structure (see [directory template](./Makefile.template#L33))
  - [ ] `include/`, `compositor/`, `host/`, `baremetal/`, `sel4/`, `bc/`, `lib/`, `lang/`, `example/`
  - [ ] `reimplementation/hal/`
  - [ ] `vendor/include/`
- [ ] Write `include/ghal.h` with complete struct definitions
  - [ ] GHalSurface (id, dimensions, format, pixels, gpu_handle)
  - [ ] GHalWindow (id, title, size, surface, registry_path)
  - [ ] GHalDriver (function pointers, singleton)
  - [ ] Public API stubs (ghal_init, ghal_win_open, etc.)
- [ ] Write `include/ghal_bc.h`
  - [ ] GALB opcode enum (0x40-0x7F range)
  - [ ] GHAL_RGBA macro
  - [ ] Documentation of bytecode format
- [ ] Create `reimplementation/hal/hal_hosted.c`
  - [ ] Copy nexs/hal/hal_hosted.c
  - [ ] Add ghal_init() bootstrap
  - [ ] Constructor to register g_ghal_driver
- [ ] Write Makefile
  - [ ] `-I reimplementation` override rule
  - [ ] Platform detection (uname)
  - [ ] Verify `nexs/` submodule integrity check target

### Validation
- [ ] `nexs/` submodule is untouched: `git diff nexs/` returns nothing
- [ ] All header files compile without errors: `make verify-includes`
- [ ] Folder structure matches layout in GHAL_ATTACK_PLAN.md

**Deliverable:** Folder structure + ghal.h + Makefile (ready for Phase 1)

---

## Phase 1: Core HAL + Compositor Registry
**Duration:** 2 days  
**Status:** [ ] Not Started [ ] In Progress [ ] Complete  
**Owner:** [Name]

### Tasks
- [ ] Implement `ghal_init()`
  - [ ] Auto-detect platform from environment (uname, GHAL_BACKEND env var)
  - [ ] Load platform-specific hal_* backend
  - [ ] Call g_ghal_driver->init()
  - [ ] Publish /dev/gfx/ registry entries
- [ ] Implement `g_ghal_driver` singleton
  - [ ] Constructor function registered via __attribute__((constructor))
  - [ ] Pattern: sets g_ghal_driver = &platform_driver
- [ ] Create `compositor/window_registry.c`
  - [ ] `gcomp_win_register()` — create /dev/win/<id>/ and populate fields
  - [ ] `gcomp_win_unregister()` — remove /dev/win/<id>/
  - [ ] `gcomp_publish_registry()` — write window fields to registry
  - [ ] Auto-ID assignment (uint32_t counter starting at 1)
- [ ] Create `compositor/compositor.c`
  - [ ] `gcomp_init()` — allocate compositor state
  - [ ] `gcomp_tick()` — main loop (check damage, composite, flip)
  - [ ] `gcomp_send_event()` — route input via /dev/win/<id>/inbox
  - [ ] Damage tracking (dirty region tracking per window)
- [ ] Registry namespace validation
  - [ ] `/dev/win/<id>/title` — readable/writable string
  - [ ] `/dev/win/<id>/width`, `/dev/win/<id>/height` — int
  - [ ] `/dev/win/<id>/visible` — int (0/1)
  - [ ] `/dev/win/<id>/surface` — pointer
  - [ ] `/dev/gfx/backend` — string ("metal", "x11-egl", etc.)
  - [ ] `/dev/gfx/width`, `/dev/gfx/height` — int

### Tests
- [ ] `ghal_init()` succeeds on target platform
- [ ] `g_ghal_driver` is non-NULL and has correct name
- [ ] `gcomp_win_register()` creates registry entries
- [ ] Registry reads return correct values
- [ ] Two windows can be registered simultaneously

**Deliverable:** Compositor service + registry namespace working

---

## Phase 2: Host macOS Backend
**Duration:** 2 days  
**Status:** [ ] Not Started [ ] In Progress [ ] Complete  
**Owner:** [Name]

### Tasks
- [ ] Create `host/macos/ghal_macos.m`
  - [ ] `macos_init()` — initialize Cocoa, Metal
  - [ ] `macos_win_open()` — create NSWindow with CAMetalLayer
  - [ ] `macos_win_close()` — destroy window
  - [ ] `macos_surface_create()` — allocate MTLTexture
  - [ ] `macos_surface_present()` — blit to CAMetalLayer drawable
  - [ ] Constructor: `g_ghal_driver = &s_macos_driver`
- [ ] Create `host/macos/ghal_metal.m`
  - [ ] Metal command buffer setup
  - [ ] Texture upload from CPU buffer
  - [ ] Blit to drawable
  - [ ] Synchronization (fence wait)
- [ ] Update Makefile
  - [ ] Detect macOS: `-framework Cocoa -framework Metal -framework QuartzCore`
  - [ ] Add *.m compilation rules
  - [ ] Objective-C++ if needed

### Tests
- [ ] On macOS: `ghal_init()` succeeds
- [ ] NSWindow opens (title + size correct)
- [ ] `ghal_surface_create()` allocates Metal texture
- [ ] `ghal_surface_present()` displays red rectangle (test pattern)
- [ ] Window close doesn't crash

### Integration with Phase 1
- [ ] Compositor can call macOS backend functions
- [ ] Window registry integrates with NSWindow lifetime
- [ ] Window title changes reflected in /dev/win/<id>/title

**Deliverable:** macOS backend working; hello_window.nx runs

---

## Phase 3: Host Linux X11 Backend
**Duration:** 2 days  
**Status:** [ ] Not Started [ ] In Progress [ ] Complete  
**Owner:** [Name]

### Tasks
- [ ] Create `host/linux_x11/ghal_x11.c`
  - [ ] `x11_init()` — xcb_connect()
  - [ ] `x11_win_open()` — xcb_create_window(), xcb_map_window()
  - [ ] `x11_win_close()` — xcb_destroy_window()
  - [ ] Event loop (xcb_poll_for_event)
  - [ ] Constructor: register s_x11_driver
- [ ] Create `host/linux_x11/ghal_egl.c`
  - [ ] EGL context creation (pbuffer or X11 window)
  - [ ] Texture upload via glTexImage2D
  - [ ] Blit to framebuffer
- [ ] Dynamic library loading
  - [ ] dlopen("libxcb.so") with fallback paths
  - [ ] dlopen("libEGL.so")
  - [ ] Zero link-time dependencies (dlopen errors caught at runtime)
- [ ] Update Makefile
  - [ ] Detect Linux: `-ldl`
  - [ ] Add xcb, EGL compiler flags (if available on system)

### Tests
- [ ] On Linux: `ghal_init()` succeeds
- [ ] X11 window opens (title + size correct)
- [ ] Surface present displays correctly
- [ ] Window close doesn't crash
- [ ] Cross-compile from macOS: binary runs on Linux in VM

### Integration with Phase 1
- [ ] Window registry works with xcb_window_t handles
- [ ] Event dispatching to /dev/win/<id>/inbox (keyboard, mouse)

**Deliverable:** Linux X11 backend working; cross-compile validated

---

## Phase 4: draw2d + font + GALB VM
**Duration:** 2 days  
**Status:** [ ] Not Started [ ] In Progress [ ] Complete  
**Owner:** [Name]

### Tasks
- [ ] Create `lib/draw2d.c`
  - [ ] `draw_filled_rect()` — software rasterizer
  - [ ] `draw_line()` — Bresenham algorithm
  - [ ] `draw_blit()` — copy src to dst with clipping
  - [ ] `draw_clear()` — fill with color
  - [ ] Clipping: `set_clip_rect()`
- [ ] Create `lib/font.c`
  - [ ] Embedded 8×16 bitmap font (builtin glyph data)
  - [ ] `draw_text()` — render UTF-8 string
  - [ ] Character lookup + blit glyph to surface
- [ ] Create `bc/ghal_bc.c`
  - [ ] GALB bytecode executor (extends HALB interpreter)
  - [ ] Opcodes: `GALO_SURF_CREATE`, `GALO_WIN_OPEN`, `GALO_DRAW_RECT`, etc.
  - [ ] Stack machine for operands
  - [ ] Virtual device file reading/writing
- [ ] Create `bc/ghal_asm.c`
  - [ ] Assembler helpers (encode GALB bytecode to binary)
  - [ ] Test: assemble and execute simple draw commands

### Tests
- [ ] `draw_filled_rect()` draws correct pixels
- [ ] `draw_line()` is anti-aliased / smooth
- [ ] `draw_text()` renders readable text
- [ ] GALB VM can execute draw commands from bytecode
- [ ] Example: assembly code executes and displays red rectangle

**Deliverable:** Software rasterizer + bytecode VM working

---

## Phase 5: NEXS Language Integration
**Duration:** 1 day  
**Status:** [ ] Not Started [ ] In Progress [ ] Complete  
**Owner:** [Name]

### Tasks
- [ ] Create `lang/ghal_builtins.c`
  - [ ] `bi_win_open()` — builtin function
  - [ ] `bi_win_close()`
  - [ ] `bi_draw_rect()`
  - [ ] `bi_surface_blit()`
  - [ ] `bi_gl_clear()`, `bi_gl_triangle()` (stubs OK for now)
  - [ ] Error handling
- [ ] Create `lang/ghal_fn_table.c`
  - [ ] `ghal_register_builtins()` — calls fn_register_builtin_sig() for each
  - [ ] Init function called from runtime startup
- [ ] Update `reimplementation/runtime/main.c`
  - [ ] Call `ghal_init()` on startup
  - [ ] Call `ghal_register_builtins()`

### Integration
- [ ] Builtin function signatures match NEXS value type system
- [ ] Error codes propagate correctly
- [ ] Registry access works from builtins (read /dev/win/<id>/*)

### Tests
- [ ] NEXS script: `win = win_open("Test" 800 600)`
- [ ] Script: `draw_rect(win 10 10 100 100 0xFF0000FF)` (red)
- [ ] Script: `surface_blit(win)` (display)
- [ ] Script: `win_close(win)` (cleanup)

**Deliverable:** hello_window.nx runs end-to-end

---

## Phase 6: Baremetal virtio-gpu Driver
**Duration:** 3 days  
**Status:** [ ] Not Started [ ] In Progress [ ] Complete  
**Owner:** [Name]

### Tasks
- [ ] Create `baremetal/virtio_gpu.c`
  - [ ] VirtIO device discovery (PCI BAR)
  - [ ] Command ring setup (virtio_gpu_ctrl_hdr)
  - [ ] `virtio_gpu_get_display_info()` — query screen size
  - [ ] `virtio_gpu_create_resource_2d()` — allocate GPU resource
  - [ ] `virtio_gpu_transfer_to_host_2d()` — upload framebuffer
  - [ ] `virtio_gpu_set_scanout()` — attach resource to display
  - [ ] `virtio_gpu_resource_flush()` — update display
  - [ ] IRQ handling (if needed)
- [ ] Create `baremetal/compositor_bare.c`
  - [ ] Software damage composite (CPU-only rendering)
  - [ ] Main loop: read /dev/draw, composite, flush
  - [ ] No window manager (single fullscreen surface for now)
- [ ] QEMU test setup
  - [ ] `-device virtio-gpu-gl,virgl=on`
  - [ ] `-display sdl,gl=on`
  - [ ] Verify virgl translates commands to host GPU

### Tests
- [ ] QEMU boots with virtio-gpu device
- [ ] ghal_init() succeeds on baremetal target
- [ ] ghal_surface_create() allocates GPU resource
- [ ] ghal_surface_present() flushes to virgl
- [ ] Display shows rendered output (color gradient test)
- [ ] Framerate ≥ 30 FPS (measure with /sys/compositor/fps)

**Deliverable:** Baremetal virtio-gpu working in QEMU

---

## Phase 7: seL4 Protection Domains
**Duration:** 2 days  
**Status:** [ ] Not Started [ ] In Progress [ ] Complete  
**Owner:** [Name]

### Tasks
- [ ] Create `sel4/compositor_pd.c`
  - [ ] Microkit PD definition (notified handler)
  - [ ] IPC channel setup (CH_APP_WIN_REQUEST, CH_COMP_TO_APP, etc.)
  - [ ] Window registry state machine
  - [ ] Shared memory allocation (framebuffer MR)
- [ ] Create `sel4/surface_pd.c`
  - [ ] Allocate page-mapped memory regions
  - [ ] Grant pages to compositor PD
  - [ ] Framebuffer descriptor structure
- [ ] Create `sel4/ghal_sel4.c`
  - [ ] GHalDriver stubs (weak implementation for seL4)
  - [ ] IPC calls to compositor PD
  - [ ] Pattern: same as nexs/hal/hal_sel4.c

### Tests
- [ ] seL4 builds for aarch64, amd64, riscv64
- [ ] Compositor PD initializes
- [ ] App PD can request window via IPC
- [ ] Framebuffer MR is page-mapped correctly
- [ ] No capability violations

**Deliverable:** seL4 PDs integrated; IPC-based window management working

---

## Phase 8: draw3d + compute (Future)
**Duration:** TBD  
**Status:** [ ] Not Started [ ] In Progress [ ] Complete  
**Owner:** [Name]

### Tasks (Planned, not yet implemented)
- [ ] Create `lib/draw3d.c`
  - [ ] OpenGL state machine (vertex buffers, shaders)
  - [ ] Rasterization pipeline
  - [ ] Texture mapping
- [ ] Create `lib/compute.c`
  - [ ] OpenCL-style kernel dispatch
  - [ ] Thread pool or GPU submission
- [ ] Language builtins: `gl_triangle()`, `compute_run()`

### Tests
- [ ] 3D triangle renders correctly
- [ ] Compute kernel produces expected results

**Deliverable:** Advanced graphics capabilities

---

## Cross-Phase Validation

### Registry Integrity
- [ ] All windows appear in /dev/win/
- [ ] Registry fields remain in sync with internal state
- [ ] IPC event delivery works
- [ ] No registry corruption under stress

### Performance
- [ ] Frame rate ≥ 30 FPS on all backends
- [ ] Memory usage < 50 MB (typical)
- [ ] No memory leaks (valgrind, seL4 benchmark)

### Code Quality
- [ ] Zero compiler warnings (on primary target)
- [ ] -Werror -Wall enabled
- [ ] All public functions documented (doxygen)
- [ ] Test coverage > 60%

---

## Sign-Off

**Project Complete:** [ ] Yes [ ] No  

Lead Approval: _________________ Date: _______  
QA Sign-Off: _________________ Date: _______  

---

## Appendix: Platform Compatibility Matrix

| Phase | macOS | Linux | Baremetal | seL4 |
|-------|-------|-------|-----------|------|
| 0-1   | ✓     | ✓     | ✓         | ✓    |
| 2     | ✓     | -     | -         | -    |
| 3     | -     | ✓     | -         | -    |
| 4     | ✓     | ✓     | ✓         | ✓    |
| 5     | ✓     | ✓     | ✓         | ✓    |
| 6     | -     | -     | ✓         | -    |
| 7     | -     | -     | -         | ✓    |
| 8     | ○     | ○     | ○         | ○    |

**Key:** ✓ = Done, ○ = Future, - = N/A
