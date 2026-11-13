// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT
//
// Fixture for LocalizationAuditCountsACallSiteOnce. NOT compiled. One tr()
// call with a literal key: the audit's clean message must count one call site.

void drawStart(UILabel& label)
{
    label.text = tr("ui.menu.start_game");
}
