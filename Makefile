PROJECT  := ghal
TARGET   ?= hosted
OUTDIR   := build/$(TARGET)

# ── Critical: reimplementation/ FIRST to intercept base-nexs headers ──────
INCLUDES := \
    -I reimplementation \
    -I base-nexs/hal/include \
    -I base-nexs/include \
    -I base-nexs/core/include \
    -I base-nexs/registry/include \
    -I base-nexs/lang/include \
    -I base-nexs/runtime/include \
    -I include \
    -I vendor/include

CFLAGS   := -Wall -Wextra -Werror -std=c17 -g $(INCLUDES) -DNEXS_HOST_TOOL -DHOST_OS_MACOS
CXXFLAGS := -Wall -Wextra -Werror -std=c++17 -g $(INCLUDES)
LDFLAGS  :=
CC       ?= cc

# Relax compiler rules for the pristine base-nexs submodule to prevent upstream warnings from halting the build
$(OUTDIR)/base-nexs/%.o: CFLAGS += -Wno-error

# ── Source files ───────────────────────────────────────────────────────────

# base-nexs core: hosted builds exclude kernel/ and fs/ (baremetal-only).
# kernel/ and fs/ use block-device and VFS symbols not available on POSIX hosts.
NEXS_SRCS := $(shell find base-nexs -name '*.c' \
    ! -path 'base-nexs/hal/hal_hosted.c' \
    ! -path 'base-nexs/hal/amd64/*' \
    ! -path 'base-nexs/hal/arm64/*' \
    ! -path 'base-nexs/hal/riscv64/*' \
    ! -path 'base-nexs/hal/sel4/*' \
    ! -path 'base-nexs/kernel/*' \
    ! -path 'base-nexs/fs/*' \
    2>/dev/null)

# GHAL platform-independent sources
GHAL_SRCS := \
    reimplementation/hal/hal_hosted.c \
    compositor/compositor.c \
    compositor/window_registry.c \
    compositor/ipc_dispatcher.c \
    host/ghal_host.c \
    lib/draw2d.c \
    lib/font.c \
    bc/ghal_bc.c \
    bc/ghal_asm.c \
    lang/ghal_builtins.c \
    lang/ghal_fn_table.c \
    services/core/ghal_service_registry.c \
    services/core/ghal_vfs.c \
    services/image/ghal_image.c \
    services/image/ghal_image_nexs.c \
    services/font/ghal_font_ttf.c \
    services/font/ghal_font_nexs.c

# Platform-specific backend
UNAME := $(shell uname)
ifeq ($(UNAME),Darwin)
  GHAL_SRCS  += host/macos/ghal_macos.m host/macos/ghal_metal.m
  LDFLAGS    += -framework Cocoa -framework Metal \
                -framework QuartzCore -framework Foundation
  CFLAGS     += -fobjc-arc
  OCC         = $(CC)  # clang handles .m natively
endif

ifeq ($(UNAME),Linux)
  GHAL_SRCS += host/linux_x11/ghal_x11.c host/linux_x11/ghal_egl.c
  LDFLAGS   += -ldl -lpthread
endif

ALL_SRCS   := $(NEXS_SRCS) $(GHAL_SRCS)
SRCS_C     := $(filter %.c,$(ALL_SRCS))
SRCS_M     := $(filter %.m,$(ALL_SRCS))

OBJS_C     := $(patsubst %.c,$(OUTDIR)/%.o,$(SRCS_C))
OBJS_M     := $(patsubst %.m,$(OUTDIR)/%.o,$(SRCS_M))
OBJS       := $(OBJS_C) $(OBJS_M)

TARGET_BIN := $(OUTDIR)/$(PROJECT)

# ── Test sources ───────────────────────────────────────────────────────────
TEST_SRCS  := test/test_draw2d.c test/test_compositor.c test/test_galb.c
TEST_BINS  := $(patsubst test/%.c,$(OUTDIR)/test/%,$(TEST_SRCS))

# ── Targets ────────────────────────────────────────────────────────────────
.PHONY: all clean test verify-includes check-submodule

all: check-submodule $(TARGET_BIN)

check-submodule:
	@[ -f base-nexs/hal/include/nexs_hal.h ] || \
		(echo "ERROR: base-nexs submodule not initialized." \
		 "Run: git submodule update --init"; exit 1)

$(OUTDIR):
	@mkdir -p $(OUTDIR)

$(OUTDIR)/%.o: %.c | $(OUTDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTDIR)/%.o: %.m | $(OUTDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET_BIN): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	@echo "Built: $(TARGET_BIN)"

# ── Tests ──────────────────────────────────────────────────────────────────
TEST_CFLAGS := $(CFLAGS) -DGHAL_TEST_MODE

$(OUTDIR)/test/%: test/%.c lib/draw2d.c lib/font.c bc/ghal_bc.c bc/ghal_asm.c \
                  compositor/compositor.c compositor/window_registry.c \
                  compositor/ipc_dispatcher.c host/ghal_host.c
	@mkdir -p $(OUTDIR)/test
	$(CC) $(TEST_CFLAGS) $^ $(LDFLAGS) -o $@

test: $(TEST_BINS)
	@echo "=== Running GHAL tests ==="
	@for t in $(TEST_BINS); do echo "--- $$t ---"; $$t; done
	@echo "=== All tests done ==="

# ── Verification ──────────────────────────────────────────────────────────
verify-includes:
	@echo "Include search order:"
	@echo "  1. reimplementation/"
	@echo "  2. base-nexs/hal/include/"
	@echo "  3. base-nexs/include/"
	@echo ""
	@echo "Verifying reimplementation/hal/hal_hosted.c exists:"
	@[ -f reimplementation/hal/hal_hosted.c ] && echo "  OK" || echo "  MISSING"
	@echo "Verifying base-nexs/ submodule:"
	@[ -f base-nexs/hal/include/nexs_hal.h ] && echo "  OK" || echo "  MISSING"
	@echo "Verifying nexs/ submodule is untouched:"
	@cd base-nexs && git diff --quiet && echo "  OK (no modifications)" || echo "  WARNING: modifications detected"
	@echo ""
	@echo "Header syntax check (ghal.h):"
	@$(CC) $(CFLAGS) -fsyntax-only include/ghal.h 2>&1 || true
	@echo "Header syntax check (ghal_bc.h):"
	@$(CC) $(CFLAGS) -fsyntax-only include/ghal_bc.h 2>&1 || true

clean:
	rm -rf build/
