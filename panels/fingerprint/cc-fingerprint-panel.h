/*
 * Copyright (C) 2023 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>
#include <shell/cc-panel.h>

G_BEGIN_DECLS

#define CC_TYPE_FINGERPRINT_PANEL (cc_fingerprint_panel_get_type ())
G_DECLARE_FINAL_TYPE (CcFingerprintPanel, cc_fingerprint_panel, CC, FINGERPRINT_PANEL, CcPanel)

void refresh_enrolled_list(CcFingerprintPanel *self);
void refresh_unenrolled_list(CcFingerprintPanel *self);
gboolean on_show_enrolled_list_toggled(GtkToggleButton *togglebutton, gpointer user_data);
gboolean on_show_unenrolled_list_toggled(GtkToggleButton *togglebutton, gpointer user_data);

CcFingerprintPanel *cc_fingerprint_panel_new (void);

gchar *last_status = NULL;

G_END_DECLS
