/* sel4/surface_pd.c — Surface Memory Allocator Protected Domain
 *
 * Allocates page-mapped shared memory regions (MR) for framebuffers.
 * Apps request an MR; surface_pd grants pages; compositor maps them.
 *
 * This enables zero-copy display updates: apps write to their MR,
 * compositor reads from the same physical pages.
 */

#ifdef NEXS_SEL4

#include <microkit.h>
#include <stdint.h>
#include <string.h>

/* Max concurrent surfaces */
#define SURF_PD_MAX 32

/* Channels */
#define CH_ALLOC_REQ    5   /* app requests surface allocation */
#define CH_ALLOC_RESP   6   /* surface_pd responds with cap */
#define CH_COMP_GRANT   7   /* grant to compositor PD */

typedef struct {
    uint32_t surf_id;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    void    *mr_base;   /* mapped physical address */
    size_t   mr_size;
    int      in_use;
} SurfRecord;

static SurfRecord s_surfs[SURF_PD_MAX];
static uint32_t   s_next_id = 1;

/* Physical page pool (provided by system description) */
extern void *_ghal_surface_pool_base;
extern size_t _ghal_surface_pool_size;
static uint8_t *s_pool;
static size_t   s_pool_used;

/* ── Allocate pages from pool ────────────────────────────────── */

static void *pool_alloc(size_t sz) {
    sz = (sz + 4095) & ~(size_t)4095;  /* page-align */
    if (s_pool_used + sz > _ghal_surface_pool_size) return NULL;
    void *p = s_pool + s_pool_used;
    s_pool_used += sz;
    return p;
}

/* ── Surface allocation handler ──────────────────────────────── */

static void handle_alloc_request(void) {
    /* Read request from message registers */
    uint32_t width  = (uint32_t)microkit_mr_get(0);
    uint32_t height = (uint32_t)microkit_mr_get(1);
    uint32_t format = (uint32_t)microkit_mr_get(2);

    size_t sz = (size_t)width * height * 4;  /* BGRA8 */
    void  *mr = pool_alloc(sz);

    if (!mr) {
        microkit_mr_set(0, (uint32_t)-1);  /* error */
        microkit_reply(microkit_msginfo_new(0, 1));
        return;
    }

    /* Find free record */
    int slot = -1;
    for (int i = 0; i < SURF_PD_MAX; i++) {
        if (!s_surfs[i].in_use) { slot = i; break; }
    }
    if (slot < 0) {
        microkit_mr_set(0, (uint32_t)-1);
        microkit_reply(microkit_msginfo_new(0, 1));
        return;
    }

    s_surfs[slot].surf_id = s_next_id++;
    s_surfs[slot].width   = width;
    s_surfs[slot].height  = height;
    s_surfs[slot].format  = format;
    s_surfs[slot].mr_base = mr;
    s_surfs[slot].mr_size = sz;
    s_surfs[slot].in_use  = 1;

    /* Grant compositor PD access (seL4: derive cap from MR, grant) */
    /* Simplified: just pass the address back */
    microkit_mr_set(0, s_surfs[slot].surf_id);
    microkit_mr_set(1, (uint32_t)(uintptr_t)mr & 0xFFFFFFFFu);
    microkit_mr_set(2, (uint32_t)sz);
    microkit_reply(microkit_msginfo_new(0, 3));

    /* Notify compositor of new surface */
    microkit_notify(CH_COMP_GRANT);
}

/* ── PD entry points ──────────────────────────────────────────── */

void notified(microkit_channel ch) {
    (void)ch;
}

void protected(microkit_channel ch, microkit_msginfo msginfo) {
    (void)msginfo;
    if (ch == CH_ALLOC_REQ) {
        handle_alloc_request();
    }
}

void init(void) {
    s_pool      = (uint8_t *)&_ghal_surface_pool_base;
    s_pool_used = 0;
    memset(s_surfs, 0, sizeof(s_surfs));
}

#endif /* NEXS_SEL4 */
