/* GXPSGlyphs
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

#ifndef __GXPS_GLYPHS_H__
#define __GXPS_GLYPHS_H__

#include <cairo.h>
#include "gxps-page-private.h"

G_BEGIN_DECLS

typedef struct _GXPSGlyphs GXPSGlyphs;

struct _GXPSGlyphs {
        GXPSRenderContext *ctx;

        gdouble            em_size;
        gchar             *font_uri;
        gdouble            origin_x;
        gdouble            origin_y;
        cairo_pattern_t   *fill_pattern;
        gchar             *text;
        gchar             *indices;
        gchar             *clip_data;
        gint               bidi_level;
        gdouble            opacity;
        cairo_pattern_t   *opacity_mask;
        guint              is_sideways : 1;
        guint              italic      : 1;
};

GXPSGlyphs *gxps_glyphs_new             (GXPSRenderContext     *ctx,
                                         gchar                 *font_uri,
                                         gdouble                font_size,
                                         gdouble                origin_x,
                                         gdouble                origin_y);
void        gxps_glyphs_free            (GXPSGlyphs            *glyphs);
gboolean    gxps_glyphs_to_cairo_glyphs (GXPSGlyphs            *gxps_glyphs,
                                         cairo_scaled_font_t   *scaled_font,
                                         const gchar           *utf8,
                                         cairo_glyph_t        **glyphs,
                                         int                   *num_glyphs,
                                         cairo_text_cluster_t **clusters,
                                         int                   *num_clusters,
                                         GError               **error);
void        gxps_glyphs_parser_push     (GMarkupParseContext   *context,
                                         GXPSGlyphs            *glyphs);

G_END_DECLS

#endif /* __GXPS_GLYPHS_H__ */
