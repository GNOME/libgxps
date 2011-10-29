/* GXPSImageConverter
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

#ifndef __GXPS_IMAGE_CONVERTER_H__
#define __GXPS_IMAGE_CONVERTER_H__

#include "gxps-converter.h"
#include "gxps-image-writer.h"

G_BEGIN_DECLS

#define GXPS_TYPE_IMAGE_CONVERTER           (gxps_image_converter_get_type ())
#define GXPS_IMAGE_CONVERTER(obj)           (G_TYPE_CHECK_INSTANCE_CAST (obj, GXPS_TYPE_IMAGE_CONVERTER, GXPSImageConverter))
#define GXPS_IMAGE_CONVERTER_CLASS(cls)     (G_TYPE_CHECK_CLASS_CAST (cls, GXPS_TYPE_IMAGE_CONVERTER, GXPSImageConverterClass))
#define GXPS_IS_IMAGE_CONVERTER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE (obj, GXPS_TYPE_IMAGE_CONVERTER))
#define GXPS_IS_IMAGE_CONVERTER_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE (obj, GXPS_TYPE_IMAGE_CONVERTER))
#define GXPS_IMAGE_CONVERTER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GXPS_TYPE_IMAGE_CONVERTER, GXPSImageConverterClass))

typedef struct _GXPSImageConverter        GXPSImageConverter;
typedef struct _GXPSImageConverterClass   GXPSImageConverterClass;

struct _GXPSImageConverter {
	GXPSConverter parent;

        GXPSImageWriter *writer;
        guint            current_page;
        gchar           *page_prefix;
        guint            n_digits;
        guint            fill_background : 1;
};

struct _GXPSImageConverterClass {
	GXPSConverterClass parent_class;
};

GType gxps_image_converter_get_type (void);

G_END_DECLS

#endif /* __GXPS_IMAGE_CONVERTER_H__ */
