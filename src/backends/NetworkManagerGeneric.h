/* NetworkManager -- Network link manager
 *
 * Timothee Lecomte <timothee.lecomte@ens.fr>
 *
 * Heavily based on NetworkManagerSystem.h by Dan Williams <dcbw@redhat.com>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2004 Red Hat, Inc.
 */

#ifndef NETWORK_MANAGER_GENERIC_H
#define NETWORK_MANAGER_GENERIC_H

#include <glib.h>
#include "nm-device.h"
#include "nm-ip4-config.h"
#include "nm-named-manager.h"

/* Prototypes for system/distribution dependent functions,
 * implemented in the backend files in backends/ directory
 */

void			nm_generic_init (void);
gboolean		nm_generic_device_has_active_routes			(NMDevice *dev);

void			nm_generic_device_flush_routes				(NMDevice *dev);
void			nm_generic_device_flush_routes_with_iface	(const char *iface);

void            nm_generic_device_replace_default_route (const char *iface, guint32 gw, guint32 mss);

void			nm_generic_device_add_route_via_device_with_iface (const char *iface, const char *route);

void			nm_generic_device_flush_addresses			(NMDevice *dev);
void			nm_generic_device_flush_addresses_with_iface	(const char *iface);

void			nm_generic_enable_loopback				(void);
void			nm_generic_flush_loopback_routes			(void);
void			nm_generic_flush_arp_cache				(void);
void			nm_generic_kill_all_dhcp_daemons			(void);
void			nm_generic_update_dns					(void);
void			nm_generic_restart_mdns_responder			(void);
void			nm_generic_device_add_ip6_link_address 		(NMDevice *dev);

void			nm_generic_set_ip4_config_from_resolv_conf (const char *filename, NMIP4Config *ip4_config);
void *		nm_generic_device_get_system_config			(NMDevice *dev);
void			nm_generic_device_free_system_config		(NMDevice *dev, void *system_config_data);
NMIP4Config *	nm_generic_device_new_ip4_system_config		(NMDevice *dev);

gboolean		nm_generic_device_get_disabled				(NMDevice *dev);

gboolean		nm_generic_device_set_from_ip4_config		(NMDevice *dev);
gboolean		nm_generic_vpn_device_set_from_ip4_config	(NMNamedManager *named, NMDevice *active_device, const char *iface, NMIP4Config *config, char **routes, int num_routes);
gboolean		nm_generic_vpn_device_unset_from_ip4_config	(NMNamedManager *named, NMDevice *active_device, const char *iface, NMIP4Config *config);

gboolean		nm_generic_device_set_up_down				(NMDevice *dev, gboolean up);
gboolean		nm_generic_device_set_up_down_with_iface		(NMDevice *dev, const char *iface, gboolean up);

gboolean		nm_generic_device_update_resolv_conf		(void *data, int len, const char *domain_name);

void			nm_generic_set_hostname (NMIP4Config *config);
void			nm_generic_activate_nis (NMIP4Config *config);
void			nm_generic_shutdown_nis (void);

gboolean		nm_generic_should_modify_resolv_conf (void);

#endif