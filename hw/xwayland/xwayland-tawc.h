/* SPDX-License-Identifier: MIT */
#ifndef XWAYLAND_TAWC_H
#define XWAYLAND_TAWC_H
#include <stdint.h>
#include "window.h"
#include "pixmap.h"
struct xwl_screen;
struct wl_buffer;
Bool xwl_tawc_init(struct xwl_screen *screen);
void xwl_tawc_wrap_close(ScreenPtr screen);
struct wl_buffer *xwl_tawc_pixmap_get_wl_buffer(PixmapPtr pixmap);
int xwl_tawc_present_native_handle(WindowPtr window, int *fds, int num_fds,
    const int32_t *ints, int num_ints, int width, int height, int stride,
    int format, uint64_t usage, uint32_t client_mask, uint32_t serial, uint32_t flags);
#endif
