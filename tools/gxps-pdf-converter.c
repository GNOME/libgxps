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
#include <stdio.h>

#ifdef G_OS_WIN32
#include <fcntl.h> /* for _O_BINARY */
#endif

struct _GXPSPdfConverter {
	GXPSPrintConverter parent;
};

struct _GXPSPdfConverterClass {
	GXPSPrintConverterClass parent_class;
};

G_DEFINE_TYPE (GXPSPdfConverter, gxps_pdf_converter, GXPS_TYPE_PRINT_CONVERTER)

static gboolean write_to_stdout = FALSE;

static const GOptionEntry options[] =
{
        { "stdout", '\0', 0, G_OPTION_ARG_NONE, &write_to_stdout, "Writes output to stdout", NULL },
        { NULL }
};

static gboolean
gxps_pdf_converter_init_with_args (GXPSConverter *converter,
                                   gint          *argc,
                                   gchar       ***argv,
                                   GList        **option_groups)
{
        GOptionContext *context;
        GOptionGroup   *option_group;
        GError         *error = NULL;

        option_group = g_option_group_new ("output", "Output Options", "Show Output Options", NULL, NULL);
        g_option_group_add_entries (option_group, options);

        *option_groups = g_list_prepend (*option_groups, option_group);

        if (GXPS_CONVERTER_CLASS (gxps_pdf_converter_parent_class)->init_with_args) {
                if (!GXPS_CONVERTER_CLASS (gxps_pdf_converter_parent_class)->init_with_args (converter, argc, argv, option_groups))
                        return FALSE;
        }

        context = g_option_context_new (NULL);
        g_option_context_set_ignore_unknown_options (context, TRUE);
        g_option_context_set_help_enabled (context, FALSE);
        g_option_context_add_main_entries (context, options, NULL);
        if (!g_option_context_parse (context, argc, argv, &error)) {
                g_printerr ("Error parsing arguments: %s\n", error->message);
                g_error_free (error);
                g_option_context_free (context);

                return FALSE;
        }
        g_option_context_free (context);

        return TRUE;
}

static const gchar *
gxps_pdf_converter_get_extension (GXPSConverter *converter)
{
        return "pdf";
}

static cairo_status_t
stdout_write_func (void                *closure,
                   const unsigned char *data,
                   unsigned int         length)
{
        size_t res;

        res = fwrite (data, length, 1, stdout);
        fflush (stdout);

        return res != 0 ? CAIRO_STATUS_SUCCESS : CAIRO_STATUS_WRITE_ERROR;
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

        if (write_to_stdout) {
#ifdef G_OS_WIN32
                /* Force binary mode on Windows to make sure that line feed character is NOT replaced with a carriage return-line feed pair */
                _setmode (fileno(stdout), _O_BINARY);
#endif
                converter->surface = cairo_pdf_surface_create_for_stream (stdout_write_func, converter, width, height);
        } else {
                converter->surface = cairo_pdf_surface_create (print_converter->filename, width, height);
        }
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

        converter_class->init_with_args = gxps_pdf_converter_init_with_args;
        converter_class->get_extension = gxps_pdf_converter_get_extension;
        converter_class->begin_document = gxps_pdf_converter_begin_document;
        converter_class->begin_page = gxps_pdf_converter_begin_page;
}
