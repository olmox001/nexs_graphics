# Graphics HAL Design Skill — Templates & Resources

This folder contains templates and supporting files for the **graphics-hal-design** skill.

## Files

| File | Purpose |
|------|---------|
| `Makefile.template` | Build system with `-I reimplementation/` override pattern |
| `ghal.h.template` | Public API header (copy to `include/ghal.h`) |
| `PHASE_CHECKLIST.md` | Implementation checklist (one per project instance) |
| `README.md` | This file |

## Usage

### 1. Invoke the Skill
```
/graphics-hal-design

Create a unified graphics abstraction layer for my OS project.
```

The skill (located at `~/Library/Application Support/Code/User/prompts/graphics-hal-design.skill.md`) will guide you through 6 phases:

1. **Phase 0:** Analyze existing HAL patterns
2. **Phase 1:** Define 4-layer architecture (bitcode VM, compositor, libs, builtins)
3. **Phase 2:** Cross-platform backend strategy (Host, Baremetal, seL4)
4. **Phase 3:** Plan registry namespace (/dev/win/, /dev/gfx/)
5. **Phase 4:** Repository structure with `reimplementation/` override pattern
6. **Phase 5:** Generate 8-phase implementation roadmap

### 2. Copy Templates to Your Project

**After Phase 5**, copy the templates into your repository:

```bash
cp .github/skills/ghal-design/Makefile.template Makefile
cp .github/skills/ghal-design/ghal.h.template include/ghal.h
cp .github/skills/ghal-design/PHASE_CHECKLIST.md IMPLEMENTATION_PROGRESS.md

# Customize for your project:
# - Edit Makefile: set PROJECT, add platform-specific rules
# - Edit ghal.h: finalize struct fields
# - Edit PHASE_CHECKLIST.md: assign owners, set dates
```

### 3. Follow the Checklist

Use `PHASE_CHECKLIST.md` as your implementation guide:
- [ ] Phase 0: Repo skeleton
- [ ] Phase 1: Core HAL + Compositor
- [ ] Phase 2: Host backend (macOS/Linux)
- [ ] Phase 3+: Continue...

## Key Concepts

### Override Pattern (`reimplementation/`)

This pattern ensures clean git submodule integration:

```
ghal/
├── nexs/                    ← NEVER EDIT (submodule)
├── reimplementation/        ← ALL OVERRIDES HERE
│   └── hal/
│       └── hal_hosted.c    ← overrides nexs/hal/hal_hosted.c
└── Makefile                ← -I reimplementation FIRST
```

The Makefile ensures:
```makefile
CFLAGS += -I reimplementation -I nexs/...
```

This means `#include "nexs_hal.h"` finds the override version first.

### 4-Layer Architecture

```
L4: Language (.nx)              ← win_open(), draw_rect()
L3: GALB Bytecode VM            ← Graphics opcodes
L2: Compositor + draw2d/3d      ← Window manager + rendering
L1: GHalDriver (unified)        ← Platform-agnostic interface
L0: Backend (macOS/Linux/bare)  ← Metal, EGL, virtio-gpu, etc.
```

### Platforms Covered

| Platform | Backend | Status |
|----------|---------|--------|
| macOS | Metal + Cocoa | Host acceleration |
| Linux | EGL + X11 | Host acceleration |
| Baremetal (x86_64, ARM64, RISC-V) | VirtIO-GPU | virgl → host GPU |
| seL4 microkernel | Protected Domains | IPC + shared memory |

## Related Files in This Workspace

- [`GHAL_ATTACK_PLAN.md`](../../GHAL_ATTACK_PLAN.md) — Full architectural proposal (starting point)
- [`context_base.md`](../../context_base.md) — Design rationale and research

## Further Customization

If you need to extend the skill for your specific use case:

1. **Add a backend:** Modify `Makefile.template` to include platform-specific sources
2. **Add language builtins:** Extend `ghal.h.template` with new function pointers
3. **Adjust phases:** Edit `PHASE_CHECKLIST.md` to reflect your team's sprint cadence

## Questions?

Refer to the full skill documentation:
- User-level skill: `~/Library/Application Support/Code/User/prompts/graphics-hal-design.skill.md`
- References section includes links to Mesa, seL4, VirtIO GPU specifications
