/* SPDX-License-Identifier: MIT */
#ifndef XWAYLAND_TAWC_H
#define XWAYLAND_TAWC_H
#include <stdint.h>
#include "window.h"
struct wl_buffer;
struct xwl_window;
struct xwl_tawc_buffer {
    struct wl_buffer *buffer;
    int width, height;
    uint32_t window_id, client_mask, serial;
    struct xwl_tawc_buffer *queue_next;
};
/* Forward native handles; only the compositor imports them into gralloc. */
int xwl_tawc_present_native_handle(WindowPtr window, int *fds, int num_fds,
    const int32_t *ints, int num_ints, int width, int height, int stride,
    int format, uint64_t usage, uint32_t client_mask, uint32_t serial);
void xwl_tawc_window_teardown(struct xwl_window *window);
#endif
