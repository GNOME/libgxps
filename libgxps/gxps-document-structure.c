/* GXPSDocumentStructure
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

#include "gxps-archive.h"
#include "gxps-private.h"
#include "gxps-error.h"

/**
 * SECTION:gxps-document-structure
 * @Short_description: Structure of XPS Document
 * @Title: GXPSDocumentStructure
 * @See_also: #GXPSDocument, #GXPSLinkTarget
 *
 * #GXPSDocumentStructure represents the structural organization of
 * a XPS document. A #GXPSDocumentStructure can contain the document
 * outline, similar to a table of contents, containing hyperlinks.
 * To iterate over the outline items you can use #GXPSOutlineIter.
 *
 * #GXPSDocumentStructure objects can not be created directly, they
 * are retrieved from a #GXPSDocument with gxps_document_get_structure().
 */

enum {
	PROP_0,
	PROP_ARCHIVE,
	PROP_SOURCE
};

struct _GXPSDocumentStructurePrivate {
	GXPSArchive *zip;
	gchar       *source;

	/* Outline */
	GList       *outline;
};

typedef struct _OutlineNode OutlineNode;
struct _OutlineNode {
	gchar       *desc;
	gchar       *target;
	guint        level;

	OutlineNode *parent;
	GList       *children;
};

G_DEFINE_TYPE_WITH_PRIVATE (GXPSDocumentStructure, gxps_document_structure, G_TYPE_OBJECT)

static void
outline_node_free (OutlineNode *node)
{
	if (G_UNLIKELY (!node))
		return;

	g_free (node->desc);
	g_free (node->target);
	g_list_free_full (node->children, (GDestroyNotify)outline_node_free);

	g_slice_free (OutlineNode, node);
}

static void
gxps_document_structure_finalize (GObject *object)
{
	GXPSDocumentStructure *structure = GXPS_DOCUMENT_STRUCTURE (object);

	g_clear_object (&structure->priv->zip);
	g_clear_pointer (&structure->priv->source, g_free);
	g_list_free_full (structure->priv->outline, (GDestroyNotify)outline_node_free);
	structure->priv->outline = NULL;

	G_OBJECT_CLASS (gxps_document_structure_parent_class)->finalize (object);
}

static void
gxps_document_structure_init (GXPSDocumentStructure *structure)
{
	structure->priv = gxps_document_structure_get_instance_private (structure);
}

static void
gxps_document_structure_set_property (GObject      *object,
				      guint         prop_id,
				      const GValue *value,
				      GParamSpec   *pspec)
{
	GXPSDocumentStructure *structure = GXPS_DOCUMENT_STRUCTURE (object);

	switch (prop_id) {
	case PROP_ARCHIVE:
		structure->priv->zip = g_value_dup_object (value);
		break;
	case PROP_SOURCE:
		structure->priv->source = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gxps_document_structure_class_init (GXPSDocumentStructureClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = gxps_document_structure_set_property;
	object_class->finalize = gxps_document_structure_finalize;

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
							      "The DocStructure Source File",
							      NULL,
							      G_PARAM_WRITABLE |
							      G_PARAM_CONSTRUCT_ONLY));
}

GXPSDocumentStructure *
_gxps_document_structure_new (GXPSArchive *zip,
			      const gchar *source)
{
	GXPSDocumentStructure *structure;

	structure = g_object_new (GXPS_TYPE_DOCUMENT_STRUCTURE,
				  "archive", zip,
				  "source", source,
				  NULL);
	return GXPS_DOCUMENT_STRUCTURE (structure);
}

/* Document Outline */
typedef struct {
	GXPSDocumentStructure *structure;
	guint                  level;
	GList                 *prevs;
	GList                 *outline;
} GXPSOutlineContext;

static void
outline_start_element (GMarkupParseContext  *context,
		       const gchar          *element_name,
		       const gchar         **names,
		       const gchar         **values,
		       gpointer              user_data,
		       GError              **error)
{
	GXPSOutlineContext *ctx = (GXPSOutlineContext *)user_data;

	if (strcmp (element_name, "DocumentOutline") == 0) {
	} else if (strcmp (element_name, "OutlineEntry") == 0) {
		gint i;
		gint level = 1;
		const gchar *desc = NULL;
		const gchar *target = NULL;
		OutlineNode *node;

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "OutlineLevel") == 0) {
				if (!gxps_value_get_int (values[i], &level))
					level = 1;
			} else if (strcmp (names[i], "Description") == 0) {
				desc = values[i];
			} else if (strcmp (names[i], "OutlineTarget") == 0) {
				target = values[i];
			} else if (strcmp (names[i], "xml:lang") == 0) {
				/* TODO */
			}
		}

		if (!desc || !target) {
			gxps_parse_error (context,
					  ctx->structure->priv->source,
					  G_MARKUP_ERROR_MISSING_ATTRIBUTE,
					  element_name,
					  !desc ? "Description" : "OutlineTarget",
					  NULL, error);
			return;
		}

		node = g_slice_new0 (OutlineNode);
		node->desc = g_strdup (desc);
		node->level = level;
		node->target = gxps_resolve_relative_path (ctx->structure->priv->source, target);

		if (ctx->level < level) {
			/* Higher level, make previous the parent of current one */
			node->parent = ctx->prevs ? (OutlineNode *)ctx->prevs->data : NULL;
		} else if (ctx->level > level) {
			/* Lower level, pop previous nodes, reordering children list,
			 * until previous node of the same level is found
			 */
			while (ctx->prevs) {
				OutlineNode *prev = (OutlineNode *)ctx->prevs->data;

				ctx->prevs = g_list_delete_link (ctx->prevs, ctx->prevs);
				prev->children = g_list_reverse (prev->children);
				if (prev->level == level) {
					node->parent = prev->parent;
					break;
				}
			}
			g_assert (level == 1 || (level > 1 && node->parent != NULL));
		} else if (ctx->level == level) {
			/* Same level, parent must be the same as the previous node */
			node->parent = (ctx->prevs) ? ((OutlineNode *)ctx->prevs->data)->parent : NULL;
			ctx->prevs = g_list_delete_link (ctx->prevs, ctx->prevs);
		}

		if (level == 1) {
			ctx->outline = g_list_prepend (ctx->outline, node);
                } else {
                        g_assert (node->parent != NULL);
			node->parent->children = g_list_prepend (node->parent->children, node);
                }

		ctx->prevs = g_list_prepend (ctx->prevs, node);
		ctx->level = level;
	}
}

static void
outline_end_element (GMarkupParseContext  *context,
		     const gchar          *element_name,
		     gpointer              user_data,
		     GError              **error)
{
	GXPSOutlineContext *ctx = (GXPSOutlineContext *)user_data;

	if (strcmp (element_name, "DocumentOutline") == 0) {
		/* Reorder children of pending node and remove them */
		while (ctx->prevs) {
			OutlineNode *prev = (OutlineNode *)ctx->prevs->data;

			ctx->prevs = g_list_delete_link (ctx->prevs, ctx->prevs);
			prev->children = g_list_reverse (prev->children);
		}
		ctx->outline = g_list_reverse (ctx->outline);
	} else if (strcmp (element_name, "OutlineEntry") == 0) {
	}
}

static const GMarkupParser outline_parser = {
	outline_start_element,
	outline_end_element,
	NULL,
	NULL,
	NULL
};

static GList *
gxps_document_structure_parse_outline (GXPSDocumentStructure *structure,
				       GError               **error)
{
	GInputStream        *stream;
	GXPSOutlineContext   ctx;
	GMarkupParseContext *context;

	stream = gxps_archive_open (structure->priv->zip,
				    structure->priv->source);
	if (!stream) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_SOURCE_NOT_FOUND,
			     "Document Structure source %s not found in archive",
			     structure->priv->source);
		return NULL;
	}

	ctx.structure = structure;
	ctx.prevs = NULL;
	ctx.level = 0;
	ctx.outline = NULL;

	context = g_markup_parse_context_new (&outline_parser, 0, &ctx, NULL);
	gxps_parse_stream (context, stream, error);
	g_object_unref (stream);
	g_markup_parse_context_free (context);

	return ctx.outline;
}

/* Try to know ASAP whether document has an outline */
static void
check_outline_start_element (GMarkupParseContext  *context,
			     const gchar          *element_name,
			     const gchar         **names,
			     const gchar         **values,
			     gpointer              user_data,
			     GError              **error)
{
	gboolean *has_outline = (gboolean *)user_data;

	if (*has_outline == TRUE)
		return;

	if (strcmp (element_name, "DocumentStructure.Outline") == 0)
		*has_outline = TRUE;
}

static const GMarkupParser check_outline_parser = {
	check_outline_start_element,
	NULL,
	NULL,
	NULL,
	NULL
};

/**
 * gxps_document_structure_has_outline:
 * @structure: a #GXPSDocumentStructure
 *
 * Whether @structure has an outline or not.
 *
 * Returns: %TRUE if @structure has an outline, %FALSE otherwise.
 */
gboolean
gxps_document_structure_has_outline (GXPSDocumentStructure *structure)
{
	GInputStream        *stream;
	GMarkupParseContext *context;
	gboolean             retval = FALSE;

	stream = gxps_archive_open (structure->priv->zip,
				    structure->priv->source);
	if (!stream)
		return FALSE;

	context = g_markup_parse_context_new (&check_outline_parser, 0, &retval, NULL);
	gxps_parse_stream (context, stream, NULL);
	g_object_unref (stream);
	g_markup_parse_context_free (context);

	return retval;
}

typedef struct {
	GXPSDocumentStructure *structure;
	GList                 *current;
} OutlineIter;

/**
 * gxps_document_structure_outline_iter_init:
 * @iter: an uninitialized #GXPSOutlineIter
 * @structure: a #GXPSDocumentStructure
 *
 * Initializes @iter to the root item of the outline contained by @structure
 * and a associates it with @structure.
 *
 * Here is a simple example of some code that walks the full outline:
 *
 * <informalexample><programlisting>
 * static void
 * walk_outline (GXPSOutlineIter *iter)
 * {
 *     do {
 *         GXPSOutlineIter child_iter;
 *         const gchar    *description = gxps_outline_iter_get_description (iter);
 *         GXPSLinkTarget *target = gxps_outline_iter_get_target (iter);
 *
 *         /<!-- -->* Do something with description and taregt *<!-- -->/
 *         if (gxps_outline_iter_children (&child_iter, iter))
 *             walk_outline (&child_iter);
 *     } while (gxps_outline_iter_next (iter));
 * }
 * ...
 * {
 *     GXPSOutlineIter iter;
 *     if (gxps_document_structure_outline_iter_init (&iter, structure))
 *         walk_outline (&iter);
 * }
 * </programlisting></informalexample>
 *
 * Returns: %TRUE if @iter was successfully initialized to the root item,
 *     %FALSE if it failed or @structure does not have an outline.
 */
gboolean
gxps_document_structure_outline_iter_init (GXPSOutlineIter       *iter,
					   GXPSDocumentStructure *structure)
{
	OutlineIter *oi = (OutlineIter *)iter;

	g_return_val_if_fail (iter != NULL, FALSE);
	g_return_val_if_fail (GXPS_IS_DOCUMENT_STRUCTURE (structure), FALSE);

	oi->structure = structure;
	if (!structure->priv->outline)
		structure->priv->outline = gxps_document_structure_parse_outline (structure, NULL);
	oi->current = structure->priv->outline;

	return oi->current != NULL;
}

/**
 * gxps_outline_iter_next:
 * @iter: an initialized #GXPSOutlineIter
 *
 * Advances @iter to the next item at the current level.
 * See gxps_document_structure_outline_iter_init() for
 * more details.
 *
 * Returns: %TRUE if @iter was set to the next item,
 *     %FALSE if the end of the current level has been reached
 */
gboolean
gxps_outline_iter_next (GXPSOutlineIter *iter)
{
	OutlineIter *oi = (OutlineIter *)iter;

	if (!oi->current)
		return FALSE;

	oi->current = g_list_next (oi->current);
	return oi->current != NULL;
}

/**
 * gxps_outline_iter_children:
 * @iter: an uninitialized #GXPSOutlineIter
 * @parent: an initialized #GXPSOutlineIter
 *
 * Initializes @iter to the first child item of @parent.
 * See gxps_document_structure_outline_iter_init() for
 * more details.
 *
 * Returns: %TRUE if @iter was set to the first child of @parent,
 *     %FALSE if @parent does not have children.
 */
gboolean
gxps_outline_iter_children (GXPSOutlineIter *iter,
			    GXPSOutlineIter *parent)
{
	OutlineIter *oi = (OutlineIter *)parent;
	OutlineIter *retval = (OutlineIter *)iter;
	OutlineNode *node;

	g_assert (oi->current != NULL);

	node = (OutlineNode *)oi->current->data;
	if (!node->children)
		return FALSE;

	retval->structure = oi->structure;
	retval->current = node->children;

	return TRUE;
}

/**
 * gxps_outline_iter_get_description:
 * @iter: an initialized #GXPSOutlineIter
 *
 * Gets the description of the outline item associated with @iter.
 * See gxps_document_structure_outline_iter_init() for
 * more details.
 *
 * Returns: the description of the outline item
 */
const gchar *
gxps_outline_iter_get_description (GXPSOutlineIter *iter)
{
	OutlineIter *oi = (OutlineIter *)iter;
	OutlineNode *node;

	g_assert (oi->current != NULL);

	node = (OutlineNode *)oi->current->data;

	return node->desc;
}

/**
 * gxps_outline_iter_get_target:
 * @iter: an initialized #GXPSOutlineIter
 *
 * Gets the #GXPSLinkTarget of the outline item associated with @iter.
 * See gxps_document_structure_outline_iter_init() for
 * more details.
 *
 * Returns: a new allocated #GXPSLinkTarget.
 *     Free the returned object with gxps_link_target_free().
 */
GXPSLinkTarget *
gxps_outline_iter_get_target (GXPSOutlineIter *iter)
{
	OutlineIter *oi = (OutlineIter *)iter;
	OutlineNode *node;

	g_assert (oi->current != NULL);

	node = (OutlineNode *)oi->current->data;

	return _gxps_link_target_new (oi->structure->priv->zip, node->target);
}

