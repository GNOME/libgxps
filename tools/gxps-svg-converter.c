/* GXPSSvgConverter
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

#include "gxps-svg-converter.h"
#include <libgxps/gxps.h>
#include <cairo-svg.h>
#include <string.h>

struct _GXPSSvgConverter {
	GXPSPrintConverter parent;
};

struct _GXPSSvgConverterClass {
	GXPSPrintConverterClass parent_class;
};

G_DEFINE_TYPE (GXPSSvgConverter, gxps_svg_converter, GXPS_TYPE_PRINT_CONVERTER)

static const gchar *
gxps_svg_converter_get_extension (GXPSConverter *converter)
{
        return "svg";
}

static void
gxps_svg_converter_begin_document (GXPSConverter *converter,
                                   const gchar   *output_filename,
                                   GXPSPage      *first_page)
{
        GXPSPrintConverter *print_converter = GXPS_PRINT_CONVERTER (converter);
        gdouble             width, height;

        GXPS_CONVERTER_CLASS (gxps_svg_converter_parent_class)->begin_document (converter, output_filename, first_page);

        _gxps_converter_print_get_output_size (print_converter, first_page, &width, &height);
        converter->surface = cairo_svg_surface_create (print_converter->filename, width, height);
        cairo_svg_surface_restrict_to_version (converter->surface, CAIRO_SVG_VERSION_1_2);
}

static void
gxps_svg_converter_init (GXPSSvgConverter *converter)
{
        GXPSPrintConverter *print_converter = GXPS_PRINT_CONVERTER (converter);

        print_converter->upside_down_coords = TRUE;
}

static void
gxps_svg_converter_class_init (GXPSSvgConverterClass *klass)
{
        GXPSConverterClass *converter_class = GXPS_CONVERTER_CLASS (klass);

        converter_class->get_extension = gxps_svg_converter_get_extension;
        converter_class->begin_document = gxps_svg_converter_begin_document;
}
