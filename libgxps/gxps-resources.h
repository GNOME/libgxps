/*
 * Copyright (C) 2015  Jason Crain <jason@aquaticape.us>
 * Copyright (C) 2017  Ignacio Casal Quinteiro <icq@gnome.org>
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

#ifndef GXPS_RESOURCES_H
#define GXPS_RESOURCES_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GXPS_TYPE_RESOURCES           (gxps_resources_get_type ())
#define GXPS_RESOURCES(obj)           (G_TYPE_CHECK_INSTANCE_CAST (obj, GXPS_TYPE_RESOURCES, GXPSResources))
#define GXPS_IS_RESOURCES(obj)        (G_TYPE_CHECK_INSTANCE_TYPE (obj, GXPS_TYPE_RESOURCES))

typedef struct _GXPSResources GXPSResources;

GType          gxps_resources_get_type     (void) G_GNUC_CONST;

void           gxps_resources_push_dict    (GXPSResources       *resources);

void           gxps_resources_pop_dict     (GXPSResources       *resources);

const gchar   *gxps_resources_get_resource (GXPSResources       *resources,
                                            const gchar         *key);

void           gxps_resources_parser_push  (GMarkupParseContext *context,
                                            GXPSResources       *resources,
                                            const gchar         *source);

void           gxps_resources_parser_pop   (GMarkupParseContext *context);

G_END_DECLS

#endif /* GXPS_RESOURCES_H */
