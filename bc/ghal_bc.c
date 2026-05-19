/* bc/ghal_bc.c — GALB bytecode VM executor
 *
 * Extends the NEXS HALB interpreter with graphics opcodes (0x40-0x7F).
 * Stack machine: operands pushed as uint64_t registers[0..GALB_MAX_REGS-1].
 * Each opcode pops its args from the register file (r0, r1, ...).
 *
 * Platform-independent — no platform includes.
 */

#include "../include/ghal_bc.h"
#include "../include/ghal.h"
#include "../include/ghal_2d.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ── VM init ─────────────────────────────────────────────────── */

int galb_vm_init(GalbVM *vm, const uint8_t *code, uint32_t len) {
    if (!vm || !code || len == 0) return -1;
    memset(vm, 0, sizeof(*vm));
    vm->code     = code;
    vm->code_len = len;
    vm->pc       = 0;
    vm->halted   = 0;
    return 0;
}

/* ── Helper: pop u64 from register file (reg[0] = top) ──────── */
static inline uint64_t pop_reg(GalbVM *vm, int slot) {
    return vm->regs[slot % GALB_MAX_REGS];
}

static inline uint32_t fetch_u32(GalbVM *vm) {
    if (vm->pc + 4 > vm->code_len) { vm->halted = 1; return 0; }
    uint32_t v;
    memcpy(&v, vm->code + vm->pc, 4);
    vm->pc += 4;
    return v;
}

static inline uint8_t fetch_u8(GalbVM *vm) {
    if (vm->pc >= vm->code_len) { vm->halted = 1; return 0; }
    return vm->code[vm->pc++];
}

/* Fetch a NUL-terminated string from bytecode (max 128 chars) */
static inline const char *fetch_str(GalbVM *vm, char *buf, size_t bufsz) {
    size_t i = 0;
    while (vm->pc < vm->code_len && i < bufsz - 1) {
        buf[i] = (char)vm->code[vm->pc++];
        if (buf[i] == '\0') break;
        i++;
    }
    buf[i] = '\0';
    return buf;
}

/* ── Single-step the VM ──────────────────────────────────────── */

int galb_vm_step(GalbVM *vm) {
    if (!vm || vm->halted) return -1;
    if (vm->pc >= vm->code_len) { vm->halted = 1; return 0; }

    uint8_t op = fetch_u8(vm);

    /* HALB halt opcode (0xFF) */
    if (op == 0xFF) { vm->halted = 1; return 0; }

    /* GALB graphics opcodes */
    switch ((GalOpcode)op) {

    /* ── GALO_SURF_CREATE: reg[0]=width reg[1]=height reg[2]=fmt → surf_reg */
    case GALO_SURF_CREATE: {
        uint8_t  dst = fetch_u8(vm);
        uint32_t w   = fetch_u32(vm);
        uint32_t h   = fetch_u32(vm);
        uint32_t fmt = fetch_u32(vm);
        int sidx = dst % GALB_MAX_SURFS;
        if (!vm->surfs[sidx]) {
            vm->surfs[sidx] = (GHalSurface *)malloc(sizeof(GHalSurface));
            if (!vm->surfs[sidx]) return -1;
        }
        memset(vm->surfs[sidx], 0, sizeof(GHalSurface));
        ghal_surface_create(vm->surfs[sidx], w, h, fmt);
        vm->regs[dst % GALB_MAX_REGS] = (uint64_t)(uintptr_t)vm->surfs[sidx];
        break;
    }

    /* ── GALO_SURF_DESTROY: reg[N] = surf ptr */
    case GALO_SURF_DESTROY: {
        uint8_t src = fetch_u8(vm);
        int sidx = src % GALB_MAX_SURFS;
        if (vm->surfs[sidx]) {
            ghal_surface_destroy(vm->surfs[sidx]);
            free(vm->surfs[sidx]);
            vm->surfs[sidx] = NULL;
        }
        break;
    }

    /* ── GALO_SURF_LOCK */
    case GALO_SURF_LOCK: {
        uint8_t src = fetch_u8(vm);
        int sidx = src % GALB_MAX_SURFS;
        if (vm->surfs[sidx]) ghal_surface_lock(vm->surfs[sidx]);
        break;
    }

    /* ── GALO_SURF_UNLOCK */
    case GALO_SURF_UNLOCK: {
        uint8_t src = fetch_u8(vm);
        int sidx = src % GALB_MAX_SURFS;
        if (vm->surfs[sidx]) ghal_surface_unlock(vm->surfs[sidx]);
        break;
    }

    /* ── GALO_SURF_PRESENT: win_reg surf_reg */
    case GALO_SURF_PRESENT: {
        uint8_t wr = fetch_u8(vm);
        uint8_t sr = fetch_u8(vm);
        int widx = wr % GALB_MAX_WINS;
        int sidx = sr % GALB_MAX_SURFS;
        if (vm->wins[widx] && vm->surfs[sidx])
            ghal_surface_present(vm->wins[widx], vm->surfs[sidx]);
        break;
    }

    /* ── GALO_WIN_OPEN: title(str) w(u32) h(u32) → win_reg */
    case GALO_WIN_OPEN: {
        uint8_t dst = fetch_u8(vm);
        char title[128] = {0};
        fetch_str(vm, title, sizeof(title));
        uint32_t w = fetch_u32(vm);
        uint32_t h = fetch_u32(vm);
        int widx = dst % GALB_MAX_WINS;
        if (!vm->wins[widx])
            vm->wins[widx] = (GHalWindow *)calloc(1, sizeof(GHalWindow));
        if (vm->wins[widx])
            ghal_win_open(vm->wins[widx], title, w, h);
        vm->regs[dst % GALB_MAX_REGS] = (uint64_t)(uintptr_t)vm->wins[widx];
        break;
    }

    /* ── GALO_WIN_CLOSE: win_reg */
    case GALO_WIN_CLOSE: {
        uint8_t wr = fetch_u8(vm);
        int widx = wr % GALB_MAX_WINS;
        if (vm->wins[widx]) {
            ghal_win_close(vm->wins[widx]);
            free(vm->wins[widx]);
            vm->wins[widx] = NULL;
        }
        break;
    }

    /* ── GALO_WIN_TITLE: win_reg title(str) */
    case GALO_WIN_TITLE: {
        uint8_t wr = fetch_u8(vm);
        char title[128] = {0};
        fetch_str(vm, title, sizeof(title));
        int widx = wr % GALB_MAX_WINS;
        if (vm->wins[widx]) ghal_win_set_title(vm->wins[widx], title);
        break;
    }

    /* ── GALO_WIN_RESIZE: win_reg w h */
    case GALO_WIN_RESIZE: {
        uint8_t  wr = fetch_u8(vm);
        uint32_t w  = fetch_u32(vm);
        uint32_t h  = fetch_u32(vm);
        int widx = wr % GALB_MAX_WINS;
        if (vm->wins[widx]) ghal_win_resize(vm->wins[widx], w, h);
        break;
    }

    /* ── GALO_WIN_SHOW: win_reg visible(u8) */
    case GALO_WIN_SHOW: {
        uint8_t wr  = fetch_u8(vm);
        uint8_t vis = fetch_u8(vm);
        int widx = wr % GALB_MAX_WINS;
        if (vm->wins[widx]) vm->wins[widx]->visible = (vis != 0);
        break;
    }

    /* ── GALO_DRAW_RECT: surf_reg x y w h color */
    case GALO_DRAW_RECT: {
        uint8_t  sr    = fetch_u8(vm);
        uint32_t x     = fetch_u32(vm);
        uint32_t y     = fetch_u32(vm);
        uint32_t w     = fetch_u32(vm);
        uint32_t h     = fetch_u32(vm);
        uint32_t color = fetch_u32(vm);
        int sidx = sr % GALB_MAX_SURFS;
        if (vm->surfs[sidx])
            draw2d_fill_rect(vm->surfs[sidx], (int32_t)x, (int32_t)y,
                             (int32_t)w, (int32_t)h, color);
        break;
    }

    /* ── GALO_DRAW_LINE: surf_reg x0 y0 x1 y1 color */
    case GALO_DRAW_LINE: {
        uint8_t  sr    = fetch_u8(vm);
        uint32_t x0    = fetch_u32(vm);
        uint32_t y0    = fetch_u32(vm);
        uint32_t x1    = fetch_u32(vm);
        uint32_t y1    = fetch_u32(vm);
        uint32_t color = fetch_u32(vm);
        int sidx = sr % GALB_MAX_SURFS;
        if (vm->surfs[sidx])
            draw2d_line(vm->surfs[sidx], (int32_t)x0, (int32_t)y0,
                        (int32_t)x1, (int32_t)y1, color);
        break;
    }

    /* ── GALO_DRAW_BLIT: dst_reg src_reg dx dy */
    case GALO_DRAW_BLIT: {
        uint8_t  dr = fetch_u8(vm);
        uint8_t  sr = fetch_u8(vm);
        uint32_t dx = fetch_u32(vm);
        uint32_t dy = fetch_u32(vm);
        int didx = dr % GALB_MAX_SURFS;
        int sidx = sr % GALB_MAX_SURFS;
        if (vm->surfs[didx] && vm->surfs[sidx])
            draw2d_blit(vm->surfs[didx], vm->surfs[sidx],
                        (int32_t)dx, (int32_t)dy);
        break;
    }

    /* ── GALO_DRAW_TEXT: surf_reg x y str color */
    case GALO_DRAW_TEXT: {
        uint8_t  sr    = fetch_u8(vm);
        uint32_t x     = fetch_u32(vm);
        uint32_t y     = fetch_u32(vm);
        char     str[256] = {0};
        fetch_str(vm, str, sizeof(str));
        uint32_t color = fetch_u32(vm);
        int sidx = sr % GALB_MAX_SURFS;
        if (vm->surfs[sidx])
            draw2d_text(vm->surfs[sidx], (int32_t)x, (int32_t)y,
                        str, color);
        break;
    }

    /* ── GALO_DRAW_CLEAR: surf_reg color */
    case GALO_DRAW_CLEAR: {
        uint8_t  sr    = fetch_u8(vm);
        uint32_t color = fetch_u32(vm);
        int sidx = sr % GALB_MAX_SURFS;
        if (vm->surfs[sidx]) draw2d_clear(vm->surfs[sidx], color);
        break;
    }

    /* ── GPU opcodes (stubs; Phase 8) */
    case GALO_GPU_BEGIN:
    case GALO_GPU_SUBMIT:
    case GALO_GPU_END:
        fetch_u8(vm);  /* skip win_reg / data args */
        break;

    /* ── GALO_GPU_VSYNC: wait for vblank */
    case GALO_GPU_VSYNC:
        ghal_vsync();
        break;

    default:
        fprintf(stderr, "GALB: unknown opcode 0x%02X at pc=%u\n",
                op, vm->pc - 1);
        vm->halted = 1;
        return -1;
    }

    return 0;
}

/* ── Run until halt ──────────────────────────────────────────── */

int galb_vm_run(GalbVM *vm) {
    if (!vm) return -1;
    int steps = 0;
    while (!vm->halted && steps < 1000000) {
        if (galb_vm_step(vm) != 0) return -1;
        steps++;
    }
    return vm->halted ? 0 : -1;  /* -1 if step limit exceeded */
}
