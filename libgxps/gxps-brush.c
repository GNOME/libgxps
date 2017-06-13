/* GXPSBrush
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

#include <math.h>
#include <string.h>

#include "gxps-brush.h"
#include "gxps-matrix.h"
#include "gxps-color.h"
#include "gxps-parse-utils.h"
#include "gxps-debug.h"

typedef struct {
        GXPSBrush        *brush;

        gchar            *image_uri;
        cairo_matrix_t    matrix;
        cairo_rectangle_t viewport;
        cairo_rectangle_t viewbox;
        cairo_extend_t    extend;
} GXPSBrushImage;

struct _GXPSBrushVisual {
        GXPSBrush        *brush;

        cairo_matrix_t    matrix;
        cairo_rectangle_t viewport;
        cairo_rectangle_t viewbox;
        cairo_extend_t    extend;
};

GXPSBrush *
gxps_brush_new (GXPSRenderContext *ctx)
{
        GXPSBrush *brush;

        brush = g_slice_new0 (GXPSBrush);
        brush->ctx = ctx;
        brush->opacity = 1.0;

        return brush;
}

static gdouble
gxps_transform_hypot (const cairo_matrix_t *matrix, double dx, double dy)
{
	cairo_matrix_transform_distance (matrix, &dx, &dy);
	return hypot (dx, dy);
}

void
gxps_brush_free (GXPSBrush *brush)
{
        if (G_UNLIKELY (!brush))
                return;

        cairo_pattern_destroy (brush->pattern);
        g_slice_free (GXPSBrush, brush);
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
gxps_color_parse (const gchar *data,
                  GXPSArchive *zip,
                  GXPSColor   *color)
{
        const gchar *p;

        p = strstr (data, "#");
        if (!p) {
                p = strstr (data, "ContextColor");
                if (p == data) {
                        p += strlen ("ContextColor");
                        return gxps_color_icc_parse (++p, zip, color);
                }
                GXPS_DEBUG (g_debug ("Unsupported color %s", data));

                return FALSE;
        }

        if (p == data)
                return gxps_color_s_rgb_parse (++p, color);

        if (strncmp (data, "sc", 2) == 0 && p == data + 2)
                return gxps_color_sc_rgb_parse (++p, color);

        GXPS_DEBUG (g_debug ("Unsupported color %s", data));

        return FALSE;
}

gboolean
gxps_brush_solid_color_parse (const gchar      *data,
                              GXPSArchive      *zip,
                              gdouble           alpha,
                              cairo_pattern_t **pattern)
{
        GXPSColor        color;
        cairo_pattern_t *retval;

        if (!gxps_color_parse (data, zip, &color))
                return FALSE;

        retval = cairo_pattern_create_rgba (color.red,
                                            color.green,
                                            color.blue,
                                            color.alpha * alpha);
        if (cairo_pattern_status (retval)) {
                cairo_pattern_destroy (retval);

                return FALSE;
        }

        if (pattern)
                *pattern = retval;

        return TRUE;
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

static void
brush_image_error (GMarkupParseContext *context,
		   GError              *error,
		   gpointer             user_data)
{
	GXPSBrushImage *image = (GXPSBrushImage *)user_data;
	gxps_brush_image_free (image);
}

static GMarkupParser brush_image_parser = {
        brush_image_start_element,
        brush_image_end_element,
        NULL,
        NULL,
        brush_image_error
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
                        } else {
                                gxps_parse_error (context,
                                                  brush->ctx->page->priv->source,
                                                  G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                                                  "GradientStop", names[i],
                                                  NULL, error);
                                return;
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
                        } else {
                                gxps_parse_error (context,
                                                  brush->ctx->page->priv->source,
                                                  G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                                                  "SolidColorBrush", names[i],
                                                  NULL, error);
                                return;
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

                GXPS_DEBUG (g_message ("set_fill_pattern (solid)"));
                if (!gxps_brush_solid_color_parse (color_str, brush->ctx->page->priv->zip,
                                                   brush->opacity, &brush->pattern)) {
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
                cairo_rectangle_t viewport = { 0, }, viewbox = { 0, };
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
                        } else  {
                                gxps_parse_error (context,
                                                  brush->ctx->page->priv->source,
                                                  G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                                                  "ImageBrush", names[i],
                                                  NULL, error);
                                return;
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

                x0 = y0 = x1 = y1 = -1;
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
                        } else if (strcmp (names[i], "ColorInterpolationMode") == 0) {
                                GXPS_DEBUG (g_debug ("Unsupported %s attribute: ColorInterpolationMode", element_name));
                        } else {
                                gxps_parse_error (context,
                                                  brush->ctx->page->priv->source,
                                                  G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                                                  element_name, names[i],
                                                  NULL, error);
                                return;
                        }
                }

                if (x0 == -1 || y0 == -1 || x1 == -1 || y1 == -1) {
                        gxps_parse_error (context,
                                          brush->ctx->page->priv->source,
                                          G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                                          element_name,
                                          (x0 == -1 || y0 == -1) ? "StartPoint" : "EndPoint",
                                          NULL, error);
                        return;
                }

                GXPS_DEBUG (g_message ("set_fill_pattern (linear)"));
                brush->pattern = cairo_pattern_create_linear (x0, y0, x1, y1);
                cairo_pattern_set_matrix (brush->pattern, &matrix);
                cairo_pattern_set_extend (brush->pattern, extend);
                g_markup_parse_context_push (context, &brush_gradient_parser, brush);
        } else if (strcmp (element_name, "RadialGradientBrush") == 0) {
                gint           i;
                gdouble        cx0, cy0, r0, cx1, cy1, r1;
                cairo_extend_t extend = CAIRO_EXTEND_PAD;
                cairo_matrix_t matrix;

                cx0 = cy0 = r0 = cx1 = cy1 = r1 = -1;

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
                        } else if (strcmp (names[i], "ColorInterpolationMode") == 0) {
                                GXPS_DEBUG (g_debug ("Unsupported %s attribute: ColorInterpolationMode", element_name));
                        } else {
                                gxps_parse_error (context,
                                                  brush->ctx->page->priv->source,
                                                  G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                                                  element_name, names[i],
                                                  NULL, error);
                                return;
                        }
                }

                if (cx0 == -1 || cy0 == -1 || cx1 == -1 || cy1 == -1) {
                        gxps_parse_error (context,
                                          brush->ctx->page->priv->source,
                                          G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                                          element_name,
                                          (cx0 == -1 || cy0 == -1) ? "GradientOrigin" : "Center",
                                          NULL, error);
                        return;
                }
                if (r0 == -1 || r1 == -1) {
                        gxps_parse_error (context,
                                          brush->ctx->page->priv->source,
                                          G_MARKUP_ERROR_MISSING_ATTRIBUTE,
                                          element_name,
                                          (r0 == -1) ? "RadiusX" : "RadiusY",
                                          NULL, error);
                        return;
                }

                GXPS_DEBUG (g_message ("set_fill_pattern (radial)"));
                brush->pattern = cairo_pattern_create_radial (cx0, cy0, 0, cx1, cy1, r1);
                cairo_pattern_set_matrix (brush->pattern, &matrix);
                cairo_pattern_set_extend (brush->pattern, extend);
                g_markup_parse_context_push (context, &brush_gradient_parser, brush);
        } else if (strcmp (element_name, "VisualBrush") == 0) {
                GXPSBrushVisual *visual;
                GXPSRenderContext *sub_ctx;
                cairo_rectangle_t viewport = { 0, }, viewbox = { 0, };
                cairo_matrix_t matrix;
                cairo_extend_t extend = CAIRO_EXTEND_NONE;
                double width, height;
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
                                GXPS_DEBUG (g_debug ("Unsupported %s attribute: Opacity", element_name));
                        } else if (strcmp (names[i], "Visual") == 0) {
                                GXPS_DEBUG (g_debug ("Unsupported %s attribute: Visual", element_name));
                        } else {
                                gxps_parse_error (context,
                                                  brush->ctx->page->priv->source,
                                                  G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
                                                  element_name, names[i],
                                                  NULL, error);
                                return;
                        }
                }

                /* TODO: check required values */

                width = gxps_transform_hypot (&matrix, viewport.width, 0);
                height = gxps_transform_hypot (&matrix, 0, viewport.height);

                cairo_save (brush->ctx->cr);
                cairo_rectangle (brush->ctx->cr, 0, 0, width, height);
                cairo_clip (brush->ctx->cr);
                cairo_push_group (brush->ctx->cr);
                cairo_translate (brush->ctx->cr, -viewbox.x, -viewbox.y);
                cairo_scale (brush->ctx->cr, width / viewbox.width, height / viewbox.height);
                visual = gxps_brush_visual_new (brush, &viewport, &viewbox);
                visual->extend = extend;
                cairo_matrix_init (&visual->matrix, viewport.width / width, 0, 0,
                                   viewport.height / height, viewport.x, viewport.y);
                cairo_matrix_multiply (&visual->matrix, &visual->matrix, &matrix);
                cairo_matrix_invert (&visual->matrix);
                sub_ctx = g_slice_new0 (GXPSRenderContext);
                sub_ctx->page = brush->ctx->page;
                sub_ctx->cr = brush->ctx->cr;
                sub_ctx->visual = visual;
                gxps_page_render_parser_push (context, sub_ctx);
        } else {
                gxps_parse_error (context,
                                  brush->ctx->page->priv->source,
                                  G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                                  element_name, NULL, NULL, error);
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
                GXPSBrushImage  *brush_image;
                GXPSImage       *image;
                GError          *err = NULL;

                brush_image = g_markup_parse_context_pop (context);

                GXPS_DEBUG (g_message ("set_fill_pattern (image)"));
                image = gxps_page_get_image (brush->ctx->page, brush_image->image_uri, &err);
                if (image) {
                        cairo_matrix_t   matrix;
                        gdouble          x_scale, y_scale;
                        cairo_surface_t *clip_surface;

                        /* viewbox units is 1/96 inch, convert to pixels */
                        brush_image->viewbox.x *= image->res_x / 96;
                        brush_image->viewbox.y *= image->res_y / 96;
                        brush_image->viewbox.width *= image->res_x / 96;
                        brush_image->viewbox.height *= image->res_y / 96;

                        clip_surface = cairo_surface_create_for_rectangle (image->surface,
                                                                           brush_image->viewbox.x,
                                                                           brush_image->viewbox.y,
                                                                           brush_image->viewbox.width,
                                                                           brush_image->viewbox.height);
                        brush_image->brush->pattern = cairo_pattern_create_for_surface (clip_surface);
                        cairo_pattern_set_extend (brush_image->brush->pattern, brush_image->extend);

                        x_scale = brush_image->viewport.width / brush_image->viewbox.width;
                        y_scale = brush_image->viewport.height / brush_image->viewbox.height;
                        cairo_matrix_init (&matrix, x_scale, 0, 0, y_scale,
                                           brush_image->viewport.x, brush_image->viewport.y);
                        cairo_matrix_multiply (&matrix, &matrix, &brush_image->matrix);
                        cairo_matrix_invert (&matrix);
                        cairo_pattern_set_matrix (brush_image->brush->pattern, &matrix);

                        if (brush->opacity != 1.0) {
                                cairo_push_group (brush->ctx->cr);
                                cairo_set_source (brush->ctx->cr, brush_image->brush->pattern);
                                cairo_pattern_destroy (brush_image->brush->pattern);
                                cairo_paint_with_alpha (brush->ctx->cr, brush->opacity);
                                brush_image->brush->pattern = cairo_pop_group (brush->ctx->cr);
                        }

                        if (cairo_pattern_status (brush_image->brush->pattern)) {
                                GXPS_DEBUG (g_debug ("%s", cairo_status_to_string (cairo_pattern_status (brush_image->brush->pattern))));
                                cairo_pattern_destroy (brush_image->brush->pattern);
                                brush_image->brush->pattern = NULL;
                        }
                        cairo_surface_destroy (clip_surface);
                } else if (err) {
                        GXPS_DEBUG (g_debug ("%s", err->message));
                        g_error_free (err);
                }
                gxps_brush_image_free (brush_image);
        } else if (strcmp (element_name, "VisualBrush") == 0) {
                GXPSRenderContext *sub_ctx;
                GXPSBrushVisual   *visual;
                cairo_matrix_t     matrix;

                sub_ctx = g_markup_parse_context_pop (context);
                visual = sub_ctx->visual;
                g_slice_free (GXPSRenderContext, sub_ctx);

                GXPS_DEBUG (g_message ("set_fill_pattern (visual)"));
                visual->brush->pattern = cairo_pop_group (brush->ctx->cr);
                /* Undo the clip */
                cairo_restore (brush->ctx->cr);
                cairo_pattern_set_extend (visual->brush->pattern, visual->extend);
                cairo_pattern_get_matrix (visual->brush->pattern, &matrix);
                cairo_matrix_multiply (&matrix, &visual->matrix, &matrix);
                cairo_pattern_set_matrix (visual->brush->pattern, &matrix);
                if (cairo_pattern_status (visual->brush->pattern)) {
                        GXPS_DEBUG (g_debug ("%s", cairo_status_to_string (cairo_pattern_status (visual->brush->pattern))));
                        cairo_pattern_destroy (visual->brush->pattern);
                        visual->brush->pattern = NULL;
                }

                gxps_brush_visual_free (visual);
        } else {
                gxps_parse_error (context,
                                  brush->ctx->page->priv->source,
                                  G_MARKUP_ERROR_UNKNOWN_ELEMENT,
                                  element_name, NULL, NULL, error);

        }
}

static void
brush_error (GMarkupParseContext *context,
	     GError              *error,
	     gpointer             user_data)
{
	GXPSBrush *brush = (GXPSBrush *)user_data;
	gxps_brush_free (brush);
}

static GMarkupParser brush_parser = {
        brush_start_element,
        brush_end_element,
        NULL,
        NULL,
        brush_error
};

void
gxps_brush_parser_push (GMarkupParseContext *context,
                        GXPSBrush           *brush)
{
        g_markup_parse_context_push (context, &brush_parser, brush);
}
