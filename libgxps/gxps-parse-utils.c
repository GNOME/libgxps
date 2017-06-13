/*
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
#include <errno.h>
#include <glib.h>

#include "gxps-parse-utils.h"
#include "gxps-private.h"

#define BUFFER_SIZE 4096

/* GXPSCharsetConverter. Based on GeditSmartCharsetConverter */
typedef struct _GXPSCharsetConverter {
	GObject parent;

	GCharsetConverter *conv;
	gboolean           is_utf8;
} GXPSCharsetConverter;

typedef struct _GXPSCharsetConverterClass {
	GObjectClass parent_class;
} GXPSCharsetConverterClass;

static GType gxps_charset_converter_get_type   (void) G_GNUC_CONST;
static void  gxps_charset_converter_iface_init (GConverterIface *iface);

#define GXPS_TYPE_CHARSET_CONVERTER (gxps_charset_converter_get_type())
#define GXPS_CHARSET_CONVERTER(obj) (G_TYPE_CHECK_INSTANCE_CAST (obj, GXPS_TYPE_CHARSET_CONVERTER, GXPSCharsetConverter))

G_DEFINE_TYPE_WITH_CODE (GXPSCharsetConverter, gxps_charset_converter, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (G_TYPE_CONVERTER,
						gxps_charset_converter_iface_init))

static void
gxps_charset_converter_finalize (GObject *object)
{
	GXPSCharsetConverter *conv = GXPS_CHARSET_CONVERTER (object);

	g_clear_object (&conv->conv);

	G_OBJECT_CLASS (gxps_charset_converter_parent_class)->finalize (object);
}

static void
gxps_charset_converter_init (GXPSCharsetConverter *converter)
{
}

static void
gxps_charset_converter_class_init (GXPSCharsetConverterClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = gxps_charset_converter_finalize;
}

static GConverterResult
gxps_charset_converter_convert (GConverter       *converter,
				const void       *inbuf,
				gsize             inbuf_size,
				void             *outbuf,
				gsize             outbuf_size,
				GConverterFlags   flags,
				gsize            *bytes_read,
				gsize            *bytes_written,
				GError          **error)
{
	GXPSCharsetConverter *conv = GXPS_CHARSET_CONVERTER (converter);

	if (!conv->conv && !conv->is_utf8) {
		const gchar *end;

		if (g_utf8_validate (inbuf, inbuf_size, &end)) {
			conv->is_utf8 = TRUE;
		} else if ((inbuf_size - (end - (gchar *)inbuf)) < 6) {
			conv->is_utf8 = TRUE;
		} else {
			conv->conv = g_charset_converter_new ("UTF-8", "UTF-16", NULL);
		}
	}

	/* if the encoding is utf8 just redirect the input to the output */
	if (conv->is_utf8) {
		gsize            size;
		GConverterResult ret;

		size = MIN (inbuf_size, outbuf_size);

		memcpy (outbuf, inbuf, size);
		*bytes_read = size;
		*bytes_written = size;

		ret = G_CONVERTER_CONVERTED;

		if (flags & G_CONVERTER_INPUT_AT_END)
			ret = G_CONVERTER_FINISHED;
		else if (flags & G_CONVERTER_FLUSH)
			ret = G_CONVERTER_FLUSHED;

		return ret;
	}

	return g_converter_convert (G_CONVERTER (conv->conv),
				    inbuf, inbuf_size,
				    outbuf, outbuf_size,
				    flags,
				    bytes_read,
				    bytes_written,
				    error);
}

static void
gxps_charset_converter_reset (GConverter *converter)
{
	GXPSCharsetConverter *conv = GXPS_CHARSET_CONVERTER (converter);

	g_clear_object (&conv->conv);
	conv->is_utf8 = FALSE;
}

static void
gxps_charset_converter_iface_init (GConverterIface *iface)
{
	iface->convert = gxps_charset_converter_convert;
	iface->reset = gxps_charset_converter_reset;
}

static GXPSCharsetConverter *
gxps_charset_converter_new (void)
{
	return (GXPSCharsetConverter *)g_object_new (GXPS_TYPE_CHARSET_CONVERTER, NULL);
}


#define utf8_has_bom(x) (x[0] == 0xef && x[1] == 0xbb && x[2] == 0xbf)

gboolean
gxps_parse_stream (GMarkupParseContext  *context,
		   GInputStream         *stream,
		   GError              **error)
{
	GXPSCharsetConverter *converter;
	GInputStream         *cstream;
	guchar                buffer[BUFFER_SIZE];
	gssize                bytes_read;
	gboolean              has_bom;
	gint                  line, column;
	gboolean              retval = TRUE;

	converter = gxps_charset_converter_new ();
	cstream = g_converter_input_stream_new (stream, G_CONVERTER (converter));
	g_object_unref (converter);

	do {
		bytes_read = g_input_stream_read (cstream, buffer, BUFFER_SIZE, NULL, error);
		if (bytes_read < 0) {
			retval = FALSE;
			break;
		}

		g_markup_parse_context_get_position (context, &line, &column);
		has_bom = line == 1 && column == 1 && bytes_read >= 3 && utf8_has_bom (buffer);
		if (!g_markup_parse_context_parse (context,
						   has_bom ? (const gchar *)buffer + 3 : (const gchar *)buffer,
						   has_bom ? bytes_read - 3 : bytes_read,
						   error)) {
			retval = FALSE;
			break;
		}
	} while (bytes_read > 0);

	if (retval)
		g_markup_parse_context_end_parse (context, error);
	g_object_unref (cstream);

	return retval;
}

void
gxps_parse_error (GMarkupParseContext  *context,
		  const gchar          *source,
		  GMarkupError          error_type,
		  const gchar          *element_name,
		  const gchar          *attribute_name,
		  const gchar          *content,
		  GError              **error)
{
	gint line, column;

	g_markup_parse_context_get_position (context, &line, &column);

	switch (error_type) {
	case G_MARKUP_ERROR_UNKNOWN_ELEMENT:
		g_set_error (error,
			     G_MARKUP_ERROR, error_type,
			     "%s:%d:%d invalid element '%s'",
			     source, line, column, element_name);
		break;
	case G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE:
		g_set_error (error,
			     G_MARKUP_ERROR, error_type,
			     "%s:%d:%d unknown attribute '%s' of element '%s'",
			     source, line, column, attribute_name, element_name);
		break;
	case G_MARKUP_ERROR_INVALID_CONTENT:
		if (attribute_name) {
			g_set_error (error,
				     G_MARKUP_ERROR, error_type,
				     "%s:%d:%d invalid content in attribute '%s' of element '%s': %s",
				     source, line, column, attribute_name, element_name, content);
		} else {
			g_set_error (error,
				     G_MARKUP_ERROR, error_type,
				     "%s:%d:%d invalid content in element '%s': %s",
				     source, line, column, element_name, content);
		}
		break;
	case G_MARKUP_ERROR_MISSING_ATTRIBUTE:
		g_set_error (error,
			     G_MARKUP_ERROR, error_type,
			     "%s:%d:%d missing attribute '%s' of element '%s'",
			     source, line, column, attribute_name, element_name);
		break;
	default:
		break;
	}
}

gboolean
gxps_value_get_int (const gchar *value,
		    gint        *int_value)
{
	gint64 result;
	gchar *endptr;

        if (!value)
                return FALSE;

	errno = 0;
	result = g_ascii_strtoll (value, &endptr, 10);
	if (errno || endptr == value || result > G_MAXINT || result < G_MININT)
		return FALSE;

	*int_value = result;

	return TRUE;
}

gboolean
gxps_value_get_double (const gchar *value,
                       gdouble     *double_value)
{
        gdouble result;
        gchar  *endptr;

        if (!value)
                return FALSE;

        errno = 0;
        result = g_ascii_strtod (value, &endptr);
        if (errno || endptr == value)
                return FALSE;

        *double_value = result;

        return TRUE;
}

gboolean
gxps_value_get_boolean (const gchar *value,
                        gboolean    *boolean_value)
{
        if (!value)
                return FALSE;

        if (strcmp (value, "true") == 0) {
                *boolean_value = TRUE;
                return TRUE;
        } else if (strcmp (value, "false") == 0) {
                *boolean_value = FALSE;
                return TRUE;
        }

        return FALSE;
}

gboolean
gxps_value_get_double_positive (const gchar *value,
                                gdouble     *double_value)
{
        if (!gxps_value_get_double (value, double_value))
                return FALSE;

        return *double_value >= 1;
}

gboolean
gxps_value_get_double_non_negative (const gchar *value,
                                    gdouble     *double_value)
{
        if (!gxps_value_get_double (value, double_value))
                return FALSE;

        return *double_value >= 0;
}

gboolean
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

void
gxps_parse_skip_number (gchar      **iter,
                        const gchar *end)
{
        gchar *p = *iter;

        p++;
        while (p != end && g_ascii_isdigit (*p))
                p++;
        if (p == end) {
                *iter = p;
                return;
        }

        if (*p == '.')
                p++;

        while (p != end && g_ascii_isdigit (*p))
                p++;
        if (p == end) {
                *iter = p;
                return;
        }

        if (*p == 'e' || *p == 'E')
                p++;
        if (p == end) {
                *iter = p;
                return;
        }

        if (*p == '+' || *p == '-')
                p++;

        while (p != end && g_ascii_isdigit (*p))
                p++;
        *iter = p;
}

/* NOTE: Taken from glocalfile. Because we always need to use / on all platforms */
static char *
canonicalize_filename (const char *filename)
{
	char *canon, *start, *p, *q;
	char *cwd;
	int i;

	if (!g_path_is_absolute (filename)) {
		cwd = g_get_current_dir ();
		canon = g_build_path ("/", cwd, filename, NULL);
		g_free (cwd);
	} else
		canon = g_strdup (filename);

	start = (char *)g_path_skip_root (canon);

	if (start == NULL) {
		/* This shouldn't really happen, as g_get_current_dir() should
		 * return an absolute pathname, but bug 573843 shows this is
		 * not always happening
		 */
		g_free (canon);
		return g_build_path ("/", "/", filename, NULL);
	}

	/* POSIX allows double slashes at the start to
	 * mean something special (as does windows too).
	 * So, "//" != "/", but more than two slashes
	 * is treated as "/".
	 */
	i = 0;
	for (p = start - 1; (p >= canon) && (*p == '/'); p--)
		i++;

	if (i > 2) {
		i -= 1;
		start -= i;
		memmove (start, start+i, strlen (start+i)+1);
	}

	/* Make sure we're using the canonical dir separator */
	p++;
	while (p < start && *p == '/')
		*p++ = '/';

	p = start;
	while (*p != 0) {
		if (p[0] == '.' && (p[1] == 0 || p[1] == '/')) {
			memmove (p, p+1, strlen (p+1)+1);
		} else if (p[0] == '.' && p[1] == '.' && (p[2] == 0 || p[2] == '/')) {
			q = p + 2;
			/* Skip previous separator */
			p = p - 2;
			if (p < start)
				p = start;
			while (p > start && *p != '/')
				p--;
			if (*p == '/')
				*p++ = '/';
			memmove (p, q, strlen (q)+1);
		} else {
			/* Skip until next separator */
			while (*p != 0 && *p != '/')
				p++;

			if (*p != 0) {
				/* Canonicalize one separator */
				*p++ = '/';
			}
		}

		/* Remove additional separators */
		q = p;
		while (*q && *q == '/')
			q++;

		if (p != q)
			memmove (p, q, strlen (q)+1);
	}

	/* Remove trailing slashes */
	if (p > start && *(p-1) == '/')
		*(p-1) = 0;

	return canon;
}

gchar *
gxps_resolve_relative_path (const gchar *source,
			    const gchar *target)
{
	gchar *dirname;
	gchar *abs_path;
	gchar *retval;

	if (target[0] == '/')
		return g_strdup (target);

	dirname = g_path_get_dirname (source);
	if (strlen (dirname) == 1 && dirname[0] == '.')
		dirname[0] = '/';
	abs_path = g_build_path ("/", dirname, target, NULL);
	g_free (dirname);

	retval = canonicalize_filename (abs_path);
	g_free (abs_path);

	return retval;
}
