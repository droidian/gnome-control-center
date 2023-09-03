/*
 * Copyright (C) 2023 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-mtp-panel.h"
#include "cc-mtp-resources.h"
#include "cc-util.h"

#include <adwaita.h>
#include <gtk/gtk.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>

struct _CcMtpPanel {
  CcPanel            parent;
  GtkWidget        *mtp_enabled_switch;
  GtkComboBoxText  *usb_state_dropdown;
  GtkWidget        *help_button;
};

G_DEFINE_TYPE (CcMtpPanel, cc_mtp_panel, CC_TYPE_PANEL)

static void
cc_mtp_panel_finalize (GObject *object)
{
  G_OBJECT_CLASS (cc_mtp_panel_parent_class)->finalize (object);
}

static gboolean
cc_mtp_panel_enable_mtp(GtkSwitch *widget, gboolean state, CcMtpPanel *self) {
  if(state) {
    system("rm -f ~/.mtp_disable");
    system("systemctl --user start mtp-server");
  } else {
    system("touch ~/.mtp_disable");
    system("systemctl --user stop mtp-server");
  }

  return FALSE;
}

static gchar *get_config_file_path() {
  if (g_file_test("/usr/lib/droidian/device/mtp-configfs.conf", G_FILE_TEST_EXISTS)) {
    return "/usr/lib/droidian/device/mtp-configfs.conf";
  } else if (g_file_test("/etc/mtp-configfs.conf", G_FILE_TEST_EXISTS)) {
    return "/etc/mtp-configfs.conf";
  }

  return NULL;
}

static void
cc_mtp_panel_help_button_clicked (GtkButton *button, CcMtpPanel *self)
{
  const gchar *selected_mode = gtk_combo_box_get_active_id(GTK_COMBO_BOX(self->usb_state_dropdown));
  gchar *message;

  if (g_strcmp0(selected_mode, "mtp") == 0) {
    message = "MTP: Media Transfer Protocol, allows you to transfer files via a USB connection";
  } else if (g_strcmp0(selected_mode, "rndis") == 0) {
    message = "RNDIS: Remote Network Driver Interface Specification, allows for USB networking and SSH over a USB connection";
  } else {
    message = "None: Disables all special USB functionalities.";
  }

  GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_ancestor(GTK_WIDGET(self), GTK_TYPE_WINDOW)), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", message);

  g_signal_connect(dialog, "response", G_CALLBACK(gtk_window_destroy), NULL);

  gtk_window_present(GTK_WINDOW(dialog));
}

static void
cc_mtp_panel_usb_state_changed(GtkComboBox *widget, CcMtpPanel *self)
{
  const gchar *new_mode = gtk_combo_box_get_active_id(widget);
  GError *error = NULL;
  gchar *config_file_path = get_config_file_path();

  if (new_mode && config_file_path) {
    gchar *contents;
    if (g_file_get_contents(config_file_path, &contents, NULL, &error)) {
      gchar **lines = g_strsplit(contents, "\n", -1);
      GString *new_contents = g_string_new("");
      gboolean line_replaced = FALSE;

      for (gint i = 0; lines[i] != NULL; i++) {
        if (g_str_has_prefix(lines[i], "USBMODE=")) {
          g_string_append_printf(new_contents, "USBMODE=%s\n", new_mode);
          line_replaced = TRUE;
        } else {
          g_string_append_printf(new_contents, "%s\n", lines[i]);
        }
      }

      if (!line_replaced) {
        g_string_append_printf(new_contents, "USBMODE=%s\n", new_mode);
      }

      FILE *fp = fopen(config_file_path, "w");
      if (fp) {
        if (fwrite(new_contents->str, sizeof(char), strlen(new_contents->str), fp) < strlen(new_contents->str)) {
          g_printerr("Failed to write to the file.\n");
        }
        fclose(fp);
      } else {
        g_printerr("Failed to open the file for writing.\n");
      }

      g_string_free(new_contents, TRUE);
      g_strfreev(lines);
      g_free(contents);
    } else {
      g_printerr("Failed to read the file: %s\n", error->message);
      g_error_free(error);
    }
  } else {
    g_printerr("Either no new mode specified or config file does not exist.\n");
  }
}

static void
cc_mtp_panel_class_init (CcMtpPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = cc_mtp_panel_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/mtp/cc-mtp-panel.ui");

  gtk_widget_class_bind_template_child (widget_class,
                                        CcMtpPanel,
                                        mtp_enabled_switch);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcMtpPanel,
                                        usb_state_dropdown);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcMtpPanel,
                                        help_button);
}

static void
cc_mtp_panel_init (CcMtpPanel *self)
{
  g_resources_register (cc_mtp_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  gchar *config_file_path = get_config_file_path();
  gboolean mtp_supported = g_file_test("/usr/lib/droidian/device/mtp-supported", G_FILE_TEST_EXISTS);

  if (!mtp_supported || !config_file_path) {
    gtk_widget_set_sensitive(GTK_WIDGET(self->mtp_enabled_switch), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->usb_state_dropdown), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->help_button), FALSE);
  } else {
    if (g_file_test("/usr/bin/mtp-server", G_FILE_TEST_EXISTS)) {
      g_signal_connect(G_OBJECT(self->mtp_enabled_switch), "state-set", G_CALLBACK(cc_mtp_panel_enable_mtp), self);
      g_signal_connect(G_OBJECT(self->usb_state_dropdown), "changed", G_CALLBACK(cc_mtp_panel_usb_state_changed), self);
      g_signal_connect(G_OBJECT(self->help_button), "clicked", G_CALLBACK(cc_mtp_panel_help_button_clicked), self);

      gchar *mtp_output;
      gchar *mtp_error;
      gint mtp_exit_status;
      g_spawn_command_line_sync("systemctl --user is-active mtp-server", &mtp_output, &mtp_error, &mtp_exit_status, NULL);

      if (g_str_has_prefix(mtp_output, "active")) {
          gtk_switch_set_state(GTK_SWITCH(self->mtp_enabled_switch), TRUE);
      } else {
          gtk_switch_set_state(GTK_SWITCH(self->mtp_enabled_switch), FALSE);
      }

      g_free(mtp_output);
      g_free(mtp_error);
    } else {
      gtk_widget_set_sensitive(GTK_WIDGET(self->mtp_enabled_switch), FALSE);
    }

    gchar *config_contents;
    if (g_file_get_contents(config_file_path, &config_contents, NULL, NULL)) {
      gchar **lines = g_strsplit(config_contents, "\n", -1);
      for (gint i = 0; lines[i] != NULL; i++) {
        if (g_str_has_prefix(lines[i], "USBMODE=")) {
          gchar **tokens = g_strsplit(lines[i], "=", 2);
          if (tokens[1] != NULL) {
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(self->usb_state_dropdown), tokens[1]);
          }

          g_strfreev(tokens);
          break;
        }
      }

      g_strfreev(lines);
      g_free(config_contents);
    }
  }
}

CcMtpPanel *
cc_mtp_panel_new (void)
{
  return CC_MTP_PANEL (g_object_new (CC_TYPE_MTP_PANEL, NULL));
}
