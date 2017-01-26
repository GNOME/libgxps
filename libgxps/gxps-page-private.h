/* GXPSPage
 *
 * Copyright (C) 2010  Carlos Garcia Campos <carlosgc@gnome.org>
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

#ifndef __GXPS_PAGE_PRIVATE_H__
#define __GXPS_PAGE_PRIVATE_H__

#include <glib.h>
#include <cairo.h>

#include "gxps-page.h"
#include "gxps-archive.h"
#include "gxps-images.h"
#include "gxps-resources.h"

G_BEGIN_DECLS

typedef struct _GXPSRenderContext GXPSRenderContext;
typedef struct _GXPSBrushVisual   GXPSBrushVisual;

struct _GXPSPagePrivate {
        GXPSArchive *zip;
        gchar       *source;

        gboolean     initialized;
        GError      *init_error;

        gdouble      width;
        gdouble      height;
        gchar       *lang;
        gchar       *name;

        /* Images */
        GHashTable  *image_cache;

        /* Anchors */
        gboolean     has_anchors;
        GHashTable  *anchors;
};

struct _GXPSRenderContext {
        GXPSPage        *page;
        cairo_t         *cr;
        GXPSBrushVisual *visual;
};

GXPSImage *gxps_page_get_image          (GXPSPage            *page,
                                         const gchar         *image_uri,
                                         GError             **error);
void       gxps_page_render_parser_push (GMarkupParseContext *context,
                                         GXPSRenderContext   *ctx);

G_END_DECLS

#endif /* __GXPS_PAGE_PRIVATE_H__ */
