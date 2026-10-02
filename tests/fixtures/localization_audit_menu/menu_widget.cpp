// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT
//
// Fixture for LocalizationAuditCatchesMenuLiteral (3D_E-0024 INV-6). NOT
// compiled. Its only defect is a literal passed to the menu builders' label
// helper; every key it uses is present, so only the sink check can fail it.

void buildFixtureMenu(UICanvas& canvas)
{
    canvas.addElement(makeLabel("Start Game", {0, 0}, 1.0f, {}, nullptr));
    // Controls: keyed text and an exempted proper noun must not be flagged.
    canvas.addElement(makeLabel(std::string(tr("ui.fixture.present")), {0, 40}, 1.0f, {}, nullptr));
    canvas.addElement(makeLabel("Vestige", {0, 80}, 1.0f, {}, nullptr));  // i18n-exempt
}
