/* GXPSPrintConverter
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

#ifndef __GXPS_PRINT_CONVERTER_H__
#define __GXPS_PRINT_CONVERTER_H__

#include "gxps-converter.h"

G_BEGIN_DECLS

#define GXPS_TYPE_PRINT_CONVERTER           (gxps_print_converter_get_type ())
#define GXPS_PRINT_CONVERTER(obj)           (G_TYPE_CHECK_INSTANCE_CAST (obj, GXPS_TYPE_PRINT_CONVERTER, GXPSPrintConverter))
#define GXPS_PRINT_CONVERTER_CLASS(cls)     (G_TYPE_CHECK_CLASS_CAST (cls, GXPS_TYPE_PRINT_CONVERTER, GXPSPrintConverterClass))
#define GXPS_IS_PRINT_CONVERTER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE (obj, GXPS_TYPE_PRINT_CONVERTER))
#define GXPS_IS_PRINT_CONVERTER_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE (obj, GXPS_TYPE_PRINT_CONVERTER))
#define GXPS_PRINT_CONVERTER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GXPS_TYPE_PRINT_CONVERTER, GXPSPrintConverterClass))

typedef enum {
        GXPS_PRINT_CONVERTER_EXPAND = 1 << 0,
        GXPS_PRINT_CONVERTER_SHRINK = 1 << 1,
        GXPS_PRINT_CONVERTER_CENTER = 1 << 2
} GXPSPrintConverterFlags;

typedef struct _GXPSPrintConverter        GXPSPrintConverter;
typedef struct _GXPSPrintConverterClass   GXPSPrintConverterClass;

struct _GXPSPrintConverter {
	GXPSConverter parent;

        gchar                  *filename;
        guint                   paper_width;
        guint                   paper_height;
        GXPSPrintConverterFlags flags;
        guint                   upside_down_coords : 1;
};

struct _GXPSPrintConverterClass {
	GXPSConverterClass parent_class;
};

GType gxps_print_converter_get_type        (void);

void _gxps_converter_print_get_output_size (GXPSPrintConverter *converter,
                                            GXPSPage           *page,
                                            gdouble            *output_width,
                                            gdouble            *output_height);

G_END_DECLS

#endif /* __GXPS_PRINT_CONVERTER_H__ */
