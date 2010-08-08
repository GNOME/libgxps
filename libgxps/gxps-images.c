/* GXPSImages
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_LIBJPEG
#include <jpeglib.h>
#include <setjmp.h>
#endif

#ifdef HAVE_LIBTIFF
#include <tiffio.h>
#endif

#include "gxps-images.h"
#include "gxps-error.h"

/* PNG */
static cairo_status_t
_read_png (GInputStream *stream,
	   guchar       *data,
	   guint         len)
{
	gssize bytes_read;

	bytes_read = g_input_stream_read (stream, data, len, NULL, NULL);

	return (bytes_read > 0 && bytes_read == len) ?
		CAIRO_STATUS_SUCCESS : CAIRO_STATUS_READ_ERROR;
}

static cairo_surface_t *
gxps_images_create_from_png (GXPSArchive *zip,
			     const gchar *image_uri,
			     GError     **error)
{
	GInputStream    *stream;
	cairo_surface_t *surface;

	stream = gxps_archive_open (zip, image_uri);
	if (!stream) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_SOURCE_NOT_FOUND,
			     "Image source %s not found in archive",
			     image_uri);
		return NULL;
	}

	surface = cairo_image_surface_create_from_png_stream ((cairo_read_func_t)_read_png, stream);
	g_object_unref (stream);
	if (cairo_surface_status (surface)) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_IMAGE,
			     "Error loading PNG image %s: %s",
			     image_uri,
			     cairo_status_to_string (cairo_surface_status (surface)));
		cairo_surface_destroy (surface);

		return NULL;
	}

	return surface;
}

/* JPEG */
#ifdef HAVE_LIBJPEG
#define JPEG_PROG_BUF_SIZE 65536

struct _jpeg_src_mgr {
	struct jpeg_source_mgr pub;
	GInputStream *stream;
	JOCTET *buffer;
	jmp_buf setjmp_buffer;
};

static const gchar *
_jpeg_color_space_name (const J_COLOR_SPACE jpeg_color_space)
{
	switch (jpeg_color_space) {
	case JCS_UNKNOWN: return "UNKNOWN";
	case JCS_GRAYSCALE: return "GRAYSCALE";
	case JCS_RGB: return "RGB";
	case JCS_YCbCr: return "YCbCr";
	case JCS_CMYK: return "CMYK";
	case JCS_YCCK: return "YCCK";
	default: return "invalid";
	}
}

static void
_jpeg_init_source (j_decompress_ptr cinfo)
{
}

static int
_jpeg_fill_input_buffer (j_decompress_ptr cinfo)
{
	struct _jpeg_src_mgr *src = (struct _jpeg_src_mgr *)cinfo->src;
	gssize num_bytes;

	num_bytes = g_input_stream_read (src->stream, src->buffer, JPEG_PROG_BUF_SIZE, NULL, NULL);
	if (num_bytes <= 0) {
		/* Insert a fake EOI marker */
		src->buffer[0] = (JOCTET) 0xFF;
		src->buffer[1] = (JOCTET) JPEG_EOI;
	}

	src->pub.next_input_byte = src->buffer;
	src->pub.bytes_in_buffer = num_bytes;

	return TRUE;
}

static void
_jpeg_skip_input_data (j_decompress_ptr cinfo, long num_bytes)
{
	struct _jpeg_src_mgr *src = (struct _jpeg_src_mgr *)cinfo->src;

	if (num_bytes > 0) {
		while (num_bytes > (long) src->pub.bytes_in_buffer) {
			num_bytes -= (long) src->pub.bytes_in_buffer;
			_jpeg_fill_input_buffer (cinfo);
		}
		src->pub.next_input_byte += (size_t) num_bytes;
		src->pub.bytes_in_buffer -= (size_t) num_bytes;
	}
}

static void
_jpeg_term_source (j_decompress_ptr cinfo)
{
}

static void
_jpeg_error_exit (j_common_ptr error)
{
	j_decompress_ptr cinfo = (j_decompress_ptr)error;
	struct _jpeg_src_mgr *src = (struct _jpeg_src_mgr *)cinfo->src;

	longjmp (src->setjmp_buffer, 1);
}
#endif /* HAVE_LIBJPEG */

static cairo_surface_t *
gxps_images_create_from_jpeg (GXPSArchive *zip,
			      const gchar *image_uri,
			      GError     **error)
{
#ifdef HAVE_LIBJPEG
	GInputStream                 *stream;
	struct jpeg_error_mgr         error_mgr;
	struct jpeg_decompress_struct cinfo;
	struct _jpeg_src_mgr          src;
	cairo_surface_t              *surface;
	guchar                       *data;
	gint                          stride;
	JSAMPARRAY                    lines;
	gint                          jpeg_stride;
	gint                          i;

	stream = gxps_archive_open (zip, image_uri);
	if (!stream) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_SOURCE_NOT_FOUND,
			     "Image source %s not found in archive",
			     image_uri);
		return NULL;
	}

	jpeg_std_error (&error_mgr);
	error_mgr.error_exit = _jpeg_error_exit;

	jpeg_create_decompress (&cinfo);
	cinfo.err = &error_mgr;

	src.stream = stream;
	src.buffer = (JOCTET *)	(*cinfo.mem->alloc_small) ((j_common_ptr) &cinfo, JPOOL_PERMANENT,
							   JPEG_PROG_BUF_SIZE * sizeof (JOCTET));

	src.pub.init_source = _jpeg_init_source;
	src.pub.fill_input_buffer = _jpeg_fill_input_buffer;
	src.pub.skip_input_data = _jpeg_skip_input_data;
	src.pub.resync_to_restart = jpeg_resync_to_restart;
	src.pub.term_source = _jpeg_term_source;
	src.pub.bytes_in_buffer = 0; /* forces fill_input_buffer on first read */
	src.pub.next_input_byte = NULL; /* until buffer loaded */
	cinfo.src = (struct jpeg_source_mgr *)&src;

	if (setjmp (src.setjmp_buffer)) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_IMAGE,
			     "Error loading JPEG image %s",
			     image_uri);
		g_object_unref (stream);
		return NULL;
	}

	jpeg_read_header (&cinfo, TRUE);

	cinfo.do_fancy_upsampling = FALSE;
	jpeg_start_decompress (&cinfo);

	surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
					      cinfo.output_width,
					      cinfo.output_height);
	if (cairo_surface_status (surface)) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_IMAGE,
			     "Error loading JPEG image %s: %s",
			     image_uri,
			     cairo_status_to_string (cairo_surface_status (surface)));
		jpeg_destroy_decompress (&cinfo);
		cairo_surface_destroy (surface);
		g_object_unref (stream);

		return NULL;
	}

	data = cairo_image_surface_get_data (surface);
	stride = cairo_image_surface_get_stride (surface);
	jpeg_stride = cinfo.output_width * cinfo.out_color_components;
	lines = cinfo.mem->alloc_sarray((j_common_ptr) &cinfo, JPOOL_IMAGE, jpeg_stride, 4);

	while (cinfo.output_scanline < cinfo.output_height) {
		gint n_lines, x;

		n_lines = jpeg_read_scanlines (&cinfo, lines, cinfo.rec_outbuf_height);
		for (i = 0; i < n_lines; i++) {
			JSAMPLE *line = lines[i];
			guchar  *p = data;

			for (x = 0; x < cinfo.output_width; x++) {
				switch (cinfo.out_color_space) {
				case JCS_RGB:
					p[0] = line[2];
					p[1] = line[1];
					p[2] = line[0];
					p[3] = 0xff;
					break;
				default:
					g_warning ("Unsupported jpeg color space %s\n",
						   _jpeg_color_space_name (cinfo.out_color_space));

					cairo_surface_destroy (surface);
					jpeg_destroy_decompress (&cinfo);
					g_object_unref (stream);
					return NULL;
				}
				p[0] = line[2];
				p[1] = line[1];
				p[2] = line[0];
				p[3] = 0xff;
				line += cinfo.out_color_components;
				p += 4;
			}

			data += stride;
		}
	}

	jpeg_finish_decompress (&cinfo);
	jpeg_destroy_decompress (&cinfo);
	g_object_unref (stream);

	cairo_surface_mark_dirty (surface);

	if (cairo_surface_status (surface)) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_IMAGE,
			     "Error loading JPEG image %s: %s",
			     image_uri,
			     cairo_status_to_string (cairo_surface_status (surface)));
		cairo_surface_destroy (surface);

		return NULL;
	}

	return surface;
#else
	return NULL;
#endif /* HAVE_LIBJPEG */
}

/* Tiff */
#ifdef HAVE_LIBTIFF
static TIFFErrorHandler orig_error_handler = NULL;
static TIFFErrorHandler orig_warning_handler = NULL;
static gchar *_tiff_error = NULL;

typedef struct {
	guchar *buffer;
	gsize   buffer_len;
	guint   pos;
} TiffBuffer;

static void
fill_tiff_error (GError     **error,
		 const gchar *image_uri)
{
	if (_tiff_error) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_IMAGE,
			     "Error loading TIFF image %s: %s",
			     image_uri, _tiff_error);
		g_free (_tiff_error);
		_tiff_error = NULL;
	} else {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_IMAGE,
			     "Error loading TIFF image %s",
			     image_uri);
	}
}

static void
_tiff_error_handler (const char *mod,
		     const char *fmt,
		     va_list     ap)
{
	if (G_UNLIKELY (_tiff_error))
		return;

	_tiff_error = g_strdup_vprintf (fmt, ap);
}

static void
_tiff_push_handlers (void)
{
	orig_error_handler = TIFFSetErrorHandler (_tiff_error_handler);
	orig_warning_handler = TIFFSetWarningHandler (NULL);
}

static void
_tiff_pop_handlers (void)
{
	TIFFSetErrorHandler (orig_error_handler);
	TIFFSetWarningHandler (orig_warning_handler);
}

static tsize_t
_tiff_read (thandle_t handle,
	    tdata_t   buf,
	    tsize_t   size)
{
	TiffBuffer *buffer = (TiffBuffer *)handle;

	if (buffer->pos + size > buffer->buffer_len)
		return 0;

	memcpy (buf, buffer->buffer + buffer->pos, size);
	buffer->pos += size;

	return size;
}

static tsize_t
_tiff_write (thandle_t handle,
	     tdata_t   buf,
	     tsize_t   size)
{
	return -1;
}

static toff_t
_tiff_seek (thandle_t handle,
	    toff_t    offset,
	    int       whence)
{
	TiffBuffer *buffer = (TiffBuffer *)handle;

	switch (whence) {
	case SEEK_SET:
		if (offset > buffer->buffer_len)
			return -1;
		buffer->pos = offset;
		break;
	case SEEK_CUR:
		if (offset + buffer->pos >= buffer->buffer_len)
			return -1;
		buffer->pos += offset;
		break;
	case SEEK_END:
		if (offset + buffer->buffer_len > buffer->buffer_len)
			return -1;
		buffer->pos = buffer->buffer_len + offset;
		break;
	default:
		return -1;
	}

	return buffer->pos;
}

static int
_tiff_close (thandle_t context)
{
	return 0;
}

static toff_t
_tiff_size (thandle_t handle)
{
	TiffBuffer *buffer = (TiffBuffer *)handle;

	return buffer->buffer_len;
}

static int
_tiff_map_file (thandle_t handle,
		tdata_t  *buf,
		toff_t   *size)
{
	TiffBuffer *buffer = (TiffBuffer *)handle;

	*buf = buffer->buffer;
	*size = buffer->buffer_len;

	return 0;
}

static void
_tiff_unmap_file (thandle_t handle,
		  tdata_t   data,
		  toff_t    offset)
{
}
#endif /* #ifdef HAVE_LIBTIFF */

static cairo_surface_t *
gxps_images_create_from_tiff (GXPSArchive *zip,
			      const gchar *image_uri,
			      GError     **error)
{
#ifdef HAVE_LIBTIFF
	TIFF            *tiff;
	TiffBuffer       buffer;
	cairo_surface_t *surface;
	gint             width, height;
	gint             stride;
	guchar          *data;
	guchar          *p;

	if (!gxps_archive_read_entry (zip, image_uri,
				      &buffer.buffer,
				      &buffer.buffer_len,
				      error)) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_SOURCE_NOT_FOUND,
			     "Image source %s not found in archive",
			     image_uri);
		return NULL;
	}

	buffer.pos = 0;

	_tiff_push_handlers ();

	tiff = TIFFClientOpen ("libgxps-tiff", "r", &buffer,
			       _tiff_read,
			       _tiff_write,
			       _tiff_seek,
			       _tiff_close,
			       _tiff_size,
			       _tiff_map_file,
			       _tiff_unmap_file);

	if (!tiff || _tiff_error) {
		fill_tiff_error (error, image_uri);
		if (tiff)
			TIFFClose (tiff);
		_tiff_pop_handlers ();
		g_free (buffer.buffer);
		return NULL;
	}

	if (!TIFFGetField (tiff, TIFFTAG_IMAGEWIDTH, &width) || _tiff_error) {
		fill_tiff_error (error, image_uri);
		TIFFClose (tiff);
		_tiff_pop_handlers ();
		g_free (buffer.buffer);
		return NULL;
	}

	if (!TIFFGetField (tiff, TIFFTAG_IMAGELENGTH, &height) || _tiff_error) {
		fill_tiff_error (error, image_uri);
		TIFFClose (tiff);
		_tiff_pop_handlers ();
		g_free (buffer.buffer);
		return NULL;
	}

	if (width <= 0 || height <= 0) {
		fill_tiff_error (error, image_uri);
		TIFFClose (tiff);
		_tiff_pop_handlers ();
		g_free (buffer.buffer);
		return NULL;
	}

	surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
					      width, height);
	if (cairo_surface_status (surface)) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_IMAGE,
			     "Error loading TIFF image %s: %s",
			     image_uri,
			     cairo_status_to_string (cairo_surface_status (surface)));
		cairo_surface_destroy (surface);
		TIFFClose (tiff);
		_tiff_pop_handlers ();
		g_free (buffer.buffer);
		return NULL;
	}

	data = cairo_image_surface_get_data (surface);
	if (!TIFFReadRGBAImageOriented (tiff, width, height,
					(uint32 *)data,
					ORIENTATION_TOPLEFT, 1) || _tiff_error) {
		fill_tiff_error (error, image_uri);
		cairo_surface_destroy (surface);
		TIFFClose (tiff);
		_tiff_pop_handlers ();
		g_free (buffer.buffer);
		return NULL;
	}

	TIFFClose (tiff);
	_tiff_pop_handlers ();
	g_free (buffer.buffer);

	stride = cairo_image_surface_get_stride (surface);
	p = data;
	while (p < data + (height * stride)) {
		guint32 *pixel = (guint32 *)p;
		guint8   r = TIFFGetR (*pixel);
		guint8   g = TIFFGetG (*pixel);
		guint8   b = TIFFGetB (*pixel);
		guint8   a = TIFFGetA (*pixel);

		*pixel = (a << 24) | (r << 16) | (g << 8) | b;

		p += 4;
	}

	cairo_surface_mark_dirty (surface);

	return surface;
#else
	return NULL;
#endif /* #ifdef HAVE_LIBTIFF */
}

static gchar *
gxps_images_guess_content_type (GXPSArchive *zip,
				const gchar *image_uri)
{
	GInputStream *stream;
	guchar        buffer[1024];
	gssize        bytes_read;
	gchar        *mime_type;

	stream = gxps_archive_open (zip, image_uri);
	if (!stream)
		return NULL;

	bytes_read = g_input_stream_read (stream, buffer, 1024, NULL, NULL);
	mime_type = g_content_type_guess (NULL, buffer, bytes_read, NULL);
	g_object_unref (stream);

	return mime_type;
}

cairo_surface_t *
gxps_images_get_image (GXPSArchive *zip,
		       const gchar *image_uri,
		       GError     **error)
{
	cairo_surface_t *surface = NULL;

	/* First try with extensions,
	 * as it's recommended by the spec
	 * (2.1.5 Image Parts)
	 */
	if (g_str_has_suffix (image_uri, ".png")) {
		surface = gxps_images_create_from_png (zip, image_uri, error);
	} else if (g_str_has_suffix (image_uri, ".jpg")) {
		surface = gxps_images_create_from_jpeg (zip, image_uri, error);
	} else if (g_str_has_suffix (image_uri, ".tif")) {
		surface = gxps_images_create_from_tiff (zip, image_uri, error);
	} else if (g_str_has_suffix (image_uri, "wdp")) {
		g_warning ("Unsupported image format windows media photo\n");
		return NULL;
	}

	if (!surface) {
		gchar *mime_type;

		mime_type = gxps_images_guess_content_type (zip, image_uri);
		if (g_strcmp0 (mime_type, "image/png") == 0) {
			surface = gxps_images_create_from_png (zip, image_uri, error);
		} else if (g_strcmp0 (mime_type, "image/jpeg") == 0) {
			surface = gxps_images_create_from_jpeg (zip, image_uri, error);
		} else if (g_strcmp0 (mime_type, "image/tiff") == 0) {
			surface = gxps_images_create_from_tiff (zip, image_uri, error);
		} else {
			g_warning ("Unsupported image format: %s\n", mime_type);
		}
		g_free (mime_type);
	}

	return surface;
}
