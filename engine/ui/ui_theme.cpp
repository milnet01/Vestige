// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_theme.cpp
/// @brief Alternate-register implementations for `UITheme`.

#include "ui/ui_theme.h"

namespace Vestige
{

UITheme UITheme::plumbline()
{
    UITheme t;

    // Backgrounds — colder, near-black ink.
    t.bgBase           = {0.043f, 0.043f, 0.047f, 1.0f};   // #0B0B0C
    t.bgRaised         = {0.075f, 0.075f, 0.082f, 1.0f};   // #131315
    t.panelBg          = {0.075f, 0.075f, 0.082f, 0.94f};
    t.panelBgHover     = {0.110f, 0.110f, 0.122f, 0.96f};
    t.panelBgPressed   = {0.047f, 0.047f, 0.055f, 0.98f};

    // Strokes — cool bone, lower alpha.
    t.panelStroke       = {0.941f, 0.933f, 0.918f, 0.12f};
    t.panelStrokeStrong = {0.941f, 0.933f, 0.918f, 0.36f};
    t.rule              = {0.941f, 0.933f, 0.918f, 0.10f};
    t.ruleStrong        = {0.941f, 0.933f, 0.918f, 0.28f};

    // Text — neutral bone with slightly cooler bias.
    t.textPrimary      = {0.941f, 0.933f, 0.918f};         // #F0EEEA
    t.textSecondary    = {0.553f, 0.541f, 0.518f};         // #8D8A84
    t.textDisabled     = {0.290f, 0.282f, 0.271f};         // #4A4845
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

} // namespace Vestige
