/*
 * Copyright (C) 2023 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>
#include <shell/cc-panel.h>

G_BEGIN_DECLS

#define CC_TYPE_WAYDROID_PANEL (cc_waydroid_panel_get_type ())
G_DECLARE_FINAL_TYPE (CcWaydroidPanel, cc_waydroid_panel, CC, WAYDROID_PANEL, CcPanel)

void on_dialog_response(GtkDialog *dialog, gint response_id, CcWaydroidPanel *self);

CcWaydroidPanel *cc_waydroid_panel_new (void);

G_END_DECLS
