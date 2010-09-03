/* GXPSLinks
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

#include "config.h"

#include <string.h>

#include "gxps-links.h"
#include "gxps-private.h"

struct _GXPSLink {
	GXPSLinkTarget   *target;
	cairo_rectangle_t area;
};

struct _GXPSLinkTarget {
	gboolean is_internal;
	gchar   *uri;
	gchar   *anchor;
};

G_DEFINE_BOXED_TYPE (GXPSLink, gxps_link, gxps_link_copy, gxps_link_free)
G_DEFINE_BOXED_TYPE (GXPSLinkTarget, gxps_link_target, gxps_link_target_copy, gxps_link_target_free)

GXPSLink *
gxps_link_copy (GXPSLink *link)
{
	GXPSLink *link_copy;

	link_copy = g_slice_new (GXPSLink);
	*link_copy = *link;

	if (link->target)
		link_copy->target = gxps_link_target_copy (link->target);

	return link_copy;
}

void
gxps_link_free (GXPSLink *link)
{
	if (G_UNLIKELY (!link))
		return;

	gxps_link_target_free (link->target);
	g_slice_free (GXPSLink, link);
}

GXPSLinkTarget *
gxps_link_get_target (GXPSLink *link)
{
	return link->target;
}

void
gxps_link_get_area (GXPSLink          *link,
		    cairo_rectangle_t *area)
{
	*area = link->area;
}

GXPSLinkTarget *
gxps_link_target_copy (GXPSLinkTarget *target)
{
	GXPSLinkTarget *target_copy;

	target_copy = g_slice_new (GXPSLinkTarget);

	target_copy->is_internal = target->is_internal;
	target_copy->uri = g_strdup (target->uri);
	target_copy->anchor = target->anchor ? g_strdup (target->anchor) : NULL;

	return target_copy;
}

void
gxps_link_target_free (GXPSLinkTarget *target)
{
	if (G_UNLIKELY (!target))
		return;

	g_free (target->uri);
	g_free (target->anchor);
	g_slice_free (GXPSLinkTarget, target);
}

gboolean
gxps_link_target_is_internal (GXPSLinkTarget *target)
{
	return target->is_internal;
}

const gchar *
gxps_link_target_get_anchor (GXPSLinkTarget *target)
{
	return target->anchor;
}

const gchar *
gxps_link_target_get_uri (GXPSLinkTarget *target)
{
	return target->uri;
}

GXPSLinkTarget *
_gxps_link_target_new (GXPSArchive *zip,
		       const gchar *uri)
{
	GXPSLinkTarget *target;
	gchar          *sep;

	target = g_slice_new (GXPSLinkTarget);

	sep = g_strrstr (uri, "#");
	if (sep) {
		target->uri = g_strndup (uri, strlen (uri) - strlen (sep));
		target->anchor = g_strdup (++sep);
	} else {
		target->uri = g_strdup (uri);
		target->anchor = NULL;
	}

	target->is_internal = gxps_archive_has_entry (zip, target->uri);

	return target;
}

GXPSLink *
_gxps_link_new (GXPSArchive       *zip,
		cairo_rectangle_t *area,
		const gchar       *uri)
{
	GXPSLink *link;

	link = g_slice_new (GXPSLink);
	link->area = *area;
	link->target = _gxps_link_target_new (zip, uri);

	return link;
}
