/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

#ifndef NM_PPP_MANAGER_H
#define NM_PPP_MANAGER_H

#include <glib/gtypes.h>
#include <glib-object.h>

#include "nm-ppp-status.h"
#include "nm-activation-request.h"
#include "nm-connection.h"
#include "nm-ip4-config.h"
#include "nm-pppd-plugin.h"

#define NM_TYPE_PPP_MANAGER            (nm_ppp_manager_get_type ())
#define NM_PPP_MANAGER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_PPP_MANAGER, NMPPPManager))
#define NM_PPP_MANAGER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_PPP_MANAGER, NMPPPManagerClass))
#define NM_IS_PPP_MANAGER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_PPP_MANAGER))
#define NM_IS_PPP_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), NM_TYPE_PPP_MANAGER))
#define NM_PPP_MANAGER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_PPP_MANAGER, NMPPPManagerClass))

typedef struct {
	GObject parent;
} NMPPPManager;

typedef struct {
	GObjectClass parent;

	/* Signals */
	void (*state_changed) (NMPPPManager *manager, NMPPPStatus status);
	void (*ip4_config) (NMPPPManager *manager, const char *iface, NMIP4Config *config);
} NMPPPManagerClass;

GType nm_ppp_manager_get_type (void);

NMPPPManager *nm_ppp_manager_new (void);

gboolean nm_ppp_manager_start (NMPPPManager *manager,
						 const char *device,
						 NMActRequest *req,
						 GError **err);

void     nm_ppp_manager_update_secrets (NMPPPManager *manager,
								const char *device,
								NMConnection *connection);

void     nm_ppp_manager_stop  (NMPPPManager *manager);


#define NM_PPP_MANAGER_ERROR nm_ppp_manager_error_quark()
#define NM_TYPE_PPP_MANAGER_ERROR (nm_ppp_manager_error_get_type ()) 

GQuark nm_ppp_manager_error_quark (void);

#endif /* NM_PPP_MANAGER_H */