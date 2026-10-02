// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "ui/graphics_settings_page.h"

#include "core/settings.h"
#include "core/settings_editor.h"
#include "localization/localization_service.h"
#include "systems/ui_system.h"
#include "ui/menu_prefabs.h"
#include "ui/ui_canvas.h"
#include "ui/ui_checkbox.h"
#include "ui/ui_dropdown.h"
#include "ui/ui_frame_hook.h"
#include "ui/ui_label.h"
#include "ui/ui_slider.h"
#include "ui/ui_theme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace Vestige
{

namespace
{

// Content area of the Settings chrome (menu_prefabs.cpp): right of the
// sidebar, above the footer rule.
constexpr float kLabelX   = 468.0f;
constexpr float kControlX = 1000.0f;
constexpr float kFirstRow = 236.0f;
constexpr float kRowStep  = 52.0f;

constexpr const char* kPresetOptions[] = {
    "ui.settings.graphics.preset.low",
    "ui.settings.graphics.preset.medium",
    "ui.settings.graphics.preset.high",
    "ui.settings.graphics.preset.ultra",
    "ui.settings.graphics.preset.custom",
};
constexpr QualityPreset kPresets[] = {
    QualityPreset::Low, QualityPreset::Medium, QualityPreset::High,
    QualityPreset::Ultra, QualityPreset::Custom,
};

constexpr const char* kAntiAliasOptions[] = {
    "ui.settings.graphics.aa.off",
    "ui.settings.graphics.aa.fxaa",
    "ui.settings.graphics.aa.smaa",
    "ui.settings.graphics.aa.taa",
    "ui.settings.graphics.aa.msaa4x",
};
constexpr AntiAliasMode kAntiAliasModes[] = {
    AntiAliasMode::NONE, AntiAliasMode::FXAA, AntiAliasMode::SMAA,
    AntiAliasMode::TAA, AntiAliasMode::MSAA_4X,
};

constexpr const char* kDetailOptions[] = {
    "ui.settings.graphics.detail.low",
    "ui.settings.graphics.detail.medium",
    "ui.settings.graphics.detail.high",
};

constexpr const char* kWindowOptions[] = {
    "ui.settings.graphics.window.windowed",
    "ui.settings.graphics.window.fullscreen",
};

constexpr const char* kRowLabels[] = {
    "ui.settings.graphics.preset",
    "ui.settings.graphics.render_scale",
    "ui.settings.graphics.anti_aliasing",
    "ui.settings.graphics.ambient_occlusion",
    "ui.settings.graphics.bloom",
    "ui.settings.graphics.volumetrics",
    "ui.settings.graphics.terrain_detail",
    "ui.settings.graphics.foliage_detail",
    "ui.settings.graphics.grass_detail",
    "ui.settings.graphics.tree_detail",
    "ui.settings.graphics.vsync",
    "ui.settings.graphics.window_mode",
};

constexpr const char* kFooterKeys[] = {
    "ui.settings.saved",
    "ui.settings.unsaved",
    "ui.settings.apply",
    "ui.settings.revert",
    "ui.settings.restore_defaults",
};

template <typename T, std::size_t N>
int indexOf(const T (&values)[N], T value)
{
    for (std::size_t i = 0; i < N; ++i)
    {
        if (values[i] == value)
        {
            return static_cast<int>(i);
        }
    }
    return 0;
}

float rowY(int row)
{
    return kFirstRow + kRowStep * static_cast<float>(row);
}

template <std::size_t N>
std::unique_ptr<UIDropdown> makeDropdown(const char* const (&keys)[N], const UITheme& theme,
                                         TextRenderer* text, int row, const char* labelKey)
{
    auto d = std::make_unique<UIDropdown>();
    for (const char* key : keys)
    {
        d->options.push_back({key, std::string(tr(key))});
    }
    d->position = {kControlX, rowY(row)};
    d->size = {320.0f, 40.0f};
    d->theme = &theme;
    d->textRenderer = text;
    d->accessible().label = std::string(tr(labelKey));
    return d;
}

std::unique_ptr<UICheckbox> makeCheckbox(const UITheme& theme, TextRenderer* text,
                                         int row, const char* labelKey)
{
    auto c = std::make_unique<UICheckbox>();
    c->position = {kControlX, rowY(row) + 10.0f};
    c->size = {24.0f, 20.0f};
    c->theme = &theme;
    c->textRenderer = text;
    c->accessible().label = std::string(tr(labelKey));
    return c;
}

/// Writes one option and marks the preset Custom (spec §4.2 *Edit*).
template <typename Write>
void setOption(SettingsEditor& editor, Write write)
{
    editor.mutate([&write](Settings& s)
    {
        write(s.display);
        s.display.qualityPreset = QualityPreset::Custom;
    });
}

} // namespace

GraphicsSettingsPage buildGraphicsSettingsPage(UICanvas& canvas, const UITheme& theme,
                                               TextRenderer* text, SettingsEditor& editor)
{
    GraphicsSettingsPage page;

    for (std::size_t row = 0; row < std::size(kRowLabels); ++row)
    {
        auto label = std::make_unique<UILabel>();
        label->text = std::string(tr(kRowLabels[row]));
        label->position = {kLabelX, rowY(static_cast<int>(row)) + 10.0f};
        label->scale = 0.34f;
        label->color = theme.textPrimary;
        label->textRenderer = text;
        canvas.addElement(std::move(label));
    }

    auto preset = makeDropdown(kPresetOptions, theme, text, 0, kRowLabels[0]);
    auto scale = std::make_unique<UISlider>();
    scale->position = {kControlX, rowY(1)};
    scale->size = {420.0f, 44.0f};
    scale->minValue = 0.5f;
    scale->maxValue = 1.0f;
    scale->keyStep = 0.05f;
    scale->formatter = [](float v)
    {
        return UISlider::composePercentText(static_cast<int>(std::lround(v * 100.0f)));
    };
    scale->theme = &theme;
    scale->textRenderer = text;
    scale->accessible().label = std::string(tr(kRowLabels[1]));
    auto antiAlias = makeDropdown(kAntiAliasOptions, theme, text, 2, kRowLabels[2]);
    auto ao        = makeCheckbox(theme, text, 3, kRowLabels[3]);
    auto bloom     = makeCheckbox(theme, text, 4, kRowLabels[4]);
    auto volume    = makeCheckbox(theme, text, 5, kRowLabels[5]);
    auto terrain   = makeDropdown(kDetailOptions, theme, text, 6, kRowLabels[6]);
    auto foliage   = makeDropdown(kDetailOptions, theme, text, 7, kRowLabels[7]);
    auto grass     = makeDropdown(kDetailOptions, theme, text, 8, kRowLabels[8]);
    auto tree      = makeDropdown(kDetailOptions, theme, text, 9, kRowLabels[9]);
    auto vsync     = makeCheckbox(theme, text, 10, kRowLabels[10]);
    auto window    = makeDropdown(kWindowOptions, theme, text, 11, kRowLabels[11]);

    page = {preset.get(), scale.get(), antiAlias.get(), ao.get(), bloom.get(),
            volume.get(), terrain.get(), foliage.get(), grass.get(), tree.get(),
            vsync.get(), window.get()};

    SettingsEditor* ed = &editor;
    page.preset->onSelectionChanged.connect([ed](int i)
    {
        const QualityPreset p = kPresets[std::clamp(i, 0, static_cast<int>(std::size(kPresets)) - 1)];
        ed->mutate([p](Settings& s) { selectQualityPreset(s.display, p); });
    });
    page.renderScale->onValueChanged.connect([ed](float v)
    {
        setOption(*ed, [v](DisplaySettings& d) { d.renderScale = v; });
    });
    page.antiAlias->onSelectionChanged.connect([ed](int i)
    {
        const AntiAliasMode m = kAntiAliasModes[std::clamp(i, 0, 4)];
        setOption(*ed, [m](DisplaySettings& d) { d.graphics.antiAlias = m; });
    });
    UICheckbox* aoBox = page.ambientOcclusion;
    aoBox->onClick.connect([ed, aoBox]
    {
        setOption(*ed, [on = aoBox->checked](DisplaySettings& d) { d.graphics.ambientOcclusion = on; });
    });
    UICheckbox* bloomBox = page.bloom;
    bloomBox->onClick.connect([ed, bloomBox]
    {
        setOption(*ed, [on = bloomBox->checked](DisplaySettings& d) { d.graphics.bloom = on; });
    });
    UICheckbox* volumeBox = page.volumetrics;
    volumeBox->onClick.connect([ed, volumeBox]
    {
        setOption(*ed, [on = volumeBox->checked](DisplaySettings& d) { d.graphics.volumetrics = on; });
    });
    auto tier = [](int i) { return static_cast<DetailTier>(std::clamp(i, 0, 2)); };
    page.terrainDetail->onSelectionChanged.connect([ed, tier](int i)
    {
        setOption(*ed, [t = tier(i)](DisplaySettings& d) { d.graphics.terrainDetail = t; });
    });
    page.foliageDetail->onSelectionChanged.connect([ed, tier](int i)
    {
        setOption(*ed, [t = tier(i)](DisplaySettings& d) { d.graphics.foliageDetail = t; });
    });
    page.grassDetail->onSelectionChanged.connect([ed, tier](int i)
    {
        setOption(*ed, [t = tier(i)](DisplaySettings& d) { d.graphics.grassDetail = t; });
    });
    page.treeDetail->onSelectionChanged.connect([ed, tier](int i)
    {
        setOption(*ed, [t = tier(i)](DisplaySettings& d) { d.graphics.treeDetail = t; });
    });
    // Vertical sync and window mode are not part of a preset: no Custom.
    UICheckbox* vsyncBox = page.vsync;
    vsyncBox->onClick.connect([ed, vsyncBox]
    {
        ed->mutate([on = vsyncBox->checked](Settings& s) { s.display.vsync = on; });
    });
    page.windowMode->onSelectionChanged.connect([ed](int i)
    {
        ed->mutate([i](Settings& s) { s.display.fullscreen = (i == 1); });
    });

    // Copies pending() into the controls before they draw, every frame.
    canvas.addElement(std::make_unique<UIFrameHook>([ed, page]()
    {
        const DisplaySettings& d = ed->pending().display;
        page.preset->selectedIndex = indexOf(kPresets, d.qualityPreset);
        page.renderScale->value = std::clamp(d.renderScale, 0.5f, 1.0f);
        page.antiAlias->selectedIndex = indexOf(kAntiAliasModes, d.graphics.antiAlias);
        page.ambientOcclusion->checked = d.graphics.ambientOcclusion;
        page.bloom->checked = d.graphics.bloom;
        page.volumetrics->checked = d.graphics.volumetrics;
        page.terrainDetail->selectedIndex = static_cast<int>(d.graphics.terrainDetail);
        page.foliageDetail->selectedIndex = static_cast<int>(d.graphics.foliageDetail);
        page.grassDetail->selectedIndex = static_cast<int>(d.graphics.grassDetail);
        page.treeDetail->selectedIndex = static_cast<int>(d.graphics.treeDetail);
        page.vsync->checked = d.vsync;
        page.windowMode->selectedIndex = d.fullscreen ? 1 : 0;
    }));

    // Added bottom row first: an open dropdown list is drawn on top by
    // UISystem, so order only decides which control a press reaches first.
    canvas.addElement(std::move(window));
    canvas.addElement(std::move(vsync));
    canvas.addElement(std::move(tree));
    canvas.addElement(std::move(grass));
    canvas.addElement(std::move(foliage));
    canvas.addElement(std::move(terrain));
    canvas.addElement(std::move(volume));
    canvas.addElement(std::move(bloom));
    canvas.addElement(std::move(ao));
    canvas.addElement(std::move(antiAlias));
    canvas.addElement(std::move(scale));
    canvas.addElement(std::move(preset));
    return page;
}

void wireSettingsScreen(UISystem& ui, SettingsEditor& editor,
                        const std::filesystem::path& settingsPath)
{
    SettingsEditor* ed = &editor;
    ui.setScreenBuilder(GameScreen::Settings,
        [ed, settingsPath](UICanvas& canvas, const UITheme& theme, TextRenderer* text,
                           UISystem& system)
        {
            SettingsMenuActions actions;
            actions.apply           = [ed, settingsPath] { (void)ed->apply(settingsPath); };
            actions.revert          = [ed] { ed->revert(); };
            actions.restoreDefaults = [ed] { ed->restoreGraphicsDefaults(); };
            actions.isDirty         = [ed] { return ed->isDirty(); };
            buildSettingsMenu(canvas, theme, text, system, actions);
            buildGraphicsSettingsPage(canvas, theme, text, *ed);
        });
    ui.onModalPopped.connect([ed](GameScreen popped)
    {
        if (popped == GameScreen::Settings && ed->isDirty())
        {
            ed->revert();
        }
    });
}

std::vector<std::string_view> settingsPageKeys()
{
    std::vector<std::string_view> keys(std::begin(kRowLabels), std::end(kRowLabels));
    keys.insert(keys.end(), std::begin(kPresetOptions), std::end(kPresetOptions));
    keys.insert(keys.end(), std::begin(kAntiAliasOptions), std::end(kAntiAliasOptions));
    keys.insert(keys.end(), std::begin(kDetailOptions), std::end(kDetailOptions));
    keys.insert(keys.end(), std::begin(kWindowOptions), std::end(kWindowOptions));
    keys.insert(keys.end(), std::begin(kFooterKeys), std::end(kFooterKeys));
    return keys;
}

} // namespace Vestige
