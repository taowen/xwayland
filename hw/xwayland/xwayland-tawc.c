/* SPDX-License-Identifier: MIT
 * TAWC-DRI is an X11 adapter for android_wlegl. Buffer import belongs entirely
 * to the compositor; Xwayland only forwards handles and release notifications.
 */
#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif
#include <stdlib.h>
#include <unistd.h>
#include <wayland-client.h>
#include <X11/X.h>
#include "os.h"
#include "windowstr.h"
#include "xwayland-screen.h"
#include "xwayland-window.h"
#include "xwayland-tawc.h"
#include "wayland-android-client-protocol.h"

#define TAWC_MAX_QUEUED 8
extern void tawc_dri_send_buffer_release(uint32_t window, uint32_t client, uint32_t serial);

static void
buffer_destroy(struct xwl_tawc_buffer *buffer)
{
    if (buffer->buffer) wl_buffer_destroy(buffer->buffer);
    free(buffer);
}

static void
buffer_release(void *data, struct wl_buffer *wl_buffer)
{
    struct xwl_tawc_buffer *buffer = data;
    (void)wl_buffer;
    tawc_dri_send_buffer_release(buffer->window_id, buffer->client_mask, buffer->serial);
    buffer_destroy(buffer);
}
static const struct wl_buffer_listener buffer_listener = { .release = buffer_release };

static void frame_done(void *data, struct wl_callback *callback, uint32_t time);
static const struct wl_callback_listener frame_listener = { .done = frame_done };

static void
commit(struct xwl_window *window, struct xwl_tawc_buffer *buffer)
{
    wl_surface_attach(window->surface, buffer->buffer, 0, 0);
    wl_surface_damage(window->surface, 0, 0, buffer->width, buffer->height);
    window->tawc_frame_callback = wl_surface_frame(window->surface);
    wl_callback_add_listener(window->tawc_frame_callback, &frame_listener, window);
    wl_surface_commit(window->surface);
}

static struct xwl_tawc_buffer *
pop(struct xwl_window *window)
{
    struct xwl_tawc_buffer *buffer = window->tawc_queue_head;
    if (!buffer) return NULL;
    window->tawc_queue_head = buffer->queue_next;
    if (!window->tawc_queue_head) window->tawc_queue_tail = NULL;
    window->tawc_queue_len--;
    buffer->queue_next = NULL;
    return buffer;
}

static void
frame_done(void *data, struct wl_callback *callback, uint32_t time)
{
    struct xwl_window *window = data;
    (void)time;
    wl_callback_destroy(callback);
    window->tawc_frame_callback = NULL;
    struct xwl_tawc_buffer *buffer = pop(window);
    if (buffer) commit(window, buffer);
}

void
xwl_tawc_window_teardown(struct xwl_window *window)
{
    struct xwl_tawc_buffer *buffer;
    if (window->tawc_frame_callback) {
        wl_callback_destroy(window->tawc_frame_callback);
        window->tawc_frame_callback = NULL;
    }
    while ((buffer = pop(window))) {
        tawc_dri_send_buffer_release(buffer->window_id, buffer->client_mask, buffer->serial);
        buffer_destroy(buffer);
    }
}

int
xwl_tawc_present_native_handle(WindowPtr window, int *fds, int num_fds,
    const int32_t *ints, int num_ints, int width, int height, int stride,
    int format, uint64_t usage, uint32_t client_mask, uint32_t serial)
{
    int result = BadMatch;
    struct xwl_tawc_buffer *buffer = NULL;
    if (!window || width <= 0 || height <= 0 || stride < width ||
        num_fds <= 0 || num_ints < 0 || usage > UINT32_MAX)
        goto done;
    struct xwl_screen *screen = xwl_screen_get(window->drawable.pScreen);
    struct xwl_window *xwl = xwl_window_from_window(window);
    if (!screen->tawc_wlegl || !xwl || !xwl->surface) goto done;
    /* Never silently drop a FIFO entry or invent a release timeout. */
    result = BadAlloc;
    if (xwl->tawc_queue_len >= TAWC_MAX_QUEUED) goto done;
    buffer = calloc(1, sizeof(*buffer));
    if (!buffer) goto done;
    struct wl_array values = {
        .size = (size_t)num_ints * sizeof(int32_t), .data = (void *)ints,
    };
    struct android_wlegl_handle *handle =
        android_wlegl_create_handle(screen->tawc_wlegl, num_fds, &values);
    if (!handle) goto done;
    for (int i = 0; i < num_fds; i++) android_wlegl_handle_add_fd(handle, fds[i]);
    buffer->buffer = android_wlegl_create_buffer(screen->tawc_wlegl,
        width, height, stride, format, (uint32_t)usage, handle);
    android_wlegl_handle_destroy(handle);
    if (!buffer->buffer) goto done;
    buffer->width = width;
    buffer->height = height;
    buffer->window_id = window->drawable.id;
    buffer->client_mask = client_mask;
    buffer->serial = serial;
    wl_buffer_add_listener(buffer->buffer, &buffer_listener, buffer);
    if (xwl->tawc_frame_callback) {
        if (xwl->tawc_queue_tail) xwl->tawc_queue_tail->queue_next = buffer;
        else xwl->tawc_queue_head = buffer;
        xwl->tawc_queue_tail = buffer;
        xwl->tawc_queue_len++;
    } else {
        commit(xwl, buffer);
    }
    buffer = NULL;
    result = Success;
done:
    /* Wayland marshaling duplicates FDs, so these X11 request FDs are always
     * consumed here, including all error paths. */
    if (buffer) buffer_destroy(buffer);
    for (int i = 0; i < num_fds; i++) if (fds[i] >= 0) close(fds[i]);
    return result;
}
