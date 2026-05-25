/* reimplementation/hal/hal_hosted.c
 * Override of base-nexs/hal/hal_hosted.c.
 * Adds GHAL graphics bootstrap on top of the standard hosted HAL stubs.
 * The Makefile excludes base-nexs/hal/hal_hosted.c and compiles this instead.
 */

#ifndef NEXS_BAREMETAL

#include "nexs_hal.h"
#include <stdio.h>
#include <stdlib.h>

#include "hal_internal.h"
#include "nexs_mmu.h"
#include "nexs_registry.h"
#include "nexs_value.h"
#include "nexs_alloc.h"

/* ── Standard hosted HAL stubs (identical to base-nexs version) ── */

static void hosted_hal_init(void) {}

static void hosted_hal_putc(char c) {
    fputc((unsigned char)c, stdout);
}

static int hosted_hal_getc(void) {
    int c = fgetc(stdin);
    return (c == EOF) ? -1 : c;
}

static void hosted_hal_memory_map(NexsMemMap *map) {
    if (map) {
        map->entry_point = 0;
        map->ram_base    = 0;
        map->ram_size    = 0;
        map->uart_base   = 0;
    }
}

static void hosted_hal_irq_disable(void) {}
static void hosted_hal_irq_enable(void)  {}

static void hosted_hal_halt(void) __attribute__((noreturn));
static void hosted_hal_halt(void) { _Exit(0); }

void mmu_worker_sync(void) {}

int mm_alloc_page(uint32_t pid, vaddr_t virt, uint32_t flags) {
    (void)pid; (void)virt; (void)flags;
    void *p = page_alloc(1);
    return p ? 0 : -1;
}

int mm_free_page(uint32_t pid, vaddr_t virt) {
    (void)pid;
    page_free((void *)virt, 1);
    return 0;
}

int mm_map_range(uint32_t pid, vaddr_t virt, paddr_t phys,
                 uint32_t pages, uint32_t flags) {
    (void)phys;
    for (uint32_t i = 0; i < pages; i++) {
        if (mm_alloc_page(pid, virt + i * 4096, flags) != 0) return -1;
    }
    return 0;
}

paddr_t mmu_virt_to_phys(vaddr_t virt) { return (paddr_t)virt; }
void    mmu_init(void) {}
int     mmu_switch_address_space(uint32_t pid) { (void)pid; return 0; }
void    mmu_destroy_address_space(uint32_t pid) { (void)pid; }
paddr_t mmu_create_address_space(uint32_t pid) { (void)pid; return 0; }
void    mmu_flush_tlb(vaddr_t virt) { (void)virt; }
void    mmu_page_fault(vaddr_t fault_addr, uint64_t err) {
    (void)fault_addr; (void)err;
}

static HalDriver s_hosted_driver = {
    .name        = "hosted-stdio",
    .init        = hosted_hal_init,
    .putc        = hosted_hal_putc,
    .getc        = hosted_hal_getc,
    .halt        = hosted_hal_halt,
    .irq_disable = hosted_hal_irq_disable,
    .irq_enable  = hosted_hal_irq_enable,
    .memory_map  = hosted_hal_memory_map,
};

__attribute__((constructor(101)))
static void hosted_register_hal(void) {
    g_hal_driver = &s_hosted_driver;
}

/* ── GHAL bootstrap (constructor runs AFTER HAL registration) ── */
extern int ghal_init(void);

__attribute__((constructor(200)))
static void ghal_hosted_bootstrap(void) {
    /* Auto-initialize the graphics subsystem when the runtime loads.
     * g_ghal_driver is set by the platform backend constructor (priority 150). */
    if (ghal_init() != 0) {
        fprintf(stderr, "GHAL: init failed on hosted platform\n");
    }
}

#endif /* !NEXS_BAREMETAL */
