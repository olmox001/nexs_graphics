PROJECT  := ghal
TARGET   ?= hosted
OUTDIR   := build/$(TARGET)

# ── Critical: reimplementation/ FIRST to intercept base-nexs headers ──────
INCLUDES := \
    -I reimplementation \
    -I reimplementation/core/include \
    -I reimplementation/lang \
    -I reimplementation/lang/include \
    -I base-nexs/hal/include \
    -I base-nexs/include \
    -I base-nexs/core/include \
    -I base-nexs/registry/include \
    -I base-nexs/lang \
    -I base-nexs/lang/include \
    -I base-nexs/runtime/include \
    -I base-nexs/compiler/include \
    -I base-nexs/sys/include \
    -I hal/include \
    -I lang/include \
    -I kernel/include \
    -I kernel/services/core \
    -I vendor/include

CFLAGS   := -Wall -Wextra -Werror -std=c17 -g -include reimplementation/core/include/nexs_common.h $(INCLUDES) -DNEXS_HOST_TOOL -DHOST_OS_MACOS
CXXFLAGS := -Wall -Wextra -Werror -std=c++17 -g -include reimplementation/core/include/nexs_common.h $(INCLUDES)
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
    ! -path 'base-nexs/runtime/main.c' \
    ! -path 'base-nexs/runtime/runtime.c' \
    ! -path 'base-nexs/core/utils.c' \
    ! -path 'base-nexs/lang/lexer.c' \
    ! -path 'base-nexs/lang/parser.c' \
    ! -path 'base-nexs/compiler/dep_scan.c' \
    2>/dev/null)

# GHAL platform-independent sources
GHAL_SRCS := \
    reimplementation/hal/hal_hosted.c \
    reimplementation/runtime/main.c \
    reimplementation/runtime/runtime.c \
    reimplementation/core/utils.c \
    reimplementation/lang/lexer.c \
    reimplementation/lang/parser.c \
    reimplementation/compiler/dep_scan.c \
    kernel/compositor/compositor.c \
    kernel/compositor/window_registry.c \
    kernel/compositor/ipc_dispatcher.c \
    hal/host/ghal_host.c \
    hal/common/draw2d.c \
    hal/common/font.c \
    lang/bc/ghal_bc.c \
    lang/bc/ghal_asm.c \
    lang/bc/ghal_compiler.c \
    lang/ghal_builtins.c \
    lang/ghal_fn_table.c \
    kernel/services/core/ghal_service_registry.c \
    kernel/services/core/ghal_vfs.c \
    kernel/services/core/ghal_html.c \
    kernel/services/image/ghal_image.c \
    kernel/services/image/ghal_image_nexs.c \
    kernel/services/font/ghal_font_ttf.c \
    kernel/services/font/ghal_font_nexs.c

# Platform-specific backend
UNAME := $(shell uname)
ifeq ($(UNAME),Darwin)
  GHAL_SRCS  += hal/host/macos/ghal_macos.m hal/host/macos/ghal_metal.m
  LDFLAGS    += -framework Cocoa -framework Metal \
                -framework QuartzCore -framework Foundation
  CFLAGS     += -fobjc-arc
  OCC         = $(CC)  # clang handles .m natively
  endif

  ifeq ($(UNAME),Linux)
  GHAL_SRCS += hal/host/linux_x11/ghal_x11.c hal/host/linux_x11/ghal_egl.c
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
.PHONY: all clean test verify-includes check-submodule check-examples

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

# If EMBED_SCRIPT is specified, we bootstrap compile and link the embedded script
EXTRA_OBJS :=
ifneq ($(EMBED_SCRIPT),)
EXTRA_OBJS += $(OUTDIR)/ghal_embed.o
endif

$(OUTDIR)/ghal_bootstrap: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(OUTDIR)/ghal_embed.c: $(EMBED_SCRIPT) $(OUTDIR)/ghal_bootstrap
	$(OUTDIR)/ghal_bootstrap --codegen $(EMBED_SCRIPT) -o $@ --hosted
	@sed 's/static const char nexs_script_src/const char nexs_script_src/g' $@ > $@.tmp
	@sed 's/int main(/int duplicate_main(/g' $@.tmp > $@
	@rm -f $@.tmp

$(OUTDIR)/ghal_embed.o: $(OUTDIR)/ghal_embed.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET_BIN): $(OBJS) $(EXTRA_OBJS)
	$(CC) $(OBJS) $(EXTRA_OBJS) $(LDFLAGS) -o $@
	@echo "Built: $(TARGET_BIN)"

# ── Tests ──────────────────────────────────────────────────────────────────
TEST_CFLAGS := $(CFLAGS) -DGHAL_TEST_MODE

$(OUTDIR)/test/test_draw2d: test/test_draw2d.c hal/common/draw2d.c hal/common/font.c
	@mkdir -p $(OUTDIR)/test
	$(CC) $(TEST_CFLAGS) $^ $(LDFLAGS) -o $@

$(OUTDIR)/test/test_compositor: test/test_compositor.c kernel/compositor/compositor.c kernel/compositor/ipc_dispatcher.c
	@mkdir -p $(OUTDIR)/test
	$(CC) $(TEST_CFLAGS) $^ $(LDFLAGS) -o $@

$(OUTDIR)/test/test_galb: test/test_galb.c lang/bc/ghal_bc.c lang/bc/ghal_asm.c lang/bc/ghal_compiler.c
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
	@$(CC) $(CFLAGS) -fsyntax-only hal/include/ghal.h 2>&1 || true
	@echo "Header syntax check (ghal_bc.h):"
	@$(CC) $(CFLAGS) -fsyntax-only lang/include/ghal_bc.h 2>&1 || true

# ── Script syntax audit (no GUI needed) ───────────────────────────────────
# Checks that no banned patterns appear in example scripts.
check-examples:
	@echo "=== Example script audit ==="
	@ok=1; \
	for f in example/*.nx; do \
	    if grep -qn "halt\b\|^while \|\bwhile \b\|^ *let \| exec(" "$$f" 2>/dev/null; then \
	        echo "  FAIL: $$f contains banned pattern:"; \
	        grep -n "halt\b\|^while \|\bwhile \b\|^ *let \| exec(" "$$f"; \
	        ok=0; \
	    else \
	        echo "  OK:   $$f"; \
	    fi; \
	done; \
	[ $$ok -eq 1 ] && echo "=== All examples OK ===" || (echo "=== Audit FAILED ==="; exit 1)

# ── Baremetal amd64 (QEMU PVH / Multiboot2) ───────────────────────────────
#
# Uses x86_64-elf-gcc + the full NEXS HAL (uart, gdt, idt, mmu, apic, acpi)
# from base-nexs/hal/amd64/ and reimplementation/ overrides.
# Embed a test script with EMBED_SCRIPT=example/test_baremetal.nx.
#
# Build:  make baremetal-amd64
#         make baremetal-amd64 EMBED_SCRIPT=example/test_baremetal.nx
# Test:   make qemu-baremetal

BDIR     := build/baremetal-amd64
BCC      := x86_64-elf-gcc
BAS      := x86_64-elf-gcc  # assembles .S via -x assembler-with-cpp
BFLAGS   := -Wall -Wextra -std=c17 -ffreestanding -nostdlib -mno-red-zone \
             -DNEXS_BAREMETAL \
             -include reimplementation/core/include/nexs_common.h \
             -I reimplementation/kernel/include \
             -I base-nexs/kernel/include \
             -I reimplementation \
             -I reimplementation/core/include \
             -I reimplementation/lang \
             -I reimplementation/lang/include \
             -I base-nexs/hal/include \
             -I base-nexs/include \
             -I base-nexs/core/include \
             -I base-nexs/registry/include \
             -I base-nexs/lang \
             -I base-nexs/lang/include \
             -I base-nexs/runtime/include \
             -I base-nexs/compiler/include \
             -I base-nexs/sys/include \
             -I include
BLDFLAGS := -T base-nexs/hal/amd64/nexs.ld -nostdlib \
             /usr/local/Cellar/x86_64-elf-gcc/15.2.0/lib/gcc/x86_64-elf/15.2.0/libgcc.a

# base-nexs sources for baremetal: full HAL amd64 + core runtime (no hosted-only files)
BARE_NEXS_SRCS := \
    base-nexs/hal/amd64/uart.c \
    base-nexs/hal/amd64/gdt.c \
    base-nexs/hal/amd64/idt.c \
    base-nexs/hal/amd64/mmu.c \
    base-nexs/hal/amd64/apic.c \
    base-nexs/hal/amd64/acpi.c \
    base-nexs/hal/bc/nexs_hal_bc.c \
    base-nexs/hal/module/nexs_hal_module.c \
    base-nexs/runtime/nexs_line.c \
    base-nexs/core/buddy.c \
    base-nexs/core/pager.c \
    base-nexs/core/value.c \
    base-nexs/core/dynarray.c \
    base-nexs/runtime/runtime.c \
    base-nexs/lang/eval.c \
    base-nexs/lang/builtins.c \
    base-nexs/lang/fn_table.c \
    base-nexs/registry/registry.c \
    base-nexs/registry/reg_ipc.c \
    base-nexs/sys/sysproc.c \
    base-nexs/sys/sysio.c \
    base-nexs/compiler/codegen.c \
    base-nexs/compiler/driver.c

# Assembly sources
BARE_ASM_SRCS := \
    base-nexs/hal/amd64/boot.S \
    base-nexs/hal/amd64/ctx_amd64.S \
    base-nexs/hal/amd64/isr_stubs.S

# GHAL reimplementation sources for baremetal
# kernel/libc_stub.c provides the minimal C library (stdio, string, ctype, stdlib)
BARE_GHAL_SRCS := \
    base-nexs/kernel/libc_stub.c \
    reimplementation/core/utils.c \
    reimplementation/lang/lexer.c \
    reimplementation/lang/parser.c \
    reimplementation/compiler/dep_scan.c \
    reimplementation/runtime/main.c \
    reimplementation/hal/ghal_baremetal.c

BARE_OBJS_C   := $(patsubst %.c,$(BDIR)/%.o,$(BARE_NEXS_SRCS) $(BARE_GHAL_SRCS))
BARE_OBJS_S   := $(patsubst %.S,$(BDIR)/%.o,$(BARE_ASM_SRCS))
BARE_OBJS     := $(BARE_OBJS_S) $(BARE_OBJS_C)
BARE_BIN      := $(BDIR)/nexs_bare_amd64.elf

.PHONY: baremetal-amd64 qemu-baremetal

baremetal-amd64: check-submodule $(BARE_BIN)
	@echo "Baremetal ELF: $(BARE_BIN)"

$(BDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(BCC) $(BFLAGS) -c $< -o $@

# Assembly rule: strip -include flag (assembler can't process C headers)
BFLAGS_ASM := $(filter-out -include %nexs_common.h,$(BFLAGS))
$(BDIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(BAS) $(BFLAGS_ASM) -x assembler-with-cpp -c $< -o $@

$(BARE_BIN): $(BARE_OBJS)
	$(BCC) -T base-nexs/hal/amd64/nexs.ld -nostdlib \
	       $(BARE_OBJS) \
	       /usr/local/Cellar/x86_64-elf-gcc/15.2.0/lib/gcc/x86_64-elf/15.2.0/libgcc.a \
	       -o $@
	@echo "Linked: $@"

# QEMU test: boot baremetal ELF, dump serial output, exit after 5s
qemu-baremetal: baremetal-amd64
	@echo "=== QEMU baremetal test ==="
	qemu-system-x86_64 \
	    -kernel $(BARE_BIN) \
	    -nographic \
	    -no-reboot \
	    -m 128M \
	    -device isa-debug-exit,iobase=0x501,iosize=0x01 \
	    2>&1 | timeout 8 cat || true
	@echo "=== QEMU test done ==="

clean:
	rm -rf build/
