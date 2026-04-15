// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_system.cpp
/// @brief UISystem implementation.
#include "systems/ui_system.h"
#include "core/engine.h"
#include "core/logger.h"

#include <glad/gl.h>

namespace Vestige
{

bool UISystem::initialize(Engine& engine)
{
    m_engine = &engine;

    if (!m_spriteBatch.initialize(engine.getAssetPath()))
    {
        Logger::warning("[UISystem] Sprite batch renderer initialization failed "
                        "— in-game UI will be unavailable");
    }

    Logger::info("[UISystem] Initialized");
    return true;
}

void UISystem::shutdown()
{
    m_canvas.clear();
    m_spriteBatch.shutdown();
    m_engine = nullptr;
    Logger::info("[UISystem] Shut down");
}

void UISystem::update(float /*deltaTime*/)
{
    // Hit testing for interactive elements will be added when InputManager
    // is exposed through Engine. For now, no UI elements are interactive.
    m_wantsCaptureInput = false;
}

void UISystem::renderUI(int screenWidth, int screenHeight)
{
    if (!m_spriteBatch.isInitialized() || m_canvas.getElementCount() == 0)
    {
        return;
    }

    // Save GL state
    GLboolean depthWasEnabled;
    glGetBooleanv(GL_DEPTH_TEST, &depthWasEnabled);
    GLboolean blendWasEnabled;
    glGetBooleanv(GL_BLEND, &blendWasEnabled);

    // Set up 2D overlay state
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Render all canvas elements
    m_spriteBatch.begin(screenWidth, screenHeight);
    m_canvas.render(m_spriteBatch, screenWidth, screenHeight);
    m_spriteBatch.end();

    // Restore GL state
    if (depthWasEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blendWasEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
}

} // namespace Vestige
