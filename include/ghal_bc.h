/* include/ghal_bc.h — GALB: Graphics ABI Layer Bytecode
 *
 * Extends HALB (nexs_hal_bc.h) with graphics opcodes.
 * Magic: "GALB" v1.
 *
 * HALB opcodes 0x00-0x3F remain valid.
 * Graphics opcodes use 0x40-0x7F (non-overlapping).
 *
 * Virtual device paths:
 *   /dev/fb          → raw framebuffer (read/write pixel data)
 *   /dev/win/<id>    → window ctl (resize/title/show/hide)
 *   /dev/gpu         → GPU command stream
 *   /dev/compositor  → compositor notifications
 */

#ifndef GHAL_BC_H
#define GHAL_BC_H
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GALB_MAGIC   "GALB"
#define GALB_VERSION  1

typedef enum {
    /* Surface ops */
    GALO_SURF_CREATE  = 0x40,   /* width height fmt → surf_reg  */
    GALO_SURF_DESTROY = 0x41,   /* surf_reg                     */
    GALO_SURF_LOCK    = 0x42,   /* surf_reg → ptr               */
    GALO_SURF_UNLOCK  = 0x43,   /* surf_reg                     */
    GALO_SURF_PRESENT = 0x44,   /* win_reg surf_reg             */

    /* Window ops */
    GALO_WIN_OPEN     = 0x50,   /* title w h → win_reg          */
    GALO_WIN_CLOSE    = 0x51,   /* win_reg                      */
    GALO_WIN_TITLE    = 0x52,   /* win_reg title_str            */
    GALO_WIN_RESIZE   = 0x53,   /* win_reg w h                  */
    GALO_WIN_SHOW     = 0x54,   /* win_reg visible:u8           */

    /* 2D draw ops (on CPU-locked surface) */
    GALO_DRAW_RECT    = 0x60,   /* surf_reg x y w h color       */
    GALO_DRAW_LINE    = 0x61,   /* surf_reg x0 y0 x1 y1 color   */
    GALO_DRAW_BLIT    = 0x62,   /* dst_reg src_reg dx dy        */
    GALO_DRAW_TEXT    = 0x63,   /* surf_reg x y str color       */
    GALO_DRAW_CLEAR   = 0x64,   /* surf_reg color               */

    /* GPU command stream */
    GALO_GPU_BEGIN    = 0x70,   /* win_reg                      */
    GALO_GPU_SUBMIT   = 0x71,   /* data_str len                 */
    GALO_GPU_END      = 0x72,   /* win_reg                      */
    GALO_GPU_VSYNC    = 0x73,   /* wait for vblank              */
} GalOpcode;

/* GALB bytecode file header */
typedef struct {
    char     magic[4];    /* "GALB" */
    uint8_t  version;     /* GALB_VERSION */
    uint8_t  _pad[3];
    uint32_t code_len;    /* bytes of bytecode following this header */
    uint32_t entry_off;   /* entry point offset within code */
} GalbHeader;

/* GALB VM state (holds register file and surface/window refs) */
#define GALB_MAX_REGS   16
#define GALB_MAX_SURFS   8
#define GALB_MAX_WINS    8

struct GHalSurface;
struct GHalWindow;

typedef struct {
    uint64_t regs[GALB_MAX_REGS];
    struct GHalSurface *surfs[GALB_MAX_SURFS];
    struct GHalWindow  *wins [GALB_MAX_WINS];
    const uint8_t *code;
    uint32_t       code_len;
    uint32_t       pc;
    int            halted;
} GalbVM;

/* VM API */
int  galb_vm_init(GalbVM *vm, const uint8_t *code, uint32_t len);
int  galb_vm_step(GalbVM *vm);
int  galb_vm_run(GalbVM *vm);

#ifdef __cplusplus
}
#endif

#endif /* GHAL_BC_H */
