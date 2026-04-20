// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_button.h
/// @brief Interactive button widget — default / primary / ghost / danger / sm variants.
///
/// Phase 9C UI batch 3 — translates the `.btn` family from the
/// `vestige-ui-hud-inworld` design hand-off into a native `UIElement`.
#pragma once

#include "ui/ui_element.h"
#include "ui/ui_theme.h"

#include <glm/glm.hpp>

#include <string>

namespace Vestige
{

class TextRenderer;

/// @brief Visual style variant for `UIButton`.
enum class UIButtonStyle
{
    DEFAULT,   ///< Outlined, accent tick on hover. Used for menu items.
    PRIMARY,   ///< Filled accent — emphasised primary action.
    GHOST,     ///< No border at rest, gains border on hover. Used for close/back.
    DANGER,    ///< Outlined like DEFAULT; hover tints toward error red.
};

/// @brief Pressed-state hint for input-driven visual feedback.
enum class UIButtonState
{
    NORMAL,
    HOVERED,
    PRESSED,
    DISABLED,
};

/// @brief Sub-display label rendered as a key-cap on the right of the button
///        (matches the `.kbd` element from the design — used for shortcut hints).
struct UIButtonShortcut
{
    std::string text;     ///< e.g. "ESC", "F5", "↵"
    bool        present = false;
};

/// @brief Native menu button matching the design's `.btn` component.
class UIButton : public UIElement
{
public:
    UIButton();

    void render(SpriteBatchRenderer& batch, const glm::vec2& parentOffset,
                int screenWidth, int screenHeight) override;

    /// @brief Button label text.
    std::string label;

    /// @brief Optional key-cap shortcut hint (e.g. ESC, F5).
    UIButtonShortcut shortcut;

    /// @brief Style variant.
    UIButtonStyle style = UIButtonStyle::DEFAULT;

    /// @brief Use the smaller (40 px) button height. Matches `.btn--sm`.
    bool small = false;

    /// @brief Disabled — renders disabled colour, ignores hover/press.
    bool disabled = false;

    /// @brief Externally-set state hint (drive hover/press from input handler).
    /// Default NORMAL. The element's own `interactive` + click signals are
    /// preserved for compatibility, but state is set explicitly here so the
    /// rendering is deterministic and testable without input plumbing.
    UIButtonState state = UIButtonState::NORMAL;

    /// @brief Theme reference (set by UISystem during init).
    const UITheme* theme = nullptr;

    /// @brief Text renderer (set by UISystem during init).
    TextRenderer* textRenderer = nullptr;

    /// @brief Returns the current button height in pixels (depends on `small`).
    float effectiveHeight() const;
};

} // namespace Vestige
