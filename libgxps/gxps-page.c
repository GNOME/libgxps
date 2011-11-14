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
static cairo_surface_t *
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

typedef struct {
	GXPSRenderContext *ctx;

	gchar             *data;
	gchar             *clip_data;
	cairo_pattern_t   *fill_pattern;
	cairo_pattern_t   *stroke_pattern;
	cairo_fill_rule_t  fill_rule;
	gdouble            line_width;
	gdouble           *dash;
	guint              dash_len;
	gdouble            dash_offset;
	cairo_line_cap_t   line_cap;
	cairo_line_join_t  line_join;
	gdouble            miter_limit;
	gdouble            opacity;

	gboolean           is_stroked : 1;
	gboolean           is_filled  : 1;
	gboolean           is_closed  : 1;
} GXPSPath;

static GXPSPath *
gxps_path_new (GXPSRenderContext *ctx)
{
	GXPSPath *path;

	path = g_slice_new0 (GXPSPath);
	path->ctx = ctx;

	/* Default values */
	path->fill_rule = CAIRO_FILL_RULE_EVEN_ODD;
	path->line_width = 1.0;
	path->line_cap = CAIRO_LINE_CAP_BUTT;
	path->line_join = CAIRO_LINE_JOIN_MITER;
	path->miter_limit = 10.0;
	path->opacity = 1.0;
	path->is_filled = TRUE;
	path->is_stroked = TRUE;

	return path;
}

static void
gxps_path_free (GXPSPath *path)
{
	if (G_UNLIKELY (!path))
		return;

	g_free (path->data);
	g_free (path->clip_data);
	cairo_pattern_destroy (path->fill_pattern);
	cairo_pattern_destroy (path->stroke_pattern);
	g_free (path->dash);

	g_slice_free (GXPSPath, path);
}

typedef enum {
	PD_TOKEN_INVALID,
	PD_TOKEN_NUMBER,
	PD_TOKEN_COMMA,
	PD_TOKEN_COMMAND,
	PD_TOKEN_EOF
} PathDataTokenType;

typedef struct {
	gchar            *iter;
	gchar            *end;
	PathDataTokenType type;
	gdouble           number;
	gchar             command;
} PathDataToken;

static const gchar *
path_data_token_type_to_string (PathDataTokenType type)
{
	switch (type) {
	case PD_TOKEN_INVALID:
		return "Invalid";
	case PD_TOKEN_NUMBER:
		return "Number";
	case PD_TOKEN_COMMA:
		return "Comma";
	case PD_TOKEN_COMMAND:
		return "Command";
	case PD_TOKEN_EOF:
		return "Eof";
	default:
		g_assert_not_reached ();
	}
}

#ifdef GXPS_ENABLE_DEBUG
static void
print_token (PathDataToken *token)
{
	switch (token->type) {
	case PD_TOKEN_INVALID:
		g_debug ("Invalid token");
		break;
	case PD_TOKEN_NUMBER:
		g_debug ("Token number: %f", token->number);
		break;
	case PD_TOKEN_COMMA:
		g_debug ("Token comma");
		break;
	case PD_TOKEN_COMMAND:
		g_debug ("Token command %c", token->command);
		break;
	case PD_TOKEN_EOF:
		g_debug ("Token EOF");
		break;
	default:
		g_assert_not_reached ();
	}
}
#endif /* GXPS_ENABLE_DEBUG */

static inline gboolean
advance_char (PathDataToken *token)
{
	token->iter++;

	if (G_UNLIKELY (token->iter == token->end))
		return FALSE;

	return TRUE;
}

static inline gboolean
_isspace (char c)
{
	return c == ' ' || c == '\t';
}

static void
skip_spaces (PathDataToken *token)
{
	do {
		if (!_isspace (*token->iter))
			return;
	} while (advance_char (token));
}

static gboolean
path_data_iter_next (PathDataToken *token,
                     GError        **error)
{
	gchar c;

	skip_spaces (token);

	if (token->iter == token->end) {
		token->type = PD_TOKEN_EOF;
                GXPS_DEBUG (print_token (token));

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
                                     "Error parsing abreviated path: error converting token %s (%s) to double at %s",
                                     path_data_token_type_to_string (token->type),
                                     str, token->iter);
                        g_free (str);

                        return FALSE;
                }
		g_free (str);
		token->type = PD_TOKEN_NUMBER;
	} else if (c == ',') {
		token->type = PD_TOKEN_COMMA;
		token->iter++;
	} else if (g_ascii_isalpha (c)) {
		token->command = c;
		token->type = PD_TOKEN_COMMAND;
		token->iter++;
	} else {
		token->type = PD_TOKEN_INVALID;
		token->iter++;
	}

	GXPS_DEBUG (print_token (token));

        return TRUE;
}

static void
path_data_parse_error (PathDataToken    *token,
		       PathDataTokenType expected,
		       GError          **error)
{
	if (expected == PD_TOKEN_INVALID)
		g_set_error (error,
			     GXPS_PAGE_ERROR,
			     GXPS_PAGE_ERROR_RENDER,
			     "Error parsing abreviated path: unexpected token %s at %s",
			     path_data_token_type_to_string (token->type),
			     token->iter);
	else
		g_set_error (error,
			     GXPS_PAGE_ERROR,
			     GXPS_PAGE_ERROR_RENDER,
			     "Error parsing abreviated path: expected token %s, but %s found at %s",
			     path_data_token_type_to_string (token->type),
			     path_data_token_type_to_string (expected),
			     token->iter);
}

static gboolean
path_data_get_point (PathDataToken *token,
		     gdouble       *x,
		     gdouble       *y,
		     GError       **error)
{
	*x = token->number;

	if (!path_data_iter_next (token, error))
                return FALSE;
	if (token->type != PD_TOKEN_COMMA) {
		path_data_parse_error (token, PD_TOKEN_COMMA, error);
		return FALSE;
	}

	if (!path_data_iter_next (token, error))
                return FALSE;
	if (token->type != PD_TOKEN_NUMBER) {
		path_data_parse_error (token, PD_TOKEN_NUMBER, error);
		return FALSE;
	}
	*y = token->number;

	return TRUE;
}

static gboolean
path_data_parse (const gchar *data,
		 cairo_t     *cr,
		 GError     **error)
{
	PathDataToken token;
        gdouble       control_point_x;
        gdouble       control_point_y;

	token.iter = (gchar *)data;
	token.end = token.iter + strlen (data);

	if (!path_data_iter_next (&token, error))
                return FALSE;
	if (G_UNLIKELY (token.type != PD_TOKEN_COMMAND))
		return TRUE;

        control_point_x = control_point_y = 0;

	do {
		gchar    command = token.command;
		gboolean is_rel = FALSE;

		if (!path_data_iter_next (&token, error))
                        return FALSE;

		switch (command) {
			/* Move */
		case 'm':
			is_rel = TRUE;
		case 'M':
			while (token.type == PD_TOKEN_NUMBER) {
				gdouble x, y;

				if (!path_data_get_point (&token, &x, &y, error))
					return FALSE;

				GXPS_DEBUG (g_message ("%s (%f, %f)", is_rel ? "rel_move_to" : "move_to", x, y));

				if (is_rel)
					cairo_rel_move_to (cr, x, y);
				else
					cairo_move_to (cr, x, y);

				if (!path_data_iter_next (&token, error))
                                        return FALSE;
			}
                        control_point_x = control_point_y = 0;
			break;
			/* Line */
		case 'l':
			is_rel = TRUE;
		case 'L':
			while (token.type == PD_TOKEN_NUMBER) {
				gdouble x, y;

				if (!path_data_get_point (&token, &x, &y, error))
					return FALSE;

				GXPS_DEBUG (g_message ("%s (%f, %f)", is_rel ? "rel_line_to" : "line_to", x, y));

				if (is_rel)
					cairo_rel_line_to (cr, x, y);
				else
					cairo_line_to (cr, x, y);

				if (!path_data_iter_next (&token, error))
                                        return FALSE;
			}
                        control_point_x = control_point_y = 0;
			break;
			/* Horizontal Line */
		case 'h':
			is_rel = TRUE;
		case 'H':
			while (token.type == PD_TOKEN_NUMBER) {
				gdouble x, y;
				gdouble offset;

				offset = token.number;

				GXPS_DEBUG (g_message ("%s (%f)", is_rel ? "rel_hline_to" : "hline_to", offset));

				cairo_get_current_point (cr, &x, &y);
				x = is_rel ? x + offset : offset;
				cairo_line_to (cr, x, y);

				if (!path_data_iter_next (&token, error))
                                        return FALSE;
			}
                        control_point_x = control_point_y = 0;
			break;
			/* Vertical Line */
		case 'v':
			is_rel = TRUE;
		case 'V':
			while (token.type == PD_TOKEN_NUMBER) {
				gdouble x, y;
				gdouble offset;

				offset = token.number;

				GXPS_DEBUG (g_message ("%s (%f)", is_rel ? "rel_vline_to" : "vline_to", offset));

				cairo_get_current_point (cr, &x, &y);
				y = is_rel ? y + offset : offset;
				cairo_line_to (cr, x, y);

				if (!path_data_iter_next (&token, error))
                                        return FALSE;
			}
                        control_point_x = control_point_y = 0;
			break;
			/* Cubic Bézier curve */
		case 'c':
			is_rel = TRUE;
		case 'C':
			while (token.type == PD_TOKEN_NUMBER) {
				gdouble x1, y1, x2, y2, x3, y3;

				if (!path_data_get_point (&token, &x1, &y1, error))
					return FALSE;

				if (!path_data_iter_next (&token, error))
                                        return FALSE;
				if (!path_data_get_point (&token, &x2, &y2, error))
					return FALSE;

				if (!path_data_iter_next (&token, error))
                                        return FALSE;
				if (!path_data_get_point (&token, &x3, &y3, error))
					return FALSE;

				GXPS_DEBUG (g_message ("%s (%f, %f, %f, %f, %f, %f)", is_rel ? "rel_curve_to" : "curve_to",
					      x1, y1, x2, y2, x3, y3));

				if (is_rel)
					cairo_rel_curve_to (cr, x1, y1, x2, y2, x3, y3);
				else
					cairo_curve_to (cr, x1, y1, x2, y2, x3, y3);

                                control_point_x = x3 - x2;
                                control_point_y = y3 - y2;

				if (!path_data_iter_next (&token, error))
                                        return FALSE;
			}
			break;
			/* Quadratic Bézier curve */
		case 'q':
			is_rel = TRUE;
		case 'Q':
			while (token.type == PD_TOKEN_NUMBER) {
				gdouble x1, y1, x2, y2;
                                gdouble x, y;

				if (!path_data_get_point (&token, &x1, &y1, error))
					return FALSE;

				if (!path_data_iter_next (&token, error))
                                        return FALSE;
				if (!path_data_get_point (&token, &x2, &y2, error))
					return FALSE;

				GXPS_DEBUG (g_message ("%s (%f, %f, %f, %f)", is_rel ? "rel_quad_curve_to" : "quad_curve_to",
					      x1, y1, x2, y2));

                                cairo_get_current_point (cr, &x, &y);
                                x1 += is_rel ? x : 0;
                                y1 += is_rel ? y : 0;
                                x2 += is_rel ? x : 0;
                                y2 += is_rel ? y : 0;
                                cairo_curve_to (cr,
                                                2.0 / 3.0 * x1 + 1.0 / 3.0 * x,
                                                2.0 / 3.0 * y1 + 1.0 / 3.0 * y,
                                                2.0 / 3.0 * x1 + 1.0 / 3.0 * x2,
                                                2.0 / 3.0 * y1 + 1.0 / 3.0 * y2,
                                                x2, y2);

				if (!path_data_iter_next (&token, error))
                                        return FALSE;
			}
                        control_point_x = control_point_y = 0;
			break;
			/* Smooth Cubic Bézier curve */
		case 's':
			is_rel = TRUE;
		case 'S':
			while (token.type == PD_TOKEN_NUMBER) {
				gdouble x2, y2, x3, y3;

				if (!path_data_get_point (&token, &x2, &y2, error))
					return FALSE;

				if (!path_data_iter_next (&token, error))
                                        return FALSE;
				if (!path_data_get_point (&token, &x3, &y3, error))
					return FALSE;

				GXPS_DEBUG (g_message ("%s (%f, %f, %f, %f, %f, %f)", is_rel ? "rel_smooth_curve_to" : "smooth_curve_to",
                                                       control_point_x, control_point_y, x2, y2, x3, y3));

                                if (is_rel) {
                                        cairo_rel_curve_to (cr, control_point_x, control_point_y, x2, y2, x3, y3);
                                } else {
                                        gdouble x, y;

                                        cairo_get_current_point (cr, &x, &y);
                                        cairo_curve_to (cr, x + control_point_x, y + control_point_y, x2, y2, x3, y3);
                                }

                                control_point_x = x3 - x2;
                                control_point_y = y3 - y2;

				if (!path_data_iter_next (&token, error))
                                        return FALSE;
			}
			break;
			/* Elliptical Arc */
		case 'a':
			is_rel = TRUE;
		case 'A':
			while (token.type == PD_TOKEN_NUMBER) {
				gdouble xr, yr, rx, farc, fsweep, x, y;

				if (!path_data_get_point (&token, &xr, &yr, error))
					return FALSE;

				if (!path_data_iter_next (&token, error))
                                        return FALSE;
				if (token.type != PD_TOKEN_NUMBER) {
					path_data_parse_error (&token, PD_TOKEN_NUMBER, error);
					return FALSE;
				}
				rx = token.number;

				if (!path_data_iter_next (&token, error))
                                        return FALSE;
				if (token.type != PD_TOKEN_NUMBER) {
					path_data_parse_error (&token, PD_TOKEN_NUMBER, error);
					return FALSE;
				}
				farc = token.number;

				if (!path_data_iter_next (&token, error))
                                        return FALSE;
				if (token.type != PD_TOKEN_NUMBER) {
					path_data_parse_error (&token, PD_TOKEN_NUMBER, error);
					return FALSE;
				}
				fsweep = token.number;

				if (!path_data_iter_next (&token, error))
                                        return FALSE;
				if (!path_data_get_point (&token, &x, &y, error))
					return FALSE;

				GXPS_DEBUG (g_message ("%s (%f, %f, %f, %f, %f, %f, %f)", is_rel ? "rel_arc" : "arc",
					      xr, yr, rx, farc, fsweep, x, y));
				GXPS_DEBUG (g_debug ("Unsupported command in path: %c", command));

				if (!path_data_iter_next (&token, error))
                                        return FALSE;
			}
                        control_point_x = control_point_y = 0;
			break;
			/* Close */
		case 'z':
			is_rel = TRUE;
		case 'Z':
			cairo_close_path (cr);
			GXPS_DEBUG (g_message ("close_path"));
                        control_point_x = control_point_y = 0;
			break;
			/* Fill Rule */
		case 'F': {
			gint fill_rule;

			fill_rule = (gint)token.number;
			cairo_set_fill_rule (cr,
					     (fill_rule == 0) ?
					     CAIRO_FILL_RULE_EVEN_ODD :
					     CAIRO_FILL_RULE_WINDING);
			GXPS_DEBUG (g_message ("set_fill_rule (%s)", (fill_rule == 0) ? "EVEN_ODD" : "WINDING"));

			if (!path_data_iter_next (&token, error))
                                return FALSE;
		}
                        control_point_x = control_point_y = 0;
			break;
		default:
			g_assert_not_reached ();
		}
	} while (token.type == PD_TOKEN_COMMAND);

	return TRUE;
}

static gboolean
hex (const gchar *spec,
     gint         len,
     guint       *c)
{
	const gchar *end;

	*c = 0;
	for (end = spec + len; spec != end; spec++) {
		if (!g_ascii_isxdigit (*spec))
			return FALSE;

		*c = (*c << 4) | g_ascii_xdigit_value (*spec);
	}

	return TRUE;
}

static gboolean
gxps_color_s_rgb_parse (const gchar *color_str,
                        GXPSColor   *color)
{
        gsize len = strlen (color_str);
        guint a = 255;
        guint r, g, b;

        switch (len) {
	case 6:
		if (!hex (color_str, 2, &r) ||
		    !hex (color_str + 2, 2, &g) ||
		    !hex (color_str + 4, 2, &b))
			return FALSE;
		break;
	case 8:
		if (!hex (color_str, 2, &a) ||
		    !hex (color_str + 2, 2, &r) ||
		    !hex (color_str + 4, 2, &g) ||
		    !hex (color_str + 6, 2, &b))
			return FALSE;
		break;
	default:
		return FALSE;
	}

        color->alpha = a / 255.;
        color->red = r / 255.;
        color->green = g / 255.;
        color->blue = b / 255.;

        return TRUE;
}

static gboolean
gxps_color_sc_rgb_parse (const gchar *color_str,
                         GXPSColor   *color)
{
        gchar **tokens;
        gsize   len;
        gdouble c[4];
        guint   i, start;

        tokens = g_strsplit (color_str, ",", 4);
        len = g_strv_length (tokens);

        switch (len) {
        case 4:
                if (!gxps_value_get_double (tokens[0], &c[0])) {
                        g_strfreev (tokens);

                        return FALSE;
                }
                start = 1;

                break;
        case 3:
                c[0] = 1.0;
                start = 0;
                break;
        default:
                g_strfreev (tokens);
                return FALSE;
        }

        for (i = start; i < len; i++) {
                if (!gxps_value_get_double (tokens[i], &c[i])) {
                        g_strfreev (tokens);

                        return FALSE;
                }
        }

        g_strfreev (tokens);

        color->alpha = CLAMP (c[0], 0., 1.);
        color->red = CLAMP (c[1], 0., 1.);
        color->green = CLAMP (c[2], 0., 1.);
        color->blue = CLAMP (c[3], 0., 1.);

        return TRUE;
}

static gboolean
gxps_color_icc_parse (const gchar *color_str,
                      GXPSArchive *zip,
                      GXPSColor   *color)
{
        const gchar *p;
        gchar       *icc_profile_uri;
        gchar      **tokens;
        gsize        len;
        gdouble      alpha;
        gdouble      values[GXPS_COLOR_MAX_CHANNELS];
        guint        i, j;
        gboolean     retval;

        p = strstr (color_str, " ");
        if (!p)
                return FALSE;

        icc_profile_uri = g_strndup (color_str, strlen (color_str) - strlen (p));

        tokens = g_strsplit (++p, ",", -1);
        len = g_strv_length (tokens);
        if (len < 2) {
                g_strfreev (tokens);
                g_free (icc_profile_uri);

                return FALSE;
        }

        if (!gxps_value_get_double (tokens[0], &alpha)) {
                g_strfreev (tokens);
                g_free (icc_profile_uri);

                return FALSE;
        }

        for (i = 0, j = 1; i < GXPS_COLOR_MAX_CHANNELS && j < len; i++, j++) {
                if (!gxps_value_get_double (tokens[j], &values[i])) {
                        g_strfreev (tokens);
                        g_free (icc_profile_uri);

                        return FALSE;
                }
        }

        g_strfreev (tokens);

        color->alpha = CLAMP (alpha, 0., 1.);
        retval = gxps_color_new_for_icc (zip, icc_profile_uri, values, i, color);
        g_free (icc_profile_uri);

        return retval;
}

static gboolean
gxps_color_parse (const gchar *color_str,
                  GXPSArchive *zip,
                  GXPSColor   *color)
{
        const gchar *p;

        p = strstr (color_str, "#");
        if (!p) {
                p = strstr (color_str, "ContextColor");
                if (p == color_str) {
                        p += strlen ("ContextColor");
                        return gxps_color_icc_parse (++p, zip, color);
                }
                GXPS_DEBUG (g_debug ("Unsupported color %s", color_str));

                return FALSE;
        }

        if (p == color_str)
                return gxps_color_s_rgb_parse (++p, color);

        if (strncmp (color_str, "sc", 2) == 0 && p == color_str + 2)
                return gxps_color_sc_rgb_parse (++p, color);

        GXPS_DEBUG (g_debug ("Unsupported color %s", color_str));

        return FALSE;
}

static cairo_pattern_t *
gxps_create_solid_color_pattern_with_alpha (GXPSArchive *zip,
                                            const gchar *color_str,
                                            gdouble      alpha)
{
	cairo_pattern_t *pattern;
        GXPSColor        color;

	if (!gxps_color_parse (color_str, zip, &color))
		return NULL;

	pattern = cairo_pattern_create_rgba (color.red,
                                             color.green,
                                             color.blue,
                                             color.alpha * alpha);
	if (cairo_pattern_status (pattern)) {
		cairo_pattern_destroy (pattern);

		return NULL;
	}

	return pattern;
}

static cairo_pattern_t *
gxps_create_solid_color_pattern (GXPSArchive *zip,
                                 const gchar *color_str)
{
	return gxps_create_solid_color_pattern_with_alpha (zip, color_str, 1.0);
}

static gboolean
gxps_boolean_parse (const gchar *value)
{
	return (strcmp (value, "true") == 0);
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

static cairo_fill_rule_t
gxps_fill_rule_parse (const gchar *rule)
{
	if (strcmp (rule, "EvenOdd") == 0)
		return CAIRO_FILL_RULE_EVEN_ODD;
	else if (strcmp (rule, "NonZero") == 0)
		return CAIRO_FILL_RULE_WINDING;
	return CAIRO_FILL_RULE_EVEN_ODD;
}

static gboolean
gxps_box_parse (const gchar       *box,
		cairo_rectangle_t *rect)
{
	gchar **tokens;
        gdouble b[4];
        guint   i;

	tokens = g_strsplit (box, ",", 4);
	if (g_strv_length (tokens) != 4) {
		g_strfreev (tokens);

		return FALSE;
	}

        for (i = 0; i < 4; i++) {
                if (!gxps_value_get_double (tokens[i], &b[i])) {
                        g_strfreev (tokens);

                        return FALSE;
                }
        }

	rect->x = b[0];
	rect->y = b[1];
	rect->width = b[2];
	rect->height = b[3];

	g_strfreev (tokens);

	return TRUE;
}

static gboolean
gxps_point_parse (const gchar *point,
		  gdouble     *x,
		  gdouble     *y)
{
	gchar *p;

	p = g_strrstr (point, ",");
	if (!p)
		return FALSE;

	if (x) {
		gchar *str;

		str = g_strndup (point, p - point);
                if (!gxps_value_get_double (str, x)) {
                        g_free (str);

                        return FALSE;
                }
		g_free (str);
	}

	if (y) {
                if (!gxps_value_get_double (++p, y))
                        return FALSE;
	}

	return TRUE;
}

static gboolean
gxps_points_parse (const gchar *points,
		   gdouble    **coords,
		   guint       *n_points)
{
	gchar  **items;
	guint    i, j = 0;
	gboolean retval = TRUE;

	*n_points = 0;
	items = g_strsplit (points, " ", -1);
	if (!items)
		return FALSE;

	for (i = 0; items[i] != NULL; i++) {
		if (*items[i] != '\0') /* don't count empty string */
			(*n_points)++;
	}

	if (*n_points == 0)
		return FALSE;

	*coords = g_malloc (*n_points * 2 * sizeof (gdouble));

	for (i = 0; items[i] != NULL; i++) {
		gdouble x, y;

		if (*items[i] == '\0')
			continue;

		if (!gxps_point_parse (items[i], &x, &y)) {
			g_free (*coords);
			retval = FALSE;
			break;
		}

		coords[0][j++] = x;
		coords[0][j++] = y;
	}

	g_strfreev (items);

	return retval;
}

static cairo_extend_t
gxps_spread_method_parse (const gchar *spread)
{
	if (strcmp (spread, "Pad") == 0)
		return CAIRO_EXTEND_PAD;
	else if (strcmp (spread, "Reflect") == 0)
		return CAIRO_EXTEND_REFLECT;
	else if (strcmp (spread, "Repeat") == 0)
		return CAIRO_EXTEND_REPEAT;
	return CAIRO_EXTEND_NONE;
}

static cairo_extend_t
gxps_tile_mode_parse (const gchar *tile)
{
	if (strcmp (tile, "Tile") == 0)
		return CAIRO_EXTEND_REPEAT;
	else if (strcmp (tile, "FlipX") == 0)
		GXPS_DEBUG (g_debug ("Unsupported tile mode FlipX"));
	else if (strcmp (tile, "FlipY") == 0)
		GXPS_DEBUG (g_debug ("Unsupported tile mode FlipY"));
	else if (strcmp (tile, "FlipXY") == 0)
		GXPS_DEBUG (g_debug ("Unsupported tile mode FlipXY"));

	return CAIRO_EXTEND_NONE;
}

typedef struct {
	GXPSRenderContext *ctx;
	cairo_pattern_t   *pattern;
	gdouble            opacity;
} GXPSBrush;

static GXPSBrush *
gxps_brush_new (GXPSRenderContext *ctx)
{
	GXPSBrush *brush;

	brush = g_slice_new0 (GXPSBrush);
	brush->ctx = ctx;
	brush->opacity = 1.0;

	return brush;
}

static void
gxps_brush_free (GXPSBrush *brush)
{
	if (G_UNLIKELY (!brush))
		return;

	cairo_pattern_destroy (brush->pattern);
	g_slice_free (GXPSBrush, brush);
}

typedef struct {
	GXPSBrush        *brush;

	gchar            *image_uri;
	cairo_matrix_t    matrix;
	cairo_rectangle_t viewport;
	cairo_rectangle_t viewbox;
	cairo_extend_t    extend;
} GXPSBrushImage;

static GXPSBrushImage *
gxps_brush_image_new (GXPSBrush         *brush,
		      gchar             *image_uri,
		      cairo_rectangle_t *viewport,
		      cairo_rectangle_t *viewbox)
{
	GXPSBrushImage *image;

	image = g_slice_new0 (GXPSBrushImage);
	image->brush = brush;

	cairo_matrix_init_identity (&image->matrix);

	/* Required values */
	image->image_uri = image_uri;
	image->viewport = *viewport;
	image->viewbox = *viewbox;

	return image;
}

static void
gxps_brush_image_free (GXPSBrushImage *image)
{
	if (G_UNLIKELY (!image))
		return;

	g_free (image->image_uri);

	g_slice_free (GXPSBrushImage, image);
}

static void
brush_image_start_element (GMarkupParseContext  *context,
			   const gchar          *element_name,
			   const gchar         **names,
			   const gchar         **values,
			   gpointer              user_data,
			   GError              **error)
{
	GXPSBrushImage *image = (GXPSBrushImage *)user_data;

	if (strcmp (element_name, "ImageBrush.Transform") == 0) {
		GXPSMatrix *matrix;

		matrix = gxps_matrix_new (image->brush->ctx);
                gxps_matrix_parser_push (context, matrix);
	} else {
		gxps_parse_error (context,
				  image->brush->ctx->page->priv->source,
				  G_MARKUP_ERROR_UNKNOWN_ELEMENT,
				  element_name, NULL, NULL, error);
	}
}

static void
brush_image_end_element (GMarkupParseContext  *context,
			 const gchar          *element_name,
			 gpointer              user_data,
			 GError              **error)
{
	GXPSBrushImage *image = (GXPSBrushImage *)user_data;

	if (strcmp (element_name, "ImageBrush.Transform") == 0) {
		GXPSMatrix *matrix;

		matrix = g_markup_parse_context_pop (context);
		image->matrix = matrix->matrix;
		gxps_matrix_free (matrix);
	} else {
		gxps_parse_error (context,
				  image->brush->ctx->page->priv->source,
				  G_MARKUP_ERROR_UNKNOWN_ELEMENT,
				  element_name, NULL, NULL, error);
	}
}

static GMarkupParser brush_image_parser = {
	brush_image_start_element,
	brush_image_end_element,
	NULL,
	NULL
};

struct _GXPSBrushVisual {
	GXPSBrush        *brush;

	cairo_matrix_t    matrix;
	cairo_rectangle_t viewport;
	cairo_rectangle_t viewbox;
	cairo_extend_t    extend;
};

static GXPSBrushVisual *
gxps_brush_visual_new (GXPSBrush         *brush,
		      cairo_rectangle_t *viewport,
		      cairo_rectangle_t *viewbox)
{
	GXPSBrushVisual *visual;

	visual = g_slice_new0 (GXPSBrushVisual);
	visual->brush = brush;

	/* Default */
	visual->extend = CAIRO_EXTEND_NONE;
	cairo_matrix_init_identity (&visual->matrix);

	/* Required values */
	visual->viewport = *viewport;
	visual->viewbox = *viewbox;

	return visual;
}

static void
gxps_brush_visual_free (GXPSBrushVisual *visual)
{
	if (G_UNLIKELY (!visual))
		return;

	g_slice_free (GXPSBrushVisual, visual);
}

static void
brush_gradient_start_element (GMarkupParseContext  *context,
			      const gchar          *element_name,
			      const gchar         **names,
			      const gchar         **values,
			      gpointer              user_data,
			      GError              **error)
{
	GXPSBrush *brush = (GXPSBrush *)user_data;

	if (strcmp (element_name, "LinearGradientBrush.GradientStops") == 0) {
	} else if (strcmp (element_name, "RadialGradientBrush.GradientStops") == 0) {
	} else if (strcmp (element_name, "GradientStop") == 0) {
		gint      i;
                GXPSColor color;
                gboolean  has_color = FALSE;
		gdouble   offset = -1;

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "Color") == 0) {
                                has_color = TRUE;
				if (!gxps_color_parse (values[i], brush->ctx->page->priv->zip, &color)) {
					gxps_parse_error (context,
							  brush->ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "GradientStop", "Color",
							  values[i], error);
					return;
				}
			} else if (strcmp (names[i], "Offset") == 0) {
                                if (!gxps_value_get_double (values[i], &offset)) {
                                        gxps_parse_error (context,
                                                          brush->ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "GradientStop", "Offset",
                                                          values[i], error);
                                        return;
                                }
			}
		}

		if (!has_color || offset == -1) {
			gxps_parse_error (context,
					  brush->ctx->page->priv->source,
					  G_MARKUP_ERROR_MISSING_ATTRIBUTE,
					  element_name,
					  !has_color ? "Color" : "Offset",
					  NULL, error);
			return;
		}

                cairo_pattern_add_color_stop_rgba (brush->pattern, offset,
                                                   color.red,
                                                   color.green,
                                                   color.blue,
                                                   color.alpha * brush->opacity);
	}
}

static GMarkupParser brush_gradient_parser = {
	brush_gradient_start_element,
	NULL,
	NULL,
	NULL
};

static void
brush_start_element (GMarkupParseContext  *context,
		     const gchar          *element_name,
		     const gchar         **names,
		     const gchar         **values,
		     gpointer              user_data,
		     GError              **error)
{
	GXPSBrush *brush = (GXPSBrush *)user_data;

	if (strcmp (element_name, "SolidColorBrush") == 0) {
		const gchar *color_str = NULL;
		gint i;

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "Color") == 0) {
				color_str = values[i];
			} else if (strcmp (names[i], "Opacity") == 0) {
				if (!gxps_value_get_double (values[i], &brush->opacity)) {
					gxps_parse_error (context,
							  brush->ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "SolidColorBrush", "Opacity",
							  values[i], error);
					return;
				}
			} else if (strcmp (names[i], "X:Key") == 0) {
				/* TODO */
			}
		}

                if (!color_str) {
                        gxps_parse_error (context,
                                          brush->ctx->page->priv->source,
                                          G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                                          "SolidColorBrush", "Color",
                                          NULL, error);
                        return;
                }

                brush->pattern = gxps_create_solid_color_pattern_with_alpha (brush->ctx->page->priv->zip,
                                                                             color_str, brush->opacity);
                GXPS_DEBUG (g_message ("set_fill_pattern (solid)"));
		if (!brush->pattern) {
			gxps_parse_error (context,
					  brush->ctx->page->priv->source,
					  G_MARKUP_ERROR_INVALID_CONTENT,
                                          "SolidColorBrush", "Color",
					  color_str, error);
			return;
		}
	} else if (strcmp (element_name, "ImageBrush") == 0) {
		GXPSBrushImage *image;
		gchar *image_source = NULL;
		cairo_rectangle_t viewport, viewbox;
		cairo_matrix_t matrix;
		cairo_extend_t extend = CAIRO_EXTEND_NONE;
		gint i;

		cairo_matrix_init_identity (&matrix);

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "ImageSource") == 0) {
				image_source = gxps_resolve_relative_path (brush->ctx->page->priv->source,
									   values[i]);
			} else if (strcmp (names[i], "Transform") == 0) {
				if (!gxps_matrix_parse (values[i], &matrix)) {
					gxps_parse_error (context,
							  brush->ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "ImageBrush", "Transform",
							  values[i], error);
					return;
				}
			} else if (strcmp (names[i], "Viewport") == 0) {
				if (!gxps_box_parse (values[i], &viewport)) {
					gxps_parse_error (context,
							  brush->ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "ImageBrush", "Viewport",
							  values[i], error);
					return;
				}
			} else if (strcmp (names[i], "ViewportUnits") == 0) {
			} else if (strcmp (names[i], "Viewbox") == 0) {
				if (!gxps_box_parse (values[i], &viewbox)) {
					gxps_parse_error (context,
							  brush->ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "ImageBrush", "Viewbox",
							  values[i], error);
					return;
				}
			} else if (strcmp (names[i], "ViewboxUnits") == 0) {
			} else if (strcmp (names[i], "TileMode") == 0) {
				extend = gxps_tile_mode_parse (values[i]);
			} else if (strcmp (names[i], "Opacity") == 0) {
                                if (!gxps_value_get_double (values[i], &brush->opacity)) {
                                        gxps_parse_error (context,
                                                          brush->ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "ImageBrush", "Opacity",
                                                          values[i], error);
                                        return;
                                }
			} else if (strcmp (names[i], "X:Key") == 0) {
				/* TODO */
			}

		}

		if (!image_source) {
			gxps_parse_error (context,
					  brush->ctx->page->priv->source,
					  G_MARKUP_ERROR_MISSING_ATTRIBUTE,
					  element_name, "ImageSource",
					  NULL, error);
			return;
		}

		/* GXPSBrushImage takes ownership of image_source */
		image = gxps_brush_image_new (brush, image_source, &viewport, &viewbox);
		image->extend = extend;
		image->matrix = matrix;
		g_markup_parse_context_push (context, &brush_image_parser, image);
	} else if (strcmp (element_name, "LinearGradientBrush") == 0) {
		gint           i;
		gdouble        x0, y0, x1, y1;
		cairo_extend_t extend = CAIRO_EXTEND_PAD;
		cairo_matrix_t matrix;

		cairo_matrix_init_identity (&matrix);

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "MappingMode") == 0) {
			} else if (strcmp (names[i], "StartPoint") == 0) {
				if (!gxps_point_parse (values[i], &x0, &y0)) {
					gxps_parse_error (context,
							  brush->ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "LinearGradientBrush", "StartPoint",
							  values[i], error);
					return;
				}
			} else if (strcmp (names[i], "EndPoint") == 0) {
				if (!gxps_point_parse (values[i], &x1, &y1)) {
					gxps_parse_error (context,
							  brush->ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "LinearGradientBrush", "EndPoint",
							  values[i], error);
					return;
				}
			} else if (strcmp (names[i], "SpreadMethod") == 0) {
				extend = gxps_spread_method_parse (values[i]);
			} else if (strcmp (names[i], "Opacity") == 0) {
                                if (!gxps_value_get_double (values[i], &brush->opacity)) {
                                        gxps_parse_error (context,
                                                          brush->ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "LinearGradientBrush", "Opacity",
                                                          values[i], error);
                                        return;
                                }
			} else if (strcmp (names[i], "Transform") == 0) {
				if (!gxps_matrix_parse (values[i], &matrix)) {
					gxps_parse_error (context,
							  brush->ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "LinearGradientBrush", "Transform",
							  values[i], error);
					return;
				}
			}
		}

		/* TODO: check required values */

		GXPS_DEBUG (g_message ("set_fill_pattern (linear)"));
		brush->pattern = cairo_pattern_create_linear (x0, y0, x1, y1);
		cairo_pattern_set_matrix (brush->pattern, &matrix);
		cairo_pattern_set_extend (brush->pattern, extend);
		g_markup_parse_context_push (context, &brush_gradient_parser, brush);
	} else if (strcmp (element_name, "RadialGradientBrush") == 0) {
		gint           i;
		gdouble        cx0, cy0, r0, cx1, cy1, r1 = 0;
		cairo_extend_t extend = CAIRO_EXTEND_PAD;
		cairo_matrix_t matrix;

		cairo_matrix_init_identity (&matrix);

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "MappingMode") == 0) {
			} else if (strcmp (names[i], "GradientOrigin") == 0) {
				if (!gxps_point_parse (values[i], &cx0, &cy0)) {
					gxps_parse_error (context,
							  brush->ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "RadialGradientBrush", "GradientOrigin",
							  values[i], error);
					return;
				}
			} else if (strcmp (names[i], "Center") == 0) {
				if (!gxps_point_parse (values[i], &cx1, &cy1)) {
					gxps_parse_error (context,
							  brush->ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "RadialGradientBrush", "Center",
							  values[i], error);
					return;
				}
			} else if (strcmp (names[i], "RadiusX") == 0) {
                                if (!gxps_value_get_double (values[i], &r0)) {
                                        gxps_parse_error (context,
                                                          brush->ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "RadialGradientBrush", "RadiusX",
                                                          values[i], error);
                                        return;
                                }
			} else if (strcmp (names[i], "RadiusY") == 0) {
                                if (!gxps_value_get_double (values[i], &r1)) {
                                        gxps_parse_error (context,
                                                          brush->ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "RadialGradientBrush", "RadiusY",
                                                          values[i], error);
                                        return;
                                }
			} else if (strcmp (names[i], "SpreadMethod") == 0) {
				extend = gxps_spread_method_parse (values[i]);
			} else if (strcmp (names[i], "Opacity") == 0) {
                                if (!gxps_value_get_double (values[i], &brush->opacity)) {
                                        gxps_parse_error (context,
                                                          brush->ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "RadialGradientBrush", "Opacity",
                                                          values[i], error);
                                        return;
                                }
			} else if (strcmp (names[i], "Transform") == 0) {
				if (!gxps_matrix_parse (values[i], &matrix)) {
					gxps_parse_error (context,
							  brush->ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "RadialGradientBrush", "Transform",
							  values[i], error);
					return;
				}
			}
		}

		/* TODO: Check required values */

		GXPS_DEBUG (g_message ("set_fill_pattern (radial)"));
		brush->pattern = cairo_pattern_create_radial (cx0, cy0, 0, cx1, cy1, r1);
		cairo_pattern_set_matrix (brush->pattern, &matrix);
		cairo_pattern_set_extend (brush->pattern, extend);
		g_markup_parse_context_push (context, &brush_gradient_parser, brush);
	} else if (strcmp (element_name, "VisualBrush") == 0) {
		GXPSBrushVisual *visual;
		GXPSRenderContext *sub_ctx;
		cairo_rectangle_t viewport, viewbox;
		cairo_matrix_t matrix;
		cairo_extend_t extend = CAIRO_EXTEND_NONE;
		gint i;

		cairo_matrix_init_identity (&matrix);

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "TileMode") == 0) {
				extend = gxps_tile_mode_parse (values[i]);
			} else if (strcmp (names[i], "Transform") == 0) {
				if (!gxps_matrix_parse (values[i], &matrix)) {
					gxps_parse_error (context,
							  brush->ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "VisualBrush", "Transform",
							  values[i], error);
					return;
				}
			} else if (strcmp (names[i], "Viewport") == 0) {
				if (!gxps_box_parse (values[i], &viewport)) {
					gxps_parse_error (context,
							  brush->ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "VisualBrush", "Viewport",
							  values[i], error);
					return;
				}
			} else if (strcmp (names[i], "ViewportUnits") == 0) {
			} else if (strcmp (names[i], "Viewbox") == 0) {
				if (!gxps_box_parse (values[i], &viewbox)) {
					gxps_parse_error (context,
							  brush->ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "VisualBrush", "Viewbox",
							  values[i], error);
					return;
				}
			} else if (strcmp (names[i], "ViewboxUnits") == 0) {
			} else if (strcmp (names[i], "Opacity") == 0) {
			} else {
			}
		}

		/* TODO: check required values */

		/* Clip to viewport so that push group
		 * will create a surface with the viport size
		 */
		cairo_save (brush->ctx->cr);
		cairo_rectangle (brush->ctx->cr, viewbox.x, viewbox.y, viewbox.width, viewbox.height);
		cairo_clip (brush->ctx->cr);
		cairo_push_group (brush->ctx->cr);
		visual = gxps_brush_visual_new (brush, &viewport, &viewbox);
		visual->extend = extend;
		sub_ctx = g_slice_new0 (GXPSRenderContext);
		sub_ctx->page = brush->ctx->page;
		sub_ctx->cr = brush->ctx->cr;
		sub_ctx->visual = visual;
		g_markup_parse_context_push (context, &render_parser, sub_ctx);
	} else {
		GXPS_DEBUG (g_debug ("Unsupported Brush: %s", element_name));

	}
}

static void
brush_end_element (GMarkupParseContext  *context,
		   const gchar          *element_name,
		   gpointer              user_data,
		   GError              **error)
{
	GXPSBrush *brush = (GXPSBrush *)user_data;

	if (strcmp (element_name, "SolidColorBrush") == 0) {
	} else if (strcmp (element_name, "LinearGradientBrush") == 0) {
		g_markup_parse_context_pop (context);
	} else if (strcmp (element_name, "RadialGradientBrush") == 0) {
		g_markup_parse_context_pop (context);
	} else if (strcmp (element_name, "ImageBrush") == 0) {
		GXPSBrushImage  *image;
		cairo_surface_t *surface;
		GError          *err = NULL;

		image = g_markup_parse_context_pop (context);

		GXPS_DEBUG (g_message ("set_fill_pattern (image)"));
		surface = gxps_page_get_image (brush->ctx->page, image->image_uri, &err);
		if (surface) {
			cairo_matrix_t matrix, port_matrix;
			gdouble        x_scale, y_scale;

			image->brush->pattern = cairo_pattern_create_for_surface (surface);
			cairo_pattern_set_extend (image->brush->pattern, image->extend);
			cairo_surface_destroy (surface);

			cairo_matrix_init (&port_matrix,
					   image->viewport.width,
					   0, 0,
					   image->viewport.height,
					   image->viewport.x,
					   image->viewport.y);
			cairo_matrix_multiply (&port_matrix, &port_matrix, &image->matrix);

			x_scale = image->viewbox.width / port_matrix.xx;
			y_scale = image->viewbox.height / port_matrix.yy;
			cairo_matrix_init (&matrix, x_scale, 0, 0, y_scale,
					   -port_matrix.x0 * x_scale,
					   -port_matrix.y0 * y_scale);
			cairo_pattern_set_matrix (image->brush->pattern, &matrix);

			if (brush->opacity != 1.0) {
				cairo_push_group (brush->ctx->cr);
				cairo_set_source (brush->ctx->cr, image->brush->pattern);
				cairo_pattern_destroy (image->brush->pattern);
				cairo_paint_with_alpha (brush->ctx->cr, brush->opacity);
				image->brush->pattern = cairo_pop_group (brush->ctx->cr);
			}

			if (cairo_pattern_status (image->brush->pattern)) {
				GXPS_DEBUG (g_debug ("%s", cairo_status_to_string (cairo_pattern_status (image->brush->pattern))));
				cairo_pattern_destroy (image->brush->pattern);
				image->brush->pattern = NULL;
			}
		} else if (err) {
			GXPS_DEBUG (g_debug ("%s", err->message));
			g_error_free (err);
		}
		gxps_brush_image_free (image);
	} else if (strcmp (element_name, "VisualBrush") == 0) {
		GXPSRenderContext *sub_ctx;
		GXPSBrushVisual   *visual;
		cairo_matrix_t     matrix, port_matrix;
		gdouble            x_scale, y_scale;

		sub_ctx = g_markup_parse_context_pop (context);
		visual = sub_ctx->visual;
		g_slice_free (GXPSRenderContext, sub_ctx);

		GXPS_DEBUG (g_message ("set_fill_pattern (visual)"));
		visual->brush->pattern = cairo_pop_group (brush->ctx->cr);
		/* Undo the clip */
		cairo_restore (brush->ctx->cr);
		cairo_pattern_set_extend (visual->brush->pattern, visual->extend);
		cairo_matrix_init (&port_matrix,
				   visual->viewport.width,
				   0, 0,
				   visual->viewport.height,
				   visual->viewport.x,
				   visual->viewport.y);
		cairo_matrix_multiply (&port_matrix, &port_matrix, &visual->matrix);

		x_scale = visual->viewbox.width / port_matrix.xx;
		y_scale = visual->viewbox.height / port_matrix.yy;
		cairo_matrix_init (&matrix, x_scale, 0, 0, y_scale,
				   -port_matrix.x0 * x_scale,
				   -port_matrix.y0 * y_scale);

		cairo_pattern_set_matrix (visual->brush->pattern, &matrix);
		if (cairo_pattern_status (visual->brush->pattern)) {
			GXPS_DEBUG (g_debug ("%s", cairo_status_to_string (cairo_pattern_status (visual->brush->pattern))));
			cairo_pattern_destroy (visual->brush->pattern);
			visual->brush->pattern = NULL;
		}

		gxps_brush_visual_free (visual);
	} else {
		GXPS_DEBUG (g_debug ("Unsupported Brush: %s", element_name));

	}
}

static GMarkupParser brush_parser = {
	brush_start_element,
	brush_end_element,
	NULL,
	NULL
};

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
		g_markup_parse_context_push (context, &brush_parser, brush);
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

static void
path_geometry_start_element (GMarkupParseContext  *context,
			     const gchar          *element_name,
			     const gchar         **names,
			     const gchar         **values,
			     gpointer              user_data,
			     GError              **error)
{
	GXPSPath *path = (GXPSPath *)user_data;

	if (strcmp (element_name, "PathGeometry.Transform") == 0) {
		GXPSMatrix *matrix;

		matrix = gxps_matrix_new (path->ctx);
		gxps_matrix_parser_push (context, matrix);
	} else if (strcmp (element_name, "PathFigure") == 0) {
		gint     i;
                gboolean has_start_point = FALSE;

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "StartPoint") == 0) {
				gdouble x, y;

				if (!gxps_point_parse (values[i], &x, &y)) {
					gxps_parse_error (context,
							  path->ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "PathFigure", "StartPoint",
							  values[i], error);
					return;
				}

				GXPS_DEBUG (g_message ("move_to (%f, %f)", x, y));
				cairo_move_to (path->ctx->cr, x, y);
                                has_start_point = TRUE;
			} else if (strcmp (names[i], "IsClosed") == 0) {
				path->is_closed = gxps_boolean_parse (values[i]);
			} else if (strcmp (names[i], "IsFilled") == 0) {
				path->is_filled = gxps_boolean_parse (values[i]);

			}
		}

                if (!has_start_point) {
                        gxps_parse_error (context,
                                          path->ctx->page->priv->source,
                                          G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                                          "PathFigure", "StartPoint",
                                          NULL, error);
                        return;
                }
	} else if (strcmp (element_name, "PolyLineSegment") == 0) {
		gint         i, j;
		const gchar *points_str = NULL;
                gdouble     *points = NULL;
                guint        n_points;
		gboolean     is_stroked = TRUE;

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "Points") == 0) {
				points_str = values[i];
			} else if (strcmp (names[i], "IsStroked") == 0) {
				is_stroked = gxps_boolean_parse (values[i]);
			}
		}

		if (!is_stroked)
			return;

                if (!points_str) {
                        gxps_parse_error (context,
                                          path->ctx->page->priv->source,
                                          G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                                          "PolyLineSegment", "Points",
                                          NULL, error);
                        return;
                }

                if (!gxps_points_parse (points_str, &points, &n_points)) {
                        gxps_parse_error (context,
                                          path->ctx->page->priv->source,
                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                          "PolyLineSegment", "Points",
                                          points_str, error);
                        return;
                }

                for (j = 0; j < n_points * 2; j += 2) {
                        GXPS_DEBUG (g_message ("line_to (%f, %f)", points[j], points[j + 1]));
                        cairo_line_to (path->ctx->cr, points[j], points[j + 1]);
                }

                g_free (points);
	} else if (strcmp (element_name, "PolyBezierSegment") == 0) {
		gint         i, j;
		const gchar *points_str = NULL;
                gdouble     *points = NULL;
                guint        n_points;
		gboolean     is_stroked = TRUE;

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "Points") == 0) {
				points_str = values[i];

			} else if (strcmp (names[i], "IsStroked") == 0) {
				is_stroked = gxps_boolean_parse (values[i]);
			}
		}

		if (!is_stroked)
			return;

                if (!points_str) {
                        gxps_parse_error (context,
                                          path->ctx->page->priv->source,
                                          G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                                          "PolyBezierSegment", "Points",
                                          NULL, error);
                        return;
                }

                if (!gxps_points_parse (points_str, &points, &n_points)) {
                        gxps_parse_error (context,
                                          path->ctx->page->priv->source,
                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                          "PolyBezierSegment", "Points",
                                          points_str, error);
                        return;
                }

                for (j = 0; j < n_points * 2; j += 6) {
                        GXPS_DEBUG (g_message ("curve_to (%f, %f, %f, %f, %f, %f)",
                                               points[j], points[j + 1],
                                               points[j + 2], points[j + 3],
                                               points[j + 4], points[j + 5]));
                        cairo_curve_to (path->ctx->cr,
                                        points[j], points[j + 1],
                                        points[j + 2], points[j + 3],
                                        points[j + 4], points[j + 5]);
                }

                g_free (points);
        } else if (strcmp (element_name, "PolyQuadraticBezierSegment") == 0) {
		gint         i, j;
		const gchar *points_str = NULL;
                gdouble     *points = NULL;
                guint        n_points;
		gboolean     is_stroked = TRUE;

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "Points") == 0) {
				points_str = values[i];

			} else if (strcmp (names[i], "IsStroked") == 0) {
				is_stroked = gxps_boolean_parse (values[i]);
			}
		}

		if (!is_stroked)
			return;

                if (!points_str) {
                        gxps_parse_error (context,
                                          path->ctx->page->priv->source,
                                          G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                                          "PolyQuadraticBezierSegment", "Points",
                                          NULL, error);
                        return;
                }

                if (!gxps_points_parse (points_str, &points, &n_points)) {
                        gxps_parse_error (context,
                                          path->ctx->page->priv->source,
                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                          "PolyQuadraticBezierSegment", "Points",
                                          points_str, error);
                        return;
                }

                for (j = 0; j < n_points * 2; j += 4) {
                        gdouble x1, y1, x2, y2;
                        gdouble x, y;

                        x1 = points[j];
                        y1 = points[j + 1];
                        x2 = points[j + 2];
                        y2 = points[j + 3];

                        GXPS_DEBUG (g_message ("quad_curve_to (%f, %f, %f, %f)", x1, y1, x2, y2));
                        cairo_get_current_point (path->ctx->cr, &x, &y);
                        cairo_curve_to (path->ctx->cr,
                                        2.0 / 3.0 * x1 + 1.0 / 3.0 * x,
                                        2.0 / 3.0 * y1 + 1.0 / 3.0 * y,
                                        2.0 / 3.0 * x1 + 1.0 / 3.0 * x2,
                                        2.0 / 3.0 * y1 + 1.0 / 3.0 * y2,
                                        x2, y2);
                }

                g_free (points);
        } else if (strcmp (element_name, "ArcSegment") == 0) {
                GXPS_DEBUG (g_debug ("Unsupported PathGeometry: ArcSegment"));
	}
}

static void
path_geometry_end_element (GMarkupParseContext  *context,
			   const gchar          *element_name,
			   gpointer              user_data,
			   GError              **error)
{
	GXPSPath *path = (GXPSPath *)user_data;

	if (strcmp (element_name, "PathGeometry.Transform") == 0) {
		GXPSMatrix *matrix;

		matrix = g_markup_parse_context_pop (context);
		GXPS_DEBUG (g_message ("transform (%f, %f, %f, %f) [%f, %f]",
			      matrix->matrix.xx, matrix->matrix.yx,
			      matrix->matrix.xy, matrix->matrix.yy,
			      matrix->matrix.x0, matrix->matrix.y0));
		cairo_transform (path->ctx->cr, &matrix->matrix);

		gxps_matrix_free (matrix);
	} else if (strcmp (element_name, "PathFigure") == 0) {
		if (path->is_closed) {
			GXPS_DEBUG (g_message ("close_path"));
			cairo_close_path (path->ctx->cr);
		}

		if (path->is_filled && path->fill_pattern) {
			GXPS_DEBUG (g_message ("fill"));
			cairo_set_source (path->ctx->cr, path->fill_pattern);
			if (path->is_stroked && path->stroke_pattern)
				cairo_fill_preserve (path->ctx->cr);
			else
				cairo_fill (path->ctx->cr);
		}

		if (path->stroke_pattern) {
			GXPS_DEBUG (g_message ("stroke"));
			cairo_set_source (path->ctx->cr, path->stroke_pattern);
			cairo_set_line_width (path->ctx->cr, path->line_width);
			if (path->dash && path->dash_len > 0)
				cairo_set_dash (path->ctx->cr, path->dash, path->dash_len, path->dash_offset);
			cairo_set_line_join (path->ctx->cr, path->line_join);
			cairo_set_miter_limit (path->ctx->cr, path->miter_limit);
			cairo_stroke (path->ctx->cr);
		}
	}
}

static GMarkupParser path_geometry_parser = {
	path_geometry_start_element,
	path_geometry_end_element,
	NULL,
	NULL
};

static void
path_start_element (GMarkupParseContext  *context,
		    const gchar          *element_name,
		    const gchar         **names,
		    const gchar         **values,
		    gpointer              user_data,
		    GError              **error)
{
	GXPSPath *path = (GXPSPath *)user_data;

	if (strcmp (element_name, "Path.Fill") == 0) {
		GXPSBrush *brush;

		brush = gxps_brush_new (path->ctx);
		g_markup_parse_context_push (context, &brush_parser, brush);
	} else if (strcmp (element_name, "Path.Stroke") == 0) {
		GXPSBrush *brush;

		brush = gxps_brush_new (path->ctx);
		g_markup_parse_context_push (context, &brush_parser, brush);
	} else if (strcmp (element_name, "Path.Data") == 0) {
	} else if (strcmp (element_name, "PathGeometry") == 0) {
		gint i;

		for (i = 0; names[i] != NULL; i++) {
			if (strcmp (names[i], "Figures") == 0) {
				path->data = g_strdup (values[i]);
			} else if (strcmp (names[i], "FillRule") == 0) {
				path->fill_rule = gxps_fill_rule_parse (values[i]);
				GXPS_DEBUG (g_message ("set_fill_rule (%s)", values[i]));
			} else if (strcmp (names[i], "Transform") == 0) {
				cairo_matrix_t matrix;

				if (!gxps_matrix_parse (values[i], &matrix)) {
					gxps_parse_error (context,
							  path->ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "PathGeometry", "Transform",
							  values[i], error);
					return;
				}
				GXPS_DEBUG (g_message ("transform (%f, %f, %f, %f) [%f, %f]",
					      matrix.xx, matrix.yx,
					      matrix.xy, matrix.yy,
					      matrix.x0, matrix.y0));
				cairo_transform (path->ctx->cr, &matrix);
			}
		}

		if (!path->data) {
			cairo_set_fill_rule (path->ctx->cr, path->fill_rule);
			if (path->clip_data) {
				if (!path_data_parse (path->clip_data, path->ctx->cr, error))
					return;
				GXPS_DEBUG (g_message ("clip"));
				cairo_clip (path->ctx->cr);
			}
			g_markup_parse_context_push (context, &path_geometry_parser, path);
		}
	} else if (strcmp (element_name, "Path.RenderTransform") == 0) {
		GXPSMatrix *matrix;

		matrix = gxps_matrix_new (path->ctx);
		gxps_matrix_parser_push (context, matrix);
	} else {
		GXPS_DEBUG (g_debug ("Unsupported path child %s", element_name));
	}
}

static void
path_end_element (GMarkupParseContext  *context,
		  const gchar          *element_name,
		  gpointer              user_data,
		  GError              **error)
{
	GXPSPath *path = (GXPSPath *)user_data;

	if (strcmp (element_name, "Path.Fill") == 0) {
		GXPSBrush *brush;

		brush = g_markup_parse_context_pop (context);
		path->fill_pattern = cairo_pattern_reference (brush->pattern);
		gxps_brush_free (brush);
	} else if (strcmp (element_name, "Path.Stroke") == 0) {
		GXPSBrush *brush;

		brush = g_markup_parse_context_pop (context);
		path->stroke_pattern = cairo_pattern_reference (brush->pattern);
		gxps_brush_free (brush);
	} else if (strcmp (element_name, "Path.Data") == 0) {
	} else if (strcmp (element_name, "PathGeometry") == 0) {
		if (!path->data)
			g_markup_parse_context_pop (context);
	} else if (strcmp (element_name, "Path.RenderTransform") == 0) {
		GXPSMatrix *matrix;

		matrix = g_markup_parse_context_pop (context);
		GXPS_DEBUG (g_message ("transform (%f, %f, %f, %f) [%f, %f]",
			      matrix->matrix.xx, matrix->matrix.yx,
			      matrix->matrix.xy, matrix->matrix.yy,
			      matrix->matrix.x0, matrix->matrix.y0));
		cairo_transform (path->ctx->cr, &matrix->matrix);

		gxps_matrix_free (matrix);
	} else {

	}
}

static GMarkupParser path_parser = {
	path_start_element,
	path_end_element,
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
		g_markup_parse_context_push (context, &brush_parser, brush);
	} else if (strcmp (element_name, "Glyphs.OpacityMask") == 0) {
		GXPSBrush *brush;

		brush = gxps_brush_new (glyphs->ctx);
		g_markup_parse_context_push (context, &brush_parser, brush);
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
				path->fill_pattern = gxps_create_solid_color_pattern (ctx->page->priv->zip,
                                                                                      values[i]);
				if (!path->fill_pattern) {
					gxps_parse_error (context,
							  ctx->page->priv->source,
							  G_MARKUP_ERROR_INVALID_CONTENT,
							  "Path", "Fill", values[i], error);
					return;
				}
			} else if (strcmp (names[i], "Stroke") == 0) {
				GXPS_DEBUG (g_message ("set_stroke_pattern (solid)"));
				path->stroke_pattern = gxps_create_solid_color_pattern (ctx->page->priv->zip,
                                                                                        values[i]);
				if (!path->stroke_pattern) {
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
		g_markup_parse_context_push (context, &path_parser, path);
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
                                is_sideways = gxps_boolean_parse (values[i]);
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
			glyphs->fill_pattern = gxps_create_solid_color_pattern (ctx->page->priv->zip,
                                                                                fill_color);
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
				if (!path_data_parse (values[i], ctx->cr, error)) {
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
			if (!path_data_parse (path->clip_data, ctx->cr, error)) {
				if (path->opacity != 1.0)
					cairo_pattern_destroy (cairo_pop_group (ctx->cr));
				gxps_path_free (path);
				return;
			}
			GXPS_DEBUG (g_message ("clip"));
			cairo_clip (ctx->cr);
		}

		if (!path_data_parse (path->data, ctx->cr, error)) {
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
			if (!path_data_parse (glyphs->clip_data, ctx->cr, error)) {
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
				if (!path_data_parse (values[i], ctx->cr, error))
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
				path_data_parse (path_link->data, ctx->cr, error);

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
				path_data_parse (path_anchor->data, ctx->cr, error);

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
