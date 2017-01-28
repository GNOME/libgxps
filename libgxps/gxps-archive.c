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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <archive_entry.h>

#include "gxps-archive.h"

enum {
	PROP_0,
	PROP_FILE
};

struct _GXPSArchive {
	GObject parent;

	gboolean    initialized;
	GError     *init_error;
	GFile      *filename;
	GHashTable *entries;
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

static int
_archive_open (struct archive *archive,
	       void           *data)
{
	ZipArchive *zip = (ZipArchive *)data;

	zip->stream = g_file_read (zip->file, NULL, &zip->error);

	return (zip->error) ? ARCHIVE_FATAL : ARCHIVE_OK;
}

static __LA_SSIZE_T
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

static __LA_INT64_T
_archive_skip (struct archive *archive,
	       void           *data,
	       __LA_INT64_T    request)
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

	g_clear_object (&zip->stream);

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

static gboolean
gxps_zip_archive_iter_next (ZipArchive            *zip,
                            struct archive_entry **entry)
{
        int result;

        result = archive_read_next_header (zip->archive, entry);
        if (result >= ARCHIVE_WARN && result <= ARCHIVE_OK) {
                if (result < ARCHIVE_OK) {
                        g_warning ("Error: %s\n", archive_error_string (zip->archive));
                        archive_set_error (zip->archive, ARCHIVE_OK, "No error");
                        archive_clear_error (zip->archive);
                }

                return TRUE;
        }

        return result != ARCHIVE_FATAL && result != ARCHIVE_EOF;
}

static void
gxps_zip_archive_destroy (ZipArchive *zip)
{
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	/* This is a deprecated synonym for archive_read_free() in libarchive
	 * 3.0; but is not deprecated in libarchive 2.0, which we continue to
	 * support. */
	archive_read_finish (zip->archive);
G_GNUC_END_IGNORE_DEPRECATIONS
	g_slice_free (ZipArchive, zip);
}

static void
gxps_archive_finalize (GObject *object)
{
	GXPSArchive *archive = GXPS_ARCHIVE (object);

	g_clear_pointer (&archive->entries, g_hash_table_unref);
	g_clear_object (&archive->filename);
	g_clear_error (&archive->init_error);

	G_OBJECT_CLASS (gxps_archive_parent_class)->finalize (object);
}

static guint
caseless_hash (gconstpointer v)
{
	gchar *lower;
	guint ret;

	lower = g_ascii_strdown (v, -1);
	ret = g_str_hash (lower);
	g_free (lower);

	return ret;
}

static gboolean
caseless_equal (gconstpointer v1,
                gconstpointer v2)
{
	return g_ascii_strcasecmp (v1, v2) == 0;
}

static void
gxps_archive_init (GXPSArchive *archive)
{
	archive->entries = g_hash_table_new_full (caseless_hash, caseless_equal, g_free, NULL);
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

        while (gxps_zip_archive_iter_next (zip, &entry)) {
                /* FIXME: We can ignore directories here */
                g_hash_table_add (archive->entries, g_strdup (archive_entry_pathname (entry)));
                archive_read_data_skip (zip->archive);
        }

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
	if (path == NULL)
		return FALSE;

	if (path[0] == '/')
		path++;

	return g_hash_table_contains (archive->entries, path);
}

/* GXPSArchiveInputStream */
typedef struct _GXPSArchiveInputStream {
	GInputStream          parent;

	ZipArchive           *zip;
        gboolean              is_interleaved;
        guint                 piece;
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
	gchar                  *first_piece_path = NULL;

	if (path == NULL)
		return NULL;

	if (path[0] == '/')
		path++;

	if (!g_hash_table_contains (archive->entries, path)) {
                first_piece_path = g_build_path ("/", path, "[0].piece", NULL);
                if (!g_hash_table_contains (archive->entries, first_piece_path)) {
                        g_free (first_piece_path);

                        return NULL;
                }
                path = first_piece_path;
        }

	stream = (GXPSArchiveInputStream *)g_object_new (GXPS_TYPE_ARCHIVE_INPUT_STREAM, NULL);
	stream->zip = gxps_zip_archive_create (archive->filename);
        stream->is_interleaved = first_piece_path != NULL;

        while (gxps_zip_archive_iter_next (stream->zip, &stream->entry)) {
                if (g_ascii_strcasecmp (path, archive_entry_pathname (stream->entry)) == 0)
                        break;
                archive_read_data_skip (stream->zip->archive);
        }

        g_free (first_piece_path);

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

		g_object_unref (stream);

		if (*bytes_read == 0) {
			/* TODO: Error */
			g_free (*buffer);
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

static gboolean
gxps_archive_input_stream_is_last_piece (GXPSArchiveInputStream *stream)
{
        return g_str_has_suffix (archive_entry_pathname (stream->entry), ".last.piece");
}

static void
gxps_archive_input_stream_next_piece (GXPSArchiveInputStream *stream)
{
        gchar *dirname;
        gchar *prefix;

        if (!stream->is_interleaved)
                return;

        dirname = g_path_get_dirname (archive_entry_pathname (stream->entry));
        if (!dirname)
                return;

        stream->piece++;
        prefix = g_strdup_printf ("%s/[%u]", dirname, stream->piece);
        g_free (dirname);

        while (gxps_zip_archive_iter_next (stream->zip, &stream->entry)) {
                if (g_str_has_prefix (archive_entry_pathname (stream->entry), prefix)) {
                        const gchar *suffix = archive_entry_pathname (stream->entry) + strlen (prefix);

                        if (g_ascii_strcasecmp (suffix, ".piece") == 0 ||
                            g_ascii_strcasecmp (suffix, ".last.piece") == 0)
                                break;
                }
                archive_read_data_skip (stream->zip->archive);
        }

        g_free (prefix);
}

static gssize
gxps_archive_input_stream_read (GInputStream  *stream,
				void          *buffer,
				gsize          count,
				GCancellable  *cancellable,
				GError       **error)
{
	GXPSArchiveInputStream *istream = GXPS_ARCHIVE_INPUT_STREAM (stream);
        gssize                  bytes_read;

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return -1;

        bytes_read = archive_read_data (istream->zip->archive, buffer, count);
        if (bytes_read == 0 && istream->is_interleaved && !gxps_archive_input_stream_is_last_piece (istream)) {
                /* Read next piece */
                gxps_archive_input_stream_next_piece (istream);
                bytes_read = gxps_archive_input_stream_read (stream, buffer, count, cancellable, error);
        }

	return bytes_read;
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

	g_clear_pointer (&istream->zip, gxps_zip_archive_destroy);

	return TRUE;
}

static void
gxps_archive_input_stream_finalize (GObject *object)
{
	GXPSArchiveInputStream *stream = GXPS_ARCHIVE_INPUT_STREAM (object);

	g_clear_pointer (&stream->zip, gxps_zip_archive_destroy);

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

