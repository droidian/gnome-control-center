/*
 * Copyright (c) 2009, 2010 Intel, Inc.
 * Copyright (c) 2010 Red Hat, Inc.
 *
 * The Control Center is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * The Control Center is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with the Control Center; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Author: Thomas Wood <thos@gnome.org>
 */

#include "config.h"

#include <stdlib.h>
#include <locale.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#ifdef GDK_WINDOWING_X11
#include <X11/Xlib.h>
#endif

#include "cc-log.h"
#include "cc-application.h"
#include "cc-panel-loader.h"
#include "cc-panel-list.h"

/* Extension points */
extern GType cc_applications_panel_get_type (void);
extern GType cc_background_panel_get_type (void);
#ifdef BUILD_BLUETOOTH
extern GType cc_bluetooth_panel_get_type (void);
#endif /* BUILD_BLUETOOTH */
extern GType cc_date_time_panel_get_type (void);
extern GType cc_display_panel_get_type (void);
extern GType cc_keyboard_panel_get_type (void);
extern GType cc_mouse_panel_get_type (void);
extern GType cc_multitasking_panel_get_type (void);
#ifdef BUILD_NETWORK
extern GType cc_network_panel_get_type (void);
extern GType cc_wifi_panel_get_type (void);
#endif /* BUILD_NETWORK */
extern GType cc_notifications_panel_get_type (void);
extern GType cc_online_accounts_panel_get_type (void);
extern GType cc_power_panel_get_type (void);
extern GType cc_printers_panel_get_type (void);
extern GType cc_privacy_panel_get_type (void);
extern GType cc_search_panel_get_type (void);
extern GType cc_sound_panel_get_type (void);
extern GType cc_system_panel_get_type (void);
extern GType cc_ua_panel_get_type (void);
#ifdef BUILD_WACOM
extern GType cc_wacom_panel_get_type (void);
#endif /* BUILD_WACOM */
#ifdef BUILD_WWAN
extern GType cc_wwan_panel_get_type (void);
#endif /* BUILD_WWAN */

/* Static init functions */
#ifdef BUILD_NETWORK
extern void cc_wifi_panel_static_init_func (void);
#endif /* BUILD_NETWORK */
extern void cc_sharing_panel_static_init_func (void);
#ifdef BUILD_WACOM
extern void cc_wacom_panel_static_init_func (void);
#endif /* BUILD_WACOM */
#ifdef BUILD_WWAN
extern void cc_wwan_panel_static_init_func (void);
#endif /* BUILD_WWAN */

extern GType cc_droidian_encryption_panel_get_type (void);

#define PANEL_TYPE(name, get_type, init_func) { name, get_type, init_func }

static CcPanelLoaderVtable droidian_panels[] =
{
  PANEL_TYPE("applications",     cc_applications_panel_get_type,         NULL),
  PANEL_TYPE("background",       cc_background_panel_get_type,           NULL),
#ifdef BUILD_BLUETOOTH
  PANEL_TYPE("bluetooth",        cc_bluetooth_panel_get_type,            NULL),
#endif
  PANEL_TYPE("display",          cc_display_panel_get_type,              NULL),
  PANEL_TYPE("keyboard",         cc_keyboard_panel_get_type,             NULL),
  PANEL_TYPE("mouse",            cc_mouse_panel_get_type,                NULL),
#ifdef BUILD_NETWORK
  PANEL_TYPE("network",          cc_network_panel_get_type,              NULL),
  PANEL_TYPE("wifi",             cc_wifi_panel_get_type,                 cc_wifi_panel_static_init_func),
#endif
  PANEL_TYPE("notifications",    cc_notifications_panel_get_type,        NULL),
  PANEL_TYPE("online-accounts",  cc_online_accounts_panel_get_type,      NULL),
  PANEL_TYPE("power",            cc_power_panel_get_type,                NULL),
  PANEL_TYPE("printers",         cc_printers_panel_get_type,             NULL),
  PANEL_TYPE("privacy",          cc_privacy_panel_get_type,              NULL),
  PANEL_TYPE("sound",            cc_sound_panel_get_type,                NULL),
  PANEL_TYPE("system",           cc_system_panel_get_type,               NULL),
  PANEL_TYPE("universal-access", cc_ua_panel_get_type,                   NULL),
#ifdef BUILD_WACOM
  PANEL_TYPE("wacom",            cc_wacom_panel_get_type,                cc_wacom_panel_static_init_func),
#endif
#ifdef BUILD_WWAN
  PANEL_TYPE("wwan",             cc_wwan_panel_get_type,                 cc_wwan_panel_static_init_func),
#endif
  PANEL_TYPE("droidian-encryption", cc_droidian_encryption_panel_get_type, NULL),
};


static gchar * const droidian_panel_order[] = {
  /* Main page */
  "wifi",
  "network",
  "wwan",
  "mobile-broadband",
  "bluetooth",

  "separator",

  "background",
  "display",
  "sound",
  "power",

  "separator",

  "applications",
  "notifications",
  "online-accounts",

  "separator",

  "mouse",
  "keyboard",
  "printers",
  "wacom",

  "separator",

  "universal-access",
  "privacy",
  "droidian-encryption",
  "system",
  "reset-settings",
};


int
main (gint    argc,
      gchar **argv)
{
  g_autoptr(GtkApplication) application = NULL;

  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  setlocale (LC_ALL, "");
  cc_log_init ();

  cc_panel_loader_override_vtable (droidian_panels, G_N_ELEMENTS (droidian_panels));
  cc_panel_list_override_order (droidian_panel_order, G_N_ELEMENTS (droidian_panel_order));

  application = cc_application_new ();

  return g_application_run (G_APPLICATION (application), argc, argv);
}
