/*
 * Copyright (C) 2023 Eugenio "g7" Paolantonio <me@medesimo.eu>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-droidian-encryption-panel.h"
#include "cc-droidian-encryption-resources.h"
#include "cc-util.h"

#include <adwaita.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>
#include <string.h>

struct _CcDroidianEncryptionPanel {
  CcPanel            parent;

  GCancellable       *cancellable;

  GDBusProxy              *encryption_service;
  EncryptionServiceStatus  encryption_service_status;
  guint                    encryption_service_timeout;

  GtkSwitch *enable_switch;
  GtkStack *stack;

  GtkSpinner *encryption_spinner;
  GtkLabel   *encryption_status;

  AdwPasswordEntryRow *passphrase_entry;
  AdwPasswordEntryRow *passphrase_confirm_entry;
  GtkButton           *encryption_start_button;
  gboolean             passphrases_match;

  AdwStatusPage      *encryption_unknown_page;
  AdwStatusPage      *encryption_unsupported_page;
  AdwStatusPage      *encryption_unconfigured_page;
  AdwPreferencesPage *encryption_enable_page;
  AdwPreferencesPage *encryption_status_page;
};

G_DEFINE_TYPE (CcDroidianEncryptionPanel, cc_droidian_encryption_panel, CC_TYPE_PANEL)

typedef enum
{
  PROP_0,
  PROP_STATUS,
  PROP_MATCH,
  PROP_LAST,
} CcDroidianEncryptionPanelProperty;

static GParamSpec *properties[PROP_LAST];

#define CC_TYPE_DROIDIAN_ENCRYPTION_PANEL_STATUS (cc_droidian_encryption_panel_status_get_type())
static GType
cc_droidian_encryption_panel_status_get_type (void)
{
  static GType status_type = 0;

  static  const GEnumValue statuses[] = {
    {ENCRYPTION_SERVICE_STATUS_UNKNOWN, "Unknown", "unknown"},
    {ENCRYPTION_SERVICE_STATUS_UNSUPPORTED, "Unsupported", "unsupported"},
    {ENCRYPTION_SERVICE_STATUS_UNCONFIGURED, "Unconfigured", "unconfigured"},
    {ENCRYPTION_SERVICE_STATUS_CONFIGURING, "Configuring", "configuring"},
    {ENCRYPTION_SERVICE_STATUS_CONFIGURED, "Please reboot to start", "configured"},
    {ENCRYPTION_SERVICE_STATUS_ENCRYPTING, "Encrypting", "encrypting"},
    {ENCRYPTION_SERVICE_STATUS_ENCRYPTED, "Encrypted", "encrypted"},
    {ENCRYPTION_SERVICE_STATUS_FAILED, "Failed", "failed"},
    {0, NULL, NULL},
  };

  if (!status_type)
    status_type = g_enum_register_static ("CcDroidianEncryptionPanelStatus", statuses);

  return status_type;
}

static void
service_refresh_status (CcDroidianEncryptionPanel *self)
{
  g_return_if_fail (CC_IS_DROIDIAN_ENCRYPTION_PANEL (self));

  /* We don't care about the callback as we're going to react anyways if
   * the status changes */

  g_dbus_proxy_call (self->encryption_service,
                     "RefreshStatus",
                     NULL,
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     self->cancellable,
                     NULL,
                     NULL);
}

/**
 * This method simply fetches the Status property from DBus - it doesn't
 * call RefreshStatus(). This should only be used to determine the status
 * on first start (if the encryption service is already running).
 */
static void
service_refresh_status_property (CcDroidianEncryptionPanel *self)
{
  g_autoptr (GVariant) variant = NULL;
  EncryptionServiceStatus value;

  g_return_if_fail (CC_IS_DROIDIAN_ENCRYPTION_PANEL (self));

  variant = g_dbus_proxy_get_cached_property (self->encryption_service, "Status");
  value = (EncryptionServiceStatus) g_variant_get_int32 (variant);

  g_debug ("Got encryption status from DBus: %s",
           g_enum_get_value (g_type_class_peek (CC_TYPE_DROIDIAN_ENCRYPTION_PANEL_STATUS),
                             value)->value_nick);

  self->encryption_service_status = value;

  /* Notify */
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATUS]);
}

static void
select_page (CcDroidianEncryptionPanel *self,
             EncryptionServiceStatus    status)
{
  GEnumValue *value = NULL;

  switch (status)
    {
    case ENCRYPTION_SERVICE_STATUS_UNKNOWN:
      gtk_widget_set_sensitive (GTK_WIDGET (self->enable_switch), FALSE);
      gtk_stack_set_visible_child (self->stack, GTK_WIDGET (self->encryption_unknown_page));
      break;
    case ENCRYPTION_SERVICE_STATUS_UNSUPPORTED:
      gtk_widget_set_sensitive (GTK_WIDGET (self->enable_switch), FALSE);
      gtk_stack_set_visible_child (self->stack, GTK_WIDGET (self->encryption_unsupported_page));
      break;
    case ENCRYPTION_SERVICE_STATUS_UNCONFIGURED:
      gtk_widget_set_sensitive (GTK_WIDGET (self->enable_switch), TRUE);
      gtk_stack_set_visible_child (self->stack, GTK_WIDGET (self->encryption_unconfigured_page));
      break;
    default:
      /* Get status */
      value = g_enum_get_value (g_type_class_peek (CC_TYPE_DROIDIAN_ENCRYPTION_PANEL_STATUS), status);

      /* Should the spinner be shown? TODO: move to a property? */
      gtk_widget_set_visible (GTK_WIDGET (self->encryption_spinner),
                              (status == ENCRYPTION_SERVICE_STATUS_CONFIGURING ||
                               status == ENCRYPTION_SERVICE_STATUS_ENCRYPTING));

      /* Should the switch show that the encryption is enabled? */
      gtk_switch_set_active (self->enable_switch, (status == ENCRYPTION_SERVICE_STATUS_ENCRYPTED ||
                                                   status == ENCRYPTION_SERVICE_STATUS_ENCRYPTING ||
                                                   status == ENCRYPTION_SERVICE_STATUS_CONFIGURING ||
                                                   status == ENCRYPTION_SERVICE_STATUS_CONFIGURED));

      gtk_label_set_text (GTK_LABEL (self->encryption_status), value->value_name);
      gtk_widget_set_sensitive (GTK_WIDGET (self->enable_switch), FALSE);
      gtk_stack_set_visible_child (self->stack, GTK_WIDGET (self->encryption_status_page));
      break;
    }
}

static void
select_enable_page (CcDroidianEncryptionPanel *self)
{
  /* Reset entries */
  gtk_editable_set_text (GTK_EDITABLE (self->passphrase_entry), "");
  gtk_editable_set_text (GTK_EDITABLE (self->passphrase_confirm_entry), "");

  gtk_widget_grab_focus (GTK_WIDGET (self->passphrase_entry));

  gtk_stack_set_visible_child (self->stack, GTK_WIDGET (self->encryption_enable_page));
}

static void
on_enable_switch_changed (CcDroidianEncryptionPanel *self,
                          GtkSwitch                 *switch_widget)
{
  g_return_if_fail (CC_IS_DROIDIAN_ENCRYPTION_PANEL (self));

  if (gtk_switch_get_state (self->enable_switch))
    /* Show the enable page */
    select_enable_page (self);
  else
    /* Only supported variant right now is to go back, i.e. let select_page() do the work */
    select_page (self, self->encryption_service_status);
}

static void
on_service_start_call_done (GDBusProxy                *proxy,
                            GAsyncResult              *res,
                            CcDroidianEncryptionPanel *self)
{
  g_autoptr (GError) error = NULL;
  GVariant *result;

  g_return_if_fail (CC_IS_DROIDIAN_ENCRYPTION_PANEL (self));

  result = g_dbus_proxy_call_finish (proxy, res, &error);

  if (result == NULL)
    {
      g_warning ("Failure while calling EncryptionService.Start(): %s", error->message);
    }
  else
    {
      /* Schedule refresh */
      service_refresh_status (self);
    }
}

static void
on_encryption_start_button_clicked (CcDroidianEncryptionPanel *self,
                                    GtkButton                 *button)
{
  g_return_if_fail (CC_IS_DROIDIAN_ENCRYPTION_PANEL (self));

  g_dbus_proxy_call (self->encryption_service,
                     "Start",
                     g_variant_new ("(s)",
                                    gtk_editable_get_text (GTK_EDITABLE (self->passphrase_entry))),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     self->cancellable,
                     (GAsyncReadyCallback) on_service_start_call_done,
                     self);
}

static void
on_passphrase_changed (CcDroidianEncryptionPanel *self,
                       GtkPasswordEntry          *entry)
{
  gboolean match = FALSE;
  const char* passphrase_entry_text;
  const char* passphrase_confirm_entry_text;

  g_return_if_fail (CC_IS_DROIDIAN_ENCRYPTION_PANEL (self));

  passphrase_entry_text = gtk_editable_get_text (GTK_EDITABLE (self->passphrase_entry));
  passphrase_confirm_entry_text = gtk_editable_get_text (GTK_EDITABLE (self->passphrase_confirm_entry));

  match = (strlen (passphrase_entry_text) > 0 &&
           (g_strcmp0 (passphrase_entry_text, passphrase_confirm_entry_text) == 0));
  if (match != self->passphrases_match)
    {
      self->passphrases_match = match;

      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MATCH]);
    }
}

static void
on_encryption_status_changed (CcDroidianEncryptionPanel *self)
{
  g_return_if_fail (CC_IS_DROIDIAN_ENCRYPTION_PANEL (self));

  select_page (self, self->encryption_service_status);
}

static void
on_service_properties_changed (GDBusProxy                *proxy,
                               GVariant                  *changed_properties,
                               const gchar* const        *invalidated_properties,
                               CcDroidianEncryptionPanel *self)
{
  gint value;

  if (g_variant_lookup (changed_properties, "Status", "i", &value))
    {
      g_debug ("Encryption status changed to: %i", value);

      select_page (self, (EncryptionServiceStatus) value);
    }
}

static gboolean
on_refresh_status_timeout_elapsed (CcDroidianEncryptionPanel *self)
{
  gboolean result = G_SOURCE_REMOVE;

  g_return_val_if_fail (CC_IS_DROIDIAN_ENCRYPTION_PANEL (self), result);

  switch (self->encryption_service_status)
    {
    case ENCRYPTION_SERVICE_STATUS_UNKNOWN:
    case ENCRYPTION_SERVICE_STATUS_UNCONFIGURED:
    case ENCRYPTION_SERVICE_STATUS_CONFIGURING:
    case ENCRYPTION_SERVICE_STATUS_ENCRYPTING:
      /* Non-permanent status, ensure we refresh again and keep the timeout */
      g_debug ("Scheduling new refresh status");

      result = G_SOURCE_CONTINUE;
      service_refresh_status (self);
      break;

    default:
      /* Permanent status, we can't do anything, drop */
      g_debug ("Status is permanent, removing timeout");

      self->encryption_service_timeout = 0;
      break;
    }

  return result;
}

static void
on_encryption_service_ready (GObject                    *source_object,
                             GAsyncResult               *res,
                             CcDroidianEncryptionPanel  *self)
{
  g_autoptr (GError) error = NULL;

  g_return_if_fail (CC_IS_DROIDIAN_ENCRYPTION_PANEL (self));

  self->encryption_service = g_dbus_proxy_new_for_bus_finish (res, &error);
  if (!self->encryption_service)
    {
      g_warning ("Unable to connect to encryption service: %s", error->message);
      return;
    }

  g_signal_connect (self->encryption_service,
                    "g-properties-changed",
                    G_CALLBACK (on_service_properties_changed),
                    self);

  /* Refresh status property and try to obtain a fresher state */
  service_refresh_status_property (self);
  service_refresh_status (self);

  /* Schedule status refresh */
  self->encryption_service_timeout =
    g_timeout_add_seconds (30, G_SOURCE_FUNC (on_refresh_status_timeout_elapsed), self);
}

static void
cc_droidian_encryption_panel_set_property (GObject      *object,
                                           uint          property_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
  switch ((CcDroidianEncryptionPanelProperty) property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
cc_droidian_encryption_panel_get_property (GObject    *object,
                                           uint        property_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
  CcDroidianEncryptionPanel *self = CC_DROIDIAN_ENCRYPTION_PANEL (object);

  switch ((CcDroidianEncryptionPanelProperty) property_id)
    {
    case PROP_STATUS:
      g_value_set_enum (value, self->encryption_service_status);
      break;

    case PROP_MATCH:
      g_value_set_boolean (value, self->passphrases_match);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
cc_droidian_encryption_panel_constructed (GObject *obj)
{
  CcDroidianEncryptionPanel *self = CC_DROIDIAN_ENCRYPTION_PANEL (obj);

  G_OBJECT_CLASS (cc_droidian_encryption_panel_parent_class)->constructed (obj);

  g_signal_connect_object (self, "notify::status", G_CALLBACK (on_encryption_status_changed),
                           NULL, G_CONNECT_DEFAULT);
  g_signal_connect_object (self->enable_switch, "notify::state", G_CALLBACK (on_enable_switch_changed),
                           self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->passphrase_entry, "changed", G_CALLBACK (on_passphrase_changed),
                           self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->passphrase_confirm_entry, "changed", G_CALLBACK (on_passphrase_changed),
                           self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->encryption_start_button, "clicked", G_CALLBACK (on_encryption_start_button_clicked),
                           self, G_CONNECT_SWAPPED);

  g_object_bind_property (self, "match", self->encryption_start_button,
                          "sensitive", G_BINDING_SYNC_CREATE);
}

static void
cc_droidian_encryption_panel_dispose (GObject *object)
{
  CcDroidianEncryptionPanel *self = CC_DROIDIAN_ENCRYPTION_PANEL (object);

  if (self->encryption_service_timeout > 0)
    g_source_remove (self->encryption_service_timeout);

  if (self->encryption_service)
    g_clear_object (&self->encryption_service);

  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (cc_droidian_encryption_panel_parent_class)->dispose (object);
}

static void
cc_droidian_encryption_panel_class_init (CcDroidianEncryptionPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = cc_droidian_encryption_panel_dispose;
  object_class->constructed = cc_droidian_encryption_panel_constructed;
  object_class->get_property = cc_droidian_encryption_panel_get_property;
  object_class->set_property = cc_droidian_encryption_panel_set_property;

  /* Properties */
  properties[PROP_STATUS] = g_param_spec_enum ("status", "Status", "The status",
                                               CC_TYPE_DROIDIAN_ENCRYPTION_PANEL_STATUS,
                                               ENCRYPTION_SERVICE_STATUS_UNKNOWN,
                                               G_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_STATUS, properties[PROP_STATUS]);

  properties[PROP_MATCH] = g_param_spec_boolean ("match", "Match", "Whether the passphrases match",
                                                 FALSE, G_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_MATCH, properties[PROP_MATCH]);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/droidian-encryption/cc-droidian-encryption-panel.ui");

  /* Generic stuff */
  gtk_widget_class_bind_template_child (widget_class,
                                        CcDroidianEncryptionPanel,
                                        enable_switch);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcDroidianEncryptionPanel,
                                        stack);

  /* Status pages */
  gtk_widget_class_bind_template_child (widget_class,
                                        CcDroidianEncryptionPanel,
                                        encryption_unknown_page);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcDroidianEncryptionPanel,
                                        encryption_unsupported_page);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcDroidianEncryptionPanel,
                                        encryption_unconfigured_page);


  /* Encryption status page */
  gtk_widget_class_bind_template_child (widget_class,
                                        CcDroidianEncryptionPanel,
                                        encryption_status_page);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcDroidianEncryptionPanel,
                                        encryption_status);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcDroidianEncryptionPanel,
                                        encryption_spinner);

  /* Encryption enable page */
  gtk_widget_class_bind_template_child (widget_class,
                                        CcDroidianEncryptionPanel,
                                        encryption_enable_page);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcDroidianEncryptionPanel,
                                        passphrase_entry);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcDroidianEncryptionPanel,
                                        passphrase_confirm_entry);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcDroidianEncryptionPanel,
                                        encryption_start_button);
}

static void
cc_droidian_encryption_panel_init (CcDroidianEncryptionPanel *self)
{
  g_resources_register (cc_droidian_encryption_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  self->cancellable = g_cancellable_new ();

  self->encryption_service = NULL;
  self->encryption_service_status = ENCRYPTION_SERVICE_STATUS_UNKNOWN;
  self->encryption_service_timeout = 0;
  self->passphrases_match = FALSE;

  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                            G_DBUS_PROXY_FLAGS_NONE,
                            NULL,
                            "org.droidian.EncryptionService",
                            "/Encryption",
                            "org.droidian.EncryptionService.Encryption",
                            self->cancellable,
                            (GAsyncReadyCallback) on_encryption_service_ready,
                            self);
}

CcDroidianEncryptionPanel *
cc_droidian_encryption_panel_new (void)
{
  return CC_DROIDIAN_ENCRYPTION_PANEL (g_object_new (CC_TYPE_DROIDIAN_ENCRYPTION_PANEL, NULL));
}
