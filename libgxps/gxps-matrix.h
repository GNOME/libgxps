/* GXPSMatrix
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

#ifndef __GXPS_MATRIX_H__
#define __GXPS_MATRIX_H__

#include <cairo.h>
#include "gxps-page-private.h"

G_BEGIN_DECLS

typedef struct _GXPSMatrix GXPSMatrix;

struct _GXPSMatrix {
        GXPSRenderContext *ctx;
        cairo_matrix_t     matrix;
};

GXPSMatrix *gxps_matrix_new         (GXPSRenderContext   *ctx);
void        gxps_matrix_free        (GXPSMatrix          *matrix);
gboolean    gxps_matrix_parse       (const gchar         *data,
                                     cairo_matrix_t      *matrix);
void        gxps_matrix_parser_push (GMarkupParseContext *context,
                                     GXPSMatrix          *matrix);

G_END_DECLS

#endif /* __GXPS_MATRIX_H__ */
