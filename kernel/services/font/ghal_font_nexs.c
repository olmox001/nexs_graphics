/* services/font/ghal_font_nexs.c — Font service NEXS integration layer
 *
 * Wraps ghal_font_ttf.h in Value-native builtins, publishes font metadata
 * into the NEXS registry under /dev/font/<id>/, and self-registers.
 *
 * .nx API:
 *   font_load(path str, size float) → int   (handle id)
 *   font_draw(win int, id int, x int, y int, text str, color int) → int
 *   font_draw_wrap(win int, id int, x int, y int, maxw int, text str, color int) → int
 *   font_measure(id int, text str) → int
 *   font_line_height(id int) → int
 *   font_ascent(id int) → int
 *   font_free(id int) → int
 *
 * Registry layout:
 *   /dev/font/<id>/path        str
 *   /dev/font/<id>/size        int  (pixel height, truncated)
 *   /dev/font/<id>/line_height int
 *   /dev/font/<id>/ascent      int
 *   /dev/font/<id>/handle      int  (uintptr_t of GhalFontTTF*)
 */

#ifndef NEXS_API
#define NEXS_API
#endif
#ifndef NEXS_HAL_H
#define NEXS_HAL_H
#endif

#include "../../include/ghal_font_ttf.h"
#include "../../include/ghal_service.h"
#include "../../include/ghal.h"
#include "../../include/ghal_2d.h"

#include "nexs_fn.h"
#include "nexs_value.h"
#include "nexs_registry.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── Font handle table ─────────────────────────────────────────── */

#define FONT_MAX 32

typedef struct {
    uint32_t      id;
    GhalFontTTF  *ttf;
    float         size_px;
    char          path[256];
    int           in_use;
} FontEntry;

static FontEntry s_fonts[FONT_MAX];
static uint32_t  s_next_id = 1;

static FontEntry *font_alloc(void) {
    for (int i = 0; i < FONT_MAX; i++)
        if (!s_fonts[i].in_use) { s_fonts[i].in_use = 1; return &s_fonts[i]; }
    return NULL;
}
static FontEntry *font_find(uint32_t id) {
    for (int i = 0; i < FONT_MAX; i++)
        if (s_fonts[i].in_use && s_fonts[i].id == id) return &s_fonts[i];
    return NULL;
}
static void font_publish(const FontEntry *e) {
    char p[128];
#define P(sub, v) snprintf(p,sizeof(p),"/dev/font/%u/" sub, e->id); \
                  reg_set(p, v, RK_READ)
    P("path",        VAL_STR(e->path));
    P("size",        VAL_INT((int64_t)e->size_px));
    P("line_height", VAL_INT(ghal_font_ttf_line_height(e->ttf)));
    P("ascent",      VAL_INT(ghal_font_ttf_ascent(e->ttf)));
    P("handle",      VAL_INT((int64_t)(uintptr_t)e->ttf));
#undef P
}

/* Resolve window surface from NEXS registry (/dev/win/<id>/surface) */
static GHalSurface *resolve_win_surface(uint32_t win_id) {
    char rp[64];
    snprintf(rp, sizeof(rp), "/dev/win/%u/surface", win_id);
    Value sv = reg_get(rp);
    return (GHalSurface *)(uintptr_t)(uint64_t)sv.ival;
}

/* ── Builtins ─────────────────────────────────────────────────── */

static Value bi_font_load(Value *args, int n) {
    if (n < 2) return val_err(1, "font_load: need path size");
    const char *path = (const char *)args[0].data;
    float       size = (float)args[1].fval;
    if (!path) return val_err(1, "font_load: nil path");
    if (size <= 0) size = 16.0f;

    FontEntry *e = font_alloc();
    if (!e) return val_err(-1, "font_load: handle table full");

    e->ttf = ghal_font_ttf_load(path, size);
    if (!e->ttf) {
        e->in_use = 0;
        return val_err(-1, "font_load: TTF load failed");
    }
    e->id      = s_next_id++;
    e->size_px = size;
    strncpy(e->path, path, sizeof(e->path) - 1);
    font_publish(e);
    return val_int((int64_t)e->id);
}

static Value bi_font_draw(Value *args, int n) {
    if (n < 6) return val_err(1, "font_draw: need win id x y text color");
    uint32_t    win_id  = (uint32_t)args[0].ival;
    uint32_t    font_id = (uint32_t)args[1].ival;
    int32_t     x       = (int32_t)args[2].ival;
    int32_t     y       = (int32_t)args[3].ival;
    const char *text    = (const char *)args[4].data;
    uint32_t    color   = (uint32_t)args[5].ival;

    FontEntry   *fe  = font_find(font_id);
    GHalSurface *dst = resolve_win_surface(win_id);
    if (!fe)  return val_err(-1, "font_draw: unknown font id");
    if (!dst) return val_err(-1, "font_draw: unknown window id");
    if (!text) text = "";

    ghal_font_ttf_draw(dst, fe->ttf, x, y, text, color);
    return val_int(0);
}

static Value bi_font_draw_wrap(Value *args, int n) {
    if (n < 7) return val_err(1, "font_draw_wrap: need win id x y maxw text color");
    uint32_t    win_id  = (uint32_t)args[0].ival;
    uint32_t    font_id = (uint32_t)args[1].ival;
    int32_t     x       = (int32_t)args[2].ival;
    int32_t     y       = (int32_t)args[3].ival;
    int32_t     maxw    = (int32_t)args[4].ival;
    const char *text    = (const char *)args[5].data;
    uint32_t    color   = (uint32_t)args[6].ival;

    FontEntry   *fe  = font_find(font_id);
    GHalSurface *dst = resolve_win_surface(win_id);
    if (!fe || !dst) return val_err(-1, "font_draw_wrap: invalid args");
    if (!text) text = "";

    int end_y = ghal_font_ttf_draw_wrapped(dst, fe->ttf, x, y, maxw, text, color);
    return val_int(end_y);
}

static Value bi_font_measure(Value *args, int n) {
    if (n < 2) return val_err(1, "font_measure: need id text");
    uint32_t    id   = (uint32_t)args[0].ival;
    const char *text = (const char *)args[1].data;
    FontEntry  *fe   = font_find(id);
    if (!fe) return val_err(-1, "font_measure: unknown id");
    return val_int(ghal_font_ttf_measure(fe->ttf, text ? text : ""));
}

static Value bi_font_line_height(Value *args, int n) {
    if (n < 1) return val_err(1, "font_line_height: need id");
    FontEntry *fe = font_find((uint32_t)args[0].ival);
    if (!fe) return val_err(-1, "font_line_height: unknown id");
    return val_int(ghal_font_ttf_line_height(fe->ttf));
}

static Value bi_font_ascent(Value *args, int n) {
    if (n < 1) return val_err(1, "font_ascent: need id");
    FontEntry *fe = font_find((uint32_t)args[0].ival);
    if (!fe) return val_err(-1, "font_ascent: unknown id");
    return val_int(ghal_font_ttf_ascent(fe->ttf));
}

static Value bi_font_free(Value *args, int n) {
    if (n < 1) return val_err(1, "font_free: need id");
    uint32_t   id = (uint32_t)args[0].ival;
    FontEntry *fe = font_find(id);
    if (!fe) return val_err(-1, "font_free: unknown id");
    ghal_font_ttf_free(fe->ttf);
    char p[80];
    snprintf(p, sizeof(p), "/dev/font/%u", id);
    reg_delete(p);
    memset(fe, 0, sizeof(*fe));
    return val_int(0);
}

/* ── Module descriptor ────────────────────────────────────────── */

static const GhalBuiltinDef s_builtins[] = {
    { "font_load",
      "font_load(path str, size float) -> int",
      bi_font_load },
    { "font_draw",
      "font_draw(win int, id int, x int, y int, text str, color int) -> int",
      bi_font_draw },
    { "font_draw_wrap",
      "font_draw_wrap(win int, id int, x int, y int, maxw int, text str, color int) -> int",
      bi_font_draw_wrap },
    { "font_measure",
      "font_measure(id int, text str) -> int",
      bi_font_measure },
    { "font_line_height",
      "font_line_height(id int) -> int",
      bi_font_line_height },
    { "font_ascent",
      "font_ascent(id int) -> int",
      bi_font_ascent },
    { "font_free",
      "font_free(id int) -> int",
      bi_font_free },
};

static void font_mod_init(void) {
    memset(s_fonts, 0, sizeof(s_fonts));
    s_next_id = 1;
}
static void font_mod_shutdown(void) {
    for (int i = 0; i < FONT_MAX; i++) {
        if (s_fonts[i].in_use) ghal_font_ttf_free(s_fonts[i].ttf);
    }
    memset(s_fonts, 0, sizeof(s_fonts));
}

static GhalServiceModule s_font_module = {
    .name        = "font",
    .version     = "1.0",
    .reg_prefix  = "/dev/font",
    .init        = font_mod_init,
    .shutdown    = font_mod_shutdown,
    .builtins    = s_builtins,
    .n_builtins  = (int)(sizeof(s_builtins) / sizeof(s_builtins[0])),
};

GHAL_SERVICE_REGISTER(s_font_module)
