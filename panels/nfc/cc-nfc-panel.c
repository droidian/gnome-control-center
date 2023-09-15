/*
 * Copyright (C) 2023 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-nfc-panel.h"
#include "cc-nfc-resources.h"
#include "cc-util.h"

#include <adwaita.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>

struct _CcNfcPanel {
  CcPanel            parent;
  GtkWidget        *nfc_enabled_switch;
  GtkWidget        *nfc_read_button;
  GtkWidget        *nfc_read_label;
};

typedef struct {
    CcNfcPanel *self;
    gint exit_status;
} ThreadData;

G_DEFINE_TYPE (CcNfcPanel, cc_nfc_panel, CC_TYPE_PANEL)

static void
cc_nfc_panel_finalize (GObject *object)
{
  G_OBJECT_CLASS (cc_nfc_panel_parent_class)->finalize (object);
}

static void
cc_nfc_panel_enable_nfc(GtkSwitch *widget, gboolean state, CcNfcPanel *self)
{
    GError *error = NULL;
    gchar *command;
    gchar *stderr_output = NULL;
    gint exit_status;

    gchar *argv[] = {"pkexec", "env", "/bin/sh", "-c", NULL, NULL};

    if (state) {
        command = "systemctl enable --now nfcd";
        gtk_widget_set_sensitive(GTK_WIDGET(self->nfc_read_button), TRUE);
    } else {
        command = "systemctl disable --now nfcd";
        gtk_widget_set_sensitive(GTK_WIDGET(self->nfc_read_button), FALSE);
    }

    argv[4] = command;

    if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &stderr_output, &exit_status, &error)) {
        g_warning("Failed to execute command: %s", error->message);
    }

    if (exit_status != 0) {
        g_signal_handlers_block_by_func(widget, cc_nfc_panel_enable_nfc, self);
        gtk_switch_set_state(widget, !state);
        g_signal_handlers_unblock_by_func(widget, cc_nfc_panel_enable_nfc, self);
    }

    g_free(stderr_output);
    if (error) {
        g_error_free(error);
    }
}

static gboolean
update_label_idle (gpointer user_data)
{
    ThreadData *data = user_data;

    if (data->exit_status == 0) {
        gtk_label_set_text(GTK_LABEL(data->self->nfc_read_label), "Success");
    } else if (data->exit_status == 1) {
        gtk_label_set_text(GTK_LABEL(data->self->nfc_read_label), "Fail");
    } else {
        gtk_label_set_text(GTK_LABEL(data->self->nfc_read_label), "Unexpected");
    }

    g_free(data);

    return G_SOURCE_REMOVE;
}

static gpointer
nfc_read_ndef (gpointer user_data)
{
    CcNfcPanel *self = (CcNfcPanel *)user_data;
    gchar *nfc_read_error;
    gboolean spawn_success;
    gint nfc_read_exit_status;

    const gchar *command = "bash -c 'stdbuf -oL ndef-read &> /tmp/nfcd-test & counter=0; "
                           "while [ $counter -lt 20 ]; do ((counter++)); "
                           "if cat /tmp/nfcd-test | grep -vE \"Waiting for tag|Giving up\" | grep -q .; then "
                           "FOUND=1; break; fi; sleep 1; done; "
                           "pkill -15 ndef-read; "
                           "if [ \"$FOUND\" == \"1\" ]; then exit 0; else exit 1; fi'";

    spawn_success = g_spawn_command_line_sync(command,
                                              NULL,
                                              &nfc_read_error,
                                              &nfc_read_exit_status,
                                              NULL);

    if (!spawn_success) {
        nfc_read_exit_status = -1;
    }

    ThreadData *data = g_new(ThreadData, 1);
    data->self = self;
    data->exit_status = nfc_read_exit_status;

    g_idle_add(update_label_idle, data);

    g_free(nfc_read_error);

    return NULL;
}

static void
nfc_read_ndef_threaded (CcNfcPanel *self)
{
    g_thread_new("nfc_read_ndef", nfc_read_ndef, self);
}

static void
cc_nfc_panel_class_init (CcNfcPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = cc_nfc_panel_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/nfc/cc-nfc-panel.ui");

  gtk_widget_class_bind_template_child (widget_class,
                                        CcNfcPanel,
                                        nfc_enabled_switch);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcNfcPanel,
                                        nfc_read_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcNfcPanel,
                                        nfc_read_label);
}

static void
cc_nfc_panel_init (CcNfcPanel *self)
{
  g_resources_register (cc_nfc_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  gchar *output = NULL;
  gchar *error = NULL;
  gint exit_status;
  g_spawn_command_line_sync("systemctl --no-pager --quiet is-failed nfcd", &output, &error, &exit_status, NULL);

  if(g_file_test("/usr/sbin/nfcd", G_FILE_TEST_EXISTS)) {
      if(exit_status == 0) {
          gtk_widget_set_sensitive(GTK_WIDGET(self->nfc_enabled_switch), FALSE);
      } else {
          g_signal_connect(G_OBJECT(self->nfc_enabled_switch), "state-set", G_CALLBACK(cc_nfc_panel_enable_nfc), self);
          g_signal_connect(G_OBJECT(self->nfc_read_button), "clicked", G_CALLBACK(nfc_read_ndef_threaded), self);

          // Check if nfcd is running
          gchar *nfc_output;
          gchar *nfc_error;
          gint nfc_exit_status;
          g_spawn_command_line_sync("systemctl is-active -q nfcd", &nfc_output, &nfc_error, &nfc_exit_status, NULL);

          // If the nfcd is active, set the switch to ON
          if(nfc_exit_status == 0) {
              g_signal_handlers_block_by_func(self->nfc_enabled_switch, cc_nfc_panel_enable_nfc, self);
              gtk_switch_set_state(GTK_SWITCH(self->nfc_enabled_switch), TRUE);
              gtk_switch_set_active(GTK_SWITCH(self->nfc_enabled_switch), TRUE);
              g_signal_handlers_unblock_by_func(self->nfc_enabled_switch, cc_nfc_panel_enable_nfc, self);
              gtk_widget_set_sensitive(GTK_WIDGET(self->nfc_read_button), TRUE);
          } else {
              g_signal_handlers_block_by_func(self->nfc_enabled_switch, cc_nfc_panel_enable_nfc, self);
              gtk_switch_set_state(GTK_SWITCH(self->nfc_enabled_switch), FALSE);
              gtk_switch_set_active(GTK_SWITCH(self->nfc_enabled_switch), FALSE);
              g_signal_handlers_unblock_by_func(self->nfc_enabled_switch, cc_nfc_panel_enable_nfc, self);
              gtk_widget_set_sensitive(GTK_WIDGET(self->nfc_read_button), FALSE);
          }

          g_free(nfc_output);
          g_free(nfc_error);
      }
  } else {
      gtk_widget_set_sensitive(GTK_WIDGET(self->nfc_enabled_switch), FALSE);
  }

  g_free(output);
  g_free(error);
}

CcNfcPanel *
cc_nfc_panel_new (void)
{
  return CC_NFC_PANEL (g_object_new (CC_TYPE_NFC_PANEL, NULL));
}
