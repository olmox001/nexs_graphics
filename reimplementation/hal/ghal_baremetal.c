/* reimplementation/hal/ghal_baremetal.c — GHAL baremetal integration
 *
 * This file is the GHAL overlay for NEXS_BAREMETAL builds.
 * It registers GHAL builtins (graphics when available) and provides
 * the glue between nexs_main_baremetal() and the GHAL graphics stack.
 *
 * In the minimal test build (no compositor/GPU), ghal_register_builtins
 * is a no-op.  Full graphics support (VirtIO-GPU) is enabled by defining
 * GHAL_BAREMETAL_GRAPHICS and linking baremetal/virtio_gpu.c etc.
 */

#ifdef NEXS_BAREMETAL

/* ── Embedded script table stub (provided by codegen in standalone builds) ─ */
typedef struct { const char *path; const char *src; } NexsEmbedDep;
const NexsEmbedDep nexs_embed_deps_table[] = { { NULL, NULL } };

/* ── Embedded test script — overrides the weak symbol in main.c ─ */
const char nexs_script_src[] =
    "out \"=== NEXS baremetal boot OK ===\"\n"
    "x = 21\n"
    "y = x * 2\n"
    "out \"21 * 2 = \" + str(y)\n"
    "fn fact(n) {\n"
    "    if n <= 1 { ret 1 }\n"
    "    ret n * fact(n - 1)\n"
    "}\n"
    "out \"5! = \" + str(fact(5))\n"
    "out \"=== NEXS interpreter: ALL OK ===\"\n";

/* ── Missing libc extensions not in base-nexs/kernel/libc_stub.c ─ */

int isxdigit(int c) {
    return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');
}

int isprint(int c) { return c >= 0x20 && c < 0x7F; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }

long long strtoll(const char *s, char **endptr, int base) {
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    if (base == 0) {
        if (s[0]=='0'&&(s[1]=='x'||s[1]=='X')) { base=16; s+=2; }
        else if (*s=='0') { base=8; s++; }
        else base=10;
    } else if (base==16&&s[0]=='0'&&(s[1]=='x'||s[1]=='X')) s+=2;
    long long val = 0;
    while (*s) {
        int d;
        if (*s>='0'&&*s<='9') d=*s-'0';
        else if (*s>='a'&&*s<='f') d=*s-'a'+10;
        else if (*s>='A'&&*s<='F') d=*s-'A'+10;
        else break;
        if (d >= base) break;
        val = val*base + d;
        s++;
    }
    if (endptr) *endptr = (char *)s;
    return neg ? -val : val;
}

/* ── Full graphics path ─────────────────────────────────────── */
#ifdef GHAL_BAREMETAL_GRAPHICS

#include "../../include/ghal.h"

extern void ghal_service_init_all(void);
extern int fn_register_builtin_sig(const char *sig,
                                   void *fn, int n_params);

/* Include full GHAL builtin registration when graphics is available */
extern void _ghal_register_draw_builtins(void);

void ghal_register_builtins(void) {
    ghal_service_init_all();
    _ghal_register_draw_builtins();
}

#else /* GHAL_BAREMETAL_GRAPHICS not defined */

/* ── Minimal test path: no graphics builtins ─────────────────── */

void ghal_register_builtins(void) {
    /* Stub: graphics builtins deferred until GPU driver is confirmed.
     * The NEXS interpreter still runs — out/str/math/registry all work.
     * Enable GHAL_BAREMETAL_GRAPHICS once VirtIO-GPU init is verified. */
}

#endif /* GHAL_BAREMETAL_GRAPHICS */

#endif /* NEXS_BAREMETAL */
