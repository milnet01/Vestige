// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_theme.cpp
/// @brief Alternate-register implementations for `UITheme`.

#include "ui/ui_theme.h"

#include <algorithm>
#include <cmath>

namespace Vestige
{

namespace ui_contrast
{

namespace
{

// WCAG 2.2 sRGB-to-linear transfer for a single channel.
float srgbChannelToLinear(float c)
{
    // Channels outside [0, 1] can arise from caller arithmetic
    // (composited colours may over/underflow); clamp so the
    // piecewise formula stays in its defined domain.
    c = std::clamp(c, 0.0f, 1.0f);
    if (c <= 0.03928f)
    {
        return c / 12.92f;
    }
    return std::pow((c + 0.055f) / 1.055f, 2.4f);
}

} // namespace

float relativeLuminance(const glm::vec3& srgb)
{
    const float r = srgbChannelToLinear(srgb.r);
    const float g = srgbChannelToLinear(srgb.g);
    const float b = srgbChannelToLinear(srgb.b);
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

float contrastRatio(const glm::vec3& a, const glm::vec3& b)
{
    const float la = relativeLuminance(a);
    const float lb = relativeLuminance(b);
    const float lighter = std::max(la, lb);
    const float darker  = std::min(la, lb);
    return (lighter + 0.05f) / (darker + 0.05f);
}

glm::vec3 compositeOver(const glm::vec4& fg, const glm::vec3& bg)
{
    const float a = std::clamp(fg.a, 0.0f, 1.0f);
    return a * glm::vec3(fg) + (1.0f - a) * bg;
}

} // namespace ui_contrast

UITheme UITheme::plumbline()
{
    UITheme t;

    // Backgrounds — colder, near-black ink.
    t.bgBase           = {0.043f, 0.043f, 0.047f, 1.0f};   // #0B0B0C
    t.bgRaised         = {0.075f, 0.075f, 0.082f, 1.0f};   // #131315
    t.panelBg          = {0.075f, 0.075f, 0.082f, 0.94f};
    t.panelBgHover     = {0.110f, 0.110f, 0.122f, 0.96f};
    t.panelBgPressed   = {0.047f, 0.047f, 0.055f, 0.98f};

    // Strokes — cool bone. Phase 10.9 S9 bumped the base alpha
    // 0.12 -> 0.45 so the composited stroke clears WCAG 1.4.11
    // (>= 3:1 against Plumbline's near-black bgBase). Strong
    // bumped 0.36 -> 0.68 to preserve the hover-vs-rest distinction.
    t.panelStroke       = {0.941f, 0.933f, 0.918f, 0.45f};
    t.panelStrokeStrong = {0.941f, 0.933f, 0.918f, 0.68f};
    t.rule              = {0.941f, 0.933f, 0.918f, 0.10f};
    t.ruleStrong        = {0.941f, 0.933f, 0.918f, 0.28f};

    // Text — neutral bone with slightly cooler bias. Phase 10.9 S9
    // bumped `textDisabled` #4A4845 -> ~#918E87 so dimmed labels
    // clear WCAG 1.4.3 (>= 4.5:1) against Plumbline's near-black.
    t.textPrimary      = {0.941f, 0.933f, 0.918f};         // #F0EEEA
    t.textSecondary    = {0.553f, 0.541f, 0.518f};         // #8D8A84
    t.textDisabled     = {0.570f, 0.550f, 0.520f};         // ~#918E87 (S9).
    t.textWarning      = {0.851f, 0.627f, 0.290f};         // #D9A04A
    t.textError        = {0.800f, 0.353f, 0.243f};         // #CC5A3E

    // Accent — same warm-amber family, single hit only.
    t.accent           = {0.851f, 0.627f, 0.290f, 1.0f};
    t.accentDim        = {0.478f, 0.353f, 0.149f, 1.0f};
    t.accentInk        = {0.043f, 0.043f, 0.047f};

    // HUD — cooler crosshair to match.
    t.crosshair        = {0.941f, 0.933f, 0.918f, 0.85f};
    t.progressBarFill  = {0.851f, 0.627f, 0.290f, 1.0f};
    t.progressBarEmpty = {0.941f, 0.933f, 0.918f, 0.14f};

    // All sizing + typography stays identical between registers — both are
    // designed to share the component system. Only the palette differs.
    return t;
}

float scaleFactorOf(UIScalePreset preset)
{
    switch (preset)
    {
        case UIScalePreset::X1_0:  return 1.00f;
        case UIScalePreset::X1_25: return 1.25f;
        case UIScalePreset::X1_5:  return 1.50f;
        case UIScalePreset::X2_0:  return 2.00f;
    }
    return 1.0f;
}

UITheme UITheme::withScale(float factor) const
{
    UITheme t = *this;

    // Sizes baked at 1080p baseline — multiplied verbatim.
    t.defaultTextScale       *= factor;
    t.crosshairLength        *= factor;
    t.crosshairThickness     *= factor;
    t.progressBarHeight      *= factor;
    t.panelBorderWidth       *= factor;

    t.buttonHeight           *= factor;
    t.buttonHeightSmall      *= factor;
    t.buttonPadX             *= factor;
    t.buttonAccentTickWidth  *= factor;
    t.sliderHeight           *= factor;
    t.sliderTrackHeight      *= factor;
    t.sliderThumbSize        *= factor;
    t.sliderThumbBorder      *= factor;
    t.checkboxSize           *= factor;
    t.checkboxStroke         *= factor;
    t.dropdownHeight         *= factor;
    t.dropdownMinWidth       *= factor;
    t.dropdownMenuMaxHeight  *= factor;
    t.keybindKeyHeight       *= factor;
    t.keybindKeyMinWidth     *= factor;
    t.settingRowControlWidth *= factor;
    t.settingRowVerticalPad  *= factor;

    t.typeDisplay            *= factor;
    t.typeH1                 *= factor;
    t.typeH2                 *= factor;
    t.typeBody               *= factor;
    t.typeButton             *= factor;
    t.typeCaption            *= factor;
    t.typeMicro              *= factor;

    t.focusRingThickness     *= factor;
    t.focusRingOffset        *= factor;

    // `transitionDuration` is seconds, not pixels — intentionally unscaled.
    return t;
}

UITheme UITheme::withHighContrast() const
{
    UITheme t = *this;

    // High-contrast palette: pure-black surfaces, pure-white text, saturated
    // accent, full-alpha strokes. Anchored to WCAG 2.2 AAA contrast (≥ 7:1
    // for body text, ≥ 4.5:1 for large text) and to the IGDA GA-SIG
    // "high-contrast mode" pattern referenced in Phase 10's accessibility
    // bullets.
    t.bgBase           = {0.0f, 0.0f, 0.0f, 1.0f};
    t.bgRaised         = {0.0f, 0.0f, 0.0f, 1.0f};
    t.panelBg          = {0.0f, 0.0f, 0.0f, 1.0f};
    t.panelBgHover     = {0.12f, 0.12f, 0.12f, 1.0f};
    t.panelBgPressed   = {0.20f, 0.20f, 0.20f, 1.0f};

    t.panelStroke       = {1.0f, 1.0f, 1.0f, 1.0f};
    t.panelStrokeStrong = {1.0f, 1.0f, 1.0f, 1.0f};
    t.rule              = {1.0f, 1.0f, 1.0f, 0.85f};
    t.ruleStrong        = {1.0f, 1.0f, 1.0f, 1.0f};

    t.textPrimary      = {1.0f, 1.0f, 1.0f};
    t.textSecondary    = {1.0f, 1.0f, 1.0f};
    t.textDisabled     = {0.60f, 0.60f, 0.60f};
    t.textWarning      = {1.0f, 0.85f, 0.25f};   ///< Saturated amber.
    t.textError        = {1.0f, 0.40f, 0.30f};   ///< Saturated red-orange.

    t.accent           = {1.0f, 0.85f, 0.25f, 1.0f};
    t.accentDim        = {0.70f, 0.55f, 0.10f, 1.0f};
    t.accentInk        = {0.0f, 0.0f, 0.0f};

    t.crosshair        = {1.0f, 1.0f, 1.0f, 1.0f};
    t.progressBarFill  = {1.0f, 0.85f, 0.25f, 1.0f};
    t.progressBarEmpty = {1.0f, 1.0f, 1.0f, 0.30f};

    return t;
}

UITheme UITheme::withReducedMotion() const
{
    UITheme t = *this;
    t.transitionDuration = 0.0f;
    return t;
}

} // namespace Vestige
