/* GXPSJpegConverter
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

#include "gxps-jpeg-converter.h"
#include "gxps-jpeg-writer.h"
#include <libgxps/gxps.h>

struct _GXPSJpegConverter {
	GXPSImageConverter parent;
};

struct _GXPSJpegConverterClass {
	GXPSImageConverterClass parent_class;
};

G_DEFINE_TYPE (GXPSJpegConverter, gxps_jpeg_converter, GXPS_TYPE_IMAGE_CONVERTER)

static const gchar *
gxps_jpeg_converter_get_extension (GXPSConverter *converter)
{
        return "jpg";
}

static void
gxps_jpeg_converter_end_page (GXPSConverter *converter)
{
        GXPSImageConverter *image_converter = GXPS_IMAGE_CONVERTER (converter);

        if (!image_converter->writer)
                image_converter->writer = gxps_jpeg_writer_new ();

        GXPS_CONVERTER_CLASS (gxps_jpeg_converter_parent_class)->end_page (converter);
}

static void
gxps_jpeg_converter_init (GXPSJpegConverter *converter)
{
}

static void
gxps_jpeg_converter_class_init (GXPSJpegConverterClass *klass)
{
        GXPSConverterClass *converter_class = GXPS_CONVERTER_CLASS (klass);

        converter_class->get_extension = gxps_jpeg_converter_get_extension;
        converter_class->end_page = gxps_jpeg_converter_end_page;
}
