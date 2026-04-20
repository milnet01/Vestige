// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "ui/menu_prefabs.h"
#include "ui/ui_button.h"
#include "ui/ui_label.h"
#include "ui/ui_panel.h"

#include <memory>

namespace Vestige
{

namespace
{

// Convenience builders that DRY up the boilerplate of new+configure+addChild.
std::unique_ptr<UIPanel> makePanel(const glm::vec2& pos, const glm::vec2& sz,
                                    const glm::vec4& bg)
{
    auto p = std::make_unique<UIPanel>();
    p->position = pos;
    p->size     = sz;
    p->backgroundColor = bg;
    p->anchor   = Anchor::TOP_LEFT;
    return p;
}

std::unique_ptr<UILabel> makeLabel(const std::string& text,
                                    const glm::vec2& pos, float scale,
                                    const glm::vec3& color,
                                    TextRenderer* textRenderer)
{
    auto l = std::make_unique<UILabel>();
    l->text = text;
    l->position = pos;
    l->scale = scale;
    l->color = color;
    l->anchor = Anchor::TOP_LEFT;
    l->textRenderer = textRenderer;
    return l;
}

std::unique_ptr<UIButton> makeButton(const std::string& label,
                                      const glm::vec2& pos,
                                      const glm::vec2& sz,
                                      UIButtonStyle style,
                                      const UITheme& theme,
                                      TextRenderer* textRenderer)
{
    auto b = std::make_unique<UIButton>();
    b->label    = label;
    b->position = pos;
    b->size     = sz;
    b->style    = style;
    b->anchor   = Anchor::TOP_LEFT;
    b->theme    = &theme;
    b->textRenderer = textRenderer;
    return b;
}

} // namespace

void buildMainMenu(UICanvas& canvas, const UITheme& theme,
                    TextRenderer* textRenderer)
{
    // Background fill — full 1920×1080 base panel.
    canvas.addElement(makePanel({0, 0}, {1920, 1080}, theme.bgBase));

    // Top chrome — caption + version + hairline rule.
    canvas.addElement(makeLabel("VESTIGE  3D ENGINE",
                                  {96, 56}, 0.22f, theme.textSecondary, textRenderer));
    canvas.addElement(makeLabel("v 0.6.2  OPENGL 4.5  MIT",
                                  {1920 - 96 - 280, 56}, 0.22f, theme.textSecondary, textRenderer));
    canvas.addElement(makePanel({96, 86}, {1920 - 192, 1}, theme.rule));

    // Left column — wordmark + chapter caption.
    canvas.addElement(makeLabel("EXPLORATION ENGINE",
                                  {96, 220}, 0.22f, theme.textSecondary, textRenderer));
    auto wordmark = makeLabel("Vestige",
                                {96, 244}, 1.4f, theme.textPrimary, textRenderer);
    wordmark->size = {720, 168};
    canvas.addElement(std::move(wordmark));
    canvas.addElement(makeLabel("CHAPTER I  THE TABERNACLE",
                                  {96, 420}, 0.22f, glm::vec3(theme.accent), textRenderer));

    // Menu buttons — vertical stack at left:96, top:520.
    struct MenuItem
    {
        const char*    label;
        UIButtonStyle  style;
    };
    const MenuItem items[] = {
        {"New Walkthrough", UIButtonStyle::DEFAULT},
        {"Continue",        UIButtonStyle::DEFAULT},
        {"Templates",       UIButtonStyle::DEFAULT},
        {"Settings",        UIButtonStyle::DEFAULT},
        {"Quit",            UIButtonStyle::DANGER},
    };
    constexpr float btnHeight = 68.0f;
    constexpr float btnGap    = 0.0f;          // Adjacent borders share an edge.
    constexpr float btnWidth  = 520.0f;
    constexpr float btnLeftX  = 96.0f;
    float y = 520.0f;
    for (const auto& it : items)
    {
        auto b = makeButton(it.label, {btnLeftX, y}, {btnWidth, btnHeight},
                             it.style, theme, textRenderer);
        canvas.addElement(std::move(b));
        y += btnHeight + btnGap;
    }

    // Right column — continue card placeholder (320 × 220 panel).
    canvas.addElement(makePanel({1920 - 96 - 540, 220}, {540, 240}, theme.panelBg));
    canvas.addElement(makeLabel("LAST SESSION",
                                  {1920 - 96 - 512, 248}, 0.22f, theme.textSecondary, textRenderer));
    canvas.addElement(makeLabel("The Tabernacle",
                                  {1920 - 96 - 512, 296}, 0.6f, theme.textPrimary, textRenderer));
    canvas.addElement(makeLabel("Outer Court  Pillar 07 of 20",
                                  {1920 - 96 - 512, 348}, 0.24f, theme.textSecondary, textRenderer));

    // Footer keyboard hints.
    canvas.addElement(makeLabel("(c) 2026 ANTHONY SCHEMEL  BUILD 0.6.2-a14f  MIT",
                                  {96, 1080 - 56}, 0.20f, theme.textSecondary, textRenderer));
    canvas.addElement(makeLabel("UP DOWN NAVIGATE     ENTER SELECT     ESC QUIT",
                                  {1920 - 96 - 600, 1080 - 56}, 0.20f, theme.textSecondary, textRenderer));
}

void buildPauseMenu(UICanvas& canvas, const UITheme& theme,
                     TextRenderer* textRenderer)
{
    constexpr float panelW = 720.0f;
    constexpr float panelH = 760.0f;
    const float panelX = (1920.0f - panelW) * 0.5f;
    constexpr float panelY = 260.0f;

    // Scrim (tinted dark overlay).
    canvas.addElement(makePanel({0, 0}, {1920, 1080},
                                  {0.039f, 0.031f, 0.024f, 0.72f}));

    // "PAUSED" caption above the panel.
    canvas.addElement(makeLabel("PAUSED",
                                  {(1920.0f - 200.0f) * 0.5f, 160.0f},
                                  0.28f, glm::vec3(theme.accent), textRenderer));
    canvas.addElement(makeLabel("INPUT SUSPENDED  WORLD TIME 14:22:08",
                                  {(1920.0f - 480.0f) * 0.5f, 200.0f},
                                  0.22f, theme.textSecondary, textRenderer));

    // Modal panel.
    canvas.addElement(makePanel({panelX, panelY}, {panelW, panelH}, theme.panelBg));

    // Corner brackets — 18×18 angles in accent at each corner.
    constexpr float bracketLen = 18.0f;
    constexpr float bracketThick = 2.0f;
    auto cornerStripeH = [&](float x, float y) {
        canvas.addElement(makePanel({x, y}, {bracketLen, bracketThick}, theme.accent));
    };
    auto cornerStripeV = [&](float x, float y) {
        canvas.addElement(makePanel({x, y}, {bracketThick, bracketLen}, theme.accent));
    };
    // Top-left
    cornerStripeH(panelX, panelY);
    cornerStripeV(panelX, panelY);
    // Top-right
    cornerStripeH(panelX + panelW - bracketLen, panelY);
    cornerStripeV(panelX + panelW - bracketThick, panelY);
    // Bottom-left
    cornerStripeH(panelX, panelY + panelH - bracketThick);
    cornerStripeV(panelX, panelY + panelH - bracketLen);
    // Bottom-right
    cornerStripeH(panelX + panelW - bracketLen, panelY + panelH - bracketThick);
    cornerStripeV(panelX + panelW - bracketThick, panelY + panelH - bracketLen);

    // Headline + caption inside panel.
    canvas.addElement(makeLabel("The walk is held.",
                                  {panelX + panelW * 0.5f - 220.0f, panelY + 80.0f},
                                  1.0f, theme.textPrimary, textRenderer));
    canvas.addElement(makeLabel("TABERNACLE  OUTER COURT  PILLAR 07",
                                  {panelX + panelW * 0.5f - 220.0f, panelY + 140.0f},
                                  0.22f, theme.textSecondary, textRenderer));

    // Buttons.
    struct PauseItem
    {
        const char*    label;
        UIButtonStyle  style;
        const char*    shortcut;  // nullable
    };
    const PauseItem items[] = {
        {"Resume",            UIButtonStyle::PRIMARY, "ESC"},
        {"Save",              UIButtonStyle::DEFAULT, "F5"},
        {"Save As...",        UIButtonStyle::DEFAULT, nullptr},
        {"Load",              UIButtonStyle::DEFAULT, nullptr},
        {"Settings",          UIButtonStyle::DEFAULT, nullptr},
        {"Quit to Main Menu", UIButtonStyle::DEFAULT, nullptr},
        {"Quit to Desktop",   UIButtonStyle::DANGER,  nullptr},
    };
    constexpr float btnH = 52.0f;
    constexpr float btnGap = 4.0f;
    const float btnW = panelW - 112.0f;        // 56 px panel padding each side.
    float by = panelY + 220.0f;
    for (const auto& it : items)
    {
        auto b = makeButton(it.label, {panelX + 56.0f, by}, {btnW, btnH},
                             it.style, theme, textRenderer);
        b->small = true;  // 40 px-class button-text size; fits 52 px height with padding.
        if (it.shortcut)
        {
            b->shortcut.text    = it.shortcut;
            b->shortcut.present = true;
        }
        canvas.addElement(std::move(b));
        by += btnH + btnGap;
    }

    // Footer line in panel.
    canvas.addElement(makeLabel("AUTOSAVE  14:19:42",
                                  {panelX + 56.0f, panelY + panelH - 36.0f},
                                  0.20f, theme.textSecondary, textRenderer));
    canvas.addElement(makeLabel("SLOT 03",
                                  {panelX + panelW - 56.0f - 80.0f, panelY + panelH - 36.0f},
                                  0.20f, theme.textSecondary, textRenderer));
}

void buildSettingsMenu(UICanvas& canvas, const UITheme& theme,
                        TextRenderer* textRenderer)
{
    // Darkened backdrop.
    canvas.addElement(makePanel({0, 0}, {1920, 1080},
                                  {0.039f, 0.031f, 0.024f, 0.55f}));

    // Modal panel — inset 120 left/right, 80 top/bottom.
    constexpr float modalX = 120.0f;
    constexpr float modalY = 80.0f;
    constexpr float modalW = 1920.0f - 240.0f;
    constexpr float modalH = 1080.0f - 160.0f;
    canvas.addElement(makePanel({modalX, modalY}, {modalW, modalH}, theme.panelBg));

    // Header — "Settings" title + ESC close button.
    canvas.addElement(makeLabel("VESTIGE  CONFIGURATION",
                                  {modalX + 48.0f, modalY + 28.0f},
                                  0.22f, theme.textSecondary, textRenderer));
    canvas.addElement(makeLabel("Settings",
                                  {modalX + 48.0f, modalY + 60.0f},
                                  0.95f, theme.textPrimary, textRenderer));
    auto closeBtn = makeButton("ESC  CLOSE",
                                {modalX + modalW - 200.0f, modalY + 32.0f},
                                {160.0f, 40.0f},
                                UIButtonStyle::GHOST, theme, textRenderer);
    closeBtn->small = true;
    canvas.addElement(std::move(closeBtn));

    // Header bottom rule.
    canvas.addElement(makePanel({modalX + 48.0f, modalY + 120.0f},
                                  {modalW - 96.0f, 1}, theme.ruleStrong));

    // Sidebar — 5 categories, each 60 px tall.
    constexpr float sidebarX = modalX;
    constexpr float sidebarW = 300.0f;
    const float sidebarY = modalY + 130.0f;
    canvas.addElement(makePanel({sidebarX + sidebarW, sidebarY},
                                  {1, modalH - 160.0f}, theme.ruleStrong));

    const char* categories[] = {
        "01  Display",
        "02  Audio",
        "03  Controls",
        "04  Gameplay",
        "05  Accessibility",
    };
    for (size_t i = 0; i < std::size(categories); ++i)
    {
        const float catY = sidebarY + 16.0f + static_cast<float>(i) * 56.0f;
        // Active-highlight strip on the first item (Display) — accent vertical bar.
        if (i == 0)
        {
            canvas.addElement(makePanel({sidebarX, catY},
                                          {2.0f, 56.0f}, theme.accent));
            canvas.addElement(makePanel({sidebarX + 2.0f, catY},
                                          {sidebarW - 2.0f, 56.0f}, theme.panelBgHover));
        }
        const glm::vec3 col = (i == 0)
            ? glm::vec3(theme.accent)
            : theme.textPrimary;
        canvas.addElement(makeLabel(categories[i],
                                      {sidebarX + 48.0f, catY + 18.0f},
                                      0.34f, col, textRenderer));
    }

    // Footer — Restore Defaults / Revert / Apply buttons + dirty indicator.
    const float footerY = modalY + modalH - 70.0f;
    canvas.addElement(makePanel({modalX + 48.0f, footerY - 16.0f},
                                  {modalW - 96.0f, 1}, theme.ruleStrong));
    canvas.addElement(makeLabel("ALL CHANGES SAVED",
                                  {modalX + 48.0f, footerY + 18.0f},
                                  0.22f, theme.textSecondary, textRenderer));

    auto defaultsBtn = makeButton("RESTORE DEFAULTS",
                                    {modalX + modalW - 600.0f, footerY},
                                    {200.0f, 40.0f},
                                    UIButtonStyle::GHOST, theme, textRenderer);
    defaultsBtn->small = true;
    canvas.addElement(std::move(defaultsBtn));

    auto revertBtn = makeButton("REVERT",
                                  {modalX + modalW - 380.0f, footerY},
                                  {120.0f, 40.0f},
                                  UIButtonStyle::DEFAULT, theme, textRenderer);
    revertBtn->small = true;
    revertBtn->disabled = true;
    canvas.addElement(std::move(revertBtn));

    auto applyBtn = makeButton("APPLY",
                                 {modalX + modalW - 240.0f, footerY},
                                 {120.0f, 40.0f},
                                 UIButtonStyle::PRIMARY, theme, textRenderer);
    applyBtn->small = true;
    applyBtn->disabled = true;
    canvas.addElement(std::move(applyBtn));
}

} // namespace Vestige
