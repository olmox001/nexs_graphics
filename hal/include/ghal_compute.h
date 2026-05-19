/* include/ghal_compute.h — OpenCL-style compute dispatch
 *
 * Uniform across Host (Metal Compute / OpenCL), Baremetal (VirGL compute),
 * and seL4 (future: separate compute PD).
 * Phase 8 feature — stubs in Phase 5.
 */

#ifndef GHAL_COMPUTE_H
#define GHAL_COMPUTE_H
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t GhalKernel;
typedef uint32_t GhalComputeBuf;

/* Compile a compute kernel from C source string.
 * Returns 0 on error. */
GhalKernel ghal_kernel_create(const char *src, const char *entry);
void       ghal_kernel_destroy(GhalKernel k);

/* Buffers shared between CPU and GPU */
GhalComputeBuf ghal_compute_buf_create(size_t bytes, const void *init_data);
void           ghal_compute_buf_destroy(GhalComputeBuf buf);
void          *ghal_compute_buf_map(GhalComputeBuf buf);    /* CPU access */
void           ghal_compute_buf_unmap(GhalComputeBuf buf);

/* Bind buffer to kernel argument slot */
void ghal_kernel_set_buf(GhalKernel k, uint32_t slot, GhalComputeBuf buf);
void ghal_kernel_set_u32(GhalKernel k, uint32_t slot, uint32_t val);

/* Dispatch: launches gx*gy*gz work-groups */
int ghal_kernel_dispatch(GhalKernel k,
                         uint32_t gx, uint32_t gy, uint32_t gz);

/* Synchronize (wait for all GPU work to finish) */
void ghal_compute_sync(void);

#ifdef __cplusplus
}
#endif

#endif /* GHAL_COMPUTE_H */
