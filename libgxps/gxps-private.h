/*
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

#ifndef __GXPS_PRIVATE_H__
#define __GXPS_PRIVATE_H__

#include <cairo.h>

#include "gxps-archive.h"
#include "gxps-document.h"
#include "gxps-page.h"
#include "gxps-parse-utils.h"
#include "gxps-links.h"
#include "gxps-document-structure.h"
#include "gxps-core-properties.h"

G_BEGIN_DECLS

GXPSDocument          *_gxps_document_new           (GXPSArchive       *zip,
						     const gchar       *source,
						     GError           **error);
GXPSPage              *_gxps_page_new               (GXPSArchive       *zip,
						     const gchar       *source,
						     GError           **error);
GXPSLink              *_gxps_link_new               (GXPSArchive       *zip,
						     cairo_rectangle_t *area,
						     const gchar       *dest);
GXPSLinkTarget        *_gxps_link_target_new        (GXPSArchive       *zip,
						     const gchar       *uri);
GXPSDocumentStructure *_gxps_document_structure_new (GXPSArchive       *zip,
						     const gchar       *source);
GXPSCoreProperties    *_gxps_core_properties_new    (GXPSArchive       *zip,
                                                     const gchar       *source,
                                                     GError           **error);

G_END_DECLS

#endif /* __GXPS_PRIVATE_H__ */
