/* GXPSPage
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

#include "gxps-page-private.h"
#include "gxps-matrix.h"
#include "gxps-brush.h"
#include "gxps-path.h"
#include "gxps-fonts.h"
#include "gxps-links.h"
#include "gxps-images.h"
#include "gxps-color.h"
#include "gxps-private.h"
#include "gxps-error.h"
#include "gxps-debug.h"

/**
 * SECTION:gxps-page
 * @Short_description: Page of XPS document
 * @Title: GXPSPage
 * @See_also: #GXPSDocument, #GXPSLink, #GXPSLinkTarget
 *
 * #GXPSPage represents a page in a XPS document. #GXPSPage<!-- -->s
 * can be rendered into a cairo context with gxps_page_render().
 * #GXPSPage objects can not be created directly, they are retrieved
 * from a #GXPSDocument with gxps_document_get_page().
 */

enum {
	PROP_0,
	PROP_ARCHIVE,
	PROP_SOURCE
};

static void render_start_element (GMarkupParseContext  *context,
				  const gchar          *element_name,
				  const gchar         **names,
				  const gchar         **values,
				  gpointer              user_data,
				  GError              **error);
static void render_end_element   (GMarkupParseContext  *context,
				  const gchar          *element_name,
				  gpointer              user_data,
				  GError              **error);
static void initable_iface_init  (GInitableIface       *initable_iface);

G_DEFINE_TYPE_WITH_CODE (GXPSPage, gxps_page, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init))

GQuark
gxps_page_error_quark (void)
{
	return g_quark_from_static_string ("gxps-page-error-quark");
}

/* Images */
cairo_surface_t *
gxps_page_get_image (GXPSPage    *page,
		     const gchar *image_uri,
		     GError     **error)
{
	cairo_surface_t *surface;

	if (page->priv->image_cache) {
		surface = g_hash_table_lookup (page->priv->image_cache,
					       image_uri);
		if (surface)
			return cairo_surface_reference (surface);
	}

	surface = gxps_images_get_image (page->priv->zip, image_uri, error);
	if (!surface)
		return NULL;

	if (!page->priv->image_cache) {
		page->priv->image_cache = g_hash_table_new_full (g_str_hash,
								 g_str_equal,
								 (GDestroyNotify)g_free,
								 (GDestroyNotify)cairo_surface_destroy);
	}

	g_hash_table_insert (page->priv->image_cache,
			     g_strdup (image_uri),
			     cairo_surface_reference (surface));
	return surface;
}

/* FixedPage parser */
static void
fixed_page_start_element (GMarkupParseContext  *context,
			  const gchar          *element_name,
			  const gchar         **names,
			  const gchar         **values,
			  gpointer              user_data,
			  GError              **error)
{
	GXPSPage *page = GXPS_PAGE (user_data);
	gint      i;

	if (strcmp (element_name, "FixedPage") == 0) {
		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "Width") == 0) {
				if (!gxps_value_get_double_positive (values[i], &page->priv->width)) {
                                        gxps_parse_error (context,
                                                          page->priv->source,
                                                          G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                                                          element_name, "Width",
                                                          NULL, error);
                                        return;
                                }
			} else if (strcmp (names[i], "Height") == 0) {
				if (!gxps_value_get_double_positive (values[i], &page->priv->height)) {
                                        gxps_parse_error (context,
                                                          page->priv->source,
                                                          G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                                                          element_name, "Height",
                                                          NULL, error);
                                        return;
                                }
			} else if (strcmp (names[i], "xml:lang") == 0) {
				page->priv->lang = g_strdup (values[i]);
			} else if (strcmp (names[i], "ContentBox") == 0) {
				/* TODO */
			} else if (strcmp (names[i], "BleedBox") == 0) {
				/* TODO */
			} else if (strcmp (names[i], "Name") == 0) {
				page->priv->name = g_strdup (values[i]);
			}
		}
	}
}

static const GMarkupParser fixed_page_parser = {
	fixed_page_start_element,
	NULL,
	NULL,
	NULL
};

static gboolean
gxps_page_parse_fixed_page (GXPSPage *page,
			    GError  **error)
{
	GInputStream        *stream;
	GMarkupParseContext *ctx;

	stream = gxps_archive_open (page->priv->zip,
				    page->priv->source);
	if (!stream) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_SOURCE_NOT_FOUND,
			     "Page source %s not found in archive",
			     page->priv->source);

		return FALSE;
	}

	ctx = g_markup_parse_context_new (&fixed_page_parser, 0, page, NULL);
	gxps_parse_stream (ctx, stream, error);
	g_object_unref (stream);
	g_markup_parse_context_free (ctx);

	return (*error != NULL) ? FALSE : TRUE;
}

/* Page Render Parser */
static GMarkupParser render_parser = {
	render_start_element,
	render_end_element,
	NULL,
	NULL
};

void
gxps_page_render_parser_push (GMarkupParseContext *context,
                              GXPSRenderContext   *ctx)
{
        g_markup_parse_context_push (context, &render_parser, ctx);
}

static gboolean
gxps_dash_array_parse (const gchar *dash,
		       gdouble    **dashes_out,
		       guint       *num_dashes_out)
{
	gchar  **items;
	guint    i;
        gdouble *dashes;
        guint    num_dashes;

	items = g_strsplit (dash, " ", -1);
	if (!items)
		return FALSE;

	num_dashes = g_strv_length (items);
	dashes = g_malloc (num_dashes * sizeof (gdouble));

	for (i = 0; i < num_dashes; i++) {
                if (!gxps_value_get_double (items[i], &dashes[i])) {
                        g_free (dashes);
                        g_strfreev (items);

                        return FALSE;
                }
        }

	g_strfreev (items);

        *dashes_out = dashes;
        *num_dashes_out = num_dashes;

	return TRUE;
}

static cairo_line_cap_t
gxps_line_cap_parse (const gchar *cap)
{
	if (strcmp (cap, "Flat") == 0)
		return CAIRO_LINE_CAP_BUTT;
	else if (strcmp (cap, "Round") == 0)
		return CAIRO_LINE_CAP_ROUND;
	else if (strcmp (cap, "Square") == 0)
		return CAIRO_LINE_CAP_SQUARE;
	else if (strcmp (cap, "Triangle") == 0)
		GXPS_DEBUG (g_debug ("Unsupported dash cap Triangle"));

	return CAIRO_LINE_CAP_BUTT;
}

static cairo_line_join_t
gxps_line_join_parse (const gchar *join)
{
	if (strcmp (join, "Miter") == 0)
		return CAIRO_LINE_JOIN_MITER;
	else if (strcmp (join, "Bevel") == 0)
		return CAIRO_LINE_JOIN_BEVEL;
	else if (strcmp (join, "Round") == 0)
		return CAIRO_LINE_JOIN_ROUND;
	return CAIRO_LINE_JOIN_MITER;
}

typedef struct {
	GXPSRenderContext *ctx;

	gdouble            opacity;
	cairo_pattern_t   *opacity_mask;
} GXPSCanvas;

static GXPSCanvas *
gxps_canvas_new (GXPSRenderContext *ctx)
{
	GXPSCanvas *canvas;

	canvas = g_slice_new0 (GXPSCanvas);
	canvas->ctx = ctx;

	/* Default values */
	canvas->opacity = 1.0;

	return canvas;
}

static void
gxps_canvas_free (GXPSCanvas *canvas)
{
	if (G_UNLIKELY (!canvas))
		return;

	cairo_pattern_destroy (canvas->opacity_mask);
	g_slice_free (GXPSCanvas, canvas);
}

static void
canvas_start_element (GMarkupParseContext  *context,
		      const gchar          *element_name,
		      const gchar         **names,
		      const gchar         **values,
		      gpointer              user_data,
		      GError              **error)
{
	GXPSCanvas *canvas = (GXPSCanvas *)user_data;

	if (strcmp (element_name, "Canvas.RenderTransform") == 0) {
		GXPSMatrix *matrix;

		matrix = gxps_matrix_new (canvas->ctx);
		gxps_matrix_parser_push (context, matrix);
	} else if (strcmp (element_name, "Canvas.OpacityMask") == 0) {
		GXPSBrush *brush;

		brush = gxps_brush_new (canvas->ctx);
		gxps_brush_parser_push (context, brush);
	} else {
		render_start_element (context,
				      element_name,
				      names,
				      values,
				      canvas->ctx,
				      error);
	}
}

static void
canvas_end_element (GMarkupParseContext  *context,
		    const gchar          *element_name,
		    gpointer              user_data,
		    GError              **error)
{
	GXPSCanvas *canvas = (GXPSCanvas *)user_data;

	if (strcmp (element_name, "Canvas.RenderTransform") == 0) {
		GXPSMatrix *matrix;

		matrix = g_markup_parse_context_pop (context);
		GXPS_DEBUG (g_message ("transform (%f, %f, %f, %f) [%f, %f]",
			      matrix->matrix.xx, matrix->matrix.yx,
			      matrix->matrix.xy, matrix->matrix.yy,
			      matrix->matrix.x0, matrix->matrix.y0));
		cairo_transform (canvas->ctx->cr, &matrix->matrix);
		gxps_matrix_free (matrix);
	} else if (strcmp (element_name, "Canvas.OpacityMask") == 0) {
		GXPSBrush *brush;

		brush = g_markup_parse_context_pop (context);
		if (!canvas->opacity_mask) {
			canvas->opacity_mask = cairo_pattern_reference (brush->pattern);
			cairo_push_group (canvas->ctx->cr);
		}
		gxps_brush_free (brush);
	} else {
		render_end_element (context,
				    element_name,
				    canvas->ctx,
				    error);
	}
}

static GMarkupParser canvas_parser = {
	canvas_start_element,
	canvas_end_element,
	NULL,
	NULL
};

typedef struct {
	GXPSRenderContext *ctx;
	gdouble            em_size;
	gchar             *font_uri;
	gdouble            origin_x;
	gdouble            origin_y;
	cairo_pattern_t   *fill_pattern;
	gchar             *text;
	gchar             *indices;
	gchar             *clip_data;
        gint               bidi_level;
        guint              is_sideways : 1;
        guint              italic : 1;
	gdouble            opacity;
	cairo_pattern_t   *opacity_mask;
} GXPSGlyphs;

static GXPSGlyphs *
gxps_glyphs_new (GXPSRenderContext *ctx,
		 gchar             *font_uri,
		 gdouble            font_size,
		 gdouble            origin_x,
		 gdouble            origin_y)
{
	GXPSGlyphs *glyphs;

	glyphs = g_slice_new0 (GXPSGlyphs);
	glyphs->ctx = ctx;

	/* Required values */
	glyphs->font_uri = font_uri;
	glyphs->em_size = font_size;
	glyphs->origin_x = origin_x;
	glyphs->origin_y = origin_y;
	glyphs->opacity = 1.0;

	return glyphs;
}

static void
gxps_glyphs_free (GXPSGlyphs *glyphs)
{
	if (G_UNLIKELY (!glyphs))
		return;

	g_free (glyphs->font_uri);
	g_free (glyphs->text);
	g_free (glyphs->indices);
	g_free (glyphs->clip_data);
	cairo_pattern_destroy (glyphs->fill_pattern);
	cairo_pattern_destroy (glyphs->opacity_mask);

	g_slice_free (GXPSGlyphs, glyphs);
}

typedef enum {
	GI_TOKEN_INVALID,
	GI_TOKEN_NUMBER,
	GI_TOKEN_COMMA,
	GI_TOKEN_COLON,
	GI_TOKEN_SEMICOLON,
	GI_TOKEN_START_CLUSTER,
	GI_TOKEN_END_CLUSTER,
	GI_TOKEN_EOF
} GlyphsIndicesTokenType;

typedef struct {
	gchar                 *iter;
	gchar                 *end;
	GlyphsIndicesTokenType type;
	gdouble                number;
} GlyphsIndicesToken;

static const gchar *
glyphs_indices_token_type_to_string (GlyphsIndicesTokenType type)
{
	switch (type) {
	case GI_TOKEN_INVALID:
		return "Invalid";
	case GI_TOKEN_NUMBER:
		return "Number";
	case GI_TOKEN_COMMA:
		return "Comma";
	case GI_TOKEN_COLON:
		return "Colon";
	case GI_TOKEN_SEMICOLON:
		return "Semicolon";
	case GI_TOKEN_START_CLUSTER:
		return "StartCluster";
	case GI_TOKEN_END_CLUSTER:
		return "EndCluster";
	case GI_TOKEN_EOF:
		return "Eof";
	default:
		g_assert_not_reached ();
	}
}

static gboolean
glyphs_indices_iter_next (GlyphsIndicesToken *token,
                          GError            **error)
{
	gchar c;

	if (token->iter == token->end) {
		token->type = GI_TOKEN_EOF;

		return TRUE;
	}

	c = *token->iter;

	if (g_ascii_isdigit (c) || c == '+' || c == '-') {
		gchar *start;
		gchar *str;

		start = token->iter;
		token->iter++;
		while (token->iter != token->end && (g_ascii_isdigit (*token->iter) || *token->iter == '.'))
			token->iter++;
		str = g_strndup (start, token->iter - start);
                if (!gxps_value_get_double (str, &token->number)) {
                        g_set_error (error,
                                     GXPS_PAGE_ERROR,
                                     GXPS_PAGE_ERROR_RENDER,
                                     "Error parsing glyphs indices: error converting token %s (%s) to double at %s",
                                     glyphs_indices_token_type_to_string (token->type),
                                     str, token->iter);
                        g_free (str);

                        return FALSE;
                }
		g_free (str);
		token->type = GI_TOKEN_NUMBER;
	} else if (c == '(') {
		token->type = GI_TOKEN_START_CLUSTER;
		token->iter++;
	} else if (c == ')') {
		token->type = GI_TOKEN_END_CLUSTER;
		token->iter++;
	} else if (c == ',') {
		token->type = GI_TOKEN_COMMA;
		token->iter++;
	} else if (c == ':') {
		token->type = GI_TOKEN_COLON;
		token->iter++;
	} else if (c == ';') {
		token->type = GI_TOKEN_SEMICOLON;
		token->iter++;
	} else {
		token->type = GI_TOKEN_INVALID;
		token->iter++;
	}

        return TRUE;
}

static void
glyphs_indices_parse_error (GlyphsIndicesToken    *token,
			    GlyphsIndicesTokenType expected,
			    GError               **error)
{
	if (expected == GI_TOKEN_INVALID)
		g_set_error (error,
			     GXPS_PAGE_ERROR,
			     GXPS_PAGE_ERROR_RENDER,
			     "Error parsing glyphs indices: unexpected token %s at %s",
			     glyphs_indices_token_type_to_string (token->type),
			     token->iter);
	else
		g_set_error (error,
			     GXPS_PAGE_ERROR,
			     GXPS_PAGE_ERROR_RENDER,
			     "Error parsing glyphs indices: expected token %s, but %s found at %s",
			     glyphs_indices_token_type_to_string (token->type),
			     glyphs_indices_token_type_to_string (expected),
			     token->iter);
}

static gulong
glyphs_lookup_index (cairo_scaled_font_t *scaled_font,
		     const gchar         *utf8)
{
	cairo_status_t status;
	cairo_glyph_t stack_glyphs[1];
	cairo_glyph_t *glyphs = stack_glyphs;
	int num_glyphs = 1;
	int utf8_len = g_utf8_next_char (utf8) - utf8;
	gulong index = 0;

        if (utf8 == NULL || *utf8 == '\0')
                return index;

	status = cairo_scaled_font_text_to_glyphs (scaled_font,
						   0, 0,
						   utf8, utf8_len,
						   &glyphs, &num_glyphs,
						   NULL, NULL, NULL);

	if (status == CAIRO_STATUS_SUCCESS) {
		index = glyphs[0].index;
		if (glyphs != stack_glyphs)
			cairo_glyph_free (glyphs);
	}

	return index;
}

static gboolean
glyphs_indices_parse (const char          *indices,
                      cairo_scaled_font_t *scaled_font,
		      gdouble              x,
		      gdouble              y,
		      const char          *utf8,
                      gint                 bidi_level,
                      gboolean             is_sideways,
		      GArray              *glyph_array,
		      GArray              *cluster_array,
		      GError             **error)
{
	GlyphsIndicesToken    token;
	cairo_text_cluster_t  cluster;
	cairo_glyph_t         glyph;
	gint                  cluster_pos = 1;
	gboolean              have_index = FALSE;
	gdouble               advance_width;
	gdouble               advance_height;
	gboolean              have_advance_width = FALSE;
	gdouble               h_offset = 0;
	gdouble               v_offset = 0;
	cairo_matrix_t        font_matrix;
        cairo_font_extents_t  font_extents;
        gboolean              is_rtl = bidi_level % 2;
        gboolean              eof = FALSE;

        cairo_scaled_font_get_font_matrix (scaled_font, &font_matrix);
        cairo_scaled_font_extents (scaled_font, &font_extents);

        cluster.num_glyphs = 1;
        cluster.num_bytes = 0;

        token.iter = (gchar *)indices;
        token.end = token.iter + strlen (indices);
        if (!glyphs_indices_iter_next (&token, error))
                return FALSE;

        while (1) {
		switch (token.type) {
		case GI_TOKEN_START_CLUSTER: {
                        gint num_code_units;
                        const gchar *utf8_unit_end;

			if (!glyphs_indices_iter_next (&token, error))
                                return FALSE;
			if (token.type != GI_TOKEN_NUMBER) {
				glyphs_indices_parse_error (&token,
							    GI_TOKEN_NUMBER,
							    error);
				return FALSE;
			}

			/* Spec defines ClusterCodeUnitCount in terms of UTF-16 code units */
			num_code_units = (gint)token.number;
			utf8_unit_end = utf8;

			while (utf8 && num_code_units > 0) {
				gunichar utf8_char = g_utf8_get_char (utf8_unit_end);

				if (*utf8_unit_end != '\0')
					utf8_unit_end = g_utf8_next_char (utf8_unit_end);

				num_code_units--;
				if (utf8_char > 0xFFFF) /* 2 code units */
					num_code_units--;
			}
			cluster.num_bytes = utf8_unit_end - utf8;

			if (!glyphs_indices_iter_next (&token, error))
                                return FALSE;
			if (token.type == GI_TOKEN_END_CLUSTER)
				break;

			if (token.type != GI_TOKEN_COLON) {
				glyphs_indices_parse_error (&token,
							    GI_TOKEN_COLON,
							    error);
				return FALSE;
			}

			if (!glyphs_indices_iter_next (&token, error))
                                return FALSE;
			if (token.type != GI_TOKEN_NUMBER) {
				glyphs_indices_parse_error (&token,
							    GI_TOKEN_NUMBER,
							    error);

				return FALSE;
			}

			cluster.num_glyphs = (gint)token.number;
			cluster_pos = (gint)token.number;

			if (!glyphs_indices_iter_next (&token, error))
                                return FALSE;
			if (token.type != GI_TOKEN_END_CLUSTER) {
				glyphs_indices_parse_error (&token,
							    GI_TOKEN_END_CLUSTER,
							    error);
				return FALSE;
			}
                }
			break;
		case GI_TOKEN_NUMBER:
			glyph.index = (gint)token.number;
			have_index = TRUE;
			break;
		case GI_TOKEN_COMMA:
			if (!glyphs_indices_iter_next (&token, error))
                                return FALSE;
			if (token.type == GI_TOKEN_NUMBER) {
				advance_width = token.number / 100.0;
				have_advance_width = TRUE;
				if (!glyphs_indices_iter_next (&token, error))
                                        return FALSE;
			}

			if (token.type != GI_TOKEN_COMMA)
				continue;

			if (!glyphs_indices_iter_next (&token, error))
                                return FALSE;
			if (token.type == GI_TOKEN_NUMBER) {
				h_offset = token.number / 100.0;
				if (!glyphs_indices_iter_next (&token, error))
                                        return FALSE;
			}

			if (token.type != GI_TOKEN_COMMA)
				continue;

			if (!glyphs_indices_iter_next (&token, error))
                                return FALSE;
			if (token.type != GI_TOKEN_NUMBER) {
				glyphs_indices_parse_error (&token,
							    GI_TOKEN_NUMBER,
							    error);

				return FALSE;
			}

			v_offset = token.number / 100.0;
			break;
                case GI_TOKEN_EOF:
                        eof = TRUE;
		case GI_TOKEN_SEMICOLON: {
                        cairo_text_extents_t extents;

			if (!have_index)
				glyph.index = glyphs_lookup_index (scaled_font, utf8);

                        if (is_rtl)
                                h_offset = -h_offset;

                        if (is_sideways) {
                                gdouble tmp = h_offset;

                                h_offset = -v_offset;
                                v_offset = tmp;
                        }

			cairo_matrix_transform_distance (&font_matrix, &h_offset, &v_offset);
			glyph.x = x + h_offset;
			glyph.y = y - v_offset;

			cairo_scaled_font_glyph_extents (scaled_font, &glyph, 1, &extents);
                        if (is_sideways) {
                                glyph.x -= extents.x_bearing;
                                glyph.y -= extents.y_advance / 2;
                        }

                        advance_height = 0;
			if (!have_advance_width) {
                                advance_width = is_sideways ? -extents.x_bearing + font_extents.descent : extents.x_advance;
			} else {
                                if (is_sideways) {
                                        advance_height = advance_width;
                                        advance_width = 0;
                                }
				cairo_matrix_transform_distance (&font_matrix, &advance_width, &advance_height);
			}

                        if (is_rtl) {
                                glyph.x -= extents.x_advance;
                                advance_width = -advance_width;
                        }

			if (utf8 != NULL && *utf8 != '\0' && cluster.num_bytes == 0)
				cluster.num_bytes = g_utf8_next_char (utf8) - utf8;

			if (cluster_pos == 1) {
				utf8 += cluster.num_bytes;
                                if (cluster_array)
                                        g_array_append_val (cluster_array, cluster);
				cluster.num_bytes = 0;
				cluster.num_glyphs = 1;
			} else {
				cluster_pos--;
			}

			x += advance_width;
			y += advance_height;
			have_index = FALSE;
			have_advance_width = FALSE;
			h_offset = 0;
			v_offset = 0;
			g_array_append_val (glyph_array, glyph);

                        if (eof && (utf8 == NULL || *utf8 == '\0'))
                                return TRUE;
                }
			break;
		case GI_TOKEN_INVALID:
			g_set_error (error,
				     GXPS_PAGE_ERROR,
				     GXPS_PAGE_ERROR_RENDER,
				     "Error parsing glyphs indices: Invalid token at %s",
				     token.iter);
			return FALSE;
		default:
			glyphs_indices_parse_error (&token, GI_TOKEN_INVALID, error);
			return FALSE;
		}

		if (!glyphs_indices_iter_next (&token, error))
                        return FALSE;
        }

	return TRUE;
}

static gboolean
gxps_glyphs_to_cairo_glyphs (GXPSGlyphs            *gxps_glyphs,
			     cairo_scaled_font_t   *scaled_font,
			     const gchar           *utf8,
			     cairo_glyph_t        **glyphs,
			     int                   *num_glyphs,
			     cairo_text_cluster_t **clusters,
			     int                   *num_clusters,
			     GError               **error)
{
	GArray  *glyph_array = g_array_new (FALSE, FALSE, sizeof (cairo_glyph_t));
	GArray  *cluster_array = clusters ? g_array_new (FALSE, FALSE, sizeof (cairo_text_cluster_t)) : NULL;
        gboolean success;

        if (!gxps_glyphs->indices) {
                cairo_glyph_t         glyph;
                cairo_text_cluster_t  cluster;
                gboolean              is_rtl = gxps_glyphs->bidi_level % 2;
                gboolean              is_sideways = gxps_glyphs->is_sideways;
                double                x = gxps_glyphs->origin_x;
                double                y = gxps_glyphs->origin_y;
                cairo_font_extents_t  font_extents;

                if (utf8 == NULL || *utf8 == '\0') {
                        g_set_error (error,
                                     GXPS_PAGE_ERROR,
                                     GXPS_PAGE_ERROR_RENDER,
                                     "Error parsing glyphs: Both UnicodeString and Indices are empty");

                        *num_glyphs = 0;
                        *glyphs = NULL;
                        g_array_free (glyph_array, TRUE);

                        if (clusters) {
                                *num_clusters = 0;
                                *clusters = NULL;
                                g_array_free (cluster_array, TRUE);
                        }

                        return FALSE;
                }

                cluster.num_glyphs = 1;
                cairo_scaled_font_extents (scaled_font, &font_extents);

                do {
                        cairo_text_extents_t extents;
                        gdouble              advance_width;

                        glyph.index = glyphs_lookup_index (scaled_font, utf8);
                        glyph.x = x;
                        glyph.y = y;
                        cluster.num_bytes = g_utf8_next_char (utf8) - utf8;

                        cairo_scaled_font_glyph_extents (scaled_font, &glyph, 1, &extents);
                        if (is_sideways) {
                                glyph.x -= extents.x_bearing;
                                glyph.y -= extents.y_advance / 2;
                        }

                        advance_width = is_sideways ? -extents.x_bearing + font_extents.descent : extents.x_advance;

                        if (is_rtl) {
                                glyph.x -= extents.x_advance;
                                advance_width = -advance_width;
                        }

                        x += advance_width;

                        g_array_append_val (glyph_array, glyph);
                        if (cluster_array)
                                g_array_append_val (cluster_array, cluster);

                        utf8 += cluster.num_bytes;
                } while (utf8 != NULL && *utf8 != '\0');
        } else {
                success = glyphs_indices_parse (gxps_glyphs->indices,
                                                scaled_font,
                                                gxps_glyphs->origin_x,
                                                gxps_glyphs->origin_y,
                                                utf8,
                                                gxps_glyphs->bidi_level,
                                                gxps_glyphs->is_sideways,
                                                glyph_array,
                                                cluster_array,
                                                error);
                if (!success) {
                        *num_glyphs = 0;
                        *glyphs = NULL;
                        g_array_free (glyph_array, TRUE);

                        if (clusters) {
                                *num_clusters = 0;
                                *clusters = NULL;
                                g_array_free (cluster_array, TRUE);
                        }

                        return FALSE;
                }
        }

	*num_glyphs = glyph_array->len;
	*glyphs = (cairo_glyph_t *)g_array_free (glyph_array, FALSE);
        if (clusters) {
                *num_clusters = cluster_array->len;
                *clusters = (cairo_text_cluster_t *)g_array_free (cluster_array, FALSE);
        }

	return TRUE;
}

static void
glyphs_start_element (GMarkupParseContext  *context,
		      const gchar          *element_name,
		      const gchar         **names,
		      const gchar         **values,
		      gpointer              user_data,
		      GError              **error)
{
	GXPSGlyphs *glyphs = (GXPSGlyphs *)user_data;

	if (strcmp (element_name, "Glyphs.RenderTransform") == 0) {
		GXPSMatrix *matrix;

		matrix = gxps_matrix_new (glyphs->ctx);
		gxps_matrix_parser_push (context, matrix);
	} else if (strcmp (element_name, "Glyphs.Clip") == 0) {
	} else if (strcmp (element_name, "Glyphs.Fill") == 0) {
		GXPSBrush *brush;

		brush = gxps_brush_new (glyphs->ctx);
		gxps_brush_parser_push (context, brush);
	} else if (strcmp (element_name, "Glyphs.OpacityMask") == 0) {
		GXPSBrush *brush;

		brush = gxps_brush_new (glyphs->ctx);
		gxps_brush_parser_push (context, brush);
	} else {
	}
}

static void
glyphs_end_element (GMarkupParseContext  *context,
		    const gchar          *element_name,
		    gpointer              user_data,
		    GError              **error)
{
	GXPSGlyphs *glyphs = (GXPSGlyphs *)user_data;

	if (strcmp (element_name, "Glyphs.RenderTransform") == 0) {
		GXPSMatrix *matrix;

		matrix = g_markup_parse_context_pop (context);
		GXPS_DEBUG (g_message ("transform (%f, %f, %f, %f) [%f, %f]",
			      matrix->matrix.xx, matrix->matrix.yx,
			      matrix->matrix.xy, matrix->matrix.yy,
			      matrix->matrix.x0, matrix->matrix.y0));
		cairo_transform (glyphs->ctx->cr, &matrix->matrix);

		gxps_matrix_free (matrix);
	} else if (strcmp (element_name, "Glyphs.Clip") == 0) {
	} else if (strcmp (element_name, "Glyphs.Fill") == 0) {
		GXPSBrush *brush;

		brush = g_markup_parse_context_pop (context);
		glyphs->fill_pattern = cairo_pattern_reference (brush->pattern);
		gxps_brush_free (brush);
	} else if (strcmp (element_name, "Glyphs.OpacityMask") == 0) {
		GXPSBrush *brush;

		brush = g_markup_parse_context_pop (context);
		if (!glyphs->opacity_mask) {
			glyphs->opacity_mask = cairo_pattern_reference (brush->pattern);
			cairo_push_group (glyphs->ctx->cr);
		}
		gxps_brush_free (brush);
	} else {
	}
}

static GMarkupParser glyphs_parser = {
	glyphs_start_element,
	glyphs_end_element,
	NULL,
	NULL
};

static void
render_start_element (GMarkupParseContext  *context,
		      const gchar          *element_name,
		      const gchar         **names,
		      const gchar         **values,
		      gpointer              user_data,
		      GError              **error)
{
	GXPSRenderContext *ctx = (GXPSRenderContext *)user_data;

	if (strcmp (element_name, "Path") == 0) {
		GXPSPath *path;
		gint      i;

		GXPS_DEBUG (g_message ("save"));
		cairo_save (ctx->cr);

		path = gxps_path_new (ctx);

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "Data") == 0) {
				path->data = g_strdup (values[i]);
			} else if (strcmp (names[i], "RenderTransform") == 0) {
				cairo_matrix_t matrix;

				if (!gxps_matrix_parse (values[i], &matrix)) {
					gxps_parse_error (context,
							  ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "Path", "RenderTransform", values[i], error);
					return;
				}
				GXPS_DEBUG (g_message ("transform (%f, %f, %f, %f) [%f, %f]",
					      matrix.xx, matrix.yx,
					      matrix.xy, matrix.yy,
					      matrix.x0, matrix.y0));
				cairo_transform (ctx->cr, &matrix);
			} else if (strcmp (names[i], "Clip") == 0) {
				path->clip_data = g_strdup (values[i]);
			} else if (strcmp (names[i], "Fill") == 0) {
				GXPS_DEBUG (g_message ("set_fill_pattern (solid)"));
                                if (!gxps_brush_solid_color_parse (values[i], ctx->page->priv->zip, 1., &path->fill_pattern)) {
					gxps_parse_error (context,
							  ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "Path", "Fill", values[i], error);
					return;
				}
			} else if (strcmp (names[i], "Stroke") == 0) {
				GXPS_DEBUG (g_message ("set_stroke_pattern (solid)"));
                                if (!gxps_brush_solid_color_parse (values[i], ctx->page->priv->zip, 1., &path->stroke_pattern)) {
					gxps_parse_error (context,
							  ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "Path", "Stroke", values[i], error);
					return;
				}
			} else if (strcmp (names[i], "StrokeThickness") == 0) {
                                if (!gxps_value_get_double (values[i], &path->line_width)) {
                                        gxps_parse_error (context,
                                                          ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "Path", "StrokeThickness", values[i], error);
                                        return;
                                }
				GXPS_DEBUG (g_message ("set_line_width (%f)", path->line_width));
			} else if (strcmp (names[i], "StrokeDashArray") == 0) {
				if (!gxps_dash_array_parse (values[i], &path->dash, &path->dash_len)) {
					gxps_parse_error (context,
							  ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "Path", "StrokeDashArray", values[i], error);
					return;
				}
				GXPS_DEBUG (g_message ("set_dash"));
			} else if (strcmp (names[i], "StrokeDashOffset") == 0) {
                                if (!gxps_value_get_double (values[i], &path->dash_offset)) {
                                        gxps_parse_error (context,
                                                          ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "Path", "StrokeDashOffset", values[i], error);
                                        return;
                                }
				GXPS_DEBUG (g_message ("set_dash_offset (%f)", path->dash_offset));
			} else if (strcmp (names[i], "StrokeDashCap") == 0) {
				path->line_cap = gxps_line_cap_parse (values[i]);
				GXPS_DEBUG (g_message ("set_line_cap (%s)", values[i]));
			} else if (strcmp (names[i], "StrokeLineJoin") == 0) {
				path->line_join = gxps_line_join_parse (values[i]);
				GXPS_DEBUG (g_message ("set_line_join (%s)", values[i]));
			} else if (strcmp (names[i], "StrokeMiterLimit") == 0) {
                                if (!gxps_value_get_double (values[i], &path->miter_limit)) {
                                        gxps_parse_error (context,
                                                          ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "Path", "StrokeMiterLimit", values[i], error);
                                        return;
                                }
				GXPS_DEBUG (g_message ("set_miter_limit (%f)", path->miter_limit));
			} else if (strcmp (names[i], "Opacity") == 0) {
                                if (!gxps_value_get_double (values[i], &path->opacity)) {
                                        gxps_parse_error (context,
                                                          ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "Path", "Opacity", values[i], error);
                                        return;
                                }
				GXPS_DEBUG (g_message ("set_opacity (%f)", path->opacity));
			}
		}

		if (path->opacity != 1.0)
			cairo_push_group (ctx->cr);
		gxps_path_parser_push (context, path);
	} else if (strcmp (element_name, "Glyphs") == 0) {
		GXPSGlyphs  *glyphs;
		gchar       *font_uri = NULL;
		gdouble      font_size = -1;
		gdouble      x = -1, y = -1;
		const gchar *text = NULL;
		const gchar *fill_color = NULL;
		const gchar *indices = NULL;
		const gchar *clip_data = NULL;
                gint         bidi_level = 0;
                gboolean     is_sideways = FALSE;
                gboolean     italic = FALSE;
		gdouble      opacity = 1.0;
		gint         i;

		GXPS_DEBUG (g_message ("save"));
		cairo_save (ctx->cr);

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "FontRenderingEmSize") == 0) {
                                if (!gxps_value_get_double (values[i], &font_size)) {
                                        gxps_parse_error (context,
                                                          ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "Glyphs", "FontRenderingEmSize",
                                                          values[i], error);
                                        g_free (font_uri);
                                        return;
                                }
			} else if (strcmp (names[i], "FontUri") == 0) {
				font_uri = gxps_resolve_relative_path (ctx->page->priv->source,
								       values[i]);
			} else if (strcmp (names[i], "OriginX") == 0) {
                                if (!gxps_value_get_double (values[i], &x)) {
                                        gxps_parse_error (context,
                                                          ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "Glyphs", "OriginX",
                                                          values[i], error);
                                        g_free (font_uri);
                                        return;
                                }
			} else if (strcmp (names[i], "OriginY") == 0) {
                                if (!gxps_value_get_double (values[i], &y)) {
                                        gxps_parse_error (context,
                                                          ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "Glyphs", "OriginY",
                                                          values[i], error);
                                        g_free (font_uri);
                                        return;
                                }
			} else if (strcmp (names[i], "UnicodeString") == 0) {
				text = values[i];
			} else if (strcmp (names[i], "Fill") == 0) {
				fill_color = values[i];
			} else if (strcmp (names[i], "Indices") == 0) {
				indices = values[i];
			} else if (strcmp (names[i], "RenderTransform") == 0) {
				cairo_matrix_t matrix;

				if (!gxps_matrix_parse (values[i], &matrix)) {
					gxps_parse_error (context,
							  ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "Glyphs", "RenderTransform",
							  values[i], error);
					g_free (font_uri);
					return;
				}

				GXPS_DEBUG (g_message ("transform (%f, %f, %f, %f) [%f, %f]",
					      matrix.xx, matrix.yx,
					      matrix.xy, matrix.yy,
					      matrix.x0, matrix.y0));
				cairo_transform (ctx->cr, &matrix);
			} else if (strcmp (names[i], "Clip") == 0) {
				clip_data = values[i];
                        } else if (strcmp (names[i], "BidiLevel") == 0) {
                                if (!gxps_value_get_int (values[i], &bidi_level)) {
                                        gxps_parse_error (context,
                                                          ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "Glyphs", "BidiLevel",
                                                          values[i], error);
                                        g_free (font_uri);
                                        return;
                                }
                        } else if (strcmp (names[i], "IsSideways") == 0) {
                                if (!gxps_value_get_boolean (values[i], &is_sideways)) {
                                        gxps_parse_error (context,
                                                          ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "Glyphs", "IsSideways",
                                                          values[i], error);
                                        g_free (font_uri);
                                        return;
                                }
			} else if (strcmp (names[i], "Opacity") == 0) {
                                if (!gxps_value_get_double (values[i], &opacity)) {
                                        gxps_parse_error (context,
                                                          ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "Glyphs", "Opacity",
                                                          values[i], error);
                                        g_free (font_uri);
                                        return;
                                }
                        } else if (strcmp (names[i], "StyleSimulations") == 0) {
                                if (strcmp (values[i], "ItalicSimulation") == 0) {
                                        italic = TRUE;
                                }
			}
		}

		if (!font_uri || font_size == -1 || x == -1 || y == -1) {
			if (!font_uri) {
				gxps_parse_error (context,
						  ctx->page->priv->source,
						  G_MARKUP_ERROR_MISSING_ATTRIBUTE,
						  element_name, "FontUri", NULL, error);
			} else if (font_size == -1) {
				gxps_parse_error (context,
						  ctx->page->priv->source,
						  G_MARKUP_ERROR_MISSING_ATTRIBUTE,
						  element_name, "FontRenderingEmSize", NULL, error);
			} else if (x == -1 || y == -1) {
				gxps_parse_error (context,
						  ctx->page->priv->source,
						  G_MARKUP_ERROR_MISSING_ATTRIBUTE,
						  element_name,
						  (x == -1) ? "OriginX" : "OriginY", NULL, error);
			}

			g_free (font_uri);
			return;
		}

		/* GXPSGlyphs takes ownership of font_uri */
		glyphs = gxps_glyphs_new (ctx, font_uri, font_size, x, y);
		glyphs->text = g_strdup (text);
		glyphs->indices = g_strdup (indices);
		glyphs->clip_data = g_strdup (clip_data);
                glyphs->bidi_level = bidi_level;
                glyphs->is_sideways = is_sideways;
                glyphs->italic = italic;
		glyphs->opacity = opacity;
		if (fill_color) {
			GXPS_DEBUG (g_message ("set_fill_pattern (solid)"));
                        gxps_brush_solid_color_parse (fill_color, ctx->page->priv->zip, 1., &glyphs->fill_pattern);
		}

		if (glyphs->opacity != 1.0)
			cairo_push_group (glyphs->ctx->cr);
		g_markup_parse_context_push (context, &glyphs_parser, glyphs);
	} else if (strcmp (element_name, "Canvas") == 0) {
		GXPSCanvas *canvas;
		gint i;

		GXPS_DEBUG (g_message ("save"));
		cairo_save (ctx->cr);

		canvas = gxps_canvas_new (ctx);

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "RenderTransform") == 0) {
				cairo_matrix_t matrix;

				if (!gxps_matrix_parse (values[i], &matrix)) {
					gxps_parse_error (context,
							  ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "Canvas", "RenderTransform", values[i], error);
					gxps_canvas_free (canvas);
					return;
				}
				GXPS_DEBUG (g_message ("transform (%f, %f, %f, %f) [%f, %f]",
					      matrix.xx, matrix.yx,
					      matrix.xy, matrix.yy,
					      matrix.x0, matrix.y0));
				cairo_transform (ctx->cr, &matrix);
			} else if (strcmp (names[i], "Opacity") == 0) {
                                if (!gxps_value_get_double (values[i], &canvas->opacity)) {
                                        gxps_parse_error (context,
                                                          ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "Canvas", "Opacity", values[i], error);
                                        gxps_canvas_free (canvas);
                                        return;
                                }
				GXPS_DEBUG (g_message ("set_opacity (%f)", canvas->opacity));
			} else if (strcmp (names[i], "Clip") == 0) {
				if (!gxps_path_parse (values[i], ctx->cr, error)) {
					gxps_parse_error (context,
							  ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "Canvas", "Clip", values[i], error);
					gxps_canvas_free (canvas);
					return;
				}
				GXPS_DEBUG (g_message ("clip"));
				cairo_clip (ctx->cr);
			}
		}
		if (canvas->opacity != 1.0)
			cairo_push_group (canvas->ctx->cr);
		g_markup_parse_context_push (context, &canvas_parser, canvas);
	} else if (strcmp (element_name, "FixedPage") == 0) {
		/* Do Nothing */
	} else {
		/* TODO: error */
	}
}

static void
render_end_element (GMarkupParseContext  *context,
		    const gchar          *element_name,
		    gpointer              user_data,
		    GError              **error)
{
	GXPSRenderContext *ctx = (GXPSRenderContext *)user_data;

	if (strcmp (element_name, "Path") == 0) {
		GXPSPath *path;

		path = g_markup_parse_context_pop (context);

		if (!path->data) {
			GXPS_DEBUG (g_message ("restore"));
			/* Something may have been drawn in a PathGeometry */
			if (path->opacity != 1.0) {
				cairo_pop_group_to_source (ctx->cr);
				cairo_paint_with_alpha (ctx->cr, path->opacity);
			}
			cairo_restore (ctx->cr);
			gxps_path_free (path);
			return;
		}

		cairo_set_fill_rule (ctx->cr, path->fill_rule);

		if (path->clip_data) {
			if (!gxps_path_parse (path->clip_data, ctx->cr, error)) {
				if (path->opacity != 1.0)
					cairo_pattern_destroy (cairo_pop_group (ctx->cr));
				gxps_path_free (path);
				return;
			}
			GXPS_DEBUG (g_message ("clip"));
			cairo_clip (ctx->cr);
		}

		if (!gxps_path_parse (path->data, ctx->cr, error)) {
			if (path->opacity != 1.0)
				cairo_pattern_destroy (cairo_pop_group (ctx->cr));
			gxps_path_free (path);
			return;
		}

		if (path->fill_pattern) {
			GXPS_DEBUG (g_message ("fill"));

			cairo_set_source (ctx->cr, path->fill_pattern);
			if (path->stroke_pattern)
				cairo_fill_preserve (ctx->cr);
			else
				cairo_fill (ctx->cr);
		}

		if (path->stroke_pattern) {
			GXPS_DEBUG (g_message ("stroke"));
			cairo_set_source (ctx->cr, path->stroke_pattern);
			cairo_set_line_width (ctx->cr, path->line_width);
			if (path->dash && path->dash_len > 0)
				cairo_set_dash (ctx->cr, path->dash, path->dash_len, path->dash_offset);
			/* FIXME: square cap doesn't work with dashed lines */
//					cairo_set_line_cap (ctx->cr, path->line_cap);
			cairo_set_line_join (ctx->cr, path->line_join);
			cairo_set_miter_limit (ctx->cr, path->miter_limit);
			cairo_stroke (ctx->cr);
		}

		if (path->opacity != 1.0) {
			cairo_pop_group_to_source (ctx->cr);
			cairo_paint_with_alpha (ctx->cr, path->opacity);
		}
		gxps_path_free (path);

		GXPS_DEBUG (g_message ("restore"));
		cairo_restore (ctx->cr);
	} else if (strcmp (element_name, "Glyphs") == 0) {
		GXPSGlyphs           *glyphs;
		gchar                *utf8;
		cairo_text_cluster_t *cluster_list = NULL;
		gint                  num_clusters;
		cairo_glyph_t        *glyph_list = NULL;
		gint                  num_glyphs;
		cairo_matrix_t        ctm, font_matrix;
		cairo_font_face_t    *font_face;
		cairo_font_options_t *font_options;
		cairo_scaled_font_t  *scaled_font;
                gboolean              use_show_text_glyphs;
                gboolean              success;

		glyphs = g_markup_parse_context_pop (context);

		font_face = gxps_fonts_get_font (ctx->page->priv->zip, glyphs->font_uri, error);
		if (!font_face) {
			if (glyphs->opacity_mask)
				cairo_pattern_destroy (cairo_pop_group (ctx->cr));
			if (glyphs->opacity != 1.0)
				cairo_pattern_destroy (cairo_pop_group (ctx->cr));
			gxps_glyphs_free (glyphs);

			GXPS_DEBUG (g_message ("restore"));
			cairo_restore (ctx->cr);
			return;
		}

		if (glyphs->clip_data) {
			if (!gxps_path_parse (glyphs->clip_data, ctx->cr, error)) {
				if (glyphs->opacity_mask)
					cairo_pattern_destroy (cairo_pop_group (ctx->cr));
				if (glyphs->opacity != 1.0)
					cairo_pattern_destroy (cairo_pop_group (ctx->cr));
				gxps_glyphs_free (glyphs);
				GXPS_DEBUG (g_message ("restore"));
				cairo_restore (ctx->cr);
				return;
			}
			GXPS_DEBUG (g_message ("clip"));
			cairo_clip (ctx->cr);
		}

		font_options = cairo_font_options_create ();
		cairo_get_font_options (ctx->cr, font_options);

		cairo_matrix_init_identity (&font_matrix);
		cairo_matrix_scale (&font_matrix, glyphs->em_size, glyphs->em_size);
		cairo_get_matrix (ctx->cr, &ctm);

                /* italics is 20 degrees slant.  0.342 = sin(20 deg) */
                if (glyphs->italic)
                        font_matrix.xy = glyphs->em_size * -0.342;

                if (glyphs->is_sideways)
                        cairo_matrix_rotate (&font_matrix, -G_PI_2);

		scaled_font = cairo_scaled_font_create (font_face,
							&font_matrix,
							&ctm,
							font_options);

		cairo_font_options_destroy (font_options);

                /* UnicodeString may begin with escape sequence "{}" */
                utf8 = glyphs->text;
                if (utf8 && g_str_has_prefix (utf8, "{}"))
                        utf8 += 2;

                use_show_text_glyphs = cairo_surface_has_show_text_glyphs (cairo_get_target (ctx->cr));

		success = gxps_glyphs_to_cairo_glyphs (glyphs, scaled_font, utf8,
						       &glyph_list, &num_glyphs,
						       use_show_text_glyphs ? &cluster_list : NULL,
                                                       use_show_text_glyphs ? &num_clusters : NULL,
						       error);
		if (!success) {
			if (glyphs->opacity_mask)
				cairo_pattern_destroy (cairo_pop_group (ctx->cr));
			if (glyphs->opacity != 1.0)
				cairo_pattern_destroy (cairo_pop_group (ctx->cr));
			gxps_glyphs_free (glyphs);
			cairo_scaled_font_destroy (scaled_font);
			GXPS_DEBUG (g_message ("restore"));
			cairo_restore (ctx->cr);
			return;
		}

		if (glyphs->fill_pattern)
			cairo_set_source (ctx->cr, glyphs->fill_pattern);

		GXPS_DEBUG (g_message ("show_text (%s)", glyphs->text));

		cairo_set_scaled_font (ctx->cr, scaled_font);
                if (use_show_text_glyphs) {
                        cairo_show_text_glyphs (ctx->cr, utf8, -1,
                                                glyph_list, num_glyphs,
                                                cluster_list, num_clusters,
                                                0);
                        g_free (cluster_list);
                } else {
                        cairo_show_glyphs (ctx->cr, glyph_list, num_glyphs);
                }

		if (glyphs->opacity_mask) {
			cairo_pop_group_to_source (ctx->cr);
			cairo_mask (ctx->cr, glyphs->opacity_mask);
		}
		if (glyphs->opacity != 1.0) {
			cairo_pop_group_to_source (ctx->cr);
			cairo_paint_with_alpha (ctx->cr, glyphs->opacity);
		}
		g_free (glyph_list);
		gxps_glyphs_free (glyphs);
		cairo_scaled_font_destroy (scaled_font);

		GXPS_DEBUG (g_message ("restore"));
		cairo_restore (ctx->cr);
	} else if (strcmp (element_name, "Canvas") == 0) {
		GXPSCanvas *canvas;

		canvas = g_markup_parse_context_pop (context);

		if (canvas->opacity_mask) {
			cairo_pop_group_to_source (ctx->cr);
			cairo_mask (ctx->cr, canvas->opacity_mask);
		}
		if (canvas->opacity != 1.0) {
			cairo_pop_group_to_source (ctx->cr);
			cairo_paint_with_alpha (ctx->cr, canvas->opacity);
		}
		cairo_restore (ctx->cr);
		GXPS_DEBUG (g_message ("restore"));
		gxps_canvas_free (canvas);
	} else if (strcmp (element_name, "FixedPage") == 0) {
		/* Do Nothing */
	} else {
		/* TODO: error */
	}
}

static gboolean
gxps_page_parse_for_rendering (GXPSPage *page,
			       cairo_t  *cr,
			       GError  **error)
{
	GInputStream        *stream;
	GMarkupParseContext *context;
	GXPSRenderContext    ctx;
	GError              *err = NULL;

	stream = gxps_archive_open (page->priv->zip,
				    page->priv->source);
	if (!stream) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_SOURCE_NOT_FOUND,
			     "Page source %s not found in archive",
			     page->priv->source);
		return FALSE;
	}

	ctx.page = page;
	ctx.cr = cr;

	context = g_markup_parse_context_new (&render_parser, 0, &ctx, NULL);
	gxps_parse_stream (context, stream, &err);
	g_object_unref (stream);
	g_markup_parse_context_free (context);


	if (g_error_matches (err, GXPS_PAGE_ERROR, GXPS_PAGE_ERROR_RENDER)) {
		g_propagate_error (error, err);
	} else if (err) {
		g_set_error (error,
			     GXPS_PAGE_ERROR,
			     GXPS_PAGE_ERROR_RENDER,
			     "Error rendering page %s: %s",
			     page->priv->source, err->message);
		g_error_free (err);
	}

	return (*error != NULL) ? FALSE : TRUE;
}

/* Links */
typedef struct {
	GXPSPage *page;
	cairo_t  *cr;

	GList    *st;
	GList    *links;
	gboolean  do_transform;
} GXPSLinksContext;

typedef struct {
	gchar *data;
	gchar *uri;
} GXPSPathLink;

static void
links_start_element (GMarkupParseContext  *context,
		     const gchar          *element_name,
		     const gchar         **names,
		     const gchar         **values,
		     gpointer              user_data,
		     GError              **error)
{
	GXPSLinksContext *ctx = (GXPSLinksContext *)user_data;

	if (strcmp (element_name, "Canvas") == 0) {
		gint i;

		GXPS_DEBUG (g_message ("save"));
		cairo_save (ctx->cr);

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "RenderTransform") == 0) {
				cairo_matrix_t matrix;

				if (!gxps_matrix_parse (values[i], &matrix)) {
					gxps_parse_error (context,
							  ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "Canvas", "RenderTransform", values[i], error);
					return;
				}
				GXPS_DEBUG (g_message ("transform (%f, %f, %f, %f) [%f, %f]",
					      matrix.xx, matrix.yx,
					      matrix.xy, matrix.yy,
					      matrix.x0, matrix.y0));
				cairo_transform (ctx->cr, &matrix);

				return;
			} else if (strcmp (names[i], "Clip") == 0) {
				/* FIXME: do we really need clips? */
				if (!gxps_path_parse (values[i], ctx->cr, error))
					return;
				GXPS_DEBUG (g_message ("clip"));
				cairo_clip (ctx->cr);
			}
		}
	} else if (strcmp (element_name, "Path") == 0) {
		gint i;
		GXPSPathLink *path_link;
		const gchar *data = NULL;
		const gchar *link_uri = NULL;

		GXPS_DEBUG (g_message ("save"));
		cairo_save (ctx->cr);

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "Data") == 0) {
				data = values[i];
			} else if (strcmp (names[i], "RenderTransform") == 0) {
				cairo_matrix_t matrix;

				if (!gxps_matrix_parse (values[i], &matrix)) {
					gxps_parse_error (context,
							  ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "Path", "RenderTransform", values[i], error);
					return;
				}
				GXPS_DEBUG (g_message ("transform (%f, %f, %f, %f) [%f, %f]",
					      matrix.xx, matrix.yx,
					      matrix.xy, matrix.yy,
					      matrix.x0, matrix.y0));
				cairo_transform (ctx->cr, &matrix);
			} else if (strcmp (names[i], "FixedPage.NavigateUri") == 0) {
				link_uri = values[i];
			}
		}

		path_link = g_slice_new0 (GXPSPathLink);
		if (link_uri) {
			path_link->data = data ? g_strdup (data) : NULL;
			path_link->uri = gxps_resolve_relative_path (ctx->page->priv->source, link_uri);
		}

		ctx->st = g_list_prepend (ctx->st, path_link);
	} else if (strcmp (element_name, "Glyphs") == 0) {
		gint i;

		GXPS_DEBUG (g_message ("save"));
		cairo_save (ctx->cr);

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "RenderTransform") == 0) {
				cairo_matrix_t matrix;

				if (!gxps_matrix_parse (values[i], &matrix)) {
					gxps_parse_error (context,
							  ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "Glyphs", "RenderTransform", values[i], error);
					return;
				}
				GXPS_DEBUG (g_message ("transform (%f, %f, %f, %f) [%f, %f]",
					      matrix.xx, matrix.yx,
					      matrix.xy, matrix.yy,
					      matrix.x0, matrix.y0));
				cairo_transform (ctx->cr, &matrix);
			} else if (strcmp (names[i], "FixedPage.NavigateUri") == 0) {
				/* TODO */
			}
		}
	} else if (strcmp (element_name, "Canvas.RenderTransform") == 0 ||
		   strcmp (element_name, "Path.RenderTransform") == 0 ||
		   strcmp (element_name, "Glyphs.RenderTransform") == 0 ) {
		ctx->do_transform = TRUE;
	} else if (strcmp (element_name, "MatrixTransform") == 0) {
		gint i;

		if (!ctx->do_transform) {
			return;
		}

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "Matrix") == 0) {
				cairo_matrix_t matrix;

				if (!gxps_matrix_parse (values[i], &matrix)) {
					gxps_parse_error (context,
							  ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "MatrixTransform", "Matrix",
							  values[i], error);
					return;
				}
				GXPS_DEBUG (g_message ("transform (%f, %f, %f, %f) [%f, %f]",
					      matrix.xx, matrix.yx,
					      matrix.xy, matrix.yy,
					      matrix.x0, matrix.y0));
				cairo_transform (ctx->cr, &matrix);
				return;
			}
		}
	}
}

static void
links_end_element (GMarkupParseContext  *context,
		   const gchar          *element_name,
		   gpointer              user_data,
		   GError              **error)
{
	GXPSLinksContext *ctx = (GXPSLinksContext *)user_data;

	if (strcmp (element_name, "Canvas") == 0) {
		GXPS_DEBUG (g_message ("restore"));
		cairo_restore (ctx->cr);
	} else if (strcmp (element_name, "Path") == 0) {
		GXPSPathLink *path_link;

		path_link = (GXPSPathLink *)ctx->st->data;
		ctx->st = g_list_delete_link (ctx->st, ctx->st);
		if (path_link->uri) {
			GXPSLink         *link;
			gdouble           x1, y1, x2, y2;
			cairo_rectangle_t area;

			if (path_link->data)
				gxps_path_parse (path_link->data, ctx->cr, error);

			cairo_path_extents (ctx->cr, &x1, &y1, &x2, &y2);
			cairo_user_to_device (ctx->cr, &x1, &y1);
			cairo_user_to_device (ctx->cr, &x2, &y2);

			area.x = x1;
			area.y = y1;
			area.width = x2 - x1;
			area.height = y2 - y1;
			link = _gxps_link_new (ctx->page->priv->zip, &area, path_link->uri);
			ctx->links = g_list_prepend (ctx->links, link);
			g_free (path_link->uri);
		}
		g_free (path_link->data);
		g_slice_free (GXPSPathLink, path_link);
		cairo_new_path (ctx->cr);
		GXPS_DEBUG (g_message ("restore"));
		cairo_restore (ctx->cr);
	} else if (strcmp (element_name, "Glyphs") == 0) {
		GXPS_DEBUG (g_message ("restore"));
		cairo_restore (ctx->cr);
	} else if (strcmp (element_name, "Canvas.RenderTransform") == 0 ||
		   strcmp (element_name, "Path.RenderTransform") == 0 ||
		   strcmp (element_name, "Glyphs.RenderTransform") == 0 ) {
		ctx->do_transform = FALSE;
	}
}

static const GMarkupParser links_parser = {
	links_start_element,
	links_end_element,
	NULL,
	NULL,
	NULL
};

static GList *
gxps_page_parse_links (GXPSPage *page,
		       cairo_t  *cr,
		       GError  **error)
{
	GInputStream        *stream;
	GXPSLinksContext     ctx;
	GMarkupParseContext *context;

	stream = gxps_archive_open (page->priv->zip,
				    page->priv->source);
	if (!stream) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_SOURCE_NOT_FOUND,
			     "Page source %s not found in archive",
			     page->priv->source);
		return FALSE;
	}

	ctx.cr = cr;
	ctx.page = page;
	ctx.st = NULL;
	ctx.links = NULL;

	context = g_markup_parse_context_new (&links_parser, 0, &ctx, NULL);
	gxps_parse_stream (context, stream, error);
	g_object_unref (stream);
	g_markup_parse_context_free (context);

	return ctx.links;
}

typedef struct {
	GXPSPage   *page;
	cairo_t    *cr;

	GList      *st;
	GHashTable *anchors;
	gboolean    do_transform;
} GXPSAnchorsContext;

typedef struct {
	gchar *data;
	gchar *name;
} GXPSPathAnchor;

static void
anchors_start_element (GMarkupParseContext  *context,
		       const gchar          *element_name,
		       const gchar         **names,
		       const gchar         **values,
		       gpointer              user_data,
		       GError              **error)
{
	GXPSAnchorsContext *ctx = (GXPSAnchorsContext *)user_data;

	if (strcmp (element_name, "Canvas") == 0) {
		gint i;

		GXPS_DEBUG (g_message ("save"));
		cairo_save (ctx->cr);

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "RenderTransform") == 0) {
				cairo_matrix_t matrix;

				if (!gxps_matrix_parse (values[i], &matrix)) {
					gxps_parse_error (context,
							  ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "Canvas", "RenderTransform", values[i], error);
					return;
				}
				GXPS_DEBUG (g_message ("transform (%f, %f, %f, %f) [%f, %f]",
					      matrix.xx, matrix.yx,
					      matrix.xy, matrix.yy,
					      matrix.x0, matrix.y0));
				cairo_transform (ctx->cr, &matrix);

				return;
			}
		}
	} else if (strcmp (element_name, "Path") == 0) {
		gint i;
		GXPSPathAnchor *path_anchor;
		const gchar *data = NULL;
		const gchar *name = NULL;

		GXPS_DEBUG (g_message ("save"));
		cairo_save (ctx->cr);

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "Data") == 0) {
				data = values[i];
			} else if (strcmp (names[i], "RenderTransform") == 0) {
				cairo_matrix_t matrix;

				if (!gxps_matrix_parse (values[i], &matrix)) {
					gxps_parse_error (context,
							  ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "Path", "RenderTransform", values[i], error);
					return;
				}
				GXPS_DEBUG (g_message ("transform (%f, %f, %f, %f) [%f, %f]",
					      matrix.xx, matrix.yx,
					      matrix.xy, matrix.yy,
					      matrix.x0, matrix.y0));
				cairo_transform (ctx->cr, &matrix);
			} else if (strcmp (names[i], "Name") == 0) {
				name = values[i];
			}
		}

		path_anchor = g_slice_new0 (GXPSPathAnchor);
		if (name) {
			path_anchor->data = data ? g_strdup (data) : NULL;
			path_anchor->name = g_strdup (name);
		}

		ctx->st = g_list_prepend (ctx->st, path_anchor);
	} else if (strcmp (element_name, "Glyphs") == 0) {
		gint i;

		GXPS_DEBUG (g_message ("save"));
		cairo_save (ctx->cr);

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "RenderTransform") == 0) {
				cairo_matrix_t matrix;

				if (!gxps_matrix_parse (values[i], &matrix)) {
					gxps_parse_error (context,
							  ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "Glyphs", "RenderTransform", values[i], error);
					return;
				}
				GXPS_DEBUG (g_message ("transform (%f, %f, %f, %f) [%f, %f]",
					      matrix.xx, matrix.yx,
					      matrix.xy, matrix.yy,
					      matrix.x0, matrix.y0));
				cairo_transform (ctx->cr, &matrix);
			} else if (strcmp (names[i], "Name") == 0) {
				/* TODO */
			}
		}
	} else if (strcmp (element_name, "Canvas.RenderTransform") == 0 ||
		   strcmp (element_name, "Path.RenderTransform") == 0 ||
		   strcmp (element_name, "Glyphs.RenderTransform") == 0 ) {
		ctx->do_transform = TRUE;
	} else if (strcmp (element_name, "MatrixTransform") == 0) {
		gint i;

		if (!ctx->do_transform) {
			return;
		}

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "Matrix") == 0) {
				cairo_matrix_t matrix;

				if (!gxps_matrix_parse (values[i], &matrix)) {
					gxps_parse_error (context,
							  ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "MatrixTransform", "Matrix",
							  values[i], error);
					return;
				}
				GXPS_DEBUG (g_message ("transform (%f, %f, %f, %f) [%f, %f]",
					      matrix.xx, matrix.yx,
					      matrix.xy, matrix.yy,
					      matrix.x0, matrix.y0));
				cairo_transform (ctx->cr, &matrix);
				return;
			}
		}
	}
}

static void
anchors_end_element (GMarkupParseContext  *context,
		     const gchar          *element_name,
		     gpointer              user_data,
		     GError              **error)
{
	GXPSAnchorsContext *ctx = (GXPSAnchorsContext *)user_data;

	if (strcmp (element_name, "Canvas") == 0) {
		GXPS_DEBUG (g_message ("restore"));
		cairo_restore (ctx->cr);
	} else if (strcmp (element_name, "Path") == 0) {
		GXPSPathAnchor *path_anchor;

		path_anchor = (GXPSPathAnchor *)ctx->st->data;
		ctx->st = g_list_delete_link (ctx->st, ctx->st);
		if (path_anchor->name) {
			gdouble x1, y1, x2, y2;
			cairo_rectangle_t *rect;

			if (path_anchor->data)
				gxps_path_parse (path_anchor->data, ctx->cr, error);

			cairo_path_extents (ctx->cr, &x1, &y1, &x2, &y2);
			cairo_user_to_device (ctx->cr, &x1, &y1);
			cairo_user_to_device (ctx->cr, &x2, &y2);

			rect = g_slice_new (cairo_rectangle_t);
			rect->x = x1;
			rect->y = y1;
			rect->width = x2 - x1;
			rect->height = y2 - y1;
			g_hash_table_insert (ctx->anchors, path_anchor->name, rect);
		}
		g_free (path_anchor->data);
		g_slice_free (GXPSPathAnchor, path_anchor);
		cairo_new_path (ctx->cr);
		GXPS_DEBUG (g_message ("restore"));
		cairo_restore (ctx->cr);
	} else if (strcmp (element_name, "Glyphs") == 0) {
		GXPS_DEBUG (g_message ("restore"));
		cairo_restore (ctx->cr);
	} else if (strcmp (element_name, "Canvas.RenderTransform") == 0 ||
		   strcmp (element_name, "Path.RenderTransform") == 0 ||
		   strcmp (element_name, "Glyphs.RenderTransform") == 0 ) {
		ctx->do_transform = FALSE;
	}
}

static const GMarkupParser anchors_parser = {
	anchors_start_element,
	anchors_end_element,
	NULL,
	NULL,
	NULL
};

static void
anchor_area_free (cairo_rectangle_t *area)
{
	g_slice_free (cairo_rectangle_t, area);
}

static gboolean
gxps_page_parse_anchors (GXPSPage *page,
			 cairo_t  *cr,
			 GError  **error)
{
	GInputStream        *stream;
	GXPSAnchorsContext   ctx;
	GMarkupParseContext *context;

	stream = gxps_archive_open (page->priv->zip,
				    page->priv->source);
	if (!stream) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_SOURCE_NOT_FOUND,
			     "Page source %s not found in archive",
			     page->priv->source);
		return FALSE;
	}

	ctx.cr = cr;
	ctx.page = page;
	ctx.st = NULL;
	ctx.anchors = g_hash_table_new_full (g_str_hash,
					     g_str_equal,
					     (GDestroyNotify)g_free,
					     (GDestroyNotify)anchor_area_free);

	context = g_markup_parse_context_new (&anchors_parser, 0, &ctx, NULL);
	gxps_parse_stream (context, stream, error);
	g_object_unref (stream);
	g_markup_parse_context_free (context);

	if (g_hash_table_size (ctx.anchors) > 0) {
		page->priv->has_anchors = TRUE;
		page->priv->anchors = ctx.anchors;
	} else {
		page->priv->has_anchors = FALSE;
		g_hash_table_destroy (ctx.anchors);
	}

	return TRUE;
}

static void
gxps_page_finalize (GObject *object)
{
	GXPSPage *page = GXPS_PAGE (object);

	if (page->priv->zip) {
		g_object_unref (page->priv->zip);
		page->priv->zip = NULL;
	}

	if (page->priv->source) {
		g_free (page->priv->source);
		page->priv->source = NULL;
	}

	if (page->priv->init_error) {
		g_error_free (page->priv->init_error);
		page->priv->init_error = NULL;
	}

	if (page->priv->lang) {
		g_free (page->priv->lang);
		page->priv->lang = NULL;
	}

	if (page->priv->name) {
		g_free (page->priv->name);
		page->priv->name = NULL;
	}

	if (page->priv->image_cache) {
		g_hash_table_destroy (page->priv->image_cache);
		page->priv->image_cache = NULL;
	}

	if (page->priv->anchors) {
		g_hash_table_destroy (page->priv->anchors);
		page->priv->anchors = NULL;
		page->priv->has_anchors = FALSE;
	}

	G_OBJECT_CLASS (gxps_page_parent_class)->finalize (object);
}

static void
gxps_page_init (GXPSPage *page)
{
	page->priv = G_TYPE_INSTANCE_GET_PRIVATE (page,
						  GXPS_TYPE_PAGE,
						  GXPSPagePrivate);
	page->priv->has_anchors = TRUE;
}

static void
gxps_page_set_property (GObject      *object,
			guint         prop_id,
			const GValue *value,
			GParamSpec   *pspec)
{
	GXPSPage *page = GXPS_PAGE (object);

	switch (prop_id) {
	case PROP_ARCHIVE:
		page->priv->zip = g_value_dup_object (value);
		break;
	case PROP_SOURCE:
		page->priv->source = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gxps_page_class_init (GXPSPageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = gxps_page_set_property;
	object_class->finalize = gxps_page_finalize;

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
							      "The Page Source File",
							      NULL,
							      G_PARAM_WRITABLE |
							      G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (GXPSPagePrivate));
}

static gboolean
gxps_page_initable_init (GInitable     *initable,
			 GCancellable  *cancellable,
			 GError       **error)
{
	GXPSPage *page = GXPS_PAGE (initable);

	if (page->priv->initialized) {
		if (page->priv->init_error) {
			g_propagate_error (error, g_error_copy (page->priv->init_error));

			return FALSE;
		}
		return TRUE;
	}

	page->priv->initialized = TRUE;

	if (!gxps_page_parse_fixed_page (page, &page->priv->init_error)) {
		g_propagate_error (error, g_error_copy (page->priv->init_error));
		return FALSE;
	}

	if (!page->priv->lang || page->priv->width == -1 || page->priv->height == -1) {
		if (!page->priv->lang) {
			g_set_error_literal (&page->priv->init_error,
					     GXPS_PAGE_ERROR,
					     GXPS_PAGE_ERROR_INVALID,
					     "Missing required attribute xml:lang");
		} else {
			g_set_error_literal (&page->priv->init_error,
					     GXPS_PAGE_ERROR,
					     GXPS_PAGE_ERROR_INVALID,
					     "Missing page size");
		}

		g_propagate_error (error, g_error_copy (page->priv->init_error));

		return FALSE;
	}

	return TRUE;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
	initable_iface->init = gxps_page_initable_init;
}

GXPSPage *
_gxps_page_new (GXPSArchive *zip,
		const gchar *source,
		GError     **error)
{
	return g_initable_new (GXPS_TYPE_PAGE,
			       NULL, error,
			       "archive", zip,
			       "source", source,
			       NULL);
}

/**
 * gxps_page_get_size:
 * @page: a #GXPSPage
 * @width: (out) (allow-none): return location for the page width
 * @height: (out) (allow-none): return location for the page height
 *
 * Gets the size of the page.
 */
void
gxps_page_get_size (GXPSPage *page,
		    gdouble  *width,
		    gdouble  *height)
{
	g_return_if_fail (GXPS_IS_PAGE (page));

	if (width)
		*width = page->priv->width;
	if (height)
		*height = page->priv->height;
}

/**
 * gxps_page_render:
 * @page: a #GXPSPage
 * @cr: a cairo context to render to
 * @error: #GError for error reporting, or %NULL to ignore
 *
 * Render the page to the given cairo context. In case of
 * error, %FALSE is returned and @error is filled with
 * information about error.
 *
 * Returns: %TRUE if page was successfully rendered,
 *     %FALSE otherwise.
 */
gboolean
gxps_page_render (GXPSPage *page,
		  cairo_t  *cr,
		  GError  **error)
{
	g_return_val_if_fail (GXPS_IS_PAGE (page), FALSE);
	g_return_val_if_fail (cr != NULL, FALSE);

	return gxps_page_parse_for_rendering (page, cr, error);
}

/**
 * gxps_page_get_links:
 * @page: a #GXPSPage
 * @error: #GError for error reporting, or %NULL to ignore
 *
 * Gets a list of #GXPSLink items that map from a location
 * in @page to a #GXPSLinkTarget. Items in the list should
 * be freed with gxps_link_free() and the list itself with
 * g_list_free() when done.
 *
 * Returns: (element-type GXPS.Link) (transfer full):  a #GList
 *     of #GXPSLink items.
 */
GList *
gxps_page_get_links (GXPSPage *page,
		     GError  **error)
{
	cairo_surface_t  *surface;
	cairo_t          *cr;
	GList            *links;
	cairo_rectangle_t extents;

        g_return_val_if_fail (GXPS_IS_PAGE (page), NULL);

	extents.x = extents.y = 0;
	extents.width = page->priv->width;
	extents.height = page->priv->height;

	surface = cairo_recording_surface_create (CAIRO_CONTENT_COLOR, &extents);
	cr = cairo_create (surface);
	cairo_surface_destroy (surface);

	links = gxps_page_parse_links (page, cr, error);
	cairo_destroy (cr);

	return links;
}

/**
 * gxps_page_get_anchor_destination:
 * @page: a #GXPSPage
 * @anchor: the name of an anchor in @page
 * @area: (out): return location for page area of @anchor
 * @error: #GError for error reporting, or %NULL to ignore
 *
 * Gets the rectangle of @page corresponding to the destination
 * of the given anchor. If @anchor is not found in @page, %FALSE
 * will be returned and @error will contain %GXPS_PAGE_ERROR_INVALID_ANCHOR
 *
 * Returns: %TRUE if the destination for the anchor was found in page
 *     and @area contains the rectangle, %FALSE otherwise.
 */
gboolean
gxps_page_get_anchor_destination (GXPSPage          *page,
				  const gchar       *anchor,
				  cairo_rectangle_t *area,
				  GError           **error)
{
	cairo_rectangle_t *anchor_area;

        g_return_val_if_fail (GXPS_IS_PAGE (page), FALSE);
        g_return_val_if_fail (anchor != NULL, FALSE);
        g_return_val_if_fail (area != NULL, FALSE);

	if (!page->priv->has_anchors)
		return FALSE;

	if (!page->priv->anchors) {
		cairo_surface_t  *surface;
		cairo_t          *cr;
		cairo_rectangle_t extents;
		gboolean          success;

		extents.x = extents.y = 0;
		extents.width = page->priv->width;
		extents.height = page->priv->height;

		surface = cairo_recording_surface_create (CAIRO_CONTENT_COLOR, &extents);
		cr = cairo_create (surface);
		cairo_surface_destroy (surface);

		success = gxps_page_parse_anchors (page, cr, error);
		cairo_destroy (cr);
		if (!success)
			return FALSE;
	}

	anchor_area = g_hash_table_lookup (page->priv->anchors, anchor);
	if (!anchor_area) {
		g_set_error (error,
			     GXPS_PAGE_ERROR,
			     GXPS_PAGE_ERROR_INVALID_ANCHOR,
			     "Invalid anchor '%s' for page", anchor);
		return FALSE;
	}

	*area = *anchor_area;

	return TRUE;
}
