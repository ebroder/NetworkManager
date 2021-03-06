/* NetworkManager -- Network link manager
 *
 * Dan Williams <dcbw@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2008 - 2011 Red Hat, Inc.
 */

#ifndef NM_GLIB_COMPAT_H
#define NM_GLIB_COMPAT_H

#include <glib.h>

#if !GLIB_CHECK_VERSION(2,31,0)
#define g_value_set_schar g_value_set_char
#define g_value_get_schar g_value_get_char
#endif

#endif  /* NM_GLIB_COMPAT_H */
