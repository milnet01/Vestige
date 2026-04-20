// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_theme.h
/// @brief Central theme palette + sizes consulted by every in-game UI element.
///
/// Phase 9C UI/HUD foundation. UI elements (`UILabel`, `UIPanel`,
/// `UIButton`, `UIProgressBar`, etc.) consult `UISystem::getTheme()` for
/// default colours and sizes rather than hardcoding them at construction.
///
/// Defaults map to the **Vellum** register from the
/// `vestige-ui-hud-inworld` design hand-off (Claude Design, 2026-04-20):
/// warm parchment / bone text on deep walnut-ink with burnished-brass
/// accent. The alternative **Plumbline** register (monastic minimal,
/// near-black + single accent) is documented in the spec sheet and can be
/// applied by overriding individual fields at startup.
#pragma once

#include <glm/glm.hpp>

#include <string>

namespace Vestige
{

/// @brief Engine-wide UI style defaults.
///
/// Field naming mirrors the CSS variables in
/// `vestige-ui-hud-inworld/project/styles.css` so design updates can be
/// translated 1:1.
struct UITheme
{
    // -- Backgrounds --
    glm::vec4 bgBase           = {0.078f, 0.071f, 0.063f, 1.0f};   ///< #141210 — deep walnut-ink.
    glm::vec4 bgRaised         = {0.110f, 0.098f, 0.086f, 1.0f};   ///< #1c1916 — slightly lifted surface.
    glm::vec4 panelBg          = {0.110f, 0.098f, 0.086f, 0.92f};  ///< rgba(28,25,22,0.92).
    glm::vec4 panelBgHover     = {0.149f, 0.133f, 0.118f, 0.94f};  ///< rgba(38,34,30,0.94).
    glm::vec4 panelBgPressed   = {0.086f, 0.075f, 0.063f, 0.96f};  ///< rgba(22,19,16,0.96).

    // -- Strokes / rules (warm bone, alpha-modulated) --
    glm::vec4 panelStroke       = {0.839f, 0.769f, 0.635f, 0.22f}; ///< Hairline border.
    glm::vec4 panelStrokeStrong = {0.839f, 0.769f, 0.635f, 0.48f}; ///< Hover / active border.
    glm::vec4 rule              = {0.839f, 0.769f, 0.635f, 0.18f}; ///< Hair separator.
    glm::vec4 ruleStrong        = {0.839f, 0.769f, 0.635f, 0.42f}; ///< Strong separator.

    // -- Text --
    glm::vec3 textPrimary      = {0.918f, 0.875f, 0.780f};         ///< #EADFC7 — warm bone.
    glm::vec3 textSecondary    = {0.659f, 0.604f, 0.494f};         ///< #A89A7E.
    glm::vec3 textDisabled     = {0.361f, 0.329f, 0.278f};         ///< #5C5447.
    glm::vec3 textWarning      = {0.878f, 0.659f, 0.353f};         ///< #E0A85A.
    glm::vec3 textError        = {0.839f, 0.416f, 0.310f};         ///< #D66A4F.

    // -- Accent (interactive: focus rings, buttons, sliders, primary actions) --
    glm::vec4 accent           = {0.784f, 0.604f, 0.243f, 1.0f};   ///< #C89A3E — burnished brass.
    glm::vec4 accentDim        = {0.541f, 0.416f, 0.165f, 1.0f};   ///< #8A6A2A.
    glm::vec3 accentInk        = {0.078f, 0.071f, 0.063f};         ///< #141210 — text drawn on accent fill.

    // -- HUD-specific --
    glm::vec4 crosshair        = {0.918f, 0.875f, 0.780f, 0.85f};  ///< matches textPrimary, slightly translucent.
    glm::vec4 progressBarFill  = {0.784f, 0.604f, 0.243f, 1.0f};   ///< accent.
    glm::vec4 progressBarEmpty = {0.918f, 0.875f, 0.780f, 0.18f};  ///< textPrimary @ α 0.18.

    // -- Sizes (pixels, at 1080p baseline) --
    float defaultTextScale     = 1.0f;
    float crosshairLength      = 12.0f;     ///< Length of each crosshair arm.
    float crosshairThickness   = 2.0f;
    float progressBarHeight    = 18.0f;
    float panelBorderWidth     = 1.0f;      ///< Hairline border on panels.

    // -- Component sizing (matches Claude Design spec sheet) --
    float buttonHeight         = 56.0f;
    float buttonHeightSmall    = 40.0f;
    float buttonPadX           = 24.0f;
    float buttonAccentTickWidth = 4.0f;     ///< Accent strip on hover for non-primary buttons.
    float sliderHeight         = 44.0f;
    float sliderTrackHeight    = 4.0f;
    float sliderThumbSize      = 16.0f;
    float sliderThumbBorder    = 2.0f;
    float checkboxSize         = 20.0f;
    float checkboxStroke       = 1.5f;
    float dropdownHeight       = 40.0f;
    float dropdownMinWidth     = 220.0f;
    float dropdownMenuMaxHeight = 240.0f;
    float keybindKeyHeight     = 36.0f;
    float keybindKeyMinWidth   = 96.0f;
    float settingRowControlWidth = 340.0f;
    float settingRowVerticalPad  = 16.0f;

    // -- Type sizes (px @ 1080p) --
    float typeDisplay          = 88.0f;
    float typeH1               = 42.0f;
    float typeH2               = 26.0f;
    float typeBody             = 18.0f;
    float typeButton           = 20.0f;
    float typeCaption          = 14.0f;
    float typeMicro            = 12.0f;

    // -- Type families (logical names; engine resolves to actual fonts) --
    std::string fontDisplay    = "Cormorant Garamond";   ///< Wordmark / modal titles.
    std::string fontUI         = "Inter Tight";          ///< Body / button / labels.
    std::string fontMono       = "JetBrains Mono";       ///< Captions, micro, numerics, key-caps.

    // -- Motion --
    float transitionDuration   = 0.140f;    ///< Seconds; Reduce-Motion sets this to 0 at runtime.
    float focusRingThickness   = 2.0f;
    float focusRingOffset      = 3.0f;

    /// @brief Default theme — Vellum register (warm parchment on deep ink).
    static UITheme defaultTheme() { return {}; }

    /// @brief The Plumbline alternative — monastic minimal, near-black + single accent.
    static UITheme plumbline();
};

} // namespace Vestige
