// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT
//
// Fixture for LocalizationAuditCatchesMissingTableKey (3D_E-0024 INV-6).
// NOT compiled. Its only defect is a key held in a table that the fixture's
// en.json lacks, which matching tr() calls alone would never see.

struct Item { const char* key; };
const Item kItems[] = {
    {"ui.fixture.present"},   // control: in en.json, must not be flagged
    {"ui.fixture.missing"},   // deliberate defect: absent from en.json
};
