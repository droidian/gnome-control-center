/*
 * Copyright (C) 2024 Cédric Bellegarde <cedric.bellegarde@adishatz.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <glib/gi18n.h>

#include "cc-waydroid-app-row.h"

/* signals */
enum
{
  APP_REMOVED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

const char* ROOT_APPLICATIONS[] = {
  "com.android.documentsui",
  "com.android.contacts",
  "com.android.camera2",
  "org.lineageos.recorder",
  "com.android.gallery3d",
  "org.lineageos.jelly",
  "org.lineageos.eleven",
  "org.lineageos.etar",
  "com.android.settings",
  "com.android.calculator2",
  "com.android.deskclock",
  "com.android.traceur",
  "com.android.inputmethod.latin"
};

struct _CcAppRow {
  AdwExpanderRow parent_instance;

  GtkWidget       *action_row;
  GtkWidget       *action_button;
  GtkWidget       *show_in_launcher_switch;
  GtkImage        *icon;
  GtkWidget       *spinner;

  char            *app_id;
  char            *name;
  char            *url;
  char            *filename;
  gboolean         installed;
  char            *apk_file;

  GCancellable    *cancellable;
};

G_DEFINE_TYPE (CcAppRow, cc_app_row, ADW_TYPE_EXPANDER_ROW)

static void
application_downloaded_cb (GObject      *source_object,
                           GAsyncResult *res,
                           gpointer      user_data);
static void
remove_application_dialog_cb (AdwAlertDialog *dialog,
                              GAsyncResult   *result,
                              gpointer        user_data);

static gboolean
row_set_app_info (CcAppRow *self)
{
  g_autofree char *desktop_id = g_strdup_printf ("waydroid.%s.desktop",
                                                 self->app_id);
  g_autoptr (GDesktopAppInfo) app_info = g_desktop_app_info_new (desktop_id);
  static uint count = 0;

  g_return_val_if_fail (!g_cancellable_is_cancelled (self->cancellable), FALSE);

  count += 1;

  gtk_widget_set_sensitive (self->show_in_launcher_switch, app_info != NULL);
  g_return_val_if_fail (app_info != NULL, count != 10);

  g_clear_pointer (&self->filename, g_free);
  self->filename = g_strdup (g_desktop_app_info_get_filename (app_info));

  gtk_widget_set_sensitive (self->show_in_launcher_switch,
                            self->filename != NULL);
  g_return_val_if_fail (self->filename != NULL, count != 10);

  count = 0;
  gtk_widget_set_sensitive (self->show_in_launcher_switch, TRUE);

  return FALSE;
}

static void
show_error (CcAppRow *self,
            gchar       *error)
{
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->action_row),
                                 error);
  gtk_spinner_stop (GTK_SPINNER (self->spinner));
  gtk_widget_set_sensitive (self->action_button, TRUE);
}

static void
action_child_exit (GPid     pid,
                   gint     wait_status,
                   gpointer user_data)
{
  CcAppRow *self = CC_APP_ROW (user_data);

  gtk_widget_set_sensitive (self->action_button, TRUE);
  gtk_spinner_stop (GTK_SPINNER (self->spinner));

  /* App has been installed, remove apk file */
  if (!self->installed) {
    g_autoptr (GFile) file = g_file_new_for_path (self->apk_file);
    g_file_delete_async (file, 0, self->cancellable, NULL, NULL);
    g_clear_pointer (&self->apk_file, g_free);
  }

  if (wait_status != 0) {
    if (self->installed) {
      adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->action_row),
                                     _("Error removing application"));
    } else {
      adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->action_row),
                                     _("Error installing application"));
    }
    return;
  }

  cc_app_row_set_installed (self, !self->installed);

  if (self->installed) {
    g_timeout_add (1000, (GSourceFunc) row_set_app_info , self);
  } else {
    gtk_widget_set_sensitive (self->show_in_launcher_switch, FALSE);
    g_signal_emit(self,
                  signals[APP_REMOVED],
                  0,
                  NULL);
    g_clear_pointer (&self->filename, g_free);
  }
}

static void
install_app_from_disk (CcAppRow *self)
{
  g_autoptr(GStrvBuilder) builder = NULL;
  g_auto(GStrv) argv = NULL;
  g_autoptr (GError) error = NULL;
  GPid child_pid;

  builder = g_strv_builder_new ();
  g_strv_builder_add_many (builder,
                           "/usr/bin/waydroid",
                           "app",
                           "install",
                           self->apk_file,
                           NULL);
  argv = g_strv_builder_end (builder);

  if (!g_spawn_async_with_pipes (NULL,
                                 argv,
                                 NULL,
                                 G_SPAWN_DO_NOT_REAP_CHILD,
                                 NULL,
                                 NULL,
                                 &child_pid,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &error)) {
    show_error (self, error->message);
    g_clear_pointer (&self->apk_file, g_free);
    return;
  }

  g_child_watch_add(child_pid, action_child_exit, self);
}

static void
uninstall_app_from_disk (CcAppRow *self)
{
  g_autoptr (GError) error = NULL;
  g_autoptr(GStrvBuilder) builder = NULL;
  g_auto(GStrv) argv = NULL;
  GPid child_pid;

  builder = g_strv_builder_new ();
  g_strv_builder_add_many (builder,
                           "/usr/bin/waydroid",
                           "app",
                           "remove",
                           self->app_id,
                           NULL);

  argv = g_strv_builder_end (builder);

  if (!g_spawn_async_with_pipes (NULL,
                                 argv,
                                 NULL,
                                 G_SPAWN_DO_NOT_REAP_CHILD,
                                 NULL,
                                 NULL,
                                 &child_pid,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &error)) {
    show_error (self, error->message);
    return;
  }

  g_child_watch_add(child_pid, action_child_exit, self);
}

static void
install_application (CcAppRow *self)
{
  g_autoptr (GFile) input_file  = NULL;
  g_autoptr (GFile) output_file = NULL;

  g_return_if_fail (self->url != NULL);

  input_file = g_file_new_for_uri (self->url);

  self->apk_file = g_strdup_printf ("%s/%s.apk",
                                    g_get_tmp_dir (),
                                    g_uuid_string_random ());
  output_file = g_file_new_for_path (self->apk_file);

  gtk_spinner_start (GTK_SPINNER (self->spinner));
  gtk_widget_set_sensitive (self->action_button, FALSE);

  g_file_copy_async (input_file,
                     output_file,
                     G_FILE_COPY_OVERWRITE,
                     0,
                     self->cancellable,
                     NULL,
                     NULL,
                     application_downloaded_cb,
                     self);
}

static void
uninstall_application (CcAppRow *self)
{
  AdwDialog *dialog = adw_alert_dialog_new (_("Remove application?"), NULL);

  adw_alert_dialog_format_body (ADW_ALERT_DIALOG (dialog),
                                _("This will remove %s"),
                                self->name);

  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog),
                                  "cancel",  _("_Cancel"),
                                  "remove", _("_Remove"),
                                  NULL);

  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog),
                                            "remove",
                                            ADW_RESPONSE_DESTRUCTIVE);

  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "cancel");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "cancel");

  adw_alert_dialog_choose (ADW_ALERT_DIALOG (dialog), GTK_WIDGET (self), NULL,
                           (GAsyncReadyCallback) remove_application_dialog_cb,
                           self);

  adw_dialog_present (dialog, NULL);
}

static void
application_downloaded_cb (GObject      *source_object,
                           GAsyncResult *res,
                           gpointer      user_data)
{
  CcAppRow *self = CC_APP_ROW (user_data);
  g_autoptr (GError) error = NULL;
  gboolean success;

  success = g_file_copy_finish (G_FILE (source_object),
                                res,
                                &error);

  if (!success) {
    show_error (self, error->message);
    g_clear_pointer (&self->apk_file, g_free);
    return;
  }

  install_app_from_disk (self);
}

static void
show_in_launcher_active_cb (CcAppRow *self)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GKeyFile) desktop_file = g_key_file_new ();
  gboolean active = gtk_switch_get_active (GTK_SWITCH (self->show_in_launcher_switch));


  g_return_if_fail (self->filename != NULL);

  g_return_if_fail (g_key_file_load_from_file (desktop_file,
                                               self->filename,
                                               G_KEY_FILE_KEEP_COMMENTS |
                                               G_KEY_FILE_KEEP_TRANSLATIONS,
                                               NULL));

  g_key_file_set_boolean (desktop_file,
                          G_KEY_FILE_DESKTOP_GROUP,
                          G_KEY_FILE_DESKTOP_KEY_NO_DISPLAY,
                          !active);
  g_key_file_save_to_file (desktop_file, self->filename, &error);

  if (error != NULL) {
      g_warning ("%s", error->message);
  }
}

static void
remove_application_dialog_cb (AdwAlertDialog *dialog,
                              GAsyncResult   *result,
                              gpointer        user_data)

{
  CcAppRow *self = CC_APP_ROW (user_data);
  const char *response = adw_alert_dialog_choose_finish (dialog, result);

  if (g_strcmp0 (response, "remove") == 0) {
    uninstall_app_from_disk (self);
  }
}

static void
action_button_clicked_cb (CcAppRow *self)
{
  if (self->installed)
    uninstall_application (self);
  else
    install_application (self);
}

static void
cc_app_row_dispose (GObject *object)
{
  G_OBJECT_CLASS (cc_app_row_parent_class)->dispose (object);
}

static void
cc_app_row_finalize (GObject *object)
{
  CcAppRow *self = CC_APP_ROW (object);

  g_clear_pointer (&self->app_id, g_free);
  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->url, g_free);
  g_clear_pointer (&self->filename, g_free);
  g_clear_pointer (&self->apk_file, g_free);

  g_cancellable_cancel (self->cancellable);
  g_object_unref (self->cancellable);

  G_OBJECT_CLASS (cc_app_row_parent_class)->finalize (object);
}

static void
cc_app_row_class_init (CcAppRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = cc_app_row_dispose;
  object_class->finalize = cc_app_row_finalize;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/control-center/waydroid/cc-waydroid-app-row.ui");

  gtk_widget_class_bind_template_child (widget_class, CcAppRow, icon);
  gtk_widget_class_bind_template_child (widget_class, CcAppRow, spinner);
  gtk_widget_class_bind_template_child (widget_class, CcAppRow, action_row);
  gtk_widget_class_bind_template_child (widget_class, CcAppRow, action_button);
  gtk_widget_class_bind_template_child (widget_class, CcAppRow, show_in_launcher_switch);

  gtk_widget_class_bind_template_callback (widget_class,
                                           show_in_launcher_active_cb);
  gtk_widget_class_bind_template_callback (widget_class,
                                           action_button_clicked_cb);

  signals[APP_REMOVED] = g_signal_new (
        "app-removed",
        G_OBJECT_CLASS_TYPE (object_class),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL, NULL,
        G_TYPE_NONE,
        0,
        G_TYPE_NONE
    );
}

static void
cc_app_row_init (CcAppRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->apk_file = NULL;
  self->cancellable = g_cancellable_new ();
}

GtkWidget*
cc_app_row_new (GDesktopAppInfo *app_info)
{
  CcAppRow *self;
  g_autoptr (GKeyFile) desktop_file = g_key_file_new ();
  g_autofree char *name = NULL;
  g_autofree char *icon_file = NULL;
  g_autofree char *no_display = NULL;
  GString *exec = NULL;
  const char *filename = g_desktop_app_info_get_filename (app_info);

  g_return_val_if_fail (filename != NULL, NULL);

  g_return_val_if_fail (g_key_file_load_from_file (desktop_file,
                                                   filename,
                                                   G_KEY_FILE_KEEP_COMMENTS |
                                                   G_KEY_FILE_KEEP_TRANSLATIONS,
                                                   NULL), NULL);

  name = g_key_file_get_string (desktop_file,
                                G_KEY_FILE_DESKTOP_GROUP,
                                G_KEY_FILE_DESKTOP_KEY_NAME,
                                NULL);
  g_return_val_if_fail (name != NULL, NULL);

  icon_file = g_key_file_get_string (desktop_file,
                                     G_KEY_FILE_DESKTOP_GROUP,
                                     G_KEY_FILE_DESKTOP_KEY_ICON,
                                     NULL);
  g_return_val_if_fail (icon_file != NULL, NULL);

  no_display = g_key_file_get_string (desktop_file,
                                      G_KEY_FILE_DESKTOP_GROUP,
                                      G_KEY_FILE_DESKTOP_KEY_NO_DISPLAY,
                                      NULL);
  g_return_val_if_fail (no_display != NULL, NULL);

  self = CC_APP_ROW (g_object_new (CC_TYPE_APP_ROW, NULL));
  self->name = g_strdup (name);
  self->filename = g_strdup (filename);

  cc_app_row_set_installed (self, TRUE);

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self), name);
  gtk_image_set_from_file (self->icon, icon_file);

  g_signal_handlers_block_by_func(self->show_in_launcher_switch,
                                  show_in_launcher_active_cb,
                                  self);

  gtk_switch_set_active (GTK_SWITCH (self->show_in_launcher_switch),
                         g_strcmp0 (no_display, "true") != 0);

  g_signal_handlers_unblock_by_func(self->show_in_launcher_switch,
                                    show_in_launcher_active_cb,
                                    self);

  exec = g_string_new (g_key_file_get_string (desktop_file,
                                              G_KEY_FILE_DESKTOP_GROUP,
                                              G_KEY_FILE_DESKTOP_KEY_EXEC,
                                              NULL));
  g_string_replace (exec, "waydroid app launch ", "", 0);
  self->app_id = g_string_free_and_steal (exec);

  if (cc_app_row_is_root (self)) {
    gtk_widget_set_sensitive (self->action_button, FALSE);
  }

  self->url = NULL;

  return GTK_WIDGET (self);
}

GtkWidget* cc_app_row_new_for_app_id (const char *name,
                                      const char *app_id,
                                      const char *url)
{
  CcAppRow *self = CC_APP_ROW (g_object_new (CC_TYPE_APP_ROW, NULL));

  self->name = g_strdup (name);
  self->app_id = g_strdup (app_id);
  self->url = g_strdup (url);
  self->filename = NULL;

  row_set_app_info (self);

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self), name);
  gtk_image_set_from_icon_name (self->icon, name);

  return GTK_WIDGET (self);
}

const char*
cc_app_row_get_app_id (CcAppRow *self)
{
    return self->app_id;
}

gboolean
cc_app_row_is_root (CcAppRow *self)
{
  guint root_count = sizeof (ROOT_APPLICATIONS) / sizeof (char*);

  for (unsigned int i = 0; i < root_count; i++) {
    if (g_strcmp0 (self->app_id, ROOT_APPLICATIONS[i]) == 0)
      return TRUE;
  }
  return FALSE;
}

gboolean
cc_app_row_can_be_downloaded (CcAppRow *self)
{
  return self->url != NULL;
}

void
cc_app_row_set_installed (CcAppRow *self,
                          gboolean  installed)
{
  self->installed = installed;

  if (self->installed) {
    const char *classes[] = {"destructive-action", NULL};

    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->action_row),
                                   _("Remove application"));
    gtk_button_set_label (GTK_BUTTON (self->action_button),
                          _("Uninstall"));
    gtk_widget_set_css_classes (self->action_button, classes);
  } else {
    const char *classes[] = {"suggested-action", NULL};

    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->action_row),
                                   _("Add application"));
    gtk_button_set_label (GTK_BUTTON (self->action_button),
                          _("Install"));
    gtk_widget_set_css_classes (self->action_button, classes);
  }
}