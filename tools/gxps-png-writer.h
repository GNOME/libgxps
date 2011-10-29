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

#ifndef __GXPS_PNG_WRITER_H__
#define __GXPS_PNG_WRITER_H__

#include <glib-object.h>
#include "gxps-image-writer.h"

G_BEGIN_DECLS

#define GXPS_TYPE_PNG_WRITER           (gxps_png_writer_get_type ())
#define GXPS_PNG_WRITER(obj)           (G_TYPE_CHECK_INSTANCE_CAST (obj, GXPS_TYPE_PNG_WRITER, GXPSPngWriter))
#define GXPS_PNG_WRITER_CLASS(cls)     (G_TYPE_CHECK_CLASS_CAST (cls, GXPS_TYPE_PNG_WRITER, GXPSPngWriterClass))
#define GXPS_IS_PNG_WRITER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE (obj, GXPS_TYPE_PNG_WRITER))
#define GXPS_IS_PNG_WRITER_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE (obj, GXPS_TYPE_PNG_WRITER))
#define GXPS_PNG_WRITER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GXPS_TYPE_PNG_WRITER, GXPSPngWriterClass))

typedef enum {
        GXPS_PNG_FORMAT_RGB,
        GXPS_PNG_FORMAT_RGBA
} GXPSPngFormat;

typedef struct _GXPSPngWriter        GXPSPngWriter;
typedef struct _GXPSPngWriterClass   GXPSPngWriterClass;

GType            gxps_png_writer_get_type (void);
GXPSImageWriter *gxps_png_writer_new      (GXPSPngFormat format);


G_END_DECLS

#endif /* __GXPS_PNG_WRITER_H__ */
