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

#ifndef __GXPS_CONVERTER_H__
#define __GXPS_CONVERTER_H__

#include <glib-object.h>
#include <libgxps/gxps.h>

G_BEGIN_DECLS

#define GXPS_TYPE_CONVERTER           (gxps_converter_get_type ())
#define GXPS_CONVERTER(obj)           (G_TYPE_CHECK_INSTANCE_CAST (obj, GXPS_TYPE_CONVERTER, GXPSConverter))
#define GXPS_CONVERTER_CLASS(cls)     (G_TYPE_CHECK_CLASS_CAST (cls, GXPS_TYPE_CONVERTER, GXPSConverterClass))
#define GXPS_IS_CONVERTER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE (obj, GXPS_TYPE_CONVERTER))
#define GXPS_IS_CONVERTER_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE (obj, GXPS_TYPE_CONVERTER))
#define GXPS_CONVERTER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GXPS_TYPE_CONVERTER, GXPSConverterClass))

typedef struct _GXPSConverter        GXPSConverter;
typedef struct _GXPSConverterClass   GXPSConverterClass;

struct _GXPSConverter {
	GObject parent;

        GXPSDocument    *document;
        cairo_surface_t *surface;
        gchar           *input_filename;

        guint                 first_page;
        guint                 last_page;
        gdouble               x_resolution;
        gdouble               y_resolution;
        cairo_rectangle_int_t crop;
        guint                 only_odd  : 1;
        guint                 only_even : 1;
};

struct _GXPSConverterClass {
	GObjectClass parent_class;

        gboolean     (* init_with_args)  (GXPSConverter *converter,
                                          gint          *argc,
                                          gchar       ***argv,
                                          GList        **option_groups);

        void         (* begin_document)  (GXPSConverter *converter,
                                          const gchar   *output_filename,
                                          GXPSPage      *first_page);
        cairo_t     *(* begin_page)      (GXPSConverter *converter,
                                          GXPSPage      *page,
                                          guint          n_page);
        void         (* end_page)        (GXPSConverter *converter);
        void         (* end_document)    (GXPSConverter *converter);

        const gchar *(* get_extension)   (GXPSConverter *converter);
};

GType        gxps_converter_get_type        (void);

gboolean     gxps_converter_init_with_args  (GXPSConverter *converter,
                                             gint          *argc,
                                             gchar       ***argv);

const gchar *gxps_converter_get_extension   (GXPSConverter *converter);

void         gxps_converter_get_crop_size   (GXPSConverter *converter,
                                             gdouble        page_width,
                                             gdouble        page_height,
                                             gdouble       *output_width,
                                             gdouble       *output_height);

void         gxps_converter_run             (GXPSConverter *converter);

G_END_DECLS

#endif /* __GXPS_CONVERTER_H__ */
