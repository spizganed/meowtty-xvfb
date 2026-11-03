/*
 * GLX for the native Xvfb of MeowTTY, with direct rendering only (MIT, like the X server).
 *
 * The clients render with their own Mesa (through DRI3 or the software path). The server only keeps the GLX records:
 * the fbconfigs and visuals, the contexts and the drawables. It has no GL of its own, so indirect contexts fail.
 * The fbconfigs copy the values of the gallium driver configs (src/gallium/frontends/dri of Mesa): the client keeps
 * only the server fbconfigs that match a config of its driver.
 */
#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <GL/glxtokens.h>

#include "glxserver.h"
#include "glxutil.h"
#include "extension_string.h"
#include "extinit.h"

static GLboolean
vfbGlxSwapBuffers(ClientPtr client, __GLXdrawable *drawable)
{
    return GL_TRUE; /* A direct context swaps in the client. */
}

static void
vfbGlxDrawableDestroy(__GLXdrawable *drawable)
{
    __glXDrawableRelease(drawable);
    free(drawable);
}

static __GLXdrawable *
vfbGlxCreateDrawable(ClientPtr client, __GLXscreen *screen, DrawablePtr pDraw, XID drawId, int type, XID glxDrawId,
                     __GLXconfig *config)
{
    __GLXdrawable *drawable = calloc(1, sizeof(*drawable));

    if (!drawable)
        return NULL;
    if (!__glXDrawableInit(drawable, screen, pDraw, type, glxDrawId, config)) {
        free(drawable);
        return NULL;
    }
    drawable->destroy = vfbGlxDrawableDestroy;
    drawable->swapBuffers = vfbGlxSwapBuffers;
    return drawable;
}

static __GLXcontext *
vfbGlxCreateContext(__GLXscreen *screen, __GLXconfig *config, __GLXcontext *share, unsigned num_attribs,
                    const uint32_t *attribs, int *error)
{
    *error = BadValue; /* No indirect contexts: the server has no GL. */
    return NULL;
}

static void
vfbGlxScreenDestroy(__GLXscreen *screen)
{
    __glXScreenDestroy(screen); /* It frees the fbconfigs too. */
    free(screen);
}

static glx_func_ptr
vfbGlxGetProcAddress(const char *name)
{
    return NULL;
}

/* One fbconfig. alpha: the 32-bit ARGB format, else XRGB. */
static __GLXconfig *
vfbGlxConfig(int alpha, int depth, int stencil, int doubleBuffer, int accum, int visualType, int forComposite)
{
    __GLXconfig *c = calloc(1, sizeof(*c));

    if (!c)
        return NULL;
    c->doubleBufferMode = doubleBuffer;
    c->redBits = c->greenBits = c->blueBits = 8;
    c->alphaBits = alpha ? 8 : 0;
    c->redMask = 0x00ff0000;
    c->greenMask = 0x0000ff00;
    c->blueMask = 0x000000ff;
    c->alphaMask = alpha ? 0xff000000 : 0;
    c->rgbBits = alpha ? 32 : 24;
    c->accumRedBits = c->accumGreenBits = c->accumBlueBits = c->accumAlphaBits = accum ? 16 : 0;
    c->depthBits = depth;
    c->stencilBits = stencil;
    c->visualType = visualType;
    /* The driver marks a config with an accumulation buffer as slow. */
    c->visualRating = accum ? GLX_SLOW_CONFIG : GLX_NONE;
    c->transparentPixel = GLX_NONE;
    c->transparentRed = c->transparentGreen = c->transparentBlue = c->transparentAlpha = GLX_DONT_CARE;
    c->transparentIndex = GLX_NONE;
    c->drawableType = GLX_WINDOW_BIT | GLX_PIXMAP_BIT | GLX_PBUFFER_BIT;
    c->renderType = GLX_RGBA_BIT;
    c->swapMethod = GLX_SWAP_UNDEFINED_OML;
    c->bindToTextureRgb = GL_TRUE;
    c->bindToTextureRgba = GL_TRUE;
    c->bindToMipmapTexture = GL_FALSE;
    c->bindToTextureTargets = GLX_TEXTURE_1D_BIT_EXT | GLX_TEXTURE_2D_BIT_EXT | GLX_TEXTURE_RECTANGLE_BIT_EXT;
    c->yInverted = GL_TRUE;
#ifdef COMPOSITE
    c->duplicatedForComp = forComposite;
#endif
    return c;
}

/* The configs in the order of glxConvertConfigs: TrueColor, DirectColor, then the copies for the composite visual. */
static __GLXconfig *
vfbGlxConfigs(void)
{
    static const int zs[][2] = { { 0, 0 }, { 16, 0 }, { 24, 0 }, { 24, 8 }, { 32, 0 } };
    static const int types[] = { GLX_TRUE_COLOR, GLX_DIRECT_COLOR };
    __GLXconfig head = { 0 }, *tail = &head;
    int pass, alpha, k, db, accum;

    for (pass = 0; pass < 3; pass++) {
        int composite = pass == 2;
#ifdef COMPOSITE
        if (composite && noCompositeExtension)
            break;
#else
        if (composite)
            break;
#endif
        for (alpha = 1; alpha >= 0; alpha--)
            for (k = 0; k < (int) ARRAY_SIZE(zs); k++)
                for (db = 1; db >= 0; db--)
                    for (accum = 0; accum <= 1; accum++) {
                        /* The composite visual takes only 32-bit configs that are not slow. */
                        if (composite && (!alpha || accum))
                            continue;
                        tail->next = vfbGlxConfig(alpha, zs[k][0], zs[k][1], db, accum,
                                                  composite ? GLX_TRUE_COLOR : types[pass], composite);
                        if (!tail->next)
                            return head.next;
                        tail = tail->next;
                    }
    }
    return head.next;
}

static __GLXscreen *
vfbGlxScreenProbe(ScreenPtr pScreen)
{
    __GLXscreen *screen = calloc(1, sizeof(*screen));

    if (!screen)
        return NULL;
    screen->destroy = vfbGlxScreenDestroy;
    screen->createContext = vfbGlxCreateContext;
    screen->createDrawable = vfbGlxCreateDrawable;
    screen->swapInterval = NULL;
    screen->pScreen = pScreen;

    __glXInitExtensionEnableBits(screen->glx_enable_bits);
    __glXEnableExtension(screen->glx_enable_bits, "GLX_EXT_no_config_context");
    __glXEnableExtension(screen->glx_enable_bits, "GLX_ARB_create_context");
    __glXEnableExtension(screen->glx_enable_bits, "GLX_ARB_create_context_no_error");
    __glXEnableExtension(screen->glx_enable_bits, "GLX_ARB_create_context_profile");
    __glXEnableExtension(screen->glx_enable_bits, "GLX_EXT_create_context_es_profile");
    __glXEnableExtension(screen->glx_enable_bits, "GLX_EXT_create_context_es2_profile");
    __glXEnableExtension(screen->glx_enable_bits, "GLX_EXT_framebuffer_sRGB");
    __glXEnableExtension(screen->glx_enable_bits, "GLX_EXT_texture_from_pixmap");

    screen->fbconfigs = vfbGlxConfigs();
    if (!screen->fbconfigs) {
        free(screen);
        return NULL;
    }
    screen->glvnd = strdup("mesa");
    __glXScreenInit(screen, pScreen);
    __glXsetGetProcAddress(vfbGlxGetProcAddress);
    LogMessage(X_INFO, "GLX: direct rendering only, %d fbconfigs\n", screen->numFBConfigs);
    return screen;
}

__GLXprovider __glXMeowProvider = {
    vfbGlxScreenProbe,
    "MeowTTY",
    NULL
};
