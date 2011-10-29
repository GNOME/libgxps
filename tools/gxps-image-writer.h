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

#ifndef __GXPS_IMAGE_WRITER_H__
#define __GXPS_IMAGE_WRITER_H__

#include <stdio.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GXPS_TYPE_IMAGE_WRITER           (gxps_image_writer_get_type ())
#define GXPS_IMAGE_WRITER(obj)           (G_TYPE_CHECK_INSTANCE_CAST (obj, GXPS_TYPE_IMAGE_WRITER, GXPSImageWriter))
#define GXPS_IS_IMAGE_WRITER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE (obj, GXPS_TYPE_IMAGE_WRITER))
#define GXPS_IMAGE_WRITER_GET_IFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), GXPS_TYPE_IMAGE_WRITER, GXPSImageWriterInterface))

typedef struct _GXPSImageWriter          GXPSImageWriter;
typedef struct _GXPSImageWriterInterface GXPSImageWriterInterface;

struct _GXPSImageWriterInterface {
        GTypeInterface g_iface;

        gboolean (* init)   (GXPSImageWriter *writer,
                             FILE            *fd,
                             guint            width,
                             guint            height,
                             guint            x_resolution,
                             guint            y_resolution);
        gboolean (* write)  (GXPSImageWriter *writer,
                             guchar          *row);
        gboolean (* finish) (GXPSImageWriter *writer);
};

GType    gxps_image_writer_get_type (void);

gboolean gxps_image_writer_init     (GXPSImageWriter *image_writer,
                                     FILE            *fd,
                                     guint            width,
                                     guint            height,
                                     guint            x_resolution,
                                     guint            y_resolution);
gboolean gxps_image_writer_write    (GXPSImageWriter *image_writer,
                                     guchar          *row);
gboolean gxps_image_writer_finish   (GXPSImageWriter *image_writer);

G_END_DECLS

#endif /* __GXPS_IMAGE_WRITER_H__ */
