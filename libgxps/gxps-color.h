/* GXPSColor
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

#ifndef __GXPS_COLOR_H__
#define __GXPS_COLOR_H__

#include <glib.h>
#include "gxps-archive.h"

G_BEGIN_DECLS

#define GXPS_COLOR_MAX_CHANNELS 8

typedef struct _GXPSColor {
        gdouble alpha;
        gdouble red;
        gdouble green;
        gdouble blue;
} GXPSColor;

gboolean gxps_color_new_for_icc (GXPSArchive *zip,
                                 const gchar *icc_profile_uri,
                                 gdouble     *values,
                                 guint        n_values,
                                 GXPSColor   *color);

G_END_DECLS

#endif /* __GXPS_COLOR_H__ */
