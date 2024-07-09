/*
 * Copyright (C) 2024 Cedric Bellegarde <cedric.bellegarde@adishatz.org>
 * Copyright (C) 2023 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>
#include <shell/cc-panel.h>

G_BEGIN_DECLS

#define CC_TYPE_USB_PANEL (cc_usb_panel_get_type ())
G_DECLARE_FINAL_TYPE (CcUsbPanel, cc_usb_panel, CC, USB_PANEL, CcPanel)

CcUsbPanel *cc_usb_panel_new (void);

G_END_DECLS
