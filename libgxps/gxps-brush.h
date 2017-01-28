/* GXPSBrush
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

#ifndef __GXPS_BRUSH_H__
#define __GXPS_BRUSH_H__

#include <cairo.h>
#include "gxps-page-private.h"

G_BEGIN_DECLS

typedef struct _GXPSBrush GXPSBrush;

struct _GXPSBrush {
        GXPSRenderContext *ctx;
        cairo_pattern_t   *pattern;
        gdouble            opacity;
};

GXPSBrush *gxps_brush_new               (GXPSRenderContext   *ctx);
void       gxps_brush_free              (GXPSBrush           *brush);
gboolean   gxps_brush_solid_color_parse (const gchar         *data,
                                         GXPSArchive         *zip,
                                         gdouble              alpha,
                                         cairo_pattern_t    **pattern);
void       gxps_brush_parser_push       (GMarkupParseContext *context,
                                         GXPSBrush           *brush);

G_END_DECLS

#endif /* __GXPS_BRUSH_H__ */
