/* host/macos/ghal_metal.m — Metal GPU utilities
 *
 * Provides helper functions for Metal command buffer management,
 * texture upload, and blit operations used by the macOS backend.
 * GPU path is optional — software fallback is always available.
 */

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Foundation/Foundation.h>

#include "ghal.h"

/* ── Blit helper — upload RGBA/BGRA CPU buffer to MTLTexture ─── */

int ghal_metal_upload_texture(id<MTLDevice> device,
                               id<MTLCommandQueue> queue __attribute__((unused)),
                               const void *pixels,
                               uint32_t w, uint32_t h, uint32_t stride,
                               id<MTLTexture> *out_tex) {
    @autoreleasepool {
        MTLTextureDescriptor *desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                              width:w
                                                             height:h
                                                          mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead;
        desc.storageMode = MTLStorageModeShared;

        id<MTLTexture> tex = [device newTextureWithDescriptor:desc];
        if (!tex) return -1;

        MTLRegion region = MTLRegionMake2D(0, 0, w, h);
        [tex replaceRegion:region mipmapLevel:0 withBytes:pixels bytesPerRow:stride];

        if (out_tex) *out_tex = tex;
        return 0;
    }
}

/* ── Blit texture to drawable ────────────────────────────────── */

int ghal_metal_blit_to_drawable(id<MTLCommandQueue> queue,
                                 id<MTLTexture>       src_tex,
                                 id<CAMetalDrawable>  drawable) {
    @autoreleasepool {
        if (!queue || !src_tex || !drawable) return -1;

        id<MTLCommandBuffer> cmd = [queue commandBuffer];
        if (!cmd) return -1;

        id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
        [blit copyFromTexture:src_tex
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(src_tex.width, src_tex.height, 1)
                    toTexture:drawable.texture
             destinationSlice:0
             destinationLevel:0
            destinationOrigin:MTLOriginMake(0, 0, 0)];
        [blit endEncoding];

        [cmd presentDrawable:drawable];
        [cmd commit];
        [cmd waitUntilCompleted];
        return 0;
    }
}

/* ── Simple compute pass wrapper (Phase 8 stub) ──────────────── */

int ghal_metal_compute_dispatch(id<MTLDevice> device __attribute__((unused)),
                                 id<MTLCommandQueue> queue,
                                 id<MTLComputePipelineState> pso,
                                 id<MTLBuffer> in_buf,
                                 id<MTLBuffer> out_buf,
                                 uint32_t count) {
    @autoreleasepool {
        id<MTLCommandBuffer> cmd = [queue commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];

        [enc setComputePipelineState:pso];
        [enc setBuffer:in_buf  offset:0 atIndex:0];
        [enc setBuffer:out_buf offset:0 atIndex:1];

        NSUInteger w = pso.maxTotalThreadsPerThreadgroup;
        if (w > count) w = count;
        MTLSize tg   = MTLSizeMake(w,     1, 1);
        MTLSize grid = MTLSizeMake(count, 1, 1);
        [enc dispatchThreads:grid threadsPerThreadgroup:tg];
        [enc endEncoding];

        [cmd commit];
        [cmd waitUntilCompleted];
        return 0;
    }
}
