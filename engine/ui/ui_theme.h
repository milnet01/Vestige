// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_theme.h
/// @brief Central theme palette + sizes consulted by every in-game UI element.
///
/// Phase 9C UI/HUD foundation. UI elements (`UILabel`, `UIPanel`,
/// `UIProgressBar`, etc.) consult `UISystem::getTheme()` for default colours
/// and sizes rather than hardcoding them at construction. Per-element
/// overrides are still allowed — the theme is a default, not a mandate.
///
/// Marketing-facing visual choices (final colour palette, typography pairing,
/// motion language) are best driven by Claude Design before locking the
/// final defaults; this struct ships sane neutrals so the engine renders
/// reasonably until the visual language is agreed.
#pragma once

#include <glm/glm.hpp>

namespace Vestige
{

/// @brief Engine-wide UI style defaults.
struct UITheme
{
    // -- Background colours --
    glm::vec4 panelBg          = {0.05f, 0.05f, 0.07f, 0.85f};
    glm::vec4 panelBgHover     = {0.10f, 0.10f, 0.13f, 0.90f};
    glm::vec4 panelBgPressed   = {0.02f, 0.02f, 0.04f, 0.95f};

    // -- Text --
    glm::vec3 textPrimary      = {0.95f, 0.95f, 0.96f};
    glm::vec3 textSecondary    = {0.65f, 0.66f, 0.68f};
    glm::vec3 textDisabled     = {0.40f, 0.40f, 0.42f};
    glm::vec3 textWarning      = {1.00f, 0.78f, 0.30f};
    glm::vec3 textError        = {1.00f, 0.40f, 0.40f};

    // -- Accent (interactive elements: buttons, sliders, focus rings) --
    glm::vec4 accent           = {0.20f, 0.65f, 1.00f, 1.00f};
    glm::vec4 accentDim        = {0.20f, 0.65f, 1.00f, 0.40f};

    // -- HUD-specific --
    glm::vec4 crosshair        = {1.00f, 1.00f, 1.00f, 0.85f};
    glm::vec4 progressBarFill  = {0.20f, 0.85f, 0.30f, 1.00f};
    glm::vec4 progressBarEmpty = {0.10f, 0.10f, 0.12f, 0.70f};

    // -- Sizes (pixels, at 1080p baseline; UI rendering does not auto-scale yet) --
    float defaultTextScale     = 1.0f;
    float crosshairLength      = 12.0f;     ///< Length of each crosshair arm.
    float crosshairThickness   = 2.0f;
    float progressBarHeight    = 18.0f;
    float panelBorderWidth     = 0.0f;      ///< 0 = no border (Step 9C-1 ships borderless).

    /// @brief Default theme — sane neutrals that ship with the engine.
    /// Game projects override fields after constructing UISystem.
    static UITheme defaultTheme() { return {}; }
};

} // namespace Vestige
