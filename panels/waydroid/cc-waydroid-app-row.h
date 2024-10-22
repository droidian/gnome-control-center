/*
 * Copyright (C) 2024 Cédric Bellegarde <cedric.bellegarde@adishatz.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>
#include <shell/cc-panel.h>
#include <gio/gdesktopappinfo.h>

G_BEGIN_DECLS

#define CC_TYPE_APP_ROW (cc_app_row_get_type())
G_DECLARE_FINAL_TYPE (CcAppRow, cc_app_row, CC, APP_ROW, AdwExpanderRow)

GtkWidget*  cc_app_row_new                    (GDesktopAppInfo *app_info);
GtkWidget*  cc_app_row_new_for_app_id         (const char *name,
                                               const char *app_id,
                                               const char *url);
const char* cc_app_row_get_app_id             (CcAppRow *self);
gboolean    cc_app_row_is_root                (CcAppRow *self);
gboolean    cc_app_row_can_be_downloaded      (CcAppRow *self);
void        cc_app_row_set_installed          (CcAppRow *self,
                                               gboolean  installed);
G_END_DECLS
