/* bc/ghal_asm.c — GALB bytecode assembler helpers
 *
 * Provides functions to programmatically build GALB bytecode buffers.
 * Used by tests and by the compiler back-end in Phase 8.
 */

#include "../include/ghal_bc.h"
#include "../include/ghal.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ── GalbBuf — growable bytecode buffer ──────────────────────── */

typedef struct {
    uint8_t *data;
    uint32_t len;
    uint32_t cap;
} GalbBuf;

static int galb_buf_init(GalbBuf *b) {
    b->cap  = 256;
    b->len  = 0;
    b->data = (uint8_t *)malloc(b->cap);
    return b->data ? 0 : -1;
}

static int galb_buf_ensure(GalbBuf *b, uint32_t need) {
    if (b->len + need <= b->cap) return 0;
    uint32_t newcap = b->cap * 2 + need;
    uint8_t *p = (uint8_t *)realloc(b->data, newcap);
    if (!p) return -1;
    b->data = p;
    b->cap  = newcap;
    return 0;
}

static void galb_buf_free(GalbBuf *b) __attribute__((unused));
static void galb_buf_free(GalbBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len  = 0;
    b->cap  = 0;
}

/* ── Emit helpers ─────────────────────────────────────────────── */

static void emit_u8(GalbBuf *b, uint8_t v) {
    if (galb_buf_ensure(b, 1) == 0)
        b->data[b->len++] = v;
}

static void emit_u32(GalbBuf *b, uint32_t v) {
    if (galb_buf_ensure(b, 4) != 0) return;
    memcpy(b->data + b->len, &v, 4);
    b->len += 4;
}

static void emit_str(GalbBuf *b, const char *s) {
    size_t n = strlen(s) + 1;  /* include NUL */
    if (galb_buf_ensure(b, (uint32_t)n) != 0) return;
    memcpy(b->data + b->len, s, n);
    b->len += (uint32_t)n;
}

/* ── Public assembler API ────────────────────────────────────── */

/* Assemble a simple "hello GHAL" program that:
 *   1. Opens a window
 *   2. Creates a surface
 *   3. Clears to dark background
 *   4. Draws a colored rectangle
 *   5. Draws text
 *   6. Presents the surface
 *   7. Waits for vsync
 *   8. Halts
 *
 * Returns malloc'd buffer (caller must free) or NULL on error.
 * Sets *out_len to the bytecode length.
 */
uint8_t *galb_asm_hello(uint32_t win_w, uint32_t win_h,
                         const char *title,
                         uint32_t *out_len) {
    GalbBuf b;
    if (galb_buf_init(&b) != 0) return NULL;

    /* GALO_WIN_OPEN: reg[0] ← win ptr */
    emit_u8(&b,  (uint8_t)GALO_WIN_OPEN);
    emit_u8(&b,  0);  /* dst = reg[0]/win[0] */
    emit_str(&b, title);
    emit_u32(&b, win_w);
    emit_u32(&b, win_h);

    /* GALO_SURF_CREATE: surf[0] */
    emit_u8(&b,  (uint8_t)GALO_SURF_CREATE);
    emit_u8(&b,  0);             /* dst surf[0] */
    emit_u32(&b, win_w);
    emit_u32(&b, win_h);
    emit_u32(&b, GHAL_FMT_BGRA8);

    /* GALO_SURF_LOCK */
    emit_u8(&b, (uint8_t)GALO_SURF_LOCK);
    emit_u8(&b, 0);

    /* GALO_DRAW_CLEAR: dark background */
    emit_u8(&b,  (uint8_t)GALO_DRAW_CLEAR);
    emit_u8(&b,  0);             /* surf[0] */
    emit_u32(&b, GHAL_RGBA(0x22,0x22,0x22,0xFF));

    /* GALO_DRAW_RECT: pink rectangle */
    emit_u8(&b,  (uint8_t)GALO_DRAW_RECT);
    emit_u8(&b,  0);
    emit_u32(&b, 100); emit_u32(&b, 100);
    emit_u32(&b, 200); emit_u32(&b, 150);
    emit_u32(&b, GHAL_RGBA(0xFF,0x44,0x88,0xFF));

    /* GALO_DRAW_TEXT: "Hello GHAL" */
    emit_u8(&b,  (uint8_t)GALO_DRAW_TEXT);
    emit_u8(&b,  0);
    emit_u32(&b, 110); emit_u32(&b, 170);
    emit_str(&b, "Hello GHAL");
    emit_u32(&b, GHAL_RGBA(0xFF,0xFF,0xFF,0xFF));

    /* GALO_SURF_UNLOCK */
    emit_u8(&b, (uint8_t)GALO_SURF_UNLOCK);
    emit_u8(&b, 0);

    /* GALO_SURF_PRESENT */
    emit_u8(&b, (uint8_t)GALO_SURF_PRESENT);
    emit_u8(&b, 0);  /* win[0] */
    emit_u8(&b, 0);  /* surf[0] */

    /* GALO_GPU_VSYNC: wait one frame */
    emit_u8(&b, (uint8_t)GALO_GPU_VSYNC);

    /* Halt */
    emit_u8(&b, 0xFF);

    *out_len = b.len;
    return b.data;  /* caller owns */
}

/* Print disassembly of bytecode to stdout */
void galb_disasm(const uint8_t *code, uint32_t len) {
    uint32_t pc = 0;
    printf("GALB disassembly (%u bytes):\n", len);
    while (pc < len) {
        uint8_t op = code[pc++];
        printf("  %04X: ", pc - 1);
        switch ((GalOpcode)op) {
        case GALO_SURF_CREATE:  printf("SURF_CREATE\n"); pc += 1+4+4+4; break;
        case GALO_SURF_DESTROY: printf("SURF_DESTROY\n"); pc += 1; break;
        case GALO_SURF_LOCK:    printf("SURF_LOCK\n"); pc += 1; break;
        case GALO_SURF_UNLOCK:  printf("SURF_UNLOCK\n"); pc += 1; break;
        case GALO_SURF_PRESENT: printf("SURF_PRESENT\n"); pc += 2; break;
        case GALO_WIN_OPEN:     printf("WIN_OPEN\n"); pc += 1; /* skip str+u32+u32 */
                                while (pc < len && code[pc++] != 0) {}
                                pc += 8; break;
        case GALO_WIN_CLOSE:    printf("WIN_CLOSE\n"); pc += 1; break;
        case GALO_WIN_TITLE:    printf("WIN_TITLE\n"); pc += 1;
                                while (pc < len && code[pc++] != 0) {} break;
        case GALO_WIN_RESIZE:   printf("WIN_RESIZE\n"); pc += 1+4+4; break;
        case GALO_WIN_SHOW:     printf("WIN_SHOW\n"); pc += 2; break;
        case GALO_DRAW_RECT:    printf("DRAW_RECT\n"); pc += 1+4+4+4+4+4; break;
        case GALO_DRAW_LINE:    printf("DRAW_LINE\n"); pc += 1+4+4+4+4+4; break;
        case GALO_DRAW_BLIT:    printf("DRAW_BLIT\n"); pc += 2+4+4; break;
        case GALO_DRAW_TEXT:    printf("DRAW_TEXT\n"); pc += 1+4+4;
                                while (pc < len && code[pc++] != 0) {}
                                pc += 4; break;
        case GALO_DRAW_CLEAR:   printf("DRAW_CLEAR\n"); pc += 1+4; break;
        case GALO_GPU_BEGIN:    printf("GPU_BEGIN\n"); pc += 1; break;
        case GALO_GPU_SUBMIT:   printf("GPU_SUBMIT\n"); pc += 1; break;
        case GALO_GPU_END:      printf("GPU_END\n"); pc += 1; break;
        case GALO_GPU_VSYNC:    printf("GPU_VSYNC\n"); break;
        default:
            if (op == 0xFF) { printf("HALT\n"); return; }
            printf("UNKNOWN(0x%02X)\n", op);
            return;
        }
    }
}
