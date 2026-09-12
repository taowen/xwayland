/* SPDX-License-Identifier: MIT
 * TAWC-DRI 0.4: native handle FDs/ints over X11, forwarded to android_wlegl.
 * The compositor imports the AHB. Xwayland relays resize/release XGE events.
 */

#ifndef _TAWCDRIPROTO_H_
#define _TAWCDRIPROTO_H_

#include <X11/X.h>
#include <X11/Xmd.h>
#include <X11/Xproto.h>
#include <arlinux/tawc-dri.h>

#define TAWCDRI_NAME TAWC_DRI_NAME
#define TAWCDRI_MAJOR TAWC_DRI_MAJOR
#define TAWCDRI_MINOR TAWC_DRI_MINOR

#define X_TAWCDRIQueryVersion X_TAWCDRI_QueryVersion
#define X_TAWCDRIPresentBuffer X_TAWCDRI_PresentBuffer
#define X_TAWCDRISelectInput X_TAWCDRI_SelectInput
#define X_TAWCDRIPresentBuffer2 X_TAWCDRI_PresentBuffer2

/* XGE generic events (response_type 35), not core events. */
#define TAWCDRINumberErrors 0
#define TAWCDRINumberEvents 0

#define TAWCDRIEventConfigureNotify TAWC_DRI_EVENT_CONFIGURE_NOTIFY
#define TAWCDRIEventBufferRelease TAWC_DRI_EVENT_BUFFER_RELEASE

#define TAWCDRIConfigureNotifyMask TAWC_DRI_EVENT_MASK_CONFIGURE_NOTIFY
#define TAWCDRIBufferReleaseMask TAWC_DRI_EVENT_MASK_BUFFER_RELEASE
#define TAWCDRIAllEventMasks \
    (TAWCDRIConfigureNotifyMask | TAWCDRIBufferReleaseMask)

typedef tawc_dri_query_version_req xTAWCDRIQueryVersionReq;
#define sz_xTAWCDRIQueryVersionReq 12
typedef tawc_dri_query_version_reply xTAWCDRIQueryVersionReply;
#define sz_xTAWCDRIQueryVersionReply 32
typedef tawc_dri_present_buffer_req xTAWCDRIPresentBufferReq;
#define sz_xTAWCDRIPresentBufferReq 40
typedef tawc_dri_present_buffer2_req xTAWCDRIPresentBuffer2Req;
#define sz_xTAWCDRIPresentBuffer2Req 44
typedef tawc_dri_select_input_req xTAWCDRISelectInputReq;
#define sz_xTAWCDRISelectInputReq 16
typedef tawc_dri_configure_notify_event xTAWCDRIConfigureNotify;
#define sz_xTAWCDRIConfigureNotify 32
typedef tawc_dri_buffer_release_event xTAWCDRIBufferRelease;
#define sz_xTAWCDRIBufferRelease 32

#endif
