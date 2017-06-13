/* GXPSMatrix
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

#include "gxps-matrix.h"
#include "gxps-parse-utils.h"

GXPSMatrix *
gxps_matrix_new (GXPSRenderContext *ctx)
{
        GXPSMatrix *matrix;

        matrix = g_slice_new0 (GXPSMatrix);
        matrix->ctx = ctx;
        cairo_matrix_init_identity (&matrix->matrix);

        return matrix;
}

void
gxps_matrix_free (GXPSMatrix *matrix)
{
        if (G_UNLIKELY (!matrix))
                return;

        g_slice_free (GXPSMatrix, matrix);
}

gboolean
gxps_matrix_parse (const gchar    *data,
                   cairo_matrix_t *matrix)
{
        gchar **items;
        gdouble mm[6];
        guint   i;

        items = g_strsplit (data, ",", 6);
        if (g_strv_length (items) != 6) {
                g_strfreev (items);

                return FALSE;
        }

        for (i = 0; i < 6; i++) {
                if (!gxps_value_get_double (items[i], &mm[i])) {
                        g_strfreev (items);
                        return FALSE;
                }
        }

        g_strfreev (items);

        cairo_matrix_init (matrix, mm[0], mm[1], mm[2], mm[3], mm[4], mm[5]);

        return TRUE;
}

static void
matrix_start_element (GMarkupParseContext  *context,
                      const gchar          *element_name,
                      const gchar         **names,
                      const gchar         **values,
                      gpointer              user_data,
                      GError              **error)
{
        GXPSMatrix *matrix = (GXPSMatrix *)user_data;

        if (strcmp (element_name, "MatrixTransform") == 0) {
                gint i;

                for (i = 0; names[i] != NULL; i++) {
                        if (strcmp (names[i], "Matrix") == 0) {
                                if (!gxps_matrix_parse (values[i], &matrix->matrix)) {
                                        gxps_parse_error (context,
                                                          matrix->ctx->page->priv->source,
                                                          G_MARKUP_ERROR_INVALID_CONTENT,
                                                          "MatrixTransform", "Matrix",
                                                          values[i], error);
                                }
                        } else {
                                gxps_parse_error (context,
                                                  matrix->ctx->page->priv->source,
                                                  G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                                                  "MatrixTransform", names[i],
                                                  NULL, error);
                        }
                }
        } else {
                gxps_parse_error (context,
                                  matrix->ctx->page->priv->source,
                                  G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                                  element_name, NULL, NULL, error);
        }
}

static void
matrix_error (GMarkupParseContext *context,
              GError              *error,
              gpointer             user_data)
{
	GXPSMatrix *matrix = (GXPSMatrix *)user_data;
	gxps_matrix_free (matrix);
}

static GMarkupParser matrix_parser = {
        matrix_start_element,
        NULL,
        NULL,
        NULL,
        matrix_error
};

void
gxps_matrix_parser_push (GMarkupParseContext *context,
                         GXPSMatrix          *matrix)
{
        g_markup_parse_context_push (context, &matrix_parser, matrix);
}


