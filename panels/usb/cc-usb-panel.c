/*
 * Copyright (C) 2024 Cedric Bellegarde <cedric.bellegarde@adishatz.org>
 * Copyright (C) 2023 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-usb-panel.h"
#include "cc-usb-resources.h"
#include "cc-util.h"

#include <adwaita.h>
#include <gtk/gtk.h>

#define SYSTEMD_DBUS_NAME       "org.freedesktop.systemd1"
#define SYSTEMD_DBUS_PATH       "/org/freedesktop/systemd1"
#define SYSTEMD_DBUS_INTERFACE  "org.freedesktop.systemd1.Manager"

#define MODES 3
const char* modes[] = { "none", "mtp", "rndis" };

struct _CcUsbPanel {
  CcPanel            parent;

  GDBusProxy     *systemd_system;
  GDBusProxy     *systemd_session;
  GSimpleAction  *usbconfig;
  GCancellable   *cancellable;

  AdwActionRow   *row_mtp;

  gboolean        mtp_supported;
};

G_DEFINE_TYPE (CcUsbPanel, cc_usb_panel, CC_TYPE_PANEL)

static void
manage_unit_cb (GObject      *source_object,
                GAsyncResult *res,
                gpointer      user_data)
{
  g_autoptr(GVariant) value = NULL;
  g_autoptr(GError) error = NULL;

  value = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object),
                                    res, &error);

  if (error != NULL)
    {
      g_warning("Can't manage unit: %s", error->message);
      return;
    }
}

static void
enable_mtp(CcUsbPanel *self, gboolean enable)
{
  g_autofree gchar *path = g_build_path (G_DIR_SEPARATOR_S,
                                         g_get_home_dir (),
                                         ".mtp_disable",
                                         NULL);
  g_autoptr(GFile) file = g_file_new_for_path (path);
  g_autoptr(GFileOutputStream) stream = NULL;

  if(enable)
    {
      g_file_delete (file,
                     self->cancellable,
                     NULL);
      g_dbus_proxy_call (
                  self->systemd_session,
                  "StartUnit",
                  g_variant_new ("(&s&s)", "mtp-server.service", "replace"),
                  G_DBUS_CALL_FLAGS_NONE,
                  5000,
                  self->cancellable,
                  manage_unit_cb,
                  self);
    }
  else
    {
      g_dbus_proxy_call (
                  self->systemd_session,
                  "StopUnit",
                  g_variant_new ("(&s&s)", "mtp-server.service", "replace"),
                  G_DBUS_CALL_FLAGS_NONE,
                  5000,
                  self->cancellable,
                  manage_unit_cb,
                  self);
      stream = g_file_create (file,
                              G_FILE_CREATE_NONE,
                              self->cancellable,
                              NULL);
    }
}

static void
set_mode(CcUsbPanel  *self,
         const gchar *mode)
{
  g_autofree gchar *service = g_strdup_printf("mtp-configfs@%s.service", mode);

  /*
   * Systemd org.freedesktop.systemd1.manage-unit-files does not give details
   * on polkit bus. So we can't allow user to enable/disable mtp-configfs@.*.
   *
   * For now, we are enabling the wanted target on start,
   * see: mtp-configfs@.service
   */

  /* GVariantBuilder builder; */
  /* g_variant_builder_init (&builder, G_VARIANT_TYPE ("as")); */
  /* for (int i=0; i < MODES; i++) */
  /*   { */
  /*     const gchar *mode = modes[i]; */
  /*     g_autofree gchar *service = g_strdup_printf("mtp-configfs@%s.service", */
  /*                                                 mode); */
  /*     g_variant_builder_add (&builder, "s", service); */
  /*   } */
  /* g_dbus_proxy_call ( */
  /*                 self->systemd_system, */
  /*                 "DisableUnitFiles", */
  /*                 g_variant_new("(asb)", &builder, FALSE), */
  /*                 G_DBUS_CALL_FLAGS_NONE, */
  /*                 5000, */
  /*                 self->cancellable, */
  /*                 manage_unit_cb, */
  /*                 self); */

  /* g_variant_builder_init (&builder, G_VARIANT_TYPE ("as")); */
  /* g_variant_builder_add (&builder, "s", service); */
  /* g_dbus_proxy_call ( */
  /*               self->systemd_system, */
  /*               "EnableUnitFiles", */
  /*               g_variant_new("(asbb)", &builder, FALSE, TRUE), */
  /*               G_DBUS_CALL_FLAGS_NONE, */
  /*               5000, */
  /*               self->cancellable, */
  /*               manage_unit_cb, */
  /*               self); */

  g_dbus_proxy_call (
                  self->systemd_system,
                  "StartUnit",
                  g_variant_new ("(&s&s)", service, "replace"),
                  G_DBUS_CALL_FLAGS_NONE,
                  5000,
                  self->cancellable,
                  manage_unit_cb,
                  self);
}

static void
usbconfig_change_state_cb (GSimpleAction *action,
                           GVariant      *value,
                           gpointer       userdata)
{
  CcUsbPanel *self = (CcUsbPanel *) userdata;
  const gchar *config = NULL;

  g_variant_get (value, "&s", &config);

  set_mode (self, config);
  enable_mtp (self, g_strcmp0 (config, "mtp") == 0);

  g_simple_action_set_state (action, value);
}

static void
cc_usb_panel_dispose (GObject *object)
{
  CcUsbPanel *self = (CcUsbPanel *) object;

  g_clear_object (&self->systemd_system);
  g_clear_object (&self->systemd_session);
  g_clear_object (&self->usbconfig);
  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (cc_usb_panel_parent_class)->dispose (object);
}

static void
cc_usb_panel_finalize (GObject *object)
{
  CcUsbPanel *self = (CcUsbPanel *) object;

  g_cancellable_cancel (self->cancellable);

  G_OBJECT_CLASS (cc_usb_panel_parent_class)->finalize (object);
}

static void
cc_usb_panel_class_init (CcUsbPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = cc_usb_panel_dispose;
  object_class->finalize = cc_usb_panel_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/usb/cc-usb-panel.ui");
  gtk_widget_class_bind_template_child (widget_class,
                                        CcUsbPanel,
                                        row_mtp);
}

static void
cc_usb_panel_init (CcUsbPanel *self)
{
  g_autoptr (GError) error = NULL;
  gboolean mtp_supported = g_file_test ("/usr/lib/droidian/device/mtp-supported",
                                        G_FILE_TEST_EXISTS);

  g_resources_register (cc_usb_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  if (!mtp_supported)
    gtk_widget_hide (GTK_WIDGET (self->row_mtp));

  self->cancellable = g_cancellable_new ();

  self->systemd_system = g_dbus_proxy_new_for_bus_sync (
                                                    G_BUS_TYPE_SYSTEM,
                                                    0,
                                                    NULL,
                                                    SYSTEMD_DBUS_NAME,
                                                    SYSTEMD_DBUS_PATH,
                                                    SYSTEMD_DBUS_INTERFACE,
                                                    self->cancellable,
                                                    &error);

  if (error != NULL)
    {
      g_warning("Can't connect to %s: %s", SYSTEMD_DBUS_NAME, error->message);
      g_clear_object (&self->systemd_system);
      return;
    }

  self->systemd_session = g_dbus_proxy_new_for_bus_sync (
                                                    G_BUS_TYPE_SESSION,
                                                    0,
                                                    NULL,
                                                    SYSTEMD_DBUS_NAME,
                                                    SYSTEMD_DBUS_PATH,
                                                    SYSTEMD_DBUS_INTERFACE,
                                                    self->cancellable,
                                                    &error);
  if (error != NULL)
    {
      g_warning("Can't connect to %s: %s", SYSTEMD_DBUS_NAME, error->message);
      g_clear_object (&self->systemd_session);
      return;
    }

  self->usbconfig = g_simple_action_new_stateful ("usbconfig",
                                                  g_variant_type_new ("s"),
                                                  g_variant_new (
                                                              "s",
                                                              "none"));
  g_signal_connect (self->usbconfig,
                    "change-state",
                    (GCallback) usbconfig_change_state_cb,
                    self);
  g_action_map_add_action (G_ACTION_MAP (g_application_get_default ()),
                           G_ACTION (self->usbconfig));

  for (int i=0; i < MODES; i++)
    {
      const gchar *mode = modes[i];
      g_autofree gchar *filename = g_strdup_printf("mtp-configfs@%s.service",
                                                   mode);
      g_autofree gchar *path = g_build_path (G_DIR_SEPARATOR_S,
                                             "/etc/systemd/system",
                                             "graphical.target.wants",
                                             filename,
                                             NULL);

      if (g_file_test(path, G_FILE_TEST_EXISTS))
        {
          g_simple_action_set_state (self->usbconfig,
                                     g_variant_new ("s", mode));
          break;
        }
    }
}

CcUsbPanel *
cc_usb_panel_new (void)
{
  return CC_USB_PANEL (g_object_new (CC_TYPE_USB_PANEL, NULL));
}
