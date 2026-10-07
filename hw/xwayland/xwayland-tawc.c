/* SPDX-License-Identifier: MIT
 * TAWC-DRI forwards native buffers without importing or reading back pixels.
 * Each X drawable has its own Wayland subsurface, lifetime and FIFO queue.
 */
#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif
#include <stdlib.h>
#include <unistd.h>
#include <wayland-client.h>
#include <X11/X.h>
#include <arlinux/tawc-dri.h>
#include "windowstr.h"
#include "xwayland-screen.h"
#include "xwayland-window.h"
#include "xwayland-tawc.h"
#include "wayland-android-client-protocol.h"
#include "viewporter-client-protocol.h"

#define TAWC_MAX_QUEUED 8
extern void tawc_dri_send_buffer_release(uint32_t, uint32_t, uint32_t);
extern Bool tawc_dri_has_presenter(XID);

struct xwl_tawc_surface {
    struct xwl_tawc_surface *next;
    struct xwl_window *owner;
    WindowPtr window;
    struct wl_surface *surface;
    struct wl_subsurface *subsurface;
    struct wp_viewport *viewport;
    struct wl_callback *frame;
    OsTimerPtr frame_timer;
    struct xwl_tawc_buffer *head, *tail;
    int queued, width, height;
    Bool root_surface;
};

static void buffer_destroy(struct xwl_tawc_buffer *buffer)
{
    if (buffer->buffer) wl_buffer_destroy(buffer->buffer);
    if (buffer->opaque_region) wl_region_destroy(buffer->opaque_region);
    free(buffer);
}

static void buffer_release(void *data, struct wl_buffer *wl_buffer)
{
    struct xwl_tawc_buffer *buffer = data;
    (void)wl_buffer;
    tawc_dri_send_buffer_release(buffer->window_id, buffer->client_mask, buffer->serial);
    buffer_destroy(buffer);
}
static const struct wl_buffer_listener buffer_listener = { .release = buffer_release };

static struct xwl_tawc_buffer *pop(struct xwl_tawc_surface *surface)
{
    struct xwl_tawc_buffer *buffer = surface->head;
    if (!buffer) return NULL;
    surface->head = buffer->queue_next;
    if (!surface->head) surface->tail = NULL;
    surface->queued--;
    buffer->queue_next = NULL;
    return buffer;
}

/* Ancestors clip a drawable; siblings are handled by compositor stacking. */
static Bool geometry(struct xwl_tawc_surface *surface)
{
    if (surface->root_surface) return TRUE;
    WindowPtr window = surface->window;
    WindowPtr root = surface->owner->surface_window;
    int x1 = window->drawable.x, y1 = window->drawable.y;
    int x2 = x1 + min(surface->width, window->drawable.width);
    int y2 = y1 + min(surface->height, window->drawable.height);
    for (WindowPtr p = window; p; p = p->parent) {
        x1 = max(x1, p->drawable.x);
        y1 = max(y1, p->drawable.y);
        x2 = min(x2, p->drawable.x + p->drawable.width);
        y2 = min(y2, p->drawable.y + p->drawable.height);
        if (p == surface->owner->toplevel) break;
    }
    if (x2 <= x1 || y2 <= y1) return FALSE;
    wl_subsurface_set_position(surface->subsurface, x1 - root->drawable.x, y1 - root->drawable.y);
    wp_viewport_set_source(surface->viewport,
        wl_fixed_from_int(x1 - window->drawable.x), wl_fixed_from_int(y1 - window->drawable.y),
        wl_fixed_from_int(x2 - x1), wl_fixed_from_int(y2 - y1));
    wp_viewport_set_destination(surface->viewport, x2 - x1, y2 - y1);
    return TRUE;
}

static void frame_done(void *, struct wl_callback *, uint32_t);
static const struct wl_callback_listener frame_listener = { .done = frame_done };
static CARD32 frame_timeout(OsTimerPtr, CARD32, void *);

static void commit(struct xwl_tawc_surface *surface, struct xwl_tawc_buffer *buffer)
{
    surface->width = buffer->width;
    surface->height = buffer->height;
    if (!geometry(surface)) {
        buffer_release(buffer, buffer->buffer);
        return;
    }
    wl_surface_set_opaque_region(surface->surface, buffer->opaque_region);
    wl_surface_attach(surface->surface, buffer->buffer, 0, 0);
    wl_surface_damage(surface->surface, 0, 0, buffer->width, buffer->height);
    surface->frame = wl_surface_frame(surface->surface);
    wl_callback_add_listener(surface->frame, &frame_listener, surface);
    /* Like Xwayland Present's TIMER_LEN_FLIP: compositors may withhold frame
     * callbacks for invisible surfaces. Keep those clients making progress at
     * 1 fps, without ever releasing a buffer still owned by the compositor. */
    surface->frame_timer = TimerSet(surface->frame_timer, 0, 1000, frame_timeout, surface);
    wl_surface_commit(surface->surface);
    /* Subsurface position and stacking are parent-commit synchronized. */
    if (!surface->root_surface) wl_surface_commit(surface->owner->surface);
}

static void frame_done(void *data, struct wl_callback *callback, uint32_t time)
{
    struct xwl_tawc_surface *surface = data;
    (void)time;
    wl_callback_destroy(callback);
    surface->frame = NULL;
    TimerCancel(surface->frame_timer);
    struct xwl_tawc_buffer *buffer;
    while (!surface->frame && (buffer = pop(surface))) commit(surface, buffer);
}

static CARD32 frame_timeout(OsTimerPtr timer, CARD32 now, void *data)
{
    struct xwl_tawc_surface *surface = data;
    (void)timer;
    (void)now;
    if (surface->frame) wl_callback_destroy(surface->frame);
    surface->frame = NULL;
    struct xwl_tawc_buffer *buffer;
    while (!surface->frame && (buffer = pop(surface))) commit(surface, buffer);
    return surface->frame ? 1000 : 0;
}

static void surface_destroy(struct xwl_tawc_surface *surface)
{
    struct xwl_tawc_buffer *buffer;
    if (surface->frame_timer) TimerFree(surface->frame_timer);
    if (surface->frame) wl_callback_destroy(surface->frame);
    while ((buffer = pop(surface))) buffer_release(buffer, buffer->buffer);
    if (surface->viewport) wp_viewport_destroy(surface->viewport);
    if (surface->subsurface) wl_subsurface_destroy(surface->subsurface);
    if (surface->surface && !surface->root_surface) wl_surface_destroy(surface->surface);
    /* Committed buffers are released by the compositor, never early here. */
    free(surface);
}

void xwl_tawc_window_teardown(struct xwl_window *window)
{
    while (window->tawc_surfaces) {
        struct xwl_tawc_surface *surface = window->tawc_surfaces;
        window->tawc_surfaces = surface->next;
        surface_destroy(surface);
    }
}

void xwl_tawc_unrealize(WindowPtr window)
{
    struct xwl_window *owner = xwl_window_from_window(window);
    if (!owner) return;
    struct xwl_tawc_surface **link = &owner->tawc_surfaces;
    while (*link) {
        struct xwl_tawc_surface *surface = *link;
        WindowPtr p = surface->window;
        while (p && p != window) p = p->parent;
        if (p) {
            *link = surface->next;
            surface_destroy(surface);
        } else link = &surface->next;
    }
}

/* Walk X siblings bottom-to-top, with children above their parent. */
static void restack(struct xwl_window *owner, WindowPtr window, struct wl_surface **below)
{
    for (struct xwl_tawc_surface *s = owner->tawc_surfaces; s; s = s->next) {
        if (s->window != window || s->root_surface) continue;
        wl_subsurface_place_above(s->subsurface, *below);
        *below = s->surface;
    }
    for (WindowPtr child = window->lastChild; child; child = child->prevSib)
        restack(owner, child, below);
}

void xwl_tawc_window_changed(struct xwl_window *owner)
{
    if (!owner->tawc_surfaces) return;
    struct xwl_tawc_surface **link = &owner->tawc_surfaces;
    while (*link) {
        struct xwl_tawc_surface *s = *link;
        if (!s->window->realized || !tawc_dri_has_presenter(s->window->drawable.id) ||
            (s->width && !geometry(s))) {
            *link = s->next;
            surface_destroy(s);
            continue;
        }
        if (s->width && geometry(s)) wl_surface_commit(s->surface);
        link = &s->next;
    }
    struct wl_surface *below = owner->surface;
    restack(owner, owner->toplevel, &below);
    wl_surface_commit(owner->surface);
}

Bool xwl_tawc_owns_surface(struct xwl_window *owner)
{
    for (struct xwl_tawc_surface *s = owner->tawc_surfaces; s; s = s->next)
        if (s->root_surface) return TRUE;
    return FALSE;
}

static struct xwl_tawc_surface *get_surface(struct xwl_screen *screen, struct xwl_window *owner, WindowPtr window)
{
    for (struct xwl_tawc_surface *s = owner->tawc_surfaces; s; s = s->next)
        if (s->window == window) return s;
    struct xwl_tawc_surface *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->owner = owner;
    s->window = window;
    WindowPtr root = owner->surface_window;
    /* Preserve the top-level buffer's alpha semantics. A full-window native
     * presenter replaces its X backing pixmap; other drawables are children,
     * never a competing attachment to that top-level surface. */
    if (!xwl_tawc_owns_surface(owner) &&
        window->drawable.x == root->drawable.x && window->drawable.y == root->drawable.y &&
        window->drawable.width == root->drawable.width && window->drawable.height == root->drawable.height) {
        s->root_surface = TRUE;
        s->surface = owner->surface;
        s->next = owner->tawc_surfaces;
        owner->tawc_surfaces = s;
        return s;
    }
    s->surface = wl_compositor_create_surface(screen->compositor);
    if (!s->surface) goto fail;
    s->subsurface = wl_subcompositor_get_subsurface(screen->subcompositor, s->surface, owner->surface);
    if (!s->subsurface) goto fail;
    s->viewport = wp_viewporter_get_viewport(screen->viewporter, s->surface);
    if (!s->viewport) goto fail;
    wl_subsurface_set_desync(s->subsurface);
    /* The top-level X surface remains the sole input target. */
    struct wl_region *empty = wl_compositor_create_region(screen->compositor);
    if (!empty) goto fail;
    wl_surface_set_input_region(s->surface, empty);
    wl_region_destroy(empty);
    s->next = owner->tawc_surfaces;
    owner->tawc_surfaces = s;
    struct wl_surface *below = owner->surface;
    restack(owner, owner->toplevel, &below);
    return s;
fail:
    surface_destroy(s);
    return NULL;
}

int xwl_tawc_present_native_handle(WindowPtr window, int *fds, int num_fds,
    const int32_t *ints, int num_ints, int width, int height, int stride,
    int format, uint64_t usage, uint32_t client_mask, uint32_t serial, uint32_t flags)
{
    int result = BadMatch;
    struct xwl_tawc_buffer *buffer = NULL;
    if (!window || width <= 0 || height <= 0 || stride < width ||
        num_fds <= 0 || num_ints < 0 || usage > UINT32_MAX) goto done;
    struct xwl_screen *screen = xwl_screen_get(window->drawable.pScreen);
    if (!window->realized && screen->tawc_wlegl) {
        tawc_dri_send_buffer_release(window->drawable.id, client_mask, serial);
        result = Success;
        goto done;
    }
    struct xwl_window *owner = xwl_window_from_window(window);
    if (!screen->tawc_wlegl || !screen->subcompositor || !screen->viewporter ||
        !owner || !owner->surface) goto done;
    result = BadAlloc;
    struct xwl_tawc_surface *surface = get_surface(screen, owner, window);
    if (!surface || surface->queued >= TAWC_MAX_QUEUED) goto done;
    buffer = calloc(1, sizeof(*buffer));
    if (!buffer) goto done;
    if (flags & TAWC_DRI_PRESENT_OPAQUE) {
        buffer->opaque_region = wl_compositor_create_region(screen->compositor);
        if (!buffer->opaque_region) goto done;
        wl_region_add(buffer->opaque_region, 0, 0, width, height);
    }
    struct wl_array values = { .size = (size_t)num_ints * sizeof(int32_t), .data = (void *)ints };
    struct android_wlegl_handle *handle = android_wlegl_create_handle(screen->tawc_wlegl, num_fds, &values);
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
    if (surface->frame) {
        if (surface->tail) surface->tail->queue_next = buffer;
        else surface->head = buffer;
        surface->tail = buffer;
        surface->queued++;
    } else commit(surface, buffer);
    buffer = NULL;
    result = Success;
done:
    if (buffer) buffer_destroy(buffer);
    for (int i = 0; i < num_fds; i++) if (fds[i] >= 0) close(fds[i]);
    return result;
}
