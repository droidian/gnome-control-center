/*
 * Copyright (C) 2024 Cédric Bellegarde <cedric.bellegarde@adishatz.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-waydroid-panel.h"
#include "cc-waydroid-resources.h"
#include "cc-waydroid-app-row.h"
#include "cc-util.h"

#include <adwaita.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>
#include <unistd.h>
#include <packagekit-glib2/packagekit.h>

#define WAYDROID_DBUS_NAME          "id.waydro.Container"
#define WAYDROID_DBUS_PATH          "/ContainerManager"
#define WAYDROID_DBUS_INTERFACE     "id.waydro.ContainerManager"

#define SYSTEMD_DBUS_NAME       "org.freedesktop.systemd1"
#define SYSTEMD_DBUS_PATH       "/org/freedesktop/systemd1"
#define SYSTEMD_DBUS_INTERFACE  "org.freedesktop.systemd1.Manager"

#define FDROID_NAME "F-Droid"
#define FDROID_URL "https://f-droid.org/F-Droid.apk"
#define FDROID_APPID "org.fdroid.fdroid"

#define GFOREACH(list, item) \
  for(GList *__glist = list; \
    __glist && (item = __glist->data, TRUE); \
    __glist = __glist->next)

#define GFOREACH_SUB(list, item) \
  for(GList *__glist_sub = list; \
    __glist_sub && (item = __glist_sub->data, TRUE); \
    __glist_sub = __glist_sub->next)

struct IOWatchProp {
  CcWaydroidPanel *panel;
  const gchar           *prop;
};

struct _CcWaydroidPanel {
  CcPanel           parent;

  GDBusProxy       *waydroid_proxy;

  GtkWidget        *stack;
  GtkWidget        *install_box;
  GtkWidget        *install_status_page;
  GtkWidget        *install_waydroid_button;
  GtkWidget        *install_waydroid_spinner;
  GtkWidget        *enable_waydroid_switch;
  GtkWidget        *enable_waydroid_spinner;
  GtkWidget        *waydroid_settings;
  GtkWidget        *reset_settings;
  GtkWidget        *setting_uevent_switch;
  GtkWidget        *setting_suspend_switch;
  GtkWidget        *setting_shared_folder_switch;
  GtkWidget        *setting_notifications_switch;
  GtkWidget        *android_applications;

  GList            *application_rows;
  GList            *installed_applications;

  GDBusProxy       *systemd_proxy;

  GCancellable     *cancellable;

  gulong            focus_id;
};

G_DEFINE_TYPE (CcWaydroidPanel, cc_waydroid_panel, CC_TYPE_PANEL)

struct MaskServiceParams {
  CcWaydroidPanel *self;
  const char *action;
};

struct ApplicationsData {
  CcWaydroidPanel *self;
  GList *applications;
};

static void
waydroid_resolved_cb (GObject      *object,
                      GAsyncResult *res,
                      gpointer      user_data);
static void
waydroid_get_session_cb (GObject      *object,
                         GAsyncResult *res,
                         gpointer      data);
static void
setting_uevent_active_cb (CcWaydroidPanel *self);
static void
setting_suspend_active_cb (CcWaydroidPanel *self);
static void
setting_shared_folder_active_cb (CcWaydroidPanel *self);
static void
setting_notifications_active_cb (CcWaydroidPanel *self);
static void
enable_waydroid_active_cb (CcWaydroidPanel *self);
static void
app_removed_cb (CcAppRow *row,
                gpointer  user_data);
static void
start_stop_service_cb (GObject      *object,
                       GAsyncResult *res,
                       gpointer      user_data);
static void
mask_service_cb  (GObject      *object,
                  GAsyncResult *res,
                  gpointer      user_data);
static void
reload_service_cb  (GObject      *object,
                    GAsyncResult *res,
                    gpointer      user_data);

static void
reload_service (gpointer user_data)
{
  struct MaskServiceParams *mask_service_params = user_data;

  g_dbus_proxy_call (mask_service_params->self->systemd_proxy,
                     "Reload",
                     NULL,
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     mask_service_params->self->cancellable,
                     reload_service_cb,
                     user_data);
}

static void
start_service (CcWaydroidPanel *self,
               const char      *service)
{
  g_dbus_proxy_call (self->systemd_proxy,
                     "StartUnit",
                     g_variant_new ("(&s&s)", service, "replace"),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     self->cancellable,
                     start_stop_service_cb,
                     self);
}

static void
stop_service (CcWaydroidPanel *self,
              const char      *service)
{
  g_dbus_proxy_call (self->systemd_proxy,
                     "StopUnit",
                     g_variant_new ("(&s&s)", service, "replace"),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     self->cancellable,
                     start_stop_service_cb,
                     self);
}

static void
mask_notifications_service (CcWaydroidPanel *self,
                            gboolean         mask)
{
  GVariantBuilder builder;
  struct MaskServiceParams *mask_service_params = g_new0 (struct MaskServiceParams, 1);

  mask_service_params->self = self;
  if (mask)
    mask_service_params->action = "MaskUnitFiles";
  else
    mask_service_params->action = "UnmaskUnitFiles";

  g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
  g_variant_builder_add(&builder, "s", "waydroid-notification-client.service");

  g_dbus_proxy_call (self->systemd_proxy,
                     mask_service_params->action,
                     mask ? g_variant_new ("(asbb)", &builder, FALSE, FALSE) :
                            g_variant_new ("(asb)", &builder, FALSE),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     self->cancellable,
                     mask_service_cb,
                     mask_service_params);
}

static int
sort_applications (gconstpointer a,
                   gconstpointer b)
{
  GDesktopAppInfo *app_info_a = G_DESKTOP_APP_INFO (a);
  GDesktopAppInfo *app_info_b = G_DESKTOP_APP_INFO (b);
  g_autofree char *name_a = g_desktop_app_info_get_string (app_info_a, "Name");
  g_autofree char *name_b = g_desktop_app_info_get_string (app_info_b, "Name");

  return strcmp (name_a, name_b);
}

static void
add_application (GDesktopAppInfo *app_info, gpointer user_data)
{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (user_data);
  GtkWidget *new_row = cc_app_row_new (app_info);
  GtkWidget *row;
  gboolean exists = FALSE;

  g_return_if_fail (new_row != NULL);

  GFOREACH (self->application_rows, row) {
    if (g_strcmp0 (cc_app_row_get_app_id (CC_APP_ROW (row)),
                   cc_app_row_get_app_id (CC_APP_ROW (new_row))) == 0) {
      exists = TRUE;
      break;
    }
  }

  if (!exists) {
    self->application_rows = g_list_append (self->application_rows, new_row);
    adw_preferences_group_add (ADW_PREFERENCES_GROUP (self->android_applications),
                               new_row);
    g_signal_connect (new_row, "app-removed", G_CALLBACK (app_removed_cb), self);
  }
}

static gboolean
add_applications (gpointer user_data)
{
  struct ApplicationsData *data = user_data;
  GList *first = g_list_first (data->applications);

  if (data->self->cancellable == NULL) {
    goto out;
  }

  add_application (first->data, data->self);

  data->applications = g_list_remove_link (data->applications, first);

  if (g_list_length (data->applications) != 0 && !g_cancellable_is_cancelled (data->self->cancellable)) {
    g_idle_add ((GSourceFunc) add_applications, data);
    return FALSE;
  }

out:
  g_list_free_full (data->applications, g_object_unref);
  g_free (data);
  return FALSE;
}

static void
check_available_apps (CcWaydroidPanel *self)
{
  gchar ***apps = g_desktop_app_info_search ("waydroid");
  struct ApplicationsData *data = g_new0 (struct ApplicationsData, 1);
  unsigned int i = 0;

  data->self = self;
  data->applications = NULL;

  while (apps[i] != NULL) {
    for (unsigned int h = 0; h < g_strv_length (apps[i]); h++) {
      GDesktopAppInfo *app_info = g_desktop_app_info_new (apps[i][h]);
      g_autofree char *exec = NULL;

      g_return_if_fail (app_info != NULL);

      exec = g_desktop_app_info_get_string (app_info, "Exec");

      /* TODO: Fix this as soon we add another installable app */
      if (g_strrstr (exec, FDROID_APPID) != NULL)
        continue;

      if (g_strrstr (exec, "waydroid app launch ") != NULL) {
        data->applications = g_list_insert_sorted (data->applications,
                                                   app_info,
                                                   sort_applications);
      }
    }
    g_strfreev (apps[i]);
    i += 1;
  }

  g_idle_add ((GSourceFunc) add_applications, data);

  g_free (apps);
}

static void
set_enable_waydroid_switch (CcWaydroidPanel *self, gboolean active)
{
  g_signal_handlers_block_by_func(self->enable_waydroid_switch,
                                  enable_waydroid_active_cb,
                                  self);
  gtk_switch_set_active (GTK_SWITCH (self->enable_waydroid_switch), active);
  g_signal_handlers_unblock_by_func(self->enable_waydroid_switch,
                                    enable_waydroid_active_cb,
                                    self);
}

static void
handle_waydroid_package (CcWaydroidPanel *self,
                         GPtrArray *packages)
{
  for (unsigned int i = 0; i < packages->len; i++) {
    PkPackage *package = packages->pdata[i];
    PkInfoEnum info = pk_package_get_info (package);

    if ((info & PK_INFO_ENUM_INSTALLED) == PK_INFO_ENUM_INSTALLED) {
      gtk_widget_set_sensitive (self->enable_waydroid_switch, TRUE);
      check_available_apps (self);
      gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "configure");
    } else {
      gtk_widget_set_name (self->install_waydroid_button,
                           pk_package_get_id (package));
      gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "install");
    }
  }
}

static GList*
get_installed_rows (CcWaydroidPanel *self)
{
  GList *installed_rows = NULL;
  GtkWidget *row;

  GFOREACH (self->application_rows, row) {
    const gchar *app_id = cc_app_row_get_app_id (CC_APP_ROW (row));
    gboolean is_root_application = cc_app_row_is_root (CC_APP_ROW (row));
    const gchar *installed_app_id;

    if (is_root_application)
      continue;

    GFOREACH_SUB (self->installed_applications, installed_app_id) {
      if (g_strrstr(installed_app_id, app_id) != NULL) {
        installed_rows = g_list_append (installed_rows, row);
        break;
      }
    }
  }

  return installed_rows;
}

static GList*
get_removed_rows (CcWaydroidPanel *self)
{
  GList *installed_rows = get_installed_rows (self);
  GList *removed_rows = NULL;
  GtkWidget *row;

  GFOREACH (self->application_rows, row) {
    if (g_list_find (removed_rows, row) == NULL &&
        !cc_app_row_can_be_downloaded (CC_APP_ROW (row))) {
      removed_rows = g_list_append (removed_rows, row);
    }
  }

  g_list_free (installed_rows);

  return removed_rows;
}

static void
remove_rows (CcWaydroidPanel *self,
             GList           *rows)
{
  GtkWidget *row;

  GFOREACH (rows, row) {
    self->application_rows = g_list_remove (self->application_rows,
                                            row);
    adw_preferences_group_remove (ADW_PREFERENCES_GROUP (self->android_applications),
                                  row);
  }
}

static void
start_stop_service_cb (GObject      *object,
               GAsyncResult *res,
               gpointer      user_data)
{
  GDBusProxy *proxy = G_DBUS_PROXY (object);
  g_autoptr (GVariant) result = NULL;
  g_autoptr (GError) error = NULL;

  result = g_dbus_proxy_call_finish (proxy, res, &error);

  if (error != NULL) {
    g_warning (error->message);
  }
}

static void
mask_service_cb (GObject      *object,
                 GAsyncResult *res,
                 gpointer      user_data)
{
  GDBusProxy *proxy = G_DBUS_PROXY (object);
  g_autoptr (GVariant) result = NULL;
  g_autoptr (GError) error = NULL;

  result = g_dbus_proxy_call_finish (proxy, res, &error);

  if (error != NULL) {
    g_warning (error->message);
    g_free (user_data);
  } else {
    reload_service (user_data);
  }
}

static void
reload_service_cb (GObject      *object,
                   GAsyncResult *res,
                   gpointer      user_data)
{
  GDBusProxy *proxy = G_DBUS_PROXY (object);
  struct MaskServiceParams *mask_service_params = user_data;

  g_autoptr (GVariant) result = NULL;
  g_autoptr (GError) error = NULL;

  result = g_dbus_proxy_call_finish (proxy, res, &error);

  if (error != NULL) {
    g_warning (error->message);
  } else if (g_strcmp0 (mask_service_params->action, "MaskUnitFiles") == 0) {
    stop_service (mask_service_params->self, "waydroid-notification-client.service");
  } else {
    start_service (mask_service_params->self, "waydroid-notification-client.service");
  }

  g_free (user_data);
}

static void
data_reset_cb (GPid     pid,
               gint     wait_status,
               gpointer user_data)
{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (user_data);
  GList *removed_rows = get_removed_rows (self);

  remove_rows (self, removed_rows);

  g_list_free (removed_rows);
}

static void
installed_apps_cb (GPid     pid,
                   gint     wait_status,
                   gpointer user_data)
{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (user_data);
  GList *installed_rows = NULL;
  GtkWidget *row;

  g_return_if_fail (self->cancellable != NULL);

  installed_rows = get_installed_rows (self);

  GFOREACH (self->application_rows, row) {
    gboolean installed = g_list_find (installed_rows, row) != NULL;

    cc_app_row_set_installed (CC_APP_ROW (row), installed);
  }

  g_list_free (installed_rows);
}

static gboolean
handle_application_list (GIOChannel   *source,
                         GIOCondition  condition,
                         gpointer      user_data)
{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (user_data);
  g_autofree char *line = NULL;
  gsize  size;

  g_return_val_if_fail (self->cancellable != NULL, FALSE);

  g_return_val_if_fail (g_io_channel_read_line (source, &line, &size, NULL, NULL) == G_IO_STATUS_NORMAL,
                        FALSE);

  if (g_strrstr (line, "packageName: ") != NULL) {
    GString *package = g_string_new (line);

    g_string_replace (package, "packageName: ", "", 0);
    self->installed_applications = g_list_append (self->installed_applications,
                                                  g_string_free_and_steal (package));
  }

  return TRUE;
}

static void
set_installed_apps (CcWaydroidPanel *self)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GIOChannel) stdout = NULL;
  const char *argv[] = {
    "/usr/bin/waydroid",
    "app",
    "list",
    NULL
  };
  GPid child_pid;
  int app_stdout;

  g_list_free_full (self->installed_applications, g_free);
  self->installed_applications = NULL;

  if (!g_spawn_async_with_pipes (NULL,
                                 (char **) argv,
                                 NULL,
                                 G_SPAWN_DO_NOT_REAP_CHILD,
                                 NULL,
                                 NULL,
                                 &child_pid,
                                 NULL,
                                 &app_stdout,
                                 NULL,
                                 &error)) {
    g_warning ("Can't check installed apps");
    return;
  }

  g_child_watch_add(child_pid, installed_apps_cb, self);

  stdout = g_io_channel_unix_new (app_stdout);
  g_io_channel_set_close_on_unref (stdout, TRUE);

  g_io_add_watch (stdout,
                  G_IO_IN | G_IO_PRI,
                  (GIOFunc) handle_application_list, self);
}

static void
check_waydroid_installed (CcWaydroidPanel *self)
{
  g_autoptr (PkTask) task = NULL;
  const char *values[] = {
    "waydroid",
    NULL
  };

  task = pk_task_new ();

  pk_task_resolve_async (task,
                         PK_FILTER_ENUM_NONE,
                         (char **) values,
                         self->cancellable,
                         NULL,
                         NULL,
                         waydroid_resolved_cb,
                         self);
}

static void
check_waydroid_running (CcWaydroidPanel *self)
{
  g_dbus_proxy_call (self->waydroid_proxy,
                     "GetSession",
                     NULL,
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     self->cancellable,
                     waydroid_get_session_cb,
                     self);
}

static gboolean
get_user_0_watch (GIOChannel   *source,
                  GIOCondition  condition,
                  gpointer      user_data)
{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (user_data);
  g_autofree char *line = NULL;
  gsize  size;

  if (g_io_channel_read_line (source, &line, &size, NULL, NULL) != G_IO_STATUS_NORMAL) {
    gtk_widget_set_sensitive (self->enable_waydroid_switch, TRUE);
    gtk_spinner_stop (GTK_SPINNER (self->enable_waydroid_spinner));
    return FALSE;
  }

  if (g_strrstr (line, "Android with user 0 is ready") != NULL) {
    check_waydroid_running (self);
    gtk_widget_set_sensitive (self->enable_waydroid_switch, TRUE);
    gtk_spinner_stop (GTK_SPINNER (self->enable_waydroid_spinner));

    return FALSE;
  }

  return TRUE;
}

static gboolean
get_prop_watch (GIOChannel   *source,
                GIOCondition  condition,
                gpointer      user_data)
{
  struct IOWatchProp *io_watch_prop = user_data;
  g_autofree char *line = NULL;
  gsize  size;

  if (g_io_channel_read_line (source, &line, &size, NULL, NULL) != G_IO_STATUS_NORMAL)
    goto out;

  if (g_strcmp0 (io_watch_prop->prop, "persist.waydroid.uevent") == 0) {
    g_signal_handlers_block_by_func(io_watch_prop->panel->setting_uevent_switch,
                                    setting_uevent_active_cb,
                                    io_watch_prop->panel);
    gtk_switch_set_active (GTK_SWITCH (io_watch_prop->panel->setting_uevent_switch),
                           g_strrstr (line, "true") != NULL);
    g_signal_handlers_unblock_by_func(io_watch_prop->panel->setting_uevent_switch,
                                      setting_uevent_active_cb,
                                      io_watch_prop->panel);
  } else if (g_strcmp0 (io_watch_prop->prop, "persist.waydroid.suspend") == 0) {
    g_signal_handlers_block_by_func(io_watch_prop->panel->setting_suspend_switch,
                                    setting_suspend_active_cb,
                                    io_watch_prop->panel);
    gtk_switch_set_active (GTK_SWITCH (io_watch_prop->panel->setting_suspend_switch),
                           g_strrstr (line, "true") != NULL);
    g_signal_handlers_unblock_by_func(io_watch_prop->panel->setting_suspend_switch,
                                      setting_suspend_active_cb,
                                      io_watch_prop->panel);
  }

out:
  g_free (io_watch_prop);
  return FALSE;
}

static void
get_android_prop (CcWaydroidPanel *self,
                  const char      *prop)
{
  g_autoptr (GError) error = NULL;
  g_autoptr(GStrvBuilder) builder = NULL;
  g_auto(GStrv) argv = NULL;
  g_autoptr (GIOChannel) stdout = NULL;
  int uevent_stdout;
  struct IOWatchProp *io_watch_prop;

  builder = g_strv_builder_new ();
  g_strv_builder_add_many (builder,
                           "/usr/bin/waydroid",
                           "prop",
                           "get",
                           prop,
                           NULL);
  argv = g_strv_builder_end (builder);

  if (!g_spawn_async_with_pipes (NULL,
                                 argv,
                                 NULL,
                                 G_SPAWN_DO_NOT_REAP_CHILD,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &uevent_stdout,
                                 NULL,
                                 &error)) {
    g_warning ("Can't read property: %s, %s", prop, error->message);
    return;
  }

  stdout = g_io_channel_unix_new (uevent_stdout);
  g_io_channel_set_close_on_unref (stdout, TRUE);

  io_watch_prop = g_malloc (sizeof (struct IOWatchProp));
  io_watch_prop->panel = self;
  io_watch_prop->prop = prop;

  g_io_add_watch (stdout,
                  G_IO_IN | G_IO_PRI,
                  (GIOFunc) get_prop_watch, io_watch_prop);
}

static void
set_android_prop (CcWaydroidPanel *self,
                  const char      *prop,
                  const char      *value)
{
  g_autoptr (GError) error = NULL;
  g_autoptr(GStrvBuilder) builder = NULL;
  g_auto(GStrv) argv = NULL;

  builder = g_strv_builder_new ();
  g_strv_builder_add_many (builder,
                           "/usr/bin/waydroid",
                           "prop",
                           "set",
                           prop,
                           value,
                           NULL);
  argv = g_strv_builder_end (builder);

  if (!g_spawn_async_with_pipes (NULL,
                                 argv,
                                 NULL,
                                 G_SPAWN_DO_NOT_REAP_CHILD,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &error)) {
    g_warning ("Can't write property: %s, %s", prop, error->message);
  }
}


static void
set_shared_folder_state (CcWaydroidPanel *self)
{
  g_autofree char *filename = g_build_filename (g_get_user_config_dir (),
                                                "Droidian",
                                                "waydroid_shared_folder",
                                                NULL);
  g_autoptr (GFile) file = g_file_new_for_path (filename);
  gboolean active = g_file_query_exists (file, self->cancellable);

  g_signal_handlers_block_by_func(self->setting_shared_folder_switch,
                                  setting_shared_folder_active_cb,
                                  self);
  gtk_switch_set_active (GTK_SWITCH (self->setting_shared_folder_switch), active);
  g_signal_handlers_unblock_by_func(self->setting_shared_folder_switch,
                                    setting_shared_folder_active_cb,
                                    self);
}

static void
set_notifications_state (CcWaydroidPanel *self)
{
  g_autofree char *filename = g_build_filename (g_get_user_config_dir (),
                                                "systemd",
                                                "user",
                                                "waydroid-notification-client.service",
                                                NULL);
  g_autoptr (GFile) file = g_file_new_for_path (filename);
  gboolean exists = g_file_query_exists (file, self->cancellable);

  g_signal_handlers_block_by_func(self->setting_notifications_switch,
                                  setting_notifications_active_cb,
                                  self);
  gtk_switch_set_active (GTK_SWITCH (self->setting_notifications_switch), !exists);
  g_signal_handlers_unblock_by_func(self->setting_notifications_switch,
                                    setting_notifications_active_cb,
                                    self);
}

static void
app_removed_cb (CcAppRow *row,
                gpointer  user_data)
{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (user_data);

  adw_preferences_group_remove (ADW_PREFERENCES_GROUP (self->android_applications),
                                GTK_WIDGET (row));
}

static void
session_stop_cb (GPid     pid,
                 gint     status,
                 gpointer user_data)
{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (user_data);

  g_spawn_close_pid (pid);

  gtk_spinner_stop (GTK_SPINNER (self->enable_waydroid_spinner));
  gtk_widget_set_sensitive (self->enable_waydroid_switch, TRUE);

  check_waydroid_running (self);
}

static void
waydroid_installed_cb (GObject      *object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (user_data);
  g_autoptr (PkResults) results = NULL;
  g_autoptr (GError) error = NULL;

  gtk_spinner_stop (GTK_SPINNER (self->install_waydroid_spinner));

  results = pk_client_generic_finish (PK_CLIENT (object), res, &error);
  if (error == NULL) {
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "configure");
    gtk_widget_set_sensitive (self->enable_waydroid_switch, TRUE);
    check_waydroid_running (self);
  } else {
    g_warning ("Can't install waydroid: %s", error->message);
  }
}

static void
waydroid_resolved_cb (GObject      *object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (user_data);
  g_autoptr (PkResults) results = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (PkError) error_code = NULL;
  g_autoptr (GPtrArray) packages = NULL;

  results = pk_task_generic_finish (PK_TASK (object), res, &error);
  if (error != NULL) {
    g_warning ("Can't contact PackageKit");
    return;
  }

  error_code = pk_results_get_error_code (results);
  if (error_code != NULL) {
    g_warning ("Can't find waydroid in packages");
    return;
  }

  packages = pk_results_get_package_array (results);
  handle_waydroid_package (self, packages);
}

static void
clear_data_dialog_cb (AdwAlertDialog *dialog,
                      GAsyncResult   *result,
                      gpointer        user_data)

{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (user_data);
  const char *response = adw_alert_dialog_choose_finish (dialog, result);

  if (g_strcmp0 (response, "clear") == 0) {
    g_autoptr (GError) error = NULL;
    g_autoptr(GStrvBuilder) builder = NULL;
    g_auto(GStrv) argv = NULL;
    g_autofree char *command = g_strdup_printf ("rm -rf %s/.local/share/waydroid %s/.local/share/applications/waydroid.*.desktop",
                                                g_get_home_dir(),
                                                g_get_home_dir());
    GPid child_pid;

    builder = g_strv_builder_new ();
    g_strv_builder_add_many (builder,
                             "/usr/bin/pkexec",
                             "/bin/sh",
                             "-c",
                             command,
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
      g_warning ("Can't clear data: %s", error->message);
    }
    g_child_watch_add(child_pid, data_reset_cb, self);
  }
}

static void
clear_data_button_clicked_cb (CcWaydroidPanel *self)
{
  AdwDialog *dialog = adw_alert_dialog_new (_("Clear data?"), NULL);

  adw_alert_dialog_format_body (ADW_ALERT_DIALOG (dialog),
                                _("This will remove applications and settings!"));

  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog),
                                  "cancel",  _("_Cancel"),
                                  "clear", _("Clear"),
                                  NULL);

  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog),
                                            "clear",
                                            ADW_RESPONSE_DESTRUCTIVE);

  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "cancel");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "cancel");

  adw_alert_dialog_choose (ADW_ALERT_DIALOG (dialog), GTK_WIDGET (self), NULL,
                           (GAsyncReadyCallback) clear_data_dialog_cb,
                           self);

  adw_dialog_present (dialog, NULL);
}

static void
install_waydroid_clicked_cb (CcWaydroidPanel *self)
{
  g_autoptr (PkClient) client = NULL;
  g_autoptr(GStrvBuilder) builder = NULL;
  g_auto(GStrv) values = NULL;

  gtk_widget_set_sensitive (self->install_waydroid_button, FALSE);

  client = pk_client_new ();

  builder = g_strv_builder_new ();
  g_strv_builder_add_many (builder,
                           gtk_widget_get_name (self->install_waydroid_button),
                           NULL);
  values = g_strv_builder_end (builder);

  gtk_spinner_start (GTK_SPINNER (self->install_waydroid_spinner));

  pk_client_install_packages_async (client,
                                    PK_FILTER_ENUM_NONE,
                                    values,
                                    self->cancellable,
                                    NULL,
                                    NULL,
                                    waydroid_installed_cb,
                                    self);
}

static void
enable_waydroid_active_cb (CcWaydroidPanel *self)
{
  gboolean active = gtk_switch_get_active (GTK_SWITCH (self->enable_waydroid_switch));
  g_autoptr (GError) error = NULL;
  g_autoptr (GIOChannel) stdout = NULL;
  int start_stdout;

  gtk_spinner_start (GTK_SPINNER (self->enable_waydroid_spinner));
  gtk_widget_set_sensitive (self->enable_waydroid_switch, FALSE);

  if (active) {
    gchar *argv[] = { "/usr/bin/waydroid", "session", "start", NULL };

    if (g_spawn_async_with_pipes (NULL,
                                  argv,
                                  NULL,
                                  G_SPAWN_DO_NOT_REAP_CHILD,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  &start_stdout, /* waydroid use stderr :( */
                                  &error)) {
      stdout = g_io_channel_unix_new (start_stdout);
      g_io_channel_set_close_on_unref (stdout, TRUE);
      g_io_add_watch (stdout,
                      G_IO_IN | G_IO_PRI,
                      (GIOFunc) get_user_0_watch, self);
    } else {
      g_warning ("Can't start waydroid: %s", error->message);
    }
  } else {
    gchar *argv[] = { "/usr/bin/waydroid", "session", "stop", NULL };
    GPid child_pid;

    if (g_spawn_async_with_pipes (NULL,
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
      gtk_widget_set_sensitive (self->android_applications, FALSE);
      gtk_widget_set_sensitive (self->waydroid_settings, FALSE);
      gtk_widget_set_sensitive (self->reset_settings, TRUE);
      g_child_watch_add(child_pid, (GChildWatchFunc) session_stop_cb, self);
    } else {
      g_warning ("Can't stop waydroid: %s", error->message);
    }
  }
}

static void
setting_uevent_active_cb (CcWaydroidPanel *self)
{
  gboolean active = gtk_switch_get_active (GTK_SWITCH (self->setting_uevent_switch));
  const char *value = active ? "true" : "false";

  set_android_prop (self, "persist.waydroid.uevent", value);
}

static void
setting_suspend_active_cb (CcWaydroidPanel *self)
{
  gboolean active = gtk_switch_get_active (GTK_SWITCH (self->setting_suspend_switch));
  const char *value = active ? "true" : "false";

  set_android_prop (self, "persist.waydroid.suspend", value);
}

static void
setting_shared_folder_active_cb (CcWaydroidPanel *self)
{
  g_autofree char *dirname = g_build_filename (g_get_user_config_dir (),
                                               "Droidian",
                                               NULL);
  g_autofree char *filename = g_build_filename (dirname,
                                                "waydroid_shared_folder",
                                                NULL);
  g_autoptr (GFile) directory = g_file_new_for_path (dirname);
  g_autoptr (GFile) file = g_file_new_for_path (filename);
  g_autoptr (GError) error = NULL;
  gboolean active = gtk_switch_get_active (GTK_SWITCH (self->setting_shared_folder_switch));

  if (active) {
    g_autoptr (GFileOutputStream) stream = NULL;

    g_file_make_directory_with_parents (directory, self->cancellable, NULL);
    if (error == NULL) {
      stream = g_file_create (file,
                              G_FILE_CREATE_PRIVATE,
                              self->cancellable,
                              &error);
      start_service (self, "waydroid-mount-shared-folders.service");
    }
  } else {
    stop_service (self, "waydroid-mount-shared-folders.service");
    g_file_delete (file, self->cancellable, &error);
  }

  if (error != NULL)
    g_warning ("%s", error->message);
}

static void
setting_notifications_active_cb (CcWaydroidPanel *self)
{
  gboolean active = gtk_switch_get_active (GTK_SWITCH (self->setting_notifications_switch));

  mask_notifications_service (self, !active);
}

static void
waydroid_bus_cb (GObject  *object,
                 GAsyncResult* res,
                 gpointer  user_data)
{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (user_data);
  g_autoptr (GError) error = NULL;

  self->waydroid_proxy = g_dbus_proxy_new_for_bus_finish (res, &error);

  if (error != NULL) {
    g_warning ("Can't enable Waydroid bus proxy");
    return;
  }

  check_waydroid_running (self);
}

static void
waydroid_get_session_cb (GObject      *object,
                         GAsyncResult *res,
                         gpointer      data)
{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (data);
  GDBusProxy *proxy = G_DBUS_PROXY (object);
  g_autoptr (GVariant) result = NULL;

  result = g_dbus_proxy_call_finish (proxy, res, NULL);

  g_return_if_fail (result != NULL);

  if (g_variant_n_children(g_variant_get_child_value (result, 0)) == 0) {
    set_enable_waydroid_switch (self, FALSE);
    gtk_widget_set_sensitive (self->android_applications, FALSE);
    gtk_widget_set_sensitive (self->waydroid_settings, FALSE);
    gtk_widget_set_sensitive (self->reset_settings, TRUE);
    return;
  }

  gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "configure");
  set_enable_waydroid_switch (self, g_variant_get_size (result) > 0);

  set_installed_apps (self);

  get_android_prop (self, "persist.waydroid.uevent");
  get_android_prop (self, "persist.waydroid.suspend");
  set_shared_folder_state (self);
  set_notifications_state (self);
  check_available_apps (self);

  gtk_widget_set_sensitive (self->android_applications, TRUE);
  gtk_widget_set_sensitive (self->waydroid_settings, TRUE);
  gtk_widget_set_sensitive (self->reset_settings, FALSE);
}

static void
dialog_toplevel_is_active_changed (CcWaydroidPanel *self)
{
  check_available_apps (self);
  set_installed_apps (self);
}

static void
mapped_cb (CcWaydroidPanel *self)
{
  CcShell *shell;
  GtkWidget *toplevel;

  shell = cc_panel_get_shell (CC_PANEL (self));
  toplevel = cc_shell_get_toplevel (shell);
  if (toplevel && !self->focus_id)
    self->focus_id = g_signal_connect_object (toplevel, "notify::is-active",
                                        G_CALLBACK (dialog_toplevel_is_active_changed), self, G_CONNECT_SWAPPED);
}

static void
systemd_proxy_cb (GObject      *source_object,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (user_data);
  g_autoptr (GError) error = NULL;

  self->systemd_proxy = g_dbus_proxy_new_for_bus_finish (res, &error);

  if (error != NULL) {
    g_warning ("Can't connect to systemd bus: %s", error->message);
  }
}

static void
cc_waydroid_panel_dispose (GObject *object)
{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (object);
  CcShell *shell;
  GtkWidget *toplevel;

  g_clear_object (&self->waydroid_proxy);
  g_clear_object (&self->systemd_proxy);

  if (self->focus_id) {
    shell = cc_panel_get_shell (CC_PANEL (self));
    toplevel = cc_shell_get_toplevel (shell);
    g_clear_signal_handler (&self->focus_id, toplevel);
  }

  G_OBJECT_CLASS (cc_waydroid_panel_parent_class)->dispose (object);
}

static void
cc_waydroid_panel_finalize (GObject *object)
{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (object);

  g_list_free (self->application_rows);
  g_list_free_full (self->installed_applications, g_free);

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (cc_waydroid_panel_parent_class)->finalize (object);
}

static void
cc_waydroid_panel_class_init (CcWaydroidPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = cc_waydroid_panel_dispose;
  object_class->finalize = cc_waydroid_panel_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/waydroid/cc-waydroid-panel.ui");

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        install_box);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        install_status_page);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        enable_waydroid_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        enable_waydroid_spinner);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        install_waydroid_button);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        install_waydroid_spinner);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        stack);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        setting_uevent_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        setting_shared_folder_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        setting_notifications_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        setting_suspend_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        android_applications);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        waydroid_settings);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        reset_settings);

  gtk_widget_class_bind_template_callback (widget_class,
                                           install_waydroid_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class,
                                           enable_waydroid_active_cb);
  gtk_widget_class_bind_template_callback (widget_class,
                                           setting_uevent_active_cb);
  gtk_widget_class_bind_template_callback (widget_class,
                                           setting_suspend_active_cb);
  gtk_widget_class_bind_template_callback (widget_class,
                                           setting_shared_folder_active_cb);
  gtk_widget_class_bind_template_callback (widget_class,
                                           setting_notifications_active_cb);
  gtk_widget_class_bind_template_callback (widget_class,
                                           clear_data_button_clicked_cb);
}

static void
cc_waydroid_panel_init (CcWaydroidPanel *self)
{
  GtkWidget *row = cc_app_row_new_for_app_id (FDROID_NAME, FDROID_APPID, FDROID_URL);

  g_resources_register (cc_waydroid_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  self->application_rows = NULL;
  self->installed_applications = NULL;
  self->cancellable = g_cancellable_new ();

  g_signal_connect (self, "map", G_CALLBACK (mapped_cb), NULL);

  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                            0,
                            NULL,
                            WAYDROID_DBUS_NAME,
                            WAYDROID_DBUS_PATH,
                            WAYDROID_DBUS_INTERFACE,
                            self->cancellable,
                            waydroid_bus_cb,
                            self);

  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                            0,
                            NULL,
                            SYSTEMD_DBUS_NAME,
                            SYSTEMD_DBUS_PATH,
                            SYSTEMD_DBUS_INTERFACE,
                            self->cancellable,
                            systemd_proxy_cb,
                            self);

  if (row != NULL) {
    self->application_rows = g_list_append (self->application_rows, row);
    adw_preferences_group_add (ADW_PREFERENCES_GROUP (self->android_applications),
                               row);
  }

  check_waydroid_installed (self);

  gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "configure");
}

CcWaydroidPanel *
cc_waydroid_panel_new (void)
{
  return CC_WAYDROID_PANEL (g_object_new (CC_TYPE_WAYDROID_PANEL, NULL));
}