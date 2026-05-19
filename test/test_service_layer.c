/* test/test_service_layer.c — Unit tests for GhalServiceModule system
 *
 * Uses real base-nexs header types (Value, RegKey, …) for ABI compatibility
 * with the _nexs.c service files.  Link-time stubs replace the nexs runtime
 * so no real registry/allocator is needed.
 *
 * Compile with:
 *   cc ... -I base-nexs/core/include -I base-nexs/lang/include \
 *          -I base-nexs/registry/include -I include -I vendor/include
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ── Real nexs headers (types only — we provide all implementations) ── */
#define NEXS_API            /* strip NEXS_API so decls compile without issues */
#define NEXS_HAL_H          /* skip nexs_hal.h */
#define NEXS_ALLOC_H        /* skip nexs_alloc.h (we don't need buddy alloc) */

/* Provide dummy globals that nexs_alloc.h would normally define */
typedef struct {} BuddyAllocator;

#include "nexs_value.h"     /* base-nexs/core/include — resolved via -I */
#include "nexs_fn.h"        /* base-nexs/lang/include */
#include "nexs_registry.h"  /* base-nexs/registry/include */

/* ── nexs runtime stubs (link-time) ─────────────────────────────── */

/* value stubs */
Value val_nil(void)         { Value v={0}; v.type=TYPE_NIL; return v; }
Value val_bool(int b)       { Value v={0}; v.type=TYPE_BOOL; v.ival=b; return v; }
Value val_int(int64_t n)    { Value v={0}; v.type=TYPE_INT;  v.ival=n; return v; }
Value val_float(double f)   { Value v={0}; v.type=TYPE_FLOAT;v.fval=f; return v; }
Value val_str(const char *s){ Value v={0}; v.type=TYPE_STR;  v.data=(void*)s; return v; }
Value val_err(int c, const char *m) {
    Value v={0}; v.type=TYPE_ERR; v.err_code=c; v.err_msg=(char*)m; return v;
}
void val_to_str(const Value *v, char *buf, size_t sz) {
    if (!v||!buf||sz==0) return;
    if (v->type==TYPE_STR && v->data) strncpy(buf,(char*)v->data,sz-1);
    else if (v->type==TYPE_INT) snprintf(buf,sz,"%lld",(long long)v->ival);
    else buf[0]='\0';
}
Value val_copy(const Value *v) { return v ? *v : val_nil(); }
void  val_free(Value *v)       { (void)v; }

/* fn table stubs */
typedef struct { const char *name; BuiltinFn fn; const char *sig; } FnEntry2;
static FnEntry2 s_fns[256];
static int      s_fn_count = 0;

int fn_register_builtin_sig(const char *name, BuiltinFn fn, const char *sig) {
    if (s_fn_count < 256) {
        s_fns[s_fn_count++] = (FnEntry2){name, fn, sig};
    }
    return 0;
}
int fn_register_builtin(const char *name, BuiltinFn fn) {
    return fn_register_builtin_sig(name, fn, "");
}

/* registry stubs */
Registry g_registry = {0};
DynArray *g_arrays[256] = {0};
size_t g_array_count = 0;

typedef struct RegEntry { char path[128]; Value val; } RegEntry;
static RegEntry s_reg[512];
static int      s_reg_count = 0;

void    reg_init(void) {}
RegKey *reg_mkpath(const char *path, uint8_t rights) { (void)path;(void)rights; return NULL; }
RegKey *reg_lookup(const char *path) { (void)path; return NULL; }
RegKey *reg_resolve(const char *n, const char *s) { (void)n;(void)s; return NULL; }
int     reg_set(const char *path, Value val, uint8_t rights) {
    (void)rights;
    for (int i=0;i<s_reg_count;i++)
        if (!strcmp(s_reg[i].path,path)){s_reg[i].val=val;return 0;}
    if (s_reg_count<512){strncpy(s_reg[s_reg_count].path,path,127);s_reg[s_reg_count++].val=val;}
    return 0;
}
Value   reg_get(const char *path) {
    for (int i=0;i<s_reg_count;i++)
        if (!strcmp(s_reg[i].path,path)) return s_reg[i].val;
    return val_nil();
}
int     reg_delete(const char *path) {
    size_t plen = strlen(path);
    for (int i=0;i<s_reg_count;) {
        if (!strncmp(s_reg[i].path,path,plen))
            s_reg[i]=s_reg[--s_reg_count];
        else i++;
    }
    return 0;
}
void reg_ls(const char *p,FILE *o){(void)p;(void)o;}
void reg_ls_recursive(const char *p,FILE *o,int d){(void)p;(void)o;(void)d;}
int  reg_set_ptr(const char *p,const char *t){(void)p;(void)t;return 0;}
Value reg_get_deref(const char *p){(void)p;return val_nil();}
int  reg_ipc_send(const char *p,Value m){(void)p;(void)m;return 0;}
int  reg_ipc_recv(const char *p,Value *m){(void)p;(void)m;return 0;}

/* draw2d stubs */
#include "../include/ghal.h"
void draw2d_blit(GHalSurface *d,const GHalSurface *s,int32_t x,int32_t y){
    (void)d;(void)s;(void)x;(void)y;
}
void draw2d_fill_rect(GHalSurface *s,int32_t x,int32_t y,int32_t w,int32_t h,uint32_t c){
    (void)s;(void)x;(void)y;(void)w;(void)h;(void)c;
}

/* ghal stubs */
GHalDriver *g_ghal_driver = NULL;

/* ── After stubs: include service header ────────────────────────── */
#include "../include/ghal_service.h"

/* ── Test helpers ─────────────────────────────────────────────── */
static int g_pass = 0, g_fail = 0;
#define EXPECT(cond, name) do { \
    if (cond) { printf("  PASS: %s\n", name); g_pass++; } \
    else       { printf("  FAIL: %s\n", name); g_fail++; } \
} while(0)

static BuiltinFn find_builtin(const char *name) {
    for (int i=0;i<s_fn_count;i++)
        if (!strcmp(s_fns[i].name,name)) return s_fns[i].fn;
    return NULL;
}

/* ── Tests ───────────────────────────────────────────────────── */

static void test_service_registry(void) {
    printf("test_service_registry:\n");
    ghal_service_init_all();

    GhalServiceModule *img  = ghal_service_find("image");
    GhalServiceModule *font = ghal_service_find("font");
    EXPECT(img  != NULL, "image module registered");
    EXPECT(font != NULL, "font module registered");
    if (img)  EXPECT(img->n_builtins  == 8, "image has 8 builtins");
    if (font) EXPECT(font->n_builtins == 7, "font has 7 builtins");

    EXPECT(find_builtin("img_load")     != NULL, "img_load in fn table");
    EXPECT(find_builtin("img_blit")     != NULL, "img_blit in fn table");
    EXPECT(find_builtin("img_free")     != NULL, "img_free in fn table");
    EXPECT(find_builtin("font_load")    != NULL, "font_load in fn table");
    EXPECT(find_builtin("font_draw")    != NULL, "font_draw in fn table");
    EXPECT(find_builtin("font_measure") != NULL, "font_measure in fn table");

    Value v = reg_get("/dev/img/__version__");
    EXPECT(v.type == TYPE_STR, "/dev/img/__version__ published");
    v = reg_get("/dev/font/__version__");
    EXPECT(v.type == TYPE_STR, "/dev/font/__version__ published");
}

static void test_img_null_safety(void) {
    printf("test_img_null_safety:\n");
    BuiltinFn img_load = find_builtin("img_load");
    if (!img_load) { printf("  SKIP: img_load not found\n"); return; }

    Value r = img_load(NULL, 0);
    EXPECT(r.type == TYPE_ERR, "img_load(nil,0) → error");

    Value args[1] = { val_str("/nonexistent/x.png") };
    r = img_load(args, 1);
    EXPECT(r.type == TYPE_ERR, "img_load(bad path) → error");

    Value np = val_nil();
    r = img_load(&np, 1);
    EXPECT(r.type == TYPE_ERR, "img_load(nil str) → error");
}

static void test_font_null_safety(void) {
    printf("test_font_null_safety:\n");
    BuiltinFn font_load = find_builtin("font_load");
    if (!font_load) { printf("  SKIP: font_load not found\n"); return; }

    Value args[2] = { val_str("/nonexistent/font.ttf"), {0} };
    args[1].type = TYPE_FLOAT; args[1].fval = 16.0;
    Value r = font_load(args, 2);
    EXPECT(r.type == TYPE_ERR, "font_load(bad path) → error");

    Value args2[2] = { val_str("/nonexistent.ttf"), {0} };
    args2[1].type = TYPE_FLOAT; args2[1].fval = 0.0;
    r = font_load(args2, 2);
    EXPECT(r.type == TYPE_ERR, "font_load(size=0) → error");
}

static void test_img_load_from_memory(void) {
    printf("test_img_load_from_memory:\n");
    static const uint8_t red_png[] = {
        0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,
        0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
        0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,
        0x08,0x02,0x00,0x00,0x00,0x90,0x77,0x53,
        0xDE,0x00,0x00,0x00,0x0C,0x49,0x44,0x41,
        0x54,0x08,0xD7,0x63,0xF8,0xCF,0xC0,0x00,
        0x00,0x00,0x02,0x00,0x01,0xE2,0x21,0xBC,
        0x33,0x00,0x00,0x00,0x00,0x49,0x45,0x4E,
        0x44,0xAE,0x42,0x60,0x82
    };
    BuiltinFn img_load_mem = find_builtin("img_load_mem");
    BuiltinFn img_info     = find_builtin("img_info");
    BuiltinFn img_free     = find_builtin("img_free");
    if (!img_load_mem) { printf("  SKIP: img_load_mem not found\n"); return; }

    Value args[2];
    args[0]      = val_str(NULL);
    args[0].data = (void *)red_png;
    args[1]      = val_int((int64_t)sizeof(red_png));

    Value r = img_load_mem(args, 2);
    EXPECT(r.type == TYPE_INT, "img_load_mem → int id");
    EXPECT(r.ival > 0,         "id > 0");

    if (r.type == TYPE_INT && r.ival > 0) {
        char p[64];
        snprintf(p, sizeof(p), "/dev/img/%lld/width", (long long)r.ival);
        Value wv = reg_get(p);
        EXPECT(wv.type == TYPE_INT && wv.ival == 1, "/dev/img/<id>/width == 1");

        if (img_info) {
            Value info = img_info(&r, 1);
            EXPECT(info.type == TYPE_STR, "img_info returns str");
            if (info.data)
                EXPECT(strstr((char*)info.data,"w=1") != NULL, "info has w=1");
        }
        if (img_free) {
            Value fr = img_free(&r, 1);
            EXPECT(fr.type == TYPE_INT && fr.ival == 0, "img_free ok");
        }
    }
}

static void test_module_iteration(void) {
    printf("test_module_iteration:\n");
    int n = 0;
    for (GhalServiceModule *m = ghal_service_head(); m; m = m->_next) {
        n++;
        EXPECT(m->name    != NULL, "module has name");
        EXPECT(m->version != NULL, "module has version");
        EXPECT(m->n_builtins > 0,  "module has builtins");
    }
    EXPECT(n >= 2, "at least 2 modules registered");
}

/* ── main ────────────────────────────────────────────────────── */

int main(void) {
    printf("=== GHAL service layer tests ===\n");
    test_service_registry();
    test_img_null_safety();
    test_font_null_safety();
    test_img_load_from_memory();
    test_module_iteration();
    printf("\nResult: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail > 0) ? 1 : 0;
}
