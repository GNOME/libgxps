/* GXPSDocument
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

#include <stdlib.h>
#include <string.h>

#include "gxps-document.h"
#include "gxps-archive.h"
#include "gxps-links.h"
#include "gxps-private.h"
#include "gxps-error.h"

/**
 * SECTION:gxps-document
 * @Short_description: XPS Documents
 * @Title: GXPSDocument
 * @See_also: #GXPSFile, #GXPSPage, #GXPSDocumentStructure
 *
 * #GXPSDocument represents a document in a #GXPSFile. #GXPSDocument
 * objects can not be created directly, they are retrieved from a
 * #GXPSFile with gxps_file_get_document().
 */

enum {
	PROP_0,
	PROP_ARCHIVE,
	PROP_SOURCE
};

typedef struct _Page {
	gchar *source;
	gint   width;
	gint   height;
	GList *links;
} Page;

struct _GXPSDocumentPrivate {
	GXPSArchive *zip;
	gchar       *source;
	gboolean     has_rels;
	gchar       *structure;

	gboolean     initialized;
	GError      *init_error;

	Page       **pages;
	guint        n_pages;
};

static void initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (GXPSDocument, gxps_document, G_TYPE_OBJECT,
			 G_ADD_PRIVATE (GXPSDocument)
			 G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init))

#define REL_DOCUMENT_STRUCTURE "http://schemas.microsoft.com/xps/2005/06/documentstructure"

static Page *
page_new (void)
{
	return g_slice_new0 (Page);
}

static void
page_free (Page *page)
{
	g_free (page->source);
	g_list_foreach (page->links, (GFunc)g_free, NULL);
	g_list_free (page->links);

	g_slice_free (Page, page);
}

/* FixedDoc parser */
typedef struct _FixedDocParserData {
	GXPSDocument *doc;
	Page         *page;
	guint         n_pages;
	GList        *pages;
} FixedDocParserData;

static void
fixed_doc_start_element (GMarkupParseContext  *context,
			 const gchar          *element_name,
			 const gchar         **names,
			 const gchar         **values,
			 gpointer              user_data,
			 GError              **error)
{
	FixedDocParserData *data = (FixedDocParserData *)user_data;
	gint                i;

	if (strcmp (element_name, "PageContent") == 0) {
		gchar  *source = NULL;
		gdouble width = -1, height = -1;

		for (i = 0; names[i]; i++) {
			if (strcmp (names[i], "Source") == 0) {
				source = gxps_resolve_relative_path (data->doc->priv->source,
								     values[i]);
			} else if (strcmp (names[i], "Width") == 0) {
				if (!gxps_value_get_double_positive (values[i], &width))
					width = 0;
			} else if (strcmp (names[i], "Height") == 0) {
				if (!gxps_value_get_double_positive (values[i], &height))
					height = 0;
			}
		}

		if (!source) {
			gxps_parse_error (context,
					  data->doc->priv->source,
					  G_MARKUP_ERROR_MISSING_ATTRIBUTE,
					  element_name, "Source", NULL, error);
			return;
		}

		data->page = page_new ();
		data->page->source = source;
		data->page->width = width;
		data->page->height = height;
	} else if (strcmp (element_name, "LinkTarget") == 0) {
		if (!data->page) {
			/* TODO: error */
			return;
		}

		for (i = 0; names[i]; i++) {
			if (strcmp (names[i], "Name") == 0) {
				data->page->links = g_list_prepend (data->page->links, g_strdup (values[i]));
			}
		}
	} else if (strcmp (element_name, "PageContent.LinkTargets") == 0) {
	} else if (strcmp (element_name, "FixedDocument") == 0) {
		/* Nothing to do */
	} else {
		gxps_parse_error (context,
				  data->doc->priv->source,
				  G_MARKUP_ERROR_UNKNOWN_ELEMENT,
				  element_name, NULL, NULL, error);
	}
}

static void
fixed_doc_end_element (GMarkupParseContext  *context,
		       const gchar          *element_name,
		       gpointer              user_data,
		       GError              **error)
{
	FixedDocParserData *data = (FixedDocParserData *)user_data;

	if (strcmp (element_name, "PageContent") == 0) {
		data->n_pages++;
		data->pages = g_list_prepend (data->pages, data->page);
		data->page = NULL;
	} else if (strcmp (element_name, "PageContent.LinkTargets") == 0) {
		if (!data->page) {
			/* TODO: error */
			return;
		}
		data->page->links = g_list_reverse (data->page->links);
	} else if (strcmp (element_name, "FixedDocument") == 0) {
		GList *l;

		data->doc->priv->n_pages = data->n_pages;
                if (data->doc->priv->n_pages > 0) {
                        data->doc->priv->pages = g_new (Page *, data->n_pages);

                        for (l = data->pages; l; l = g_list_next (l))
                                data->doc->priv->pages[--data->n_pages] = (Page *)l->data;
                }
                g_list_free (data->pages);
	} else if (strcmp (element_name, "LinkTarget") == 0) {
		/* Do Nothing */
	} else {
		gxps_parse_error (context,
				  data->doc->priv->source,
				  G_MARKUP_ERROR_UNKNOWN_ELEMENT,
				  element_name, NULL, NULL, error);
	}
}

static const GMarkupParser fixed_doc_parser = {
	fixed_doc_start_element,
	fixed_doc_end_element,
	NULL,
	NULL,
	NULL
};

static gboolean
gxps_document_parse_fixed_doc (GXPSDocument *doc,
			       GError      **error)
{
	GInputStream        *stream;
	GMarkupParseContext *ctx;
	FixedDocParserData  *parser_data;

	stream = gxps_archive_open (doc->priv->zip,
				    doc->priv->source);
	if (!stream) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_SOURCE_NOT_FOUND,
			     "Document source %s not found in archive",
			     doc->priv->source);
		return FALSE;
	}

	parser_data = g_new0 (FixedDocParserData, 1);
	parser_data->doc = doc;

	ctx = g_markup_parse_context_new (&fixed_doc_parser, 0, parser_data, NULL);
	gxps_parse_stream (ctx, stream, error);
	g_object_unref (stream);

	g_free (parser_data);
	g_markup_parse_context_free (ctx);

	return (*error != NULL) ? FALSE : TRUE;
}

static void
doc_rels_start_element (GMarkupParseContext  *context,
			const gchar          *element_name,
			const gchar         **names,
			const gchar         **values,
			gpointer              user_data,
			GError              **error)
{
	GXPSDocument *doc = GXPS_DOCUMENT (user_data);

	if (strcmp (element_name, "Relationship") == 0) {
		const gchar *type = NULL;
		const gchar *target = NULL;
		gint         i;

		for (i = 0; names[i]; i++) {
			if (strcmp (names[i], "Type") == 0) {
				type = values[i];
			} else if (strcmp (names[i], "Target") == 0) {
				target = values[i];
			} else if (strcmp (names[i], "Id") == 0) {
				/* Ignore ids for now */
			}
		}

		if (g_strcmp0 (type, REL_DOCUMENT_STRUCTURE) == 0) {
			doc->priv->structure = target ? gxps_resolve_relative_path (doc->priv->source, target) : NULL;
		}
	}
}

static const GMarkupParser doc_rels_parser = {
	doc_rels_start_element,
	NULL,
	NULL,
	NULL,
	NULL
};

static gboolean
gxps_document_parse_rels (GXPSDocument *doc,
			  GError      **error)
{
	GInputStream        *stream;
	GMarkupParseContext *ctx;
	gchar               *filename;
	gchar               *rels, *doc_rels;
	gboolean             retval;

	if (!doc->priv->has_rels)
		return FALSE;

	filename = g_path_get_basename (doc->priv->source);
	rels = g_strconcat ("_rels/", filename, ".rels", NULL);
	doc_rels = gxps_resolve_relative_path (doc->priv->source, rels);
	g_free (filename);
	g_free (rels);

	stream = gxps_archive_open (doc->priv->zip, doc_rels);
	if (!stream) {
		doc->priv->has_rels = FALSE;
		g_free (doc_rels);

		return FALSE;
	}

	ctx = g_markup_parse_context_new (&doc_rels_parser, 0, doc, NULL);
	retval = gxps_parse_stream (ctx, stream, error);
	g_object_unref (stream);
	g_free (doc_rels);

	g_markup_parse_context_free (ctx);

	return retval;
}

static void
gxps_document_finalize (GObject *object)
{
	GXPSDocument *doc = GXPS_DOCUMENT (object);

	g_clear_object (&doc->priv->zip);
	g_clear_pointer (&doc->priv->source, g_free);
	g_clear_pointer (&doc->priv->structure, g_free);

	if (doc->priv->pages) {
		gint i;

		for (i = 0; i < doc->priv->n_pages; i++)
			page_free (doc->priv->pages[i]);
		g_free (doc->priv->pages);
		doc->priv->pages = NULL;
	}

	g_clear_error (&doc->priv->init_error);

	G_OBJECT_CLASS (gxps_document_parent_class)->finalize (object);
}

static void
gxps_document_init (GXPSDocument *doc)
{
	doc->priv = gxps_document_get_instance_private (doc);

	doc->priv->has_rels = TRUE;
}

static void
gxps_document_set_property (GObject      *object,
			    guint         prop_id,
			    const GValue *value,
			    GParamSpec   *pspec)
{
	GXPSDocument *doc = GXPS_DOCUMENT (object);

	switch (prop_id) {
	case PROP_ARCHIVE:
		doc->priv->zip = g_value_dup_object (value);
		break;
	case PROP_SOURCE:
		doc->priv->source = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gxps_document_class_init (GXPSDocumentClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = gxps_document_set_property;
	object_class->finalize = gxps_document_finalize;

	g_object_class_install_property (object_class,
					 PROP_ARCHIVE,
					 g_param_spec_object ("archive",
							      "Archive",
							      "The document archive",
							      GXPS_TYPE_ARCHIVE,
							      G_PARAM_WRITABLE |
							      G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_SOURCE,
					 g_param_spec_string ("source",
							      "Source",
							      "The Document Source File",
							      NULL,
							      G_PARAM_WRITABLE |
							      G_PARAM_CONSTRUCT_ONLY));
}

static gboolean
gxps_document_initable_init (GInitable     *initable,
			     GCancellable  *cancellable,
			     GError       **error)
{
	GXPSDocument *doc = GXPS_DOCUMENT (initable);

	if (doc->priv->initialized) {
		if (doc->priv->init_error) {
			g_propagate_error (error, g_error_copy (doc->priv->init_error));

			return FALSE;
		}

		return TRUE;
	}

	doc->priv->initialized = TRUE;

	if (!gxps_document_parse_fixed_doc (doc, &doc->priv->init_error)) {
		g_propagate_error (error, g_error_copy (doc->priv->init_error));
		return FALSE;
	}

	return TRUE;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
	initable_iface->init = gxps_document_initable_init;
}

GXPSDocument *
_gxps_document_new (GXPSArchive *zip,
		    const gchar *source,
		    GError     **error)
{
	return g_initable_new (GXPS_TYPE_DOCUMENT,
			       NULL, error,
			       "archive", zip,
			       "source", source,
			       NULL);
}

/**
 * gxps_document_get_n_pages:
 * @doc: a #GXPSDocument
 *
 * Gets the number of pages in @doc.
 *
 * Returns: the number of pages.
 */
guint
gxps_document_get_n_pages (GXPSDocument *doc)
{
	g_return_val_if_fail (GXPS_IS_DOCUMENT (doc), 0);

	return doc->priv->n_pages;
}

/**
 * gxps_document_get_page:
 * @doc: a #GXPSDocument
 * @n_page: the index of the page to get
 * @error: #GError for error reporting, or %NULL to ignore
 *
 * Creates a new #GXPSPage representing the page at
 * index @n_doc in @doc document.
 *
 * Returns: (transfer full): a new #GXPSPage or %NULL on error.
 *     Free the returned object with g_object_unref().
 */
GXPSPage *
gxps_document_get_page (GXPSDocument *doc,
			guint         n_page,
			GError      **error)
{
	const gchar *source;

	g_return_val_if_fail (GXPS_IS_DOCUMENT (doc), NULL);
	g_return_val_if_fail (n_page < doc->priv->n_pages, NULL);

	source = doc->priv->pages[n_page]->source;
	g_assert (source != NULL);

	return _gxps_page_new (doc->priv->zip, source, error);
}

/**
 * gxps_document_get_page_size:
 * @doc: a #GXPSDocument
 * @n_page: the index of a page in @doc
 * @width: (out) (allow-none): return location for the width of @n_page
 * @height: (out) (allow-none): return location for the height of @n_page
 *
 * Gets the typical size of the page at index @n_page in @doc document.
 * This function is useful to get the advisory size of pages in a document
 * without creating #GXPSPage objects. This page size might be different than
 * the actual page size so page dimension might need to be updated once the
 * page is loaded. Advisory page sizes are not always available in @doc,
 * in which case this function returns %FALSE.
 * To get the authoritative size of a page you should use gxps_page_get_size()
 * instead.
 *
 * Returns: %TRUE if the page size information is available in @doc,
 *     %FALSE otherwise.
 */
gboolean
gxps_document_get_page_size (GXPSDocument *doc,
			     guint         n_page,
			     gdouble      *width,
			     gdouble      *height)
{
	Page *page;

	g_return_val_if_fail (GXPS_IS_DOCUMENT (doc), FALSE);
	g_return_val_if_fail (n_page < doc->priv->n_pages, FALSE);

	page = doc->priv->pages[n_page];
	if (page->width == 0 || page->height == 0)
		return FALSE;

	if (width)
		*width = page->width;
	if (height)
		*height = page->height;

	return TRUE;
}

/**
 * gxps_document_get_page_for_anchor:
 * @doc: a #GXPSDocument
 * @anchor: the name of an anchor
 *
 * Gets the index of the page in @doc where the given
 * anchor is.
 *
 * Returns: the page index of the given anchor.
 */
gint
gxps_document_get_page_for_anchor (GXPSDocument *doc,
				   const gchar  *anchor)
{
	guint i;

	g_return_val_if_fail (GXPS_IS_DOCUMENT (doc), -1);
	g_return_val_if_fail (anchor != NULL, -1);

	for (i = 0; i < doc->priv->n_pages; i++) {
		if (g_list_find_custom (doc->priv->pages[i]->links, anchor, (GCompareFunc)strcmp))
			return i;
	}

	return -1;
}

/**
 * gxps_document_get_structure:
 * @doc: a a #GXPSDocument
 *
 * Creates a new #GXPSDocumentStructure representing the document
 * structure of @doc.
 *
 * Returns: (transfer full): a new #GXPSDocumentStructure or %NULL if document doesn't have a structure.
 *     Free the returned object with g_object_unref().
 */
GXPSDocumentStructure *
gxps_document_get_structure (GXPSDocument *doc)
{
	g_return_val_if_fail (GXPS_IS_DOCUMENT (doc), NULL);

	if (!doc->priv->structure) {
		if (!gxps_document_parse_rels (doc, NULL))
			return NULL;
	}

	if (!doc->priv->structure)
		return NULL;

	if (!gxps_archive_has_entry (doc->priv->zip, doc->priv->structure))
		return NULL;

	return _gxps_document_structure_new (doc->priv->zip,
					     doc->priv->structure);
}

