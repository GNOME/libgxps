/* GXPSPngConverter
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

#include "gxps-png-converter.h"
#include "gxps-png-writer.h"
#include <math.h>
#include <libgxps/gxps.h>

struct _GXPSPngConverter {
	GXPSImageConverter parent;

        guint bg_transparent : 1;
};

struct _GXPSPngConverterClass {
	GXPSImageConverterClass parent_class;
};

G_DEFINE_TYPE (GXPSPngConverter, gxps_png_converter, GXPS_TYPE_IMAGE_CONVERTER)

static gboolean bg_transparent = FALSE;

static const GOptionEntry options[] =
{
        { "transparent-bg", 't', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &bg_transparent, "use a transparent background instead of white", NULL },
        { NULL }
};

static gboolean
gxps_png_converter_init_with_args (GXPSConverter *converter,
                                   gint          *argc,
                                   gchar       ***argv,
                                   GList        **option_groups)
{
        GXPSPngConverter *png_converter = GXPS_PNG_CONVERTER (converter);
        GOptionContext   *context;
        GOptionGroup     *option_group;
        GError           *error = NULL;

        option_group = g_option_group_new ("png", "PNG Options", "Show PNG Options", NULL, NULL);
        g_option_group_add_entries (option_group, options);

        *option_groups = g_list_prepend (*option_groups, option_group);

        if (GXPS_CONVERTER_CLASS (gxps_png_converter_parent_class)->init_with_args) {
                if (!GXPS_CONVERTER_CLASS (gxps_png_converter_parent_class)->init_with_args (converter, argc, argv, option_groups))
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

        png_converter->bg_transparent = bg_transparent;

        return TRUE;
}

static const gchar *
gxps_png_converter_get_extension (GXPSConverter *converter)
{
        return "png";
}

static void
gxps_png_converter_begin_document (GXPSConverter *converter,
                                   const gchar   *output_filename,
                                   GXPSPage      *first_page)
{
        GXPSPngConverter   *png_converter = GXPS_PNG_CONVERTER (converter);
        GXPSImageConverter *image_converter = GXPS_IMAGE_CONVERTER (converter);

        image_converter->fill_background = !png_converter->bg_transparent;
        GXPS_CONVERTER_CLASS (gxps_png_converter_parent_class)->begin_document (converter, output_filename, first_page);
}

static void
gxps_png_converter_end_page (GXPSConverter *converter)
{
        GXPSImageConverter *image_converter = GXPS_IMAGE_CONVERTER (converter);
        GXPSPngConverter   *png_converter = GXPS_PNG_CONVERTER (converter);

        if (!image_converter->writer) {
                GXPSPngFormat format = png_converter->bg_transparent ? GXPS_PNG_FORMAT_RGBA : GXPS_PNG_FORMAT_RGB;

                image_converter->writer = gxps_png_writer_new (format);
        }

        GXPS_CONVERTER_CLASS (gxps_png_converter_parent_class)->end_page (converter);
}

static void
gxps_png_converter_init (GXPSPngConverter *converter)
{
}

static void
gxps_png_converter_class_init (GXPSPngConverterClass *klass)
{
        GXPSConverterClass *converter_class = GXPS_CONVERTER_CLASS (klass);

        converter_class->init_with_args = gxps_png_converter_init_with_args;
        converter_class->get_extension = gxps_png_converter_get_extension;
        converter_class->begin_document = gxps_png_converter_begin_document;
        converter_class->end_page = gxps_png_converter_end_page;
}
