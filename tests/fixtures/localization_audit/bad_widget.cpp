// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT
//
// Fixture for L6 test 21 (LocalizationAuditCatchesHardcoded). NOT compiled —
// it exists only so tools/localization_audit.py has a known violation to
// detect. Do not "fix" the hardcoded string below; the test asserts the audit
// flags it.

void drawBadWidget(TextRenderer* tr)
{
    // Deliberate violation: a user-visible literal at a sink, not wrapped in
    // tr(). The audit must flag this and exit non-zero in --strict mode.
    tr->renderText2D("Start Game", 10.0f, 10.0f, 1.0f, {}, 800, 600);

    // Control: an exempted line must NOT be flagged (pins the suppression).
    tr->renderText2D("DEBUG fps", 10.0f, 40.0f, 1.0f, {}, 800, 600); // i18n-exempt
}
