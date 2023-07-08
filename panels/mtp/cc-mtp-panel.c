/*
 * Copyright (C) 2023 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-mtp-panel.h"
#include "cc-mtp-resources.h"
#include "cc-util.h"

#include <adwaita.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>

struct _CcMtpPanel {
  CcPanel            parent;
  GtkWidget        *mtp_enabled_switch;
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
}

static void
cc_mtp_panel_init (CcMtpPanel *self)
{
  g_resources_register (cc_mtp_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  gchar *output;
  gchar *error;
  gint exit_status;
  g_spawn_command_line_sync("systemctl is-active usb-tethering", &output, &error, &exit_status, NULL);

  if(g_str_has_prefix(output, "active")) {
      gtk_widget_set_sensitive(GTK_WIDGET(self->mtp_enabled_switch), FALSE);
  } else {
      if(g_file_test("/usr/bin/mtp-server", G_FILE_TEST_EXISTS)) {
          g_signal_connect(G_OBJECT(self->mtp_enabled_switch), "state-set", G_CALLBACK(cc_mtp_panel_enable_mtp), self);

          // Check if mtp-server is running
          gchar *mtp_output;
          gchar *mtp_error;
          gint mtp_exit_status;
          g_spawn_command_line_sync("systemctl --user is-active mtp-server", &mtp_output, &mtp_error, &mtp_exit_status, NULL);

          // If the mtp-server is active, set the switch to ON
          if(g_str_has_prefix(mtp_output, "active")) {
              gtk_switch_set_state(GTK_SWITCH(self->mtp_enabled_switch), TRUE);
          } else {
              gtk_switch_set_state(GTK_SWITCH(self->mtp_enabled_switch), FALSE);
          }

          g_free(mtp_output);
          g_free(mtp_error);
      } else {
          gtk_widget_set_sensitive(GTK_WIDGET(self->mtp_enabled_switch), FALSE);
      }
  }

  g_free(output);
  g_free(error);
}

CcMtpPanel *
cc_mtp_panel_new (void)
{
  return CC_MTP_PANEL (g_object_new (CC_TYPE_MTP_PANEL, NULL));
}
