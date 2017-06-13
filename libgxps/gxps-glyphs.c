/* GXPSGlyphs
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

#include "gxps-glyphs.h"
#include "gxps-brush.h"
#include "gxps-matrix.h"
#include "gxps-parse-utils.h"
#include "gxps-debug.h"

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

GXPSGlyphs *
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

void
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

        return NULL;
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
                gxps_parse_skip_number (&token->iter, token->end);
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
        gulong index = 0;

        if (utf8 == NULL || *utf8 == '\0')
                return index;

        status = cairo_scaled_font_text_to_glyphs (scaled_font,
                                                   0, 0,
                                                   utf8,
                                                   g_utf8_next_char (utf8) - utf8,
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
                        /* fall through */
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

gboolean
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

static void
glyphs_error (GMarkupParseContext *context,
              GError              *error,
              gpointer             user_data)
{
	GXPSGlyphs *glyphs = (GXPSGlyphs *)user_data;
	gxps_glyphs_free (glyphs);
}

static GMarkupParser glyphs_parser = {
        glyphs_start_element,
        glyphs_end_element,
        NULL,
        NULL,
        glyphs_error
};

void
gxps_glyphs_parser_push (GMarkupParseContext *context,
                         GXPSGlyphs          *glyphs)
{
        g_markup_parse_context_push (context, &glyphs_parser, glyphs);
}
