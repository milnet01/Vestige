// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "ui/audio_settings_page.h"

#include "core/settings.h"
#include "core/settings_editor.h"
#include "ui/settings_page_layout.h"
#include "ui/ui_canvas.h"
#include "ui/ui_checkbox.h"
#include "ui/ui_frame_hook.h"
#include "ui/ui_panel.h"
#include "ui/ui_slider.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace Vestige
{

namespace
{

using namespace SettingsPageLayout;

// Rows in `AudioSettings::busGains` order, then the headphone checkbox.
constexpr const char* kRowLabels[] = {
    "ui.settings.audio.master",
    "ui.settings.audio.music",
    "ui.settings.audio.voice",
    "ui.settings.audio.effects",
    "ui.settings.audio.ambient",
    "ui.settings.audio.interface",
    "ui.settings.audio.headphone_3d",
};
constexpr int kHeadphoneRow = 6;

} // namespace

AudioSettingsPage buildAudioSettingsPage(UICanvas& canvas, const UITheme& theme,
                                         TextRenderer* text, SettingsEditor& editor)
{
    AudioSettingsPage page;

    auto root = makePageRoot();
    page.root = root.get();
    addRowLabels(*root, kRowLabels, std::size(kRowLabels), theme, text);

    std::array<std::unique_ptr<UISlider>, 6> sliders;
    SettingsEditor* ed = &editor;
    for (std::size_t bus = 0; bus < sliders.size(); ++bus)
    {
        sliders[bus] = makeSlider(theme, text, static_cast<int>(bus), kRowLabels[bus],
                                  0.0f, 1.0f, 0.05f, percentText);
        page.volumes[bus] = sliders[bus].get();
        sliders[bus]->onValueChanged.connect([ed, bus](float v)
        {
            ed->mutate([bus, v](Settings& s) { s.audio.busGains[bus] = v; });
        });
    }

    auto headphone = makeCheckbox(theme, text, kHeadphoneRow, kRowLabels[kHeadphoneRow]);
    page.headphone3d = headphone.get();
    UICheckbox* headphoneBox = page.headphone3d;
    headphoneBox->onClick.connect([ed, headphoneBox]
    {
        ed->mutate([on = headphoneBox->checked](Settings& s) { s.audio.hrtfEnabled = on; });
    });

    // Copies pending() into the controls before they draw, every frame.
    // Inside the page's container: it runs only while the page shows.
    root->addChild(std::make_unique<UIFrameHook>([ed, page]()
    {
        const AudioSettings& a = ed->pending().audio;
        for (std::size_t bus = 0; bus < page.volumes.size(); ++bus)
        {
            page.volumes[bus]->value = std::clamp(a.busGains[bus], 0.0f, 1.0f);
        }
        page.headphone3d->checked = a.hrtfEnabled;
    }));

    for (auto& slider : sliders)
    {
        root->addChild(std::move(slider));
    }
    root->addChild(std::move(headphone));
    canvas.addElement(std::move(root));
    return page;
}

std::vector<std::string_view> audioSettingsPageKeys()
{
    return {std::begin(kRowLabels), std::end(kRowLabels)};
}

} // namespace Vestige
