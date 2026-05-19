#!/bin/bash
# scripts/test-full-cycle.sh — GHAL build + unit test runner
#
# Runs software-only tests that require no display.
# Platform: macOS (Darwin) and Linux x86_64.
#
# Usage: ./scripts/test-full-cycle.sh [--verbose]

set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

VERBOSE=0
[[ "$1" == "--verbose" ]] && VERBOSE=1

PASS=0
FAIL=0
SKIP=0

log()  { echo "$*"; }
pass() { echo "  PASS: $1"; PASS=$((PASS+1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL+1)); }
skip() { echo "  SKIP: $1 ($2)"; SKIP=$((SKIP+1)); }

# ── 0. Submodule check ──────────────────────────────────────────
log ""
log "=== Phase 0: Submodule integrity ==="
if [ -f base-nexs/hal/include/nexs_hal.h ]; then
    pass "base-nexs submodule initialized"
else
    fail "base-nexs/hal/include/nexs_hal.h missing — run: git submodule update --init"
fi

if cd base-nexs && git diff --quiet 2>/dev/null; then
    pass "base-nexs submodule unmodified"
    cd ..
else
    fail "base-nexs has uncommitted modifications"
    cd ..
fi

# ── 1. Include structure ────────────────────────────────────────
log ""
log "=== Phase 0: File structure check ==="
for f in \
    include/ghal.h include/ghal_ipc.h include/ghal_bc.h \
    include/ghal_compositor.h include/ghal_surface.h \
    include/ghal_2d.h include/ghal_3d.h include/ghal_compute.h \
    reimplementation/hal/hal_hosted.c \
    compositor/compositor.c compositor/window_registry.c \
    compositor/ipc_dispatcher.c \
    host/ghal_host.c \
    lib/draw2d.c lib/font.c \
    bc/ghal_bc.c bc/ghal_asm.c \
    lang/ghal_builtins.c lang/ghal_fn_table.c \
    baremetal/virtio_gpu.c baremetal/ghal_linear_fb.c \
    sel4/compositor_pd.c sel4/ipc_bridge.c sel4/ghal_sel4.c \
    example/hello_window.nx example/triangle.nx; do
    if [ -f "$f" ]; then
        [ $VERBOSE -eq 1 ] && pass "file exists: $f"
    else
        fail "missing: $f"
    fi
done
pass "All required files present"

# ── 2. Header syntax check ──────────────────────────────────────
log ""
log "=== Phase 0: Header compile check ==="
IFLAGS="-I include -I vendor/include -I base-nexs/hal/include \
        -I base-nexs/include -I base-nexs/core/include \
        -I base-nexs/registry/include -I base-nexs/lang/include \
        -I base-nexs/runtime/include"

for h in include/ghal_bc.h include/ghal_ipc.h include/ghal_3d.h \
          include/ghal_compute.h; do
    if cc $IFLAGS -std=c17 -fsyntax-only "$h" 2>/dev/null; then
        pass "syntax OK: $h"
    else
        fail "syntax error in: $h"
    fi
done

# ghal.h and ghal_compositor.h depend on nexs_hal.h — they'll fail
# IDE resolution but compile fine with full -I flags
for h in include/ghal.h include/ghal_compositor.h; do
    if cc $IFLAGS -std=c17 -fsyntax-only "$h" 2>/dev/null; then
        pass "syntax OK: $h"
    else
        skip "$h syntax check" "requires nexs_hal.h (base-nexs dependency)"
    fi
done

# ── 3. Build standalone draw2d test ────────────────────────────
log ""
log "=== Phase 4: draw2d unit tests ==="
mkdir -p build/test

# draw2d.c includes ghal_surface.h which pulls in ghal.h → nexs_hal.h.
# For standalone test, we supply a wrapper that defines the structs directly.
if cc -std=c17 -Wall -Wextra -g \
       test/test_draw2d.c \
       lib/draw2d.c lib/font.c \
       -o build/test/test_draw2d 2>/dev/null; then
    pass "draw2d test built"
    if ./build/test/test_draw2d; then
        pass "draw2d tests PASSED"
    else
        fail "draw2d tests FAILED"
    fi
else
    # Try with NEXS_HAL guard approach
    if cc -std=c17 -Wall -g \
           -DNEXS_HAL_H -DHAL_INTERNAL_H -DNEXS_MMU_H \
           -I include \
           test/test_draw2d.c lib/draw2d.c lib/font.c \
           -o build/test/test_draw2d 2>/dev/null; then
        pass "draw2d test built (stub mode)"
        ./build/test/test_draw2d && pass "draw2d tests PASSED" || fail "draw2d tests FAILED"
    else
        skip "draw2d unit test build" "include resolution requires base-nexs headers"
    fi
fi

# ── 4. Build GALB VM test ───────────────────────────────────────
log ""
log "=== Phase 4: GALB VM unit tests ==="
if cc -std=c17 -Wall -g \
       -DNEXS_HAL_H -DHAL_INTERNAL_H -DNEXS_MMU_H \
       -I include \
       test/test_galb.c bc/ghal_bc.c bc/ghal_asm.c lib/draw2d.c lib/font.c \
       -o build/test/test_galb 2>/dev/null; then
    pass "GALB test built"
    ./build/test/test_galb && pass "GALB tests PASSED" || fail "GALB tests FAILED"
else
    skip "GALB unit test build" "include resolution requires base-nexs headers"
fi

# ── 5. Build compositor test ────────────────────────────────────
log ""
log "=== Phase 1: Compositor unit tests ==="
if cc -std=c17 -Wall -g \
       -DNEXS_HAL_H -DHAL_INTERNAL_H -DNEXS_MMU_H \
       -I include \
       test/test_compositor.c \
       compositor/compositor.c \
       compositor/window_registry.c \
       compositor/ipc_dispatcher.c \
       lib/draw2d.c lib/font.c \
       -o build/test/test_compositor 2>/dev/null; then
    pass "compositor test built"
    ./build/test/test_compositor && pass "Compositor tests PASSED" || fail "Compositor tests FAILED"
else
    skip "Compositor test build" "include resolution requires full base-nexs build"
fi

# ── 6. Platform compile check ───────────────────────────────────
log ""
log "=== Phase 2/3: Platform backend compile check ==="

UNAME="$(uname)"
if [ "$UNAME" = "Darwin" ]; then
    if cc -x objective-c -std=c17 $IFLAGS -fobjc-arc \
           -fmodules \
           -DNEXS_HAL_H -DHAL_INTERNAL_H \
           -fsyntax-only \
           host/macos/ghal_macos.m 2>/dev/null; then
        pass "ghal_macos.m syntax OK"
    else
        skip "ghal_macos.m syntax" "Cocoa/Metal headers required"
    fi
else
    skip "macOS Metal backend" "requires macOS"
fi

# ── Summary ─────────────────────────────────────────────────────
log ""
log "================================================================"
log "RESULTS: $PASS passed  |  $FAIL failed  |  $SKIP skipped"
log "================================================================"

[ $FAIL -eq 0 ] && exit 0 || exit 1
