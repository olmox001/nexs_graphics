/* services/core/ghal_vfs.c — VFS backend selection
 *
 * Compile with -DNEXS_VFS for native NEXS file ops,
 * -DNEXS_SEL4 for seL4 IPC fs PD, otherwise POSIX fopen.
 */

#include "ghal_vfs.h"
#include <stdlib.h>
#include <string.h>

/* ================================================================
   POSIX backend (hosted / test)
   ================================================================ */
#if !defined(NEXS_VFS) && !defined(NEXS_SEL4)

#include <stdio.h>

struct GhalVfsFile { FILE *fp; };

GhalVfsFile *ghal_vfs_open(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    GhalVfsFile *f = malloc(sizeof(*f));
    if (!f) { fclose(fp); return NULL; }
    f->fp = fp;
    return f;
}
int  ghal_vfs_read(GhalVfsFile *f, void *buf, int n) {
    return (int)fread(buf, 1, (size_t)n, f->fp);
}
void ghal_vfs_skip(GhalVfsFile *f, int n) {
    fseek(f->fp, n, SEEK_CUR);
}
int  ghal_vfs_eof(GhalVfsFile *f) {
    return feof(f->fp);
}
void ghal_vfs_close(GhalVfsFile *f) {
    if (!f) return;
    fclose(f->fp);
    free(f);
}
void *ghal_vfs_read_all(const char *path, int *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);
    void *buf = malloc((size_t)sz);
    if (!buf) { fclose(fp); return NULL; }
    fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    if (out_len) *out_len = (int)sz;
    return buf;
}

/* ================================================================
   NEXS native VFS backend
   ================================================================ */
#elif defined(NEXS_VFS)

#include "nexs_vfs.h"

struct GhalVfsFile {
    int fd;
    int at_eof;
};

GhalVfsFile *ghal_vfs_open(const char *path) {
    int fd = vfs_open(path, 0); /* 0 = O_RDONLY */
    if (fd < 0) return NULL;
    GhalVfsFile *f = malloc(sizeof(*f));
    if (!f) { vfs_close(fd); return NULL; }
    f->fd = fd;
    f->at_eof = 0;
    return f;
}
int  ghal_vfs_read(GhalVfsFile *f, void *buf, int n) {
    if (!f || f->fd < 0) return -1;
    int r = vfs_read(f->fd, buf, (size_t)n);
    if (r <= 0) f->at_eof = 1;
    return r;
}
void ghal_vfs_skip(GhalVfsFile *f, int n) {
    if (!f || f->fd < 0) return;
    vfs_seek(f->fd, (int64_t)n, 1); /* 1 = SEEK_CUR */
}
int  ghal_vfs_eof(GhalVfsFile *f) {
    if (!f) return 1;
    return f->at_eof;
}
void ghal_vfs_close(GhalVfsFile *f) {
    if (!f) return;
    if (f->fd >= 0) vfs_close(f->fd);
    free(f);
}
void *ghal_vfs_read_all(const char *path, int *out_len) {
    GhalVfsFile *f = ghal_vfs_open(path);
    if (!f) return NULL;
    size_t cap = 65536, used = 0;
    uint8_t *buf = malloc(cap);
    if (!buf) { ghal_vfs_close(f); return NULL; }
    int r;
    while ((r = ghal_vfs_read(f, buf + used, (int)(cap - used))) > 0) {
        used += (size_t)r;
        if (used == cap) { cap *= 2; buf = realloc(buf, cap); }
    }
    ghal_vfs_close(f);
    if (out_len) *out_len = (int)used;
    return buf;
}


/* ================================================================
   seL4 Microkit: IPC to fs PD
   ================================================================ */
#elif defined(NEXS_SEL4)

#include <microkit.h>

/* Channels to the filesystem protection domain */
#define CH_FS_OPEN   10
#define CH_FS_READ   11
#define CH_FS_CLOSE  12

struct GhalVfsFile { uint32_t fd; int at_eof; };

GhalVfsFile *ghal_vfs_open(const char *path) {
    /* Send path via MRs */
    size_t len = strlen(path) + 1;
    const uint8_t *p = (const uint8_t *)path;
    int mr = 0;
    for (size_t i = 0; i < len && mr < 8; i += 4, mr++)
        microkit_mr_set(mr, *(const uint32_t *)(p + i));
    microkit_msginfo rep = microkit_ppcall(CH_FS_OPEN,
                               microkit_msginfo_new(0, mr));
    uint32_t fd = (uint32_t)microkit_mr_get(0);
    if (fd == (uint32_t)-1) return NULL;
    GhalVfsFile *f = malloc(sizeof(*f));
    if (!f) return NULL;
    f->fd = fd; f->at_eof = 0;
    (void)rep;
    return f;
}
int  ghal_vfs_read(GhalVfsFile *f, void *buf, int n) {
    microkit_mr_set(0, f->fd);
    microkit_mr_set(1, (uint32_t)n);
    microkit_msginfo rep = microkit_ppcall(CH_FS_READ,
                               microkit_msginfo_new(0, 2));
    int got = (int)microkit_mr_get(0);
    if (got <= 0) { f->at_eof = 1; return 0; }
    /* Data in MRs 1..N (max 28 bytes per call — good for stb chunking) */
    memcpy(buf, (void *)(uintptr_t)microkit_mr_get(1), (size_t)got);
    (void)rep;
    return got;
}
void ghal_vfs_skip(GhalVfsFile *f, int n) {
    uint8_t tmp[64];
    while (n > 0) {
        int r = ghal_vfs_read(f, tmp, n < 64 ? n : 64);
        if (r <= 0) break;
        n -= r;
    }
}
int  ghal_vfs_eof(GhalVfsFile *f) { return f->at_eof; }
void ghal_vfs_close(GhalVfsFile *f) {
    if (!f) return;
    microkit_mr_set(0, f->fd);
    microkit_ppcall(CH_FS_CLOSE, microkit_msginfo_new(0, 1));
    free(f);
}
void *ghal_vfs_read_all(const char *path, int *out_len) {
    GhalVfsFile *f = ghal_vfs_open(path);
    if (!f) return NULL;
    size_t cap = 65536, used = 0;
    uint8_t *buf = malloc(cap);
    if (!buf) { ghal_vfs_close(f); return NULL; }
    int r;
    while ((r = ghal_vfs_read(f, buf + used, (int)(cap - used))) > 0) {
        used += (size_t)r;
        if (used == cap) { cap *= 2; buf = realloc(buf, cap); }
    }
    ghal_vfs_close(f);
    if (out_len) *out_len = (int)used;
    return buf;
}

#endif /* backend selection */

/* ── stbi_io_callbacks shims (backend-independent) ───────────── */

int  ghal_vfs_stbi_read(void *user, char *data, int size) {
    return ghal_vfs_read((GhalVfsFile *)user, data, size);
}
void ghal_vfs_stbi_skip(void *user, int n) {
    ghal_vfs_skip((GhalVfsFile *)user, n);
}
int  ghal_vfs_stbi_eof(void *user) {
    return ghal_vfs_eof((GhalVfsFile *)user);
}
