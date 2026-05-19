/* services/image/ghal_image_nexs.c — Image service NEXS integration layer
 *
 * Wraps ghal_image.h in Value-native builtins, publishes loaded images
 * into the NEXS registry under /dev/img/<id>/, and self-registers as a
 * GhalServiceModule so the service loader picks it up automatically.
 *
 * .nx API:
 *   img_load(path str)              → int   (handle id, -1 on error)
 *   img_load_mem(data str, len int) → int
 *   img_save(id int, path str)      → int   (0=ok, -1=err)
 *   img_save_fmt(id int, path str, fmt int) → int
 *   img_scale(id int, w int, h int) → int   (new handle)
 *   img_blit(win int, id int, x int, y int) → int
 *   img_info(id int)                → str   ("w=N,h=N,fmt=bgra8")
 *   img_free(id int)                → int
 *
 * Registry layout per loaded image:
 *   /dev/img/<id>/path    str
 *   /dev/img/<id>/width   int
 *   /dev/img/<id>/height  int
 *   /dev/img/<id>/surface int  (uintptr_t of GHalSurface*)
 */

#define NEXS_API
#ifndef NEXS_HAL_H
#define NEXS_HAL_H
#endif

#include "../../include/ghal_image.h"
#include "../../include/ghal_service.h"
#include "../../include/ghal.h"
#include "../../include/ghal_2d.h"

/* NEXS headers — resolved via Makefile -I */
#include "nexs_fn.h"
#include "nexs_value.h"
#include "nexs_registry.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── Image handle table ───────────────────────────────────────── */

#define IMG_MAX 64

typedef struct {
    uint32_t     id;
    GHalSurface  surf;
    char         path[256];
    int          in_use;
} ImgEntry;

static ImgEntry  s_imgs[IMG_MAX];
static uint32_t  s_next_id = 1;

static ImgEntry *img_alloc(void) {
    for (int i = 0; i < IMG_MAX; i++)
        if (!s_imgs[i].in_use) { s_imgs[i].in_use = 1; return &s_imgs[i]; }
    return NULL;
}
static ImgEntry *img_find(uint32_t id) {
    for (int i = 0; i < IMG_MAX; i++)
        if (s_imgs[i].in_use && s_imgs[i].id == id) return &s_imgs[i];
    return NULL;
}
static void img_publish(const ImgEntry *e) {
    char p[128];
#define P(sub, v) snprintf(p,sizeof(p),"/dev/img/%u/" sub, e->id); \
                  reg_set(p, v, RK_READ)
    P("path",    VAL_STR(e->path));
    P("width",   VAL_INT(e->surf.width));
    P("height",  VAL_INT(e->surf.height));
    P("surface", VAL_INT((int64_t)(uintptr_t)&e->surf));
#undef P
}

/* ── Builtins ─────────────────────────────────────────────────── */

static Value bi_img_load(Value *args, int n) {
    if (n < 1) return val_err(1, "img_load: need path");
    const char *path = (const char *)args[0].data;
    if (!path) return val_err(1, "img_load: path is nil");

    ImgEntry *e = img_alloc();
    if (!e) return val_err(-1, "img_load: handle table full");

    if (ghal_image_load(path, &e->surf) != 0) {
        e->in_use = 0;
        return val_err(-1, "img_load: decode failed");
    }
    e->id = s_next_id++;
    strncpy(e->path, path, sizeof(e->path) - 1);
    img_publish(e);
    return val_int((int64_t)e->id);
}

static Value bi_img_load_mem(Value *args, int n) {
    if (n < 2) return val_err(1, "img_load_mem: need data len");
    const void *buf = args[0].data;
    int         len = (int)args[1].ival;
    if (!buf || len <= 0) return val_err(1, "img_load_mem: invalid args");

    ImgEntry *e = img_alloc();
    if (!e) return val_err(-1, "img_load_mem: handle table full");

    if (ghal_image_load_mem(buf, len, &e->surf) != 0) {
        e->in_use = 0;
        return val_err(-1, "img_load_mem: decode failed");
    }
    e->id = s_next_id++;
    strncpy(e->path, "<mem>", sizeof(e->path) - 1);
    img_publish(e);
    return val_int((int64_t)e->id);
}

static Value bi_img_save(Value *args, int n) {
    if (n < 2) return val_err(1, "img_save: need id path");
    uint32_t    id   = (uint32_t)args[0].ival;
    const char *path = (const char *)args[1].data;
    ImgEntry   *e    = img_find(id);
    if (!e)   return val_err(-1, "img_save: unknown id");
    if (!path) return val_err(1,  "img_save: nil path");
    int rc = ghal_image_save(path, GHAL_IMG_PNG, &e->surf);
    return val_int(rc);
}

static Value bi_img_save_fmt(Value *args, int n) {
    if (n < 3) return val_err(1, "img_save_fmt: need id path fmt");
    uint32_t       id   = (uint32_t)args[0].ival;
    const char    *path = (const char *)args[1].data;
    GhalImgFormat  fmt  = (GhalImgFormat)args[2].ival;
    ImgEntry      *e    = img_find(id);
    if (!e) return val_err(-1, "img_save_fmt: unknown id");
    return val_int(ghal_image_save(path, fmt, &e->surf));
}

static Value bi_img_scale(Value *args, int n) {
    if (n < 3) return val_err(1, "img_scale: need id w h");
    uint32_t   src_id = (uint32_t)args[0].ival;
    uint32_t   w      = (uint32_t)args[1].ival;
    uint32_t   h      = (uint32_t)args[2].ival;
    ImgEntry  *src    = img_find(src_id);
    if (!src) return val_err(-1, "img_scale: unknown id");

    ImgEntry *dst = img_alloc();
    if (!dst) return val_err(-1, "img_scale: handle table full");

    if (ghal_surface_scale_new(&src->surf, w, h, &dst->surf) != 0) {
        dst->in_use = 0;
        return val_err(-1, "img_scale: resize failed");
    }
    dst->id = s_next_id++;
    snprintf(dst->path, sizeof(dst->path), "%s@%ux%u", src->path, w, h);
    img_publish(dst);
    return val_int((int64_t)dst->id);
}

static Value bi_img_blit(Value *args, int n) {
    if (n < 4) return val_err(1, "img_blit: need win id x y");
    /* win is a GHalWindow id managed by ghal_builtins.c win table —
     * we reach it via the registry /dev/win/<id>/surface */
    uint32_t win_id = (uint32_t)args[0].ival;
    uint32_t img_id = (uint32_t)args[1].ival;
    int32_t  x      = (int32_t)args[2].ival;
    int32_t  y      = (int32_t)args[3].ival;

    ImgEntry *e = img_find(img_id);
    if (!e) return val_err(-1, "img_blit: unknown img id");

    /* Resolve window surface via registry */
    char rp[64];
    snprintf(rp, sizeof(rp), "/dev/win/%u/surface", win_id);
    Value sv = reg_get(rp);
    GHalSurface *dst = (GHalSurface *)(uintptr_t)(uint64_t)sv.ival;
    if (!dst || !dst->pixels) return val_err(-1, "img_blit: invalid window surface");

    draw2d_blit(dst, &e->surf, x, y);
    return val_int(0);
}

static Value bi_img_info(Value *args, int n) {
    if (n < 1) return val_err(1, "img_info: need id");
    uint32_t  id = (uint32_t)args[0].ival;
    ImgEntry *e  = img_find(id);
    if (!e) return val_err(-1, "img_info: unknown id");
    static char buf[128];
    snprintf(buf, sizeof(buf), "w=%u,h=%u,fmt=bgra8,path=%s",
             e->surf.width, e->surf.height, e->path);
    return val_str(buf);
}

static Value bi_img_free(Value *args, int n) {
    if (n < 1) return val_err(1, "img_free: need id");
    uint32_t  id = (uint32_t)args[0].ival;
    ImgEntry *e  = img_find(id);
    if (!e) return val_err(-1, "img_free: unknown id");

    free(e->surf.pixels);
    /* Remove registry entries */
    char p[80];
    snprintf(p, sizeof(p), "/dev/img/%u", id);
    reg_delete(p);
    memset(e, 0, sizeof(*e));
    return val_int(0);
}

/* ── Module descriptor ────────────────────────────────────────── */

static const GhalBuiltinDef s_builtins[] = {
    { "img_load",     "img_load(path str) -> int",                          bi_img_load     },
    { "img_load_mem", "img_load_mem(data str, len int) -> int",             bi_img_load_mem },
    { "img_save",     "img_save(id int, path str) -> int",                  bi_img_save     },
    { "img_save_fmt", "img_save_fmt(id int, path str, fmt int) -> int",     bi_img_save_fmt },
    { "img_scale",    "img_scale(id int, w int, h int) -> int",             bi_img_scale    },
    { "img_blit",     "img_blit(win int, id int, x int, y int) -> int",     bi_img_blit     },
    { "img_info",     "img_info(id int) -> str",                            bi_img_info     },
    { "img_free",     "img_free(id int) -> int",                            bi_img_free     },
};

static void img_mod_init(void) {
    memset(s_imgs, 0, sizeof(s_imgs));
    s_next_id = 1;
}
static void img_mod_shutdown(void) {
    for (int i = 0; i < IMG_MAX; i++) {
        if (s_imgs[i].in_use) free(s_imgs[i].surf.pixels);
    }
    memset(s_imgs, 0, sizeof(s_imgs));
}

static GhalServiceModule s_image_module = {
    .name        = "image",
    .version     = "1.0",
    .reg_prefix  = "/dev/img",
    .init        = img_mod_init,
    .shutdown    = img_mod_shutdown,
    .builtins    = s_builtins,
    .n_builtins  = (int)(sizeof(s_builtins) / sizeof(s_builtins[0])),
};

GHAL_SERVICE_REGISTER(s_image_module)
