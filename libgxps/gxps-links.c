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

#include <config.h>

#include <string.h>

#include "gxps-links.h"
#include "gxps-private.h"

/**
 * SECTION:gxps-links
 * @Short_description: XPS Links
 * @Title: GXPS Links
 * @See_also: #GXPSFile, #GXPSPage, #GXPSDocumentStructure
 *
 * #GXPSLinkTarget is a hyperlink source that can point to a location in
 * any of the documents of the XPS file or to an external document.
 * Internal #GXPSLinkTarget<!-- -->s have an URI relative to the XPS file,
 * and a named destination represented by an anchor. External
 * #GXPSLinkTarget<!-- -->s have an absolute URI pointing to a location
 * in another XPS File and might optionally have an anchor.
 * To get the destination pointed by a in internal #GXPSLinkTarget you can use
 * gxps_file_get_document_for_link_target() to get the document within the file,
 * gxps_document_get_page_for_anchor() to get the page within the document, and
 * gxps_page_get_anchor_destination() to get the page area.
 *
 * #GXPSLink maps a location in a page to a #GXPSLinkTarget. To get the list of
 * link for a page you can use gxps_page_get_links().
 */

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

/**
 * gxps_link_copy:
 * @link: a #GXPSLink
 *
 * Creates a copy of a #GXPSLink.
 *
 * Returns: a copy of @link.
 *     Free the returned object with gxps_link_free().
 */
GXPSLink *
gxps_link_copy (GXPSLink *link)
{
	GXPSLink *link_copy;

        g_return_val_if_fail (link != NULL, NULL);

	link_copy = g_slice_new (GXPSLink);
	*link_copy = *link;

	if (link->target)
		link_copy->target = gxps_link_target_copy (link->target);

	return link_copy;
}

/**
 * gxps_link_free:
 * @link: a #GXPSLink
 *
 * Frees a #GXPSLink.
 */
void
gxps_link_free (GXPSLink *link)
{
	if (G_UNLIKELY (!link))
		return;

	gxps_link_target_free (link->target);
	g_slice_free (GXPSLink, link);
}

/**
 * gxps_link_get_target:
 * @link: a #GXPSLink
 *
 * Gets the #GXPSLinkTarget mapped by @link.
 *
 * Returns: (transfer none): the #GXPSLinkTarget of @link.
 */
GXPSLinkTarget *
gxps_link_get_target (GXPSLink *link)
{
        g_return_val_if_fail (link != NULL, NULL);

	return link->target;
}

/**
 * gxps_link_get_area:
 * @link: a #GXPSLink
 * @area: (out): return location for page area
 *
 * Gets the rectangle of a page where the #GXPSLinkTarget
 * mapped by @link is.
 */
void
gxps_link_get_area (GXPSLink          *link,
		    cairo_rectangle_t *area)
{
        g_return_if_fail (link != NULL);
        g_return_if_fail (area != NULL);

	*area = link->area;
}

/**
 * gxps_link_target_copy:
 * @target: a #GXPSLinkTarget
 *
 * Creates a copy of a #GXPSLinkTarget
 *
 * Returns: a copy of @target.
 *     Free the returned object with gxps_link_target_free()
 */
GXPSLinkTarget *
gxps_link_target_copy (GXPSLinkTarget *target)
{
	GXPSLinkTarget *target_copy;

        g_return_val_if_fail (target != NULL, NULL);

	target_copy = g_slice_new (GXPSLinkTarget);

	target_copy->is_internal = target->is_internal;
	target_copy->uri = g_strdup (target->uri);
	target_copy->anchor = target->anchor ? g_strdup (target->anchor) : NULL;

	return target_copy;
}

/**
 * gxps_link_target_free:
 * @target: a #GXPSLinkTarget
 *
 * Frees a #GXPSLinkTarget.
 */
void
gxps_link_target_free (GXPSLinkTarget *target)
{
	if (G_UNLIKELY (!target))
		return;

	g_free (target->uri);
	g_free (target->anchor);
	g_slice_free (GXPSLinkTarget, target);
}

/**
 * gxps_link_target_is_internal:
 * @target: a #GXPSLinkTarget
 *
 * Gets whether @target destination is internal or not.
 *
 * Returns: %TRUE if the #GXPSLinkTarget points to an internal location,
 *     %FALSE if it points to a external one.
 */
gboolean
gxps_link_target_is_internal (GXPSLinkTarget *target)
{
        g_return_val_if_fail (target != NULL, FALSE);

	return target->is_internal;
}

/**
 * gxps_link_target_get_anchor:
 * @target: a #GXPSLinkTarget
 *
 * Gets the anchor name @target links to. If @target is
 * an internal #GXPSLinkTarget this function always returns
 * and anchor, if it is external it might return %NULL if the
 * @target does not have an anchor.
 *
 * Returns: the name of the anchor of @target.
 */
const gchar *
gxps_link_target_get_anchor (GXPSLinkTarget *target)
{
        g_return_val_if_fail (target != NULL, NULL);

	return target->anchor;
}

/**
 * gxps_link_target_get_uri:
 * @target: a #GXPSLinkTarget
 *
 * Gets the URI @target links to.
 *
 * Returns: the URI of @target.
 */
const gchar *
gxps_link_target_get_uri (GXPSLinkTarget *target)
{
        g_return_val_if_fail (target != NULL, NULL);

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
