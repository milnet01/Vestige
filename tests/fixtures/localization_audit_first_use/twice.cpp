// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT
//
// Fixture for LocalizationAuditNamesTheFirstUse (3D_E-0874). NOT compiled.
// The same missing key twice: the report must name the first line.
struct Item { const char* key; };
const Item kItems[] = {
    {"ui.fixture.missing"},
    {"ui.fixture.present"},
    {"ui.fixture.missing"},
};
