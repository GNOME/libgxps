/* GXPSFonts
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

#include <glib.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <cairo-ft.h>
#include <string.h>

#include "gxps-fonts.h"
#include "gxps-error.h"

#define FONTS_CACHE_KEY "gxps-fonts-cache"

static gsize ft_font_face_cache = 0;
static FT_Library ft_lib;
static const cairo_user_data_key_t ft_cairo_key;

static void
init_ft_lib (void)
{
	static gsize ft_init = 0;

	if (g_once_init_enter (&ft_init)) {
		FT_Init_FreeType (&ft_lib);

		g_once_init_leave (&ft_init, (gsize)1);
	}
}

typedef struct {
	guchar *font_data;
	gsize   font_data_len;
} FtFontFace;

static FtFontFace *
ft_font_face_new (guchar *font_data,
		  gsize   font_data_len)
{
	FtFontFace *ff;

	ff = g_slice_new (FtFontFace);

	ff->font_data = font_data;
	ff->font_data_len = font_data_len;

	return ff;
}

static void
ft_font_face_free (FtFontFace *font_face)
{
	if (!font_face)
		return;

	g_slice_free (FtFontFace, font_face);
}

static guint
ft_font_face_hash (gconstpointer v)
{
	FtFontFace *ft_face = (FtFontFace *)v;
	guchar     *bytes = ft_face->font_data;
	gssize      len = ft_face->font_data_len;
	guint       hash = 5381;

	while (len--) {
		guchar c = *bytes++;

		hash *= 33;
		hash ^= c;
	}

	return hash;
}

static gboolean
ft_font_face_equal (gconstpointer v1,
		    gconstpointer v2)
{
	FtFontFace *ft_face_1 = (FtFontFace *)v1;
	FtFontFace *ft_face_2 = (FtFontFace *)v2;

	if (ft_face_1->font_data_len != ft_face_2->font_data_len)
		return FALSE;

	return memcmp (ft_face_1->font_data, ft_face_2->font_data, ft_face_1->font_data_len) == 0;
}

static GHashTable *
get_ft_font_face_cache (void)
{
	if (g_once_init_enter (&ft_font_face_cache)) {
		GHashTable *h;

		h = g_hash_table_new_full (ft_font_face_hash,
					   ft_font_face_equal,
					   (GDestroyNotify)ft_font_face_free,
					   (GDestroyNotify)cairo_font_face_destroy);
		g_once_init_leave (&ft_font_face_cache, (gsize)h);
	}

	return (GHashTable *)ft_font_face_cache;
}

static gboolean
hex_int (const gchar *spec,
	 gint         len,
	 guint       *c)
{
	const gchar *end;

	*c = 0;
	for (end = spec + len; spec != end; spec++) {
		if (!g_ascii_isxdigit (*spec))
			return FALSE;

		*c = g_ascii_xdigit_value (*spec);
	}

	return TRUE;
}

/* Obfuscated fonts? Based on okular code */
static gboolean
parse_guid (gchar *string, unsigned short guid[16])
{
	// Maps bytes to positions in guidString
	static const int indexes[] = {6, 4, 2, 0, 11, 9, 16, 14, 19, 21, 24, 26, 28, 30, 32, 34};
	int i;

	if (strlen (string) <= 35) {
		return FALSE;
	}

	for (i = 0; i < 16; i++) {
		guint hex1;
		guint hex2;

		if (!hex_int (string + indexes[i], 1, &hex1) ||
		    !hex_int (string + indexes[i] + 1, 1, &hex2))
			return FALSE;

		guid[i] = hex1 * 16 + hex2;
	}

	return TRUE;
}

static gboolean
gxps_fonts_new_ft_face (const gchar *font_uri,
			guchar      *font_data,
			gsize        font_data_len,
			FT_Face     *face)
{
	init_ft_lib ();

	if (FT_New_Memory_Face (ft_lib, font_data, font_data_len, 0, face)) {
		/* Failed to load, probably obfuscated font */
		gchar         *base_name;
		unsigned short guid[16];

		base_name = g_path_get_basename (font_uri);
		if (!parse_guid (base_name, guid)) {
			g_warning ("Failed to parse guid for font %s\n", font_uri);
			g_free (base_name);

			return FALSE;
		}
		g_free (base_name);

		if (font_data_len >= 32) {
			// Obfuscation - xor bytes in font binary with bytes from guid (font's filename)
			static const gint mapping[] = {15, 14, 13, 12, 11, 10, 9, 8, 6, 7, 4, 5, 0, 1, 2, 3};
			gint i;

			for (i = 0; i < 16; i++) {
				font_data[i] ^= guid[mapping[i]];
				font_data[i + 16] ^= guid[mapping[i]];
			}

			if (FT_New_Memory_Face (ft_lib, font_data, font_data_len, 0, face))
				return FALSE;
		} else {
			g_warning ("Font file is too small\n");
			return FALSE;
		}
	}

	return TRUE;
}

static cairo_font_face_t *
gxps_fonts_new_font_face (GXPSArchive *zip,
			  const gchar *font_uri,
			  GError     **error)
{
	GHashTable        *ft_cache;
	FtFontFace         ft_face;
	FtFontFace        *ft_font_face;
	FT_Face            face;
	cairo_font_face_t *font_face;
	guchar            *font_data;
	gsize              font_data_len;

        if (!gxps_archive_read_entry (zip, font_uri,
                                      &font_data, &font_data_len,
                                      error)) {
                return NULL;
        }

	ft_face.font_data = font_data;
	ft_face.font_data_len = (gssize)font_data_len;

	ft_cache = get_ft_font_face_cache ();
	font_face = g_hash_table_lookup (ft_cache, &ft_face);
	if (font_face) {
		g_free (font_data);

		return font_face;
	}

	if (!gxps_fonts_new_ft_face (font_uri, font_data, font_data_len, &face)) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_FONT,
			     "Failed to load font %s", font_uri);
		g_free (font_data);

		return NULL;
	}

	font_face = cairo_ft_font_face_create_for_ft_face (face, 0);
	if (cairo_font_face_set_user_data (font_face,
					   &ft_cairo_key,
					   face,
					   (cairo_destroy_func_t) FT_Done_Face)) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_FONT,
			     "Failed to load font %s: %s",
			     font_uri,
			     cairo_status_to_string (cairo_font_face_status (font_face)));
		cairo_font_face_destroy (font_face);
		FT_Done_Face (face);

		return NULL;
	}

	ft_font_face = ft_font_face_new (font_data, (gssize)font_data_len);
	g_hash_table_insert (ft_cache, ft_font_face, font_face);

	return font_face;
}

cairo_font_face_t *
gxps_fonts_get_font (GXPSArchive *zip,
		     const gchar *font_uri,
		     GError     **error)
{
	GHashTable        *fonts_cache;
	cairo_font_face_t *font_face = NULL;

	fonts_cache = g_object_get_data (G_OBJECT (zip), FONTS_CACHE_KEY);
	if (fonts_cache) {
		font_face = g_hash_table_lookup (fonts_cache, font_uri);
		if (font_face)
			return font_face;
	}

	font_face = gxps_fonts_new_font_face (zip, font_uri, error);
	if (font_face) {
		if (!fonts_cache) {
			fonts_cache = g_hash_table_new_full (g_str_hash,
							     g_str_equal,
							     (GDestroyNotify)g_free,
							     (GDestroyNotify)cairo_font_face_destroy);
			g_object_set_data_full (G_OBJECT (zip), FONTS_CACHE_KEY,
						fonts_cache,
						(GDestroyNotify)g_hash_table_destroy);
		}

		g_hash_table_insert (fonts_cache,
				     g_strdup (font_uri),
				     cairo_font_face_reference (font_face));
	}

	return font_face;
}
