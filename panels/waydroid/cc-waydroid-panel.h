/*
 * Copyright (C) 2024 Cédric Bellegarde <cedric.bellegarde@adishatz.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>
#include <shell/cc-panel.h>

G_BEGIN_DECLS

#define CC_TYPE_WAYDROID_PANEL (cc_waydroid_panel_get_type ())
G_DECLARE_FINAL_TYPE (CcWaydroidPanel, cc_waydroid_panel, CC, WAYDROID_PANEL, CcPanel)

CcWaydroidPanel *cc_waydroid_panel_new (void);

G_END_DECLS