// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_keybind_row.h
/// @brief Key-rebind row — label + key-cap button + CLEAR.
#pragma once

#include "ui/ui_element.h"
#include "ui/ui_theme.h"

#include <string>

namespace Vestige
{

class TextRenderer;

/// @brief Settings key-rebind row matching the design's `.rebind` component.
///
/// Layout: `label 1fr · key-cap 96 min · CLEAR`. The key-cap shows the
/// currently bound key (mono uppercase). When `listening == true`, the
/// key-cap shows "PRESS ANY KEY…" and the border / text colour shifts to
/// accent. `keyText.empty() && !listening` renders an em-dash to indicate
/// "unbound".
class UIKeybindRow : public UIElement
{
public:
    UIKeybindRow();

    void render(SpriteBatchRenderer& batch, const glm::vec2& parentOffset,
                int screenWidth, int screenHeight) override;

    std::string label;
    std::string keyText;       ///< Currently bound key (e.g. "W", "Space").
    bool        listening = false;
    bool        hovered   = false;

    const UITheme* theme = nullptr;
    TextRenderer*  textRenderer = nullptr;
};

} // namespace Vestige
