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

#include "gxps-ps-converter.h"
#include <libgxps/gxps.h>
#include <cairo-ps.h>
#include <string.h>

struct _GXPSPsConverter {
	GXPSPrintConverter parent;

        cairo_ps_level_t level;
        guint            eps    : 1;
        guint            duplex : 1;
};

struct _GXPSPsConverterClass {
	GXPSPrintConverterClass parent_class;
};

G_DEFINE_TYPE (GXPSPsConverter, gxps_ps_converter, GXPS_TYPE_PRINT_CONVERTER)

typedef struct _GXPSPaperSize {
        const gchar *name;
        guint width;
        guint height;
} GXPSPaperSize;

static const GXPSPaperSize paper_sizes[] =
{
        { "match",       0,    0 },
        { "A0",       2384, 3371 },
        { "A1",       1685, 2384 },
        { "A2",       1190, 1684 },
        { "A3",        842, 1190 },
        { "A4",        595,  842 },
        { "A5",        420,  595 },
        { "B4",        729, 1032 },
        { "B5",        516,  729 },
        { "Letter",    612,  792 },
        { "Tabloid",   792, 1224 },
        { "Ledger",   1224,  792 },
        { "Legal",     612, 1008 },
        { "Statement", 396,  612 },
        { "Executive", 540,  720 },
        { "Folio",     612,  936 },
        { "Quarto",    610,  780 },
        { "10x14",     720, 1008 },
};

static gboolean level2 = FALSE;
static gboolean level3 = FALSE;
static gboolean eps = FALSE;
static gboolean duplex = FALSE;
static gchar *paper = NULL;

static const GOptionEntry options[] =
{
        { "level2", '\0', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &level2, "generate Level 2 PostScript", NULL },
        { "level3", '\0', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &level3, "generate Level 3 PostScript", NULL },
        { "eps", '\0', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &eps, "generate Encapsulated PostScript", NULL },
        { "paper", '\0', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_STRING, &paper, "paper size (match, letter, legal, A4, A3, ...)", "PAPER" },
        { "duplex", '\0', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &duplex, "enable duplex printing", NULL },
        { NULL }
};

static gboolean
gxps_ps_converter_init_with_args (GXPSConverter *converter,
                                  gint          *argc,
                                  gchar       ***argv,
                                  GList        **option_groups)
{
        GXPSPsConverter    *ps_converter = GXPS_PS_CONVERTER (converter);
        GXPSPrintConverter *print_converter = GXPS_PRINT_CONVERTER (converter);
        GOptionContext     *context;
        GOptionGroup       *option_group;
        guint               i;
        GError             *error = NULL;

        option_group = g_option_group_new ("postscrit", "PostScript Options", "Show PostScript Options", NULL, NULL);
        g_option_group_add_entries (option_group, options);

        *option_groups = g_list_prepend (*option_groups, option_group);

        if (GXPS_CONVERTER_CLASS (gxps_ps_converter_parent_class)->init_with_args) {
                if (!GXPS_CONVERTER_CLASS (gxps_ps_converter_parent_class)->init_with_args (converter, argc, argv, option_groups))
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

        ps_converter->level = (level2 && !level3) ? CAIRO_PS_LEVEL_2 : CAIRO_PS_LEVEL_3;
        ps_converter->eps = eps;
        ps_converter->duplex = duplex;
        for (i = 0; paper && i < G_N_ELEMENTS (paper_sizes); i++) {
                if (g_ascii_strcasecmp (paper, paper_sizes[i].name) == 0) {
                        print_converter->paper_width = paper_sizes[i].width;
                        print_converter->paper_height = paper_sizes[i].height;
                        break;
                }
        }

        g_print ("DBG: paper size: %s %d, %d\n", paper, print_converter->paper_width, print_converter->paper_height);

        return TRUE;
}

static const gchar *
gxps_ps_converter_get_extension (GXPSConverter *converter)
{
        return "ps";
}

static void
gxps_ps_converter_begin_document (GXPSConverter *converter,
                                  const gchar   *output_filename,
                                  GXPSPage      *first_page)
{
        GXPSPsConverter    *ps_converter = GXPS_PS_CONVERTER (converter);
        GXPSPrintConverter *print_converter = GXPS_PRINT_CONVERTER (converter);
        gdouble             width, height;

        GXPS_CONVERTER_CLASS (gxps_ps_converter_parent_class)->begin_document (converter, output_filename, first_page);

        _gxps_converter_print_get_output_size (print_converter, first_page, &width, &height);
        converter->surface = cairo_ps_surface_create (print_converter->filename, width, height);
        if (ps_converter->level == CAIRO_PS_LEVEL_2)
                cairo_ps_surface_restrict_to_level (converter->surface, ps_converter->level);
        if (ps_converter->eps)
                cairo_ps_surface_set_eps (converter->surface, 1);
        if (ps_converter->duplex) {
                cairo_ps_surface_dsc_comment (converter->surface, "%%Requirements: duplex");
                cairo_ps_surface_dsc_begin_setup (converter->surface);
                cairo_ps_surface_dsc_comment (converter->surface, "%%IncludeFeature: *Duplex DuplexNoTumble");
        }
        cairo_ps_surface_dsc_begin_page_setup (converter->surface);
}

static cairo_t *
gxps_ps_converter_begin_page (GXPSConverter *converter,
                               GXPSPage      *page,
                               guint          n_page)
{
        GXPSPrintConverter *print_converter = GXPS_PRINT_CONVERTER (converter);
        gdouble             width, height;

        g_return_val_if_fail (converter->surface != NULL, NULL);

        _gxps_converter_print_get_output_size (print_converter, page, &width, &height);
        if (width > height) {
                cairo_ps_surface_dsc_comment (converter->surface, "%%PageOrientation: Landscape");
                cairo_ps_surface_set_size (converter->surface, height, width);
        } else {
                cairo_ps_surface_dsc_comment (converter->surface, "%%PageOrientation: Portrait");
                cairo_ps_surface_set_size (converter->surface, width, height);
        }

        return GXPS_CONVERTER_CLASS (gxps_ps_converter_parent_class)->begin_page (converter, page, n_page);
}

static void
gxps_ps_converter_init (GXPSPsConverter *converter)
{
}

static void
gxps_ps_converter_class_init (GXPSPsConverterClass *klass)
{
        GXPSConverterClass *converter_class = GXPS_CONVERTER_CLASS (klass);

        converter_class->init_with_args = gxps_ps_converter_init_with_args;
        converter_class->get_extension = gxps_ps_converter_get_extension;
        converter_class->begin_document = gxps_ps_converter_begin_document;
        converter_class->begin_page = gxps_ps_converter_begin_page;
}
