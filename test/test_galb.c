/* test/test_galb.c — Unit tests for GALB bytecode VM
 *
 * Tests assembler + VM round-trip without a live display.
 * Stubs out ghal_win_open / ghal_surface_* so no platform needed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ── Minimal stubs for GHAL API ─────────────────────────────── */
#ifndef NEXS_API
#define NEXS_API
#endif
typedef unsigned long uintptr_t;

#include "ghal.h"
#include "ghal_bc.h"
#include "ghal_2d.h"

/* Test stubs */
static int g_win_open_calls = 0;
static int g_surf_create_calls = 0;
static int g_draw_rect_calls = 0;

GHalDriver *g_ghal_driver = NULL;

int ghal_win_open(GHalWindow *w, const char *title, uint32_t width, uint32_t height) {
    (void)title;
    w->id = 1;
    w->width = width;
    w->height = height;
    g_win_open_calls++;
    return 0;
}
void ghal_win_close(GHalWindow *w) { (void)w; }
void ghal_win_set_title(GHalWindow *w, const char *t) { (void)w; (void)t; }
void ghal_win_resize(GHalWindow *w, uint32_t ww, uint32_t h) { (void)w; (void)ww; (void)h; }

int ghal_surface_create(GHalSurface *s, uint32_t w, uint32_t h, uint32_t fmt) {
    s->stride = w * 4;
    s->pixel_size = (size_t)s->stride * h;
    s->pixels = calloc(1, s->pixel_size);
    s->width = w; s->height = h; s->format = fmt;
    g_surf_create_calls++;
    return s->pixels ? 0 : -1;
}
void ghal_surface_destroy(GHalSurface *s) { free(s->pixels); s->pixels = NULL; }
int  ghal_surface_lock(GHalSurface *s)    { (void)s; return 0; }
void ghal_surface_unlock(GHalSurface *s)  { (void)s; }
void ghal_surface_present(GHalWindow *w, GHalSurface *s) { (void)w; (void)s; }
uint32_t ghal_vsync(void) { return 16667; }

extern uint8_t *galb_asm_hello(uint32_t w, uint32_t h, const char *title, uint32_t *len);
extern void galb_disasm(const uint8_t *code, uint32_t len);

/* draw2d stubs */
void draw2d_fill_rect(GHalSurface *s, int32_t x, int32_t y,
                      int32_t w, int32_t h, uint32_t color) {
    (void)x; (void)y; (void)w; (void)h; (void)color;
    if (s && s->pixels) g_draw_rect_calls++;
}
void draw2d_line(GHalSurface *s, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c) {
    (void)s; (void)x0; (void)y0; (void)x1; (void)y1; (void)c;
}
void draw2d_clear(GHalSurface *s, uint32_t c) { (void)s; (void)c; }
void draw2d_blit(GHalSurface *d, const GHalSurface *s, int32_t dx, int32_t dy) {
    (void)d; (void)s; (void)dx; (void)dy;
}
void draw2d_text(GHalSurface *s, int32_t x, int32_t y, const char *str, uint32_t c) {
    (void)s; (void)x; (void)y; (void)str; (void)c;
}
void draw2d_set_clip(GHalSurface *s, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)s; (void)x; (void)y; (void)w; (void)h;
}
void draw2d_reset_clip(GHalSurface *s) { (void)s; }

/* ── Test helpers ────────────────────────────────────────────── */
static int g_pass = 0, g_fail = 0;
#define EXPECT(cond, name) do { \
    if (cond) { printf("  PASS: %s\n", name); g_pass++; } \
    else { printf("  FAIL: %s\n", name); g_fail++; } \
} while(0)

/* ── Test: assemble + disassemble ─────────────────────────────── */
static void test_asm_disasm(void) {
    printf("test_asm_disasm:\n");
    uint32_t len = 0;
    uint8_t *code = galb_asm_hello(320, 240, "Test", &len);
    EXPECT(code != NULL, "galb_asm_hello returns non-NULL");
    EXPECT(len > 0, "bytecode length > 0");
    EXPECT(code[len - 1] == 0xFF, "last byte is HALT (0xFF)");

    printf("  --- disassembly ---\n");
    galb_disasm(code, len);
    printf("  --- end ---\n");

    free(code);
}

/* ── Test: VM init ────────────────────────────────────────────── */
static void test_vm_init(void) {
    printf("test_vm_init:\n");
    static const uint8_t halt_prog[] = { 0xFF };
    GalbVM vm;
    int rc = galb_vm_init(&vm, halt_prog, sizeof(halt_prog));
    EXPECT(rc == 0, "galb_vm_init succeeds");
    EXPECT(vm.pc == 0, "pc starts at 0");
    EXPECT(vm.halted == 0, "not halted initially");
}

/* ── Test: VM halt ────────────────────────────────────────────── */
static void test_vm_halt(void) {
    printf("test_vm_halt:\n");
    static const uint8_t prog[] = { 0xFF };
    GalbVM vm;
    galb_vm_init(&vm, prog, sizeof(prog));
    int rc = galb_vm_run(&vm);
    EXPECT(rc == 0, "run returns 0");
    EXPECT(vm.halted == 1, "halted after HALT opcode");
}

/* ── Test: DRAW_CLEAR + DRAW_RECT ─────────────────────────────── */
static void test_vm_draw(void) {
    printf("test_vm_draw:\n");

    /* Build bytecode manually:
     * SURF_CREATE s0 64 64 BGRA8
     * DRAW_CLEAR s0 0x222222FF
     * DRAW_RECT  s0 10 10 20 20 0xFF4488FF
     * SURF_DESTROY s0
     * HALT
     */
    uint8_t prog[128];
    int pc = 0;
#define P8(v)  prog[pc++] = (uint8_t)(v)
#define P32(v) do { uint32_t _v = (uint32_t)(v); \
    prog[pc++]=_v&0xFF; prog[pc++]=(_v>>8)&0xFF; \
    prog[pc++]=(_v>>16)&0xFF; prog[pc++]=(_v>>24)&0xFF; } while(0)

    P8(0x40); P8(0); P32(64); P32(64); P32(1);   /* SURF_CREATE */
    P8(0x64); P8(0); P32(0x222222FF);              /* DRAW_CLEAR */
    P8(0x60); P8(0); P32(10); P32(10); P32(20); P32(20); P32(0xFF4488FF); /* DRAW_RECT */
    P8(0x41); P8(0);                               /* SURF_DESTROY */
    P8(0xFF);                                      /* HALT */
#undef P8
#undef P32

    g_surf_create_calls = 0;
    g_draw_rect_calls   = 0;

    GalbVM vm;
    galb_vm_init(&vm, prog, (uint32_t)pc);
    int rc = galb_vm_run(&vm);

    EXPECT(rc == 0, "run succeeds");
    EXPECT(vm.halted == 1, "halted at HALT");
    EXPECT(g_surf_create_calls == 1, "surface created once");
    EXPECT(g_draw_rect_calls   == 1, "draw_rect called once");
}

/* ── Test: WIN_OPEN + WIN_CLOSE ───────────────────────────────── */
static void test_vm_window(void) {
    printf("test_vm_window:\n");

    /* Build: WIN_OPEN w0 "Hello" 320 240; WIN_CLOSE w0; HALT */
    uint8_t prog[128];
    int pc = 0;
#define P8(v)  prog[pc++] = (uint8_t)(v)
#define P32(v) do { uint32_t _v = (uint32_t)(v); \
    prog[pc++]=_v&0xFF; prog[pc++]=(_v>>8)&0xFF; \
    prog[pc++]=(_v>>16)&0xFF; prog[pc++]=(_v>>24)&0xFF; } while(0)

    P8(0x50); P8(0);  /* WIN_OPEN dst=0 */
    /* title string NUL-terminated */
    const char *t = "Hello";
    for (const char *p = t; *p; p++) P8((uint8_t)*p);
    P8(0);            /* NUL */
    P32(320); P32(240);

    P8(0x51); P8(0);  /* WIN_CLOSE */
    P8(0xFF);         /* HALT */
#undef P8
#undef P32

    g_win_open_calls = 0;
    GalbVM vm;
    galb_vm_init(&vm, prog, (uint32_t)pc);
    int rc = galb_vm_run(&vm);

    EXPECT(rc == 0, "window run succeeds");
    EXPECT(g_win_open_calls == 1, "win_open called");
}

static void test_g_nx_compiler(void) {
    printf("test_g_nx_compiler:\n");
    const char *script =
        "win_open(\"Test Compile\", 640, 480)\n"
        "loop 5 {\n"
        "    draw_clear(572667391)\n"
        "    draw_rect(10, 20, 100, 200, 4282747135)\n"
        "    draw_text(30, 40, \"Compiled Text\", 4294967295)\n"
        "    surface_blit()\n"
        "    vsync()\n"
        "}\n"
        "win_close()\n";

    g_win_open_calls = 0;
    g_surf_create_calls = 0;
    g_draw_rect_calls = 0;

    uint32_t len = 0;
    uint8_t *code = ghal_compile_g_nx(script, &len);
    EXPECT(code != NULL, "ghal_compile_g_nx returns non-NULL");
    EXPECT(len > 0, "compiled code length > 0");

    printf("  --- compiled disassembly ---\n");
    galb_disasm(code, len);
    printf("  --- end ---\n");

    GalbVM vm;
    int rc_init = galb_vm_init(&vm, code, len);
    EXPECT(rc_init == 0, "VM init of compiled code succeeds");

    int rc_run = galb_vm_run(&vm);
    EXPECT(rc_run == 0, "VM execution of compiled code succeeds");
    EXPECT(vm.halted == 1, "VM reaches halt state");
    
    EXPECT(g_win_open_calls == 1, "win_open called once");
    EXPECT(g_surf_create_calls == 1, "surf_create called once");
    EXPECT(g_draw_rect_calls == 5, "draw_rect called 5 times inside the compiled loop");

    free(code);
}

int main(void) {
    printf("=== GHAL GALB VM unit tests ===\n");
    test_asm_disasm();
    test_vm_init();
    test_vm_halt();
    test_vm_draw();
    test_vm_window();
    test_g_nx_compiler();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return (g_fail > 0) ? 1 : 0;
}
