/*
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

#ifndef __GXPS_DEBUG_H__
#define __GXPS_DEBUG_H__

#include <glib.h>

#include "gxps-version.h"

G_BEGIN_DECLS

#ifdef GXPS_ENABLE_DEBUG
GXPS_VAR gboolean gxps_debug_enabled (void);
#define GXPS_DEBUG(action) G_STMT_START {                \
                              if (gxps_debug_enabled ()) \
                                action;                  \
                           } G_STMT_END

#else
#define GXPS_DEBUG(action)
#endif

G_END_DECLS

#endif /* __GXPS_DEBUG_H__ */
