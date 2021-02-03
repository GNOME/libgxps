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

#include <config.h>

#include "gxps-print-converter.h"
#include <string.h>

G_DEFINE_ABSTRACT_TYPE (GXPSPrintConverter, gxps_print_converter, GXPS_TYPE_CONVERTER)

static guint paper_width = 0;
static guint paper_height = 0;
static gboolean expand = FALSE;
static gboolean no_shrink = FALSE;
static gboolean no_center = FALSE;

static const GOptionEntry options[] =
{
        { "paper-width", '\0', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_INT, &paper_width, "paper width, in points", "WIDTH" },
        { "paper-height", '\0', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_INT, &paper_height, "paper height, in points", "HEIGHT" },
        { "expand", '\0', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &expand, "expand pages smaller than the paper size", NULL },
        { "no-shrink", '\0', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &no_shrink, "don't shrink pages larger than the paper size", NULL },
        { "no-center", '\0', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &no_center, "don't center pages smaller than the paper size", NULL },
        { NULL }
};

static gboolean
gxps_print_converter_init_with_args (GXPSConverter *converter,
                                     gint          *argc,
                                     gchar       ***argv,
                                     GList        **option_groups)
{
        GXPSPrintConverter *print_converter = GXPS_PRINT_CONVERTER (converter);
        GOptionContext     *context;
        GOptionGroup       *option_group;
        GError             *error = NULL;

        option_group = g_option_group_new ("printing", "Printing Options", "Show Printing Options", NULL, NULL);
        g_option_group_add_entries (option_group, options);

        *option_groups = g_list_prepend (*option_groups, option_group);

        if (GXPS_CONVERTER_CLASS (gxps_print_converter_parent_class)->init_with_args) {
                if (!GXPS_CONVERTER_CLASS (gxps_print_converter_parent_class)->init_with_args (converter, argc, argv, option_groups))
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

        print_converter->paper_width = paper_width;
        print_converter->paper_height = paper_height;

        print_converter->flags = GXPS_PRINT_CONVERTER_SHRINK | GXPS_PRINT_CONVERTER_CENTER;
        if (expand)
                print_converter->flags |= GXPS_PRINT_CONVERTER_EXPAND;
        if (no_shrink)
                print_converter->flags &= ~GXPS_PRINT_CONVERTER_SHRINK;
        if (no_center)
                print_converter->flags &= ~GXPS_PRINT_CONVERTER_CENTER;

        return TRUE;
}


static void
gxps_converter_print_converter_begin_document (GXPSConverter *converter,
                                               const gchar   *output_filename,
                                               GXPSPage      *first_page)
{
        GXPSPrintConverter *print_converter = GXPS_PRINT_CONVERTER (converter);
        gchar              *basename;
        gchar              *basename_lower;
        const gchar        *ext;

        if (output_filename) {
                print_converter->filename = g_strdup (output_filename);
                return;
        }

        basename = g_path_get_basename (converter->input_filename);
        basename_lower = g_ascii_strdown (basename, -1);
        ext = g_strrstr (basename_lower, ".xps");

        if (ext) {
                gchar *name;

                name = g_strndup (basename, strlen (basename) - strlen (ext));
                print_converter->filename = g_strdup_printf ("%s.%s", name,
                                                             gxps_converter_get_extension (converter));
                g_free (name);
        } else {
                print_converter->filename = g_strdup_printf ("%s.%s", basename,
                                                             gxps_converter_get_extension (converter));
        }

        g_free (basename_lower);
        g_free (basename);
}

static void
gxps_converter_print_get_fit_to_page_transform (GXPSPrintConverter *print_converter,
                                                gdouble             page_width,
                                                gdouble             page_height,
                                                gdouble             paper_width,
                                                gdouble             paper_height,
                                                cairo_matrix_t     *matrix)
{
        gdouble x_scale, y_scale;
        gdouble scale;

        x_scale = paper_width / page_width;
        y_scale = paper_height / page_height;
        scale = (x_scale < y_scale) ? x_scale : y_scale;

        cairo_matrix_init_identity (matrix);
        if (scale > 1.0) {
                /* Page is smaller than paper */
                if (print_converter->flags & GXPS_PRINT_CONVERTER_EXPAND) {
                        cairo_matrix_scale (matrix, scale, scale);
                } else if (print_converter->flags & GXPS_PRINT_CONVERTER_CENTER) {
                        cairo_matrix_translate (matrix,
                                                (paper_width - page_width) / 2,
                                                (paper_height - page_height) / 2);
                } else {
                        if (!print_converter->upside_down_coords) {
                                /* Move to PostScript origin */
                                cairo_matrix_translate (matrix, 0, (paper_height - page_height));
                        }
                }
        } else if (scale < 1.0) {
                /* Page is larger than paper */
                if (print_converter->flags & GXPS_PRINT_CONVERTER_SHRINK)
                        cairo_matrix_scale (matrix, scale, scale);
        }
}

static cairo_t *
gxps_converter_print_converter_begin_page (GXPSConverter *converter,
                                           GXPSPage      *page,
                                           guint          n_page)
{
        GXPSPrintConverter *print_converter = GXPS_PRINT_CONVERTER (converter);
        gdouble             page_width, page_height;
        gdouble             cropped_width, cropped_height;
        gdouble             output_width, output_height;
        cairo_matrix_t      matrix;
        cairo_t            *cr;

        g_return_val_if_fail (converter->surface != NULL, NULL);

        cairo_surface_set_fallback_resolution (converter->surface,
                                               converter->x_resolution,
                                               converter->y_resolution);
        cr = cairo_create (converter->surface);
        cairo_translate (cr, -converter->crop.x, -converter->crop.y);

        gxps_page_get_size (page, &page_width, &page_height);
        gxps_converter_get_crop_size (converter,
                                      page_width, page_height,
                                      &cropped_width, &cropped_height);
        _gxps_converter_print_get_output_size (print_converter, page,
                                               &output_width, &output_height);
        gxps_converter_print_get_fit_to_page_transform (print_converter,
                                                        cropped_width, cropped_height,
                                                        output_width, output_height,
                                                        &matrix);
        cairo_transform (cr, &matrix);
        cairo_rectangle (cr, converter->crop.x, converter->crop.y, cropped_width, cropped_height);
        cairo_clip (cr);

        return cr;
}

static void
gxps_converter_print_converter_end_page (GXPSConverter *converter)
{
        g_return_if_fail (converter->surface != NULL);

        cairo_surface_show_page (converter->surface);
}

static void
gxps_converter_print_converter_end_document (GXPSConverter *converter)
{
        GXPSPrintConverter *print_converter = GXPS_PRINT_CONVERTER (converter);

        if (converter->surface) {
                cairo_status_t status;

                cairo_surface_finish (converter->surface);
                status = cairo_surface_status (converter->surface);
                if (status)
                        g_printerr ("Cairo error: %s\n", cairo_status_to_string (status));
                cairo_surface_destroy (converter->surface);
                converter->surface = NULL;
        }

        g_free (print_converter->filename);
        print_converter->filename = NULL;
}

static void
gxps_print_converter_finalize (GObject *object)
{
        GXPSPrintConverter *print_converter = GXPS_PRINT_CONVERTER (object);

        if (print_converter->filename) {
                g_free (print_converter->filename);
                print_converter->filename = NULL;
        }

        G_OBJECT_CLASS (gxps_print_converter_parent_class)->finalize (object);
}

static void
gxps_print_converter_init (GXPSPrintConverter *converter)
{
}

static void
gxps_print_converter_class_init (GXPSPrintConverterClass *klass)
{
        GObjectClass       *object_class = G_OBJECT_CLASS (klass);
        GXPSConverterClass *converter_class = GXPS_CONVERTER_CLASS (klass);

        object_class->finalize = gxps_print_converter_finalize;

        converter_class->init_with_args = gxps_print_converter_init_with_args;
        converter_class->begin_document = gxps_converter_print_converter_begin_document;
        converter_class->begin_page = gxps_converter_print_converter_begin_page;
        converter_class->end_page = gxps_converter_print_converter_end_page;
        converter_class->end_document = gxps_converter_print_converter_end_document;
}

void
_gxps_converter_print_get_output_size (GXPSPrintConverter *converter,
                                       GXPSPage           *page,
                                       gdouble            *output_width,
                                       gdouble            *output_height)
{
        gdouble page_width, page_height;

        gxps_page_get_size (page, &page_width, &page_height);

        /* The page width is in points, Windows expects a dpi of 96 while
         * cairo will handle the dpi in 72. We need to make the conversion
         * ourselves so we have the right output size
         */
        if (output_width) {
                *output_width = converter->paper_width == 0 ?
                        page_width * 72.0 / 96.0 : converter->paper_width;
        }

        if (output_height) {
                *output_height = converter->paper_height == 0 ?
                        page_height * 72.0 / 96.0 : converter->paper_height;
        }
}
