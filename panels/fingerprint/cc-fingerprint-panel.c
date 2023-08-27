	/*
 * Copyright (C) 2023 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-fingerprint-panel.h"
#include "cc-fingerprint-resources.h"
#include "cc-util.h"

#include <adwaita.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>
#include <gdk/gdk.h>

struct _CcFingerprintPanel {
  CcPanel            parent;
  GtkComboBoxText  *finger_select_combo;
  GtkWidget        *remove_finger_button;
  GtkWidget        *enroll_finger_button;
  GtkWidget        *enroll_status_label;
  GtkWidget        *identify_finger_button;
  GtkWidget        *identify_status_label;
  GtkBox           *show_list_box;
  GtkToggleButton  *show_enrolled_list;
  GtkToggleButton  *show_unenrolled_list;
};

typedef struct {
    GtkWidget *label;
    gchar *text;
} LabelTextData;

G_DEFINE_TYPE (CcFingerprintPanel, cc_fingerprint_panel, CC_TYPE_PANEL)

static void
cc_fingerprint_panel_finalize (GObject *object)
{
  G_OBJECT_CLASS (cc_fingerprint_panel_parent_class)->finalize (object);
}

void
refresh_enrolled_list(CcFingerprintPanel *self) {
    gchar *fpd_output;
    gchar *fpd_error;
    gint fpd_exit_status;
    g_spawn_command_line_sync("droidian-fpd-client list", &fpd_error, &fpd_output, &fpd_exit_status, NULL);

    gchar **fpd_fingers = g_strsplit(fpd_output, ",", -1);

    gtk_combo_box_text_remove_all(self->finger_select_combo);

    for (int i = 0; fpd_fingers[i] != NULL; i++) {
        fpd_fingers[i] = g_strstrip(fpd_fingers[i]);
        gtk_combo_box_text_append_text(self->finger_select_combo, fpd_fingers[i]);
    }

    g_strfreev(fpd_fingers);
    g_free(fpd_output);
    g_free(fpd_error);
}

void
refresh_unenrolled_list(CcFingerprintPanel *self) {
    gchar *fingers[] = {
        "right-index-finger",
        "left-index-finger",
        "right-thumb",
        "right-middle-finger",
        "right-ring-finger",
        "right-little-finger",
        "left-thumb",
        "left-middle-finger",
        "left-ring-finger",
        "left-little-finger",
        NULL
    };

    gchar *fpd_output;
    gchar *fpd_error;
    gint fpd_exit_status;
    g_spawn_command_line_sync("droidian-fpd-client list", &fpd_error, &fpd_output, &fpd_exit_status, NULL);

    fpd_output = g_strchomp(fpd_output);
    fpd_output = g_strstrip(fpd_output);

    GError *error = NULL;
    GRegex *regex = g_regex_new("\\s*,\\s*", 0, 0, &error);
    if (!regex) {
        g_clear_error(&error);
        return;
    }

    gchar *cleaned_fpd_output = g_regex_replace_literal(regex, fpd_output, -1, 0, ",", 0, &error);
    g_regex_unref(regex);
    if (!cleaned_fpd_output) {
        g_clear_error(&error);
        g_free(fpd_output);
        return;
    }

    g_free(fpd_output);
    fpd_output = cleaned_fpd_output;

    gchar **fpd_fingers = g_strsplit(fpd_output, ",", -1);

    gtk_combo_box_text_remove_all(self->finger_select_combo);

    for (int i = 0; fingers[i] != NULL; i++) {
        if (!g_strv_contains(fpd_fingers, fingers[i])) {
            gtk_combo_box_text_append_text(self->finger_select_combo, fingers[i]);
        }
    }

    g_strfreev(fpd_fingers);
    g_free(fpd_output);
    g_free(fpd_error);
}

gboolean on_show_enrolled_list_toggled(GtkToggleButton *togglebutton, gpointer user_data) {
    CcFingerprintPanel *self = (CcFingerprintPanel *) user_data;

    refresh_enrolled_list(self);
    gtk_toggle_button_set_active(self->show_unenrolled_list, FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->enroll_finger_button), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->remove_finger_button), TRUE);

    return TRUE;
}

gboolean on_show_unenrolled_list_toggled(GtkToggleButton *togglebutton, gpointer user_data) {
    CcFingerprintPanel *self = (CcFingerprintPanel *) user_data;

    refresh_unenrolled_list(self);
    gtk_toggle_button_set_active(self->show_enrolled_list, FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->remove_finger_button), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->enroll_finger_button), TRUE);

    return TRUE;
}

static void
cc_fingerprint_panel_remove_finger(GtkButton *button, CcFingerprintPanel *self) {
  gchar *selected_finger = gtk_combo_box_text_get_active_text(self->finger_select_combo);
  gchar *command = g_strdup_printf("droidian-fpd-client remove %s", selected_finger);

  system(command);

  g_free(selected_finger);
  g_free(command);

  refresh_enrolled_list(self);
}

static gpointer
enroll_finger_thread (gpointer user_data)
{
    CcFingerprintPanel *self = (CcFingerprintPanel *)user_data;
    gchar *finger = gtk_combo_box_text_get_active_text(self->finger_select_combo);

    gtk_widget_set_sensitive(GTK_WIDGET(self->enroll_finger_button), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->finger_select_combo), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->show_unenrolled_list), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->show_enrolled_list), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->identify_finger_button), FALSE);

    if (finger != NULL) {
        gchar *command = g_strdup_printf("(rm -f /tmp/.enrollment_status; droidian-fpd-client enroll %s > /tmp/.enrollment_status 2>&1; echo 100 >> /tmp/.enrollment_status; systemctl restart --user fpdlistener)", finger);

        system(command);

        g_free(command);
        g_free(finger);
    }

    refresh_unenrolled_list(self);

    gtk_widget_set_sensitive(GTK_WIDGET(self->enroll_finger_button), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->finger_select_combo), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->show_unenrolled_list), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->show_enrolled_list), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->identify_finger_button), TRUE);

    return NULL;
}

static void
cc_fingerprint_panel_enroll_finger(GtkButton *button, CcFingerprintPanel *self) {
  g_thread_new(NULL, enroll_finger_thread, self);
}

static gboolean
set_label_text (gpointer data)
{
    LabelTextData *label_text_data = (LabelTextData *)data;
    gchar *text_chomped = g_strchomp(g_strdup(label_text_data->text));
    gchar *text_with_percent = g_strconcat(text_chomped, "%", NULL);

    gtk_widget_set_visible(GTK_WIDGET(label_text_data->label), TRUE);
    gtk_label_set_text(GTK_LABEL(label_text_data->label), text_with_percent);

    g_free(text_with_percent);
    g_free(text_chomped);
    g_free(label_text_data->text);
    g_free(label_text_data);
    return G_SOURCE_REMOVE;
}

static gpointer
update_enroll_status_label (gpointer data)
{
    GtkWidget *label = (GtkWidget *)data;
    FILE *file;
    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    while (TRUE) {
        while ((file = fopen("/tmp/.enrollment_status", "r")) == NULL) {
            g_usleep(500 * 1000);
        }

        while ((read = getline(&line, &len, file)) != -1);

        if (line != NULL) {
            LabelTextData *label_text_data = g_new(LabelTextData, 1);
            label_text_data->label = label;
            label_text_data->text = g_strdup(line);
            g_idle_add((GSourceFunc)set_label_text, label_text_data);
            free(line);
            line = NULL;
        }

        fclose(file);

        g_usleep(500 * 1000);
    }

    return NULL;
}

static gboolean
set_identify_label_text (gpointer data)
{
    LabelTextData *label_text_data = (LabelTextData *)data;
    gchar *text_chomped = g_strchomp(g_strdup(label_text_data->text));
    gchar *text_with_percent = g_strconcat(text_chomped, NULL);

    gtk_widget_set_visible(GTK_WIDGET(label_text_data->label), TRUE);
    gtk_label_set_text(GTK_LABEL(label_text_data->label), text_with_percent);

    g_free(text_with_percent);
    g_free(text_chomped);
    g_free(label_text_data->text);
    g_free(label_text_data);
    return G_SOURCE_REMOVE;
}

static gpointer
identify_finger_thread (gpointer user_data)
{
    CcFingerprintPanel *self = (CcFingerprintPanel *)user_data;

    gtk_widget_set_sensitive(GTK_WIDGET(self->identify_finger_button), FALSE);

    gchar *command = g_strdup_printf("droidian-fpd-client identify");
    gchar *output = NULL;
    gchar *error_output = NULL;
    GError *error = NULL;

    if (!g_spawn_command_line_sync(command, &error_output, &output, NULL, &error)) {
        g_printerr("Failed to execute command: %s", error->message);
        g_error_free(error);
    } else {
        LabelTextData *label_text_data = g_new(LabelTextData, 1);
        label_text_data->label = self->identify_status_label;
        label_text_data->text = g_strdup(output);
        g_idle_add((GSourceFunc)set_identify_label_text, label_text_data);
    }

    if (output) {
        g_free(output);
    }

    if (error_output) {
        g_free(error_output);
    }

    g_free(command);

    gtk_widget_set_sensitive(GTK_WIDGET(self->identify_finger_button), TRUE);

    return NULL;
}

static void
cc_fingerprint_panel_identify_finger(GtkButton *button, CcFingerprintPanel *self) {
  g_thread_new(NULL, identify_finger_thread, self);
}

static void
cc_fingerprint_panel_class_init (CcFingerprintPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = cc_fingerprint_panel_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/fingerprint/cc-fingerprint-panel.ui");

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        show_list_box);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        show_unenrolled_list);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        show_enrolled_list);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        remove_finger_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        enroll_finger_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        enroll_status_label);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        finger_select_combo);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        identify_finger_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        identify_status_label);
}

static void
cc_fingerprint_panel_init (CcFingerprintPanel *self)
{
  g_resources_register (cc_fingerprint_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  system("rm -f /tmp/.enrollment_status");

  if(g_file_test("/usr/bin/droidian-fpd-client", G_FILE_TEST_EXISTS)) {
    g_signal_connect(G_OBJECT(self->remove_finger_button), "clicked", G_CALLBACK(cc_fingerprint_panel_remove_finger), self);
    g_signal_connect(G_OBJECT(self->enroll_finger_button), "clicked", G_CALLBACK(cc_fingerprint_panel_enroll_finger), self);

    gtk_widget_set_direction (GTK_WIDGET (self->show_list_box), GTK_TEXT_DIR_LTR);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->show_enrolled_list), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->enroll_finger_button), FALSE);

    g_signal_connect(GTK_TOGGLE_BUTTON(self->show_enrolled_list), "toggled", G_CALLBACK(on_show_enrolled_list_toggled), self);
    g_signal_connect(GTK_TOGGLE_BUTTON(self->show_unenrolled_list), "toggled", G_CALLBACK(on_show_unenrolled_list_toggled), self);
    g_signal_connect(G_OBJECT(self->identify_finger_button), "clicked", G_CALLBACK(cc_fingerprint_panel_identify_finger), self);

    refresh_enrolled_list(self);
    g_thread_new("update_label_thread", update_enroll_status_label, self->enroll_status_label);
  } else {
    gtk_widget_set_sensitive(GTK_WIDGET(self->remove_finger_button), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->finger_select_combo), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->enroll_finger_button), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->identify_finger_button), FALSE);
    gtk_label_set_text(GTK_LABEL(self->enroll_status_label), "");
  }
}

CcFingerprintPanel *
cc_fingerprint_panel_new (void)
{
  return CC_FINGERPRINT_PANEL (g_object_new (CC_TYPE_FINGERPRINT_PANEL, NULL));
}
