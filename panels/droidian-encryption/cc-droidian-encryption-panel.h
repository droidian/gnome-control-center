/*
 * Copyright (C) 2023 Eugenio "g7" Paolantonio <me@medesimo.eu>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>
#include <shell/cc-panel.h>

G_BEGIN_DECLS

typedef enum
{
  ENCRYPTION_SERVICE_STATUS_UNKNOWN = 0,
  ENCRYPTION_SERVICE_STATUS_UNSUPPORTED,
  ENCRYPTION_SERVICE_STATUS_UNCONFIGURED,
  ENCRYPTION_SERVICE_STATUS_CONFIGURING,
  ENCRYPTION_SERVICE_STATUS_CONFIGURED,
  ENCRYPTION_SERVICE_STATUS_ENCRYPTING,
  ENCRYPTION_SERVICE_STATUS_ENCRYPTED,
  ENCRYPTION_SERVICE_STATUS_FAILED,
} EncryptionServiceStatus;

#define CC_TYPE_DROIDIAN_ENCRYPTION_PANEL (cc_droidian_encryption_panel_get_type ())
G_DECLARE_FINAL_TYPE (CcDroidianEncryptionPanel, cc_droidian_encryption_panel, CC, DROIDIAN_ENCRYPTION_PANEL, CcPanel)

CcDroidianEncryptionPanel *cc_droidian_encryption_panel_new (void);

G_END_DECLS
