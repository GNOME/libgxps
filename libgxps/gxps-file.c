/* GXPSFile
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

#include "gxps-file.h"
#include "gxps-archive.h"
#include "gxps-private.h"
#include "gxps-error.h"
#include "gxps-debug.h"

/**
 * SECTION:gxps-file
 * @Short_description: XPS Files
 * @Title: GXPSFile
 * @See_also: #GXPSDocument, #GXPSLinkTarget
 *
 * #GXPSFile represents a XPS file. A #GXPSFile is a set of one or more
 * documents, you can get the amount of documents contained in the set
 * with gxps_file_get_n_documents(). Documents can be retrieved by their
 * index in the set with gxps_file_get_document().
 */

enum {
	PROP_0,
	PROP_FILE
};

struct _GXPSFilePrivate {
	GFile       *file;
	GXPSArchive *zip;
	GPtrArray   *docs;

	gboolean     initialized;
	GError      *init_error;

	gchar       *fixed_repr;
	gchar       *thumbnail;
	gchar       *core_props;
};

static void initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (GXPSFile, gxps_file, G_TYPE_OBJECT,
			 G_ADD_PRIVATE (GXPSFile)
			 G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init))

GQuark
gxps_file_error_quark (void)
{
	return g_quark_from_static_string ("gxps-file-error-quark");
}

#define REL_METATADA_CORE_PROPS  "http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties"
#define REL_METATADA_THUMBNAIL   "http://schemas.openxmlformats.org/package/2006/relationships/metadata/thumbnail"
#define REL_FIXED_REPRESENTATION "http://schemas.microsoft.com/xps/2005/06/fixedrepresentation"
#define REL_OXPS_FIXED_REPRESENTATION "http://schemas.openxps.org/oxps/v1.0/fixedrepresentation"

/* Relationship parser */
static void
rels_start_element (GMarkupParseContext  *context,
		    const gchar          *element_name,
		    const gchar         **names,
		    const gchar         **values,
		    gpointer              user_data,
		    GError              **error)
{
	GXPSFile *xps = GXPS_FILE (user_data);

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

		if (!type || !target) {
			gxps_parse_error (context,
					  "_rels/.rels",
					  G_MARKUP_ERROR_MISSING_ATTRIBUTE,
					  element_name,
					  !type ? "Type" : "Target",
					  NULL, error);
			return;
		}

		if (strcmp (type, REL_FIXED_REPRESENTATION) == 0 ||
		    strcmp (type, REL_OXPS_FIXED_REPRESENTATION) == 0) {
			xps->priv->fixed_repr = g_strdup (target);
		} else if (strcmp (type, REL_METATADA_THUMBNAIL) == 0) {
			xps->priv->thumbnail = g_strdup (target);
		} else if (strcmp (type, REL_METATADA_CORE_PROPS) == 0) {
			xps->priv->core_props = g_strdup (target);
		} else {
			GXPS_DEBUG (g_debug ("Unsupported attribute of %s, %s=%s",
                                             element_name, type, target));
		}
	} else if (strcmp (element_name, "Relationships") == 0) {
		/* Nothing to do */
	} else {
		gxps_parse_error (context,
				  "_rels/.rels",
				  G_MARKUP_ERROR_UNKNOWN_ELEMENT,
				  element_name, NULL, NULL, error);
	}
}

static const GMarkupParser rels_parser = {
	rels_start_element,
	NULL,
	NULL,
	NULL,
	NULL
};

static gboolean
gxps_file_parse_rels (GXPSFile *xps,
		      GError  **error)
{
	GInputStream        *stream;
	GMarkupParseContext *ctx;

	stream = gxps_archive_open (xps->priv->zip, "_rels/.rels");
	if (!stream) {
		g_set_error_literal (error,
				     GXPS_ERROR,
				     GXPS_ERROR_SOURCE_NOT_FOUND,
				     "Source _rels/.rels not found in archive");
		return FALSE;
	}

	ctx = g_markup_parse_context_new (&rels_parser, 0, xps, NULL);
	gxps_parse_stream (ctx, stream, error);
	g_object_unref (stream);
	g_markup_parse_context_free (ctx);

	return (*error != NULL) ? FALSE : TRUE;
}

/* FixedRepresentation parser */
static void
fixed_repr_start_element (GMarkupParseContext  *context,
			  const gchar          *element_name,
			  const gchar         **names,
			  const gchar         **values,
			  gpointer              user_data,
			  GError              **error)
{
	GXPSFile *xps = GXPS_FILE (user_data);

	if (strcmp (element_name, "DocumentReference") == 0) {
		gint i;

		for (i = 0; names[i]; i++) {
			if (strcmp (names[i], "Source") == 0) {
				g_ptr_array_add (xps->priv->docs,
						 gxps_resolve_relative_path (xps->priv->fixed_repr, values[i]));
			}
		}
	} else if (strcmp (element_name, "FixedDocumentSequence") == 0) {
		/* Nothing to do */
	} else {
		gxps_parse_error (context,
				  xps->priv->fixed_repr,
				  G_MARKUP_ERROR_UNKNOWN_ELEMENT,
				  element_name, NULL, NULL, error);
	}
}

static const GMarkupParser fixed_repr_parser = {
	fixed_repr_start_element,
	NULL,
	NULL,
	NULL,
	NULL
};

static gboolean
gxps_file_parse_fixed_repr (GXPSFile *xps,
			    GError  **error)
{
	GInputStream        *stream;
	GMarkupParseContext *ctx;

	stream = gxps_archive_open (xps->priv->zip,
				    xps->priv->fixed_repr);
	if (!stream) {
		g_set_error_literal (error,
				     GXPS_FILE_ERROR,
				     GXPS_FILE_ERROR_INVALID,
				     "Invalid XPS File: cannot open fixedrepresentation");
		return FALSE;
	}

	ctx = g_markup_parse_context_new (&fixed_repr_parser, 0, xps, NULL);
	gxps_parse_stream (ctx, stream, error);
	g_object_unref (stream);
	g_markup_parse_context_free (ctx);

	return (*error != NULL) ? FALSE : TRUE;
}

static void
gxps_file_finalize (GObject *object)
{
	GXPSFile *xps = GXPS_FILE (object);

	g_clear_object (&xps->priv->zip);
	g_clear_object (&xps->priv->file);
	g_clear_pointer (&xps->priv->docs, g_ptr_array_unref);
	g_clear_pointer (&xps->priv->fixed_repr, g_free);
	g_clear_pointer (&xps->priv->thumbnail, g_free);
	g_clear_pointer (&xps->priv->core_props, g_free);
	g_clear_error (&xps->priv->init_error);

	G_OBJECT_CLASS (gxps_file_parent_class)->finalize (object);
}

static void
gxps_file_init (GXPSFile *xps)
{
	xps->priv = gxps_file_get_instance_private (xps);
}

static void
gxps_file_set_property (GObject      *object,
			guint         prop_id,
			const GValue *value,
			GParamSpec   *pspec)
{
	GXPSFile *xps = GXPS_FILE (object);

	switch (prop_id) {
	case PROP_FILE:
		xps->priv->file = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gxps_file_class_init (GXPSFileClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = gxps_file_set_property;
	object_class->finalize = gxps_file_finalize;

	g_object_class_install_property (object_class,
					 PROP_FILE,
					 g_param_spec_object ("file",
							      "File",
							      "The file file",
							      G_TYPE_FILE,
							      G_PARAM_WRITABLE |
							      G_PARAM_CONSTRUCT_ONLY));
}

static gboolean
gxps_file_initable_init (GInitable     *initable,
			 GCancellable  *cancellable,
			 GError       **error)
{
	GXPSFile *xps = GXPS_FILE (initable);

	if (xps->priv->initialized) {
		if (xps->priv->init_error) {
			g_propagate_error (error, g_error_copy (xps->priv->init_error));
			return FALSE;
		}

		return TRUE;
	}

	xps->priv->initialized = TRUE;

	xps->priv->docs = g_ptr_array_new_with_free_func (g_free);

	xps->priv->zip = gxps_archive_new (xps->priv->file, &xps->priv->init_error);
	if (!xps->priv->zip) {
		g_propagate_error (error, g_error_copy (xps->priv->init_error));
		return FALSE;
	}

	if (!gxps_file_parse_rels (xps, &xps->priv->init_error)) {
		g_propagate_error (error, g_error_copy (xps->priv->init_error));
		return FALSE;
	}

	if (!xps->priv->fixed_repr) {
		g_set_error_literal (&xps->priv->init_error,
				     GXPS_FILE_ERROR,
				     GXPS_FILE_ERROR_INVALID,
				     "Invalid XPS File: fixedrepresentation not found");
		g_propagate_error (error, g_error_copy (xps->priv->init_error));
		return FALSE;
	}

	if (!gxps_file_parse_fixed_repr (xps, &xps->priv->init_error)) {
		g_propagate_error (error, g_error_copy (xps->priv->init_error));
		return FALSE;
	}

	if (xps->priv->docs->len == 0) {
		g_set_error_literal (&xps->priv->init_error,
				     GXPS_FILE_ERROR,
				     GXPS_FILE_ERROR_INVALID,
				     "Invalid XPS File: no documents found");
		g_propagate_error (error, g_error_copy (xps->priv->init_error));
		return FALSE;
	}

	return TRUE;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
	initable_iface->init = gxps_file_initable_init;
}

/**
 * gxps_file_new:
 * @filename: a #GFile
 * @error: #GError for error reporting, or %NULL to ignore
 *
 * Creates a new #GXPSFile for the given #GFile.
 *
 * Returns: a #GXPSFile or %NULL on error.
 */
GXPSFile *
gxps_file_new (GFile   *filename,
	       GError **error)
{
	g_return_val_if_fail (G_IS_FILE (filename), NULL);

	return g_initable_new (GXPS_TYPE_FILE,
			       NULL, error,
			       "file", filename,
			       NULL);
}

/**
 * gxps_file_get_n_documents:
 * @xps: a #GXPSFile
 *
 * Gets the number of documents in @xps.
 *
 * Returns: the number of documents.
 */
guint
gxps_file_get_n_documents (GXPSFile *xps)
{
	g_return_val_if_fail (GXPS_IS_FILE (xps), 0);

	return xps->priv->docs->len;
}

/**
 * gxps_file_get_document:
 * @xps: a #GXPSFile
 * @n_doc: the index of the document to get
 * @error: #GError for error reporting, or %NULL to ignore
 *
 * Creates a new #GXPSDocument representing the document at
 * index @n_doc in @xps file.
 *
 * Returns: (transfer full): a new #GXPSDocument or %NULL on error.
 *     Free the returned object with g_object_unref().
 */
GXPSDocument *
gxps_file_get_document (GXPSFile *xps,
			guint     n_doc,
			GError  **error)
{
	const gchar  *source;

	g_return_val_if_fail (GXPS_IS_FILE (xps), NULL);
	g_return_val_if_fail (n_doc < xps->priv->docs->len, NULL);

	source = g_ptr_array_index (xps->priv->docs, n_doc);
	g_assert (source != NULL);

	return _gxps_document_new (xps->priv->zip, source, error);
}

/**
 * gxps_file_get_document_for_link_target:
 * @xps: a #GXPSFile
 * @target: a #GXPSLinkTarget
 *
 * Gets the index of the document in @xps pointed by @target.
 * If the #GXPSLinkTarget does not reference a document, or
 * referenced document is not found in @xps file -1 will be
 * returned. In this case you can look for the page pointed by
 * the link target by calling gxps_document_get_page_for_anchor()
 * with the anchor of the #GXPSLinkTarget for every document in
 * @xps.
 *
 * Returns: the index of the document pointed by the given
 *     #GXPSLinkTarget or -1.
 */
gint
gxps_file_get_document_for_link_target (GXPSFile       *xps,
					GXPSLinkTarget *target)
{
	guint        i;
	const gchar *uri;

        g_return_val_if_fail (GXPS_IS_FILE (xps), -1);
        g_return_val_if_fail (target != NULL, -1);

	uri = gxps_link_target_get_uri (target);
	for (i = 0; i < xps->priv->docs->len; ++i) {
		if (g_ascii_strcasecmp (uri, (gchar *)xps->priv->docs->pdata[i]) == 0)
			return i;
	}

	return -1;
}

/**
 * gxps_file_get_core_properties:
 * @xps: a #GXPSFile
 * @error: #GError for error reporting, or %NULL to ignore
 *
 * Create a #GXPSCoreProperties object containing the metadata
 * of @xpsm, or %NULL in case of error or if the #GXPSFile
 * doesn't contain core properties.
 *
 * Returns: (transfer full): a new #GXPSCoreProperties or %NULL.
 *    Free the returned object with g_object_unref().
 */
GXPSCoreProperties *
gxps_file_get_core_properties (GXPSFile *xps,
                               GError  **error)
{
        g_return_val_if_fail (GXPS_IS_FILE (xps), NULL);

        if (!xps->priv->core_props)
                return NULL;

        return _gxps_core_properties_new (xps->priv->zip,
                                          xps->priv->core_props,
                                          error);
}
