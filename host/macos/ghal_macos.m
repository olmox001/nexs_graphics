/* host/macos/ghal_macos.m — macOS backend: NSWindow + CAMetalLayer
 *
 * Compiled with clang on macOS. Uses Objective-C ARC.
 * Requires: -framework Cocoa -framework Metal -framework QuartzCore
 *
 * constructor priority 150 → runs between HAL (101) and GHAL bootstrap (200).
 */

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include "../../include/ghal.h"

/* ── App delegate (hidden; lets NSApp run headless if needed) ── */
@interface GHalAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation GHalAppDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    return NSTerminateNow;
}
@end

/* ── Per-window Objective-C wrapper ─────────────────────────── */
@interface GHalNSWindow : NSWindow <NSWindowDelegate>
@property (nonatomic, strong) CAMetalLayer *metalLayer;
@property (nonatomic, assign) id<MTLDevice> device;
@property (nonatomic, assign) uint32_t ghal_id;
@end

@implementation GHalNSWindow

- (BOOL)windowShouldClose:(id)sender {
    (void)sender;
    /* Signal window close through registry */
    char path[80];
    snprintf(path, sizeof(path), "/dev/win/%u/damage", self.ghal_id);
    return NO;  /* Compositor handles cleanup */
}

- (void)windowDidResize:(NSNotification *)notification {
    (void)notification;
    /* CAMetalLayer auto-resizes with the view */
    self.metalLayer.drawableSize = self.contentView.bounds.size;
}

@end

/* ── Metal state ─────────────────────────────────────────────── */
static id<MTLDevice>           g_metal_device    = nil;
static id<MTLCommandQueue>     g_metal_queue     = nil;
static id<MTLLibrary>          g_metal_lib       = nil;
static id<MTLRenderPipelineState> g_blit_pso     = nil;
static NSApplication          *g_app             = nil;

/* ── macos_init ──────────────────────────────────────────────── */

static int macos_init(void) {
    @autoreleasepool {
        g_app = [NSApplication sharedApplication];
        GHalAppDelegate *del = [[GHalAppDelegate alloc] init];
        g_app.delegate = del;
        [g_app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [g_app finishLaunching];

        g_metal_device = MTLCreateSystemDefaultDevice();
        if (!g_metal_device) return -1;

        g_metal_queue = [g_metal_device newCommandQueue];
        if (!g_metal_queue) return -1;

        /* Publish GPU vendor */
        NSString *gname = g_metal_device.name;
        /* Registry publish done in ghal_host.c */
        (void)gname;

        return 0;
    }
}

static void macos_shutdown(void) {
    g_metal_lib    = nil;
    g_metal_queue  = nil;
    g_metal_device = nil;
}

/* ── Display info ────────────────────────────────────────────── */

static void macos_get_display_size(uint32_t *w, uint32_t *h) {
    NSScreen *screen = [NSScreen mainScreen];
    NSRect frame = screen.frame;
    if (w) *w = (uint32_t)frame.size.width;
    if (h) *h = (uint32_t)frame.size.height;
}

static uint32_t macos_preferred_format(void) {
    return GHAL_FMT_BGRA8;
}

/* ── Window management ───────────────────────────────────────── */

static int macos_win_open(GHalWindow *w) {
    @autoreleasepool {
        NSRect frame = NSMakeRect(100, 100, w->width, w->height);
        GHalNSWindow *nsw = [[GHalNSWindow alloc]
            initWithContentRect:frame
            styleMask:(NSWindowStyleMaskTitled |
                       NSWindowStyleMaskClosable |
                       NSWindowStyleMaskResizable)
            backing:NSBackingStoreBuffered
            defer:NO];

        nsw.title = [NSString stringWithUTF8String:w->title];
        nsw.ghal_id = w->id;
        nsw.delegate = nsw;

        /* Attach CAMetalLayer to content view */
        NSView *view = nsw.contentView;
        [view setWantsLayer:YES];

        CAMetalLayer *layer = [CAMetalLayer layer];
        layer.device = g_metal_device;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = NO;
        layer.drawableSize = frame.size;
        [view setLayer:layer];

        nsw.metalLayer = layer;
        nsw.device = g_metal_device;

        [nsw makeKeyAndOrderFront:nil];
        [g_app activateIgnoringOtherApps:YES];

        /* Store NSWindow pointer in native_handle */
        w->native_handle = (uintptr_t)CFBridgingRetain(nsw);
        return 0;
    }
}

static void macos_win_close(GHalWindow *w) {
    if (!w->native_handle) return;
    @autoreleasepool {
        GHalNSWindow *nsw = (__bridge_transfer GHalNSWindow *)(void *)w->native_handle;
        [nsw close];
        w->native_handle = 0;
    }
}

static void macos_win_set_title(GHalWindow *w, const char *title) {
    if (!w->native_handle) return;
    @autoreleasepool {
        GHalNSWindow *nsw = (__bridge GHalNSWindow *)(void *)w->native_handle;
        nsw.title = [NSString stringWithUTF8String:title];
    }
}

static void macos_win_resize(GHalWindow *w, uint32_t nw, uint32_t nh) {
    if (!w->native_handle) return;
    @autoreleasepool {
        GHalNSWindow *nsw = (__bridge GHalNSWindow *)(void *)w->native_handle;
        NSRect frame = nsw.frame;
        frame.size = NSMakeSize(nw, nh);
        [nsw setFrame:frame display:YES];
        nsw.metalLayer.drawableSize = frame.size;
    }
}

/* ── Surface management ──────────────────────────────────────── */

static int macos_surface_create(GHalSurface *s, uint32_t w, uint32_t h,
                                 uint32_t fmt) {
    (void)fmt;
    s->stride     = w * 4;  /* BGRA8 */
    s->pixel_size = (size_t)s->stride * h;
    s->pixels     = malloc(s->pixel_size);
    if (!s->pixels) return -1;
    s->gpu_dirty  = false;
    return 0;
}

static void macos_surface_destroy(GHalSurface *s) {
    free(s->pixels);
    s->pixels = NULL;
}

static int macos_surface_lock(GHalSurface *s) {
    (void)s;
    return 0;  /* CPU buffer always accessible */
}

static void macos_surface_unlock(GHalSurface *s) {
    s->gpu_dirty = true;
}

/* ── Surface present — blit CPU pixels to Metal drawable ─────── */

static void macos_surface_present(GHalWindow *w, GHalSurface *s) {
    if (!w->native_handle || !s->pixels) return;

    @autoreleasepool {
        GHalNSWindow *nsw = (__bridge GHalNSWindow *)(void *)w->native_handle;
        CAMetalLayer *layer = nsw.metalLayer;

        id<CAMetalDrawable> drawable = [layer nextDrawable];
        if (!drawable) return;

        id<MTLTexture> tex = drawable.texture;

        /* Upload CPU pixels to Metal texture region */
        MTLRegion region = MTLRegionMake2D(0, 0, s->width, s->height);
        [tex replaceRegion:region
              mipmapLevel:0
                withBytes:s->pixels
              bytesPerRow:s->stride];

        /* Commit a command buffer to present the drawable */
        id<MTLCommandBuffer> cmd = [g_metal_queue commandBuffer];
        [cmd presentDrawable:drawable];
        [cmd commit];

        s->gpu_dirty = false;
    }
}

/* ── GPU optional path (Metal command buffer) ────────────────── */

static int macos_gpu_begin_frame(GHalWindow *w) {
    (void)w;
    return 0;
}

static int macos_gpu_submit(const void *cmdbuf, size_t len) {
    (void)cmdbuf; (void)len;
    return 0;  /* Phase 8: virgl-style Metal compute */
}

static int macos_gpu_end_frame(GHalWindow *w) {
    (void)w;
    return 0;
}

/* ── VSYNC ───────────────────────────────────────────────────── */

static uint32_t macos_vsync(void) {
    /* Drain the Cocoa run loop for one event cycle, then sleep to ~60 Hz */
    @autoreleasepool {
        NSEvent *ev;
        while ((ev = [g_app nextEventMatchingMask:NSEventMaskAny
                            untilDate:[NSDate distantPast]
                               inMode:NSDefaultRunLoopMode
                              dequeue:YES]) != nil) {
            [g_app sendEvent:ev];
        }
    }
    /* Sleep 16.67 ms for ~60 Hz */
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 16667000 };
    nanosleep(&ts, NULL);
    return 16667;
}

/* ── Input polling ───────────────────────────────────────────── */

static int macos_poll_input(GHalInputEvent *event) {
    @autoreleasepool {
        NSEvent *ev = [g_app nextEventMatchingMask:NSEventMaskAny
                             untilDate:[NSDate distantPast]
                                inMode:NSDefaultRunLoopMode
                               dequeue:YES];
        if (!ev) return 0;

        memset(event, 0, sizeof(*event));
        switch (ev.type) {
        case NSEventTypeKeyDown:
            event->type = GHAL_INPUT_KEY_DOWN;
            event->key.keycode = (uint32_t)ev.keyCode;
            break;
        case NSEventTypeKeyUp:
            event->type = GHAL_INPUT_KEY_UP;
            event->key.keycode = (uint32_t)ev.keyCode;
            break;
        case NSEventTypeMouseMoved:
        case NSEventTypeLeftMouseDragged:
            event->type = GHAL_INPUT_MOUSE_MOVE;
            event->mouse.x = (int32_t)ev.locationInWindow.x;
            event->mouse.y = (int32_t)ev.locationInWindow.y;
            break;
        default:
            [g_app sendEvent:ev];  /* pass other events to Cocoa */
            return 0;
        }
        return 1;
    }
}

/* ── Driver registration ─────────────────────────────────────── */

static GHalDriver s_macos_driver = {
    .name             = "metal",
    .init             = macos_init,
    .shutdown         = macos_shutdown,
    .get_display_size = macos_get_display_size,
    .preferred_format = macos_preferred_format,
    .win_open         = macos_win_open,
    .win_close        = macos_win_close,
    .win_set_title    = macos_win_set_title,
    .win_resize       = macos_win_resize,
    .surface_create   = macos_surface_create,
    .surface_destroy  = macos_surface_destroy,
    .surface_lock     = macos_surface_lock,
    .surface_unlock   = macos_surface_unlock,
    .surface_present  = macos_surface_present,
    .gpu_begin_frame  = macos_gpu_begin_frame,
    .gpu_submit       = macos_gpu_submit,
    .gpu_end_frame    = macos_gpu_end_frame,
    .vsync            = macos_vsync,
    .vsync_hz         = 60,
    .poll_input       = macos_poll_input,
};

__attribute__((constructor(150)))
static void register_macos_ghal(void) {
    g_ghal_driver = &s_macos_driver;
}
