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
    // The cursor-over-interactive cache is maintained by `updateMouseHit()`
    // (called from the engine input loop). Modal capture is sticky until
    // game code clears it via `setModalCapture(false)`. Nothing to do here.
}

void UISystem::updateMouseHit(const glm::vec2& cursor, int screenWidth, int screenHeight)
{
    m_cursorOverInteractive = m_canvas.hitTest(cursor, screenWidth, screenHeight);
}

void UISystem::setBaseTheme(const UITheme& base)
{
    m_baseTheme = base;
    rebuildTheme();
}

void UISystem::setScalePreset(UIScalePreset preset)
{
    m_scalePreset = preset;
    rebuildTheme();
}

void UISystem::setHighContrastMode(bool enabled)
{
    m_highContrast = enabled;
    rebuildTheme();
}

void UISystem::setReducedMotion(bool enabled)
{
    m_reducedMotion = enabled;
    rebuildTheme();
}

void UISystem::rebuildTheme()
{
    UITheme t = m_baseTheme.withScale(scaleFactorOf(m_scalePreset));
    if (m_highContrast)
    {
        t = t.withHighContrast();
    }
    if (m_reducedMotion)
    {
        t = t.withReducedMotion();
    }
    m_theme = t;
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
