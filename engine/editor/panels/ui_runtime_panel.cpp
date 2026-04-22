// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_runtime_panel.cpp
/// @brief `UIRuntimePanel` — editor panel for the in-game UI runtime.

#include "editor/panels/ui_runtime_panel.h"

#include "systems/ui_system.h"
#include "ui/menu_prefabs.h"
#include "ui/ui_element.h"

#include <imgui.h>

#include <algorithm>

namespace Vestige
{

namespace
{
constexpr GameScreenIntent kAllIntents[] = {
    GameScreenIntent::OpenMainMenu,
    GameScreenIntent::NewWalkthrough,
    GameScreenIntent::Continue,
    GameScreenIntent::OpenSettings,
    GameScreenIntent::CloseSettings,
    GameScreenIntent::Pause,
    GameScreenIntent::Resume,
    GameScreenIntent::QuitToMain,
    GameScreenIntent::QuitToDesktop,
    GameScreenIntent::LoadingComplete,
};
} // namespace

UIRuntimePanel::UIRuntimePanel()
{
    m_screenLog.reserve(SCREEN_LOG_CAPACITY);
}

// -- Screen-push log --------------------------------------------------------

void UIRuntimePanel::recordScreenTransition(GameScreen from, GameScreen to,
                                            GameScreenIntent intent)
{
    if (m_screenLog.size() >= SCREEN_LOG_CAPACITY)
    {
        m_screenLog.erase(m_screenLog.begin());
    }
    ScreenLogEntry entry;
    entry.from   = from;
    entry.to     = to;
    entry.intent = intent;
    m_screenLog.push_back(entry);
}

// -- Menu preview -----------------------------------------------------------

void UIRuntimePanel::setMenuPreview(UIRuntimePanelMenu menu)
{
    m_menuPreview = menu;
    // A caller that cares about the rebuilt canvas also has the theme
    // and text renderer handy — they should call `refreshMenuPreview`
    // explicitly. Tests rely on this split to exercise state changes
    // without materialising a theme.
}

void UIRuntimePanel::refreshMenuPreview(const UITheme& theme,
                                        TextRenderer* textRenderer)
{
    m_previewCanvas.clear();
    switch (m_menuPreview)
    {
        case UIRuntimePanelMenu::MainMenu:
            buildMainMenu(m_previewCanvas, theme, textRenderer);
            break;
        case UIRuntimePanelMenu::Paused:
            buildPauseMenu(m_previewCanvas, theme, textRenderer);
            break;
        case UIRuntimePanelMenu::Settings:
            buildSettingsMenu(m_previewCanvas, theme, textRenderer);
            break;
    }
}

// -- HUD element toggles ----------------------------------------------------

bool UIRuntimePanel::isHudElementVisible(HudElement element) const
{
    const auto idx = static_cast<std::size_t>(element);
    if (idx >= m_hudVisible.size())
    {
        return false;
    }
    return m_hudVisible[idx];
}

void UIRuntimePanel::setHudElementVisible(HudElement element, bool visible)
{
    const auto idx = static_cast<std::size_t>(element);
    if (idx >= m_hudVisible.size())
    {
        return;
    }
    m_hudVisible[idx] = visible;
}

std::size_t UIRuntimePanel::applyHudTogglesTo(UISystem& uiSystem) const
{
    // The HUD prefab only populates the root canvas when the active
    // screen is `Playing`; skip otherwise so we don't write to an
    // unrelated prefab's element tree.
    if (uiSystem.getRootScreen() != GameScreen::Playing)
    {
        return 0;
    }

    UICanvas& canvas = uiSystem.getCanvas();
    const std::size_t count = std::min<std::size_t>(canvas.getElementCount(),
                                                    HUD_ELEMENT_COUNT);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (UIElement* el = canvas.getElementAt(i))
        {
            el->visible = m_hudVisible[i];
        }
    }
    return count;
}

// -- ImGui draw path --------------------------------------------------------

void UIRuntimePanel::draw(UISystem* uiSystem)
{
    if (!m_open)
    {
        return;
    }

    if (!ImGui::Begin("UI Runtime", &m_open))
    {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("UIRuntimeTabs"))
    {
        if (ImGui::BeginTabItem("State"))
        {
            drawStateTab(uiSystem);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Menus"))
        {
            drawMenusTab(uiSystem);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("HUD"))
        {
            drawHudTab(uiSystem);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Accessibility"))
        {
            drawAccessibilityTab(uiSystem);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void UIRuntimePanel::drawStateTab(UISystem* uiSystem)
{
    if (!uiSystem)
    {
        ImGui::TextDisabled("UISystem not available.");
        return;
    }

    ImGui::TextUnformatted("Root screen:");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.78f, 0.60f, 0.24f, 1.0f),
                       "%s", gameScreenLabel(uiSystem->getRootScreen()));

    ImGui::TextUnformatted("Top modal:");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.84f, 0.42f, 0.31f, 1.0f),
                       "%s", gameScreenLabel(uiSystem->getTopModalScreen()));

    ImGui::Separator();
    ImGui::TextUnformatted("Fire intent:");
    int row = 0;
    for (GameScreenIntent intent : kAllIntents)
    {
        if (row++ % 3 != 0) ImGui::SameLine();
        if (ImGui::Button(gameScreenIntentLabel(intent)))
        {
            const GameScreen before = uiSystem->getCurrentScreen();
            uiSystem->applyIntent(intent);
            const GameScreen after  = uiSystem->getCurrentScreen();
            if (before != after)
            {
                recordScreenTransition(before, after, intent);
            }
        }
    }

    ImGui::Separator();
    ImGui::Text("Screen log (%zu)", m_screenLog.size());
    if (ImGui::Button("Clear##screen_log"))
    {
        clearScreenLog();
    }
    ImGui::BeginChild("screen_log_scroll", ImVec2(0, 160), true);
    for (const ScreenLogEntry& entry : m_screenLog)
    {
        ImGui::Text("%s --[%s]-> %s",
                    gameScreenLabel(entry.from),
                    gameScreenIntentLabel(entry.intent),
                    gameScreenLabel(entry.to));
    }
    ImGui::EndChild();
}

void UIRuntimePanel::drawMenusTab(UISystem* uiSystem)
{
    if (!uiSystem)
    {
        ImGui::TextDisabled("UISystem not available.");
        return;
    }

    int selection = static_cast<int>(m_menuPreview);
    const char* kItems[] = {"Main Menu", "Paused", "Settings"};
    if (ImGui::Combo("Preview", &selection, kItems, IM_ARRAYSIZE(kItems)))
    {
        setMenuPreview(static_cast<UIRuntimePanelMenu>(selection));
        refreshMenuPreview(uiSystem->getTheme(), /*textRenderer=*/nullptr);
    }
    ImGui::SameLine();
    if (ImGui::Button("Rebuild"))
    {
        refreshMenuPreview(uiSystem->getTheme(), /*textRenderer=*/nullptr);
    }

    ImGui::Text("Preview canvas elements: %zu",
                previewCanvas().getElementCount());
    // Offscreen composite: signed-off proposal is to render the preview
    // into a dedicated framebuffer and display it via ImGui::Image.
    // That wiring needs editor-viewport cooperation and lands in a
    // follow-up — for now the tab surfaces the structural readout so
    // prefab changes are visible without launching a game build.
    ImGui::TextDisabled("Offscreen preview composite: pending FBO wiring.");
}

void UIRuntimePanel::drawHudTab(UISystem* uiSystem)
{
    if (!uiSystem)
    {
        ImGui::TextDisabled("UISystem not available.");
        return;
    }

    static const char* const kLabels[HUD_ELEMENT_COUNT] = {
        "Crosshair",
        "FPS counter",
        "Interaction anchor",
        "Notification stack",
    };

    bool anyChanged = false;
    for (std::size_t i = 0; i < HUD_ELEMENT_COUNT; ++i)
    {
        bool on = m_hudVisible[i];
        if (ImGui::Checkbox(kLabels[i], &on))
        {
            m_hudVisible[i] = on;
            anyChanged = true;
        }
    }

    if (anyChanged)
    {
        applyHudTogglesTo(*uiSystem);
    }

    ImGui::TextDisabled("Toggles apply to the live HUD canvas when the "
                        "root screen is Playing.");
}

void UIRuntimePanel::drawAccessibilityTab(UISystem* uiSystem)
{
    if (!uiSystem)
    {
        ImGui::TextDisabled("UISystem not available.");
        return;
    }

    // Scale preset
    const char* kScaleLabels[] = {"1.0x", "1.25x", "1.5x", "2.0x"};
    int scaleIndex = static_cast<int>(uiSystem->getScalePreset());
    if (ImGui::Combo("UI scale", &scaleIndex, kScaleLabels, IM_ARRAYSIZE(kScaleLabels)))
    {
        uiSystem->setScalePreset(static_cast<UIScalePreset>(scaleIndex));
    }

    // High contrast
    bool highContrast = uiSystem->isHighContrastMode();
    if (ImGui::Checkbox("High contrast", &highContrast))
    {
        uiSystem->setHighContrastMode(highContrast);
    }

    // Reduced motion
    bool reducedMotion = uiSystem->isReducedMotion();
    if (ImGui::Checkbox("Reduced motion", &reducedMotion))
    {
        uiSystem->setReducedMotion(reducedMotion);
    }

    ImGui::Separator();
    ImGui::TextDisabled("Composes with every menu prefab and the HUD "
                        "baseline — changes are live.");
}

} // namespace Vestige
