/*
 * Copyright (C) 2023 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-waydroid-panel.h"
#include "cc-waydroid-resources.h"
#include "cc-util.h"

#include <adwaita.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>

struct _CcWaydroidPanel {
  CcPanel            parent;
  GtkWidget        *waydroid_enabled_switch;
  GtkWidget        *waydroid_ip_label;
  GtkWidget        *waydroid_vendor_label;
  GtkWidget        *waydroid_version_label;
  GtkWidget        *app_selector;
  GListStore       *app_list_store;
  GtkWidget        *launch_app_button;
  GtkWidget        *remove_app_button;
  GtkWidget        *install_app_button;
  GtkWidget        *show_ui_button;
  GtkWidget        *refresh_app_list_button;
  GtkWidget        *waydroid_uevent_switch;
  GtkWidget        *install_waydroid_button;
  GtkWidget        *waydroid_factory_reset;
};

typedef struct {
    CcWaydroidPanel *self;
    gchar *waydroid_ip_output;
    gchar *waydroid_vendor_output;
    gchar *new_version_output;
    gchar **apps;
    gchar *pkgname;
} ThreadData;

G_DEFINE_TYPE (CcWaydroidPanel, cc_waydroid_panel, CC_TYPE_PANEL)

static void
cc_waydroid_panel_finalize (GObject *object)
{
  G_OBJECT_CLASS (cc_waydroid_panel_parent_class)->finalize (object);
}

static gboolean
child_stdout_callback(GIOChannel *channel, GIOCondition condition, gpointer data)
{
    gchar *string;
    gsize size;

    if (g_io_channel_read_line(channel, &string, &size, NULL, NULL) == G_IO_STATUS_NORMAL) {
        if (strstr(string, "Android with user 0 is ready") != NULL) {
            g_io_channel_shutdown(channel, FALSE, NULL);
        }
    }

    g_free(string);
    return TRUE;
}

static void
child_exited_callback(GPid pid, gint status, gpointer data)
{
    g_spawn_close_pid(pid);
}

static gboolean
update_label_idle (gpointer user_data)
{
    ThreadData *data = user_data;

    gtk_label_set_text(GTK_LABEL(data->self->waydroid_ip_label), data->waydroid_ip_output);

    g_free(data->waydroid_ip_output);
    g_free(data);

    return G_SOURCE_REMOVE;
}

static gpointer
update_waydroid_ip (gpointer user_data)
{
    CcWaydroidPanel *self = (CcWaydroidPanel *)user_data;
    gchar *waydroid_ip_output;
    gchar *waydroid_ip_error;
    gint waydroid_ip_exit_status;

    g_spawn_command_line_sync("sh -c \"waydroid status | awk -F'\t' '/IP/ {print $2; exit}'\"", &waydroid_ip_output, &waydroid_ip_error, &waydroid_ip_exit_status, NULL);

    ThreadData *data = g_new(ThreadData, 1);
    data->self = self;
    data->waydroid_ip_output = waydroid_ip_output;

    g_idle_add(update_label_idle, data);

    g_free(waydroid_ip_error);

    return NULL;
}

static void
update_waydroid_ip_threaded (CcWaydroidPanel *self)
{
    g_thread_new("update_waydroid_ip", update_waydroid_ip, self);
}

static gboolean
update_app_list_idle (gpointer user_data)
{
    ThreadData *data = user_data;
    CcWaydroidPanel *self = data->self;
    gchar **apps = data->apps;

    GtkDropDown *drop_down = GTK_DROP_DOWN(self->app_selector);
    const char *initial_strings[] = { NULL };
    GtkStringList *list = gtk_string_list_new(initial_strings);

    for (gchar **app = apps; *app; app++) {
        if (*app[0] != '\0') {
            gtk_string_list_append(list, *app);
        }
    }

    gtk_drop_down_set_model(drop_down, G_LIST_MODEL(list));
    gtk_widget_set_sensitive(GTK_WIDGET(drop_down), TRUE);

    g_strfreev(apps);
    g_free(data);

    return G_SOURCE_REMOVE;
}

static gpointer
update_app_list (gpointer user_data)
{
    CcWaydroidPanel *self = (CcWaydroidPanel *)user_data;
    gchar *output, *error;
    gint exit_status;
    gchar **apps;

    g_spawn_command_line_sync("sh -c \"waydroid app list | awk -F': ' '/^Name:/ {print $2}'\"", &output, &error, &exit_status, NULL);

    if (exit_status != 0 || output == NULL || output[0] == '\0') {
        g_free(output);
        g_free(error);
        return NULL;
    }

    apps = g_strsplit_set(output, "\n", -1);

    ThreadData *data = g_new(ThreadData, 1);
    data->self = self;
    data->apps = apps;

    g_idle_add(update_app_list_idle, data);

    g_free(output);
    g_free(error);

    return NULL;
}

static void
update_app_list_threaded (CcWaydroidPanel *self)
{
    g_thread_new("update_app_list", update_app_list, self);
}

static gchar*
get_selected_app_pkgname (CcWaydroidPanel *self)
{
    GtkStringObject *selected_obj = GTK_STRING_OBJECT(gtk_drop_down_get_selected_item(GTK_DROP_DOWN(self->app_selector)));
    if (selected_obj) {
        const gchar *selected_app = gtk_string_object_get_string(selected_obj);

        gchar *command;
        gchar *output;
        gchar *error;
        gint exit_status;
        command = g_strdup_printf("sh -c \"waydroid app list | awk -v app=\\\"%s\\\" '/Name: / && $2 == app { getline; print $2}'\"", selected_app);

        g_spawn_command_line_sync(command, &output, &error, &exit_status, NULL);
        g_free(command);

        if (exit_status == 0 && output != NULL) {
            g_free(error);
            return output;
        }

        g_free(output);
        g_free(error);
    }

    return NULL;
}

static void
cc_waydroid_panel_uninstall_app (GtkWidget *widget, CcWaydroidPanel *self)
{
    gchar *pkgname = get_selected_app_pkgname(self);
    if (pkgname != NULL) {
        if (g_strcmp0(pkgname, "com.android.documentsui") != 0 && g_strcmp0(pkgname, "com.android.contacts") != 0 &&
            g_strcmp0(pkgname, "com.android.camera2") != 0 && g_strcmp0(pkgname, "org.lineageos.recorder") != 0 &&
            g_strcmp0(pkgname, "com.android.gallery3d") != 0 && g_strcmp0(pkgname, "org.lineageos.jelly") != 0 &&
            g_strcmp0(pkgname, "org.lineageos.eleven") != 0 && g_strcmp0(pkgname, "org.lineageos.etar") != 0 &&
            g_strcmp0(pkgname, "com.android.settings") != 0 && g_strcmp0(pkgname, "com.android.calculator2") != 0 &&
            g_strcmp0(pkgname, "com.android.deskclock") != 0 && g_strcmp0(pkgname, "com.android.traceur") != 0) {

            gchar *remove_command = g_strdup_printf("waydroid app remove %s", g_strstrip(pkgname));
            g_spawn_command_line_async(remove_command, NULL);
            g_free(remove_command);

            gtk_widget_set_sensitive(GTK_WIDGET(self->app_selector), FALSE);
            gtk_widget_set_sensitive(GTK_WIDGET(self->remove_app_button), FALSE);
            gtk_widget_set_sensitive(GTK_WIDGET(self->install_app_button), FALSE);
            gtk_widget_set_sensitive(GTK_WIDGET(self->refresh_app_list_button), FALSE);

            g_timeout_add_seconds(5, (GSourceFunc)gtk_widget_set_sensitive, GTK_WIDGET(self->app_selector));
            g_timeout_add_seconds(5, (GSourceFunc)gtk_widget_set_sensitive, GTK_WIDGET(self->remove_app_button));
            g_timeout_add_seconds(5, (GSourceFunc)gtk_widget_set_sensitive, GTK_WIDGET(self->install_app_button));
            g_timeout_add_seconds(5, (GSourceFunc)gtk_widget_set_sensitive, GTK_WIDGET(self->refresh_app_list_button));

            update_app_list_threaded(self);
        }
        g_free(pkgname);
    }
}

static gpointer
cc_waydroid_panel_launch_app_thread (gpointer user_data)
{
    ThreadData *data = user_data;
    gchar *pkgname = data->pkgname;

    if (pkgname != NULL) {
        gchar *launch_command = g_strdup_printf("waydroid app launch %s", g_strstrip(pkgname));
        g_spawn_command_line_async(launch_command, NULL);
        g_free(launch_command);
    }

    g_free(pkgname);
    g_free(data);

    return NULL;
}

static void
cc_waydroid_panel_launch_app_threaded (GtkWidget *widget, CcWaydroidPanel *self)
{
    gchar *pkgname = get_selected_app_pkgname(self);
    ThreadData *data = g_new(ThreadData, 1);
    data->pkgname = pkgname;

    g_thread_new("cc_waydroid_panel_launch_app", cc_waydroid_panel_launch_app_thread, data);
}

static gboolean
update_vendor_idle (gpointer user_data)
{
    ThreadData *data = user_data;

    gtk_label_set_text(GTK_LABEL(data->self->waydroid_vendor_label), data->waydroid_vendor_output);

    g_free(data->waydroid_vendor_output);
    g_free(data);

    return G_SOURCE_REMOVE;
}

static gpointer
update_waydroid_vendor (gpointer user_data)
{
    CcWaydroidPanel *self = (CcWaydroidPanel *)user_data;
    gchar *waydroid_vendor_output;
    gchar *waydroid_vendor_error;
    gint waydroid_vendor_exit_status;

    g_spawn_command_line_sync("sh -c \"waydroid status | awk -F'\t' '/Vendor/ {print $2; exit}'\"", &waydroid_vendor_output, &waydroid_vendor_error, &waydroid_vendor_exit_status, NULL);

    ThreadData *data = g_new(ThreadData, 1);
    data->self = self;
    data->waydroid_vendor_output = waydroid_vendor_output;

    g_idle_add(update_vendor_idle, data);

    g_free(waydroid_vendor_error);

    return NULL;
}

static void
update_waydroid_vendor_threaded (CcWaydroidPanel *self)
{
    g_thread_new("update_waydroid_vendor", update_waydroid_vendor, self);
}

static gboolean
update_version_idle (gpointer user_data)
{
    ThreadData *data = user_data;

    gtk_label_set_text(GTK_LABEL(data->self->waydroid_version_label), data->new_version_output);

    g_free(data->new_version_output);
    g_free(data);

    return G_SOURCE_REMOVE;
}

static gpointer
update_waydroid_version (gpointer user_data)
{
    CcWaydroidPanel *self = (CcWaydroidPanel *)user_data;
    gchar *waydroid_version_output;
    gchar *waydroid_version_error;
    gint waydroid_version_exit_status;

    g_spawn_command_line_sync("sh -c \"waydroid prop get ro.lineage.display.version\"", &waydroid_version_output, &waydroid_version_error, &waydroid_version_exit_status, NULL);

    gchar **parts = g_strsplit(waydroid_version_output, "-", 3);
    gchar *new_version_output = g_strconcat(parts[0], "-", parts[1], NULL);

    ThreadData *data = g_new(ThreadData, 1);
    data->self = self;
    data->new_version_output = new_version_output;

    g_idle_add(update_version_idle, data);

    g_strfreev(parts);
    g_free(waydroid_version_output);
    g_free(waydroid_version_error);

    return NULL;
}

static void
update_waydroid_version_threaded (CcWaydroidPanel *self)
{
    g_thread_new("update_waydroid_version", update_waydroid_version, self);
}

static void
cc_waydroid_refresh_button(GtkButton *button, gpointer user_data)
{
    CcWaydroidPanel *self = CC_WAYDROID_PANEL(user_data);

    update_waydroid_ip_threaded(self);
    update_waydroid_vendor_threaded(self);
    update_waydroid_version_threaded(self);
    update_app_list_threaded(self);
}

void on_dialog_response(GtkDialog *dialog, gint response_id, CcWaydroidPanel *self) {
    if(response_id == GTK_RESPONSE_ACCEPT) {
        char *filename;
        GFile *file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (dialog));
        filename = g_file_get_path(file);
        gchar *install_command = g_strdup_printf("waydroid app install %s", g_strstrip(filename));
        g_spawn_command_line_async(install_command, NULL);
        g_free(install_command);
        g_free(filename);
    }

    g_object_unref(dialog);
}

static void
install_app(CcWaydroidPanel *self, GFile *file)
{
    gchar *file_path = g_file_get_path(file);
    gchar *command = g_strdup_printf("waydroid app install %s", file_path);
    g_spawn_command_line_sync(command, NULL, NULL, NULL, NULL);

    g_free(command);
    g_free(file_path);

    update_app_list_threaded(self);
}

static void
on_file_chosen(GtkFileChooserNative *native, gint response_id, CcWaydroidPanel *self)
{
    if (response_id == GTK_RESPONSE_ACCEPT)
    {
        GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(native));
        if (file)
        {
            install_app(self, file);
            g_object_unref(file);
        }
    }
    gtk_native_dialog_destroy(GTK_NATIVE_DIALOG(native));
}

static void
cc_waydroid_panel_install_app(GtkWidget *widget, CcWaydroidPanel *self)
{
    GtkFileChooserNative *native = gtk_file_chooser_native_new("Choose an APK",
                                                               GTK_WINDOW(gtk_widget_get_root(widget)),
                                                               GTK_FILE_CHOOSER_ACTION_OPEN,
                                                               "Open",
                                                               "Cancel");

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "APK files");
    gtk_file_filter_add_pattern(filter, "*.apk");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), filter);

    g_signal_connect(native, "response", G_CALLBACK(on_file_chosen), self);

    gtk_native_dialog_show(GTK_NATIVE_DIALOG(native));
}

static void
cc_waydroid_panel_show_full_ui (GtkButton *button, gpointer user_data)
{
  GError *error = NULL;
  gchar *argv[] = {"waydroid", "show-full-ui", NULL};

  if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, NULL, &error)) {
    g_warning("Error spawning waydroid: %s", error->message);
    g_clear_error(&error);
  }
}

static gboolean
cc_waydroid_panel_toggle_uevent(GtkSwitch *widget, gboolean state, CcWaydroidPanel *self)
{
    GError *error = NULL;
    gchar *argv[] = { "waydroid", "prop", "set", "persist.waydroid.uevent", state ? "true" : "false", NULL };

    if (gtk_switch_get_state(GTK_SWITCH(self->waydroid_enabled_switch))) {
        gint exit_status = 0;

        if (!g_spawn_sync(NULL, argv, NULL,
                          G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                          NULL, NULL, NULL, NULL, &exit_status, &error)) {
            g_printerr("Error: %s\n", error->message);
            g_error_free(error);
        }
    }

    return FALSE;
}

static void
cc_waydroid_factory_reset_threaded (GtkWidget *widget, CcWaydroidPanel *self)
{
    GError *error = NULL;
    gchar *command = "rm -rf $HOME/.local/share/waydroid";
    gchar *home_env = g_strdup_printf("HOME=%s", g_get_home_dir());
    gchar *argv[] = {"pkexec", "env", home_env, "/bin/sh", "-c", command, NULL};

    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, NULL, &error)) {
        g_warning("Error running command: %s", error->message);
        g_clear_error(&error);
    } else {
        gtk_widget_set_sensitive(GTK_WIDGET(self->waydroid_factory_reset), FALSE);
        g_timeout_add_seconds(15, (GSourceFunc)gtk_widget_set_sensitive, self->waydroid_factory_reset);
    }

    g_free(home_env);
}

static gboolean
reenable_switch_and_update_info(gpointer data)
{
    CcWaydroidPanel *self = (CcWaydroidPanel *)data;
    gtk_widget_set_sensitive(GTK_WIDGET(self->waydroid_enabled_switch), TRUE);
    update_waydroid_ip_threaded(self);
    update_waydroid_vendor_threaded(self);
    update_waydroid_version_threaded(self);

    gtk_widget_set_sensitive(GTK_WIDGET(self->launch_app_button), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->remove_app_button), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->install_app_button), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->app_selector), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->show_ui_button), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->refresh_app_list_button), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->waydroid_uevent_switch), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->waydroid_factory_reset), FALSE);

    g_signal_connect(G_OBJECT(self->launch_app_button), "clicked", G_CALLBACK(cc_waydroid_panel_launch_app_threaded), self);
    g_signal_connect(G_OBJECT(self->remove_app_button), "clicked", G_CALLBACK(cc_waydroid_panel_uninstall_app), self);
    g_signal_connect(G_OBJECT(self->install_app_button), "clicked", G_CALLBACK(cc_waydroid_panel_install_app), self);
    g_signal_connect(G_OBJECT(self->show_ui_button), "clicked", G_CALLBACK(cc_waydroid_panel_show_full_ui), self);
    g_signal_connect(self->refresh_app_list_button, "clicked", G_CALLBACK(cc_waydroid_refresh_button), self);

    gchar *uevent_output;
    gchar *uevent_error;
    gint uevent_exit_status;

    g_spawn_command_line_sync("sh -c \"waydroid prop get persist.waydroid.uevent\"", &uevent_output, &uevent_error, &uevent_exit_status, NULL);
    gboolean uevent_state = g_strstr_len(uevent_output, -1, "true") != NULL ? 0 : 1;
    g_signal_handlers_block_by_func(self->waydroid_uevent_switch, cc_waydroid_panel_toggle_uevent, self);

    if (uevent_state == 0) {
        gtk_switch_set_state(GTK_SWITCH(self->waydroid_uevent_switch), TRUE);
        gtk_switch_set_active(GTK_SWITCH(self->waydroid_uevent_switch), TRUE);
    } else {
        gtk_switch_set_state(GTK_SWITCH(self->waydroid_uevent_switch), FALSE);
        gtk_switch_set_active(GTK_SWITCH(self->waydroid_uevent_switch), FALSE);
    }

    g_signal_handlers_unblock_by_func(self->waydroid_uevent_switch, cc_waydroid_panel_toggle_uevent, self);

    g_usleep(5000000);
    update_app_list_threaded(self);

    g_free(uevent_output);
    g_free(uevent_error);

    return G_SOURCE_REMOVE;
}

static gboolean
cc_waydroid_panel_enable_waydroid(GtkSwitch *widget, gboolean state, CcWaydroidPanel *self)
{
    GError *error = NULL;

    if(state) {
        gchar *argv[] = { "waydroid", "session", "start", NULL };
        GPid child_pid;
        gint stdout_fd;

        if (!g_spawn_async_with_pipes(NULL, argv, NULL,
                                      G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                      NULL, NULL, &child_pid,
                                      NULL, &stdout_fd, NULL, &error)) {
            g_printerr("Error: %s\n", error->message);
            g_error_free(error);
            return FALSE;
        }

        GIOChannel *stdout_channel = g_io_channel_unix_new(stdout_fd);
        g_io_add_watch(stdout_channel, G_IO_IN, (GIOFunc)child_stdout_callback, self);

        g_child_watch_add(child_pid, (GChildWatchFunc)child_exited_callback, NULL);

        gtk_widget_set_sensitive(GTK_WIDGET(self->waydroid_enabled_switch), FALSE);

        // we should find a way to query the container instead of waiting aimlessly, waydroid status isn't good enough either
        g_timeout_add_seconds(15, reenable_switch_and_update_info, self);
    } else {
        gchar *argv[] = { "waydroid", "session", "stop", NULL };
        gint exit_status = 0;

        if (!g_spawn_sync(NULL, argv, NULL,
                          G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                          NULL, NULL, NULL, NULL, &exit_status, &error)) {
            g_printerr("Error: %s\n", error->message);
            g_error_free(error);
        }

        update_waydroid_ip_threaded(self);
        update_waydroid_vendor_threaded(self);
        update_waydroid_version_threaded(self);

        gtk_label_set_text(GTK_LABEL(self->waydroid_vendor_label), "");
        gtk_label_set_text(GTK_LABEL(self->waydroid_version_label), "");

        GtkDropDown *drop_down = GTK_DROP_DOWN(self->app_selector);
        const char *empty_strings[] = { NULL };
        GtkStringList *empty_list = gtk_string_list_new(empty_strings);
        gtk_drop_down_set_model(drop_down, G_LIST_MODEL(empty_list));

        gtk_widget_set_sensitive(GTK_WIDGET(self->launch_app_button), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(self->remove_app_button), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(self->install_app_button), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(self->app_selector), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(self->show_ui_button), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(self->refresh_app_list_button), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(self->waydroid_uevent_switch), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(self->waydroid_factory_reset), TRUE);
    }

    return FALSE;
}

static void
cc_waydroid_panel_install_waydroid (CcWaydroidPanel *self, GtkWidget *button)
{
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button))) {
        return;
    }

    gchar *command_output;
    gchar *command_error;
    gint exit_status;

    gboolean success = g_spawn_command_line_sync("kgx -e 'sudo apt update && sudo apt install waydroid -y && sudo systemctl enable --now waydroid-container'",
                                                 &command_output,
                                                 &command_error,
                                                 &exit_status,
                                                 NULL);

    if (success && g_file_test("/usr/bin/waydroid", G_FILE_TEST_EXISTS)) {
        cc_waydroid_panel_init(self);
    } else {
        if (!success) {
            g_print("Error running command: %s", command_error);
        }
    }

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), FALSE);

    g_free(command_output);
    g_free(command_error);
}

static void
cc_waydroid_panel_class_init (CcWaydroidPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = cc_waydroid_panel_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/waydroid/cc-waydroid-panel.ui");

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        waydroid_enabled_switch);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        waydroid_ip_label);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        waydroid_vendor_label);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        waydroid_version_label);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        app_selector);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        launch_app_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        remove_app_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        install_app_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        show_ui_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        refresh_app_list_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        waydroid_uevent_switch);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        install_waydroid_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        waydroid_factory_reset);

  gtk_widget_class_bind_template_callback (widget_class,
                                           cc_waydroid_panel_install_waydroid);
}

static void
cc_waydroid_panel_init (CcWaydroidPanel *self)
{
  g_resources_register (cc_waydroid_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  self->app_list_store = g_list_store_new (G_TYPE_APP_INFO);

  g_signal_connect(self->install_waydroid_button, "clicked", G_CALLBACK(cc_waydroid_panel_install_waydroid), self);

  if(g_file_test("/usr/bin/waydroid", G_FILE_TEST_EXISTS)) {
      g_signal_connect(G_OBJECT(self->waydroid_enabled_switch), "state-set", G_CALLBACK(cc_waydroid_panel_enable_waydroid), self);
      g_signal_connect(G_OBJECT(self->waydroid_uevent_switch), "state-set", G_CALLBACK(cc_waydroid_panel_toggle_uevent), self);
      g_signal_connect(G_OBJECT(self->waydroid_factory_reset), "clicked", G_CALLBACK(cc_waydroid_factory_reset_threaded), self);

      gtk_widget_set_sensitive(GTK_WIDGET(self->install_waydroid_button), FALSE);

      gchar *waydroid_output;
      gchar *waydroid_error;
      gint waydroid_exit_status;
      g_spawn_command_line_sync("sh -c \"waydroid status | awk -F'\t' '/Session/ {print $2; exit}'\"", &waydroid_output, &waydroid_error, &waydroid_exit_status, NULL);

      if(g_str_has_prefix(waydroid_output, "RUNNING")) {
          gchar *uevent_output;
          gchar *uevent_error;
          gint uevent_exit_status;

          g_signal_handlers_block_by_func(self->waydroid_enabled_switch, cc_waydroid_panel_enable_waydroid, self);
          gtk_switch_set_state(GTK_SWITCH(self->waydroid_enabled_switch), TRUE);
          gtk_switch_set_active(GTK_SWITCH(self->waydroid_enabled_switch), TRUE);
          g_signal_handlers_unblock_by_func(self->waydroid_enabled_switch, cc_waydroid_panel_enable_waydroid, self);

          g_spawn_command_line_sync("sh -c \"waydroid prop get persist.waydroid.uevent\"", &uevent_output, &uevent_error, &uevent_exit_status, NULL);

          gboolean uevent_state = g_strstr_len(uevent_output, -1, "true") != NULL ? 0 : 1;

          g_signal_handlers_block_by_func(self->waydroid_uevent_switch, cc_waydroid_panel_toggle_uevent, self);

          if (uevent_state == 0) {
              gtk_switch_set_state(GTK_SWITCH(self->waydroid_uevent_switch), TRUE);
              gtk_switch_set_active(GTK_SWITCH(self->waydroid_uevent_switch), TRUE);
          } else {
              gtk_switch_set_state(GTK_SWITCH(self->waydroid_uevent_switch), FALSE);
              gtk_switch_set_active(GTK_SWITCH(self->waydroid_uevent_switch), FALSE);
          }

          g_signal_handlers_unblock_by_func(self->waydroid_uevent_switch, cc_waydroid_panel_toggle_uevent, self);

          gtk_widget_set_sensitive(GTK_WIDGET(self->launch_app_button), TRUE);
          gtk_widget_set_sensitive(GTK_WIDGET(self->remove_app_button), TRUE);
          gtk_widget_set_sensitive(GTK_WIDGET(self->install_app_button), TRUE);
          gtk_widget_set_sensitive(GTK_WIDGET(self->app_selector), TRUE);
          gtk_widget_set_sensitive(GTK_WIDGET(self->show_ui_button), TRUE);
          gtk_widget_set_sensitive(GTK_WIDGET(self->refresh_app_list_button), TRUE);
          gtk_widget_set_sensitive(GTK_WIDGET(self->waydroid_uevent_switch), TRUE);
          gtk_widget_set_sensitive(GTK_WIDGET(self->waydroid_factory_reset), FALSE);

          g_signal_connect(G_OBJECT(self->launch_app_button), "clicked", G_CALLBACK(cc_waydroid_panel_launch_app_threaded), self);
          g_signal_connect(G_OBJECT(self->remove_app_button), "clicked", G_CALLBACK(cc_waydroid_panel_uninstall_app), self);
          g_signal_connect(G_OBJECT(self->install_app_button), "clicked", G_CALLBACK(cc_waydroid_panel_install_app), self);
          g_signal_connect(G_OBJECT(self->show_ui_button), "clicked", G_CALLBACK(cc_waydroid_panel_show_full_ui), self);
          g_signal_connect(self->refresh_app_list_button, "clicked", G_CALLBACK(cc_waydroid_refresh_button), self);

          update_waydroid_ip_threaded(self);
          update_waydroid_vendor_threaded(self);
          update_app_list_threaded(self);
          update_waydroid_version_threaded(self);

          g_free(uevent_output);
          g_free(uevent_error);
      } else {
          g_signal_handlers_block_by_func(self->waydroid_enabled_switch, cc_waydroid_panel_enable_waydroid, self);
          gtk_switch_set_state(GTK_SWITCH(self->waydroid_enabled_switch), FALSE);
          gtk_switch_set_active(GTK_SWITCH(self->waydroid_enabled_switch), FALSE);
          g_signal_handlers_unblock_by_func(self->waydroid_enabled_switch, cc_waydroid_panel_enable_waydroid, self);
          gtk_label_set_text(GTK_LABEL(self->waydroid_vendor_label), "");
          gtk_label_set_text(GTK_LABEL(self->waydroid_version_label), "");
          gtk_widget_set_sensitive(GTK_WIDGET(self->launch_app_button), FALSE);
          gtk_widget_set_sensitive(GTK_WIDGET(self->remove_app_button), FALSE);
          gtk_widget_set_sensitive(GTK_WIDGET(self->install_app_button), FALSE);
          gtk_widget_set_sensitive(GTK_WIDGET(self->app_selector), FALSE);
          gtk_widget_set_sensitive(GTK_WIDGET(self->show_ui_button), FALSE);
          gtk_widget_set_sensitive(GTK_WIDGET(self->refresh_app_list_button), FALSE);
          gtk_widget_set_sensitive(GTK_WIDGET(self->waydroid_uevent_switch), FALSE);
      }

      g_free(waydroid_output);
      g_free(waydroid_error);
  } else {
      gtk_widget_set_sensitive(GTK_WIDGET(self->install_waydroid_button), TRUE);
      gtk_switch_set_state(GTK_SWITCH(self->waydroid_enabled_switch), FALSE);
      gtk_switch_set_active(GTK_SWITCH(self->waydroid_enabled_switch), FALSE);
      gtk_widget_set_sensitive(self->waydroid_enabled_switch, FALSE);
      gtk_label_set_text(GTK_LABEL(self->waydroid_vendor_label), "");
      gtk_label_set_text(GTK_LABEL(self->waydroid_version_label), "");
      gtk_widget_set_sensitive(GTK_WIDGET(self->launch_app_button), FALSE);
      gtk_widget_set_sensitive(GTK_WIDGET(self->remove_app_button), FALSE);
      gtk_widget_set_sensitive(GTK_WIDGET(self->install_app_button), FALSE);
      gtk_widget_set_sensitive(GTK_WIDGET(self->app_selector), FALSE);
      gtk_widget_set_sensitive(GTK_WIDGET(self->show_ui_button), FALSE);
      gtk_widget_set_sensitive(GTK_WIDGET(self->refresh_app_list_button), FALSE);
      gtk_widget_set_sensitive(GTK_WIDGET(self->waydroid_uevent_switch), FALSE);
  }
}

CcWaydroidPanel *
cc_waydroid_panel_new (void)
{
  return CC_WAYDROID_PANEL (g_object_new (CC_TYPE_WAYDROID_PANEL, NULL));
}
