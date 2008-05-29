/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdlib.h>

#include "nm-dnsmasq-manager.h"
#include "nm-utils.h"

typedef struct {
	GPid pid;
	guint32 dm_watch_id;
} NMDnsMasqManagerPrivate;

#define NM_DNSMASQ_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_DNSMASQ_MANAGER, NMDnsMasqManagerPrivate))

G_DEFINE_TYPE (NMDnsMasqManager, nm_dnsmasq_manager, G_TYPE_OBJECT)

enum {
	STATE_CHANGED,

	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

typedef enum {
	NM_DNSMASQ_MANAGER_ERROR_UNKOWN
} NMDnsMasqManagerError;

GQuark
nm_dnsmasq_manager_error_quark (void)
{
	static GQuark quark;

	if (!quark)
		quark = g_quark_from_static_string ("nm_dnsmasq_manager_error");

	return quark;
}

static void
nm_dnsmasq_manager_init (NMDnsMasqManager *manager)
{
}

static GObject *
constructor (GType type,
             guint n_construct_params,
             GObjectConstructParam *construct_params)
{
	return G_OBJECT_CLASS (nm_dnsmasq_manager_parent_class)->constructor (type,
														   n_construct_params,
														   construct_params);
}

static void
finalize (GObject *object)
{
	nm_dnsmasq_manager_stop (NM_DNSMASQ_MANAGER (object));

	G_OBJECT_CLASS (nm_dnsmasq_manager_parent_class)->finalize (object);
}

static void
nm_dnsmasq_manager_class_init (NMDnsMasqManagerClass *manager_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (manager_class);

	g_type_class_add_private (manager_class, sizeof (NMDnsMasqManagerPrivate));

	object_class->constructor = constructor;
	object_class->finalize = finalize;

	/* signals */
	signals[STATE_CHANGED] =
		g_signal_new ("state-changed",
				    G_OBJECT_CLASS_TYPE (object_class),
				    G_SIGNAL_RUN_FIRST,
				    G_STRUCT_OFFSET (NMDnsMasqManagerClass, state_changed),
				    NULL, NULL,
				    g_cclosure_marshal_VOID__UINT,
				    G_TYPE_NONE, 1,
				    G_TYPE_UINT);
}

NMDnsMasqManager *
nm_dnsmasq_manager_new (void)
{
	return (NMDnsMasqManager *) g_object_new (NM_TYPE_DNSMASQ_MANAGER, NULL);
}

typedef struct {
	GPtrArray *array;
	GStringChunk *chunk;
} NMCmdLine;

static NMCmdLine *
nm_cmd_line_new (void)
{
	NMCmdLine *cmd;

	cmd = g_slice_new (NMCmdLine);
	cmd->array = g_ptr_array_new ();
	cmd->chunk = g_string_chunk_new (1024);

	return cmd;
}

static void
nm_cmd_line_destroy (NMCmdLine *cmd)
{
	g_ptr_array_free (cmd->array, TRUE);
	g_string_chunk_free (cmd->chunk);
	g_slice_free (NMCmdLine, cmd);
}

static char *
nm_cmd_line_to_str (NMCmdLine *cmd)
{
	char *str;

	g_ptr_array_add (cmd->array, NULL);
	str = g_strjoinv (" ", (gchar **) cmd->array->pdata);
	g_ptr_array_remove_index (cmd->array, cmd->array->len - 1);

	return str;
}

static void
nm_cmd_line_add_string (NMCmdLine *cmd, const char *str)
{
	g_ptr_array_add (cmd->array, g_string_chunk_insert (cmd->chunk, str));
}

/*******************************************/

static inline const char *
nm_find_dnsmasq (void)
{
	static const char *dnsmasq_binary_paths[] =
		{
			"/usr/local/sbin/dnsmasq",
			"/usr/sbin/dnsmasq",
			"/sbin/dnsmasq",
			NULL
		};

	const char **dnsmasq_binary = dnsmasq_binary_paths;

	while (*dnsmasq_binary != NULL) {
		if (g_file_test (*dnsmasq_binary, G_FILE_TEST_EXISTS))
			break;
		dnsmasq_binary++;
	}

	return *dnsmasq_binary;
}

static void
dm_exit_code (guint dm_exit_status)
{
	const char *msg;

	switch (dm_exit_status) {
	case 1:
		msg = "Configuration problem";
		break;
	case 2:
		msg = "Network access problem (address in use; permissions; etc)";
		break;
	case 3:
		msg = "Filesystem problem (missing file/directory; permissions; etc)";
		break;
	case 4:
		msg = "Memory allocation failure";
		break;
	case 5: 
		msg = "Other problem";
		break;
	default:
		if (dm_exit_status >= 11)
			msg = "Lease-script 'init' process failure";
		break;
	}

	g_warning ("dnsmasq exited with error: %s (%d)", msg, dm_exit_status);
}

static void
dm_watch_cb (GPid pid, gint status, gpointer user_data)
{
	NMDnsMasqManager *manager = NM_DNSMASQ_MANAGER (user_data);
	NMDnsMasqManagerPrivate *priv = NM_DNSMASQ_MANAGER_GET_PRIVATE (manager);
	guint err;

	if (WIFEXITED (status)) {
		err = WEXITSTATUS (status);
		if (err != 0)
			dm_exit_code (err);
	} else if (WIFSTOPPED (status))
		g_warning ("dnsmasq stopped unexpectedly with signal %d", WSTOPSIG (status));
	else if (WIFSIGNALED (status))
		g_warning ("dnsmasq died with signal %d", WTERMSIG (status));
	else
		g_warning ("dnsmasq died from an unknown cause");
  
	/* Reap child if needed. */
	waitpid (pid, NULL, WNOHANG);

	priv->pid = 0;

	g_signal_emit (manager, signals[STATE_CHANGED], 0, NM_DNSMASQ_STATUS_DEAD);
}

static char *
get_pidfile_for_iface (const char *iface)
{
	g_return_val_if_fail (iface != NULL, NULL);

	return g_strdup_printf (LOCALSTATEDIR "/run/nm-dnsmasq-%s.pid", iface);
}

static NMCmdLine *
create_dm_cmd_line (const char *iface, GError **err)
{
	const char *dm_binary;
	NMCmdLine *cmd;
	char *s, *pidfile;

	dm_binary = nm_find_dnsmasq ();
	if (!dm_binary) {
		g_set_error (err, NM_DNSMASQ_MANAGER_ERROR, NM_DNSMASQ_MANAGER_ERROR,
				   "Could not find dnsmasq binary.");
		return NULL;
	}

	/* Create dnsmasq command line */
	cmd = nm_cmd_line_new ();
	nm_cmd_line_add_string (cmd, dm_binary);

	nm_cmd_line_add_string (cmd, "--no-hosts");
	nm_cmd_line_add_string (cmd, "--keep-in-foreground");
	nm_cmd_line_add_string (cmd, "--listen-address=10.42.43.1");
	nm_cmd_line_add_string (cmd, "--bind-interfaces");
	nm_cmd_line_add_string (cmd, "--no-poll");
	nm_cmd_line_add_string (cmd, "--dhcp-range=10.42.43.10,10.42.43.100,60m");
	nm_cmd_line_add_string (cmd, "--dhcp-option=option:router,0.0.0.0");
	nm_cmd_line_add_string (cmd, "--dhcp-lease-max=50");

	pidfile = get_pidfile_for_iface (iface);
	s = g_strdup_printf ("--pid-file=%s", pidfile);
	g_free (pidfile);
	nm_cmd_line_add_string (cmd, s);
	g_free (s);

	return cmd;
}

static void
dm_child_setup (gpointer user_data G_GNUC_UNUSED)
{
	/* We are in the child process at this point */
	pid_t pid = getpid ();
	setpgid (pid, pid);
}

static void
kill_existing_for_iface (const char *iface)
{
	char *pidfile;
	char *contents = NULL;
	glong pid;
	char *proc_path = NULL;
	char *cmdline_contents = NULL;

	pidfile = get_pidfile_for_iface (iface);
	if (!g_file_get_contents (pidfile, &contents, NULL, NULL))
		goto out;

	pid = strtol (contents, NULL, 10);
	if (pid < 1 || pid > INT_MAX)
		goto out;

	proc_path = g_strdup_printf ("/proc/%ld/cmdline", pid);
	if (!g_file_get_contents (proc_path, &cmdline_contents, NULL, NULL))
		goto out;

	if (strstr (cmdline_contents, "bin/dnsmasq")) {
		if (kill (pid, 0)) {
			nm_info ("Killing stale dnsmasq process %ld", pid);
			kill (pid, SIGKILL);
		}
		unlink (pidfile);
	}

out:
	g_free (cmdline_contents);
	g_free (proc_path);
	g_free (contents);
	g_free (pidfile);
}

gboolean
nm_dnsmasq_manager_start (NMDnsMasqManager *manager,
				  const char *iface,
				  GError **err)
{
	NMDnsMasqManagerPrivate *priv;
	NMCmdLine *dm_cmd;
	char *cmd_str;
	GSource *dm_watch;

	g_return_val_if_fail (NM_IS_DNSMASQ_MANAGER (manager), FALSE);
	g_return_val_if_fail (iface != NULL, FALSE);

	kill_existing_for_iface (iface);

	dm_cmd = create_dm_cmd_line (iface, err);
	if (!dm_cmd)
		return FALSE;

	g_ptr_array_add (dm_cmd->array, NULL);

	priv = NM_DNSMASQ_MANAGER_GET_PRIVATE (manager);

	nm_info ("Starting dnsmasq...");

	cmd_str = nm_cmd_line_to_str (dm_cmd);
	nm_debug ("Command line: %s", cmd_str);
	g_free (cmd_str);

	priv->pid = 0;
	if (!g_spawn_async (NULL, (char **) dm_cmd->array->pdata, NULL,
					G_SPAWN_DO_NOT_REAP_CHILD,
					dm_child_setup,
					NULL, &priv->pid, err)) {
		goto out;
	}

	nm_debug ("dnsmasq started with pid %d", priv->pid);

	dm_watch = g_child_watch_source_new (priv->pid);
	g_source_set_callback (dm_watch, (GSourceFunc) dm_watch_cb, manager, NULL);
	g_source_attach (dm_watch, NULL);
	priv->dm_watch_id = g_source_get_id (dm_watch);
	g_source_unref (dm_watch);

 out:
	if (dm_cmd)
		nm_cmd_line_destroy (dm_cmd);

	return priv->pid > 0;
}

static gboolean
ensure_killed (gpointer data)
{
	int pid = GPOINTER_TO_INT (data);

	if (kill (pid, 0) == 0)
		kill (pid, SIGKILL);

	return FALSE;
}

void
nm_dnsmasq_manager_stop (NMDnsMasqManager *manager)
{
	NMDnsMasqManagerPrivate *priv;

	g_return_if_fail (NM_IS_DNSMASQ_MANAGER (manager));

	priv = NM_DNSMASQ_MANAGER_GET_PRIVATE (manager);

	if (priv->dm_watch_id) {
		g_source_remove (priv->dm_watch_id);
		priv->dm_watch_id = 0;
	}

	if (priv->pid) {
		if (kill (priv->pid, SIGTERM) == 0)
			g_timeout_add (2000, ensure_killed, GINT_TO_POINTER (priv->pid));
		else
			kill (priv->pid, SIGKILL);

		priv->pid = 0;
	}
}