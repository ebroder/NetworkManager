#ifndef NM_MANAGER_H
#define NM_MANAGER_H 1

#include <glib/gtypes.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include "nm-device.h"
#include "nm-device-interface.h"

#define NM_TYPE_MANAGER            (nm_manager_get_type ())
#define NM_MANAGER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_MANAGER, NMManager))
#define NM_MANAGER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_MANAGER, NMManagerClass))
#define NM_IS_MANAGER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_MANAGER))
#define NM_IS_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), NM_TYPE_MANAGER))
#define NM_MANAGER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_MANAGER, NMManagerClass))

#define NM_MANAGER_STATE "state"
#define NM_MANAGER_WIRELESS_ENABLED "wireless-enabled"
#define NM_MANAGER_WIRELESS_HARDWARE_ENABLED "wireless-hardware-enabled"
#define NM_MANAGER_ACTIVE_CONNECTIONS "active-connections"

#define NM_MANAGER_CONNECTION_PROXY_TAG "dbus-proxy"
#define NM_MANAGER_CONNECTION_SECRETS_PROXY_TAG "dbus-secrets-proxy"

typedef struct {
	GObject parent;
} NMManager;

typedef struct {
	GObjectClass parent;

	/* Signals */
	void (*device_added) (NMManager *manager, NMDevice *device);
	void (*device_removed) (NMManager *manager, NMDevice *device);
	void (*state_changed) (NMManager *manager, guint state);
	void (*properties_changed) (NMManager *manager, GHashTable *properties);

	void (*connections_added) (NMManager *manager, NMConnectionScope scope);

	void (*connection_added) (NMManager *manager,
				  NMConnection *connection,
				  NMConnectionScope scope);

	void (*connection_updated) (NMManager *manager,
				  NMConnection *connection,
				  NMConnectionScope scope);

	void (*connection_removed) (NMManager *manager,
				    NMConnection *connection,
				    NMConnectionScope scope);
} NMManagerClass;

GType nm_manager_get_type (void);

NMManager *nm_manager_new (void);

/* Device handling */

void nm_manager_add_device (NMManager *manager, NMDevice *device);
void nm_manager_remove_device (NMManager *manager, NMDevice *device, gboolean deactivate);
GSList *nm_manager_get_devices (NMManager *manager);
NMDevice *nm_manager_get_device_by_path (NMManager *manager, const char *path);
NMDevice *nm_manager_get_device_by_udi (NMManager *manager, const char *udi);

NMDevice *nm_manager_get_active_device (NMManager *manager);

const char *nm_manager_activate_device (NMManager *manager,
				      NMDevice *device,
				      NMConnection *connection,
				      const char *specific_object,
				      gboolean user_requested,
				      GError **error);

gboolean  nm_manager_activation_pending (NMManager *manager);

/* State handling */

NMState nm_manager_get_state (NMManager *manager);
gboolean nm_manager_wireless_enabled (NMManager *manager);
gboolean nm_manager_wireless_hardware_enabled (NMManager *manager);
void nm_manager_set_wireless_hardware_enabled (NMManager *manager,
					       gboolean enabled);
void nm_manager_sleep (NMManager *manager, gboolean sleep);

/* Connections */

GSList *nm_manager_get_connections    (NMManager *manager, NMConnectionScope scope);

NMConnection * nm_manager_get_connection_by_object_path (NMManager *manager,
                                                         NMConnectionScope scope,
                                                         const char *path);

#endif /* NM_MANAGER_H */