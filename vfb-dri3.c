/*
 * A small DRI3 for Xvfb, for MeowTTY. Licence: MIT (X11), the same as the X server.
 *
 * The Mesa build for Adreno (lfdevs/mesa-for-android-container) needs DRI3 in the X server. It sends each frame as one
 * linear buffer fd (PixmapFromBuffers). This file maps that fd and makes the memory the pixmap, so Present copies the
 * frame into the screen with the CPU. There is no render node: DRI3Open fails, and Mesa goes on without it.
 * The idea comes from the DRI3 of Termux:X11 (it accepts linear fds the same way); this is our own code.
 */
#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <drm_fourcc.h>

#include "scrnintstr.h"
#include "pixmapstr.h"
#include "privates.h"
#include "dri3.h"
#include "vfb-dri3.h"

#define RAW_MMAPPABLE_FD 1274

typedef struct {
    void *map;
    size_t size;
} VfbDri3Pixmap;

typedef struct {
    DestroyPixmapProcPtr DestroyPixmap;
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

    /* The last reference: unmap the client buffer. */
    if (pixmap->refcnt == 1 && p->map) {
        munmap(p->map, p->size);
        p->map = NULL;
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
    return pixmap;
}

static int
vfbDri3FdFromPixmap(ScreenPtr screen, PixmapPtr pixmap, CARD16 *stride, CARD32 *size)
{
    return -1; /* Pixmaps of the server have no fd. */
}

static int
vfbDri3FdsFromPixmap(ScreenPtr screen, PixmapPtr pixmap, int *fds, uint32_t *strides, uint32_t *offsets, uint64_t *modifier)
{
    return 0;
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
    return dri3_screen_init(screen, &vfbDri3Info);
}
