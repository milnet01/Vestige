// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file validation_panel.h
/// @brief Scene validation panel — checks for common issues and missing assets.
#pragma once

#include "editor/panels/i_panel.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Vestige
{

class Scene;
class Selection;

/// @brief Severity levels for validation warnings.
enum class ValidationSeverity
{
    INFO,
    WARNING,
    ERROR_SEV
};

/// @brief A single validation result entry.
struct ValidationEntry
{
    ValidationSeverity severity = ValidationSeverity::WARNING;
    std::string message;
    uint32_t entityId = 0;  ///< Entity related to this warning (0 = scene-level).
};

/// @brief Validates scenes for common issues and displays results.
///
/// Checks for: missing textures, lights without shadows, entities far from origin,
/// mesh renderers with null meshes, and other potential problems.
class ValidationPanel : public IPanel
{
public:
    const char* displayName() const override { return "Scene Validation"; }

    /// @brief Draws the validation panel.
    /// @param scene Active scene to validate.
    /// @param selection Editor selection (for click-to-select on warnings).
    void draw(Scene* scene, Selection& selection);

    bool isOpen() const override { return m_open; }
    void setOpen(bool open) override { m_open = open; }
    void toggleOpen() { m_open = !m_open; }

    /// @brief Returns the number of issues found in the last validation.
    size_t getIssueCount() const { return m_entries.size(); }

private:
    void validate(Scene& scene);

    bool m_open = false;
    std::vector<ValidationEntry> m_entries;
    bool m_hasValidated = false;
};

} // namespace Vestige
