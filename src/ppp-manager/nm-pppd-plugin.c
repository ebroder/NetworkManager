/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

#include <string.h>
#include <pppd/pppd.h>
#include <pppd/fsm.h>
#include <pppd/ipcp.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <glib/gtypes.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>

#include "nm-pppd-plugin.h"
#include "nm-ppp-status.h"
#include "nm-pppd-plugin-glue.h"

int plugin_init (void);

char pppd_version[] = VERSION;

static DBusGProxy *proxy = NULL;

static void
nm_phasechange (void *data, int arg)
{
	NMPPPStatus ppp_status = NM_PPP_STATUS_UNKNOWN;
	char *ppp_phase;

	g_return_if_fail (DBUS_IS_G_PROXY (proxy));

	switch (arg) {
	case PHASE_DEAD:
		ppp_status = NM_PPP_STATUS_DEAD;
		ppp_phase = "dead";
		break;
	case PHASE_INITIALIZE:
		ppp_status = NM_PPP_STATUS_INITIALIZE;
		ppp_phase = "initialize";
		break;
	case PHASE_SERIALCONN:
		ppp_status = NM_PPP_STATUS_SERIALCONN;
		ppp_phase = "serial connection";
		break;
	case PHASE_DORMANT:
		ppp_status = NM_PPP_STATUS_DORMANT;
		ppp_phase = "dormant";
		break;
	case PHASE_ESTABLISH:
		ppp_status = NM_PPP_STATUS_ESTABLISH;
		ppp_phase = "establish";
		break;
	case PHASE_AUTHENTICATE:
		ppp_status = NM_PPP_STATUS_AUTHENTICATE;
		ppp_phase = "authenticate";
		break;
	case PHASE_CALLBACK:
		ppp_status = NM_PPP_STATUS_CALLBACK;
		ppp_phase = "callback";
		break;
	case PHASE_NETWORK:
		ppp_status = NM_PPP_STATUS_NETWORK;
		ppp_phase = "network";
		break;
	case PHASE_RUNNING:
		ppp_status = NM_PPP_STATUS_RUNNING;
		ppp_phase = "running";
		break;
	case PHASE_TERMINATE:
		ppp_status = NM_PPP_STATUS_TERMINATE;
		ppp_phase = "terminate";
		break;
	case PHASE_DISCONNECT:
		ppp_status = NM_PPP_STATUS_DISCONNECT;
		ppp_phase = "disconnect";
		break;
	case PHASE_HOLDOFF:
		ppp_status = NM_PPP_STATUS_HOLDOFF;
		ppp_phase = "holdoff";
		break;
	case PHASE_MASTER:
		ppp_status = NM_PPP_STATUS_MASTER;
		ppp_phase = "master";
		break;

	default:
		ppp_phase = "unknown";
		break;
	}

	if (ppp_status != NM_PPP_STATUS_UNKNOWN) {
		dbus_g_proxy_call_no_reply (proxy, "SetState",
							   G_TYPE_UINT, ppp_status,
							   G_TYPE_INVALID,
							   G_TYPE_INVALID);
	}
}

static GValue *
str_to_gvalue (const char *str)
{
	GValue *val;

	val = g_slice_new0 (GValue);
	g_value_init (val, G_TYPE_STRING);
	g_value_set_string (val, str);

	return val;
}

static GValue *
uint_to_gvalue (guint32 i)
{
	GValue *val;

	val = g_slice_new0 (GValue);
	g_value_init (val, G_TYPE_UINT);
	g_value_set_uint (val, i);

	return val;
}

static void
value_destroy (gpointer data)
{
	GValue *val = (GValue *) data;

	g_value_unset (val);
	g_slice_free (GValue, val);
}

static void
nm_ip_up (void *data, int arg)
{
	ipcp_options opts = ipcp_gotoptions[ifunit];
	ipcp_options peer_opts = ipcp_hisoptions[ifunit];
	GHashTable *hash;
	GArray *array;
	GValue *val;

	g_return_if_fail (DBUS_IS_G_PROXY (proxy));

	if (!opts.ouraddr) {
		g_warning ("Didn't receive an internal IP from pppd");
		return;
	}

	hash = g_hash_table_new_full (g_str_hash, g_str_equal,
							NULL, value_destroy);

	g_hash_table_insert (hash, NM_PPP_IP4_CONFIG_INTERFACE, 
					 str_to_gvalue (ifname));

	g_hash_table_insert (hash, NM_PPP_IP4_CONFIG_ADDRESS, 
					 uint_to_gvalue (opts.ouraddr));

	if (opts.hisaddr) {
		g_hash_table_insert (hash, NM_PPP_IP4_CONFIG_GATEWAY, 
						 uint_to_gvalue (opts.hisaddr));
	} else if (peer_opts.hisaddr) {
		g_hash_table_insert (hash, NM_PPP_IP4_CONFIG_GATEWAY, 
						 uint_to_gvalue (peer_opts.hisaddr));
	}

	g_hash_table_insert (hash, NM_PPP_IP4_CONFIG_NETMASK, 
					 uint_to_gvalue (0xFFFFFFFF));

	if (opts.dnsaddr[0] || opts.dnsaddr[1]) {
		array = g_array_new (FALSE, FALSE, sizeof (guint32));

		if (opts.dnsaddr[0])
			g_array_append_val (array, opts.dnsaddr[0]);
		if (opts.dnsaddr[1])
			g_array_append_val (array, opts.dnsaddr[1]);

		val = g_slice_new0 (GValue);
		g_value_init (val, DBUS_TYPE_G_UINT_ARRAY);
		g_value_set_boxed (val, array);

		g_hash_table_insert (hash, NM_PPP_IP4_CONFIG_DNS, val);
	}

	if (opts.winsaddr[0] || opts.winsaddr[1]) {
		array = g_array_new (FALSE, FALSE, sizeof (guint32));

		if (opts.winsaddr[0])
			g_array_append_val (array, opts.winsaddr[0]);
		if (opts.winsaddr[1])
			g_array_append_val (array, opts.winsaddr[1]);

		val = g_slice_new0 (GValue);
		g_value_init (val, DBUS_TYPE_G_UINT_ARRAY);
		g_value_set_boxed (val, array);

		g_hash_table_insert (hash, NM_PPP_IP4_CONFIG_WINS, val);
	}

	dbus_g_proxy_call_no_reply (proxy, "SetIp4Config",
						   dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE),
						   hash,
						   G_TYPE_INVALID,
						   G_TYPE_INVALID);

	g_hash_table_destroy (hash);
}

static int
get_credentials (char *username, char *password)
{
	char *my_username;
	char *my_password;
	size_t len;
	GError *err = NULL;

	g_return_val_if_fail (DBUS_IS_G_PROXY (proxy), -1);

	my_username = my_password = NULL;
	dbus_g_proxy_call (proxy, "NeedSecrets", &err,
				    G_TYPE_INVALID,
				    G_TYPE_STRING, &my_username,
				    G_TYPE_STRING, &my_password,
				    G_TYPE_INVALID);

	if (err) {
		g_warning ("Could not get secrets: %s", err->message);
		g_error_free (err);
		return -1;
	}

	if (my_username) {
		len = strlen (my_username) + 1;
		len = len < MAXNAMELEN ? len : MAXNAMELEN;

		strncpy (username, my_username, len);
		username[len - 1] = '\0';

		g_free (my_username);
	}

	if (my_password) {
		len = strlen (my_password) + 1;
		len = len < MAXSECRETLEN ? len : MAXSECRETLEN;

		strncpy (password, my_password, len);
		password[len - 1] = '\0';

		g_free (my_password);
	}

	return 0;
}

static void
nm_exit_notify (void *data, int arg)
{
	g_return_if_fail (DBUS_IS_G_PROXY (proxy));

	g_object_unref (proxy);
	proxy = NULL;
}

int
plugin_init (void)
{
	DBusGConnection *bus;
	GError *err = NULL;

	g_type_init ();

	bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &err);
	if (!bus) {
		g_warning ("Couldn't connect to system bus: %s", err->message);
		g_error_free (err);
		return -1;
	}

	proxy = dbus_g_proxy_new_for_name (bus,
								NM_DBUS_SERVICE_PPP,
								NM_DBUS_PATH_PPP,
								NM_DBUS_INTERFACE_PPP);

	dbus_g_connection_unref (bus);

	chap_passwd_hook = get_credentials;
	pap_passwd_hook = get_credentials;

	add_notifier (&phasechange, nm_phasechange, NULL);
	add_notifier (&ip_up_notifier, nm_ip_up, NULL);
	add_notifier (&exitnotify, nm_exit_notify, proxy);

	return 0;
}