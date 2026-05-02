// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_system.cpp
/// @brief UISystem implementation.
#include "systems/ui_system.h"
#include "core/engine.h"
#include "core/logger.h"
#include "renderer/renderer.h"
#include "renderer/text_renderer.h"
#include "ui/menu_prefabs.h"
#include "ui/subtitle_renderer.h"

#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace Vestige
{

namespace
{

// S4: depth-first walk; a child's tab position follows the parent's.
// Invisible subtrees are skipped wholesale — a screen reader wouldn't
// announce them either (`collectAccessible` uses the same rule).
void collectTabOrderRecursive(const UIElement* el,
                               std::vector<UIElement*>& out)
{
    if (el == nullptr || !el->visible)
    {
        return;
    }
    if (el->interactive)
    {
        out.push_back(const_cast<UIElement*>(el));
    }
    for (size_t i = 0; i < el->getChildCount(); ++i)
    {
        collectTabOrderRecursive(el->getChildAt(i), out);
    }
}

std::vector<UIElement*> collectTabOrderFromCanvas(const UICanvas& canvas)
{
    std::vector<UIElement*> out;
    for (size_t i = 0; i < canvas.getElementCount(); ++i)
    {
        collectTabOrderRecursive(canvas.getElementAt(i), out);
    }
    return out;
}

} // namespace

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

void UISystem::update(float deltaTime)
{
    // The cursor-over-interactive cache is maintained by `updateMouseHit()`
    // (called from the engine input loop). Modal capture is sticky until
    // game code clears it via `setModalCapture(false)`.
    //
    // Slice 12.4 — tick the notification queue so toasts fade in, plateau,
    // and fade out automatically. The active theme's `transitionDuration`
    // drives the envelope; reduced-motion collapses it to a rectangle.
    m_notifications.advance(deltaTime, m_theme.transitionDuration);
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

void UISystem::applyAccessibilityBatch(UIScalePreset scale,
                                        bool highContrast,
                                        bool reducedMotion)
{
    m_scalePreset   = scale;
    m_highContrast  = highContrast;
    m_reducedMotion = reducedMotion;
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
    // Phase 10.7 slice B2 — subtitles share the overlay pass. They
    // always run when the engine has captions active, even if the
    // canvas is empty (accessibility baseline must not depend on
    // game code populating a UICanvas first).
    const bool rootHasElements  = m_canvas.getElementCount() > 0;
    const bool modalHasElements = m_modalCanvas.getElementCount() > 0;
    const bool hasSubtitles =
        m_engine != nullptr &&
        !m_engine->getSubtitleQueue().activeSubtitles().empty();
    if (!m_spriteBatch.isInitialized() ||
        (!rootHasElements && !modalHasElements && !hasSubtitles))
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

    // Render root canvas first, then modal overlay on top. Both share the
    // same sprite-batch pass so draw order is strictly back-to-front.
    //
    // Phase 10.9 Pe1 — open a frame-scoped TextRenderer batch so every
    // widget's `renderText2D` call accumulates into one upload + draw
    // at `endBatch2D`. Pre-Pe1 a typical HUD frame issued ~18 separate
    // text draws (FPS counter + each menu label + each keybind row +
    // subtitles + toasts).
    TextRenderer* textPtrForBatch = (m_engine != nullptr)
        ? m_engine->getRenderer().getTextRenderer()
        : nullptr;
    if (textPtrForBatch != nullptr && textPtrForBatch->isInitialized())
    {
        textPtrForBatch->beginBatch2D(screenWidth, screenHeight);
    }

    m_spriteBatch.begin(screenWidth, screenHeight);
    if (rootHasElements)
    {
        m_canvas.render(m_spriteBatch, screenWidth, screenHeight);
    }
    if (modalHasElements)
    {
        m_modalCanvas.render(m_spriteBatch, screenWidth, screenHeight);
    }

    // Subtitles last so they sit on top of modal UI. Layout is computed
    // against the text renderer's actual font pixel size so plate
    // width matches rendered glyph width byte-for-byte.
    TextRenderer* textPtr = hasSubtitles ? textPtrForBatch : nullptr;
    if (hasSubtitles && textPtr != nullptr && textPtr->isInitialized())
    {
        TextRenderer& text = *textPtr;
        SubtitleLayoutParams params;
        params.screenWidth   = screenWidth;
        params.screenHeight  = screenHeight;
        params.fontPixelSize = text.getFont().getPixelSize();
        auto lines = computeSubtitleLayout(
            m_engine->getSubtitleQueue(),
            params,
            [&text](const std::string& s) { return text.measureTextWidth(s); });
        renderSubtitles(lines, m_spriteBatch, text,
                        screenWidth, screenHeight);
    }
    else
    {
        m_spriteBatch.end();
    }

    // Pe1 — flush every queued text draw in one upload + draw.
    if (textPtrForBatch != nullptr && textPtrForBatch->isBatching())
    {
        textPtrForBatch->endBatch2D();
    }

    // Restore GL state
    if (depthWasEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blendWasEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
}

// -- Phase 10 slice 12.2: screen-stack machinery ----------------------------

void UISystem::updateMouseHit(const glm::vec2& cursor, int screenWidth, int screenHeight)
{
    // Modal canvas takes priority when present — a click that hits the
    // modal must not also register on the root canvas underneath.
    if (m_modalCanvas.getElementCount() > 0
        && m_modalCanvas.hitTest(cursor, screenWidth, screenHeight))
    {
        m_cursorOverInteractive = true;
        return;
    }
    m_cursorOverInteractive = m_canvas.hitTest(cursor, screenWidth, screenHeight);
}

void UISystem::setScreenBuilder(GameScreen screen, ScreenBuilder builder)
{
    if (builder)
    {
        m_screenBuilders[screen] = std::move(builder);
    }
    else
    {
        m_screenBuilders.erase(screen);
    }
}

UISystem::ScreenBuilder UISystem::defaultBuilderFor(GameScreen screen)
{
    switch (screen)
    {
        case GameScreen::MainMenu:
            return [](UICanvas& c, const UITheme& th, TextRenderer* tr, UISystem& ui)
            {
                buildMainMenu(c, th, tr, ui);
            };
        case GameScreen::Paused:
            return [](UICanvas& c, const UITheme& th, TextRenderer* tr, UISystem& ui)
            {
                buildPauseMenu(c, th, tr, ui);
            };
        case GameScreen::Settings:
            return [](UICanvas& c, const UITheme& th, TextRenderer* tr, UISystem& ui)
            {
                buildSettingsMenu(c, th, tr, ui);
            };
        case GameScreen::Playing:
            // Default HUD: crosshair + FPS counter (hidden) + interaction
            // prompt anchor + top-right notification stack. Game projects
            // override with `setScreenBuilder` to add health bar, inventory,
            // minimap, etc. on top of this baseline (slice 12.4).
            return [](UICanvas& c, const UITheme& th, TextRenderer* tr, UISystem& ui)
            {
                buildDefaultHud(c, th, tr, ui);
            };
        case GameScreen::None:
        case GameScreen::Loading:
        case GameScreen::Exiting:
            // No built-in prefab for these — games supply their own via
            // setScreenBuilder. Loading is a splash screen the game
            // project decorates; None/Exiting intentionally render nothing.
            return {};
    }
    return {};
}

UISystem::ScreenBuilder UISystem::resolveBuilder(GameScreen screen) const
{
    const auto it = m_screenBuilders.find(screen);
    if (it != m_screenBuilders.end() && it->second)
    {
        return it->second;
    }
    return defaultBuilderFor(screen);
}

void UISystem::setRootScreen(GameScreen screen)
{
    // Clear any modals and the root canvas before rebuilding. Modal capture
    // is dropped — the caller can re-raise a modal by pushing one.
    m_modalStack.clear();
    m_modalCanvas.clear();
    m_canvas.clear();
    m_modalCapture = false;

    m_rootScreen = screen;
    if (screen != GameScreen::None)
    {
        if (auto builder = resolveBuilder(screen))
        {
            builder(m_canvas, m_theme, m_textRenderer, *this);
        }
    }
    onRootScreenChanged.emit(screen);
}

void UISystem::pushModalScreen(GameScreen screen)
{
    // The modal-stack model is single-slot in slice 12.2 — the existing
    // canvas gets rebuilt for each push/pop. Deeper stacks arrive in a
    // later slice if a design calls for nested dialogs.
    m_modalCanvas.clear();
    m_modalStack.push_back(screen);
    if (auto builder = resolveBuilder(screen))
    {
        builder(m_modalCanvas, m_theme, m_textRenderer, *this);
    }
    m_modalCapture = true;
    onModalPushed.emit(screen);
}

void UISystem::popModalScreen()
{
    if (m_modalStack.empty())
    {
        return;
    }
    const GameScreen popped = m_modalStack.back();
    m_modalStack.pop_back();
    m_modalCanvas.clear();

    if (m_modalStack.empty())
    {
        m_modalCapture = false;
    }
    else if (auto builder = resolveBuilder(m_modalStack.back()))
    {
        builder(m_modalCanvas, m_theme, m_textRenderer, *this);
    }

    onModalPopped.emit(popped);
}

void UISystem::applyIntent(GameScreenIntent intent)
{
    const GameScreen current = getCurrentScreen();
    const GameScreen next    = applyGameScreenIntent(current, intent);
    if (next == current)
    {
        return;  // Invalid intent for this screen — pure function is total.
    }

    // Modal close: pop (do not touch the root). The pure function maps
    // Settings + CloseSettings → MainMenu, but the real "return to
    // previous" semantics lives here: the root screen under the modal is
    // preserved by construction, so popping is always correct.
    if (!m_modalStack.empty() && intent == GameScreenIntent::CloseSettings)
    {
        popModalScreen();
        return;
    }

    // Modal open: Settings stacks on top of MainMenu or Paused.
    if (intent == GameScreenIntent::OpenSettings && next == GameScreen::Settings)
    {
        pushModalScreen(GameScreen::Settings);
        return;
    }

    // Everything else is a root-screen change.
    setRootScreen(next);
}

// ---------------------------------------------------------------------------
// Phase 10.9 Slice 3 S4: keyboard navigation.
// ---------------------------------------------------------------------------

void UISystem::setFocusedElement(UIElement* el)
{
    if (m_focusedElement == el)
    {
        return;
    }
    if (m_focusedElement != nullptr)
    {
        m_focusedElement->focused = false;
    }
    m_focusedElement = el;
    if (m_focusedElement != nullptr)
    {
        m_focusedElement->focused = true;
    }
}

bool UISystem::handleKey(int key, int mods)
{
    // Tab order: modal takes precedence iff it has at least one
    // interactive element (an empty modal canvas — rare, programmatic
    // — must not black-hole keyboard navigation).
    auto order = collectTabOrderFromCanvas(m_modalCanvas);
    if (order.empty())
    {
        order = collectTabOrderFromCanvas(m_canvas);
    }

    auto advance = [&](int step)
    {
        if (order.empty())
        {
            return;
        }
        auto it = std::find(order.begin(), order.end(), m_focusedElement);
        size_t next;
        if (it == order.end())
        {
            // No existing focus: a forward step seeds at 0, a backward
            // step seeds at the end.
            next = (step >= 0) ? 0u : order.size() - 1u;
        }
        else
        {
            const auto cur = static_cast<size_t>(it - order.begin());
            if (step >= 0)
            {
                next = (cur + 1u) % order.size();
            }
            else
            {
                next = (cur == 0u) ? order.size() - 1u : cur - 1u;
            }
        }
        setFocusedElement(order[next]);
    };

    const bool shift = (mods & GLFW_MOD_SHIFT) != 0;

    switch (key)
    {
        case GLFW_KEY_TAB:
            advance(shift ? -1 : +1);
            return true;
        case GLFW_KEY_DOWN:
        case GLFW_KEY_RIGHT:
            advance(+1);
            return true;
        case GLFW_KEY_UP:
        case GLFW_KEY_LEFT:
            advance(-1);
            return true;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER:
        case GLFW_KEY_SPACE:
            if (m_focusedElement != nullptr)
            {
                m_focusedElement->onClick.emit();
                return true;
            }
            // No focused element: the key was not consumed — lets game
            // code still react to Enter / Space for non-UI bindings.
            return false;
        default:
            return false;
    }
}

} // namespace Vestige
