/* include/ghal_3d.h — OpenGL-compatible 3D state machine interface
 *
 * Mirrors the OpenGL 1.x state machine. Implemented in lib/draw3d.c.
 * Phase 8 (future): routes to Metal, EGL, or software rasterizer.
 */

#ifndef GHAL_3D_H
#define GHAL_3D_H
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Buffer objects */
typedef uint32_t Ghal3dBuf;
typedef uint32_t Ghal3dTex;
typedef uint32_t Ghal3dShader;

/* Vertex format flags */
#define GHAL3D_VERTEX_XY   (1 << 0)
#define GHAL3D_VERTEX_XYZ  (1 << 1)
#define GHAL3D_VERTEX_UV   (1 << 2)
#define GHAL3D_VERTEX_RGBA (1 << 3)

/* Primitive types */
#define GHAL3D_TRIANGLES       0
#define GHAL3D_TRIANGLE_STRIP  1
#define GHAL3D_LINES           2

/* Texture formats */
#define GHAL3D_TEX_RGBA8  0
#define GHAL3D_TEX_RGB8   1

/* Buffer operations */
Ghal3dBuf ghal3d_buf_create(const void *data, size_t len);
void      ghal3d_buf_destroy(Ghal3dBuf buf);

/* Texture operations */
Ghal3dTex ghal3d_tex_create(uint32_t w, uint32_t h, uint32_t fmt,
                             const void *pixels);
void      ghal3d_tex_destroy(Ghal3dTex tex);
void      ghal3d_tex_bind(Ghal3dTex tex);

/* Shader (stub in Phase 4; real in Phase 8) */
Ghal3dShader ghal3d_shader_create(const char *vert_src,
                                   const char *frag_src);
void         ghal3d_shader_destroy(Ghal3dShader sh);
void         ghal3d_shader_bind(Ghal3dShader sh);

/* State */
void ghal3d_clear(float r, float g, float b, float a);
void ghal3d_viewport(int x, int y, int w, int h);

/* Draw call */
void ghal3d_draw(Ghal3dBuf verts, uint32_t vertex_fmt,
                 Ghal3dBuf indices, uint32_t n_indices,
                 uint32_t primitive);

#ifdef __cplusplus
}
#endif

#endif /* GHAL_3D_H */
