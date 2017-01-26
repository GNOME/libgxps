/* GXPSArchive
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

#ifndef __GXPS_ARCHIVE_H__
#define __GXPS_ARCHIVE_H__

#include <glib-object.h>
#include <gio/gio.h>
#include <archive.h>
#include <libgxps/gxps-version.h>
#include <libgxps/gxps-resources.h>

G_BEGIN_DECLS

#define GXPS_TYPE_ARCHIVE           (gxps_archive_get_type ())
#define GXPS_ARCHIVE(obj)           (G_TYPE_CHECK_INSTANCE_CAST (obj, GXPS_TYPE_ARCHIVE, GXPSArchive))
#define GXPS_ARCHIVE_CLASS(cls)     (G_TYPE_CHECK_CLASS_CAST (cls, GXPS_TYPE_ARCHIVE, GXPSArchiveClass))
#define GXPS_IS_ARCHIVE(obj)        (G_TYPE_CHECK_INSTANCE_TYPE (obj, GXPS_TYPE_ARCHIVE))
#define GXPS_IS_ARCHIVE_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE (obj, GXPS_TYPE_ARCHIVE))
#define GXPS_ARCHIVE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GXPS_TYPE_ARCHIVE, GXPSArchiveClass))

typedef struct _GXPSArchive      GXPSArchive;
typedef struct _GXPSArchiveClass GXPSArchiveClass;

GType             gxps_archive_get_type       (void) G_GNUC_CONST;
GXPSArchive      *gxps_archive_new            (GFile            *filename,
					       GError          **error);
gboolean          gxps_archive_has_entry      (GXPSArchive      *archive,
					       const gchar      *path);
GXPSResources    *gxps_archive_get_resources  (GXPSArchive      *archive);
GInputStream     *gxps_archive_open           (GXPSArchive      *archive,
					       const gchar      *path);
gboolean          gxps_archive_read_entry     (GXPSArchive      *archive,
					       const gchar      *path,
					       guchar          **buffer,
					       gsize            *bytes_read,
					       GError          **error);

G_END_DECLS

#endif /* __GXPS_ARCHIVE_H__ */
