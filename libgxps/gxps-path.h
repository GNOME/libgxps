/* GXPSPath
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

#ifndef __GXPS_PATH_H__
#define __GXPS_PATH_H__

#include <cairo.h>
#include "gxps-page-private.h"

G_BEGIN_DECLS

typedef struct _GXPSPath GXPSPath;

struct _GXPSPath {
        GXPSRenderContext *ctx;

        gchar             *data;
        gchar             *clip_data;
        cairo_pattern_t   *fill_pattern;
        cairo_pattern_t   *stroke_pattern;
        cairo_fill_rule_t  fill_rule;
        gdouble            line_width;
        gdouble           *dash;
        guint              dash_len;
        gdouble            dash_offset;
        cairo_line_cap_t   line_cap;
        cairo_line_join_t  line_join;
        gdouble            miter_limit;
        gdouble            opacity;
        cairo_pattern_t   *opacity_mask;

        gboolean           is_stroked : 1;
        gboolean           is_filled  : 1;
        gboolean           is_closed  : 1;
};

GXPSPath *gxps_path_new         (GXPSRenderContext   *ctx);
void      gxps_path_free        (GXPSPath            *path);
gboolean  gxps_path_parse       (const gchar         *data,
                                 cairo_t             *cr,
                                 GError             **error);

void      gxps_path_parser_push (GMarkupParseContext *context,
                                 GXPSPath            *path);

G_END_DECLS

#endif /* __GXPS_PATH_H__ */
