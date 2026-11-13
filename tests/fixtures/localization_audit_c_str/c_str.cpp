// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT
//
// Fixture for LocalizationAuditCatchesALiteralBesideCStr. NOT compiled. Do not
// "fix" the hardcoded string below; the test asserts the audit flags it.

void drawSaved(TextRenderer* text, const std::string& name)
{
    // Deliberate violation. The line also calls c_str(), whose last three
    // letters and bracket once read as a tr() call and excused the line.
    text->renderText2D("Saved to disk", 10.0f, 10.0f, 1.0f, colourFor(name.c_str()), 800, 600);
}
