// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_frame_hook.h
/// @brief An invisible element that runs a function once a frame, when the
///        canvas reaches it in draw order (3D_E-0035).
///
/// A screen adds one ahead of the widgets it keeps in step with outside
/// state — the Settings page copies `SettingsEditor::pending()` into its
/// controls, the footer enables Apply while there are unsaved changes — so
/// those widgets draw the current state in the same frame.
#pragma once

#include "ui/ui_element.h"

#include <functional>
#include <utility>

namespace Vestige
{

class UIFrameHook : public UIElement
{
public:
    explicit UIFrameHook(std::function<void()> onFrame)
        : m_onFrame(std::move(onFrame))
    {
        size = {0.0f, 0.0f};
    }

    void render(SpriteBatchRenderer& /*batch*/, const glm::vec2& /*parentOffset*/,
                int /*screenWidth*/, int /*screenHeight*/) override
    {
        if (m_onFrame)
        {
            m_onFrame();
        }
    }

    /// @brief Runs the function now; tests call this in place of a frame.
    void runNow() const
    {
        if (m_onFrame)
        {
            m_onFrame();
        }
    }

private:
    std::function<void()> m_onFrame;
};

} // namespace Vestige
