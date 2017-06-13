/* GXPSConverter
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

#include "gxps-converter.h"
#include <gio/gio.h>
#include <math.h>

G_DEFINE_ABSTRACT_TYPE (GXPSConverter, gxps_converter, G_TYPE_OBJECT)

static guint document = 0;
static guint first_page = 0;
static guint last_page = 0;
static gboolean only_odd = FALSE;
static gboolean only_even = FALSE;
static gdouble resolution = 0.0;
static gdouble x_resolution = 150.0;
static gdouble y_resolution = 150.0;
static guint crop_x = 0.0;
static guint crop_y = 0.0;
static guint crop_width = 0.0;
static guint crop_height = 0.0;
static const char **file_arguments = NULL;

static const GOptionEntry options[] =
{
        { "document", 'd', 0, G_OPTION_ARG_INT, &document, "the XPS document to convert", "DOCUMENT" },
        { "first", 'f', 0, G_OPTION_ARG_INT, &first_page, "first page to convert", "PAGE" },
        { "last", 'l', 0, G_OPTION_ARG_INT, &last_page, "last page to convert", "PAGE" },
        { "odd", 'o', 0, G_OPTION_ARG_NONE, &only_odd, "convert only odd pages", NULL },
        { "even", 'e', 0, G_OPTION_ARG_NONE, &only_even, "convert only even pages", NULL },
        { "resolution", 'r', 0, G_OPTION_ARG_DOUBLE, &resolution, "resolution in PPI [default: 150]", "RESOLUTION" },
        { "rx", '\0', 0, G_OPTION_ARG_DOUBLE, &x_resolution, "X resolution in PPI [default: 150]", "X RESOLUTION" },
        { "ry", '\0', 0, G_OPTION_ARG_DOUBLE, &y_resolution, "Y resolution in PPI [default: 150]", "Y RESOLUTION" },
        { "crop-x", 'x', 0, G_OPTION_ARG_INT, &crop_x, "X coordinate of the crop area top left corner", "X" },
        { "crop-y", 'y', 0, G_OPTION_ARG_INT, &crop_y, "Y coordinate of the crop area top left corner", "Y" },
        { "crop-width", 'w', 0, G_OPTION_ARG_INT, &crop_width, "width of crop area in pixels", "WIDTH" },
        { "crop-height", 'h', 0, G_OPTION_ARG_INT, &crop_height, "height of crop area in pixels", "HEIGHT" },
        { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &file_arguments, NULL, "FILE [OUTPUT FILE]" },
        { NULL }
};

static gboolean
gxps_converter_real_init_with_args (GXPSConverter *converter,
                                    gint          *argc,
                                    gchar       ***argv,
                                    GList        **option_groups)
{
        GOptionContext *context;
        GFile          *file;
        GXPSFile       *xps;
        guint           n_pages;
        GList          *group;
        GError         *error = NULL;

        context = g_option_context_new (NULL);
        g_option_context_set_help_enabled (context, TRUE);
        g_option_context_add_main_entries (context, options, NULL);

        for (group = g_list_reverse (*option_groups); group; group = g_list_next (group))
                g_option_context_add_group (context, (GOptionGroup *)group->data);

        if (!g_option_context_parse (context, argc, argv, &error)) {
                g_printerr ("Error parsing arguments: %s\n", error->message);
                g_error_free (error);
                g_option_context_free (context);

                return FALSE;
        }

        if (!file_arguments) {
                gchar *help_text = g_option_context_get_help (context, TRUE, NULL);

                g_print ("%s", help_text);
                g_free (help_text);

                g_option_context_free (context);

                return FALSE;
        }
        g_option_context_free (context);

        file = g_file_new_for_commandline_arg (file_arguments[0]);
        converter->input_filename = g_file_get_path (file);
        xps = gxps_file_new (file, &error);
        g_object_unref (file);
        if (!xps) {
                g_printerr ("Error creating XPS file: %s\n", error->message);
                g_error_free (error);

                return FALSE;
        }

        document = CLAMP (document, 1, gxps_file_get_n_documents (xps));
        converter->document = gxps_file_get_document (xps, document - 1, &error);
        g_object_unref (xps);
        if (!converter->document) {
                g_printerr ("Error getting document %d: %s\n", document, error->message);
                g_error_free (error);

                return FALSE;
        }

        n_pages = gxps_document_get_n_pages (converter->document);
        converter->first_page = MAX (first_page, 1);
        converter->last_page = last_page < 1 ? n_pages : MIN(last_page, n_pages);
        converter->only_odd = only_odd;
        converter->only_even = only_even;
        if (resolution != 0.0 && (x_resolution == 150.0 || y_resolution == 150.0)) {
                converter->x_resolution = resolution;
                converter->y_resolution = resolution;
        } else {
                converter->x_resolution = x_resolution;
                converter->y_resolution = y_resolution;
        }
        converter->crop.x = crop_x;
        converter->crop.y = crop_y;
        converter->crop.width = crop_width;
        converter->crop.height = crop_height;

        return TRUE;
}

static void
gxps_converter_begin_document (GXPSConverter *converter,
                               const gchar   *output_filename,
                               GXPSPage      *first_page)
{
        GXPSConverterClass *converter_class;

        g_return_if_fail (GXPS_IS_CONVERTER (converter));
        g_return_if_fail (GXPS_IS_PAGE (first_page));

        converter_class = GXPS_CONVERTER_GET_CLASS (converter);
        if (converter_class->begin_document)
                converter_class->begin_document (converter, output_filename, first_page);
}

static cairo_t *
gxps_converter_begin_page (GXPSConverter *converter,
                           GXPSPage      *page,
                           guint          n_page)
{
        g_return_val_if_fail (GXPS_IS_CONVERTER (converter), NULL);
        g_return_val_if_fail (GXPS_IS_PAGE (page), NULL);

        return GXPS_CONVERTER_GET_CLASS (converter)->begin_page (converter, page, n_page);
}

static void
gxps_converter_end_page (GXPSConverter *converter)
{
        GXPSConverterClass *converter_class;

        g_return_if_fail (GXPS_IS_CONVERTER (converter));

        converter_class = GXPS_CONVERTER_GET_CLASS (converter);
        if (converter_class->end_page)
                converter_class->end_page (converter);
}

static void
gxps_converter_end_document (GXPSConverter *converter)
{
        GXPSConverterClass *converter_class;

        g_return_if_fail (GXPS_IS_CONVERTER (converter));

        converter_class = GXPS_CONVERTER_GET_CLASS (converter);
        if (converter_class->end_document)
                converter_class->end_document (converter);
}

static void
gxps_converter_finalize (GObject *object)
{
        GXPSConverter *converter = GXPS_CONVERTER (object);

        g_clear_object (&converter->document);
        g_clear_object (&converter->surface);
        g_clear_pointer (&converter->input_filename, g_free);

        G_OBJECT_CLASS (gxps_converter_parent_class)->finalize (object);
}

static void
gxps_converter_init (GXPSConverter *converter)
{
}

static void
gxps_converter_class_init (GXPSConverterClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        klass->init_with_args = gxps_converter_real_init_with_args;

        object_class->finalize = gxps_converter_finalize;
}

gboolean
gxps_converter_init_with_args (GXPSConverter *converter,
                               gint          *argc,
                               gchar       ***argv)
{
        GList   *option_groups = NULL;
        gboolean retval;

        g_return_val_if_fail (GXPS_IS_CONVERTER (converter), FALSE);

        retval = GXPS_CONVERTER_GET_CLASS (converter)->init_with_args (converter, argc, argv, &option_groups);
        /* Groups are owned by the option context */
        g_list_free (option_groups);

        return retval;
}

const gchar *
gxps_converter_get_extension (GXPSConverter *converter)
{
        g_return_val_if_fail (GXPS_IS_CONVERTER (converter), NULL);

        return GXPS_CONVERTER_GET_CLASS (converter)->get_extension (converter);
}

void
gxps_converter_get_crop_size (GXPSConverter *converter,
                              gdouble        page_width,
                              gdouble        page_height,
                              gdouble       *output_width,
                              gdouble       *output_height)
{
        guint width, height;

        g_return_if_fail (GXPS_IS_CONVERTER (converter));

        width = converter->crop.width == 0 ? (int)ceil (page_width) : converter->crop.width;
        height = converter->crop.height == 0 ? (int)ceil (page_height) : converter->crop.height;

        if (output_width) {
                *output_width = (converter->crop.x + width > page_width ?
                                 (int)ceil (page_width - converter->crop.x) : width);
        }
        if (output_height) {
                *output_height = (converter->crop.y + height > page_height ?
                                  (int)ceil (page_height - converter->crop.y) : height);
        }
}

void
gxps_converter_run (GXPSConverter *converter)
{
        guint i;
        guint first_page;

        g_return_if_fail (GXPS_IS_CONVERTER (converter));

        first_page = converter->first_page;
        /* Make sure first_page is always used so that
         * gxps_converter_begin_document() is called
         */
        if ((converter->only_even && first_page % 2 == 0) ||
            (converter->only_odd && first_page % 2 == 1))
                first_page++;

        for (i = first_page; i <= converter->last_page; i++) {
                GXPSPage *page;
                cairo_t  *cr;
                GError   *error;

                if (converter->only_even && i % 2 == 0)
                        continue;
                if (converter->only_odd && i % 2 == 1)
                        continue;

                error = NULL;
                page = gxps_document_get_page (converter->document, i - 1, &error);
                if (!page) {
                        g_printerr ("Error getting page %d: %s\n", i, error->message);
                        g_error_free (error);

                        continue;
                }

                if (i == first_page) {
                        gchar *output_filename = NULL;

                        if (file_arguments[1]) {
                                GFile *file;

                                file = g_file_new_for_commandline_arg (file_arguments[1]);
                                output_filename = g_file_get_path (file);
                                g_object_unref (file);
                        }

                        gxps_converter_begin_document (converter, output_filename, page);
                        g_free (output_filename);
                }

                cr = gxps_converter_begin_page (converter, page, i);

                error = NULL;
                gxps_page_render (page, cr, &error);
                if (error) {
                        g_printerr ("Error rendering page %d: %s\n", i, error->message);
                        g_error_free (error);
                }
                cairo_destroy (cr);

                gxps_converter_end_page (converter);

                g_object_unref (page);
        }

        gxps_converter_end_document (converter);
}
