// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_graphics_settings_page.cpp
/// @brief 3D_E-0035 — the player Settings page edits the right fields and
///        marks the preset Custom (INV-2), and closing Settings without
///        Apply reverts (INV-7).

#include "core/settings.h"
#include "core/settings_apply.h"
#include "core/settings_editor.h"
#include "systems/ui_system.h"
#include "ui/graphics_settings_page.h"
#include "ui/ui_canvas.h"
#include "ui/ui_checkbox.h"
#include "ui/ui_dropdown.h"
#include "ui/ui_slider.h"
#include "ui/ui_theme.h"

#include <gtest/gtest.h>

#include <filesystem>

using namespace Vestige;

namespace
{

class BloomSink final : public RendererQualitySink
{
public:
    bool bloom = false;
    void setAntiAliasMode(AntiAliasMode) override {}
    void setSsaoEnabled(bool) override {}
    void setBloomEnabled(bool e) override { bloom = e; }
    void setHeavyPostEnabled(bool) override {}
    void setTerrainGroundQuality(TerrainGroundQuality) override {}
    void setFoliageQuality(FoliageQuality) override {}
    void setGrassQuality(GrassQuality) override {}
    void setTreeQuality(TreeQuality) override {}
};

struct PageFixture
{
    UICanvas canvas;
    UITheme theme = UITheme::defaultTheme();
    SettingsEditor editor{Settings{}, {}};
    GraphicsSettingsPage page = buildGraphicsSettingsPage(canvas, theme, nullptr, editor);

    const DisplaySettings& display() const { return editor.pending().display; }
};

} // namespace

// -- INV-2: every option marks the preset Custom ---------------------------

TEST(GraphicsSettingsPage, StartsOnTheHighPreset)
{
    PageFixture f;
    EXPECT_EQ(f.display().qualityPreset, QualityPreset::High);
}

TEST(GraphicsSettingsPage, RenderScaleSetsCustom)
{
    PageFixture f;
    f.page.renderScale->value = 1.0f;
    f.page.renderScale->adjust(-1);
    EXPECT_FLOAT_EQ(f.display().renderScale, 0.95f);
    EXPECT_EQ(f.display().qualityPreset, QualityPreset::Custom);
}

TEST(GraphicsSettingsPage, AntiAliasingSetsCustom)
{
    PageFixture f;
    f.page.antiAlias->choose(1);  // FXAA
    EXPECT_EQ(f.display().graphics.antiAlias, AntiAliasMode::FXAA);
    EXPECT_EQ(f.display().qualityPreset, QualityPreset::Custom);
}

TEST(GraphicsSettingsPage, CheckboxesSetCustom)
{
    struct Case { UICheckbox* GraphicsSettingsPage::*box; bool GraphicsSettings::*field; };
    for (const Case& c : {Case{&GraphicsSettingsPage::ambientOcclusion, &GraphicsSettings::ambientOcclusion},
                          Case{&GraphicsSettingsPage::bloom,            &GraphicsSettings::bloom},
                          Case{&GraphicsSettingsPage::volumetrics,      &GraphicsSettings::volumetrics}})
    {
        PageFixture f;
        UICheckbox* box = f.page.*(c.box);
        box->checked = true;  // as the frame sync shows High
        box->activate();
        EXPECT_FALSE(f.display().graphics.*(c.field));
        EXPECT_EQ(f.display().qualityPreset, QualityPreset::Custom);
    }
}

TEST(GraphicsSettingsPage, DetailDropdownsSetCustom)
{
    struct Case { UIDropdown* GraphicsSettingsPage::*dd; DetailTier GraphicsSettings::*field; };
    for (const Case& c : {Case{&GraphicsSettingsPage::terrainDetail, &GraphicsSettings::terrainDetail},
                          Case{&GraphicsSettingsPage::foliageDetail, &GraphicsSettings::foliageDetail},
                          Case{&GraphicsSettingsPage::grassDetail,   &GraphicsSettings::grassDetail},
                          Case{&GraphicsSettingsPage::treeDetail,    &GraphicsSettings::treeDetail}})
    {
        PageFixture f;
        (f.page.*(c.dd))->choose(0);
        EXPECT_EQ(f.display().graphics.*(c.field), DetailTier::Low);
        EXPECT_EQ(f.display().qualityPreset, QualityPreset::Custom);
    }
}

TEST(GraphicsSettingsPage, PresetDropdownAppliesTheRow)
{
    PageFixture f;
    f.page.preset->choose(0);  // Low
    EXPECT_EQ(f.display().qualityPreset, QualityPreset::Low);
    EXPECT_EQ(f.display().graphics, qualityRowFor(QualityPreset::Low).graphics);
    EXPECT_FLOAT_EQ(f.display().renderScale, qualityRowFor(QualityPreset::Low).renderScale);
}

TEST(GraphicsSettingsPage, VsyncAndWindowModeKeepThePreset)
{
    PageFixture f;
    f.page.vsync->checked = true;
    f.page.vsync->activate();
    f.page.windowMode->choose(1);
    EXPECT_FALSE(f.display().vsync);
    EXPECT_TRUE(f.display().fullscreen);
    EXPECT_EQ(f.display().qualityPreset, QualityPreset::High);
}

// -- INV-7: closing Settings without Apply reverts --------------------------

TEST(GraphicsSettingsPage, CloseWithoutApplyReverts)
{
    BloomSink sink;
    SettingsEditor::ApplyTargets targets{};
    targets.rendererQuality = &sink;
    SettingsEditor editor(Settings{}, targets);
    editor.forceLiveApply();
    ASSERT_TRUE(sink.bloom);

    UISystem ui;
    wireSettingsScreen(ui, editor, std::filesystem::path("unused-settings.json"));
    ui.pushModalScreen(GameScreen::Settings);
    editor.mutate([](Settings& s)
    {
        s.display.graphics.bloom = false;
        s.display.qualityPreset = QualityPreset::Custom;
    });
    ASSERT_TRUE(editor.isDirty());
    ASSERT_FALSE(sink.bloom);  // the live preview reached the renderer

    ui.popModalScreen();
    EXPECT_FALSE(editor.isDirty());
    EXPECT_TRUE(sink.bloom);
}
