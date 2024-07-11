/*
 * Copyright (C) 2024 Cédric Bellegarde <cedric.bellegarde@adishatz.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-fingerprint-panel.h"
#include "cc-fingerprint-resources.h"
#include "cc-util.h"

#include <adwaita.h>
#include <glib/gi18n.h>
#include <gdk/gdk.h>

#define MAX_FINGERPRINTS 5

#define GFOREACH(list, item) \
    for(GList *__glist = list; \
        __glist && (item = __glist->data, TRUE); \
        __glist = __glist->next)

#define FINGERPRINT_DBUS_NAME         "org.droidian.fingerprint"
#define FINGERPRINT_DBUS_PATH         "/org/droidian/fingerprint"
#define FINGERPRINT_DBUS_INTERFACE    "org.droidian.fingerprint"

struct _CcFingerprintPanel {
  CcPanel            parent;

  AdwToastOverlay   *toast_overlay;
  AdwExpanderRow    *fingerprints_row;
  AdwEntryRow       *fingerprint_entry;
  GtkButton         *fingerprint_new_button;
  GtkImage          *fingerprint_image;
  GtkProgressBar    *progressbar;

  GList *fingerprints;

  GDBusProxy *fingerprint_proxy;
  GCancellable *cancellable;
  gint enroll_progress;
};

G_DEFINE_TYPE (CcFingerprintPanel, cc_fingerprint_panel, CC_TYPE_PANEL)

static void
handle_max_fingerprints (CcFingerprintPanel *self)
{
  if (g_list_length (self->fingerprints) >= MAX_FINGERPRINTS)
    {
      gtk_widget_set_sensitive (GTK_WIDGET (self->fingerprint_entry), FALSE);
      adw_preferences_row_set_title (
                                  ADW_PREFERENCES_ROW (self->fingerprint_entry),
                                  _("Please remove some fingerprints"));
    }
  else
    {
      gtk_widget_set_sensitive (GTK_WIDGET (self->fingerprint_entry), TRUE);
      adw_preferences_row_set_title (
                                  ADW_PREFERENCES_ROW (self->fingerprint_entry),
                                  _("Fingerprint name"));
    }
}

static gboolean
stop_enroll_animation (CcFingerprintPanel *self)
{
  gdouble opacity = gtk_widget_get_opacity (
                                    GTK_WIDGET (self->fingerprint_image)) - 0.1;

  if (self->enroll_progress == 0 || opacity <= self->enroll_progress / 150.0)
    return FALSE;

  gtk_widget_set_opacity (GTK_WIDGET (self->fingerprint_image), opacity);

  return TRUE;
}

static gboolean
start_enroll_animation (CcFingerprintPanel *self)
{
  gdouble opacity = gtk_widget_get_opacity (
                                          GTK_WIDGET (self->fingerprint_image));

  if (self->enroll_progress == 0)
    {
      return FALSE;
    }
  else if (opacity >= 1.0)
    {
      g_timeout_add (25, (GSourceFunc) stop_enroll_animation, self);
      return FALSE;
    }

  gtk_widget_set_opacity (GTK_WIDGET (self->fingerprint_image), opacity + 0.1);

  return TRUE;
}

static void
remove_fingerprint_row (CcFingerprintPanel *self,
                        const gchar        *fingerprint)
{
  GtkWidget *widget;
  const gchar *title;

  GFOREACH (self->fingerprints, widget)
    {
      title = adw_preferences_row_get_title (ADW_PREFERENCES_ROW (widget));
      if (g_strcmp0 (title, fingerprint) == 0)
        {
          self->fingerprints = g_list_remove (self->fingerprints, widget);
          adw_expander_row_remove (self->fingerprints_row, widget);
          break;
        }
    }

  gtk_widget_set_sensitive (GTK_WIDGET (self->fingerprints_row),
                            g_list_length (self->fingerprints) > 0);
  handle_max_fingerprints (self);
}

static void
remove_fingerprint_cb (GObject      *source_object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  g_autoptr(GVariant) value = NULL;
  g_autoptr(GError) error = NULL;
  gint ret;

  value = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object),
                                    res, &error);

  g_return_if_fail (value != NULL);

  g_variant_get (value, "(i)", &ret);

  if (ret < 0)
    {
      g_warning ("Failed to remove fingerprint: %s", error->message);
      return;
    }
}

static void
remove_button_clicked_cb (GtkWidget *button,
                          gpointer   user_data)
{
    CcFingerprintPanel *self = (CcFingerprintPanel *) user_data;
    const gchar *fingerprint = gtk_widget_get_name (button);

    g_dbus_proxy_call (
        self->fingerprint_proxy,
        "Remove",
        g_variant_new("(s)", fingerprint),
        G_DBUS_CALL_FLAGS_NONE,
        5000,
        self->cancellable,
        remove_fingerprint_cb,
        self);
}

static void
add_fingerprint_row (CcFingerprintPanel *self,
                     const gchar        *fingerprint)
{
  GtkWidget *action_row = adw_action_row_new ();
  GtkWidget *button = gtk_button_new_from_icon_name ("user-trash-symbolic");

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (action_row), fingerprint);

  gtk_widget_set_halign (button, GTK_ALIGN_END);
  gtk_widget_set_name (button, fingerprint);
  gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request (button, 92, -1);
  gtk_widget_add_css_class (button, "destructive-action");
  g_signal_connect (button, "clicked",
                    (GCallback) remove_button_clicked_cb, self);

  adw_action_row_add_suffix (ADW_ACTION_ROW (action_row), button);
  self->fingerprints = g_list_append (self->fingerprints, action_row);

  adw_expander_row_add_row (self->fingerprints_row, action_row);

  gtk_widget_set_sensitive (GTK_WIDGET (self->fingerprints_row), TRUE);
  handle_max_fingerprints (self);
}

static void
get_fingerprints_cb (GObject      *source_object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  CcFingerprintPanel *self = (CcFingerprintPanel *) user_data;

  g_autoptr(GVariant) value = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr (GVariantIter) iter = NULL;
  const gchar *fingerprint;

  gtk_widget_set_sensitive (GTK_WIDGET (self->fingerprints_row), FALSE);

  value = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object),
                                    res, &error);

  g_variant_get (value, "(as)", &iter);
  while (g_variant_iter_loop (iter, "&s", &fingerprint))
    {
      add_fingerprint_row (self, fingerprint);
    }
}

static void
set_widgets_state (CcFingerprintPanel *self,
                   gboolean            enroll_active)
{
  if (enroll_active)
    {
      gtk_widget_set_opacity (GTK_WIDGET (self->fingerprint_image), 0.1);
      gtk_widget_set_opacity (GTK_WIDGET (self->progressbar), 1.0);
      gtk_widget_add_css_class (GTK_WIDGET (self->fingerprint_image),
                                "fingerprint-enroll");

      gtk_widget_add_css_class (GTK_WIDGET (self->fingerprint_new_button),
                                "destructive-action");
      gtk_widget_remove_css_class (GTK_WIDGET (self->fingerprint_new_button),
                                   "suggested-action");
      gtk_button_set_label(self->fingerprint_new_button, _("Cancel"));
      adw_expander_row_set_expanded (self->fingerprints_row, FALSE);
      gtk_widget_set_sensitive (GTK_WIDGET (self->fingerprints_row), FALSE);
      gtk_widget_set_sensitive (GTK_WIDGET (self->fingerprint_entry), FALSE);
    }
  else
    {
      self->enroll_progress = 0;
      gtk_progress_bar_set_fraction (self->progressbar, 0.0);
      gtk_widget_set_opacity (GTK_WIDGET (self->progressbar), 0.0);
      gtk_widget_set_opacity (GTK_WIDGET (self->fingerprint_image), 1.0);
      gtk_widget_remove_css_class (GTK_WIDGET (self->fingerprint_image),
                                   "fingerprint-enroll");
      gtk_widget_add_css_class (GTK_WIDGET (self->fingerprint_new_button),
                                   "suggested-action");
      gtk_widget_remove_css_class (GTK_WIDGET (self->fingerprint_new_button),
                                   "destructive-action");
      gtk_button_set_label(self->fingerprint_new_button, _("New fingerprint"));
      gtk_widget_set_sensitive (GTK_WIDGET (self->fingerprints_row),
                                g_list_length (self->fingerprints) > 0);
      handle_max_fingerprints (self);
    }
}

static void
enroll_fingerprint_cb (GObject      *source_object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  g_autoptr(GVariant) value = NULL;
  g_autoptr(GError) error = NULL;
  gint ret;

  value = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object),
                                    res, &error);

  g_return_if_fail (value != NULL);

  g_variant_get (value, "(i)", &ret);

  if (ret < 0)
    {
      g_warning ("Failed to enroll fingerprint: %s", error->message);
      return;
    }
}

static void
enroll_cancel_cb (GObject      *source_object,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  CcFingerprintPanel *self = (CcFingerprintPanel *)user_data;
  g_autoptr(GVariant) value = NULL;
  g_autoptr(GError) error = NULL;
  gint ret;

  value = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object),
                                    res, &error);

  g_return_if_fail (value != NULL);

  g_variant_get (value, "(i)", &ret);
  if (ret < 0)
    {
      g_warning ("Failed to cancel enroll: %s", error->message);
      return;
    }

  set_widgets_state (self, FALSE);
}

static void
enroll_fingerprint (CcFingerprintPanel *self, gboolean enroll)
{
  if (enroll)
    {
      const gchar *fingerprint = gtk_editable_get_text (
                                    GTK_EDITABLE (self->fingerprint_entry));

      g_dbus_proxy_call (self->fingerprint_proxy,
                         "Enroll",
                         g_variant_new("(s)", fingerprint),
                         G_DBUS_CALL_FLAGS_NONE,
                         5000,
                         self->cancellable,
                         enroll_fingerprint_cb,
                         self);
    }
  else
    {
        g_dbus_proxy_call (self->fingerprint_proxy,
                           "Abort",
                           g_variant_new ("(s)", "Cancelled"),
                           G_DBUS_CALL_FLAGS_NONE,
                           5000,
                           self->cancellable,
                           enroll_cancel_cb,
                           self);
    }
}

static void
fingerprint_name_cb (CcFingerprintPanel *self)
{
  gtk_widget_set_sensitive (GTK_WIDGET (self->fingerprint_new_button),
                            adw_entry_row_get_text_length (
                              self->fingerprint_entry) > 0);
}

static void
fingerprint_new_button_clicked_cb (CcFingerprintPanel *self)
{
  gboolean enroll = gtk_widget_has_css_class (
                            GTK_WIDGET (self->fingerprint_new_button),
                            "suggested-action");

  set_widgets_state (self, enroll);

  enroll_fingerprint (self, enroll);
}

static void
fingerprint_proxy_signal_cb (GDBusProxy  *proxy,
                             const gchar *sender_name,
                             const gchar *signal_name,
                             GVariant    *parameters,
                             gpointer     user_data)
{
  CcFingerprintPanel *self = (CcFingerprintPanel *) user_data;

  if (g_strcmp0 (signal_name, "EnrollProgressChanged") == 0)
    {
      g_variant_get (parameters, "(i)", &self->enroll_progress);
      if (self->enroll_progress > 0)
        {
          g_timeout_add (25, (GSourceFunc) start_enroll_animation, self);
          gtk_progress_bar_set_fraction (self->progressbar,
                                         self->enroll_progress / 100.0);
        }
    }
  else if (g_strcmp0 (signal_name, "Added") == 0)
    {
      AdwToast *toast = adw_toast_new (_("Fingerprint added"));
      const gchar *fingerprint_name;

      g_variant_get (parameters, "(&s)", &fingerprint_name);

      add_fingerprint_row (self, fingerprint_name);
      adw_toast_overlay_add_toast (self->toast_overlay, toast);
      gtk_editable_set_text (GTK_EDITABLE (self->fingerprint_entry), "");
      set_widgets_state (self, FALSE);
    }
  else if (g_strcmp0 (signal_name, "Removed") == 0)
    {
      const gchar *fingerprint_name;

      g_variant_get (parameters, "(&s)", &fingerprint_name);
      remove_fingerprint_row (self, fingerprint_name);
    }

}

static void
cc_fingerprint_panel_dispose (GObject *object)
{
  CcFingerprintPanel *self = (CcFingerprintPanel *) object;

  if (self->fingerprint_proxy != NULL)
    g_dbus_proxy_call_sync (self->fingerprint_proxy,
                            "Abort",
                            g_variant_new ("(s)", "Quit"),
                            G_DBUS_CALL_FLAGS_NONE,
                            5000,
                            self->cancellable,
                            NULL);

  g_clear_object (&self->fingerprint_proxy);
  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (cc_fingerprint_panel_parent_class)->dispose (object);
}

static void
cc_fingerprint_panel_finalize (GObject *object)
{
  CcFingerprintPanel *self = (CcFingerprintPanel *) object;
  g_cancellable_cancel (self->cancellable);

  g_list_free (self->fingerprints);

  G_OBJECT_CLASS (cc_fingerprint_panel_parent_class)->finalize (object);
}

static void
cc_fingerprint_panel_class_init (CcFingerprintPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = cc_fingerprint_panel_dispose;
  object_class->finalize = cc_fingerprint_panel_finalize;

  gtk_widget_class_set_template_from_resource (
              widget_class,
              "/org/gnome/control-center/fingerprint/cc-fingerprint-panel.ui");

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        toast_overlay);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        fingerprints_row);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        fingerprint_entry);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        fingerprint_new_button);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        fingerprint_image);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        progressbar);
  gtk_widget_class_bind_template_callback (widget_class,
                                           fingerprint_name_cb);
  gtk_widget_class_bind_template_callback (widget_class,
                                           fingerprint_new_button_clicked_cb);
}

static void
cc_fingerprint_panel_init (CcFingerprintPanel *self)
{
  g_autoptr (GError) error = NULL;
  g_autoptr(GtkCssProvider) provider = NULL;

  provider = gtk_css_provider_new ();

  g_resources_register (cc_fingerprint_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));
  gtk_css_provider_load_from_resource (
              provider,
              "/org/gnome/control-center/fingerprint/cc-fingerprint-panel.css");
  gtk_style_context_add_provider_for_display (
                                      gdk_display_get_default (),
                                      GTK_STYLE_PROVIDER (provider),
                                      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  self->fingerprints = NULL;

  self->cancellable = g_cancellable_new ();

  self->fingerprint_proxy = g_dbus_proxy_new_for_bus_sync (
                                                    G_BUS_TYPE_SYSTEM,
                                                    0,
                                                    NULL,
                                                    FINGERPRINT_DBUS_NAME,
                                                    FINGERPRINT_DBUS_PATH,
                                                    FINGERPRINT_DBUS_INTERFACE,
                                                    self->cancellable,
                                                    &error);

  if (error != NULL)
    {
      g_warning("Can't connect to %s: %s", FINGERPRINT_DBUS_NAME, error->message);
      g_clear_object (&self->fingerprint_proxy);
      return;
    }

  g_dbus_proxy_call (
        self->fingerprint_proxy,
        "GetAll",
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        5000,
        self->cancellable,
        get_fingerprints_cb,
        self);

    g_signal_connect (
        self->fingerprint_proxy,
        "g-signal",
        G_CALLBACK (fingerprint_proxy_signal_cb),
        self
    );
}

CcFingerprintPanel *
cc_fingerprint_panel_new (void)
{
  return CC_FINGERPRINT_PANEL (g_object_new (CC_TYPE_FINGERPRINT_PANEL, NULL));
}
