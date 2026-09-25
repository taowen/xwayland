/*
 * Minimal GLX protocol stub for Xwayland built with -Dglx=false.
 *
 * Mesa DRI (kgsl + AHB present) still requires the X server to advertise
 * GLX so clients can XInitExtension, QueryVersion, and fetch fbconfigs.
 * Rendering stays on the client; this extension only answers protocol.
 */
#include <xwayland-config.h>

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <X11/X.h>
#include "misc.h"
#include "os.h"
#include "dix.h"
#include "extinit.h"
#include "scrnintstr.h"
#include "windowstr.h"
#include "pixmapstr.h"
#include "resource.h"
#include "xace.h"
#include <string.h>

#include "xwayland-glx-stub.h"

#include <stdlib.h>

#ifndef GLX_EXTENSION_NAME
#define GLX_EXTENSION_NAME "GLX"
#endif

#define X_GLXQueryVersion             7
#define X_GLXGetVisualConfigs        14
#define X_GLXQueryExtensionsString   18
#define X_GLXQueryServerString       19
#define X_GLXClientInfo              20
#define X_GLXGetFBConfigs            21
#define X_GLXCreateNewContext        24
#define X_GLXMakeContextCurrent      26
#define X_GLXCreatePbuffer           27
#define X_GLXDestroyPbuffer          28
#define X_GLXGetDrawableAttributes   29
#define X_GLXChangeDrawableAttributes 30
#define X_GLXCreateWindow            31
#define X_GLXDestroyWindow           32
#define X_GLXSetClientInfoARB        33
#define X_GLXCreateContextAttribsARB 34
#define X_GLXSetClientInfo2ARB       35
#define X_GLXCreateContext            3
#define X_GLXDestroyContext           4
#define X_GLXMakeCurrent              5
#define X_GLXIsDirect                 6
#define X_GLXSwapBuffers             11
#define X_GLXRender                   1
#define X_GLXRenderLarge              2

#define GLX_VENDOR      1
#define GLX_VERSION     2
#define GLX_EXTENSIONS  3

#define GLX_USE_GL                1
#define GLX_BUFFER_SIZE           2
#define GLX_LEVEL                 3
#define GLX_RGBA                  4
#define GLX_DOUBLEBUFFER          5
#define GLX_STEREO                6
#define GLX_AUX_BUFFERS           7
#define GLX_RED_SIZE              8
#define GLX_GREEN_SIZE            9
#define GLX_BLUE_SIZE            10
#define GLX_ALPHA_SIZE           11
#define GLX_DEPTH_SIZE           12
#define GLX_STENCIL_SIZE         13
#define GLX_ACCUM_RED_SIZE       14
#define GLX_ACCUM_GREEN_SIZE     15
#define GLX_ACCUM_BLUE_SIZE      16
#define GLX_ACCUM_ALPHA_SIZE     17
#define GLX_X_VISUAL_TYPE      0x22
#define GLX_TRANSPARENT_TYPE   0x23
#define GLX_VISUAL_ID        0x800B
#define GLX_TRUE_COLOR       0x8002
#define GLX_NONE             0x8000
#define GLX_DRAWABLE_TYPE    0x8010
#define GLX_RENDER_TYPE      0x8011
#define GLX_X_RENDERABLE     0x8012
#define GLX_FBCONFIG_ID      0x8013
#define GLX_RGBA_BIT         0x00000001
#define GLX_WINDOW_BIT       0x00000001
#define GLX_PIXMAP_BIT       0x00000002
#define GLX_PBUFFER_BIT      0x00000004
#define GLX_WIDTH            0x801D
#define GLX_HEIGHT           0x801E
#define GLX_PBUFFER_WIDTH    0x8041
#define GLX_PBUFFER_HEIGHT   0x8040
#define GLX_SCREEN           0x800C
#define GLX_Y_INVERTED_EXT   0x20B4
#define GLX_SAMPLE_BUFFERS  100000
#define GLX_SAMPLES         100001

#define GLX_STUB_EVENTS 17
#define GLX_STUB_ERRORS 13

Bool noGlxStubExtension = FALSE;
static RESTYPE pbuffer_type;

/* RT_PIXMAP owns the pixels; this second resource only identifies GLX pbuffers.
 * Both resources are freed on destruction or client disconnect. */
static int
delete_pbuffer_marker(void *value, XID id)
{
    (void)value;
    (void)id;
    return Success;
}

typedef struct {
    CARD8 reqType, glxCode;
    CARD16 length;
    CARD32 screen, fbconfig, pbuffer, numAttribs;
} xGLXCreatePbufferReq;

typedef struct {
    CARD8 reqType, glxCode;
    CARD16 length;
    CARD32 pbuffer;
} xGLXDestroyPbufferReq;

static const char k_vendor[] = "Mesa Project and SGI";
static const char k_version[] = "1.4";
static const char k_extensions[] =
    "GLX_ARB_create_context GLX_ARB_create_context_profile "
    "GLX_ARB_create_context_no_error GLX_ARB_fbconfig_float "
    "GLX_EXT_create_context_es2_profile GLX_EXT_fbconfig_packed_float "
    "GLX_MESA_query_renderer GLX_SGIX_fbconfig GLX_SGIX_pbuffer";

typedef struct {
    CARD8 reqType;
    CARD8 glxCode;
    CARD16 length;
} xGLXReq;

typedef struct {
    CARD8 reqType;
    CARD8 glxCode;
    CARD16 length;
    CARD32 majorVersion;
    CARD32 minorVersion;
} xGLXQueryVersionReq;

typedef struct {
    BYTE type;
    CARD8 unused;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD32 majorVersion;
    CARD32 minorVersion;
    CARD32 pad2;
    CARD32 pad3;
    CARD32 pad4;
    CARD32 pad5;
} xGLXQueryVersionReply;

typedef struct {
    CARD8 reqType;
    CARD8 glxCode;
    CARD16 length;
    CARD32 screen;
} xGLXGetVisualConfigsReq;

typedef struct {
    BYTE type;
    CARD8 unused;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD32 numVisuals;
    CARD32 numProps;
    CARD32 pad2;
    CARD32 pad3;
    CARD32 pad4;
    CARD32 pad5;
} xGLXGetVisualConfigsReply;

typedef struct {
    CARD8 reqType;
    CARD8 glxCode;
    CARD16 length;
    CARD32 screen;
} xGLXGetFBConfigsReq;

typedef struct {
    BYTE type;
    CARD8 unused;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD32 numFBConfigs;
    CARD32 numAttribs;
    CARD32 pad2;
    CARD32 pad3;
    CARD32 pad4;
    CARD32 pad5;
} xGLXGetFBConfigsReply;

typedef struct {
    CARD8 reqType;
    CARD8 glxCode;
    CARD16 length;
    CARD32 screen;
    CARD32 name;
} xGLXQueryServerStringReq;

typedef struct {
    BYTE type;
    CARD8 unused;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD32 pad1;
    CARD32 n;
    CARD32 pad2;
    CARD32 pad3;
    CARD32 pad4;
    CARD32 pad5;
} xGLXQueryServerStringReply;

typedef struct {
    BYTE type;
    CARD8 unused;
    CARD16 sequenceNumber;
    CARD32 length;
    BOOL isDirect;
    CARD8 pad1;
    CARD16 pad2;
    CARD32 pad3;
    CARD32 pad4;
    CARD32 pad5;
    CARD32 pad6;
    CARD32 pad7;
} xGLXIsDirectReply;

typedef struct {
    BYTE type;
    CARD8 unused;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD32 contextTag;
    CARD32 pad1;
    CARD32 pad2;
    CARD32 pad3;
    CARD32 pad4;
    CARD32 pad5;
} xGLXMakeCurrentReply;

typedef struct {
    CARD8 reqType;
    CARD8 glxCode;
    CARD16 length;
    CARD32 drawable;
} xGLXGetDrawableAttributesReq;

typedef struct {
    BYTE type;
    CARD8 unused;
    CARD16 sequenceNumber;
    CARD32 length;
    CARD32 numAttribs;
    CARD32 pad2;
    CARD32 pad3;
    CARD32 pad4;
    CARD32 pad5;
    CARD32 pad6;
} xGLXGetDrawableAttributesReply;

static VisualID
root_visual(void)
{
    if (screenInfo.numScreens < 1 || !screenInfo.screens[0])
        return 0;
    return screenInfo.screens[0]->rootVisual;
}

static int
visual_depth(VisualID id)
{
    ScreenPtr screen = screenInfo.screens[0];
    for (int i = 0; i < screen->numVisuals; i++)
        if (screen->visuals[i].vid == id)
            return screen->visuals[i].nplanes;
    return 0;
}

static int
glx_visuals(VisualID ids[2])
{
    ScreenPtr screen = screenInfo.screens[0];
    int n = 0;
    for (int depth = 24; depth <= 32; depth += 8)
        for (int i = 0; i < screen->numVisuals; i++)
            if (screen->visuals[i].class == TrueColor && screen->visuals[i].nplanes == depth) {
                ids[n++] = screen->visuals[i].vid;
                break;
            }
    return n;
}

static int
write_string_reply(ClientPtr client, const char *str)
{
    xGLXQueryServerStringReply reply;
    CARD32 n = (CARD32)strlen(str) + 1;
    CARD32 extra = (CARD32)pad_to_int32((int)n);
    char *buf;

    /* WriteToClient pads each call to 4 bytes. Send one extra block so the
     * client does not see leftover zeros as the next reply header.
     */
    buf = calloc(1, extra);
    if (!buf)
        return BadAlloc;
    memcpy(buf, str, n);

    reply = (xGLXQueryServerStringReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = extra / 4,
        .n = n,
    };
    if (client->swapped) {
        swaps(&reply.sequenceNumber);
        swapl(&reply.length);
        swapl(&reply.n);
    }
    WriteToClient(client, 32, &reply);
    WriteToClient(client, (int)extra, buf);
    free(buf);
    return Success;
}

static void
fill_visual_props(CARD32 *p, VisualID vis)
{
    /* Untagged GLXGetVisualConfigs layout (18 INT32s). */
    p[0] = vis;
    p[1] = TrueColor;
    p[2] = 1; /* rgba */
    p[3] = 8;
    p[4] = 8;
    p[5] = 8;
    p[6] = visual_depth(vis) == 32 ? 8 : 0;
    p[7] = 0;
    p[8] = 0;
    p[9] = 0;
    p[10] = 0;
    p[11] = 1; /* double buffer */
    p[12] = 0; /* stereo */
    p[13] = visual_depth(vis);
    p[14] = 24;
    p[15] = 8;
    p[16] = 0;
    p[17] = 0;
}

static void
fill_fbconfig_pairs(CARD32 *p, VisualID vis)
{
    struct {
        CARD32 tag;
        CARD32 value;
    } attrs[] = {
        { GLX_FBCONFIG_ID, vis },
        { GLX_VISUAL_ID, vis },
        { GLX_BUFFER_SIZE, visual_depth(vis) },
        { GLX_LEVEL, 0 },
        { GLX_DOUBLEBUFFER, 1 },
        { GLX_STEREO, 0 },
        { GLX_AUX_BUFFERS, 0 },
        { GLX_RED_SIZE, 8 },
        { GLX_GREEN_SIZE, 8 },
        { GLX_BLUE_SIZE, 8 },
        { GLX_ALPHA_SIZE, visual_depth(vis) == 32 ? 8 : 0 },
        { GLX_DEPTH_SIZE, 24 },
        { GLX_STENCIL_SIZE, 8 },
        { GLX_ACCUM_RED_SIZE, 0 },
        { GLX_ACCUM_GREEN_SIZE, 0 },
        { GLX_ACCUM_BLUE_SIZE, 0 },
        { GLX_ACCUM_ALPHA_SIZE, 0 },
        { GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR },
        { GLX_TRANSPARENT_TYPE, GLX_NONE },
        { GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT | GLX_PIXMAP_BIT | GLX_PBUFFER_BIT },
        { GLX_RENDER_TYPE, GLX_RGBA_BIT },
        { GLX_X_RENDERABLE, 1 },
        { GLX_USE_GL, 1 },
        { GLX_RGBA, 1 },
        { GLX_SAMPLE_BUFFERS, 0 },
        { GLX_SAMPLES, 0 },
    };
    memcpy(p, attrs, sizeof(attrs));
}

#define FBCONFIG_NATTRIBS 26

static int
ProcGLXQueryVersion(ClientPtr client)
{
    xGLXQueryVersionReply reply;

    REQUEST(xGLXQueryVersionReq);
    REQUEST_SIZE_MATCH(xGLXQueryVersionReq);
    (void)stuff;

    reply = (xGLXQueryVersionReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .majorVersion = 1,
        .minorVersion = 4,
    };
    if (client->swapped) {
        swaps(&reply.sequenceNumber);
        swapl(&reply.length);
        swapl(&reply.majorVersion);
        swapl(&reply.minorVersion);
    }
    WriteReplyToClient(client, sizeof(reply), &reply);
    return Success;
}

static int
ProcGLXQueryServerString(ClientPtr client)
{
    const char *str = k_extensions;

    REQUEST(xGLXQueryServerStringReq);
    REQUEST_SIZE_MATCH(xGLXQueryServerStringReq);

    if (stuff->name == GLX_VENDOR)
        str = k_vendor;
    else if (stuff->name == GLX_VERSION)
        str = k_version;
    return write_string_reply(client, str);
}

static int
ProcGLXQueryExtensionsString(ClientPtr client)
{
    REQUEST(xGLXReq);
    REQUEST_AT_LEAST_SIZE(xGLXReq);
    (void)stuff;
    return write_string_reply(client, k_extensions);
}

static int
ProcGLXGetVisualConfigs(ClientPtr client)
{
    xGLXGetVisualConfigsReply reply;
    CARD32 props[36];
    VisualID visuals[2];
    int count = glx_visuals(visuals);

    REQUEST(xGLXGetVisualConfigsReq);
    REQUEST_SIZE_MATCH(xGLXGetVisualConfigsReq);
    (void)stuff;

    for (int i = 0; i < count; i++) fill_visual_props(props + 18 * i, visuals[i]);
    reply = (xGLXGetVisualConfigsReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 18 * count,
        .numVisuals = count,
        .numProps = 18,
    };
    if (client->swapped) {
        swaps(&reply.sequenceNumber);
        swapl(&reply.length);
        swapl(&reply.numVisuals);
        swapl(&reply.numProps);
        for (int i = 0; i < 18 * count; i++)
            swapl(&props[i]);
    }
    WriteToClient(client, 32, &reply);
    WriteToClient(client, 18 * count * sizeof(CARD32), props);
    return Success;
}

static int
ProcGLXGetFBConfigs(ClientPtr client)
{
    xGLXGetFBConfigsReply reply;
    CARD32 pairs[FBCONFIG_NATTRIBS * 4];
    VisualID visuals[2];
    int count = glx_visuals(visuals);

    REQUEST(xGLXGetFBConfigsReq);
    REQUEST_SIZE_MATCH(xGLXGetFBConfigsReq);
    (void)stuff;

    for (int i = 0; i < count; i++)
        fill_fbconfig_pairs(pairs + FBCONFIG_NATTRIBS * 2 * i, visuals[i]);
    reply = (xGLXGetFBConfigsReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = FBCONFIG_NATTRIBS * 2 * count,
        .numFBConfigs = count,
        .numAttribs = FBCONFIG_NATTRIBS,
    };
    if (client->swapped) {
        swaps(&reply.sequenceNumber);
        swapl(&reply.length);
        swapl(&reply.numFBConfigs);
        swapl(&reply.numAttribs);
        for (int i = 0; i < FBCONFIG_NATTRIBS * 2 * count; i++)
            swapl(&pairs[i]);
    }
    WriteToClient(client, 32, &reply);
    WriteToClient(client, FBCONFIG_NATTRIBS * 2 * count * sizeof(CARD32), pairs);
    return Success;
}

static int
ProcGLXIsDirect(ClientPtr client)
{
    xGLXIsDirectReply reply = {
        .type = X_Reply,
        .isDirect = TRUE,
        .sequenceNumber = client->sequence,
        .length = 0,
    };

    REQUEST(xGLXReq);
    REQUEST_AT_LEAST_SIZE(xGLXReq);
    (void)stuff;
    if (client->swapped)
        swaps(&reply.sequenceNumber);
    WriteReplyToClient(client, sizeof(reply), &reply);
    return Success;
}

static int
ProcGLXMakeCurrent(ClientPtr client)
{
    xGLXMakeCurrentReply reply = {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .contextTag = 1,
    };

    REQUEST(xGLXReq);
    REQUEST_AT_LEAST_SIZE(xGLXReq);
    (void)stuff;
    if (client->swapped) {
        swaps(&reply.sequenceNumber);
        swapl(&reply.contextTag);
    }
    WriteToClient(client, 32, &reply);
    return Success;
}

static int
ProcGLXCreatePbuffer(ClientPtr client)
{
    REQUEST(xGLXCreatePbufferReq);
    REQUEST_AT_LEAST_SIZE(xGLXCreatePbufferReq);
    if (client->swapped) {
        swapl(&stuff->screen);
        swapl(&stuff->fbconfig);
        swapl(&stuff->pbuffer);
        swapl(&stuff->numAttribs);
    }
    if (stuff->numAttribs > (client->req_len * 4 - sizeof(*stuff)) / 8)
        return BadLength;
    REQUEST_FIXED_SIZE(xGLXCreatePbufferReq, stuff->numAttribs * 8);
    if (stuff->screen >= screenInfo.numScreens)
        return BadValue;
    int depth = visual_depth(stuff->fbconfig);
    if (depth != 24 && depth != 32)
        return BadValue;
    LEGAL_NEW_RESOURCE(stuff->pbuffer, client);
    CARD32 width = 0, height = 0;
    CARD32 *attrs = (CARD32 *)(stuff + 1);
    for (CARD32 i = 0; i < stuff->numAttribs; i++) {
        CARD32 key = attrs[2 * i], value = attrs[2 * i + 1];
        if (client->swapped) { swapl(&key); swapl(&value); }
        if (key == GLX_PBUFFER_WIDTH) width = value;
        if (key == GLX_PBUFFER_HEIGHT) height = value;
    }
    if (!width || !height || width > 32767 || height > 32767)
        return BadValue;
    ScreenPtr screen = screenInfo.screens[stuff->screen];
    PixmapPtr pixmap = screen->CreatePixmap(screen, width, height,
                                           depth, 0);
    if (!pixmap)
        return BadAlloc;
    int error = XaceHook(XACE_RESOURCE_ACCESS, client, stuff->pbuffer, RT_PIXMAP,
                        pixmap, RT_NONE, NULL, DixCreateAccess);
    if (error != Success) {
        screen->DestroyPixmap(pixmap);
        return error;
    }
    pixmap->drawable.id = stuff->pbuffer;
    if (!AddResource(stuff->pbuffer, RT_PIXMAP, pixmap))
        return BadAlloc;
    if (!AddResource(stuff->pbuffer, pbuffer_type, pixmap)) {
        FreeResource(stuff->pbuffer, RT_NONE);
        return BadAlloc;
    }
    return Success;
}

static int
ProcGLXDestroyPbuffer(ClientPtr client)
{
    REQUEST(xGLXDestroyPbufferReq);
    REQUEST_SIZE_MATCH(xGLXDestroyPbufferReq);
    if (client->swapped) swapl(&stuff->pbuffer);
    void *marker;
    int error = dixLookupResourceByType(&marker, stuff->pbuffer, pbuffer_type,
                                        client, DixDestroyAccess);
    if (error != Success) return error;
    FreeResource(stuff->pbuffer, RT_NONE);
    return Success;
}

static int
ProcGLXGetDrawableAttributes(ClientPtr client)
{
    xGLXGetDrawableAttributesReply reply;
    CARD32 attribs[16];
    DrawablePtr drawable = NULL;
    void *marker;
    CARD32 drawable_type = GLX_WINDOW_BIT;
    CARD32 num = 0;
    CARD32 w = 1, h = 1, screen = 0;
    VisualID visual = root_visual();

    REQUEST(xGLXGetDrawableAttributesReq);
    REQUEST_AT_LEAST_SIZE(xGLXGetDrawableAttributesReq);

    if (client->swapped) swapl(&stuff->drawable);
    if (dixLookupDrawable(&drawable, stuff->drawable, client, M_ANY, DixGetAttrAccess)
            == Success && drawable) {
        w = drawable->width;
        h = drawable->height;
        screen = (CARD32)drawable->pScreen->myNum;
        VisualID ids[2];
        int count = glx_visuals(ids);
        for (int i = 0; i < count; i++)
            if (visual_depth(ids[i]) == drawable->depth) visual = ids[i];
        if (drawable->type == DRAWABLE_PIXMAP)
            drawable_type = GLX_PIXMAP_BIT;
    }
    if (dixLookupResourceByType(&marker, stuff->drawable, pbuffer_type,
                               client, DixGetAttrAccess) == Success)
        drawable_type = GLX_PBUFFER_BIT;

#define ATTRIB(a, v) do { \
    attribs[num * 2] = (a); \
    attribs[num * 2 + 1] = (v); \
    num++; \
} while (0)
    ATTRIB(GLX_Y_INVERTED_EXT, 0);
    ATTRIB(GLX_WIDTH, w);
    ATTRIB(GLX_HEIGHT, h);
    ATTRIB(GLX_SCREEN, screen);
    ATTRIB(GLX_DRAWABLE_TYPE, drawable_type);
    ATTRIB(GLX_FBCONFIG_ID, visual);
#undef ATTRIB

    reply = (xGLXGetDrawableAttributesReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = num * 2,
        .numAttribs = num,
    };
    if (client->swapped) {
        swaps(&reply.sequenceNumber);
        swapl(&reply.length);
        swapl(&reply.numAttribs);
        for (CARD32 i = 0; i < num * 2; i++)
            swapl(&attribs[i]);
    }
    WriteToClient(client, 32, &reply);
    WriteToClient(client, (int)(num * 2 * sizeof(CARD32)), attribs);
    return Success;
}

static int
ProcGLXDispatch(ClientPtr client)
{
    REQUEST(xGLXReq);

    switch (stuff->glxCode) {
    case X_GLXQueryVersion:
        return ProcGLXQueryVersion(client);
    case X_GLXQueryServerString:
        return ProcGLXQueryServerString(client);
    case X_GLXQueryExtensionsString:
        return ProcGLXQueryExtensionsString(client);
    case X_GLXGetVisualConfigs:
        return ProcGLXGetVisualConfigs(client);
    case X_GLXGetFBConfigs:
        return ProcGLXGetFBConfigs(client);
    case X_GLXIsDirect:
        return ProcGLXIsDirect(client);
    case X_GLXMakeCurrent:
    case X_GLXMakeContextCurrent:
        return ProcGLXMakeCurrent(client);
    case X_GLXGetDrawableAttributes:
        return ProcGLXGetDrawableAttributes(client);
    case X_GLXCreatePbuffer:
        return ProcGLXCreatePbuffer(client);
    case X_GLXDestroyPbuffer:
        return ProcGLXDestroyPbuffer(client);
    case X_GLXClientInfo:
    case X_GLXSetClientInfoARB:
    case X_GLXSetClientInfo2ARB:
    case X_GLXCreateContext:
    case X_GLXCreateNewContext:
    case X_GLXCreateContextAttribsARB:
    case X_GLXDestroyContext:
    case X_GLXCreateWindow:
    case X_GLXDestroyWindow:
    case X_GLXChangeDrawableAttributes:
    case X_GLXSwapBuffers:
    case X_GLXRender:
    case X_GLXRenderLarge:
        return Success;
    default:
        ErrorF("GLX stub: unhandled request %u\n", stuff->glxCode);
        return Success;
    }
}

static int _X_COLD
SProcGLXDispatch(ClientPtr client)
{
    REQUEST(xGLXReq);
    swaps(&stuff->length);
    return ProcGLXDispatch(client);
}

void
xwlGlxStubInit(void)
{
    pbuffer_type = CreateNewResourceType(delete_pbuffer_marker, "GLX pbuffer");
    if (!pbuffer_type) return;
    AddExtension(GLX_EXTENSION_NAME,
                 GLX_STUB_EVENTS, GLX_STUB_ERRORS,
                 ProcGLXDispatch, SProcGLXDispatch,
                 NULL, StandardMinorOpcode);
}
