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
};

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
    } else {
        command = "systemctl disable --now nfcd";
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

          // Check if nfcd is running
          gchar *nfc_output;
          gchar *nfc_error;
          gint nfc_exit_status;
          g_spawn_command_line_sync("systemctl is-active -q nfcd", &nfc_output, &nfc_error, &nfc_exit_status, NULL);

          // If the nfcd is active, set the switch to ON
          if(nfc_exit_status == 0) {
              g_signal_handlers_block_by_func(self->nfc_enabled_switch, cc_nfc_panel_enable_nfc, self);
              gtk_switch_set_state(GTK_SWITCH(self->nfc_enabled_switch), TRUE);
              g_signal_handlers_unblock_by_func(self->nfc_enabled_switch, cc_nfc_panel_enable_nfc, self);
          } else {
              g_signal_handlers_block_by_func(self->nfc_enabled_switch, cc_nfc_panel_enable_nfc, self);
              gtk_switch_set_state(GTK_SWITCH(self->nfc_enabled_switch), FALSE);
              g_signal_handlers_unblock_by_func(self->nfc_enabled_switch, cc_nfc_panel_enable_nfc, self);
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
