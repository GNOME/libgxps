/* GXPSFonts
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

#ifndef __GXPS_FONTS_H__
#define __GXPS_FONTS_H__

#include <cairo.h>
#include "gxps-archive.h"

G_BEGIN_DECLS

cairo_font_face_t *gxps_fonts_get_font (GXPSArchive *zip,
					const gchar *font_uri,
					GError     **error);

G_END_DECLS

#endif /* __GXPS_FONTS_H__ */
