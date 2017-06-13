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

#include <config.h>

#include "gxps-image-converter.h"
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <libgxps/gxps.h>

G_DEFINE_ABSTRACT_TYPE (GXPSImageConverter, gxps_image_converter, GXPS_TYPE_CONVERTER)

static guint
get_n_digits (GXPSDocument *document)
{
        guint n_pages = gxps_document_get_n_pages (document);
        guint retval = 0;

        while (n_pages >= 10) {
                n_pages /= 10;
                retval++;
        }

        return retval + 1;
}

static void
gxps_converter_image_converter_begin_document (GXPSConverter *converter,
                                               const gchar   *output_filename,
                                               GXPSPage      *first_page)
{
        GXPSImageConverter *image_converter = GXPS_IMAGE_CONVERTER (converter);

        image_converter->page_prefix = g_strdup (output_filename ? output_filename : "page");
        image_converter->n_digits = get_n_digits (converter->document);
}

static cairo_t *
gxps_converter_image_converter_begin_page (GXPSConverter *converter,
                                           GXPSPage      *page,
                                           guint          n_page)
{
        GXPSImageConverter *image_converter = GXPS_IMAGE_CONVERTER (converter);
        gdouble             page_width, page_height;
        gdouble             output_width, output_height;
        cairo_t            *cr;

        g_return_val_if_fail (converter->surface == NULL, NULL);

        image_converter->current_page = n_page;

        gxps_page_get_size (page, &page_width, &page_height);
        gxps_converter_get_crop_size (converter,
                                      page_width * (converter->x_resolution / 96.0),
                                      page_height * (converter->y_resolution / 96.0),
                                      &output_width, &output_height);
        converter->surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                                         ceil (output_width),
                                                         ceil (output_height));

        cr = cairo_create (converter->surface);

        if (image_converter->fill_background) {
                cairo_save (cr);
                cairo_set_source_rgb (cr, 1, 1, 1);
                cairo_paint (cr);
                cairo_restore (cr);
        }

        cairo_translate (cr, -converter->crop.x, -converter->crop.y);
        cairo_scale (cr, converter->x_resolution / 96.0, converter->y_resolution / 96.0);

        return cr;
}

static void
gxps_converter_image_converter_end_page (GXPSConverter *converter)
{
        GXPSImageConverter *image_converter = GXPS_IMAGE_CONVERTER (converter);
        cairo_status_t      status;
        const gchar        *extension = gxps_converter_get_extension (converter);
        gchar              *page_filename;
        FILE               *fd;
        guint               width, height;
        gint                stride;
        guchar             *data;
        gint                y;

        g_return_if_fail (converter->surface != NULL);
        g_return_if_fail (GXPS_IS_IMAGE_WRITER (image_converter->writer));

        width = cairo_image_surface_get_width (converter->surface);
        height = cairo_image_surface_get_height (converter->surface);
        stride = cairo_image_surface_get_stride (converter->surface);
        data = cairo_image_surface_get_data (converter->surface);

        page_filename = g_strdup_printf ("%s-%0*d.%s",
                                         image_converter->page_prefix,
                                         image_converter->n_digits,
                                         image_converter->current_page,
                                         extension);

        fd = fopen (page_filename, "wb");
        if (!fd) {
                g_printerr ("Error opening output file %s\n", page_filename);
                g_free (page_filename);

                cairo_surface_destroy (converter->surface);
                converter->surface = NULL;

                return;
        }
        if (!gxps_image_writer_init (image_converter->writer, fd, width, height,
                                     converter->x_resolution, converter->y_resolution)) {
                g_printerr ("Error writing %s\n", page_filename);
                g_free (page_filename);
                fclose (fd);

                cairo_surface_destroy (converter->surface);
                converter->surface = NULL;

                return;
        }

        for (y = 0; y < height; y++)
                gxps_image_writer_write (image_converter->writer, data + y * stride);

        gxps_image_writer_finish (image_converter->writer);
        fclose (fd);
        g_free (page_filename);

        cairo_surface_finish (converter->surface);
        status = cairo_surface_status (converter->surface);
        if (status)
                g_printerr ("Cairo error: %s\n", cairo_status_to_string (status));
        cairo_surface_destroy (converter->surface);
        converter->surface = NULL;
}

static void
gxps_image_converter_finalize (GObject *object)
{
        GXPSImageConverter *converter = GXPS_IMAGE_CONVERTER (object);

        g_clear_pointer (&converter->page_prefix, g_free);
        g_clear_object (&converter->writer);

        G_OBJECT_CLASS (gxps_image_converter_parent_class)->finalize (object);
}

static void
gxps_image_converter_init (GXPSImageConverter *converter)
{
        GXPSImageConverter *image_converter = GXPS_IMAGE_CONVERTER (converter);

        image_converter->fill_background = TRUE;
}

static void
gxps_image_converter_class_init (GXPSImageConverterClass *klass)
{
        GObjectClass       *object_class = G_OBJECT_CLASS (klass);
        GXPSConverterClass *converter_class = GXPS_CONVERTER_CLASS (klass);

        object_class->finalize = gxps_image_converter_finalize;

        converter_class->begin_document = gxps_converter_image_converter_begin_document;
        converter_class->begin_page = gxps_converter_image_converter_begin_page;
        converter_class->end_page = gxps_converter_image_converter_end_page;
}

