// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "ui/accessibility_settings_page.h"

#include "core/settings.h"
#include "core/settings_editor.h"
#include "ui/settings_page_layout.h"
#include "ui/ui_canvas.h"
#include "ui/ui_checkbox.h"
#include "ui/ui_dropdown.h"
#include "ui/ui_frame_hook.h"
#include "ui/ui_panel.h"
#include "ui/ui_slider.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace Vestige
{

namespace
{

using namespace SettingsPageLayout;

// Option labels, and the strings `AccessibilitySettings` stores for them, in
// the same order. The editor's Accessibility tab offers the same sets.
constexpr const char* kScaleOptions[] = {
    "ui.settings.accessibility.ui_scale.1_0x",
    "ui.settings.accessibility.ui_scale.1_25x",
    "ui.settings.accessibility.ui_scale.1_5x",
    "ui.settings.accessibility.ui_scale.2_0x",
};
constexpr std::string_view kScaleValues[] = {"1.0x", "1.25x", "1.5x", "2.0x"};

constexpr const char* kSubtitleSizeOptions[] = {
    "ui.settings.accessibility.subtitle_size.small",
    "ui.settings.accessibility.subtitle_size.medium",
    "ui.settings.accessibility.subtitle_size.large",
    "ui.settings.accessibility.subtitle_size.xl",
};
constexpr std::string_view kSubtitleSizeValues[] = {"small", "medium", "large", "xl"};

constexpr const char* kColourVisionOptions[] = {
    "ui.settings.accessibility.colour_vision.none",
    "ui.settings.accessibility.colour_vision.protanopia",
    "ui.settings.accessibility.colour_vision.deuteranopia",
    "ui.settings.accessibility.colour_vision.tritanopia",
};
constexpr std::string_view kColourVisionValues[] = {"none", "protanopia", "deuteranopia", "tritanopia"};

constexpr const char* kRowLabels[] = {
    "ui.settings.accessibility.ui_scale",
    "ui.settings.accessibility.high_contrast",
    "ui.settings.accessibility.reduced_motion",
    "ui.settings.accessibility.subtitles",
    "ui.settings.accessibility.subtitle_size",
    "ui.settings.accessibility.colour_vision",
    "ui.settings.accessibility.photosensitive",
    "ui.settings.accessibility.fog",
    "ui.settings.accessibility.fog_intensity",
    "ui.settings.accessibility.volumetric_fog",
    "ui.settings.accessibility.god_rays",
    "ui.settings.accessibility.dynamic_gi",
    "ui.settings.accessibility.freeze_gi",
};

template <std::size_t N>
std::string valueAt(const std::string_view (&values)[N], int index)
{
    return std::string(values[std::clamp(index, 0, static_cast<int>(N) - 1)]);
}

/// Connects @a box so a toggle writes its new state through @a write.
template <typename Write>
void bindCheckbox(UICheckbox* box, SettingsEditor* ed, Write write)
{
    box->onClick.connect([ed, box, write]
    {
        ed->mutate([on = box->checked, &write](Settings& s) { write(s.accessibility, on); });
    });
}

} // namespace

AccessibilitySettingsPage buildAccessibilitySettingsPage(UICanvas& canvas, const UITheme& theme,
                                                         TextRenderer* text,
                                                         SettingsEditor& editor)
{
    AccessibilitySettingsPage page;

    auto root = makePageRoot();
    page.root = root.get();
    addRowLabels(*root, kRowLabels, std::size(kRowLabels), theme, text);

    auto scale         = makeDropdown(kScaleOptions, theme, text, 0, kRowLabels[0]);
    auto highContrast  = makeCheckbox(theme, text, 1, kRowLabels[1]);
    auto reducedMotion = makeCheckbox(theme, text, 2, kRowLabels[2]);
    auto subtitles     = makeCheckbox(theme, text, 3, kRowLabels[3]);
    auto subtitleSize  = makeDropdown(kSubtitleSizeOptions, theme, text, 4, kRowLabels[4]);
    auto colourVision  = makeDropdown(kColourVisionOptions, theme, text, 5, kRowLabels[5]);
    auto photosafe     = makeCheckbox(theme, text, 6, kRowLabels[6]);
    auto fog           = makeCheckbox(theme, text, 7, kRowLabels[7]);
    auto fogIntensity  = makeSlider(theme, text, 8, kRowLabels[8], 0.0f, 1.0f, 0.05f,
                                    percentText);
    auto volumetricFog = makeCheckbox(theme, text, 9, kRowLabels[9]);
    auto godRays       = makeCheckbox(theme, text, 10, kRowLabels[10]);
    auto dynamicGi     = makeCheckbox(theme, text, 11, kRowLabels[11]);
    auto freezeGi      = makeCheckbox(theme, text, 12, kRowLabels[12]);

    page.interfaceScale     = scale.get();
    page.highContrast       = highContrast.get();
    page.reducedMotion      = reducedMotion.get();
    page.subtitles          = subtitles.get();
    page.subtitleSize       = subtitleSize.get();
    page.colourVisionFilter = colourVision.get();
    page.photosensitiveSafe = photosafe.get();
    page.fog                = fog.get();
    page.fogIntensity       = fogIntensity.get();
    page.volumetricFog      = volumetricFog.get();
    page.godRays            = godRays.get();
    page.dynamicLighting    = dynamicGi.get();
    page.freezeLighting     = freezeGi.get();

    SettingsEditor* ed = &editor;
    page.interfaceScale->onSelectionChanged.connect([ed](int i)
    {
        ed->mutate([v = valueAt(kScaleValues, i)](Settings& s) { s.accessibility.uiScalePreset = v; });
    });
    page.subtitleSize->onSelectionChanged.connect([ed](int i)
    {
        ed->mutate([v = valueAt(kSubtitleSizeValues, i)](Settings& s) { s.accessibility.subtitleSize = v; });
    });
    page.colourVisionFilter->onSelectionChanged.connect([ed](int i)
    {
        ed->mutate([v = valueAt(kColourVisionValues, i)](Settings& s) { s.accessibility.colorVisionFilter = v; });
    });
    page.fogIntensity->onValueChanged.connect([ed](float v)
    {
        ed->mutate([v](Settings& s) { s.accessibility.postProcess.fogIntensityScale = v; });
    });

    bindCheckbox(page.highContrast, ed,
                 [](AccessibilitySettings& a, bool on) { a.highContrast = on; });
    bindCheckbox(page.reducedMotion, ed,
                 [](AccessibilitySettings& a, bool on) { a.reducedMotion = on; });
    bindCheckbox(page.subtitles, ed,
                 [](AccessibilitySettings& a, bool on) { a.subtitlesEnabled = on; });
    bindCheckbox(page.photosensitiveSafe, ed,
                 [](AccessibilitySettings& a, bool on) { a.photosensitiveSafety.enabled = on; });
    bindCheckbox(page.fog, ed,
                 [](AccessibilitySettings& a, bool on) { a.postProcess.fogEnabled = on; });
    bindCheckbox(page.volumetricFog, ed,
                 [](AccessibilitySettings& a, bool on) { a.postProcess.volumetricFogEnabled = on; });
    bindCheckbox(page.godRays, ed,
                 [](AccessibilitySettings& a, bool on) { a.postProcess.godRaysEnabled = on; });
    bindCheckbox(page.dynamicLighting, ed,
                 [](AccessibilitySettings& a, bool on) { a.postProcess.dynamicGiEnabled = on; });
    bindCheckbox(page.freezeLighting, ed,
                 [](AccessibilitySettings& a, bool on) { a.postProcess.reduceMotionGi = on; });

    // Copies pending() into the controls before they draw, every frame.
    // Inside the page's container: it runs only while the page shows.
    root->addChild(std::make_unique<UIFrameHook>([ed, page]()
    {
        const AccessibilitySettings& a = ed->pending().accessibility;
        page.interfaceScale->selectedIndex     = indexOf(kScaleValues, std::string_view(a.uiScalePreset));
        page.highContrast->checked             = a.highContrast;
        page.reducedMotion->checked            = a.reducedMotion;
        page.subtitles->checked                = a.subtitlesEnabled;
        page.subtitleSize->selectedIndex       = indexOf(kSubtitleSizeValues, std::string_view(a.subtitleSize));
        page.colourVisionFilter->selectedIndex = indexOf(kColourVisionValues, std::string_view(a.colorVisionFilter));
        page.photosensitiveSafe->checked       = a.photosensitiveSafety.enabled;
        page.fog->checked                      = a.postProcess.fogEnabled;
        page.fogIntensity->value = std::clamp(a.postProcess.fogIntensityScale, 0.0f, 1.0f);
        page.volumetricFog->checked            = a.postProcess.volumetricFogEnabled;
        page.godRays->checked                  = a.postProcess.godRaysEnabled;
        page.dynamicLighting->checked          = a.postProcess.dynamicGiEnabled;
        page.freezeLighting->checked           = a.postProcess.reduceMotionGi;
    }));

    root->addChild(std::move(scale));
    root->addChild(std::move(highContrast));
    root->addChild(std::move(reducedMotion));
    root->addChild(std::move(subtitles));
    root->addChild(std::move(subtitleSize));
    root->addChild(std::move(colourVision));
    root->addChild(std::move(photosafe));
    root->addChild(std::move(fog));
    root->addChild(std::move(fogIntensity));
    root->addChild(std::move(volumetricFog));
    root->addChild(std::move(godRays));
    root->addChild(std::move(dynamicGi));
    root->addChild(std::move(freezeGi));
    canvas.addElement(std::move(root));
    return page;
}

std::vector<std::string_view> accessibilitySettingsPageKeys()
{
    std::vector<std::string_view> keys(std::begin(kRowLabels), std::end(kRowLabels));
    keys.insert(keys.end(), std::begin(kScaleOptions), std::end(kScaleOptions));
    keys.insert(keys.end(), std::begin(kSubtitleSizeOptions), std::end(kSubtitleSizeOptions));
    keys.insert(keys.end(), std::begin(kColourVisionOptions), std::end(kColourVisionOptions));
    return keys;
}

} // namespace Vestige
