/* GXPSPdfConverter
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

#include "gxps-pdf-converter.h"
#include <libgxps/gxps.h>
#include <cairo-pdf.h>
#include <string.h>

struct _GXPSPdfConverter {
	GXPSPrintConverter parent;
};

struct _GXPSPdfConverterClass {
	GXPSPrintConverterClass parent_class;
};

G_DEFINE_TYPE (GXPSPdfConverter, gxps_pdf_converter, GXPS_TYPE_PRINT_CONVERTER)

static const gchar *
gxps_pdf_converter_get_extension (GXPSConverter *converter)
{
        return "pdf";
}

static void
gxps_pdf_converter_begin_document (GXPSConverter *converter,
                                   const gchar   *output_filename,
                                   GXPSPage      *first_page)
{
        GXPSPrintConverter *print_converter = GXPS_PRINT_CONVERTER (converter);
        gdouble             width, height;

        GXPS_CONVERTER_CLASS (gxps_pdf_converter_parent_class)->begin_document (converter, output_filename, first_page);

        _gxps_converter_print_get_output_size (print_converter, first_page, &width, &height);
        converter->surface = cairo_pdf_surface_create (print_converter->filename, width, height);
}

static cairo_t *
gxps_pdf_converter_begin_page (GXPSConverter *converter,
                               GXPSPage      *page,
                               guint          n_page)
{
        GXPSPrintConverter *print_converter = GXPS_PRINT_CONVERTER (converter);
        gdouble             width, height;

        g_return_val_if_fail (converter->surface != NULL, NULL);

        _gxps_converter_print_get_output_size (print_converter, page, &width, &height);
        cairo_pdf_surface_set_size (converter->surface, width, height);

        return GXPS_CONVERTER_CLASS (gxps_pdf_converter_parent_class)->begin_page (converter, page, n_page);
}

static void
gxps_pdf_converter_init (GXPSPdfConverter *converter)
{
}

static void
gxps_pdf_converter_class_init (GXPSPdfConverterClass *klass)
{
        GXPSConverterClass *converter_class = GXPS_CONVERTER_CLASS (klass);

        converter_class->get_extension = gxps_pdf_converter_get_extension;
        converter_class->begin_document = gxps_pdf_converter_begin_document;
        converter_class->begin_page = gxps_pdf_converter_begin_page;
}
