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

#include "gxps-debug.h"

#ifdef GXPS_ENABLE_DEBUG
gboolean gxps_debug_enabled (void)
{
        static gboolean debug_enabled = FALSE;
        static gsize initialized;

        if (g_once_init_enter (&initialized)) {
                debug_enabled = g_getenv ("GXPS_DEBUG") != NULL;

                g_once_init_leave (&initialized, 1);
        }

        return debug_enabled;
}
#endif
