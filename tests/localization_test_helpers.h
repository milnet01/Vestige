// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file localization_test_helpers.h
/// @brief Tests whose expected text is English: install the shipped
///        `en.json` as the `tr` / `trf` override for the test's lifetime.
///
/// Without a registered LocalizationService, `tr` returns the key itself,
/// so a widget that builds its text from a pattern (3D_E-0024) would show
/// the key in a plain unit test.
#pragma once

#include "localization/localization_service.h"
#include "localization/string_table.h"

#include <optional>
#include <string>

namespace Vestige::TestLocalization
{

class EnglishStrings
{
public:
    EnglishStrings()
    {
        m_table.loadFromFile(std::string(VESTIGE_LOCALIZATION_DIR) + "/en.json");
        m_scope.emplace(m_table);
    }

    bool loaded() const { return m_table.size() > 0; }

private:
    StringTable                              m_table;
    std::optional<ScopedStringTableOverride> m_scope;
};

}  // namespace Vestige::TestLocalization
