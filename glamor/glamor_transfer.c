/*
 * Copyright © 2014 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include "glamor_priv.h"
#include "glamor_transfer.h"

#ifdef __ANDROID__
static uint32_t
glamor_swap_red_blue(uint32_t pixel)
{
    return (pixel & 0xff00ff00) | ((pixel & 0xff) << 16) | ((pixel >> 16) & 0xff);
}

/* X11 pixels are BGRA, but EGLImage-backed Android buffers have RGBA storage.
 * Convert only the client's dirty pixels; no AHB mapping or GPU readback. */
static void
glamor_upload_ahb(int x, int y, int width, int height, const uint32_t *bits,
                  uint32_t byte_stride, Bool opaque, Bool unpack_subimage)
{
    uint32_t *rgba = xnfalloc((size_t) width * height * sizeof(*rgba));
    for (int row = 0; row < height; row++) {
        const uint32_t *src = (const uint32_t *) ((const uint8_t *) bits + row * byte_stride);
        for (int col = 0; col < width; col++)
            rgba[(size_t) row * width + col] = glamor_swap_red_blue(src[col]) |
                                             (opaque ? 0xff000000 : 0);
    }
    if (unpack_subimage) glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (unpack_subimage) glPixelStorei(GL_UNPACK_ROW_LENGTH, byte_stride / 4);
    free(rgba);
}
#endif

/*
 * Write a region of bits into a drawable's backing pixmap
 */
void
glamor_upload_boxes(DrawablePtr drawable, BoxPtr in_boxes, int in_nbox,
                    int dx_src, int dy_src,
                    int dx_dst, int dy_dst,
                    uint8_t *bits, uint32_t byte_stride)
{
    ScreenPtr                   screen = drawable->pScreen;
    glamor_screen_private       *glamor_priv = glamor_get_screen_private(screen);
    PixmapPtr                   pixmap = glamor_get_drawable_pixmap(drawable);
    glamor_pixmap_private       *priv = glamor_get_pixmap_private(pixmap);
    int                         box_index;
    const struct glamor_format *f = glamor_format_for_pixmap(pixmap);
    int                         bytes_per_pixel = PICT_FORMAT_BPP(f->render_format) >> 3;
    char *tmp_bits = NULL;

    if (glamor_drawable_effective_depth(drawable) == 24 && pixmap->drawable.depth == 32)
        tmp_bits = xnfalloc(byte_stride * pixmap->drawable.height);

    glamor_make_current(glamor_priv);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    if (glamor_priv->has_unpack_subimage)
        glPixelStorei(GL_UNPACK_ROW_LENGTH, byte_stride / bytes_per_pixel);

    glamor_pixmap_loop(priv, box_index) {
        BoxPtr                  box = glamor_pixmap_box_at(priv, box_index);
        glamor_pixmap_fbo       *fbo = glamor_pixmap_fbo_at(priv, box_index);
        BoxPtr                  boxes = in_boxes;
        int                     nbox = in_nbox;

        glamor_bind_texture(glamor_priv, GL_TEXTURE0, fbo, TRUE);

        while (nbox--) {

            /* compute drawable coordinates */
            int x1 = MAX(boxes->x1 + dx_dst, box->x1);
            int x2 = MIN(boxes->x2 + dx_dst, box->x2);
            int y1 = MAX(boxes->y1 + dy_dst, box->y1);
            int y2 = MIN(boxes->y2 + dy_dst, box->y2);

            uint32_t *src_line;
            size_t ofs = (y1 - dy_dst + dy_src) * byte_stride;
            ofs += (x1 - dx_dst + dx_src) * bytes_per_pixel;

            boxes++;

            if (x2 <= x1 || y2 <= y1)
                continue;

            src_line = (uint32_t *)(bits + ofs);

#ifdef __ANDROID__
            if (priv->type == GLAMOR_TEXTURE_DRM &&
                f->format == GL_BGRA && f->type == GL_UNSIGNED_BYTE) {
                glamor_upload_ahb(x1 - box->x1, y1 - box->y1, x2 - x1, y2 - y1,
                                  src_line, byte_stride,
                                  glamor_drawable_effective_depth(drawable) == 24,
                                  glamor_priv->has_unpack_subimage);
                continue;
            }
#endif

            if (tmp_bits) {
                uint32_t *tmp_line = (uint32_t *)(tmp_bits + ofs);
                int x, y;

                /* Make sure any sampling of the alpha channel will return 1.0 */
                for (y = y1; y < y2;
                     y++, src_line += byte_stride / 4, tmp_line += byte_stride / 4) {
                    for (x = 0; x < x2 - x1; x++)
                        tmp_line[x] = src_line[x] | 0xff000000;
                }

                src_line = (uint32_t *)(tmp_bits + ofs);
            }

            if (glamor_priv->has_unpack_subimage ||
                x2 - x1 == byte_stride / bytes_per_pixel) {
                glTexSubImage2D(GL_TEXTURE_2D, 0,
                                x1 - box->x1, y1 - box->y1,
                                x2 - x1, y2 - y1,
                                f->format, f->type,
                                src_line);
            } else {
                for (; y1 < y2; y1++, src_line += byte_stride / bytes_per_pixel)
                    glTexSubImage2D(GL_TEXTURE_2D, 0,
                                    x1 - box->x1, y1 - box->y1,
                                    x2 - x1, 1,
                                    f->format, f->type,
                                    src_line);
            }
        }
    }

    free(tmp_bits);

    if (glamor_priv->has_unpack_subimage)
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

/*
 * Upload a region of data
 */

void
glamor_upload_region(DrawablePtr drawable, RegionPtr region,
                     int region_x, int region_y,
                     uint8_t *bits, uint32_t byte_stride)
{
    glamor_upload_boxes(drawable, RegionRects(region), RegionNumRects(region),
                        -region_x, -region_y,
                        0, 0,
                        bits, byte_stride);
}

/*
 * Read stuff from the drawable's backing pixmap FBOs and write to memory
 */
void
glamor_download_boxes(DrawablePtr drawable, BoxPtr in_boxes, int in_nbox,
                      int dx_src, int dy_src,
                      int dx_dst, int dy_dst,
                      uint8_t *bits, uint32_t byte_stride)
{
    ScreenPtr screen = drawable->pScreen;
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    PixmapPtr pixmap = glamor_get_drawable_pixmap(drawable);
    glamor_pixmap_private *priv = glamor_get_pixmap_private(pixmap);
    int box_index;
    const struct glamor_format *f = glamor_format_for_pixmap(pixmap);
    int bytes_per_pixel = PICT_FORMAT_BPP(f->render_format) >> 3;

    glamor_make_current(glamor_priv);

    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    if (glamor_priv->has_pack_subimage)
        glPixelStorei(GL_PACK_ROW_LENGTH, byte_stride / bytes_per_pixel);

    glamor_pixmap_loop(priv, box_index) {
        BoxPtr                  box = glamor_pixmap_box_at(priv, box_index);
        glamor_pixmap_fbo       *fbo = glamor_pixmap_fbo_at(priv, box_index);
        BoxPtr                  boxes = in_boxes;
        int                     nbox = in_nbox;

        /* This should not be called on GLAMOR_FBO_NO_FBO-allocated pixmaps. */
        assert(fbo->fb);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo->fb);

        while (nbox--) {

            /* compute drawable coordinates */
            int                     x1 = MAX(boxes->x1 + dx_src, box->x1);
            int                     x2 = MIN(boxes->x2 + dx_src, box->x2);
            int                     y1 = MAX(boxes->y1 + dy_src, box->y1);
            int                     y2 = MIN(boxes->y2 + dy_src, box->y2);
            size_t ofs = (y1 - dy_src + dy_dst) * byte_stride;
            ofs += (x1 - dx_src + dx_dst) * bytes_per_pixel;

            boxes++;

            if (x2 <= x1 || y2 <= y1)
                continue;

#ifdef __ANDROID__
            /* GLES guarantees RGBA/UNSIGNED_BYTE readback for RGBA8 storage.
             * Keep the CPU conversion confined to explicit X11 readback;
             * window presentation continues to share the AHB directly. */
            if (f->format == GL_BGRA && f->type == GL_UNSIGNED_BYTE) {
                if (glamor_priv->has_pack_subimage)
                    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
                uint32_t *rgba = xnfalloc((size_t) (x2 - x1) * (y2 - y1) * 4);
                glReadPixels(x1 - box->x1, y1 - box->y1, x2 - x1, y2 - y1,
                             GL_RGBA, GL_UNSIGNED_BYTE, rgba);
                for (int row = 0; row < y2 - y1; row++) {
                    uint32_t *dst = (uint32_t *) (bits + ofs + row * byte_stride);
                    for (int col = 0; col < x2 - x1; col++)
                        dst[col] = glamor_swap_red_blue(rgba[row * (x2 - x1) + col]);
                }
                free(rgba);
                if (glamor_priv->has_pack_subimage)
                    glPixelStorei(GL_PACK_ROW_LENGTH, byte_stride / bytes_per_pixel);
                continue;
            }
#endif

            if (glamor_priv->has_pack_subimage ||
                x2 - x1 == byte_stride / bytes_per_pixel) {
                glReadPixels(x1 - box->x1, y1 - box->y1, x2 - x1, y2 - y1, f->format, f->type, bits + ofs);
            } else {
                for (; y1 < y2; y1++, ofs += byte_stride)
                    glReadPixels(x1 - box->x1, y1 - box->y1, x2 - x1, 1, f->format, f->type, bits + ofs);
            }
        }
    }
    if (glamor_priv->has_pack_subimage)
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
}
