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
#include <stdint.h>

#ifdef HAVE_LIBPNG
#include <png.h>
#endif

#ifdef HAVE_LIBJPEG
#include <jpeglib.h>
#include <setjmp.h>
#endif

#ifdef HAVE_LIBTIFF
#include <tiffio.h>
#endif

#include "gxps-images.h"
#include "gxps-error.h"
#include "gxps-debug.h"

#define METERS_PER_INCH 0.0254
#define CENTIMETERS_PER_INCH 2.54

#ifdef G_OS_WIN32
#define COBJMACROS
#include <wincodec.h>
#include <wincodecsdk.h>
#include <combaseapi.h>
#endif

/* PNG */
#ifdef HAVE_LIBPNG

static const cairo_user_data_key_t image_data_cairo_key;

static void
_read_png (png_structp png_ptr,
	   png_bytep data,
	   png_size_t len)
{
	GInputStream *stream;

	stream = png_get_io_ptr (png_ptr);
	g_input_stream_read (stream, data, len, NULL, NULL);
}

static void
png_error_callback (png_structp png_ptr,
		    png_const_charp error_msg)
{
	char **msg;

	msg = png_get_error_ptr (png_ptr);
	*msg = g_strdup (error_msg);
	longjmp (png_jmpbuf (png_ptr), 1);
}

static void
png_warning_callback (png_structp png,
		      png_const_charp error_msg)
{
}

/* From cairo's cairo-png.c <http://cairographics.org> */
static inline int
multiply_alpha (int alpha, int color)
{
	int temp = (alpha * color) + 0x80;

	return ((temp + (temp >> 8)) >> 8);
}

/* Premultiplies data and converts RGBA bytes => native endian
 * From cairo's cairo-png.c <http://cairographics.org> */
static void
premultiply_data (png_structp   png,
                  png_row_infop row_info,
                  png_bytep     data)
{
	unsigned int i;

	for (i = 0; i < row_info->rowbytes; i += 4) {
		uint8_t *base  = &data[i];
		uint8_t  alpha = base[3];
		uint32_t p;

		if (alpha == 0) {
			p = 0;
		} else {
			uint8_t red   = base[0];
			uint8_t green = base[1];
			uint8_t blue  = base[2];

			if (alpha != 0xff) {
				red   = multiply_alpha (alpha, red);
				green = multiply_alpha (alpha, green);
				blue  = multiply_alpha (alpha, blue);
			}
			p = (alpha << 24) | (red << 16) | (green << 8) | (blue << 0);
		}
		memcpy (base, &p, sizeof (uint32_t));
	}
}

/* Converts RGBx bytes to native endian xRGB
 * From cairo's cairo-png.c <http://cairographics.org> */
static void
convert_bytes_to_data (png_structp png, png_row_infop row_info, png_bytep data)
{
	unsigned int i;

	for (i = 0; i < row_info->rowbytes; i += 4) {
		uint8_t *base  = &data[i];
		uint8_t  red   = base[0];
		uint8_t  green = base[1];
		uint8_t  blue  = base[2];
		uint32_t pixel;

		pixel = (0xff << 24) | (red << 16) | (green << 8) | (blue << 0);
		memcpy (base, &pixel, sizeof (uint32_t));
	}
}

static void
fill_png_error (GError      **error,
		const gchar  *image_uri,
		const gchar  *msg)
{
	if (msg) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_IMAGE,
			     "Error loading PNG image %s: %s",
			     image_uri, msg);
	} else {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_IMAGE,
			     "Error loading PNG image %s",
			     image_uri);
	}
}

#endif	/* HAVE_LIBPNG */

/* Adapted from cairo's read_png in cairo-png.c
 * http://cairographics.org/ */
static GXPSImage *
gxps_images_create_from_png (GXPSArchive *zip,
			     const gchar *image_uri,
			     GError     **error)
{
#ifdef HAVE_LIBPNG
	GInputStream  *stream;
	GXPSImage     *image = NULL;
	char          *png_err_msg = NULL;
	png_struct    *png;
	png_info      *info;
	png_byte      *data = NULL;
	png_byte     **row_pointers = NULL;
	png_uint_32    png_width, png_height;
	int            depth, color_type, interlace, stride;
	unsigned int   i;
	cairo_format_t format;
	cairo_status_t status;

	stream = gxps_archive_open (zip, image_uri);
	if (!stream) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_SOURCE_NOT_FOUND,
			     "Image source %s not found in archive",
			     image_uri);
		return NULL;
	}

	png = png_create_read_struct (PNG_LIBPNG_VER_STRING,
				      &png_err_msg,
				      png_error_callback,
				      png_warning_callback);
	if (png == NULL) {
		fill_png_error (error, image_uri, NULL);
		g_object_unref (stream);
		return NULL;
	}

	info = png_create_info_struct (png);
	if (info == NULL) {
		fill_png_error (error, image_uri, NULL);
		g_object_unref (stream);
		png_destroy_read_struct (&png, NULL, NULL);
		return NULL;
	}

	png_set_read_fn (png, stream, _read_png);

	if (setjmp (png_jmpbuf (png))) {
		fill_png_error (error, image_uri, png_err_msg);
		g_free (png_err_msg);
		g_object_unref (stream);
		png_destroy_read_struct (&png, &info, NULL);
		gxps_image_free (image);
		g_free (row_pointers);
		g_free (data);
		return NULL;
	}

	png_read_info (png, info);

	png_get_IHDR (png, info,
		      &png_width, &png_height, &depth,
		      &color_type, &interlace, NULL, NULL);

	/* convert palette/gray image to rgb */
	if (color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb (png);

	/* expand gray bit depth if needed */
	if (color_type == PNG_COLOR_TYPE_GRAY)
		png_set_expand_gray_1_2_4_to_8 (png);

	/* transform transparency to alpha */
	if (png_get_valid (png, info, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha (png);

	if (depth == 16)
		png_set_strip_16 (png);

	if (depth < 8)
		png_set_packing (png);

	/* convert grayscale to RGB */
	if (color_type == PNG_COLOR_TYPE_GRAY ||
	    color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb (png);

	if (interlace != PNG_INTERLACE_NONE)
		png_set_interlace_handling (png);

	png_set_filler (png, 0xff, PNG_FILLER_AFTER);

	/* recheck header after setting EXPAND options */
	png_read_update_info (png, info);
	png_get_IHDR (png, info,
		      &png_width, &png_height, &depth,
		      &color_type, &interlace, NULL, NULL);
	if (depth != 8 ||
	    !(color_type == PNG_COLOR_TYPE_RGB ||
              color_type == PNG_COLOR_TYPE_RGB_ALPHA)) {
		fill_png_error (error, image_uri, NULL);
		g_object_unref (stream);
		png_destroy_read_struct (&png, &info, NULL);
		return NULL;
	}

	switch (color_type) {
	default:
		g_assert_not_reached();
		/* fall-through just in case ;-) */

	case PNG_COLOR_TYPE_RGB_ALPHA:
		format = CAIRO_FORMAT_ARGB32;
		png_set_read_user_transform_fn (png, premultiply_data);
		break;

	case PNG_COLOR_TYPE_RGB:
		format = CAIRO_FORMAT_RGB24;
		png_set_read_user_transform_fn (png, convert_bytes_to_data);
		break;
	}

	stride = cairo_format_stride_for_width (format, png_width);
	if (stride < 0 || png_height >= INT_MAX / stride) {
		fill_png_error (error, image_uri, NULL);
		g_object_unref (stream);
		png_destroy_read_struct (&png, &info, NULL);
		return NULL;
	}

	image = g_slice_new0 (GXPSImage);
	image->res_x = png_get_x_pixels_per_meter (png, info) * METERS_PER_INCH;
	if (image->res_x == 0)
		image->res_x = 96;
	image->res_y = png_get_y_pixels_per_meter (png, info) * METERS_PER_INCH;
	if (image->res_y == 0)
		image->res_y = 96;

	data = g_malloc (png_height * stride);
	row_pointers = g_new (png_byte *, png_height);

	for (i = 0; i < png_height; i++)
		row_pointers[i] = &data[i * stride];

	png_read_image (png, row_pointers);
	png_read_end (png, info);
	png_destroy_read_struct (&png, &info, NULL);
	g_object_unref (stream);
	g_free (row_pointers);

	image->surface = cairo_image_surface_create_for_data (data, format,
							      png_width, png_height,
							      stride);
	if (cairo_surface_status (image->surface)) {
		fill_png_error (error, image_uri, NULL);
		gxps_image_free (image);
		g_free (data);
		return NULL;
	}

	status = cairo_surface_set_user_data (image->surface,
					      &image_data_cairo_key,
					      data,
					      (cairo_destroy_func_t) g_free);
	if (status) {
		fill_png_error (error, image_uri, NULL);
		gxps_image_free (image);
		g_free (data);
		return NULL;
	}

	return image;
#else
    return NULL;
#endif  /* HAVE_LIBPNG */
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

#ifdef GXPS_ENABLE_DEBUG
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
#endif

static void
_jpeg_init_source (j_decompress_ptr cinfo)
{
}

static boolean
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

static unsigned
read_uint16 (JOCTET  *data,
             gboolean is_big_endian)
{
        return is_big_endian ?
                (GETJOCTET (data[0]) << 8) | GETJOCTET (data[1]) :
                (GETJOCTET (data[1]) << 8) | GETJOCTET (data[0]);
}

static unsigned
read_uint32 (JOCTET  *data,
             gboolean is_big_endian)
{
        return is_big_endian ?
                (GETJOCTET (data[0]) << 24) | (GETJOCTET (data[1]) << 16) | (GETJOCTET (data[2]) << 8) | GETJOCTET (data[3]) :
                (GETJOCTET (data[3]) << 24) | (GETJOCTET (data[2]) << 16) | (GETJOCTET (data[1]) << 8) | GETJOCTET (data[0]);
}

static gboolean
_jpeg_read_exif_resolution (jpeg_saved_marker_ptr marker,
                            int                  *res_x,
                            int                  *res_y)
{
        gboolean is_big_endian;
        guint offset;
        JOCTET *ifd;
        JOCTET *end;
        guint ifd_length;
        guint i;
        guint res_type;
        gdouble x_res = 0;
        gdouble y_res = 0;

        /* Exif marker must be the first one */
        if (!(marker &&
              marker->marker == JPEG_APP0 + 1 &&
              marker->data_length >= 14 &&
              marker->data[0] == 'E' &&
              marker->data[1] == 'x' &&
              marker->data[2] == 'i' &&
              marker->data[3] == 'f' &&
              marker->data[4] == '\0' &&
              /* data[5] is a fill byte */
              ((marker->data[6] == 'I' &&
                marker->data[7] == 'I') ||
               (marker->data[6] == 'M' &&
                marker->data[7] == 'M'))))
                return FALSE;

        is_big_endian = marker->data[6] == 'M';
        if (read_uint16 (marker->data + 8, is_big_endian) != 42)
                return FALSE;

        offset = read_uint32 (marker->data + 10, is_big_endian) + 6;
        if (offset >= marker->data_length)
                return FALSE;

        ifd = marker->data + offset;
        end = marker->data + marker->data_length;
        if (end - ifd < 2)
                return FALSE;

        ifd_length = read_uint16 (ifd, is_big_endian);
        ifd += 2;
        for (i = 0; i < ifd_length && end - ifd >= 12; i++, ifd += 12) {
                guint tag, type, count;
                gint value_offset;

                tag = read_uint16 (ifd, is_big_endian);
                type = read_uint16 (ifd + 2, is_big_endian);
                count = read_uint32 (ifd + 4, is_big_endian);
                value_offset = read_uint32 (ifd + 8, is_big_endian) + 6;

                switch (tag) {
                case 0x11A:
                        if (type == 5 && value_offset > offset && value_offset <= marker->data_length - 8)
                                x_res = (gdouble)read_uint32 (marker->data + value_offset, is_big_endian) / read_uint32 (marker->data + value_offset + 4, is_big_endian);
                        break;
                case 0x11B:
                        if (type == 5 && value_offset > offset && value_offset <= marker->data_length - 8)
                                y_res = (gdouble)read_uint32 (marker->data + value_offset, is_big_endian) / read_uint32 (marker->data + value_offset + 4, is_big_endian);
                        break;
                case 0x128:
                        if (type == 3 && count == 1)
                                res_type = read_uint16 (ifd + 8, is_big_endian);
                        break;
                }
        }

        if (x_res <= 0 || y_res <= 0)
                return FALSE;

        switch (res_type) {
        case 2:
                *res_x = (int)x_res;
                *res_y = (int)y_res;
                break;
        case 3:
                *res_x = (int)(x_res * 254 / 100);
                *res_y = (int)(y_res * 254 / 100);
                break;
        default:
                *res_x = 0;
                *res_y = 0;
        }

        return TRUE;
}
#endif /* HAVE_LIBJPEG */

static GXPSImage *
gxps_images_create_from_jpeg (GXPSArchive *zip,
			      const gchar *image_uri,
			      GError     **error)
{
#ifdef HAVE_LIBJPEG
	GInputStream                 *stream;
	struct jpeg_error_mgr         error_mgr;
	struct jpeg_decompress_struct cinfo;
	struct _jpeg_src_mgr          src;
	GXPSImage                    *image;
	guchar                       *data;
	gint                          stride;
	JSAMPARRAY                    lines;
	gint                          jpeg_stride;
	gint                          i;
        int                           res_x, res_y;

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

        jpeg_save_markers (&cinfo, JPEG_APP0 + 1, 0xFFFF);

	jpeg_read_header (&cinfo, TRUE);

	cinfo.do_fancy_upsampling = FALSE;
	jpeg_start_decompress (&cinfo);

	image = g_slice_new (GXPSImage);
	image->surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
						     cinfo.output_width,
						     cinfo.output_height);
	image->res_x = 96;
	image->res_y = 96;
	if (cairo_surface_status (image->surface)) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_IMAGE,
			     "Error loading JPEG image %s: %s",
			     image_uri,
			     cairo_status_to_string (cairo_surface_status (image->surface)));
		jpeg_destroy_decompress (&cinfo);
		gxps_image_free (image);
		g_object_unref (stream);

		return NULL;
	}

	data = cairo_image_surface_get_data (image->surface);
	stride = cairo_image_surface_get_stride (image->surface);
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
                                case JCS_GRAYSCALE:
                                        p[0] = line[0];
                                        p[1] = line[0];
                                        p[2] = line[0];
                                        p[3] = 0xff;
                                        break;
                                case JCS_CMYK:
                                        p[0] = line[2] * line[3] / 255;
                                        p[1] = line[1] * line[3] / 255;
                                        p[2] = line[0] * line[3] / 255;
                                        p[3] = 0xff;
                                        break;
				default:
					GXPS_DEBUG (g_message ("Unsupported jpeg color space %s",
                                                               _jpeg_color_space_name (cinfo.out_color_space)));

					gxps_image_free (image);
					jpeg_destroy_decompress (&cinfo);
					g_object_unref (stream);
					return NULL;
				}
				line += cinfo.out_color_components;
				p += 4;
			}

			data += stride;
		}
	}

        if (_jpeg_read_exif_resolution (cinfo.marker_list, &res_x, &res_y)) {
                if (res_x > 0)
                        image->res_x = res_x;
                if (res_y > 0)
                        image->res_y = res_y;
        } else if (cinfo.density_unit == 1) { /* dots/inch */
		image->res_x = cinfo.X_density;
		image->res_y = cinfo.Y_density;
	} else if (cinfo.density_unit == 2) { /* dots/cm */
		image->res_x = cinfo.X_density * CENTIMETERS_PER_INCH;
		image->res_y = cinfo.Y_density * CENTIMETERS_PER_INCH;
	}

	jpeg_finish_decompress (&cinfo);
	jpeg_destroy_decompress (&cinfo);
	g_object_unref (stream);

	cairo_surface_mark_dirty (image->surface);

	if (cairo_surface_status (image->surface)) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_IMAGE,
			     "Error loading JPEG image %s: %s",
			     image_uri,
			     cairo_status_to_string (cairo_surface_status (image->surface)));
		gxps_image_free (image);

		return NULL;
	}

	return image;
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

G_GNUC_PRINTF (2, 0)
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

static GXPSImage *
gxps_images_create_from_tiff (GXPSArchive *zip,
			      const gchar *image_uri,
			      GError     **error)
{
#ifdef HAVE_LIBTIFF
	TIFF       *tiff;
	TiffBuffer  buffer;
	GXPSImage  *image;
	gint        width, height;
	guint16     res_unit;
	float       res_x, res_y;
	gint        stride;
	guchar     *data;
	guchar     *p;

        if (!gxps_archive_read_entry (zip, image_uri,
                                      &buffer.buffer,
                                      &buffer.buffer_len,
                                      error)) {
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

	image = g_slice_new (GXPSImage);
	image->surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
						     width, height);
	image->res_x = 96;
	image->res_y = 96;

	if (!TIFFGetField (tiff, TIFFTAG_RESOLUTIONUNIT, &res_unit))
		res_unit = 0;
	if (TIFFGetField (tiff, TIFFTAG_XRESOLUTION, &res_x)) {
		if (res_unit == 2) { /* inches */
			image->res_x = res_x;
		} else if (res_unit == 3) { /* centimeters */
			image->res_x = res_x * CENTIMETERS_PER_INCH;
		}
	}
	if (TIFFGetField (tiff, TIFFTAG_YRESOLUTION, &res_y)) {
		if (res_unit == 2) { /* inches */
			image->res_y = res_y;
		} else if (res_unit == 3) { /* centimeters */
			image->res_y = res_y * CENTIMETERS_PER_INCH;
		}
	}

	if (cairo_surface_status (image->surface)) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_IMAGE,
			     "Error loading TIFF image %s: %s",
			     image_uri,
			     cairo_status_to_string (cairo_surface_status (image->surface)));
		gxps_image_free (image);
		TIFFClose (tiff);
		_tiff_pop_handlers ();
		g_free (buffer.buffer);
		return NULL;
	}

	data = cairo_image_surface_get_data (image->surface);
	if (!TIFFReadRGBAImageOriented (tiff, width, height,
					(uint32 *)data,
					ORIENTATION_TOPLEFT, 1) || _tiff_error) {
		fill_tiff_error (error, image_uri);
		gxps_image_free (image);
		TIFFClose (tiff);
		_tiff_pop_handlers ();
		g_free (buffer.buffer);
		return NULL;
	}

	TIFFClose (tiff);
	_tiff_pop_handlers ();
	g_free (buffer.buffer);

	stride = cairo_image_surface_get_stride (image->surface);
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

	cairo_surface_mark_dirty (image->surface);

	return image;
#else
	return NULL;
#endif /* #ifdef HAVE_LIBTIFF */
}

#ifdef G_OS_WIN32
static GXPSImage *
image_create_from_byte_array (BYTE    *bytes,
                              int      width,
                              int      height,
                              UINT     buffer_size,
                              GError **error)
{
	int stride;
	guchar *data;
	GXPSImage  *image;
	cairo_status_t status;

	data = g_try_malloc (buffer_size);

	if (data == NULL) {
		g_set_error (error,
		             GXPS_ERROR,
		             GXPS_ERROR_IMAGE,
		             "Error allocating data buffer for cairo surface");
		return NULL;
	}

	memcpy (data, bytes, buffer_size);

	stride = cairo_format_stride_for_width (CAIRO_FORMAT_ARGB32, width);

	image = g_slice_new0 (GXPSImage);
	image->res_x = 96;
	image->res_y = 96;

	image->surface = cairo_image_surface_create_for_data (data, CAIRO_FORMAT_ARGB32, width, height, stride);
	if (cairo_surface_status (image->surface) != CAIRO_STATUS_SUCCESS) {
		g_set_error (error,
		             GXPS_ERROR,
		             GXPS_ERROR_IMAGE,
		             "Error creating cairo surface");
		gxps_image_free (image);
		g_free (data);
		return NULL;
	}

	status = cairo_surface_set_user_data (image->surface,
	                                      &image_data_cairo_key,
	                                      data,
	                                      (cairo_destroy_func_t) g_free);
	if (status) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_IMAGE,
			     "Error setting surface user data");
		gxps_image_free (image);
		g_free (data);
		return NULL;
	}

	return image;
}

static GXPSImage *
gxps_images_create_from_wdp (GXPSArchive *zip,
                             const gchar *image_uri,
                             GError     **error)
{
#define buffer_size 1024
	GInputStream  *stream;
	GXPSImage *image;
	IID iid_imaging_factory;
	HRESULT hr;
	IWICImagingFactory *image_factory;
	IWICBitmapDecoder *decoder;
	IWICBitmapFrameDecode *decoder_frame;
	IWICBitmap *bitmap;
	IWICBitmapLock *bitmap_lock;
	IStream *win_stream;
	UINT width;
	UINT height;
	guchar buffer[buffer_size];
	gsize read_bytes;
	gsize nwritten;
	UINT written_bytes;
	UINT bytes_size = 0;
	BYTE *bytes = NULL;
	WICRect rc_lock;

	stream = gxps_archive_open (zip, image_uri);
	if (!stream) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_SOURCE_NOT_FOUND,
			     "Image source %s not found in archive",
			     image_uri);
		return NULL;
	}

	/* Initialize COM. */
	hr = CoInitializeEx (NULL, COINIT_MULTITHREADED);
	if (!SUCCEEDED (hr)) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_IMAGE,
			     "Error initializing COM, hr code: %d",
		             HRESULT_CODE (hr));
		g_object_unref (stream);
		return NULL;
	} else if (hr == S_FALSE) {
		g_warning ("COM was already initialized");
	}

	/* Initialize IID IWICImagingFactory */
	IIDFromString (L"{ec5ec8a9-c395-4314-9c77-54d7a935ff70}",
		       &iid_imaging_factory);

	/* Create COM imaging factory. */
	hr = CoCreateInstance (&CLSID_WICImagingFactory,
		               NULL,
		               CLSCTX_INPROC_SERVER,
		               &iid_imaging_factory,
		               (LPVOID)&image_factory);

	if (!SUCCEEDED (hr)) {
		g_set_error (error,
		             GXPS_ERROR,
		             GXPS_ERROR_IMAGE,
		             "Error creating an instance of IWICImagingFactory, hr code: %d",
		             HRESULT_CODE (hr));
		g_object_unref (stream);
		CoUninitialize ();
		return NULL;
	}

	hr = CreateStreamOnHGlobal (NULL, TRUE, &win_stream);

	if (!SUCCEEDED (hr)) {
		g_set_error (error,
		             GXPS_ERROR,
		             GXPS_ERROR_IMAGE,
		             "Error allocating IStream, hr code: %d",
		             HRESULT_CODE (hr));
		IWICImagingFactory_Release (image_factory);
		g_object_unref (stream);
		CoUninitialize ();
		return NULL;
	}

	/* Write GInputStream data into IStream */
	do {
		read_bytes = g_input_stream_read (stream,
						  buffer,
						  sizeof (buffer),
						  NULL,
						  error);
		if (read_bytes < 0) {
			IWICImagingFactory_Release (image_factory);
			g_object_unref (stream);
			CoUninitialize ();
			return NULL;
		}

		nwritten = 0;

		while (nwritten < read_bytes) {
			IStream_Write (win_stream,
				       buffer + nwritten,
				       read_bytes - nwritten,
				       &written_bytes);
			nwritten += written_bytes;
		}

	} while (read_bytes > 0);

	g_object_unref (stream);

	hr = IWICImagingFactory_CreateDecoderFromStream (image_factory,
		                                         win_stream,
							 NULL,
							 WICDecodeMetadataCacheOnDemand,
							 &decoder);
	IStream_Release  (win_stream);

	if (!SUCCEEDED (hr)) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_IMAGE,
			     "Error creating decoder from stream, hr code: %d",
		             HRESULT_CODE (hr));
		IWICImagingFactory_Release (image_factory);
		CoUninitialize ();
		return NULL;
	}

	hr = IWICBitmapDecoder_GetFrame (decoder, 0, &decoder_frame);
	IWICBitmapDecoder_Release (decoder);

	if (!SUCCEEDED(hr)) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_IMAGE,
			     "Error getting frame, hr code: %d",
			     HRESULT_CODE (hr));
		IWICImagingFactory_Release (image_factory);
		CoUninitialize ();
		return NULL;
	}

	hr = IWICBitmapFrameDecode_GetSize (decoder_frame, &width, &height);

	if (!SUCCEEDED (hr)) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_IMAGE,
			     "Error getting image size, hr code: %d",
			     HRESULT_CODE (hr));
		IWICImagingFactory_Release (image_factory);
		IWICBitmapFrameDecode_Release (decoder_frame);
		CoUninitialize ();
		return NULL;
	}

	hr = IWICImagingFactory_CreateBitmapFromSource (image_factory,
						        (IWICBitmapSource *)decoder_frame,
						        WICBitmapCacheOnDemand,
						        &bitmap);
	IWICImagingFactory_Release (image_factory);
	IWICBitmapFrameDecode_Release (decoder_frame);

	if (!SUCCEEDED (hr)) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_IMAGE,
			     "Error creating bitmap, hr code: %d",
			     HRESULT_CODE (hr));
		CoUninitialize ();
		return NULL;
	}

	rc_lock.X = 0;
	rc_lock.Y = 0;
	rc_lock.Width = width;
	rc_lock.Height = height;

	hr = IWICBitmap_Lock (bitmap, &rc_lock, WICBitmapLockWrite, &bitmap_lock);
	IWICBitmap_Release (bitmap);

	if (!SUCCEEDED (hr)) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_IMAGE,
			     "Error locking bitmap, hr code: %d",
			     HRESULT_CODE (hr));
		CoUninitialize ();
		return NULL;
	}

	hr = IWICBitmapLock_GetDataPointer (bitmap_lock, &bytes_size, &bytes);

	if (!SUCCEEDED (hr)) {
		g_set_error (error,
			     GXPS_ERROR,
			     GXPS_ERROR_IMAGE,
			     "Error getting data pointer, hr code: %d",
		             HRESULT_CODE(hr));
		IWICBitmapLock_Release (bitmap_lock);
		CoUninitialize ();
		return NULL;
	}

	image = image_create_from_byte_array (bytes, width, height, bytes_size, error);

	IWICBitmapLock_Release (bitmap_lock);
	CoUninitialize ();

	return image;
}
#endif /* #ifdef G_OS_WIN32 */

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

GXPSImage *
gxps_images_get_image (GXPSArchive *zip,
		       const gchar *image_uri,
		       GError     **error)
{
	GXPSImage *image = NULL;
	gchar *image_uri_lower;

	/* First try with extensions,
	 * as it's recommended by the spec
	 * (2.1.5 Image Parts)
	 */
	image_uri_lower = g_utf8_strdown (image_uri, -1);
	if (g_str_has_suffix (image_uri_lower, ".png")) {
		image = gxps_images_create_from_png (zip, image_uri, error);
	} else if (g_str_has_suffix (image_uri_lower, ".jpg")) {
		image = gxps_images_create_from_jpeg (zip, image_uri, error);
	} else if (g_str_has_suffix (image_uri_lower, ".tif")) {
		image = gxps_images_create_from_tiff (zip, image_uri, error);
	} else if (g_str_has_suffix (image_uri_lower, "wdp")) {
#ifdef G_OS_WIN32
		image = gxps_images_create_from_wdp (zip, image_uri, error);
#else
		GXPS_DEBUG (g_message ("Unsupported image format windows media photo"));
		g_free (image_uri_lower);
		return NULL;
#endif
	}

	g_free (image_uri_lower);

	if (!image) {
		gchar *mime_type;

                g_clear_error(error);

		mime_type = gxps_images_guess_content_type (zip, image_uri);
		if (g_strcmp0 (mime_type, "image/png") == 0) {
			image = gxps_images_create_from_png (zip, image_uri, error);
		} else if (g_strcmp0 (mime_type, "image/jpeg") == 0) {
			image = gxps_images_create_from_jpeg (zip, image_uri, error);
		} else if (g_strcmp0 (mime_type, "image/tiff") == 0) {
			image = gxps_images_create_from_tiff (zip, image_uri, error);
		} else {
			GXPS_DEBUG (g_message ("Unsupported image format: %s", mime_type));
		}
		g_free (mime_type);
	}

	return image;
}

void
gxps_image_free (GXPSImage *image)
{
	if (G_UNLIKELY (!image))
		return;

	if (G_LIKELY (image->surface))
		cairo_surface_destroy (image->surface);

	g_slice_free (GXPSImage, image);
}
