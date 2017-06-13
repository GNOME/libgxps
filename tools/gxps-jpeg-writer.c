/* GXPSJpegWriter
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

#include "gxps-jpeg-writer.h"
#include <jpeglib.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>

struct _GXPSJpegWriter {
	GObject parent;

        guchar                     *row_buffer;
        struct jpeg_compress_struct cinfo;
        struct jpeg_error_mgr       error_mgr;
};

struct _GXPSJpegWriterClass {
	GObjectClass parent_class;
};

static void gxps_jpeg_writer_image_writer_iface_init (GXPSImageWriterInterface *iface);

G_DEFINE_TYPE_WITH_CODE (GXPSJpegWriter, gxps_jpeg_writer, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GXPS_TYPE_IMAGE_WRITER,
                                                gxps_jpeg_writer_image_writer_iface_init))

static void
gxps_jpeg_writer_init (GXPSJpegWriter *jpeg_writer)
{
}

static void
gxps_jpeg_writer_class_init (GXPSJpegWriterClass *klass)
{
}

GXPSImageWriter *
gxps_jpeg_writer_new (void)
{
        return GXPS_IMAGE_WRITER (g_object_new (GXPS_TYPE_JPEG_WRITER, NULL));
}

static void
jpeg_output_message (j_common_ptr cinfo)
{
        char buffer[JMSG_LENGTH_MAX];

        /* Create the message */
        (*cinfo->err->format_message) (cinfo, buffer);
        g_printerr ("%s\n", buffer);
}

static gboolean
gxps_jpeg_writer_image_writer_init (GXPSImageWriter *image_writer,
                                    FILE            *fd,
                                    guint            width,
                                    guint            height,
                                    guint            x_resolution,
                                    guint            y_resolution)
{
        GXPSJpegWriter *jpeg_writer = GXPS_JPEG_WRITER (image_writer);

        jpeg_writer->row_buffer = (guchar *) g_malloc (width * 4);

        jpeg_std_error (&jpeg_writer->error_mgr);
        jpeg_writer->error_mgr.output_message = jpeg_output_message;

        jpeg_create_compress (&jpeg_writer->cinfo);
        jpeg_writer->cinfo.err = &jpeg_writer->error_mgr;

        jpeg_stdio_dest (&jpeg_writer->cinfo, fd);

        jpeg_writer->cinfo.image_width = width;
        jpeg_writer->cinfo.image_height = height;
        jpeg_writer->cinfo.input_components = 3; /* color components per pixel */
        jpeg_writer->cinfo.in_color_space = JCS_RGB; /* colorspace of input image */
        jpeg_set_defaults (&jpeg_writer->cinfo);

        jpeg_writer->cinfo.density_unit = 1; /* dots per inch */
        jpeg_writer->cinfo.X_density = x_resolution;
        jpeg_writer->cinfo.Y_density = y_resolution;

        jpeg_start_compress (&jpeg_writer->cinfo, TRUE);

        return TRUE;
}

static gboolean
gxps_jpeg_writer_image_writer_write (GXPSImageWriter *image_writer,
                                     guchar          *row)
{
        GXPSJpegWriter *jpeg_writer = GXPS_JPEG_WRITER (image_writer);
        guint           image_width = jpeg_writer->cinfo.image_width;
        uint32_t       *pixel = (uint32_t *)row;
        guchar         *rowp;
        guint           i;

        rowp = jpeg_writer->row_buffer;

        for (i = 0; i < image_width; i++, pixel++) {
                *rowp++ = (*pixel & 0xff0000) >> 16;
                *rowp++ = (*pixel & 0x00ff00) >>  8;
                *rowp++ = (*pixel & 0x0000ff) >>  0;
        }

        jpeg_write_scanlines (&jpeg_writer->cinfo, &jpeg_writer->row_buffer, 1);

        return TRUE;
}

static gboolean
gxps_jpeg_writer_image_writer_finish (GXPSImageWriter *image_writer)
{
        GXPSJpegWriter *jpeg_writer = GXPS_JPEG_WRITER (image_writer);

        jpeg_finish_compress (&jpeg_writer->cinfo);

        g_free (jpeg_writer->row_buffer);
        jpeg_writer->row_buffer = NULL;

        return TRUE;
}

static void
gxps_jpeg_writer_image_writer_iface_init (GXPSImageWriterInterface *iface)
{
        iface->init = gxps_jpeg_writer_image_writer_init;
        iface->write = gxps_jpeg_writer_image_writer_write;
        iface->finish = gxps_jpeg_writer_image_writer_finish;
}
