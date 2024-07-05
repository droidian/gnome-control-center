/*
 * Copyright (C) 2024 Cédric Bellegarde <cedric.bellegarde@adishatz.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>
#include <shell/cc-panel.h>

G_BEGIN_DECLS

#define CC_TYPE_FINGERPRINT_PANEL (cc_fingerprint_panel_get_type ())
G_DECLARE_FINAL_TYPE (CcFingerprintPanel, cc_fingerprint_panel, CC, FINGERPRINT_PANEL, CcPanel)

CcFingerprintPanel *cc_fingerprint_panel_new (void);

G_END_DECLS
