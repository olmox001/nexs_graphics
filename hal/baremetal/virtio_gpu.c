/* baremetal/virtio_gpu.c — Minimal VirtIO-GPU v2 driver
 *
 * Targets QEMU virtio-gpu-gl (virgl=on) on amd64/arm64/riscv64.
 * Compiled only for NEXS_BAREMETAL builds.
 *
 * VirtIO-GPU command flow:
 *   GET_DISPLAY_INFO → RESOURCE_CREATE_2D → ATTACH_BACKING
 *   → TRANSFER_TO_HOST_2D → SET_SCANOUT → RESOURCE_FLUSH
 *
 * constructor priority 150 → same slot as host backends.
 */

#ifdef NEXS_BAREMETAL

#include "../include/ghal.h"

#include <stdint.h>
#include <string.h>

/* ── VirtIO-GPU command/response headers (from linux/virtio_gpu.h) ── */
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO        0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D      0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF          0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT             0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH          0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D     0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106
#define VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING 0x0107

#define VIRTIO_GPU_RESP_OK_NODATA              0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO        0x1101
#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM       1

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t _pad;
} VirtGpuCtrlHdr;

typedef struct {
    VirtGpuCtrlHdr hdr;
} VirtGpuGetDisplayInfo;

typedef struct {
    struct {
        struct { uint32_t x,y,w,h; } r;
        uint32_t enabled;
        uint32_t flags;
    } pmodes[16];
} VirtGpuRespDisplayInfo;

typedef struct {
    VirtGpuCtrlHdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} VirtGpuResourceCreate2d;

typedef struct {
    VirtGpuCtrlHdr hdr;
    uint32_t resource_id;
    uint32_t _pad;
    struct { uint64_t addr; uint32_t length; uint32_t _pad; } entries[1];
} VirtGpuResourceAttachBacking;

typedef struct {
    VirtGpuCtrlHdr hdr;
    struct { uint32_t x,y,w,h; } r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t _pad;
} VirtGpuTransferToHost2d;

typedef struct {
    VirtGpuCtrlHdr hdr;
    uint32_t scanout_id;
    uint32_t resource_id;
    struct { uint32_t x,y,w,h; } r;
} VirtGpuSetScanout;

typedef struct {
    VirtGpuCtrlHdr hdr;
    struct { uint32_t x,y,w,h; } r;
    uint32_t resource_id;
    uint32_t _pad;
} VirtGpuResourceFlush;

/* ── MMIO virtqueue stubs (platform-provided) ────────────────── */
extern volatile void *g_virtgpu_mmio_base;  /* set by PCI probe */

static int virtgpu_send_cmd(const void *cmd, size_t cmd_len,
                             void *resp, size_t resp_len) {
    /* In real implementation:
     *   1. Describe cmd in virtqueue descriptor
     *   2. Add resp descriptor
     *   3. Kick queue (write to MMIO notify register)
     *   4. Wait for interrupt / poll used ring
     * This is a placeholder for the platform-specific virtqueue layer.
     */
    (void)cmd; (void)cmd_len; (void)resp; (void)resp_len;
    return 0;
}

/* ── Driver state ───────────────────────────────────────────── */
static uint32_t g_display_w = 1024;
static uint32_t g_display_h = 768;
static uint32_t g_resource_id = 1;

/* Shadow CPU framebuffer (host RAM, mapped to VirtIO backing) */
static uint8_t *g_framebuf = NULL;
static size_t   g_framebuf_sz = 0;

/* ── virtio_gpu_init ─────────────────────────────────────────── */

static int virtio_gpu_init(void) {
    /* 1. Query display info */
    VirtGpuGetDisplayInfo req = {
        .hdr = { .type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO }
    };
    VirtGpuRespDisplayInfo resp = {0};
    virtgpu_send_cmd(&req, sizeof(req), &resp, sizeof(resp));

    if (resp.pmodes[0].enabled) {
        g_display_w = resp.pmodes[0].r.w;
        g_display_h = resp.pmodes[0].r.h;
    }

    /* 2. Allocate shadow framebuffer */
    g_framebuf_sz = (size_t)g_display_w * g_display_h * 4;
    /* On baremetal: use static or boot-time allocated region */
    /* Simplified: symbol resolved by linker script */
    extern uint8_t _ghal_framebuf[];
    g_framebuf = _ghal_framebuf;
    if (!g_framebuf) return -1;

    /* 3. Create GPU resource */
    VirtGpuResourceCreate2d create_req = {
        .hdr         = { .type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D },
        .resource_id = g_resource_id,
        .format      = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM,
        .width       = g_display_w,
        .height      = g_display_h,
    };
    virtgpu_send_cmd(&create_req, sizeof(create_req), NULL, 0);

    /* 4. Attach host-side pages as backing */
    VirtGpuResourceAttachBacking attach_req = {
        .hdr          = { .type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING },
        .resource_id  = g_resource_id,
        .entries[0]   = {
            .addr   = (uint64_t)(uintptr_t)g_framebuf,
            .length = (uint32_t)g_framebuf_sz,
        },
    };
    virtgpu_send_cmd(&attach_req, sizeof(attach_req), NULL, 0);

    /* 5. Set scanout */
    VirtGpuSetScanout scanout_req = {
        .hdr         = { .type = VIRTIO_GPU_CMD_SET_SCANOUT },
        .scanout_id  = 0,
        .resource_id = g_resource_id,
        .r           = { 0, 0, g_display_w, g_display_h },
    };
    virtgpu_send_cmd(&scanout_req, sizeof(scanout_req), NULL, 0);

    return 0;
}

static void virtio_gpu_shutdown(void) {}

static void virtio_gpu_get_display_size(uint32_t *w, uint32_t *h) {
    if (w) *w = g_display_w;
    if (h) *h = g_display_h;
}

static uint32_t virtio_gpu_preferred_format(void) {
    return GHAL_FMT_BGRA8;
}

/* ── Surface operations ──────────────────────────────────────── */

static int virtio_gpu_surface_create(GHalSurface *s, uint32_t w,
                                      uint32_t h, uint32_t fmt) {
    (void)fmt;
    /* Point surface at the shadow framebuffer region */
    s->stride     = w * 4;
    s->pixel_size = (size_t)s->stride * h;
    s->pixels     = g_framebuf;  /* single fullscreen surface for now */
    s->gpu_dirty  = 0;
    return 0;
}

static void virtio_gpu_surface_destroy(GHalSurface *s) {
    s->pixels = NULL;
}

static int virtio_gpu_surface_lock(GHalSurface *s) {
    (void)s;
    return 0;
}

static void virtio_gpu_surface_unlock(GHalSurface *s) {
    s->gpu_dirty = 1;
}

static void virtio_gpu_surface_present(GHalWindow *w, GHalSurface *s) {
    (void)w;
    /* Transfer shadow buffer to GPU */
    VirtGpuTransferToHost2d xfer = {
        .hdr         = { .type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D },
        .r           = { 0, 0, s->width, s->height },
        .offset      = 0,
        .resource_id = g_resource_id,
    };
    virtgpu_send_cmd(&xfer, sizeof(xfer), NULL, 0);

    /* Flush to display */
    VirtGpuResourceFlush flush = {
        .hdr         = { .type = VIRTIO_GPU_CMD_RESOURCE_FLUSH },
        .r           = { 0, 0, s->width, s->height },
        .resource_id = g_resource_id,
    };
    virtgpu_send_cmd(&flush, sizeof(flush), NULL, 0);
    s->gpu_dirty = 0;
}

/* ── VSYNC — timer-based ─────────────────────────────────────── */

static uint32_t virtio_gpu_vsync(void) {
    /* Wait for platform timer interrupt (16.67 ms for 60 Hz)
     * On amd64: HPET or LAPIC timer; on arm64: arch timer.
     * Implemented via nexs_hal timer API. */
    extern void nexs_hal_sleep_us(uint32_t us);
    nexs_hal_sleep_us(16667);
    return 16667;
}

/* ── Input polling (PS/2 or VirtIO input device) ────────────── */

static int virtio_gpu_poll_input(GHalInputEvent *event) {
    (void)event;
    /* Phase 6: PS/2 keyboard via IO port 0x60 (amd64) */
    return 0;
}

/* ── Driver registration ─────────────────────────────────────── */

static GHalDriver s_virtio_driver = {
    .name             = "virtio-gpu",
    .init             = virtio_gpu_init,
    .shutdown         = virtio_gpu_shutdown,
    .get_display_size = virtio_gpu_get_display_size,
    .preferred_format = virtio_gpu_preferred_format,
    .win_open         = NULL,   /* no windowing on baremetal */
    .win_close        = NULL,
    .win_set_title    = NULL,
    .win_resize       = NULL,
    .surface_create   = virtio_gpu_surface_create,
    .surface_destroy  = virtio_gpu_surface_destroy,
    .surface_lock     = virtio_gpu_surface_lock,
    .surface_unlock   = virtio_gpu_surface_unlock,
    .surface_present  = virtio_gpu_surface_present,
    .gpu_begin_frame  = NULL,
    .gpu_submit       = NULL,
    .gpu_end_frame    = NULL,
    .vsync            = virtio_gpu_vsync,
    .vsync_hz         = 60,
    .poll_input       = virtio_gpu_poll_input,
};

__attribute__((constructor(150)))
static void register_virtio_ghal(void) {
    g_ghal_driver = &s_virtio_driver;
}

#endif /* NEXS_BAREMETAL */
