/*
 * Copyright (C) 2023 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>
#include <shell/cc-panel.h>

G_BEGIN_DECLS

#define CC_TYPE_NFC_PANEL (cc_nfc_panel_get_type ())
G_DECLARE_FINAL_TYPE (CcNfcPanel, cc_nfc_panel, CC, NFC_PANEL, CcPanel)

CcNfcPanel *cc_nfc_panel_new (void);

G_END_DECLS
