/* GXPSImageWriter
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

#include "gxps-image-writer.h"

G_DEFINE_INTERFACE (GXPSImageWriter, gxps_image_writer, G_TYPE_OBJECT)

static void
gxps_image_writer_default_init (GXPSImageWriterInterface *iface)
{
}

gboolean
gxps_image_writer_init (GXPSImageWriter *image_writer,
                        FILE            *fd,
                        guint            width,
                        guint            height,
                        guint            x_resolution,
                        guint            y_resolution)
{
        g_return_val_if_fail (GXPS_IS_IMAGE_WRITER (image_writer), FALSE);
        g_return_val_if_fail (fd != NULL, FALSE);

        return GXPS_IMAGE_WRITER_GET_IFACE (image_writer)->init (image_writer, fd, width, height,
                                                                 x_resolution, y_resolution);
}

gboolean
gxps_image_writer_write (GXPSImageWriter *image_writer,
                         guchar          *row)
{
        g_return_val_if_fail (GXPS_IS_IMAGE_WRITER (image_writer), FALSE);

        return GXPS_IMAGE_WRITER_GET_IFACE (image_writer)->write (image_writer, row);
}

gboolean
gxps_image_writer_finish (GXPSImageWriter *image_writer)
{
        g_return_val_if_fail (GXPS_IS_IMAGE_WRITER (image_writer), FALSE);

        return GXPS_IMAGE_WRITER_GET_IFACE (image_writer)->finish (image_writer);
}
