/* GXPSPngWriter
 *
 * Copyright (C) 2011  Carlos Garcia Campos <carlosgc@gnome.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <config.h>

#include "gxps-png-writer.h"
#include <png.h>
#include <stdint.h>
#include <string.h>

/* starting with libpng15, png.h no longer #includes zlib.h */
#ifndef Z_BEST_COMPRESSION
#define Z_BEST_COMPRESSION 9
#endif

struct _GXPSPngWriter {
	GObject parent;

        GXPSPngFormat format;

        png_structp   png_ptr;
        png_infop     info_ptr;
};

struct _GXPSPngWriterClass {
	GObjectClass parent_class;
};

static void gxps_png_writer_image_writer_iface_init (GXPSImageWriterInterface *iface);

G_DEFINE_TYPE_WITH_CODE (GXPSPngWriter, gxps_png_writer, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GXPS_TYPE_IMAGE_WRITER,
                                                gxps_png_writer_image_writer_iface_init))

static void
gxps_png_writer_init (GXPSPngWriter *png_writer)
{
}

static void
gxps_png_writer_class_init (GXPSPngWriterClass *klass)
{
}

GXPSImageWriter *
gxps_png_writer_new (GXPSPngFormat format)
{
        GXPSPngWriter *png_writer = GXPS_PNG_WRITER (g_object_new (GXPS_TYPE_PNG_WRITER, NULL));

        png_writer->format = format;

        return GXPS_IMAGE_WRITER (png_writer);
}

/* Unpremultiplies data and converts native endian ARGB => RGBA bytes */
static void
unpremultiply_data (png_structp png, png_row_infop row_info, png_bytep data)
{
        unsigned int i;

        for (i = 0; i < row_info->rowbytes; i += 4) {
                uint8_t *b = &data[i];
                uint32_t pixel;
                uint8_t  alpha;

                memcpy (&pixel, b, sizeof (uint32_t));
                alpha = (pixel & 0xff000000) >> 24;
                if (alpha == 0) {
                        b[0] = b[1] = b[2] = b[3] = 0;
                } else {
                        b[0] = (((pixel & 0xff0000) >> 16) * 255 + alpha / 2) / alpha;
                        b[1] = (((pixel & 0x00ff00) >>  8) * 255 + alpha / 2) / alpha;
                        b[2] = (((pixel & 0x0000ff) >>  0) * 255 + alpha / 2) / alpha;
                        b[3] = alpha;
                }
        }
}

/* Converts native endian xRGB => RGBx bytes */
static void
convert_data_to_bytes (png_structp png, png_row_infop row_info, png_bytep data)
{
        unsigned int i;

        for (i = 0; i < row_info->rowbytes; i += 4) {
                uint8_t *b = &data[i];
                uint32_t pixel;

                memcpy (&pixel, b, sizeof (uint32_t));

                b[0] = (pixel & 0xff0000) >> 16;
                b[1] = (pixel & 0x00ff00) >>  8;
                b[2] = (pixel & 0x0000ff) >>  0;
                b[3] = 0;
        }
}

static gboolean
gxps_png_writer_image_writer_init (GXPSImageWriter *image_writer,
                                   FILE            *fd,
                                   guint            width,
                                   guint            height,
                                   guint            x_resolution,
                                   guint            y_resolution)
{
        GXPSPngWriter *png_writer = GXPS_PNG_WRITER (image_writer);

        png_writer->png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        if (!png_writer->png_ptr) {
                g_printerr ("Error initializing png writer: png_create_write_struct failed\n");
                return FALSE;
        }

        png_writer->info_ptr = png_create_info_struct (png_writer->png_ptr);
        if (!png_writer->info_ptr) {
                g_printerr ("Error initializing png writer: png_create_info_struct failed\n");
                return FALSE;
        }

        if (setjmp (png_jmpbuf (png_writer->png_ptr))) {
                g_printerr ("Error initializing png writer: png_jmpbuf failed\n");
                return FALSE;
        }

        /* write header */
        png_init_io (png_writer->png_ptr, fd);
        if (setjmp (png_jmpbuf (png_writer->png_ptr))) {
                g_printerr ("Error initializing png writer: error writing PNG header\n");
                return FALSE;
        }

        png_set_compression_level (png_writer->png_ptr, Z_BEST_COMPRESSION);

        png_set_IHDR (png_writer->png_ptr, png_writer->info_ptr,
                      width, height, 8,
                      png_writer->format == GXPS_PNG_FORMAT_RGB ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_RGB_ALPHA,
                      PNG_INTERLACE_NONE,
                      PNG_COMPRESSION_TYPE_DEFAULT,
                      PNG_FILTER_TYPE_DEFAULT);

        png_set_pHYs (png_writer->png_ptr, png_writer->info_ptr,
                      x_resolution / 0.0254, y_resolution / 0.0254,
                      PNG_RESOLUTION_METER);

        png_write_info (png_writer->png_ptr, png_writer->info_ptr);
        if (setjmp (png_jmpbuf (png_writer->png_ptr))) {
                g_printerr ("Error initializing png writer: error writing png info bytes\n");
                return FALSE;
        }

        switch (png_writer->format) {
        case GXPS_PNG_FORMAT_RGB:
                png_set_write_user_transform_fn (png_writer->png_ptr, convert_data_to_bytes);
                png_set_filler (png_writer->png_ptr, 0, PNG_FILLER_AFTER);

                break;
        case GXPS_PNG_FORMAT_RGBA:
                png_set_write_user_transform_fn (png_writer->png_ptr, unpremultiply_data);

                break;
        }

        return TRUE;
}

static gboolean
gxps_png_writer_image_writer_write (GXPSImageWriter *image_writer,
                                    guchar          *row)
{
        GXPSPngWriter *png_writer = GXPS_PNG_WRITER (image_writer);

        png_write_rows (png_writer->png_ptr, &row, 1);
        if (setjmp (png_jmpbuf (png_writer->png_ptr))) {
                g_printerr ("Error writing png: error during png row write\n");
                return FALSE;
        }

        return TRUE;
}

static gboolean
gxps_png_writer_image_writer_finish (GXPSImageWriter *image_writer)
{
        GXPSPngWriter *png_writer = GXPS_PNG_WRITER (image_writer);

        png_write_end (png_writer->png_ptr, png_writer->info_ptr);
        if (setjmp (png_jmpbuf (png_writer->png_ptr))) {
                g_printerr ("Error finishing png: error during end of write\n");
                return FALSE;
        }

        png_destroy_write_struct (&png_writer->png_ptr, &png_writer->info_ptr);

        return TRUE;
}

static void
gxps_png_writer_image_writer_iface_init (GXPSImageWriterInterface *iface)
{
        iface->init = gxps_png_writer_image_writer_init;
        iface->write = gxps_png_writer_image_writer_write;
        iface->finish = gxps_png_writer_image_writer_finish;
}


