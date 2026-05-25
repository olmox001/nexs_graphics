# nexs_graphics — Work Summary
*Aggiornato: 2026-05-19*

---

## Architettura del progetto

```
nexs_graphics/
├── base-nexs/          ← submodule NEXS OS (READ-ONLY, non modificare mai)
├── reimplementation/   ← override selettivi di base-nexs (POSIX-safe)
│   ├── hal/hal_hosted.c        override del HAL hosted con bootstrap GHAL
│   └── kernel/include/termios.h  fix del termios baremetal per host POSIX
├── include/            ← header pubblici GHAL
├── compositor/         ← gestore finestre unificato (funziona su tutti i target)
├── host/               ← backend piattaforma
│   ├── macos/ghal_macos.m  + ghal_metal.m   (NSWindow + Metal)
│   └── linux_x11/          (X11 + EGL, Linux)
├── lib/                ← renderer software (draw2d, font)
├── bc/                 ← VM bytecode GALB (estende HALB con opcode grafici)
├── lang/               ← builtins .nx (win_open, draw_rect, vsync …)
├── services/           ← moduli servizio auto-registranti
│   ├── core/           ghal_vfs.c (VFS 3-backend), ghal_service_registry.c
│   ├── image/          ghal_image.c (stb_image), ghal_image_nexs.c (builtins)
│   └── font/           ghal_font_ttf.c (stb_truetype), ghal_font_nexs.c
├── vendor/include/     ← stb_image.h, stb_truetype.h, stb_image_write.h
│                         (copie read-only da scratch/stb – NON AI zone)
├── test/               ← suite di test unitari
├── scripts/test-full-cycle.sh  ← script test manuale
└── example/            ← programmi .nx dimostrativi
```

---

## Principi di integrazione con base-nexs

**base-nexs è un submodule read-only.** Non modificare MAI file dentro `base-nexs/`.

Per sovrascrivere/correggere file di base-nexs:
1. Copia il file → `reimplementation/<stesso percorso relativo>`
2. Il Makefile mette `-I reimplementation` PRIMA di tutti gli altri path
3. Per i .c: aggiungi l'esclusione in NEXS_SRCS (`! -path 'base-nexs/...'`) e aggiungi il tuo file a GHAL_SRCS

**Pattern include guard (test standalone):**
```c
#ifndef NEXS_HAL_H
#include "nexs_hal.h"    // skippato con -DNEXS_HAL_H
#endif
#ifndef NEXS_API
#define NEXS_API
#endif
```

**Registry NEXS:** `/dev/<device>/<id>/<key>` — usa `reg_set(path, val_int(n), RK_READ)`
**Builtins .nx:** `fn_register_builtin_sig(name, fn, sig)` registra una C function come funzione .nx
**Costruttori:** HAL=101, platform driver=150, GHAL bootstrap=200, service modules=300

---

## Fasi completate

### Fase 0 — Scheletro directory e header
- `include/ghal.h` — GHalDriver, GHalWindow, GHalSurface, GHalInputEvent
- `include/ghal_compositor.h` — GCompositor, gcomp_*
- `include/ghal_2d.h` — draw2d_blit, draw2d_fill_rect, draw2d_line, draw2d_text
- `include/ghal_bc.h` — GalbVM, GalOpcode (bytecode GALB)
- `include/ghal_service.h` — GhalServiceModule, GHAL_SERVICE_REGISTER macro
- `include/ghal_font_ttf.h` — GhalFontTTF, wrapping stb_truetype
- `include/ghal_image.h` — GhalImage, wrapping stb_image

### Fase 1 — Compositor unificato + IPC
- `compositor/compositor.c` — gcomp_init, gcomp_tick, gcomp_win_register/unregister
- `compositor/window_registry.c` — winreg_set_int/str, gcomp_publish_registry
- `compositor/ipc_dispatcher.c` — routing eventi IPC

### Fase 2 — Backend macOS (Metal)
- `host/macos/ghal_macos.m` — NSWindow + CAMetalLayer, constructor(150)
- `host/macos/ghal_metal.m` — upload texture, blit to drawable
- `host/ghal_host.c` — dispatcher API, ghal_init, ghal_surface_present

### Fase 3 — Backend Linux (X11/EGL)
- `host/linux_x11/ghal_x11.c` — finestra X11, XLib
- `host/linux_x11/ghal_egl.c` — contesto EGL per OpenGL ES

### Fase 4 — Software renderer + GALB VM
- `lib/draw2d.c` — fill_rect, blit, line (Bresenham), text bitmap
- `lib/font.c` — font bitmap built-in
- `bc/ghal_bc.c` — VM GALB: step, run, opcode GALO_*
- `bc/ghal_asm.c` — assembler GALB

### Fase 5 — Builtins .nx
- `lang/ghal_builtins.c` — bi_win_open, bi_draw_rect, bi_vsync, … con `ghal_builtins_register()`
- `lang/ghal_fn_table.c` — chiama `ghal_builtins_register()` + `ghal_service_init_all()`
- `reimplementation/hal/hal_hosted.c` — HAL hosted con constructor(200) che chiama `ghal_init()`

### Fase 6 — Baremetal VirtIO-GPU + framebuffer lineare
- `host/baremetal/ghal_virtio_gpu.c` — driver VirtIO-GPU per QEMU
- `host/baremetal/ghal_fblinear.c` — framebuffer lineare (Raspberry Pi, …)

### Fase 7 — seL4 Protection Domains
- `host/sel4/ghal_sel4_compositor.c` — PD compositor
- `host/sel4/ghal_sel4_surface.c` — PD superficie
- `host/sel4/ghal_sel4_ipc.c` — bridge IPC

### Fase 8 (in corso) — Servizi stb come moduli NEXS

Integrazione stb_image e stb_truetype come servizi auto-registranti:

**Layer 1 — Protocollo** (`include/ghal_service.h`):
```c
typedef struct GhalServiceModule {
    const char *name, *version, *reg_prefix;
    void (*init)(void); void (*shutdown)(void);
    const GhalBuiltinDef *builtins; int n_builtins;
    struct GhalServiceModule *_next;
} GhalServiceModule;
#define GHAL_SERVICE_REGISTER(mod) __attribute__((constructor(300))) static void ...
```

**Layer 2 — VFS bridge** (`services/core/ghal_vfs.c`):
3 backend: POSIX (fopen), NEXS VFS (nexs_open), seL4 IPC (microkit_ppcall)

**Layer 3 — Servizio immagini** (`services/image/ghal_image_nexs.c`):
8 builtins: `img_load`, `img_load_mem`, `img_save`, `img_scale`, `img_blit`, `img_info`, `img_free`
Registry: `/dev/img/<id>/{path, width, height, surface}`

**Layer 4 — Servizio font** (`services/font/ghal_font_nexs.c`):
7 builtins: `font_load`, `font_draw`, `font_draw_wrap`, `font_measure`, `font_line_height`, `font_ascent`, `font_free`
Registry: `/dev/font/<id>/{path, size, line_height, ascent, handle}`

---

## Stato attuale del build

### Errori corretti in questa sessione
| File | Problema | Fix |
|------|----------|-----|
| `ghal.h` | `#include "nexs_hal.h"` non guardato | Aggiunto `#ifndef NEXS_HAL_H` guard |
| `base-nexs/kernel/include/termios.h` | mancava `TCSANOW` → `nexs_line.c` non compilava | Creato `reimplementation/kernel/include/termios.h` con `#include_next` |
| `Makefile` | `base-nexs/kernel/include` nel path → shadowing stdio POSIX | Rimosso dal path; aggiunto `! -path 'base-nexs/kernel/*'` e `! -path 'base-nexs/fs/*'` a NEXS_SRCS |
| `host/ghal_host.c` | `v.type = VAL_STR` (VAL_STR è macro funzione, non tipo enum) | Sostituito con `val_str()` / `val_int()` diretti |
| `bc/ghal_bc.c` | `pop_reg` unused → `-Werror` | Aggiunto `__attribute__((unused))` |
| `bc/ghal_asm.c` | `galb_buf_free` unused | Aggiunto `__attribute__((unused))` |
| `lang/ghal_builtins.c` | `val_to_str` chiamata con firma sbagliata; bi_* `static` ma referenziate come `extern` | Usato `val_to_str(v, buf, sz)`; aggiunto `ghal_builtins_register()` |
| `host/macos/ghal_metal.m` | `id<CAMetalDrawable>` senza import QuartzCore; `unused parameter` | Aggiunto `#import <QuartzCore/CAMetalLayer.h>`; marcati `__attribute__((unused))` |
| `reimplementation/hal/hal_hosted.c` | Include relative non trovate; `phys` parameter unused | Fix include a forme piatte; aggiunto `(void)phys` |

### Errori ancora aperti (link)
```
Undefined symbols:
  _main         — runtime/main.c ha __attribute__((weak)) che macOS non risolve come entry point
  _ghal_service_init_all — services/ non ancora in NEXS_SRCS/GHAL_SRCS  
  _stdout       — nexs_utils.c referenzia stdout senza stdio.h nel suo TU
```

---

## Prossimi passi critici

### Immediati (per far girare make)
1. **`_main`**: Creare `reimplementation/runtime/main.c` senza `__attribute__((weak))` e con chiamata a `ghal_register_builtins()` dentro `nexs_runtime_autoload()` o come extension hook.
2. **`_ghal_service_init_all`**: Aggiungere `services/core/ghal_service_registry.c` a GHAL_SRCS nel Makefile.
3. **`_stdout`**: Copiare `base-nexs/core/utils.c` → `reimplementation/core/utils.c` e aggiungere `#include <stdio.h>`.

### Breve termine (per vedere una finestra)
4. **Demo standalone**: `demo/ghal_window_demo.c` — main() che apre una finestra Metal su macOS senza runtime NEXS. Build separata nel Makefile.
5. **TTY driver**: Servizio HAL-level per compatibilità terminale su host e baremetal. Il NEXS runtime già include `nexs_line.c` per readline; collegarlo al compositor via `/dev/tty/<n>/`.
6. **Connettere `ghal_register_builtins` al NEXS runtime**: In `nexs_runtime_init()` (runtime/runtime.c → copia in reimplementation/) aggiungere chiamata a `ghal_register_builtins()` dopo `builtins_register_all()`.

### Medio termine (integrazione completa)
7. **Modalità compositor**: Su baremetal/seL4 gestione completa di sfondo + puntatore + focus. Su host delegare al compositor nativo (già fatto: NSWindow su macOS, X11 su Linux).
8. **TTY manager**: Framebuffer per-processo, swipe tra TTY (come VT su Linux). Su seL4 il root server è il daemon principale.
9. **Test service layer**: Far compilare `test/test_service_layer.c` con i servizi image/font e includerlo in `scripts/test-full-cycle.sh`.
10. **GALB → .nx**: Verifica che il ciclo completo `win_open → draw_rect → vsync → win_close` funzioni da uno script .nx via NEXS REPL.

---

## Come l'agente deve approcciare base-nexs

```
REGOLA ASSOLUTA: Non modificare MAI file dentro base-nexs/
```

**Come sovrascrivere un file base-nexs:**
```bash
# Esempio: correggere runtime/main.c
cp base-nexs/runtime/main.c reimplementation/runtime/main.c
# Modificare reimplementation/runtime/main.c
# Nel Makefile, aggiungere a NEXS_SRCS: ! -path 'base-nexs/runtime/main.c'
# E aggiungere a GHAL_SRCS: reimplementation/runtime/main.c
```

**Come risolvere include conflicts:**
- Se `base-nexs/kernel/include/foo.h` causa problemi su POSIX → crea `reimplementation/kernel/include/foo.h` con `#include_next <foo.h>` o con la definizione corretta

**Cosa NON includere per build hosted:**
- `base-nexs/kernel/` — syscall, proc, sched, cap, vfs (solo baremetal/seL4)
- `base-nexs/fs/` — fat, 9p, regfs (richiedono blk_read/vfs dal kernel)
- `base-nexs/hal/sel4/` — IPC seL4
- `base-nexs/hal/amd64|arm64|riscv64/` — HAL assembly per arch specifiche

**Cosa includere per build hosted:**
- `base-nexs/core/` — buddy, dynarray, value, utils, pager
- `base-nexs/lang/` — eval, parser, lexer, fn_table, builtins
- `base-nexs/runtime/` — nexs_line, runtime (ma NON main.c → sostituito)
- `base-nexs/compiler/` — codegen, dep_scan, driver
- `base-nexs/registry/` — registry, reg_ipc
- `base-nexs/sys/` — sysio, sysproc
- `base-nexs/hal/common/` + `base-nexs/hal/bc/` + `base-nexs/hal/module/`

---

## File di test

| Test | Stato | Cosa copre |
|------|-------|------------|
| `test/test_draw2d.c` | PASS (33/33) | draw2d renderer software |
| `test/test_compositor.c` | PASS | GCompositor, window reg, IPC |
| `test/test_galb.c` | PASS | VM bytecode GALB |
| `test/test_services.c` | PASS (24/24) | stb image/font round-trip |
| `test/test_service_layer.c` | IN CORSO | GhalServiceModule NEXS integration |

---

## Vincoli di sicurezza

`scratch/stb/` ha un `.NO_AI/README.md` e `CONTRIBUTING.md` che vietano esplicitamente contributi AI/LLM. Non creare, modificare o eliminare mai file dentro `scratch/stb/`. Copiare solo le header necessarie in `vendor/include/` e scrivere wrapper nel progetto.
