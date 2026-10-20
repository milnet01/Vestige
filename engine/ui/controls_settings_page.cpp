// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "ui/controls_settings_page.h"

#include "core/settings.h"
#include "core/settings_editor.h"
#include "ui/settings_page_layout.h"
#include "ui/ui_canvas.h"
#include "ui/ui_checkbox.h"
#include "ui/ui_frame_hook.h"
#include "ui/ui_panel.h"
#include "ui/ui_slider.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>

namespace Vestige
{

namespace
{

using namespace SettingsPageLayout;

constexpr const char* kRowLabels[] = {
    "ui.settings.controls.mouse_sensitivity",
    "ui.settings.controls.invert_mouse",
    "ui.settings.controls.left_dead_zone",
    "ui.settings.controls.right_dead_zone",
};

// The editor's Controls tab uses the same ranges.
constexpr float kMinSensitivity = 0.1f;
constexpr float kMaxSensitivity = 10.0f;
constexpr float kMaxDeadZone    = 0.9f;

std::string twoDecimals(float v)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(v));
    return buffer;
}

} // namespace

ControlsSettingsPage buildControlsSettingsPage(UICanvas& canvas, const UITheme& theme,
                                               TextRenderer* text, SettingsEditor& editor)
{
    ControlsSettingsPage page;

    auto root = makePageRoot();
    page.root = root.get();
    addRowLabels(*root, kRowLabels, std::size(kRowLabels), theme, text);

    auto sensitivity = makeSlider(theme, text, 0, kRowLabels[0], kMinSensitivity,
                                  kMaxSensitivity, 0.1f, twoDecimals);
    auto invert      = makeCheckbox(theme, text, 1, kRowLabels[1]);
    auto leftZone    = makeSlider(theme, text, 2, kRowLabels[2], 0.0f, kMaxDeadZone,
                                  0.05f, percentText);
    auto rightZone   = makeSlider(theme, text, 3, kRowLabels[3], 0.0f, kMaxDeadZone,
                                  0.05f, percentText);
    page.mouseSensitivity   = sensitivity.get();
    page.invertMouse        = invert.get();
    page.leftStickDeadZone  = leftZone.get();
    page.rightStickDeadZone = rightZone.get();

    SettingsEditor* ed = &editor;
    page.mouseSensitivity->onValueChanged.connect([ed](float v)
    {
        ed->mutate([v](Settings& s) { s.controls.mouseSensitivity = v; });
    });
    UICheckbox* invertBox = page.invertMouse;
    invertBox->onClick.connect([ed, invertBox]
    {
        ed->mutate([on = invertBox->checked](Settings& s) { s.controls.invertY = on; });
    });
    page.leftStickDeadZone->onValueChanged.connect([ed](float v)
    {
        ed->mutate([v](Settings& s) { s.controls.gamepadDeadzoneLeft = v; });
    });
    page.rightStickDeadZone->onValueChanged.connect([ed](float v)
    {
        ed->mutate([v](Settings& s) { s.controls.gamepadDeadzoneRight = v; });
    });

    // Copies pending() into the controls before they draw, every frame.
    // Inside the page's container: it runs only while the page shows.
    root->addChild(std::make_unique<UIFrameHook>([ed, page]()
    {
        const ControlsSettings& c = ed->pending().controls;
        page.mouseSensitivity->value =
            std::clamp(c.mouseSensitivity, kMinSensitivity, kMaxSensitivity);
        page.invertMouse->checked = c.invertY;
        page.leftStickDeadZone->value  = std::clamp(c.gamepadDeadzoneLeft, 0.0f, kMaxDeadZone);
        page.rightStickDeadZone->value = std::clamp(c.gamepadDeadzoneRight, 0.0f, kMaxDeadZone);
    }));

    root->addChild(std::move(sensitivity));
    root->addChild(std::move(invert));
    root->addChild(std::move(leftZone));
    root->addChild(std::move(rightZone));
    canvas.addElement(std::move(root));
    return page;
}

std::vector<std::string_view> controlsSettingsPageKeys()
{
    return {std::begin(kRowLabels), std::end(kRowLabels)};
}

} // namespace Vestige
