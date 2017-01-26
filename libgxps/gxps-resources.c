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

#include "gxps-resources.h"
#include "gxps-parse-utils.h"
#include "gxps-error.h"

#include <string.h>

#define GXPS_RESOURCES_CLASS(cls)     (G_TYPE_CHECK_CLASS_CAST (cls, GXPS_TYPE_RESOURCES, GXPSResourcesClass))
#define GXPS_IS_RESOURCES_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE (obj, GXPS_TYPE_RESOURCES))
#define GXPS_RESOURCES_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GXPS_TYPE_RESOURCES, GXPSResourcesClass))

struct _GXPSResources
{
	GObject parent_instance;

	GXPSArchive *zip;

	GQueue *queue;
};

struct _GXPSResourcesClass
{
	GObjectClass parent;
};

typedef struct _GXPSResourcesClass GXPSResourcesClass;

enum {
	PROP_0,
	PROP_ARCHIVE,
	LAST_PROP
};

static GParamSpec *props[LAST_PROP];

G_DEFINE_TYPE (GXPSResources, gxps_resources, G_TYPE_OBJECT)

static void
gxps_resources_finalize (GObject *object)
{
	GXPSResources *resources = GXPS_RESOURCES (object);

	g_queue_free_full (resources->queue, (GDestroyNotify)g_hash_table_destroy);
	g_object_unref (resources->zip);

	G_OBJECT_CLASS (gxps_resources_parent_class)->finalize (object);
}

static void
gxps_resources_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	GXPSResources *resources = GXPS_RESOURCES (object);

	switch (prop_id) {
	case PROP_ARCHIVE:
		resources->zip = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gxps_resources_class_init (GXPSResourcesClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = gxps_resources_finalize;
	object_class->set_property = gxps_resources_set_property;

	props[PROP_ARCHIVE] =
		g_param_spec_object ("archive",
				     "Archive",
				     "The document archive",
				     GXPS_TYPE_ARCHIVE,
				     G_PARAM_WRITABLE |
				     G_PARAM_CONSTRUCT_ONLY);

	g_object_class_install_properties (object_class, LAST_PROP, props);
}

static void
gxps_resources_init (GXPSResources *resources)
{
	resources->queue = g_queue_new ();
}

void
gxps_resources_push_dict (GXPSResources *resources)
{
	GHashTable *ht;

	g_return_if_fail (GXPS_IS_RESOURCES (resources));

	ht = g_hash_table_new_full (g_str_hash,
				    g_str_equal,
				    (GDestroyNotify)g_free,
				    (GDestroyNotify)g_free);
	g_queue_push_head (resources->queue, ht);
}

void
gxps_resources_pop_dict (GXPSResources *resources)
{
	GHashTable *ht;

	g_return_if_fail (GXPS_IS_RESOURCES (resources));

	ht = g_queue_pop_head (resources->queue);
	g_hash_table_destroy (ht);
}

const gchar *
gxps_resources_get_resource (GXPSResources *resources,
                             const gchar   *key)
{
	GList *node;

	g_return_val_if_fail (GXPS_IS_RESOURCES (resources), NULL);

	for (node = resources->queue->head; node != NULL; node = node->next) {
		GHashTable *ht;
		gpointer data;

		ht = node->data;
		data = g_hash_table_lookup (ht, key);
		if (data)
			return data;
	}

	return NULL;
}

static gboolean
gxps_resources_set (GXPSResources *resources,
                    gchar         *key,
                    gchar         *value)
{
	GHashTable *ht;

	if (g_queue_get_length (resources->queue) == 0)
		gxps_resources_push_dict (resources);

	ht = g_queue_peek_head (resources->queue);
	if (g_hash_table_contains (ht, key)) {
		g_free (key);
		g_free (value);
		return FALSE;
	}

	g_hash_table_insert (ht, key, value);

	return TRUE;
}

typedef struct {
	GXPSResources *resources;
	gchar *source;

	gchar *key;
	GString *xml;
} GXPSResourceDictContext;

static GXPSResourceDictContext *
gxps_resource_dict_context_new (GXPSResources *resources,
                                const gchar   *source)
{
	GXPSResourceDictContext *resource_dict;

	resource_dict = g_slice_new0 (GXPSResourceDictContext);
	resource_dict->resources = g_object_ref (resources);
	resource_dict->source = g_strdup (source);

	return resource_dict;
}

static void
gxps_resource_dict_context_free (GXPSResourceDictContext *resource_dict)
{
	if (G_UNLIKELY (!resource_dict))
		return;

	g_free (resource_dict->key);
	if (resource_dict->xml)
		g_string_free (resource_dict->xml, TRUE);
	g_object_unref (resource_dict->resources);
	g_slice_free (GXPSResourceDictContext, resource_dict);
}

static void
resource_concat_start_element (GMarkupParseContext  *context,
			       const gchar          *element_name,
			       const gchar         **names,
			       const gchar         **values,
			       gpointer              user_data,
			       GError              **error)
{
	GXPSResourceDictContext *resource_dict_ctx = (GXPSResourceDictContext *)user_data;
	gint i;

	g_string_append_printf (resource_dict_ctx->xml, "<%s",
			        element_name);
	for (i = 0; names[i] != NULL; i++) {
		g_string_append_printf (resource_dict_ctx->xml,
		                        " %s=\"%s\"",
		                        names[i], values[i]);
	}

	g_string_append (resource_dict_ctx->xml, ">\n");
}

static void
resource_concat_end_element (GMarkupParseContext  *context,
			     const gchar          *element_name,
			     gpointer              user_data,
			     GError              **error)
{
	GXPSResourceDictContext *resource_dict_ctx = (GXPSResourceDictContext *)user_data;

	g_string_append_printf (resource_dict_ctx->xml, "</%s>\n",
		                element_name);
}

static GMarkupParser resource_concat_parser = {
	resource_concat_start_element,
	resource_concat_end_element,
	NULL,
	NULL,
	NULL
};

static void
resource_dict_start_element (GMarkupParseContext  *context,
			     const gchar          *element_name,
			     const gchar         **names,
			     const gchar         **values,
			     gpointer              user_data,
			     GError              **error)
{
	GXPSResourceDictContext *resource_dict_ctx = (GXPSResourceDictContext *)user_data;
	gint i;

	for (i = 0; names[i] != NULL; i++) {
		if (strcmp (names[i], "x:Key") == 0) {
			resource_dict_ctx->key = g_strdup (values[i]);
			break;
		}
	}

	if (!resource_dict_ctx->key) {
		gxps_parse_error (context,
				  resource_dict_ctx->source,
				  G_MARKUP_ERROR_MISSING_ATTRIBUTE,
				  element_name, "x:Key",
				  NULL, error);
		return;
	}

	if (!resource_dict_ctx->xml) {
		resource_dict_ctx->xml = g_string_new (NULL);
		g_string_append_printf (resource_dict_ctx->xml, "<%s>\n",
		                        element_name);
	}

	g_string_append_printf (resource_dict_ctx->xml, "<%s",
			        element_name);
	for (i = 0; names[i] != NULL; i++) {
		/* Skip key */
		if (strcmp (names[i], "x:Key") != 0) {
			g_string_append_printf (resource_dict_ctx->xml,
			                        " %s=\"%s\"",
			                        names[i], values[i]);
		}
	}

	g_string_append (resource_dict_ctx->xml, ">\n");

	g_markup_parse_context_push (context, &resource_concat_parser, resource_dict_ctx);
}

static void
resource_dict_end_element (GMarkupParseContext  *context,
			   const gchar          *element_name,
			   gpointer              user_data,
			   GError              **error)
{
	GXPSResourceDictContext *resource_dict_ctx = (GXPSResourceDictContext *)user_data;

	g_string_append_printf (resource_dict_ctx->xml, "</%s>\n</%s>",
		                element_name, element_name);
	gxps_resources_set (resource_dict_ctx->resources,
		            resource_dict_ctx->key,
		            g_string_free (resource_dict_ctx->xml, FALSE));
	resource_dict_ctx->key = NULL;
	resource_dict_ctx->xml = NULL;
	g_markup_parse_context_pop (context);
}

static void
resource_dict_error (GMarkupParseContext *context,
		     GError              *error,
		     gpointer             user_data)
{
	GXPSResourceDictContext *resource_dict_ctx = (GXPSResourceDictContext *)user_data;
	gxps_resource_dict_context_free (resource_dict_ctx);
}

static GMarkupParser resource_dict_parser = {
	resource_dict_start_element,
	resource_dict_end_element,
	NULL,
	NULL,
	resource_dict_error
};

typedef struct
{
	GXPSResources *resources;
	gchar *source;
	gboolean remote;
} GXPSResourceContext;

static GXPSResourceContext *
gxps_resource_context_new (GXPSResources *resources,
                           const gchar   *source)
{
	GXPSResourceContext *context;

	context = g_slice_new0 (GXPSResourceContext);
	context->resources = g_object_ref (resources);
	context->source = g_strdup (source);

	return context;
}

static void
gxps_resource_context_free (GXPSResourceContext *context)
{
	g_object_unref (context->resources);
	g_free (context->source);
	g_slice_free (GXPSResourceContext, context);
}

static void
push_resource_dict_context (GMarkupParseContext *context,
                            GXPSResourceContext *rcontext)
{
	GXPSResourceDictContext *resource_dict_ctx;

	resource_dict_ctx = gxps_resource_dict_context_new (rcontext->resources,
	                                                    rcontext->source);
	g_markup_parse_context_push (context, &resource_dict_parser, resource_dict_ctx);
}

static void
pop_resource_dict_context (GMarkupParseContext *context)
{
	GXPSResourceDictContext *resource_dict_ctx;

	resource_dict_ctx = g_markup_parse_context_pop (context);
	gxps_resource_dict_context_free (resource_dict_ctx);
}

static void
remote_resource_start_element (GMarkupParseContext  *context,
			       const gchar          *element_name,
			       const gchar         **names,
			       const gchar         **values,
			       gpointer              user_data,
			       GError              **error)
{
	GXPSResourceContext *rcontext = (GXPSResourceContext *)user_data;

	if (strcmp (element_name, "ResourceDictionary") == 0) {
		push_resource_dict_context (context, rcontext);
	} else {
		gxps_parse_error (context,
				  rcontext->source,
				  G_MARKUP_ERROR_UNKNOWN_ELEMENT,
				  element_name, NULL, NULL, error);
	}
}

static void
remote_resource_end_element (GMarkupParseContext  *context,
			     const gchar          *element_name,
			     gpointer              user_data,
			     GError              **error)
{
	if (strcmp (element_name, "ResourceDictionary") == 0) {
		pop_resource_dict_context (context);
	}
}

static GMarkupParser remote_resource_parser = {
	remote_resource_start_element,
	remote_resource_end_element,
	NULL,
	NULL,
	NULL
};

static void
resources_start_element (GMarkupParseContext  *context,
			 const gchar          *element_name,
			 const gchar         **names,
			 const gchar         **values,
			 gpointer              user_data,
			 GError              **error)
{
	GXPSResourceContext *rcontext = (GXPSResourceContext *)user_data;
	const gchar *source = NULL;
	gint i;

	if (strcmp (element_name, "ResourceDictionary") == 0) {
		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "Source") == 0)
				source = values[i];
		}

		rcontext->remote = source != NULL;
		if (rcontext->remote) {
			GInputStream *stream;
			gchar *abs_source;
			GMarkupParseContext *parse_ctx;

			abs_source = gxps_resolve_relative_path (rcontext->source,
								 source);
			stream = gxps_archive_open (rcontext->resources->zip, abs_source);
			if (!stream) {
				g_set_error (error,
					     GXPS_ERROR,
					     GXPS_ERROR_SOURCE_NOT_FOUND,
					     "Source %s not found in archive",
					     abs_source);
				g_free (abs_source);
				return;
			}

			parse_ctx = g_markup_parse_context_new (&remote_resource_parser,
								0, rcontext, NULL);
			gxps_parse_stream (parse_ctx, stream, error);
			g_object_unref (stream);
			g_markup_parse_context_free (parse_ctx);
			g_free (abs_source);
		} else {
			push_resource_dict_context (context, rcontext);
		}
	} else {
		gxps_parse_error (context,
				  rcontext->source,
				  G_MARKUP_ERROR_UNKNOWN_ELEMENT,
				  element_name, NULL, NULL, error);
	}
}

static void
resources_end_element (GMarkupParseContext  *context,
		       const gchar          *element_name,
		       gpointer              user_data,
		       GError              **error)
{
	GXPSResourceContext *rcontext = (GXPSResourceContext *)user_data;

	if (strcmp (element_name, "ResourceDictionary") == 0) {
		if (!rcontext->remote) {
			pop_resource_dict_context (context);
		} else {
			rcontext->remote = FALSE;
		}
	}
}

static GMarkupParser resources_parser = {
	resources_start_element,
	resources_end_element,
	NULL,
	NULL,
	NULL
};

void
gxps_resources_parser_push (GMarkupParseContext  *context,
			    GXPSResources        *resources,
			    const gchar          *source)
{
	GXPSResourceContext *rcontext;

	rcontext = gxps_resource_context_new (resources, source);

	g_markup_parse_context_push (context, &resources_parser, rcontext);
}

void
gxps_resources_parser_pop (GMarkupParseContext  *context)
{
	GXPSResourceContext *rcontext;

	rcontext = g_markup_parse_context_pop (context);
	gxps_resource_context_free (rcontext);
}
