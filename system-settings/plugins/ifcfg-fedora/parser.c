/* NetworkManager system settings service
 *
 * Søren Sandmann <sandmann@daimi.au.dk>
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
 * (C) Copyright 2007 Red Hat, Inc.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <sys/inotify.h>
#include <errno.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef __user
#define __user
#endif
#include <linux/types.h>
#include <wireless.h>
#undef __user

#include <glib.h>

#include <nm-connection.h>
#include <NetworkManager.h>
#include <nm-setting-connection.h>
#include <nm-setting-ip4-config.h>
#include <nm-setting-wired.h>
#include <nm-setting-wireless.h>
#include <nm-utils.h>

#include "shvar.h"
#include "parser.h"
#include "plugin.h"

#define CONNECTION_DATA_TAG "plugin-data"

ConnectionData *
connection_data_get (NMConnection *connection)
{
	return (ConnectionData *) g_object_get_data (G_OBJECT (connection), CONNECTION_DATA_TAG);
}

static void
copy_one_cdata_secret (gpointer key, gpointer data, gpointer user_data)
{
	GHashTable *to = (GHashTable *) user_data;

	g_hash_table_insert (to, key, g_strdup (data));
}

static void
clear_one_cdata_secret (gpointer key, gpointer data, gpointer user_data)
{
	guint32 len = strlen (data);
	memset (data, 0, len);
}

void
connection_data_copy_secrets (ConnectionData *from, ConnectionData *to)
{
	g_return_if_fail (from != NULL);
	g_return_if_fail (to != NULL);

	g_hash_table_foreach (to->wifi_secrets, clear_one_cdata_secret, NULL);
	g_hash_table_remove_all (to->wifi_secrets);
	g_hash_table_foreach (from->wifi_secrets, copy_one_cdata_secret, to->wifi_secrets);

	g_hash_table_foreach (to->onex_secrets, clear_one_cdata_secret, NULL);
	g_hash_table_remove_all (to->onex_secrets);
	g_hash_table_foreach (from->onex_secrets, copy_one_cdata_secret, to->onex_secrets);

	g_hash_table_foreach (to->ppp_secrets, clear_one_cdata_secret, NULL);
	g_hash_table_remove_all (to->ppp_secrets);
	g_hash_table_foreach (from->ppp_secrets, copy_one_cdata_secret, to->ppp_secrets);
}

static void
connection_data_free (gpointer userdata)
{
	ConnectionData *cdata = (ConnectionData *) userdata;

	g_return_if_fail (cdata != NULL);

	g_hash_table_foreach (cdata->wifi_secrets, clear_one_cdata_secret, NULL);
	g_hash_table_destroy (cdata->wifi_secrets);

	g_hash_table_foreach (cdata->onex_secrets, clear_one_cdata_secret, NULL);
	g_hash_table_destroy (cdata->onex_secrets);

	g_hash_table_foreach (cdata->ppp_secrets, clear_one_cdata_secret, NULL);
	g_hash_table_destroy (cdata->ppp_secrets);

	g_free (cdata->ifcfg_path);
	memset (cdata, 0, sizeof (ConnectionData));
	g_free (cdata);
}

ConnectionData *
connection_data_add (NMConnection *connection, const char *ifcfg_path)
{
	ConnectionData *cdata;

	cdata = g_malloc0 (sizeof (ConnectionData));
	cdata->ifcfg_path = g_strdup (ifcfg_path);

	cdata->wifi_secrets = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
	cdata->onex_secrets = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
	cdata->ppp_secrets = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);

	g_object_set_data_full (G_OBJECT (connection),
	                        CONNECTION_DATA_TAG, cdata,
	                        connection_data_free);
	return cdata;
}

static gboolean
get_int (const char *str, int *value)
{
	char *e;

	*value = strtol (str, &e, 0);
	if (*e != '\0')
		return FALSE;

	return TRUE;
}

static gboolean
should_ignore_file (const char *basename, const char *tag)
{
	int len, tag_len;

	g_return_val_if_fail (basename != NULL, TRUE);
	g_return_val_if_fail (tag != NULL, TRUE);

	len = strlen (basename);
	tag_len = strlen (tag);
	if ((len > tag_len) && !strcasecmp (basename + len - tag_len, tag))
		return TRUE;
	return FALSE;
}

static char *
get_ifcfg_name (const char *file)
{
	char *ifcfg_name = NULL;
	char *basename;
	int len;

	basename = g_path_get_basename (file);
	if (!basename)
		goto error;
	len = strlen (basename);

	if (len < strlen (IFCFG_TAG) + 1)
		goto error;

	if (strncmp (basename, IFCFG_TAG, strlen (IFCFG_TAG)))
		goto error;

	/* ignore some files */
	if (should_ignore_file (basename, BAK_TAG))
		goto error;
	if (should_ignore_file (basename, TILDE_TAG))
		goto error;
	if (should_ignore_file (basename, ORIG_TAG))
		goto error;
	if (should_ignore_file (basename, REJ_TAG))
		goto error;

	ifcfg_name = g_strdup (basename + strlen (IFCFG_TAG));

error:
	g_free (basename);
	return ifcfg_name;
}

static NMSetting *
make_connection_setting (const char *file,
                         shvarFile *ifcfg,
                         const char *type,
                         const char *suggested)
{
	NMSettingConnection *s_con;
	char *ifcfg_name = NULL;

	ifcfg_name = get_ifcfg_name (file);
	if (!ifcfg_name)
		return NULL;

	s_con = NM_SETTING_CONNECTION (nm_setting_connection_new ());

 	if (suggested) {
		/* For cosmetic reasons, if the suggested name is the same as
		 * the ifcfg files name, don't use it.
		 */
		if (strcmp (ifcfg_name, suggested))
			s_con->id = g_strdup_printf ("System %s (%s)", suggested, ifcfg_name);
	}

	if (!s_con->id)
		s_con->id = g_strdup_printf ("System %s", ifcfg_name);

	s_con->type = g_strdup (type);

	/* Be somewhat conservative about autoconnect */
	if (svTrueValue (ifcfg, "ONBOOT", FALSE))
		s_con->autoconnect = TRUE;

	g_free (ifcfg_name);
	return (NMSetting *) s_con;
}

static void
get_one_ip4_addr (shvarFile *ifcfg,
                  const char *tag,
                  guint32 *out_addr,
                  GError **error)
{
	char *value = NULL;
	struct in_addr ip4_addr;

	g_return_if_fail (ifcfg != NULL);
	g_return_if_fail (tag != NULL);
	g_return_if_fail (out_addr != NULL);
	g_return_if_fail (error != NULL);
	g_return_if_fail (*error == NULL);

	value = svGetValue (ifcfg, tag);
	if (!value)
		return;

	if (inet_pton (AF_INET, value, &ip4_addr))
		*out_addr = ip4_addr.s_addr;
	else {
		g_set_error (error, ifcfg_plugin_error_quark (), 0,
		             "Invalid %s IP4 address '%s'", tag, value);
	}
	g_free (value);
}

static NMSetting *
make_ip4_setting (shvarFile *ifcfg, GError **error)
{
	NMSettingIP4Config *s_ip4 = NULL;
	char *value = NULL;
	NMSettingIP4Address tmp = { 0, 0, 0 };
	char *method = NM_SETTING_IP4_CONFIG_METHOD_MANUAL;
	guint32 dns;

	value = svGetValue (ifcfg, "BOOTPROTO");
	if (value && (!g_ascii_strcasecmp (value, "bootp") || !g_ascii_strcasecmp (value, "dhcp")))
		method = NM_SETTING_IP4_CONFIG_METHOD_DHCP;

	if (value && !g_ascii_strcasecmp (value, "autoip")) {
		method = NM_SETTING_IP4_CONFIG_METHOD_AUTOIP;
		goto done;
	}

	get_one_ip4_addr (ifcfg, "IPADDR", &tmp.address, error);
	if (*error)
		goto error;

	get_one_ip4_addr (ifcfg, "GATEWAY", &tmp.gateway, error);
	if (*error)
		goto error;

	get_one_ip4_addr (ifcfg, "NETMASK", &tmp.netmask, error);
	if (*error)
		goto error;


done:
	s_ip4 = (NMSettingIP4Config *) nm_setting_ip4_config_new ();
	s_ip4->method = g_strdup (method);
	if (tmp.address || tmp.netmask || tmp.gateway) {
		NMSettingIP4Address *addr;
		addr = g_new0 (NMSettingIP4Address, 1);
		memcpy (addr, &tmp, sizeof (NMSettingIP4Address));
		s_ip4->addresses = g_slist_append (s_ip4->addresses, addr);
	}

	/* No DNS for autoip */
	if (g_ascii_strcasecmp (method, NM_SETTING_IP4_CONFIG_METHOD_AUTOIP)) {
		s_ip4->dns = g_array_sized_new (FALSE, TRUE, sizeof (guint32), 3);

		get_one_ip4_addr (ifcfg, "DNS1", &dns, error);
		if (*error)
			goto error;
		g_array_append_val (s_ip4->dns, dns);

		get_one_ip4_addr (ifcfg, "DNS2", &dns, error);
		if (*error)
			goto error;
		g_array_append_val (s_ip4->dns, dns);

		get_one_ip4_addr (ifcfg, "DNS3", &dns, error);
		if (*error)
			goto error;
		g_array_append_val (s_ip4->dns, dns);

		if (s_ip4->dns && !s_ip4->dns->len) {
			g_array_free (s_ip4->dns, TRUE);
			s_ip4->dns = NULL;
		}

		/* DNS searches */
		value = svGetValue (ifcfg, "SEARCH");
		if (value) {
			char **searches = NULL;

			searches = g_strsplit (value, " ", 0);
			if (searches) {
				char **item;
				for (item = searches; *item; item++)
					s_ip4->dns_search = g_slist_append (s_ip4->dns_search, *item);
				g_free (searches);
			}
		}
	}

	return NM_SETTING (s_ip4);

error:
	g_free (value);
	if (s_ip4)
		g_object_unref (s_ip4);
	return NULL;
}

/*
 * utils_bin2hexstr
 *
 * Convert a byte-array into a hexadecimal string.
 *
 * Code originally by Alex Larsson <alexl@redhat.com> and
 *  copyright Red Hat, Inc. under terms of the LGPL.
 *
 */
static char *
utils_bin2hexstr (const char *bytes, int len, int final_len)
{
	static char	hex_digits[] = "0123456789abcdef";
	char *		result;
	int			i;

	g_return_val_if_fail (bytes != NULL, NULL);
	g_return_val_if_fail (len > 0, NULL);
	g_return_val_if_fail (len < 256, NULL);	/* Arbitrary limit */

	result = g_malloc0 (len * 2 + 1);
	for (i = 0; i < len; i++)
	{
		result[2*i] = hex_digits[(bytes[i] >> 4) & 0xf];
		result[2*i+1] = hex_digits[bytes[i] & 0xf];
	}
	/* Cut converted key off at the correct length for this cipher type */
	if (final_len > -1)
		result[final_len] = '\0';

	return result;
}


static char *
get_one_wep_key (shvarFile *ifcfg, guint8 idx, GError **error)
{
	char *shvar_key;
	char *key = NULL;
	char *value = NULL;

	g_return_val_if_fail (idx <= 3, NULL);

	shvar_key = g_strdup_printf ("KEY%d", idx);
	value = svGetValue (ifcfg, shvar_key);
	if (!value || !strlen (value))
		goto out;

	/* Validate keys */
	if (strlen (value) == 10 || strlen (value) == 26) {
		/* Hexadecimal WEP key */
		char *p = value;

		while (*p) {
			if (!g_ascii_isxdigit (*p)) {
				g_set_error (error, ifcfg_plugin_error_quark (), 0,
				             "Invalid hexadecimal WEP key.");
				goto out;
			}
			p++;
		}
		key = g_strdup (value);
	} else if (strlen (value) == 5 || strlen (value) == 13) {
		/* ASCII passphrase */
		char *p = value;

		while (*p) {
			if (!isascii ((int) (*p))) {
				g_set_error (error, ifcfg_plugin_error_quark (), 0,
				             "Invalid ASCII WEP passphrase.");
				goto out;
			}
			p++;
		}

		value = utils_bin2hexstr (value, strlen (value), strlen (value) * 2);
	} else {
		g_set_error (error, ifcfg_plugin_error_quark (), 0, "Invalid WEP key length.");
	}

out:
	g_free (value);
	g_free (shvar_key);
	return key;
}

#define READ_WEP_KEY(idx, ifcfg_file, cdata) \
	{ \
		char *key = get_one_wep_key (ifcfg_file, idx, error); \
		if (*error) \
			goto error; \
		if (key) { \
			g_hash_table_insert (cdata->wifi_secrets, \
			                     NM_SETTING_WIRELESS_SECURITY_WEP_KEY##idx, \
			                     key); \
		} \
	}

static shvarFile *
get_keys_ifcfg (const char *parent)
{
	char *ifcfg_name;
	char *keys_file = NULL;
	char *tmp = NULL;
	shvarFile *ifcfg = NULL;

	ifcfg_name = get_ifcfg_name (parent);
	if (!ifcfg_name)
		return NULL;

	tmp = g_path_get_dirname (parent);
	if (!tmp)
		goto out;

	keys_file = g_strdup_printf ("%s/" KEYS_TAG "%s", tmp, ifcfg_name);
	if (!keys_file)
		goto out;

	ifcfg = svNewFile (keys_file);

out:
	g_free (keys_file);
	g_free (tmp);
	g_free (ifcfg_name);
	return ifcfg;
}

static NMSetting *
make_wireless_security_setting (shvarFile *ifcfg,
                                const char *file,
                                ConnectionData *cdata,
                                GError **error)
{
	NMSettingWirelessSecurity *s_wireless_sec;
	char *value;
	shvarFile *keys_ifcfg = NULL;

	s_wireless_sec = NM_SETTING_WIRELESS_SECURITY (nm_setting_wireless_security_new ());

	READ_WEP_KEY(0, ifcfg, cdata)
	READ_WEP_KEY(1, ifcfg, cdata)
	READ_WEP_KEY(2, ifcfg, cdata)
	READ_WEP_KEY(3, ifcfg, cdata)

	/* Try to get keys from the "shadow" key file */
	keys_ifcfg = get_keys_ifcfg (file);
	if (keys_ifcfg) {
		READ_WEP_KEY(0, keys_ifcfg, cdata)
		READ_WEP_KEY(1, keys_ifcfg, cdata)
		READ_WEP_KEY(2, keys_ifcfg, cdata)
		READ_WEP_KEY(3, keys_ifcfg, cdata)
	}

	value = svGetValue (ifcfg, "DEFAULTKEY");
	if (value) {
		gboolean success;
		int key_idx = 0;

		success = get_int (value, &key_idx);
		if (success && (key_idx >= 0) && (key_idx <= 3))
			s_wireless_sec->wep_tx_keyidx = key_idx;
		else {
			g_set_error (error, ifcfg_plugin_error_quark (), 0,
			             "Invalid default WEP key '%s'", value);
	 		g_free (value);
			goto error;
		}
 		g_free (value);
	}

	value = svGetValue (ifcfg, "SECURITYMODE");
	if (value) {
		char *lcase;

		lcase = g_ascii_strdown (value, -1);
		g_free (value);

		if (!strcmp (lcase, "open")) {
			s_wireless_sec->auth_alg = g_strdup ("open");
		} else if (!strcmp (lcase, "restricted")) {
			s_wireless_sec->auth_alg = g_strdup ("shared");
		} else {
			g_set_error (error, ifcfg_plugin_error_quark (), 0,
			             "Invalid WEP authentication algoritm '%s'",
			             lcase);
			g_free (lcase);
			goto error;
		}
		g_free (lcase);
	}

	// FIXME: unencrypted and WEP-only for now
	s_wireless_sec->key_mgmt = g_strdup ("none");

	if (keys_ifcfg)
		svCloseFile (keys_ifcfg);

	return NM_SETTING (s_wireless_sec);

error:
	if (s_wireless_sec)
		g_object_unref (s_wireless_sec);
	if (keys_ifcfg)
		svCloseFile (keys_ifcfg);
	return NULL;
}


static NMSetting *
make_wireless_setting (shvarFile *ifcfg,
                       NMSetting *security,
                       GError **error)
{
	NMSettingWireless *s_wireless;
	char *value;

	s_wireless = NM_SETTING_WIRELESS (nm_setting_wireless_new ());

	value = svGetValue (ifcfg, "ESSID");
	if (value) {
		gsize len = strlen (value);

		if (len > 32 || len == 0) {
			g_set_error (error, ifcfg_plugin_error_quark (), 0,
			             "Invalid SSID '%s' (size %zu not between 1 and 32 inclusive)",
			             value, len);
			goto error;
		}

		s_wireless->ssid = g_byte_array_sized_new (strlen (value));
		g_byte_array_append (s_wireless->ssid, (const guint8 *) value, len);
		g_free (value);
	} else {
		g_set_error (error, ifcfg_plugin_error_quark (), 0, "Missing SSID");
		goto error;
	}

	value = svGetValue (ifcfg, "MODE");
	if (value) {
		char *lcase;

		lcase = g_ascii_strdown (value, -1);
		g_free (value);

		if (!strcmp (lcase, "ad-hoc")) {
			s_wireless->mode = g_strdup ("adhoc");
		} else if (!strcmp (lcase, "managed")) {
			s_wireless->mode = g_strdup ("infrastructure");
		} else {
			g_set_error (error, ifcfg_plugin_error_quark (), 0,
			             "Invalid mode '%s' (not ad-hoc or managed)",
			             lcase);
			g_free (lcase);
			goto error;
		}
		g_free (lcase);
	}

	if (security)
		s_wireless->security = g_strdup (NM_SETTING_WIRELESS_SECURITY_SETTING_NAME);

	// FIXME: channel/freq, other L2 parameters like RTS

	return NM_SETTING (s_wireless);

error:
	if (s_wireless)
		g_object_unref (s_wireless);
	return NULL;
}

static NMConnection *
wireless_connection_from_ifcfg (const char *file, shvarFile *ifcfg, GError **error)
{
	NMConnection *connection = NULL;
	NMSetting *con_setting = NULL;
	NMSetting *wireless_setting = NULL;
	NMSettingWireless *tmp;
	NMSetting *security_setting = NULL;
	char *printable_ssid = NULL;
	ConnectionData *cdata;

	g_return_val_if_fail (file != NULL, NULL);
	g_return_val_if_fail (ifcfg != NULL, NULL);
	g_return_val_if_fail (error != NULL, NULL);
	g_return_val_if_fail (*error == NULL, NULL);

	connection = nm_connection_new ();
	if (!connection) {
		g_set_error (error, ifcfg_plugin_error_quark (), 0,
		             "Failed to allocate new connection for %s.", file);
		return NULL;
	}

	/* Add plugin specific data to connection */
	cdata = connection_data_add (connection, file);

	/* Wireless security */
	security_setting = make_wireless_security_setting (ifcfg, file, cdata, error);
	if (*error)
		goto error;
	if (security_setting)
		nm_connection_add_setting (connection, security_setting);

	/* Wireless */
	wireless_setting = make_wireless_setting (ifcfg, security_setting, error);
	if (!wireless_setting)
		goto error;

	nm_connection_add_setting (connection, wireless_setting);

	tmp = NM_SETTING_WIRELESS (wireless_setting);
	printable_ssid = nm_utils_ssid_to_utf8 ((const char *) tmp->ssid->data,
	                                        (guint32) tmp->ssid->len);

	con_setting = make_connection_setting (file, ifcfg,
	                                       NM_SETTING_WIRELESS_SETTING_NAME,
	                                       printable_ssid);
	if (!con_setting) {
		g_set_error (error, ifcfg_plugin_error_quark (), 0,
		             "Failed to create connection setting.");
		goto error;
	}
	nm_connection_add_setting (connection, con_setting);

	if (!nm_connection_verify (connection)) {
		g_set_error (error, ifcfg_plugin_error_quark (), 0,
		             "Connection from %s was invalid.", file);
		goto error;
	}

	return connection;

error:
	g_free (printable_ssid);
	g_object_unref (connection);
	if (con_setting)
		g_object_unref (con_setting);
	if (wireless_setting)
		g_object_unref (wireless_setting);
	return NULL;
}

static NMSetting *
make_wired_setting (shvarFile *ifcfg, GError **error)
{
	NMSettingWired *s_wired;
	char *value;
	int mtu;

	s_wired = NM_SETTING_WIRED (nm_setting_wired_new ());

	value = svGetValue (ifcfg, "MTU");
	if (value) {
		if (get_int (value, &mtu)) {
			if (mtu >= 0 && mtu < 65536)
				s_wired->mtu = mtu;
		} else {
			g_set_error (error, ifcfg_plugin_error_quark (), 0,
			             "Invalid MTU '%s'", value);
			g_object_unref (s_wired);
			s_wired = NULL;
		}
		g_free (value);
	}

	return (NMSetting *) s_wired;
}

static NMConnection *
wired_connection_from_ifcfg (const char *file, shvarFile *ifcfg, GError **error)
{
	NMConnection *connection = NULL;
	NMSetting *con_setting = NULL;
	NMSetting *wired_setting = NULL;
	ConnectionData *cdata;

	g_return_val_if_fail (file != NULL, NULL);
	g_return_val_if_fail (ifcfg != NULL, NULL);

	connection = nm_connection_new ();
	if (!connection) {
		g_set_error (error, ifcfg_plugin_error_quark (), 0,
		             "Failed to allocate new connection for %s.", file);
		return NULL;
	}

	con_setting = make_connection_setting (file, ifcfg, NM_SETTING_WIRED_SETTING_NAME, NULL);
	if (!con_setting) {
		g_set_error (error, ifcfg_plugin_error_quark (), 0,
		             "Failed to create connection setting.");
		goto error;
	}
	nm_connection_add_setting (connection, con_setting);

	/* Add plugin data to connection */
	cdata = connection_data_add (connection, file);

	wired_setting = make_wired_setting (ifcfg, error);
	if (!wired_setting)
		goto error;

	nm_connection_add_setting (connection, wired_setting);

	if (!nm_connection_verify (connection)) {
		g_set_error (error, ifcfg_plugin_error_quark (), 0,
		             "Connection from %s was invalid.", file);
		goto error;
	}

	return connection;

error:
	g_object_unref (connection);
	if (con_setting)
		g_object_unref (con_setting);
	if (wired_setting)
		g_object_unref (wired_setting);
	return NULL;
}

static gboolean
is_wireless_device (const char *iface, gboolean *is_wireless)
{
	int fd;
	struct iw_range range;
	struct iwreq wrq;
	gboolean success = FALSE;

	g_return_val_if_fail (iface != NULL, FALSE);
	g_return_val_if_fail (is_wireless != NULL, FALSE);

	*is_wireless = FALSE;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (!fd)
		return FALSE;

	memset (&wrq, 0, sizeof (struct iwreq));
	memset (&range, 0, sizeof (struct iw_range));
	strncpy (wrq.ifr_name, iface, IFNAMSIZ);
	wrq.u.data.pointer = (caddr_t) &range;
	wrq.u.data.length = sizeof (struct iw_range);

	if (ioctl (fd, SIOCGIWRANGE, &wrq) < 0) {
		if (errno == EOPNOTSUPP)
			success = TRUE;
		goto out;
	}

	*is_wireless = TRUE;
	success = TRUE;

out:
	close (fd);
	return success;
}

#define TYPE_ETHERNET "Ethernet"
#define TYPE_WIRELESS "Wireless"

NMConnection *
parser_parse_file (const char *file, GError **error)
{
	NMConnection *connection = NULL;
	shvarFile *parsed;
	char *type;
	char *nmc = NULL;
	NMSetting *s_ip4;
	char *ifcfg_name = NULL;
	gboolean ignored = FALSE;
	ConnectionData *cdata;

	g_return_val_if_fail (file != NULL, NULL);

	ifcfg_name = get_ifcfg_name (file);
	if (!ifcfg_name) {
		g_set_error (error, ifcfg_plugin_error_quark (), 0,
		             "Ignoring connection '%s' because it's not an ifcfg file.", file);
		return NULL;
	}
	g_free (ifcfg_name);

	parsed = svNewFile (file);
	if (!parsed) {
		g_set_error (error, ifcfg_plugin_error_quark (), 0,
		             "Couldn't parse file '%s'", file);
		return NULL;
	}

	type = svGetValue (parsed, "TYPE");
	if (!type) {
		char *device;
		gboolean is_wireless = FALSE;

		/* If no type, if the device has wireless extensions, it's wifi,
		 * otherwise it's ethernet.
		 */
		device = svGetValue (parsed, "DEVICE");
		if (!device) {
			g_set_error (error, ifcfg_plugin_error_quark (), 0,
			             "File '%s' had neither TYPE nor DEVICE keys.", file);
			goto done;
		}

		if (!strcmp (device, "lo")) {
			g_set_error (error, ifcfg_plugin_error_quark (), 0,
			             "Ignoring loopback device config.");
			g_free (device);
			goto done;
		}

		/* Test wireless extensions */
		if (!is_wireless_device (device, &is_wireless)) {
			g_set_error (error, ifcfg_plugin_error_quark (), 0,
			             "File '%s' specified device '%s', but the device's "
			             "type could not be determined.", file, device);
			g_free (device);
			goto done;
		}

		if (is_wireless)
			type = g_strdup (TYPE_WIRELESS);
		else
			type = g_strdup (TYPE_ETHERNET);

		g_free (device);
	}

	nmc = svGetValue (parsed, "NM_CONTROLLED");
	if (nmc) {
		char *lower;

		lower = g_ascii_strdown (nmc, -1);
		g_free (nmc);

		if (!strcmp (lower, "no") || !strcmp (lower, "n") || !strcmp (lower, "false"))
			ignored = TRUE;
		g_free (lower);
	}

	if (!strcmp (type, TYPE_ETHERNET))
		connection = wired_connection_from_ifcfg (file, parsed, error);
	else if (!strcmp (type, TYPE_WIRELESS))
		connection = wireless_connection_from_ifcfg (file, parsed, error);
	else {
		g_set_error (error, ifcfg_plugin_error_quark (), 0,
		             "Unknown connection type '%s'", type);
	}

	g_free (type);

	if (!connection)
		goto done;

	s_ip4 = make_ip4_setting (parsed, error);
	if (*error) {
		g_object_unref (connection);
		connection = NULL;
		goto done;
	} else if (s_ip4) {
		nm_connection_add_setting (connection, s_ip4);
	}

	/* Update the ignored tag */
	cdata = connection_data_get (connection);
	cdata->ignored = ignored;

	if (!nm_connection_verify (connection)) {
		g_object_unref (connection);
		connection = NULL;
		g_set_error (error, ifcfg_plugin_error_quark (), 0,
		             "Connection was invalid");
	}

done:
	svCloseFile (parsed);
	return connection;
}
