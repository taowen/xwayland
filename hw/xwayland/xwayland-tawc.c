/* SPDX-License-Identifier: MIT
 * Android storage backend for upstream Glamor. X11 windows, including native
 * client drawables, share the ordinary X backing pixmap and clip machinery.
 * Only complete Xwayland window buffers cross the Wayland connection.
 */
#include <xwayland-config.h>
#include <android/hardware_buffer.h>
#include <android/log.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <epoxy/gl.h>
#include <epoxy/egl.h>
#include <glamor.h>
#include <glamor_context.h>
#include <arlinux/tawc-dri.h>
#include "windowstr.h"
#include "gcstruct.h"
#include "xwayland-screen.h"
#include "xwayland-pixmap.h"
#include "xwayland-tawc.h"
#include "wayland-android-client-protocol.h"

/* gralloc's native_handle_t ABI; import clones the supplied descriptors. */
struct native_handle { int version, num_fds, num_ints; int data[]; };
static int (*import_handle)(const AHardwareBuffer_Desc *, const struct native_handle *, int32_t, AHardwareBuffer **);
static const struct native_handle *(*export_handle)(const AHardwareBuffer *);
static DevPrivateKeyRec ahb_key;
static DevPrivateKeyRec screen_key;
struct ahb_screen { CloseScreenProcPtr close; };
struct ahb_pixmap {
    AHardwareBuffer *allocation;
    EGLImageKHR image;
    struct wl_buffer *buffer;
};
extern void tawc_dri_send_buffer_release(uint32_t, uint32_t, uint32_t);

static void android_error_log(const char *format, va_list args)
{
    __android_log_vprint(ANDROID_LOG_INFO, "Xwayland", format, args);
}

static void make_current(struct glamor_context *ctx)
{
    if (!eglMakeCurrent(ctx->display, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx->ctx))
        FatalError("Xwayland: Android EGL context lost\n");
}

void glamor_egl_screen_init(ScreenPtr screen, struct glamor_context *ctx)
{
    struct xwl_screen *xwl = xwl_screen_get(screen);
    ctx->display = xwl->egl_display;
    ctx->ctx = xwl->egl_context;
    ctx->make_current = make_current;
    xwl->glamor_ctx = ctx;
}

/* This backend deliberately does not advertise the DRM/DRI3 export API. */
int glamor_egl_fd_name_from_pixmap(ScreenPtr screen, PixmapPtr pixmap,
                                 CARD16 *stride, CARD32 *size) { return -1; }

static Bool destroy_pixmap(PixmapPtr pixmap)
{
    struct ahb_pixmap *ahb = dixLookupPrivate(&pixmap->devPrivates, &ahb_key);
    if (pixmap->refcnt != 1 || !ahb)
        return glamor_destroy_pixmap(pixmap);
    EGLDisplay egl_display = xwl_screen_get(pixmap->drawable.pScreen)->egl_display;
    xwl_pixmap_del_buffer_release_cb(pixmap);
    if (ahb->buffer) wl_buffer_destroy(ahb->buffer);
    /* Glamor owns the texture; drop it before the EGLImage/allocation. */
    Bool result = glamor_destroy_pixmap(pixmap);
    eglDestroyImageKHR(egl_display, ahb->image);
    AHardwareBuffer_release(ahb->allocation);
    free(ahb);
    return result;
}

/* On success the pixmap takes ownership of allocation. */
static PixmapPtr import_pixmap(ScreenPtr screen, AHardwareBuffer *allocation,
                              int width, int height, int depth, Bool opaque)
{
    struct xwl_screen *xwl = xwl_screen_get(screen);
    struct ahb_pixmap *ahb = calloc(1, sizeof(*ahb));
    if (!ahb) return NULL;
    make_current(xwl->glamor_ctx);
    EGLint attributes[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
    ahb->image = eglCreateImageKHR(xwl->egl_display, EGL_NO_CONTEXT,
        EGL_NATIVE_BUFFER_ANDROID, eglGetNativeClientBufferANDROID(allocation), attributes);
    if (ahb->image == EGL_NO_IMAGE_KHR) {
        ErrorF("Xwayland: AHB image import failed (EGL %#x)\n", eglGetError());
        goto fail;
    }
    PixmapPtr pixmap = glamor_create_pixmap(screen, width, height, depth,
                                          GLAMOR_CREATE_PIXMAP_NO_TEXTURE);
    if (!pixmap) goto fail_image;
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (opaque) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, ahb->image);
    if (!glamor_set_pixmap_texture(pixmap, texture)) {
        glamor_destroy_pixmap(pixmap);
        goto fail_image;
    }
    glamor_set_pixmap_type(pixmap, GLAMOR_TEXTURE_DRM);
    ahb->allocation = allocation;
    dixSetPrivate(&pixmap->devPrivates, &ahb_key, ahb);
    return pixmap;
fail_image:
    eglDestroyImageKHR(xwl->egl_display, ahb->image);
fail:
    free(ahb);
    return NULL;
}

static PixmapPtr create_pixmap(ScreenPtr screen, int width, int height,
                              int depth, unsigned int hint)
{
    if (!width || !height || (depth != 24 && depth != 32) ||
        hint == CREATE_PIXMAP_USAGE_GLYPH_PICTURE)
        return glamor_create_pixmap(screen, width, height, depth, hint);
    AHardwareBuffer_Desc desc = {
        .width = width, .height = height, .layers = 1,
        /* Glamor uploads depth-24 pixels as RGBA too. RGBX imports can have
         * RGB internal storage, which GLES rejects for RGBA TexSubImage2D.
         * Match the upstream GLES GBM backend: use RGBA storage for both
         * depths; the X drawable depth still controls alpha semantics. */
        .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
        .usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT,
    };
    AHardwareBuffer *allocation = NULL;
    if (AHardwareBuffer_allocate(&desc, &allocation)) return NULL;
    PixmapPtr pixmap = import_pixmap(screen, allocation, width, height, depth, FALSE);
    if (!pixmap) AHardwareBuffer_release(allocation);
    return pixmap;
}

static Bool create_screen_resources(ScreenPtr screen)
{
    struct xwl_screen *xwl = xwl_screen_get(screen);
    screen->CreateScreenResources = xwl->CreateScreenResources;
    Bool result = screen->CreateScreenResources(screen);
    xwl->CreateScreenResources = screen->CreateScreenResources;
    screen->CreateScreenResources = create_screen_resources;
    if (!result) return FALSE;
    screen->devPrivate = create_pixmap(screen, xwl->rootless ? 0 : xwl->width,
        xwl->rootless ? 0 : xwl->height, screen->rootDepth, CREATE_PIXMAP_USAGE_BACKING_PIXMAP);
    SetRootClip(screen, xwl->root_clip_mode);
    return screen->devPrivate != NULL;
}

static Bool close_screen(ScreenPtr screen)
{
    struct xwl_screen *xwl = xwl_screen_get(screen);
    struct ahb_screen *state = dixLookupPrivate(&screen->devPrivates, &screen_key);
    EGLDisplay egl_display = xwl->egl_display;
    EGLContext context = xwl->egl_context;
    PixmapPtr root = screen->GetScreenPixmap(screen);
    struct ahb_pixmap *ahb = dixLookupPrivate(&root->devPrivates, &ahb_key);
    /* Glamor restores fbDestroyPixmap before freeing the root pixmap. Keep
     * its Android storage alive until Glamor has destroyed the GL texture. */
    dixSetPrivate(&root->devPrivates, &ahb_key, NULL);
    if (ahb && ahb->buffer) {
        xwl_pixmap_del_buffer_release_cb(root);
        wl_buffer_destroy(ahb->buffer);
    }
    screen->CloseScreen = state->close;
    Bool result = screen->CloseScreen(screen);
    if (ahb) {
        eglDestroyImageKHR(egl_display, ahb->image);
        AHardwareBuffer_release(ahb->allocation);
        free(ahb);
    }
    eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(egl_display, context);
    eglTerminate(egl_display);
    return result;
}

void xwl_tawc_wrap_close(ScreenPtr screen)
{
    struct ahb_screen *state = dixLookupPrivate(&screen->devPrivates, &screen_key);
    state->close = screen->CloseScreen;
    screen->CloseScreen = close_screen;
}

Bool xwl_tawc_init(struct xwl_screen *xwl)
{
    OsVendorVErrorFProc = android_error_log;
    static void *native;
    if (!native) native = dlopen("libnativewindow.so", RTLD_NOW | RTLD_LOCAL);
    if (!native) return FALSE;
    import_handle = dlsym(native, "AHardwareBuffer_createFromHandle");
    export_handle = dlsym(native, "AHardwareBuffer_getNativeHandle");
    if (!import_handle || !export_handle || !xwl->tawc_wlegl) return FALSE;
    xwl->egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(xwl->egl_display, NULL, NULL) ||
        !epoxy_has_egl_extension(xwl->egl_display, "EGL_KHR_surfaceless_context") ||
        !epoxy_has_egl_extension(xwl->egl_display, "EGL_ANDROID_get_native_client_buffer"))
        return FALSE;
    EGLConfig config;
    EGLint count;
    EGLint attributes[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
    EGLint context[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    if (!eglChooseConfig(xwl->egl_display, attributes, &config, 1, &count) || !count ||
        !eglBindAPI(EGL_OPENGL_ES_API)) return FALSE;
    xwl->egl_context = eglCreateContext(xwl->egl_display, config, EGL_NO_CONTEXT, context);
    if (xwl->egl_context == EGL_NO_CONTEXT) return FALSE;
    if (!dixRegisterPrivateKey(&ahb_key, PRIVATE_PIXMAP, 0) ||
        !dixRegisterPrivateKey(&screen_key, PRIVATE_SCREEN, sizeof(struct ahb_screen)) ||
        !glamor_init(xwl->screen, GLAMOR_USE_EGL_SCREEN)) return FALSE;
    xwl->glamor = XWL_GLAMOR_GLES;
    xwl->screen->CreatePixmap = create_pixmap;
    xwl->screen->DestroyPixmap = destroy_pixmap;
    xwl->CreateScreenResources = xwl->screen->CreateScreenResources;
    xwl->screen->CreateScreenResources = create_screen_resources;
    ErrorF("Xwayland: Glamor/AHB renderer: %s\n", glGetString(GL_RENDERER));
    return TRUE;
}

struct wl_buffer *xwl_tawc_pixmap_get_wl_buffer(PixmapPtr pixmap)
{
    struct ahb_pixmap *ahb = dixLookupPrivate(&pixmap->devPrivates, &ahb_key);
    if (!ahb) return NULL;
    struct xwl_screen *xwl = xwl_screen_get(pixmap->drawable.pScreen);
    make_current(xwl->glamor_ctx);
    /* android_wlegl has no acquire fence. Finish GPU writes before handing off;
     * this synchronizes GPU work, it does not map or read pixels on the CPU. */
    glFinish();
    if (ahb->buffer) return ahb->buffer;
    AHardwareBuffer_Desc desc;
    AHardwareBuffer_describe(ahb->allocation, &desc);
    const struct native_handle *native = export_handle(ahb->allocation);
    if (!native) return NULL;
    struct wl_array ints = { .size = native->num_ints * sizeof(int),
        .data = (void *)(native->data + native->num_fds) };
    struct android_wlegl_handle *handle = android_wlegl_create_handle(xwl->tawc_wlegl, native->num_fds, &ints);
    if (!handle) return NULL;
    for (int i = 0; i < native->num_fds; i++)
        android_wlegl_handle_add_fd(handle, native->data[i]);
    ahb->buffer = android_wlegl_create_buffer(xwl->tawc_wlegl, desc.width, desc.height,
        desc.stride, desc.format, desc.usage, handle);
    android_wlegl_handle_destroy(handle);
    static const struct wl_buffer_listener listener = { xwl_pixmap_buffer_release_cb };
    if (ahb->buffer) wl_buffer_add_listener(ahb->buffer, &listener, pixmap);
    return ahb->buffer;
}

int xwl_tawc_present_native_handle(WindowPtr window, int *fds, int num_fds,
    const int32_t *ints, int num_ints, int width, int height, int stride,
    int format, uint64_t usage, uint32_t client_mask, uint32_t serial, uint32_t flags)
{
    int result = BadMatch;
    if (!window || width <= 0 || height <= 0 || stride < width ||
        num_fds <= 0 || num_fds > 64 || num_ints < 0 || num_ints > 1024) goto done;
    if (!window->realized) { result = Success; goto done; }
    ScreenPtr screen = window->drawable.pScreen;
    struct native_handle *handle = malloc(sizeof(*handle) + (num_fds + num_ints) * sizeof(int));
    if (!handle) { result = BadAlloc; goto done; }
    *handle = (struct native_handle){ sizeof(*handle), num_fds, num_ints };
    memcpy(handle->data, fds, num_fds * sizeof(int));
    memcpy(handle->data + num_fds, ints, num_ints * sizeof(int));
    AHardwareBuffer_Desc desc = { .width = width, .height = height, .stride = stride,
        .layers = 1, .format = format, .usage = usage };
    AHardwareBuffer *allocation = NULL;
    int status = import_handle(&desc, handle, 3 /* CLONE */, &allocation);
    free(handle);
    if (status) {
        ErrorF("Xwayland: AHB handle import failed (%d, %dx%d format=%d usage=%llu)\n",
            status, width, height, format, (unsigned long long)usage);
        goto done;
    }
    PixmapPtr source = import_pixmap(screen, allocation, width, height, window->drawable.depth,
                                   flags & TAWC_DRI_PRESENT_OPAQUE);
    if (!source) { AHardwareBuffer_release(allocation); goto done; }
    GCPtr gc = GetScratchGC(window->drawable.depth, screen);
    if (!gc) { result = BadAlloc; destroy_pixmap(source); goto done; }
    /* ValidateGC computes the X11 hierarchy/shape/sibling clip. Glamor executes
     * this copy on the GPU and the normal Damage/Present path submits it. */
    ValidateGC(&window->drawable, gc);
    RegionPtr exposed = gc->ops->CopyArea(&source->drawable, &window->drawable, gc,
        0, 0, min(width, window->drawable.width), min(height, window->drawable.height), 0, 0);
    if (exposed) RegionDestroy(exposed);
    FreeScratchGC(gc);
    glFinish(); /* Client may reuse its allocation after BufferRelease. */
    destroy_pixmap(source);
    result = Success;
done:
    for (int i = 0; i < num_fds; i++) if (fds[i] >= 0) close(fds[i]);
    if (result == Success)
        tawc_dri_send_buffer_release(window->drawable.id, client_mask, serial);
    return result;
}
