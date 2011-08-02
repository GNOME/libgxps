/* GXPSArchive
 *
 * Copyright (C) 2010 Carlos Garcia Campos <carlosgc@gnome.org>
 * Copyright (C) 2008 Benjamin Otte <otte@gnome.org>
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
#include <archive_entry.h>

#include "gxps-archive.h"

enum {
	PROP_0,
	PROP_FILE
};

struct _GXPSArchive {
	GObject parent;

	gboolean  initialized;
	GError   *init_error;
	GFile    *filename;
	GList    *entries;
};

struct _GXPSArchiveClass {
	GObjectClass parent_class;
};

static void initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (GXPSArchive, gxps_archive, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init))

#define BUFFER_SIZE 4096

/* Based on code from GVFS */
typedef struct {
	struct archive   *archive;
	GFile            *file;
	GFileInputStream *stream;
	guchar            buffer[BUFFER_SIZE];
	GError           *error;
} ZipArchive;

struct _GXPSArchiveEntry {
	ZipArchive           *zip;
	struct archive_entry *entry;
};

static int
_archive_open (struct archive *archive,
	       void           *data)
{
	ZipArchive *zip = (ZipArchive *)data;

	zip->stream = g_file_read (zip->file, NULL, &zip->error);

	return (zip->error) ? ARCHIVE_FATAL : ARCHIVE_OK;
}

static ssize_t
_archive_read (struct archive *archive,
	       void           *data,
	       const void    **buffer)
{
	ZipArchive *zip = (ZipArchive *)data;
	gssize read_bytes;

	*buffer = zip->buffer;
	read_bytes = g_input_stream_read (G_INPUT_STREAM (zip->stream),
					  zip->buffer,
					  sizeof (zip->buffer),
					  NULL,
					  &zip->error);
	return read_bytes;
}

static off_t
_archive_skip (struct archive *archive,
	       void           *data,
	       off_t           request)
{
	ZipArchive *zip = (ZipArchive *)data;

	if (!g_seekable_can_seek (G_SEEKABLE (zip->stream)))
		return 0;

	g_seekable_seek (G_SEEKABLE (zip->stream),
			 request,
			 G_SEEK_CUR,
			 NULL,
			 &zip->error);

	if (zip->error) {
		g_clear_error (&zip->error);
		request = 0;
	}

	return request;
}

static int
_archive_close (struct archive *archive,
		void *data)
{
	ZipArchive *zip = (ZipArchive *)data;

	if (zip->stream)
		g_object_unref (zip->stream);
	zip->stream = NULL;

	return ARCHIVE_OK;
}

static ZipArchive *
gxps_zip_archive_create (GFile *filename)
{
	ZipArchive *zip;

	zip = g_slice_new0 (ZipArchive);
	zip->file = filename;
	zip->archive = archive_read_new ();
	archive_read_support_format_zip (zip->archive);
	archive_read_open2 (zip->archive,
			    zip,
			    _archive_open,
			    _archive_read,
			    _archive_skip,
			    _archive_close);
	return zip;
}

static void
gxps_zip_archive_destroy (ZipArchive *zip)
{
	archive_read_finish (zip->archive);
	g_slice_free (ZipArchive, zip);
}

static void
gxps_archive_finalize (GObject *object)
{
	GXPSArchive *archive = GXPS_ARCHIVE (object);

	if (archive->entries) {
		g_list_foreach (archive->entries, (GFunc)g_free, NULL);
		g_list_free (archive->entries);
		archive->entries = NULL;
	}

	if (archive->filename) {
		g_object_unref (archive->filename);
		archive->filename = NULL;
	}

	g_clear_error (&archive->init_error);

	G_OBJECT_CLASS (gxps_archive_parent_class)->finalize (object);
}

static void
gxps_archive_init (GXPSArchive *archive)
{

}

static void
gxps_archive_set_property (GObject      *object,
			   guint         prop_id,
			   const GValue *value,
			   GParamSpec   *pspec)
{
	GXPSArchive *archive = GXPS_ARCHIVE (object);

	switch (prop_id) {
	case PROP_FILE:
		archive->filename = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gxps_archive_class_init (GXPSArchiveClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = gxps_archive_set_property;
	object_class->finalize = gxps_archive_finalize;

	g_object_class_install_property (object_class,
					 PROP_FILE,
					 g_param_spec_object ("file",
							      "File",
							      "The archive file",
							      G_TYPE_FILE,
							      G_PARAM_WRITABLE |
							      G_PARAM_CONSTRUCT_ONLY));
}

static gboolean
gxps_archive_initable_init (GInitable     *initable,
			    GCancellable  *cancellable,
			    GError       **error)
{
	GXPSArchive          *archive;
	ZipArchive           *zip;
	struct archive_entry *entry;
	gint                  result;

	archive = GXPS_ARCHIVE (initable);

	if (archive->initialized) {
		if (archive->init_error) {
			g_propagate_error (error, g_error_copy (archive->init_error));
			return FALSE;
		}

		return TRUE;
	}

	archive->initialized = TRUE;

	zip = gxps_zip_archive_create (archive->filename);
	if (zip->error) {
		g_propagate_error (&archive->init_error, zip->error);
		g_propagate_error (error, g_error_copy (archive->init_error));
		gxps_zip_archive_destroy (zip);
		return FALSE;
	}

	do {
		result = archive_read_next_header (zip->archive, &entry);
		if (result >= ARCHIVE_WARN && result <= ARCHIVE_OK) {
			if (result < ARCHIVE_OK) {
				g_print ("Error: %s\n", archive_error_string (zip->archive));
				archive_set_error (zip->archive, ARCHIVE_OK, "No error");
				archive_clear_error (zip->archive);
			}
			/* FIXME: We can ignore directories here */
			archive->entries = g_list_prepend (archive->entries,
							   g_strdup (archive_entry_pathname (entry)));
			archive_read_data_skip (zip->archive);
		}
	} while (result != ARCHIVE_FATAL && result != ARCHIVE_EOF);

	gxps_zip_archive_destroy (zip);

	return TRUE;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
	initable_iface->init = gxps_archive_initable_init;
}

GXPSArchive *
gxps_archive_new (GFile   *filename,
		  GError **error)
{
	return g_initable_new (GXPS_TYPE_ARCHIVE,
			       NULL, error,
			       "file", filename,
			       NULL);
}

gboolean
gxps_archive_has_entry (GXPSArchive *archive,
			const gchar *path)
{
	if (path && path[0] == '/')
		path++;

	return g_list_find_custom (archive->entries, path, (GCompareFunc)g_ascii_strcasecmp) != NULL;
}

/* GXPSArchiveInputStream */
typedef struct _GXPSArchiveInputStream {
	GInputStream          parent;

	ZipArchive           *zip;
	struct archive_entry *entry;
} GXPSArchiveInputStream;

typedef struct _GXPSArchiveInputStreamClass {
	GInputStreamClass parent_class;
} GXPSArchiveInputStreamClass;

static GType gxps_archive_input_stream_get_type (void) G_GNUC_CONST;

#define GXPS_TYPE_ARCHIVE_INPUT_STREAM (gxps_archive_input_stream_get_type())
#define GXPS_ARCHIVE_INPUT_STREAM(obj) (G_TYPE_CHECK_INSTANCE_CAST (obj, GXPS_TYPE_ARCHIVE_INPUT_STREAM, GXPSArchiveInputStream))

G_DEFINE_TYPE (GXPSArchiveInputStream, gxps_archive_input_stream, G_TYPE_INPUT_STREAM)

GInputStream *
gxps_archive_open (GXPSArchive *archive,
		   const gchar *path)
{
	GXPSArchiveInputStream *stream;
	gint                    result;

	if (path && path[0] == '/')
		path++;

	if (!g_list_find_custom (archive->entries, path, (GCompareFunc)g_ascii_strcasecmp))
		return NULL;

	stream = (GXPSArchiveInputStream *)g_object_new (GXPS_TYPE_ARCHIVE_INPUT_STREAM, NULL);

	stream->zip = gxps_zip_archive_create (archive->filename);
	do {
		result = archive_read_next_header (stream->zip->archive, &stream->entry);
		if (result >= ARCHIVE_WARN && result <= ARCHIVE_OK) {
			if (result < ARCHIVE_OK) {
				g_print ("Error: %s\n", archive_error_string (stream->zip->archive));
				archive_set_error (stream->zip->archive, ARCHIVE_OK, "No error");
				archive_clear_error (stream->zip->archive);
			}
			if (g_ascii_strcasecmp (path, archive_entry_pathname (stream->entry)) == 0)
				break;
			archive_read_data_skip (stream->zip->archive);
		}
	} while (result != ARCHIVE_FATAL && result != ARCHIVE_EOF);

	return G_INPUT_STREAM (stream);
}

gboolean
gxps_archive_read_entry (GXPSArchive *archive,
			 const gchar *path,
			 guchar     **buffer,
			 gsize       *bytes_read,
			 GError     **error)
{
	GInputStream *stream;
	gssize        entry_size;
	gboolean      retval;

	stream = gxps_archive_open (archive, path);
	if (!stream)
		/* TODO: Error */
		return FALSE;

	entry_size = archive_entry_size (GXPS_ARCHIVE_INPUT_STREAM (stream)->entry);
	if (entry_size <= 0) {
		gssize bytes;
		guchar buf[BUFFER_SIZE];
		guint  buffer_size = BUFFER_SIZE * 4;

		/* In some cases, I don't know why, archive_entry_size() returns 0,
		 * but the entry can be read, so let's try here.
		 */
		*bytes_read = 0;
		*buffer = g_malloc (buffer_size);
		do {
			bytes = g_input_stream_read (stream, &buf, BUFFER_SIZE, NULL, error);
			if (*error != NULL) {
				g_free (*buffer);
				g_object_unref (stream);

				return FALSE;
			}

			if (*bytes_read + bytes > buffer_size) {
				buffer_size += BUFFER_SIZE * 4;
				*buffer = g_realloc (*buffer, buffer_size);
			}
			memcpy (*buffer + *bytes_read, buf, bytes);
			*bytes_read += bytes;
		} while (bytes > 0);

		if (*bytes_read == 0) {
			/* TODO: Error */
			g_free (*buffer);
			g_object_unref (stream);
			return FALSE;
		}

		return TRUE;
	}

	*buffer = g_malloc (entry_size);
	retval = g_input_stream_read_all (stream,
					  *buffer, entry_size,
					  bytes_read, NULL,
					  error);
	if (!retval)
		g_free (*buffer);

	g_object_unref (stream);

	return retval;
}

static gssize
gxps_archive_input_stream_read (GInputStream  *stream,
				void          *buffer,
				gsize          count,
				GCancellable  *cancellable,
				GError       **error)
{
	GXPSArchiveInputStream *istream = GXPS_ARCHIVE_INPUT_STREAM (stream);

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return -1;
	return archive_read_data (istream->zip->archive, buffer, count);
}

static gssize
gxps_archive_input_stream_skip (GInputStream  *stream,
				gsize          count,
				GCancellable  *cancellable,
				GError       **error)
{
	return 0;
}

static gboolean
gxps_archive_input_stream_close (GInputStream  *stream,
				 GCancellable  *cancellable,
				 GError       **error)
{
	GXPSArchiveInputStream *istream = GXPS_ARCHIVE_INPUT_STREAM (stream);

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	if (istream->zip) {
		gxps_zip_archive_destroy (istream->zip);
		istream->zip = NULL;
	}

	return TRUE;
}

static void
gxps_archive_input_stream_finalize (GObject *object)
{
	GXPSArchiveInputStream *stream = GXPS_ARCHIVE_INPUT_STREAM (object);

	if (stream->zip) {
		gxps_zip_archive_destroy (stream->zip);
		stream->zip = NULL;
	}

	G_OBJECT_CLASS (gxps_archive_input_stream_parent_class)->finalize (object);
}

static void
gxps_archive_input_stream_init (GXPSArchiveInputStream *istream)
{
}

static void
gxps_archive_input_stream_class_init (GXPSArchiveInputStreamClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	GInputStreamClass *istream_class = G_INPUT_STREAM_CLASS (klass);

	object_class->finalize = gxps_archive_input_stream_finalize;

	istream_class->read_fn = gxps_archive_input_stream_read;
	istream_class->skip = gxps_archive_input_stream_skip;
	istream_class->close_fn = gxps_archive_input_stream_close;
}

