/* GXPSPath
 *
 * Copyright (C) 2011  Carlos Garcia Campos <carlosgc@gnome.org>
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

#include "gxps-path.h"
#include "gxps-matrix.h"
#include "gxps-brush.h"
#include "gxps-parse-utils.h"
#include "gxps-debug.h"

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

GXPSPath *
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

void
gxps_path_free (GXPSPath *path)
{
        if (G_UNLIKELY (!path))
                return;

        g_free (path->data);
        g_free (path->clip_data);
        cairo_pattern_destroy (path->fill_pattern);
        cairo_pattern_destroy (path->stroke_pattern);
        cairo_pattern_destroy (path->opacity_mask);
        g_free (path->dash);

        g_slice_free (GXPSPath, path);
}

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

        return NULL;
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
                gxps_parse_skip_number (&token->iter, token->end);
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

gboolean
gxps_path_parse (const gchar *data,
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
                                gdouble xr, yr, x, y;
#ifdef GXPS_ENABLE_DEBUG
                                /* TODO: for now these variables are only used
                                 * in debug mode.
                                 */
                                gdouble rx, farc, fsweep;
#endif

                                if (!path_data_get_point (&token, &xr, &yr, error))
                                        return FALSE;

                                if (!path_data_iter_next (&token, error))
                                        return FALSE;
                                if (token.type != PD_TOKEN_NUMBER) {
                                        path_data_parse_error (&token, PD_TOKEN_NUMBER, error);
                                        return FALSE;
                                }
#ifdef GXPS_ENABLE_DEBUG
                                rx = token.number;
#endif

                                if (!path_data_iter_next (&token, error))
                                        return FALSE;
                                if (token.type != PD_TOKEN_NUMBER) {
                                        path_data_parse_error (&token, PD_TOKEN_NUMBER, error);
                                        return FALSE;
                                }
#ifdef GXPS_ENABLE_DEBUG
                                farc = token.number;
#endif

                                if (!path_data_iter_next (&token, error))
                                        return FALSE;
                                if (token.type != PD_TOKEN_NUMBER) {
                                        path_data_parse_error (&token, PD_TOKEN_NUMBER, error);
                                        return FALSE;
                                }
#ifdef GXPS_ENABLE_DEBUG
                                fsweep = token.number;
#endif

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
                                gboolean is_closed;

                                if (!gxps_value_get_boolean (values[i], &is_closed)) {
                                        gxps_parse_error (context,
                                                          path->ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "PathFigure", "IsClosed",
                                                          values[i], error);
                                        return;
                                }
                                path->is_closed = is_closed;
			} else if (strcmp (names[i], "IsFilled") == 0) {
                                gboolean is_filled;

                                if (!gxps_value_get_boolean (values[i], &is_filled)) {
                                        gxps_parse_error (context,
                                                          path->ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "PathFigure", "IsFilled",
                                                          values[i], error);
                                        return;
                                }
                                path->is_filled = is_filled;
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
                                if (!gxps_value_get_boolean (values[i], &is_stroked)) {
                                        gxps_parse_error (context,
                                                          path->ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "PolyLineSegment", "IsStroked",
                                                          points_str, error);
                                        return;
                                }
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
                                if (!gxps_value_get_boolean (values[i], &is_stroked)) {
                                        gxps_parse_error (context,
                                                          path->ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "PolyBezierSegment", "IsStroked",
                                                          points_str, error);
                                        return;
                                }
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
                                if (!gxps_value_get_boolean (values[i], &is_stroked)) {
                                        gxps_parse_error (context,
                                                          path->ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "PolyQuadraticBezierSegment", "IsStroked",
                                                          points_str, error);
                                        return;
                                }
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

		if (path->stroke_pattern) {
			cairo_set_line_width (path->ctx->cr, path->line_width);
			if (path->dash && path->dash_len > 0)
				cairo_set_dash (path->ctx->cr, path->dash, path->dash_len, path->dash_offset);
			cairo_set_line_join (path->ctx->cr, path->line_join);
			cairo_set_miter_limit (path->ctx->cr, path->miter_limit);
		}

		if (path->opacity_mask) {
			gdouble x1 = 0, y1 = 0, x2 = 0, y2 = 0;
			cairo_path_t *cairo_path;

			if (path->stroke_pattern)
				cairo_stroke_extents (path->ctx->cr, &x1, &y1, &x2, &y2);
			else if (path->fill_pattern)
				cairo_fill_extents (path->ctx->cr, &x1, &y1, &x2, &y2);

			cairo_path = cairo_copy_path (path->ctx->cr);
			cairo_new_path (path->ctx->cr);
			cairo_rectangle (path->ctx->cr, x1, y1, x2 - x1, y2 - y1);
			cairo_clip (path->ctx->cr);
			cairo_push_group (path->ctx->cr);
			cairo_append_path (path->ctx->cr, cairo_path);
			cairo_path_destroy (cairo_path);
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
			cairo_stroke (path->ctx->cr);
		}

		if (path->opacity_mask) {
			cairo_pop_group_to_source (path->ctx->cr);
			cairo_mask (path->ctx->cr, path->opacity_mask);
		}
	}
}

static GMarkupParser path_geometry_parser = {
	path_geometry_start_element,
	path_geometry_end_element,
	NULL,
	NULL
};

static cairo_fill_rule_t
gxps_fill_rule_parse (const gchar *rule)
{
        if (strcmp (rule, "EvenOdd") == 0)
                return CAIRO_FILL_RULE_EVEN_ODD;
        else if (strcmp (rule, "NonZero") == 0)
                return CAIRO_FILL_RULE_WINDING;
        return CAIRO_FILL_RULE_EVEN_ODD;
}

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
		gxps_brush_parser_push (context, brush);
	} else if (strcmp (element_name, "Path.Stroke") == 0) {
		GXPSBrush *brush;

		brush = gxps_brush_new (path->ctx);
		gxps_brush_parser_push (context, brush);
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
				if (!gxps_path_parse (path->clip_data, path->ctx->cr, error))
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
	} else if (strcmp (element_name, "Path.OpacityMask") == 0) {
		GXPSBrush *brush;

		brush = gxps_brush_new (path->ctx);
		gxps_brush_parser_push (context, brush);
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
	} else if (strcmp (element_name, "Path.OpacityMask") == 0) {
		GXPSBrush *brush;

		brush = g_markup_parse_context_pop (context);
		if (!path->opacity_mask)
			path->opacity_mask = cairo_pattern_reference (brush->pattern);
		gxps_brush_free (brush);
	} else {

	}
}

static void
path_error (GMarkupParseContext *context,
	    GError              *error,
	    gpointer             user_data)
{
	GXPSPath *path = (GXPSPath *)user_data;
	gxps_path_free (path);
}

static GMarkupParser path_parser = {
        path_start_element,
        path_end_element,
        NULL,
        NULL,
        path_error
};

void
gxps_path_parser_push (GMarkupParseContext *context,
                       GXPSPath            *path)
{
        g_markup_parse_context_push (context, &path_parser, path);
}
