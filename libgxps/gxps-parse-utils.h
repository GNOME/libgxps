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

#ifndef __GXPS_PARSE_UTILS_H__
#define __GXPS_PARSE_UTILS_H__

#include "gxps-archive.h"
#include "gxps-document.h"

G_BEGIN_DECLS

gboolean gxps_parse_stream                  (GMarkupParseContext  *context,
                                             GInputStream         *stream,
                                             GError              **error);
void     gxps_parse_error                   (GMarkupParseContext  *context,
                                             const gchar          *source,
                                             GMarkupError          error_type,
                                             const gchar          *element_name,
                                             const gchar          *attribute_name,
                                             const gchar          *content,
                                             GError              **error);
gboolean gxps_value_get_int                 (const gchar          *value,
                                             gint                 *int_value);
gboolean gxps_value_get_double              (const gchar          *value,
                                             gdouble              *double_value);
gboolean gxps_value_get_double_positive     (const gchar          *value,
                                             gdouble              *double_value);
gboolean gxps_value_get_double_non_negative (const gchar          *value,
                                             gdouble              *double_value);
gboolean gxps_value_get_boolean             (const gchar          *value,
                                             gboolean             *boolean_value);
gboolean gxps_point_parse                   (const gchar          *point,
                                             gdouble              *x,
                                             gdouble              *y);
void     gxps_parse_skip_number             (gchar               **iter,
                                             const gchar          *end);
gchar   *gxps_resolve_relative_path         (const gchar          *source,
                                             const gchar          *target);

G_END_DECLS

#endif /* __GXPS_PARSE_UTILS_H__ */
