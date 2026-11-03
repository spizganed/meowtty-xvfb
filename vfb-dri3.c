/*
 * A small DRI3 for Xvfb, for MeowTTY. Licence: MIT (X11), the same as the X server.
 *
 * The Mesa build for Adreno (lfdevs/mesa-for-android-container) needs DRI3 in the X server. It sends each frame as one
 * linear buffer fd (PixmapFromBuffers). This file maps that fd and makes the memory the pixmap, so Present copies the
 * frame into the screen with the CPU. There is no render node: DRI3Open fails, and Mesa goes on without it.
 * The idea comes from the DRI3 of Termux:X11 (it accepts linear fds the same way); this is our own code.
 *
 * A compositor (KWin) needs the window pixmaps as fds (FdsFromPixmap) to use them as GPU textures. When the composite
 * extension asks for a window pixmap, this file puts it in a dma-buf from /dev/dma_heap/system, which the Adreno Mesa
 * can import. Other pixmaps stay in normal memory.
 */
#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <drm_fourcc.h>

#include "scrnintstr.h"
#include "pixmapstr.h"
#include "privates.h"
#include "dri3.h"
#include "vfb-dri3.h"

#define RAW_MMAPPABLE_FD 1274

/* linux/dma-heap.h */
struct vfb_dma_heap_allocation_data {
    uint64_t len;
    uint32_t fd;
    uint32_t fd_flags;
    uint64_t heap_flags;
};
#define VFB_DMA_HEAP_IOCTL_ALLOC _IOWR('H', 0x0, struct vfb_dma_heap_allocation_data)

/* Window pixmaps smaller than this stay in normal memory. */
#define DMABUF_MIN_PIXELS (32 * 32)

typedef struct {
    void *map;
    size_t size;
    int fd_plus_1; /* The fd of the memory plus 1: the private starts as zeros. */
    CARD32 stride;
} VfbDri3Pixmap;

typedef struct {
    DestroyPixmapProcPtr DestroyPixmap;
    CreatePixmapProcPtr CreatePixmap;
    int heap; /* /dev/dma_heap/system, or -1 */
} VfbDri3Screen;

static DevPrivateKeyRec vfbDri3PixmapKey;
static DevPrivateKeyRec vfbDri3ScreenKey;

#define PIXMAP_PRIV(p) ((VfbDri3Pixmap *) dixLookupPrivate(&(p)->devPrivates, &vfbDri3PixmapKey))
#define SCREEN_PRIV(s) ((VfbDri3Screen *) dixLookupPrivate(&(s)->devPrivates, &vfbDri3ScreenKey))

static Bool
vfbDri3DestroyPixmap(PixmapPtr pixmap)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    VfbDri3Screen *s = SCREEN_PRIV(screen);
    VfbDri3Pixmap *p = PIXMAP_PRIV(pixmap);
    Bool ret;

    /* The last reference: unmap the buffer. */
    if (pixmap->refcnt == 1 && p->map) {
        munmap(p->map, p->size);
        p->map = NULL;
    }
    if (pixmap->refcnt == 1 && p->fd_plus_1) {
        close(p->fd_plus_1 - 1);
        p->fd_plus_1 = 0;
    }
    screen->DestroyPixmap = s->DestroyPixmap;
    ret = screen->DestroyPixmap(pixmap);
    s->DestroyPixmap = screen->DestroyPixmap;
    screen->DestroyPixmap = vfbDri3DestroyPixmap;
    return ret;
}

static PixmapPtr
vfbDri3PixmapFromFds(ScreenPtr screen, CARD8 num_fds, const int *fds, CARD16 width, CARD16 height,
                     const CARD32 *strides, const CARD32 *offsets, CARD8 depth, CARD8 bpp, CARD64 modifier)
{
    PixmapPtr pixmap;
    VfbDri3Pixmap *p;
    size_t size;
    void *map;

    if (num_fds != 1 || bpp != 32 || (depth != 24 && depth != 32))
        return NULL;
    /* The Adreno Mesa (its Termux:X11 patch) marks a plain mappable fd with the modifier 1274. */
    if (modifier != DRM_FORMAT_MOD_INVALID && modifier != DRM_FORMAT_MOD_LINEAR && modifier != RAW_MMAPPABLE_FD)
        return NULL;
    if (strides[0] < (CARD32) width * 4)
        return NULL;

    size = (size_t) offsets[0] + (size_t) strides[0] * height;
    map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fds[0], 0);
    if (map == MAP_FAILED) {
        ErrorF("vfb-dri3: mmap of %zu bytes failed: %s\n", size, strerror(errno));
        return NULL;
    }

    pixmap = screen->CreatePixmap(screen, 0, 0, depth, 0);
    if (!pixmap) {
        munmap(map, size);
        return NULL;
    }
    if (!screen->ModifyPixmapHeader(pixmap, width, height, depth, bpp, strides[0], (char *) map + offsets[0])) {
        munmap(map, size);
        screen->DestroyPixmap(pixmap);
        return NULL;
    }
    p = PIXMAP_PRIV(pixmap);
    p->map = map;
    p->size = size;
    /* Keep the fd: a compositor can ask for it again (FdsFromPixmap). Only for a buffer at offset 0. */
    if (offsets[0] == 0) {
        int fd = fcntl(fds[0], F_DUPFD_CLOEXEC, 3);
        if (fd >= 0) {
            p->fd_plus_1 = fd + 1;
            p->stride = strides[0];
        }
    }
    return pixmap;
}

/* A window pixmap of the composite extension in a dma-buf; other pixmaps as usual. */
static PixmapPtr
vfbDri3CreatePixmap(ScreenPtr screen, int width, int height, int depth, unsigned usage)
{
    VfbDri3Screen *s = SCREEN_PRIV(screen);
    struct vfb_dma_heap_allocation_data alloc = { 0 };
    PixmapPtr pixmap;
    VfbDri3Pixmap *p;
    CARD32 stride;
    size_t size;
    void *map;

    if (usage != CREATE_PIXMAP_USAGE_BACKING_PIXMAP || s->heap < 0 || (depth != 24 && depth != 32) ||
        width <= 0 || height <= 0 || width > 16384 || height > 16384 || width * height < DMABUF_MIN_PIXELS)
        goto plain;
    /* A 256-byte row and whole pages: the GPU import needs aligned buffers. */
    stride = ((CARD32) width * 4 + 255) & ~255u;
    size = ((size_t) stride * height + 4095) & ~(size_t) 4095;
    alloc.len = size;
    alloc.fd_flags = O_RDWR | O_CLOEXEC;
    if (ioctl(s->heap, VFB_DMA_HEAP_IOCTL_ALLOC, &alloc) != 0)
        goto plain;
    map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, (int) alloc.fd, 0);
    if (map == MAP_FAILED) {
        close((int) alloc.fd);
        goto plain;
    }
    screen->CreatePixmap = s->CreatePixmap;
    pixmap = screen->CreatePixmap(screen, 0, 0, depth, usage);
    screen->CreatePixmap = vfbDri3CreatePixmap;
    if (!pixmap || !screen->ModifyPixmapHeader(pixmap, width, height, depth, 32, stride, map)) {
        if (pixmap)
            screen->DestroyPixmap(pixmap);
        munmap(map, size);
        close((int) alloc.fd);
        goto plain;
    }
    p = PIXMAP_PRIV(pixmap);
    p->map = map;
    p->size = size;
    p->fd_plus_1 = (int) alloc.fd + 1;
    p->stride = stride;
    return pixmap;

plain:
    screen->CreatePixmap = s->CreatePixmap;
    pixmap = screen->CreatePixmap(screen, width, height, depth, usage);
    screen->CreatePixmap = vfbDri3CreatePixmap;
    return pixmap;
}

/* Only pixmaps in a dma-buf or a client buffer have an fd. */
static int
vfbDri3FdFromPixmap(ScreenPtr screen, PixmapPtr pixmap, CARD16 *stride, CARD32 *size)
{
    VfbDri3Pixmap *p = PIXMAP_PRIV(pixmap);

    if (!p->fd_plus_1 || p->stride > 65535)
        return -1;
    *stride = (CARD16) p->stride;
    *size = (CARD32) p->size;
    return fcntl(p->fd_plus_1 - 1, F_DUPFD_CLOEXEC, 3);
}

static int
vfbDri3FdsFromPixmap(ScreenPtr screen, PixmapPtr pixmap, int *fds, uint32_t *strides, uint32_t *offsets, uint64_t *modifier)
{
    VfbDri3Pixmap *p = PIXMAP_PRIV(pixmap);

    if (!p->fd_plus_1)
        return 0;
    fds[0] = fcntl(p->fd_plus_1 - 1, F_DUPFD_CLOEXEC, 3);
    if (fds[0] < 0)
        return 0;
    strides[0] = p->stride;
    offsets[0] = 0;
    *modifier = DRM_FORMAT_MOD_LINEAR;
    return 1;
}

/* The DRI3 core frees the lists that these return: each call gives a new copy. */
static int
vfbDri3GetFormats(ScreenPtr screen, CARD32 *num_formats, CARD32 **formats)
{
    CARD32 *list = calloc(2, sizeof(CARD32));
    if (!list)
        return FALSE;
    list[0] = DRM_FORMAT_XRGB8888;
    list[1] = DRM_FORMAT_ARGB8888;
    *num_formats = 2;
    *formats = list;
    return TRUE;
}

static int
vfbDri3GetModifiers(ScreenPtr screen, uint32_t format, uint32_t *num_modifiers, uint64_t **modifiers)
{
    uint64_t *list;
    *num_modifiers = 0;
    *modifiers = NULL;
    if (format != DRM_FORMAT_XRGB8888 && format != DRM_FORMAT_ARGB8888)
        return TRUE;
    list = calloc(1, sizeof(uint64_t));
    if (!list)
        return FALSE;
    list[0] = DRM_FORMAT_MOD_LINEAR;
    *num_modifiers = 1;
    *modifiers = list;
    return TRUE;
}

static int
vfbDri3GetDrawableModifiers(DrawablePtr draw, uint32_t format, uint32_t *num_modifiers, uint64_t **modifiers)
{
    return vfbDri3GetModifiers(draw->pScreen, format, num_modifiers, modifiers);
}

static const dri3_screen_info_rec vfbDri3Info = {
    .version = 2,
    .open = NULL,
    .pixmap_from_fd = NULL,
    .fd_from_pixmap = vfbDri3FdFromPixmap,
    .open_client = NULL,
    .get_formats = vfbDri3GetFormats,
    .get_modifiers = vfbDri3GetModifiers,
    .get_drawable_modifiers = vfbDri3GetDrawableModifiers,
    .pixmap_from_fds = vfbDri3PixmapFromFds,
    .fds_from_pixmap = vfbDri3FdsFromPixmap,
};

Bool
vfbDri3Init(ScreenPtr screen)
{
    VfbDri3Screen *s;

    if (!dixRegisterPrivateKey(&vfbDri3PixmapKey, PRIVATE_PIXMAP, sizeof(VfbDri3Pixmap)))
        return FALSE;
    if (!dixRegisterPrivateKey(&vfbDri3ScreenKey, PRIVATE_SCREEN, sizeof(VfbDri3Screen)))
        return FALSE;
    s = SCREEN_PRIV(screen);
    s->DestroyPixmap = screen->DestroyPixmap;
    screen->DestroyPixmap = vfbDri3DestroyPixmap;
    s->CreatePixmap = screen->CreatePixmap;
    screen->CreatePixmap = vfbDri3CreatePixmap;
    s->heap = open("/dev/dma_heap/system", O_RDONLY | O_CLOEXEC);
    if (s->heap < 0)
        ErrorF("vfb-dri3: no /dev/dma_heap/system (%s): window pixmaps have no fd\n", strerror(errno));
    return dri3_screen_init(screen, &vfbDri3Info);
}
