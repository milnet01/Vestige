// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "ui/menu_prefabs.h"
#include "localization/localization_service.h"
#include "systems/ui_system.h"
#include "ui/ui_button.h"
#include "ui/ui_crosshair.h"
#include "ui/ui_frame_hook.h"
#include "ui/ui_fps_counter.h"
#include "ui/ui_label.h"
#include "ui/ui_notification_toast.h"
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

// Wiring helper — connects a button's onClick to `uiSystem->applyIntent(...)`
// iff the caller passed a UISystem. Keeps the 3-arg path free of any UISystem
// coupling and avoids touching `b->interactive` when no system is available.
void wireIntent(UIButton* b, UISystem* uiSystem, GameScreenIntent intent)
{
    if (!uiSystem || !b)
    {
        return;
    }
    b->interactive = true;
    UISystem* ui = uiSystem;
    b->onClick.connect([ui, intent]() { ui->applyIntent(intent); });
}

void buildMainMenuImpl(UICanvas& canvas, const UITheme& theme,
                        TextRenderer* textRenderer, UISystem* uiSystem)
{
    // Background fill — full 1920×1080 base panel.
    canvas.addElement(makePanel({0, 0}, {1920, 1080}, theme.bgBase));

    // Top chrome — caption + version + hairline rule.
    canvas.addElement(makeLabel("VESTIGE  3D ENGINE",  // i18n-exempt: product name
                                  {96, 56}, 0.22f, theme.textSecondary, textRenderer));
    canvas.addElement(makeLabel(std::string("v ") + VESTIGE_ENGINE_VERSION + "  OPENGL 4.5  MIT",
                                  {1920 - 96 - 280, 56}, 0.22f, theme.textSecondary, textRenderer));
    canvas.addElement(makePanel({96, 86}, {1920 - 192, 1}, theme.rule));

    // Left column — wordmark + chapter caption.
    canvas.addElement(makeLabel(std::string(Vestige::tr("ui.menu.tagline")),
                                  {96, 220}, 0.22f, theme.textSecondary, textRenderer));
    auto wordmark = makeLabel("Vestige",  // i18n-exempt: product name
                                {96, 244}, 1.4f, theme.textPrimary, textRenderer);
    wordmark->size = {720, 168};
    canvas.addElement(std::move(wordmark));
    canvas.addElement(makeLabel(std::string(Vestige::tr("ui.menu.chapter")),
                                  {96, 420}, 0.22f, glm::vec3(theme.accent), textRenderer));

    // Menu buttons — vertical stack at left:96, top:520. Each item carries an
    // optional intent; Templates is intentionally null (per-game concern).
    // Labels resolve through tr() (Phase 10 Localization L4) — the first
    // call-site migration proving the string-table path end-to-end. tr() falls
    // back to English then to the key itself, so a menu built without a
    // registered LocalizationService (unit tests, editor preview) still renders
    // a sensible string.
    struct MenuItem
    {
        const char*      key;
        UIButtonStyle    style;
        GameScreenIntent intent;
        bool             hasIntent;
    };
    const MenuItem items[] = {
        {"ui.menu.new_walkthrough", UIButtonStyle::DEFAULT, GameScreenIntent::NewWalkthrough, true},
        {"ui.menu.continue",        UIButtonStyle::DEFAULT, GameScreenIntent::Continue,       true},
        {"ui.menu.templates",       UIButtonStyle::DEFAULT, GameScreenIntent::OpenMainMenu,   false},
        {"ui.menu.settings",        UIButtonStyle::DEFAULT, GameScreenIntent::OpenSettings,   true},
        {"ui.menu.quit",            UIButtonStyle::DANGER,  GameScreenIntent::QuitToDesktop,  true},
    };
    constexpr float btnHeight = 68.0f;
    constexpr float btnGap    = 0.0f;          // Adjacent borders share an edge.
    constexpr float btnWidth  = 520.0f;
    constexpr float btnLeftX  = 96.0f;
    float y = 520.0f;
    for (const auto& it : items)
    {
        auto b = makeButton(std::string(Vestige::tr(it.key)), {btnLeftX, y},
                             {btnWidth, btnHeight},
                             it.style, theme, textRenderer);
        if (it.hasIntent)
        {
            wireIntent(b.get(), uiSystem, it.intent);
        }
        canvas.addElement(std::move(b));
        y += btnHeight + btnGap;
    }

    // 3D_E-0744: the design's "last session" card showed made-up data
    // (scene, pillar). It returns when a save system records a real session.

    // Footer keyboard hints.
    canvas.addElement(makeLabel("(c) 2026 ANTHONY SCHEMEL  MIT",  // i18n-exempt: copyright
                                  {96, 1080 - 56}, 0.20f, theme.textSecondary, textRenderer));
    canvas.addElement(makeLabel(std::string(Vestige::tr("ui.menu.hints")),
                                  {1920 - 96 - 600, 1080 - 56}, 0.20f, theme.textSecondary, textRenderer));
}

void buildPauseMenuImpl(UICanvas& canvas, const UITheme& theme,
                         TextRenderer* textRenderer, UISystem* uiSystem)
{
    constexpr float panelW = 720.0f;
    constexpr float panelH = 760.0f;
    const float panelX = (1920.0f - panelW) * 0.5f;
    constexpr float panelY = 260.0f;

    // Scrim (tinted dark overlay).
    canvas.addElement(makePanel({0, 0}, {1920, 1080},
                                  {0.039f, 0.031f, 0.024f, 0.72f}));

    // "PAUSED" caption above the panel.
    canvas.addElement(makeLabel(std::string(Vestige::tr("ui.pause.title")),
                                  {(1920.0f - 200.0f) * 0.5f, 160.0f},
                                  0.28f, glm::vec3(theme.accent), textRenderer));
    canvas.addElement(makeLabel(std::string(Vestige::tr("ui.pause.subtitle")),
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
    canvas.addElement(makeLabel(std::string(Vestige::tr("ui.pause.headline")),
                                  {panelX + panelW * 0.5f - 220.0f, panelY + 80.0f},
                                  1.0f, theme.textPrimary, textRenderer));

    // Buttons.
    struct PauseItem
    {
        const char*      key;         // string-table key (3D_E-0024)
        UIButtonStyle    style;
        const char*      shortcut;    // nullable
        GameScreenIntent intent;
        bool             hasIntent;   // Save / Save As / Load stay inert.
    };
    const PauseItem items[] = {
        {"ui.pause.resume",         UIButtonStyle::PRIMARY, "ESC", GameScreenIntent::Resume,        true},
        {"ui.pause.save",           UIButtonStyle::DEFAULT, "F5",  GameScreenIntent::OpenMainMenu,  false},
        {"ui.pause.save_as",        UIButtonStyle::DEFAULT, nullptr, GameScreenIntent::OpenMainMenu, false},
        {"ui.pause.load",           UIButtonStyle::DEFAULT, nullptr, GameScreenIntent::OpenMainMenu, false},
        {"ui.pause.settings",       UIButtonStyle::DEFAULT, nullptr, GameScreenIntent::OpenSettings, true},
        {"ui.pause.quit_to_main",   UIButtonStyle::DEFAULT, nullptr, GameScreenIntent::QuitToMain,   true},
        {"ui.pause.quit_to_desktop", UIButtonStyle::DANGER,  nullptr, GameScreenIntent::QuitToDesktop, true},
    };
    constexpr float btnH = 52.0f;
    constexpr float btnGap = 4.0f;
    const float btnW = panelW - 112.0f;        // 56 px panel padding each side.
    float by = panelY + 220.0f;
    for (const auto& it : items)
    {
        auto b = makeButton(std::string(Vestige::tr(it.key)), {panelX + 56.0f, by}, {btnW, btnH},
                             it.style, theme, textRenderer);
        b->small = true;  // 40 px-class button-text size; fits 52 px height with padding.
        if (it.shortcut)
        {
            b->shortcut.text    = it.shortcut;
            b->shortcut.present = true;
        }
        if (it.hasIntent)
        {
            wireIntent(b.get(), uiSystem, it.intent);
        }
        canvas.addElement(std::move(b));
        by += btnH + btnGap;
    }

    // 3D_E-0744: the design's location line and autosave / slot footer showed
    // made-up data; they return when a save system supplies the real values.
}

void buildSettingsMenuImpl(UICanvas& canvas, const UITheme& theme,
                            TextRenderer* textRenderer, UISystem* uiSystem,
                            const SettingsMenuActions* actions = nullptr)
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
    canvas.addElement(makeLabel(std::string(Vestige::tr("ui.settings.caption")),
                                  {modalX + 48.0f, modalY + 28.0f},
                                  0.22f, theme.textSecondary, textRenderer));
    canvas.addElement(makeLabel(std::string(Vestige::tr("ui.settings.title")),
                                  {modalX + 48.0f, modalY + 60.0f},
                                  0.95f, theme.textPrimary, textRenderer));
    auto closeBtn = makeButton(std::string(Vestige::tr("ui.settings.close")),
                                {modalX + modalW - 200.0f, modalY + 32.0f},
                                {160.0f, 40.0f},
                                UIButtonStyle::GHOST, theme, textRenderer);
    closeBtn->small = true;
    wireIntent(closeBtn.get(), uiSystem, GameScreenIntent::CloseSettings);
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

    // String-table keys (3D_E-0024); the "01  " number prefix is not text
    // to translate and is joined in code below.
    const char* categories[] = {
        "ui.settings.category.display",
        "ui.settings.category.audio",
        "ui.settings.category.controls",
        "ui.settings.category.gameplay",
        "ui.settings.category.accessibility",
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
        const std::string categoryLabel = "0" + std::to_string(i + 1) + "  "
                                        + std::string(Vestige::tr(categories[i]));
        canvas.addElement(makeLabel(categoryLabel,
                                      {sidebarX + 48.0f, catY + 18.0f},
                                      0.34f, col, textRenderer));
    }

    // Footer — Restore Defaults / Revert / Apply buttons + dirty indicator.
    const float footerY = modalY + modalH - 70.0f;
    canvas.addElement(makePanel({modalX + 48.0f, footerY - 16.0f},
                                  {modalW - 96.0f, 1}, theme.ruleStrong));
    auto status = makeLabel(std::string(Vestige::tr("ui.settings.saved")),
                            {modalX + 48.0f, footerY + 18.0f},
                            0.22f, theme.textSecondary, textRenderer);

    auto defaultsBtn = makeButton(std::string(Vestige::tr("ui.settings.restore_defaults")),
                                    {modalX + modalW - 600.0f, footerY},
                                    {200.0f, 40.0f},
                                    UIButtonStyle::GHOST, theme, textRenderer);
    defaultsBtn->small = true;

    auto revertBtn = makeButton(std::string(Vestige::tr("ui.settings.revert")),
                                  {modalX + modalW - 380.0f, footerY},
                                  {120.0f, 40.0f},
                                  UIButtonStyle::DEFAULT, theme, textRenderer);
    revertBtn->small = true;
    revertBtn->disabled = true;

    auto applyBtn = makeButton(std::string(Vestige::tr("ui.settings.apply")),
                                 {modalX + modalW - 240.0f, footerY},
                                 {120.0f, 40.0f},
                                 UIButtonStyle::PRIMARY, theme, textRenderer);
    applyBtn->small = true;
    applyBtn->disabled = true;

    // 3D_E-0035: with actions, the footer buttons work, and Apply / Revert
    // and the status line follow isDirty() each frame.
    if (actions != nullptr)
    {
        if (actions->restoreDefaults) defaultsBtn->onClick.connect(actions->restoreDefaults);
        if (actions->revert)          revertBtn->onClick.connect(actions->revert);
        if (actions->apply)           applyBtn->onClick.connect(actions->apply);
        UIButton* revert = revertBtn.get();
        UIButton* apply  = applyBtn.get();
        UILabel*  label  = status.get();
        canvas.addElement(std::make_unique<UIFrameHook>(
            [revert, apply, label, isDirty = actions->isDirty]()
            {
                const bool dirty = isDirty && isDirty();
                revert->disabled = !dirty;
                apply->disabled  = !dirty;
                label->text = std::string(Vestige::tr(dirty ? "ui.settings.unsaved"
                                                            : "ui.settings.saved"));
            }));
    }

    canvas.addElement(std::move(status));
    canvas.addElement(std::move(defaultsBtn));
    canvas.addElement(std::move(revertBtn));
    canvas.addElement(std::move(applyBtn));
}

} // namespace (anonymous)

// -- Public 3-arg (legacy) overloads ----------------------------------------

void buildMainMenu(UICanvas& canvas, const UITheme& theme,
                    TextRenderer* textRenderer)
{
    buildMainMenuImpl(canvas, theme, textRenderer, nullptr);
}

void buildPauseMenu(UICanvas& canvas, const UITheme& theme,
                     TextRenderer* textRenderer)
{
    buildPauseMenuImpl(canvas, theme, textRenderer, nullptr);
}

void buildSettingsMenu(UICanvas& canvas, const UITheme& theme,
                        TextRenderer* textRenderer)
{
    buildSettingsMenuImpl(canvas, theme, textRenderer, nullptr);
}

// -- Public 4-arg overloads (slice 12.2) — wire signals to UISystem ---------

void buildMainMenu(UICanvas& canvas, const UITheme& theme,
                    TextRenderer* textRenderer, UISystem& uiSystem)
{
    buildMainMenuImpl(canvas, theme, textRenderer, &uiSystem);
}

void buildPauseMenu(UICanvas& canvas, const UITheme& theme,
                     TextRenderer* textRenderer, UISystem& uiSystem)
{
    buildPauseMenuImpl(canvas, theme, textRenderer, &uiSystem);
}

void buildSettingsMenu(UICanvas& canvas, const UITheme& theme,
                        TextRenderer* textRenderer, UISystem& uiSystem)
{
    buildSettingsMenuImpl(canvas, theme, textRenderer, &uiSystem);
}

void buildSettingsMenu(UICanvas& canvas, const UITheme& theme,
                        TextRenderer* textRenderer, UISystem& uiSystem,
                        const SettingsMenuActions& actions)
{
    buildSettingsMenuImpl(canvas, theme, textRenderer, &uiSystem, &actions);
}

// -- Phase 10 slice 12.4: default HUD prefab --------------------------------

void buildDefaultHud(UICanvas& canvas, const UITheme& theme,
                      TextRenderer* textRenderer, UISystem& /*uiSystem*/)
{
    // (1) Crosshair — centred, theme-coloured. UICrosshair ignores anchor
    // internally (always renders at screen centre) but we still tag the
    // anchor so the accessibility walk and hit-test see a sensible slot.
    auto crosshair = std::make_unique<UICrosshair>();
    crosshair->anchor     = Anchor::CENTER;
    crosshair->armLength  = theme.crosshairLength;
    crosshair->thickness  = theme.crosshairThickness;
    crosshair->color      = theme.crosshair;
    canvas.addElement(std::move(crosshair));

    // (2) FPS counter — hidden by default. A debug flag toggles visibility.
    // Anchored top-left with a small inset so it doesn't collide with the
    // screen edge under high-contrast.
    auto fps = std::make_unique<UIFpsCounter>();
    fps->anchor       = Anchor::TOP_LEFT;
    fps->position     = {16.0f, 16.0f};
    fps->color        = theme.textSecondary;
    fps->textRenderer = textRenderer;
    fps->visible      = false;
    canvas.addElement(std::move(fps));

    // (3) Interaction-prompt anchor — an invisible slot at the bottom-centre
    // where game code attaches its `UIInteractionPrompt` widgets (the
    // prompt itself is a `UIWorldLabel` subclass, not a fixed HUD widget,
    // so we only reserve the layout slot here). 4 body-lines of inset
    // above the bottom edge matches the design doc.
    auto promptSlot = std::make_unique<UIPanel>();
    promptSlot->anchor          = Anchor::BOTTOM_CENTER;
    promptSlot->size            = {360.0f, theme.typeBody * 2.0f};
    promptSlot->position        = {0.0f, theme.typeBody * 4.0f};
    promptSlot->backgroundColor = glm::vec4(0.0f);  // Fully transparent.
    canvas.addElement(std::move(promptSlot));

    // (4) Notification stack — three pre-created toast slots at TOP_RIGHT,
    // stacked vertically with a small gap. Each slot starts at alpha 0;
    // game code pushes notifications into the queue and reconciles them
    // into these widgets each frame. The container panel itself is an
    // invisible layout holder (drawn with alpha 0 so hit-tests still pass
    // through cleanly).
    const float toastWidth   = 320.0f;
    const float toastHeight  = 56.0f;
    const float toastGap     = 8.0f;
    const float stackHeight  = NotificationQueue::DEFAULT_CAPACITY * toastHeight
                             + (NotificationQueue::DEFAULT_CAPACITY - 1) * toastGap;

    auto stack = std::make_unique<UIPanel>();
    stack->anchor          = Anchor::TOP_RIGHT;
    stack->size            = {toastWidth, stackHeight};
    // Right-anchored panels use positive-X for "inset from right edge".
    // A 24 px inset keeps the stack clear of the screen bevel.
    stack->position        = {toastWidth + 24.0f, 24.0f};
    stack->backgroundColor = glm::vec4(0.0f);

    for (std::size_t i = 0; i < NotificationQueue::DEFAULT_CAPACITY; ++i)
    {
        auto toast = std::make_unique<UINotificationToast>();
        toast->anchor = Anchor::TOP_LEFT;
        toast->position = {0.0f, static_cast<float>(i) * (toastHeight + toastGap)};
        toast->size = {toastWidth, toastHeight};
        toast->accentWidth = theme.buttonAccentTickWidth;
        toast->backgroundColor = theme.panelBg;
        toast->alpha = 0.0f;  // Empty until the queue populates it.
        stack->addChild(std::move(toast));
    }
    canvas.addElement(std::move(stack));
}

} // namespace Vestige
