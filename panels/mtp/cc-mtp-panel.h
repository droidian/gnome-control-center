/*
 * Copyright (C) 2023 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>
#include <shell/cc-panel.h>

G_BEGIN_DECLS

#define CC_TYPE_MTP_PANEL (cc_mtp_panel_get_type ())
G_DECLARE_FINAL_TYPE (CcMtpPanel, cc_mtp_panel, CC, MTP_PANEL, CcPanel)

CcMtpPanel *cc_mtp_panel_new (void);

G_END_DECLS
