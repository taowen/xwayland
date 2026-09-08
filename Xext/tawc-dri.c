/* SPDX-License-Identifier: MIT
 * TAWC-DRI 0.3: forward native handles from X11 clients to android_wlegl.
 * The compositor imports buffers and signals release. XGE events report
 * resize and release to each client's selected XCB event queue.
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include <X11/X.h>
#include <X11/Xproto.h>

#include "misc.h"
#include "os.h"
#include "dixstruct.h"
#include "resource.h"
#include "extnsionst.h"
#include "extinit.h"
#include "windowstr.h"
#include "scrnintstr.h"
#include "Xext/geext.h"

#include "Xext/tawcdriproto.h"

/* Forward decl — pulling in the Xwayland-private xwayland-tawc.h here
 * would force an include path the rest of Xext/ doesn't have. The
 * function is implemented in hw/xwayland/xwayland-tawc.c and resolved
 * at link time via Xwayland's executable target. */
extern int xwl_tawc_present_native_handle(WindowPtr window,
                                          int *fds, int num_fds,
                                          const int32_t *ints, int num_ints,
                                          int width, int height, int stride,
                                          int format, uint64_t usage,
                                          uint32_t client_mask,
                                          uint32_t serial);

/* ── Event selections ──
 *
 * One record per SelectInput, on a single global list (the population
 * is a handful of GL clients, not worth per-window privates). Cleanup
 * is resource-driven, like Present's event contexts:
 *   - the eid is AddResource'd under the requesting client, so client
 *     disconnect frees the record;
 *   - a marker resource under the window's XID frees every record for
 *     that window when the window is destroyed.
 */
typedef struct tawc_dri_event {
    struct tawc_dri_event *next;
    ClientPtr client;
    WindowPtr window;
    XID eid;
    CARD32 mask;
} tawc_dri_event_rec;

static tawc_dri_event_rec *tawc_dri_event_list;
static RESTYPE tawc_dri_event_type;  /* eid resources */
static RESTYPE tawc_dri_window_type; /* per-window cleanup marker */
static int tawc_dri_request;         /* major opcode, for XGE events */

static int
tawc_dri_free_event(void *data, XID id)
{
    tawc_dri_event_rec *event = data;
    tawc_dri_event_rec **prev, *cur;

    for (prev = &tawc_dri_event_list; (cur = *prev); prev = &cur->next) {
        if (cur == event) {
            *prev = event->next;
            break;
        }
    }
    free(event);
    return Success;
}

static int
tawc_dri_free_window(void *data, XID id)
{
    WindowPtr window = data;
    tawc_dri_event_rec *event, *next;

    for (event = tawc_dri_event_list; event; event = next) {
        next = event->next;
        if (event->window == window)
            FreeResource(event->eid, RT_NONE);
    }
    return Success;
}

static void
tawc_dri_send_configure_notify_to(tawc_dri_event_rec *event,
                                  int width, int height)
{
    xTAWCDRIConfigureNotify cn = {
        .type = GenericEvent,
        .extension = tawc_dri_request,
        .length = 0,
        .evtype = TAWCDRIEventConfigureNotify,
        .eid = event->eid,
        .window = event->window->drawable.id,
        .width = width,
        .height = height,
    };
    WriteEventsToClient(event->client, 1, (xEvent *) &cn);
}

/* ConfigNotify screen hook: dix calls this from ConfigureWindow before
 * the new geometry is applied, so drawable.width/height still hold the
 * old size — comparing tells us whether the size actually changes
 * (position-only moves shouldn't wake GL clients). A size check at
 * PresentBuffer time would not be enough on its own: a low-fps client
 * would render a full wrong-sized frame before hearing about it. */
static ConfigNotifyProcPtr tawc_dri_wrapped_config_notify[MAXSCREENS];

static int
tawc_dri_config_notify(WindowPtr window,
                       int x, int y, int w, int h, int bw,
                       WindowPtr sibling)
{
    ScreenPtr screen = window->drawable.pScreen;
    int ret = Success;

    /* unwrap → call → rewrap, like Present's hooks. Xwayland's own
     * xwl_config_notify reinstalls itself outermost on every call, so
     * a wrapper that doesn't rewrap gets silently dropped from the
     * chain after the first ConfigureWindow. */
    screen->ConfigNotify = tawc_dri_wrapped_config_notify[screen->myNum];
    if (screen->ConfigNotify)
        ret = screen->ConfigNotify(window, x, y, w, h, bw, sibling);
    tawc_dri_wrapped_config_notify[screen->myNum] = screen->ConfigNotify;
    screen->ConfigNotify = tawc_dri_config_notify;

    /* Emit only after the wrapped chain accepted the configure — a
     * failure there aborts the resize, and a client that already
     * reallocated to the phantom size would render wrong-sized buffers
     * (the exact black-window class this event exists to fix). The
     * drawable still holds the old size here (dix applies the new
     * geometry after ConfigNotify returns), so the change check works. */
    if (ret == Success &&
        (w != window->drawable.width || h != window->drawable.height)) {
        tawc_dri_event_rec *event;

        for (event = tawc_dri_event_list; event; event = event->next) {
            if (event->window == window &&
                (event->mask & TAWCDRIConfigureNotifyMask))
                tawc_dri_send_configure_notify_to(event, w, h);
        }
    }
    return ret;
}

/* Compositor released a wl_buffer created by a v0.3 PresentBuffer.
 * Called from hw/xwayland/xwayland-tawc.c's wl_buffer.release
 * listener. Serials are per-presenting-client, so the release goes
 * only to that client's selection — matched by clientAsMask + window
 * XID rather than pointers: if the client or window is gone, its
 * selections were freed with it and nothing is sent; recycled
 * identities at worst deliver a stale serial the client ignores. */
void
tawc_dri_send_buffer_release(uint32_t window_id, uint32_t client_mask,
                             uint32_t serial)
{
    tawc_dri_event_rec *event;

    for (event = tawc_dri_event_list; event; event = event->next) {
        if (event->window->drawable.id == window_id &&
            (uint32_t)event->client->clientAsMask == client_mask &&
            (event->mask & TAWCDRIBufferReleaseMask)) {
            xTAWCDRIBufferRelease br = {
                .type = GenericEvent,
                .extension = tawc_dri_request,
                .length = 0,
                .evtype = TAWCDRIEventBufferRelease,
                .eid = event->eid,
                .window = window_id,
                .serial = serial,
            };
            WriteEventsToClient(event->client, 1, (xEvent *) &br);
        }
    }
}

static void _X_COLD
tawc_dri_event_swap(xGenericEvent *from, xGenericEvent *to)
{
    *to = *from;
    swaps(&to->sequenceNumber);
    swapl(&to->length);
    swaps(&to->evtype);
    switch (from->evtype) {
    case TAWCDRIEventConfigureNotify: {
        xTAWCDRIConfigureNotify *cn = (xTAWCDRIConfigureNotify *) to;
        swapl(&cn->eid);
        swapl(&cn->window);
        swapl(&cn->width);
        swapl(&cn->height);
        break;
    }
    case TAWCDRIEventBufferRelease: {
        xTAWCDRIBufferRelease *br = (xTAWCDRIBufferRelease *) to;
        swapl(&br->eid);
        swapl(&br->window);
        swapl(&br->serial);
        break;
    }
    }
}

static int
ProcTAWCDRIQueryVersion(ClientPtr client)
{
    REQUEST(xTAWCDRIQueryVersionReq);
    xTAWCDRIQueryVersionReply rep;

    REQUEST_SIZE_MATCH(xTAWCDRIQueryVersionReq);

    rep = (xTAWCDRIQueryVersionReply) {
        .type = X_Reply,
        .sequenceNumber = client->sequence,
        .length = 0,
        .majorVersion = TAWCDRI_MAJOR,
        .minorVersion = TAWCDRI_MINOR,
    };
    if (client->swapped) {
        swaps(&rep.sequenceNumber);
        swapl(&rep.length);
        swapl(&rep.majorVersion);
        swapl(&rep.minorVersion);
    }
    WriteToClient(client, sizeof(rep), &rep);
    return Success;
}

static int
ProcTAWCDRIPresentBuffer(ClientPtr client)
{
    REQUEST(xTAWCDRIPresentBufferReq);
    WindowPtr window;
    int *fds = NULL;
    int32_t *ints_buf = NULL;
    int rc;
    uint64_t usage;
    int num_fds, num_ints;
    size_t total, ints_sz, ints_offset;
    uint32_t serial;

    if (client->req_len < (sz_xTAWCDRIPresentBufferReq >> 2))
        return BadLength;

    num_fds  = stuff->numFds;
    num_ints = stuff->numInts;

    /* Reasonable caps so a malformed client can't OOM us. AOSP gralloc4
     * private_handle_t maxes at numFds=2 (gralloc fd + sync fd) and
     * numInts ~ 32 in practice; 16/256 is a generous upper bound. */
    if (num_fds < 0 || num_fds > 16 || num_ints < 0 || num_ints > 256) {
        client->errorValue = num_fds;
        return BadValue;
    }

    total   = (size_t)stuff->length * 4;
    ints_sz = (size_t)num_ints * sizeof(int32_t);
    if (total != sz_xTAWCDRIPresentBufferReq + ints_sz)
        return BadLength;
    ints_offset = sz_xTAWCDRIPresentBufferReq;
    serial = stuff->serial;

    /* DIX requires every dispatch that pulls fds to declare the count up
     * front; otherwise libxtrans drops the SCM_RIGHTS-attached fds and
     * ReadFdFromClient returns -1. Same shape as DRI3's
     * proc_dri3_pixmap_from_buffers. */
    SetReqFds(client, num_fds);

    rc = dixLookupWindow(&window, stuff->window, client, DixWriteAccess);
    if (rc != Success)
        return rc;

    /* Inline ints sit immediately after the request header. The X
     * server's request buffer is byte-swapped on the wire-side if
     * client->swapped, so for a swapped client we have to swap each
     * int here. The values are opaque gralloc bookkeeping (BPP, layer
     * count, vendor flags) — the client's libnativewindow generates
     * them on the same architecture as the server in our setup, so
     * cross-endian is paranoia rather than a real path. */
    if (num_ints > 0) {
        const int32_t *raw =
            (const int32_t *)((const char *)stuff + ints_offset);
        ints_buf = malloc(num_ints * sizeof(int32_t));
        if (!ints_buf)
            return BadAlloc;
        for (int i = 0; i < num_ints; i++) {
            int32_t v = raw[i];
            if (client->swapped) {
                CARD32 tmp = (CARD32)v;
                swapl(&tmp);
                v = (int32_t)tmp;
            }
            ints_buf[i] = v;
        }
    }

    if (num_fds > 0) {
        fds = malloc(num_fds * sizeof(int));
        if (!fds) {
            free(ints_buf);
            return BadAlloc;
        }
        for (int i = 0; i < num_fds; i++) {
            fds[i] = ReadFdFromClient(client);
            if (fds[i] < 0) {
                while (--i >= 0) close(fds[i]);
                free(fds);
                free(ints_buf);
                return BadValue;
            }
        }
    }

    usage = ((uint64_t)stuff->usageHi << 32) | (uint64_t)stuff->usageLo;

    rc = xwl_tawc_present_native_handle(window, fds, num_fds,
                                        ints_buf, num_ints,
                                        (int)stuff->width,
                                        (int)stuff->height,
                                        (int)stuff->stride,
                                        (int)stuff->format,
                                        usage,
                                        (uint32_t)client->clientAsMask,
                                        serial);
    /* xwl_tawc_present_native_handle owns + closes the fds on both
     * success (duplicated by Wayland) and failure (closed during cleanup). */
    free(fds);
    free(ints_buf);
    return rc;
}

static int
ProcTAWCDRISelectInput(ClientPtr client)
{
    REQUEST(xTAWCDRISelectInputReq);
    WindowPtr window;
    tawc_dri_event_rec *event;
    int rc;

    REQUEST_SIZE_MATCH(xTAWCDRISelectInputReq);

    if (stuff->eventMask & ~TAWCDRIAllEventMasks) {
        client->errorValue = stuff->eventMask;
        return BadValue;
    }

    rc = dixLookupWindow(&window, stuff->window, client, DixGetAttrAccess);
    if (rc != Success)
        return rc;

    /* Modifying an existing selection? (Same shape as Present.) */
    rc = dixLookupResourceByType((void **) &event, stuff->eid,
                                 tawc_dri_event_type, client,
                                 DixWriteAccess);
    if (rc == Success) {
        if (event->window != window || event->client != client)
            return BadMatch;
        if (stuff->eventMask) {
            event->mask = stuff->eventMask;
            if (stuff->eventMask & TAWCDRIConfigureNotifyMask)
                tawc_dri_send_configure_notify_to(event,
                                                  window->drawable.width,
                                                  window->drawable.height);
        }
        else
            FreeResource(stuff->eid, RT_NONE);
        return Success;
    }
    if (rc != BadValue)
        return rc;

    if (stuff->eventMask == 0)
        return Success;

    LEGAL_NEW_RESOURCE(stuff->eid, client);

    event = calloc(1, sizeof(*event));
    if (!event)
        return BadAlloc;
    event->client = client;
    event->window = window;
    event->eid = stuff->eid;
    event->mask = stuff->eventMask;
    event->next = tawc_dri_event_list;
    tawc_dri_event_list = event;

    if (!AddResource(event->eid, tawc_dri_event_type, (void *) event))
        return BadAlloc;

    /* First selection on this window: attach the cleanup marker so
     * window destruction frees the selections. The marker shares the
     * window's XID; dix frees every resource under that XID when the
     * window dies. */
    {
        void *marker;
        rc = dixLookupResourceByType(&marker, window->drawable.id,
                                     tawc_dri_window_type, serverClient,
                                     DixReadAccess);
        if (rc != Success &&
            !AddResource(window->drawable.id, tawc_dri_window_type,
                         (void *) window)) {
            /* A failed AddResource already ran tawc_dri_free_window,
             * which freed `event` via its eid resource — use the
             * request's copy of the XID (a second FreeResource on an
             * already-freed XID is a harmless no-op). */
            FreeResource(stuff->eid, RT_NONE);
            return BadAlloc;
        }
    }

    /* Deliver the window's current size right away: a WM resize that
     * landed between window creation and this SelectInput would
     * otherwise be missed forever (this exact race is how eglx11-test
     * accidentally survived the black-window bug while es2gears_x11
     * didn't). */
    if (stuff->eventMask & TAWCDRIConfigureNotifyMask)
        tawc_dri_send_configure_notify_to(event,
                                          window->drawable.width,
                                          window->drawable.height);

    return Success;
}

static int
ProcTAWCDRIDispatch(ClientPtr client)
{
    REQUEST(xReq);
    switch (stuff->data) {
    case X_TAWCDRIQueryVersion:
        return ProcTAWCDRIQueryVersion(client);
    case X_TAWCDRIPresentBuffer:
        return ProcTAWCDRIPresentBuffer(client);
    case X_TAWCDRISelectInput:
        return ProcTAWCDRISelectInput(client);
    default:
        return BadRequest;
    }
}

static int _X_COLD
SProcTAWCDRIQueryVersion(ClientPtr client)
{
    REQUEST(xTAWCDRIQueryVersionReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xTAWCDRIQueryVersionReq);
    swapl(&stuff->majorVersion);
    swapl(&stuff->minorVersion);
    return ProcTAWCDRIQueryVersion(client);
}

static int _X_COLD
SProcTAWCDRIPresentBuffer(ClientPtr client)
{
    REQUEST(xTAWCDRIPresentBufferReq);
    swaps(&stuff->length);
    if (client->req_len < (sz_xTAWCDRIPresentBufferReq >> 2))
        return BadLength;
    swapl(&stuff->window);
    swaps(&stuff->numFds);
    swaps(&stuff->numInts);
    swapl(&stuff->width);
    swapl(&stuff->height);
    swapl(&stuff->stride);
    swapl(&stuff->format);
    swapl(&stuff->usageLo);
    swapl(&stuff->usageHi);
    swapl(&stuff->serial);
    return ProcTAWCDRIPresentBuffer(client);
}

static int _X_COLD
SProcTAWCDRISelectInput(ClientPtr client)
{
    REQUEST(xTAWCDRISelectInputReq);
    swaps(&stuff->length);
    REQUEST_SIZE_MATCH(xTAWCDRISelectInputReq);
    swapl(&stuff->eid);
    swapl(&stuff->window);
    swapl(&stuff->eventMask);
    return ProcTAWCDRISelectInput(client);
}

static int _X_COLD
SProcTAWCDRIDispatch(ClientPtr client)
{
    REQUEST(xReq);
    switch (stuff->data) {
    case X_TAWCDRIQueryVersion:
        return SProcTAWCDRIQueryVersion(client);
    case X_TAWCDRIPresentBuffer:
        return SProcTAWCDRIPresentBuffer(client);
    case X_TAWCDRISelectInput:
        return SProcTAWCDRISelectInput(client);
    default:
        return BadRequest;
    }
}

void
tawc_dri_extension_init(void)
{
    ExtensionEntry *extEntry;
    int i;

    /* Per-generation state: resources were freed by the reset, but the
     * statics survive. */
    tawc_dri_event_list = NULL;

    tawc_dri_event_type =
        CreateNewResourceType(tawc_dri_free_event, "TAWCDRIEvent");
    tawc_dri_window_type =
        CreateNewResourceType(tawc_dri_free_window, "TAWCDRIWindow");
    if (!tawc_dri_event_type || !tawc_dri_window_type)
        FatalError("TAWC-DRI: failed to create resource types\n");

    extEntry = AddExtension(TAWCDRI_NAME,
                            TAWCDRINumberEvents,
                            TAWCDRINumberErrors,
                            ProcTAWCDRIDispatch,
                            SProcTAWCDRIDispatch,
                            NULL,
                            StandardMinorOpcode);
    if (!extEntry)
        FatalError("TAWC-DRI: AddExtension failed\n");
    tawc_dri_request = extEntry->base;
    GERegisterExtension(tawc_dri_request, tawc_dri_event_swap);

    /* Extensions initialize after InitOutput, so the screens exist and
     * we can hook ConfigNotify to hear about window resizes. */
    for (i = 0; i < screenInfo.numScreens; i++) {
        ScreenPtr screen = screenInfo.screens[i];
        tawc_dri_wrapped_config_notify[i] = screen->ConfigNotify;
        screen->ConfigNotify = tawc_dri_config_notify;
    }
}
