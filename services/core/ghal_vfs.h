/* services/core/ghal_vfs.h — Abstract file I/O for service layer
 *
 * Routes to:
 *   POSIX  (hosted/test)  — fopen/fread/fclose
 *   NEXS VFS (native)     — nexs_open/nexs_read/nexs_close via registry IPC
 *   seL4  (microkit PD)   — sendmessage to fs PD
 *
 * Selected at compile time:
 *   default       → POSIX
 *   -DNEXS_VFS    → NEXS native VFS
 *   -DNEXS_SEL4   → seL4 IPC to fs PD
 *
 * stb_image supports stbi_io_callbacks — use ghal_vfs_stbi_cb() to get
 * a callback struct pre-wired to the right backend.
 */

#ifndef GHAL_VFS_H
#define GHAL_VFS_H
#pragma once

#include <stddef.h>
#include <stdint.h>

/* Opaque file handle */
typedef struct GhalVfsFile GhalVfsFile;

/* ── Core API ─────────────────────────────────────────────────── */

GhalVfsFile *ghal_vfs_open(const char *path);
int          ghal_vfs_read(GhalVfsFile *f, void *buf, int n);
void         ghal_vfs_skip(GhalVfsFile *f, int n);
int          ghal_vfs_eof(GhalVfsFile *f);
void         ghal_vfs_close(GhalVfsFile *f);

/* Read entire file into malloc'd buffer.  Caller frees. */
void        *ghal_vfs_read_all(const char *path, int *out_len);

/* ── stbi_io_callbacks shim ───────────────────────────────────── */

/* These three functions match the stbi_io_callbacks signatures exactly.
 * Pass them to stbi_load_from_callbacks with a GhalVfsFile* as user_data. */
int  ghal_vfs_stbi_read(void *user, char *data, int size);
void ghal_vfs_stbi_skip(void *user, int n);
int  ghal_vfs_stbi_eof(void *user);

#endif /* GHAL_VFS_H */
