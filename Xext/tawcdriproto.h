/* SPDX-License-Identifier: MIT
 * TAWC-DRI 0.3: native handle FDs/ints over X11, forwarded to android_wlegl.
 * The compositor imports the AHB. Xwayland relays resize/release XGE events.
 */

#ifndef _TAWCDRIPROTO_H_
#define _TAWCDRIPROTO_H_

#include <X11/X.h>
#include <X11/Xmd.h>
#include <X11/Xproto.h>

#define TAWCDRI_NAME "TAWC-DRI"
#define TAWCDRI_MAJOR 0
#define TAWCDRI_MINOR 3

#define X_TAWCDRIQueryVersion  0
#define X_TAWCDRIPresentBuffer 1
#define X_TAWCDRISelectInput   2

/* XGE generic events (response_type 35), not core events. */
#define TAWCDRINumberErrors 0
#define TAWCDRINumberEvents 0

#define TAWCDRIEventConfigureNotify 0
#define TAWCDRIEventBufferRelease   1

#define TAWCDRIConfigureNotifyMask (1 << 0)
#define TAWCDRIBufferReleaseMask   (1 << 1)
#define TAWCDRIAllEventMasks \
    (TAWCDRIConfigureNotifyMask | TAWCDRIBufferReleaseMask)

typedef struct {
    CARD8  reqType;
    CARD8  tawcReqType;
    CARD16 length B16;
    CARD32 majorVersion B32;
    CARD32 minorVersion B32;
} xTAWCDRIQueryVersionReq;
#define sz_xTAWCDRIQueryVersionReq 12

typedef struct {
    BYTE   type;
    BYTE   pad1;
    CARD16 sequenceNumber B16;
    CARD32 length B32;
    CARD32 majorVersion B32;
    CARD32 minorVersion B32;
    CARD32 pad2;
    CARD32 pad3;
    CARD32 pad4;
    CARD32 pad5;
} xTAWCDRIQueryVersionReply;
#define sz_xTAWCDRIQueryVersionReply 32

/* PresentBuffer ships an entire AHB-backed pixmap: the native_handle
 * payload (numFds + numInts and the inline ints) plus the AHB descriptor
 * (width, height, stride, format, usage). FDs follow out-of-band via
 * X11 FD passing; the server reads them with ReadFdFromClient(). The
 * `numInts` inline u32s start immediately after this struct, padded
 * up to 4-byte alignment by libxtrans. usage is split into lo/hi u32
 * because Xproto wire types are 32-bit.
 *
 * The buffer is bound to `window`'s X11 toplevel and committed on its
 * wl_surface; the request is void (no reply, errors arrive
 * asynchronously).
 *
 * v0.3: `serial` is a client-chosen cookie echoed back in the
 * TAWCDRIBufferRelease event when the compositor releases the
 * wl_buffer this present created. */
typedef struct {
    CARD8  reqType;
    CARD8  tawcReqType;
    CARD16 length B16;
    CARD32 window B32;
    CARD16 numFds B16;
    CARD16 numInts B16;
    CARD32 width B32;
    CARD32 height B32;
    CARD32 stride B32;
    CARD32 format B32;
    CARD32 usageLo B32;
    CARD32 usageHi B32;
    CARD32 serial B32;
} xTAWCDRIPresentBufferReq;
#define sz_xTAWCDRIPresentBufferReq 40


/* v0.3: select TAWC-DRI events on `window`. `eid` is a client-allocated
 * XID (xcb_generate_id) used as the routing key for libxcb's special
 * event queues — same role as the Present extension's event id. An
 * eventMask of 0 deletes the selection. Selecting ConfigureNotify
 * immediately emits one event carrying the window's current size, so
 * a resize that landed before the selection is never missed. */
typedef struct {
    CARD8  reqType;
    CARD8  tawcReqType;
    CARD16 length B16;
    CARD32 eid B32;
    CARD32 window B32;
    CARD32 eventMask B32;
} xTAWCDRISelectInputReq;
#define sz_xTAWCDRISelectInputReq 16

/* Events are XGE generic events. Field layout deliberately matches the
 * Present extension's events — evtype at bytes 8-9, eid at bytes 12-15 —
 * because libxcb's special-event matching (xcb_in.c, xcb_ge_special_event_t)
 * reads the routing eid at that fixed offset. */

typedef struct {
    CARD8  type;                /* GenericEvent */
    CARD8  extension;           /* TAWC-DRI major opcode */
    CARD16 sequenceNumber B16;
    CARD32 length B32;          /* 0 — no payload past 32 bytes */
    CARD16 evtype B16;          /* TAWCDRIEventConfigureNotify */
    CARD16 pad1 B16;
    CARD32 eid B32;
    CARD32 window B32;
    CARD32 width B32;
    CARD32 height B32;
    CARD32 pad2 B32;
} xTAWCDRIConfigureNotify;
#define sz_xTAWCDRIConfigureNotify 32

typedef struct {
    CARD8  type;                /* GenericEvent */
    CARD8  extension;           /* TAWC-DRI major opcode */
    CARD16 sequenceNumber B16;
    CARD32 length B32;          /* 0 */
    CARD16 evtype B16;          /* TAWCDRIEventBufferRelease */
    CARD16 pad1 B16;
    CARD32 eid B32;
    CARD32 window B32;
    CARD32 serial B32;          /* echoed from PresentBuffer */
    CARD32 pad2 B32;
    CARD32 pad3 B32;
} xTAWCDRIBufferRelease;
#define sz_xTAWCDRIBufferRelease 32

#endif /* _TAWCDRIPROTO_H_ */
