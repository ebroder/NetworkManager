/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager system settings service
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
 * (C) Copyright 2008 Novell, Inc.
 * (C) Copyright 2008 - 2010 Red Hat, Inc.
 */

#ifndef NM_POLKIT_HELPERS_H
#define NM_POLKIT_HELPERS_H

#include <polkit/polkit.h>

/* Fix for polkit 0.97 and later */
#if !HAVE_POLKIT_AUTHORITY_GET_SYNC
static inline PolkitAuthority *
polkit_authority_get_sync (GCancellable *cancellable, GError **error)
{
	PolkitAuthority *authority;

	authority = polkit_authority_get ();
	if (!authority)
		g_set_error (error, 0, 0, "failed to get the PolicyKit authority");
	return authority;
}
#endif

#endif /* NM_POLKIT_HELPERS_H */