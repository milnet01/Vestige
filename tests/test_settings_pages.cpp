// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_settings_pages.cpp
/// @brief 3D_E-0759 — the Audio, Controls and Accessibility Settings pages
///        edit the right fields, show pending() each frame, and the sidebar
///        switches pages; Restore defaults resets only the page on show.

#include "core/settings.h"
#include "core/settings_editor.h"
#include "localization/localization_service.h"
#include "systems/ui_system.h"
#include "ui/accessibility_settings_page.h"
#include "ui/audio_settings_page.h"
#include "ui/controls_settings_page.h"
#include "ui/graphics_settings_page.h"
#include "ui/ui_button.h"
#include "ui/ui_canvas.h"
#include "ui/ui_checkbox.h"
#include "ui/ui_dropdown.h"
#include "ui/ui_frame_hook.h"
#include "ui/ui_slider.h"
#include "ui/ui_theme.h"

#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace Vestige;

namespace
{

// Runs every frame hook directly under @a root, as a frame would.
void runFrameHooks(UIElement* root)
{
    for (std::size_t i = 0; i < root->getChildCount(); ++i)
    {
        if (const auto* hook = dynamic_cast<const UIFrameHook*>(root->getChildAt(i)))
        {
            hook->runNow();
        }
    }
}

template <typename Page, Page (*Build)(UICanvas&, const UITheme&, TextRenderer*, SettingsEditor&)>
struct Fixture
{
    UICanvas canvas;
    UITheme theme = UITheme::defaultTheme();
    SettingsEditor editor{Settings{}, {}};
    Page page = Build(canvas, theme, nullptr, editor);

    const Settings& pending() const { return editor.pending(); }
};

using AudioFixture = Fixture<AudioSettingsPage, &buildAudioSettingsPage>;
using ControlsFixture = Fixture<ControlsSettingsPage, &buildControlsSettingsPage>;
using AccessibilityFixture = Fixture<AccessibilitySettingsPage, &buildAccessibilitySettingsPage>;

// The elements Tab visits in order, starting from no focus, until it wraps.
std::vector<UIElement*> tabOrder(UISystem& ui)
{
    ui.setFocusedElement(nullptr);
    std::vector<UIElement*> order;
    for (int i = 0; i < 500; ++i)
    {
        ui.handleKey(GLFW_KEY_TAB, 0);
        UIElement* focused = ui.getFocusedElement();
        if (focused == nullptr || (!order.empty() && focused == order.front()))
        {
            break;
        }
        order.push_back(focused);
    }
    return order;
}

// The sidebar button whose accessible label is category @a key's text.
UIButton* findCategoryButton(UISystem& ui, const char* key)
{
    const std::string label(tr(key));
    for (UIElement* el : tabOrder(ui))
    {
        auto* button = dynamic_cast<UIButton*>(el);
        if (button != nullptr && button->accessible().label == label)
        {
            return button;
        }
    }
    return nullptr;
}

UIButton* findFooterButton(UISystem& ui, const char* key)
{
    const std::string label(tr(key));
    for (UIElement* el : tabOrder(ui))
    {
        auto* button = dynamic_cast<UIButton*>(el);
        if (button != nullptr && button->label == label)
        {
            return button;
        }
    }
    return nullptr;
}

template <typename T>
std::size_t countOf(const std::vector<UIElement*>& elements)
{
    return static_cast<std::size_t>(std::count_if(elements.begin(), elements.end(),
        [](UIElement* el) { return dynamic_cast<T*>(el) != nullptr; }));
}

struct ScreenFixture
{
    SettingsEditor editor{Settings{}, {}};
    UISystem ui;

    ScreenFixture()
    {
        wireSettingsScreen(ui, editor, std::filesystem::path("unused-settings.json"));
        ui.pushModalScreen(GameScreen::Settings);
    }

    void select(const char* categoryKey)
    {
        UIButton* button = findCategoryButton(ui, categoryKey);
        ASSERT_NE(button, nullptr) << categoryKey;
        ui.setFocusedElement(button);
        ui.handleKey(GLFW_KEY_ENTER, 0);
    }
};

} // namespace

// -- Audio -------------------------------------------------------------------

TEST(AudioSettingsPage, VolumeSlidersWriteTheirBus)
{
    for (std::size_t bus = 0; bus < 6; ++bus)
    {
        AudioFixture f;
        runFrameHooks(f.page.root);
        const float before = f.pending().audio.busGains[bus];
        f.page.volumes[bus]->adjust(-1);
        EXPECT_NEAR(f.pending().audio.busGains[bus], before - 0.05f, 1e-5f) << bus;
        for (std::size_t other = 0; other < 6; ++other)
        {
            if (other != bus)
            {
                EXPECT_FLOAT_EQ(f.pending().audio.busGains[other],
                                AudioSettings{}.busGains[other]) << bus << "/" << other;
            }
        }
    }
}

TEST(AudioSettingsPage, HeadphoneCheckboxWritesHrtf)
{
    AudioFixture f;
    runFrameHooks(f.page.root);
    ASSERT_TRUE(f.page.headphone3d->checked);
    f.page.headphone3d->activate();
    EXPECT_FALSE(f.pending().audio.hrtfEnabled);
}

TEST(AudioSettingsPage, FrameHookShowsPending)
{
    AudioFixture f;
    f.editor.mutate([](Settings& s)
    {
        s.audio.busGains[1] = 0.25f;
        s.audio.hrtfEnabled = false;
    });
    runFrameHooks(f.page.root);
    EXPECT_FLOAT_EQ(f.page.volumes[1]->value, 0.25f);
    EXPECT_FALSE(f.page.headphone3d->checked);
}

// -- Controls ----------------------------------------------------------------

TEST(ControlsSettingsPage, ControlsWriteTheirFields)
{
    ControlsFixture f;
    runFrameHooks(f.page.root);
    f.page.mouseSensitivity->adjust(+1);
    f.page.invertMouse->activate();
    f.page.leftStickDeadZone->adjust(+1);
    f.page.rightStickDeadZone->adjust(-1);
    const ControlsSettings& c = f.pending().controls;
    EXPECT_NEAR(c.mouseSensitivity, 1.1f, 1e-5f);
    EXPECT_TRUE(c.invertY);
    EXPECT_NEAR(c.gamepadDeadzoneLeft, 0.20f, 1e-5f);
    EXPECT_NEAR(c.gamepadDeadzoneRight, 0.05f, 1e-5f);
}

TEST(ControlsSettingsPage, FrameHookShowsPending)
{
    ControlsFixture f;
    f.editor.mutate([](Settings& s)
    {
        s.controls.mouseSensitivity = 3.5f;
        s.controls.invertY = true;
        s.controls.gamepadDeadzoneLeft = 0.4f;
        s.controls.gamepadDeadzoneRight = 0.6f;
    });
    runFrameHooks(f.page.root);
    EXPECT_FLOAT_EQ(f.page.mouseSensitivity->value, 3.5f);
    EXPECT_TRUE(f.page.invertMouse->checked);
    EXPECT_FLOAT_EQ(f.page.leftStickDeadZone->value, 0.4f);
    EXPECT_FLOAT_EQ(f.page.rightStickDeadZone->value, 0.6f);
}

// -- Accessibility -----------------------------------------------------------

TEST(AccessibilitySettingsPage, DropdownsWriteTheStoredStrings)
{
    AccessibilityFixture f;
    f.page.interfaceScale->choose(2);
    f.page.subtitleSize->choose(3);
    f.page.colourVisionFilter->choose(1);
    const AccessibilitySettings& a = f.pending().accessibility;
    EXPECT_EQ(a.uiScalePreset, "1.5x");
    EXPECT_EQ(a.subtitleSize, "xl");
    EXPECT_EQ(a.colorVisionFilter, "protanopia");
}

TEST(AccessibilitySettingsPage, CheckboxesWriteTheirFields)
{
    AccessibilityFixture f;
    runFrameHooks(f.page.root);
    for (UICheckbox* box : {f.page.highContrast, f.page.reducedMotion, f.page.subtitles,
                            f.page.photosensitiveSafe, f.page.fog, f.page.volumetricFog,
                            f.page.godRays, f.page.dynamicLighting, f.page.freezeLighting})
    {
        box->activate();
    }
    const AccessibilitySettings& a = f.pending().accessibility;
    const AccessibilitySettings defaults;
    EXPECT_NE(a.highContrast, defaults.highContrast);
    EXPECT_NE(a.reducedMotion, defaults.reducedMotion);
    EXPECT_NE(a.subtitlesEnabled, defaults.subtitlesEnabled);
    EXPECT_NE(a.photosensitiveSafety.enabled, defaults.photosensitiveSafety.enabled);
    EXPECT_NE(a.postProcess.fogEnabled, defaults.postProcess.fogEnabled);
    EXPECT_NE(a.postProcess.volumetricFogEnabled, defaults.postProcess.volumetricFogEnabled);
    EXPECT_NE(a.postProcess.godRaysEnabled, defaults.postProcess.godRaysEnabled);
    EXPECT_NE(a.postProcess.dynamicGiEnabled, defaults.postProcess.dynamicGiEnabled);
    EXPECT_NE(a.postProcess.reduceMotionGi, defaults.postProcess.reduceMotionGi);
    // Not on the page: the unshipped effects stay as they were.
    EXPECT_EQ(a.postProcess.depthOfFieldEnabled, defaults.postProcess.depthOfFieldEnabled);
    EXPECT_EQ(a.postProcess.motionBlurEnabled, defaults.postProcess.motionBlurEnabled);
}

TEST(AccessibilitySettingsPage, FogIntensitySliderWritesTheScale)
{
    AccessibilityFixture f;
    runFrameHooks(f.page.root);
    f.page.fogIntensity->adjust(-1);
    EXPECT_NEAR(f.pending().accessibility.postProcess.fogIntensityScale, 0.95f, 1e-5f);
}

TEST(AccessibilitySettingsPage, FrameHookShowsPending)
{
    AccessibilityFixture f;
    f.editor.mutate([](Settings& s)
    {
        s.accessibility.uiScalePreset = "2.0x";
        s.accessibility.subtitleSize = "small";
        s.accessibility.colorVisionFilter = "tritanopia";
        s.accessibility.highContrast = true;
        s.accessibility.postProcess.fogIntensityScale = 0.3f;
        s.accessibility.postProcess.reduceMotionGi = true;
    });
    runFrameHooks(f.page.root);
    EXPECT_EQ(f.page.interfaceScale->selectedIndex, 3);
    EXPECT_EQ(f.page.subtitleSize->selectedIndex, 0);
    EXPECT_EQ(f.page.colourVisionFilter->selectedIndex, 3);
    EXPECT_TRUE(f.page.highContrast->checked);
    EXPECT_FLOAT_EQ(f.page.fogIntensity->value, 0.3f);
    EXPECT_TRUE(f.page.freezeLighting->checked);
}

TEST(AccessibilitySettingsPage, LastRowStaysAboveTheFooterRule)
{
    AccessibilityFixture f;
    // menu_prefabs.cpp: footer rule at modalY + modalH - 70 - 16.
    constexpr float kFooterRuleY = 80.0f + 920.0f - 70.0f - 16.0f;
    const UIElement* last = f.page.freezeLighting;
    EXPECT_LT(last->position.y + last->size.y, kFooterRuleY);
}

// -- The sidebar ---------------------------------------------------------------

TEST(SettingsScreen, OpensOnDisplay)
{
    ScreenFixture f;
    const auto order = tabOrder(f.ui);
    EXPECT_GT(countOf<UIDropdown>(order), 0u);  // the Display page's dropdowns
    EXPECT_EQ(countOf<UICheckbox>(order), 4u);  // AO, bloom, volumetrics, vsync
}

TEST(SettingsScreen, SidebarListsFourCategoriesWithoutGameplay)
{
    ScreenFixture f;
    for (const char* key : {"ui.settings.category.display", "ui.settings.category.audio",
                            "ui.settings.category.controls",
                            "ui.settings.category.accessibility"})
    {
        EXPECT_NE(findCategoryButton(f.ui, key), nullptr) << key;
    }
    EXPECT_EQ(findCategoryButton(f.ui, "ui.settings.category.gameplay"), nullptr);
}

TEST(SettingsScreen, SelectingAudioShowsOnlyTheAudioPage)
{
    ScreenFixture f;
    f.select("ui.settings.category.audio");
    const auto order = tabOrder(f.ui);
    EXPECT_EQ(countOf<UISlider>(order), 6u);    // the six volumes
    EXPECT_EQ(countOf<UICheckbox>(order), 1u);  // headphone 3D sound
    EXPECT_EQ(countOf<UIDropdown>(order), 0u);  // nothing from Display

    // Hit-testing skips the hidden Display page too: its render-scale
    // slider sat at row 1 of the control column, where audio's Music is.
    glm::vec2 absPos{0.0f};
    UIElement* hit = f.ui.getModalCanvas().findInteractiveAt({1100.0f, 300.0f}, 1920, 1080, absPos);
    ASSERT_NE(hit, nullptr);
    EXPECT_NE(std::find(order.begin(), order.end(), hit), order.end());
}

TEST(SettingsScreen, EachCategoryShowsItsOwnControls)
{
    ScreenFixture f;
    f.select("ui.settings.category.controls");
    auto order = tabOrder(f.ui);
    EXPECT_EQ(countOf<UISlider>(order), 3u);
    EXPECT_EQ(countOf<UICheckbox>(order), 1u);
    EXPECT_EQ(countOf<UIDropdown>(order), 0u);

    f.select("ui.settings.category.accessibility");
    order = tabOrder(f.ui);
    EXPECT_EQ(countOf<UISlider>(order), 1u);
    EXPECT_EQ(countOf<UICheckbox>(order), 9u);
    EXPECT_EQ(countOf<UIDropdown>(order), 3u);

    f.select("ui.settings.category.display");
    order = tabOrder(f.ui);
    EXPECT_EQ(countOf<UICheckbox>(order), 4u);
    EXPECT_EQ(countOf<UIDropdown>(order), 7u);
}

TEST(SettingsScreen, ReopeningStartsOnDisplay)
{
    ScreenFixture f;
    f.select("ui.settings.category.audio");
    f.ui.popModalScreen();
    f.ui.pushModalScreen(GameScreen::Settings);
    EXPECT_EQ(countOf<UIDropdown>(tabOrder(f.ui)), 7u);
}

TEST(SettingsScreen, RestoreDefaultsResetsOnlyTheActivePage)
{
    ScreenFixture f;
    f.editor.mutate([](Settings& s)
    {
        s.display.graphics.bloom = false;
        s.audio.busGains[0] = 0.2f;
        s.controls.mouseSensitivity = 4.0f;
        s.controls.bindings.push_back(ActionBindingWire{});
        s.accessibility.highContrast = true;
    });

    f.select("ui.settings.category.audio");
    UIButton* restore = findFooterButton(f.ui, "ui.settings.restore_defaults");
    ASSERT_NE(restore, nullptr);
    restore->activate();
    EXPECT_FLOAT_EQ(f.editor.pending().audio.busGains[0], AudioSettings{}.busGains[0]);
    EXPECT_FALSE(f.editor.pending().display.graphics.bloom);
    EXPECT_FLOAT_EQ(f.editor.pending().controls.mouseSensitivity, 4.0f);
    EXPECT_TRUE(f.editor.pending().accessibility.highContrast);

    f.select("ui.settings.category.controls");
    restore->activate();
    EXPECT_FLOAT_EQ(f.editor.pending().controls.mouseSensitivity, 1.0f);
    EXPECT_EQ(f.editor.pending().controls.bindings.size(), 1u);  // bindings kept
    EXPECT_FALSE(f.editor.pending().display.graphics.bloom);
    EXPECT_TRUE(f.editor.pending().accessibility.highContrast);

    f.select("ui.settings.category.accessibility");
    restore->activate();
    EXPECT_FALSE(f.editor.pending().accessibility.highContrast);
    EXPECT_FALSE(f.editor.pending().display.graphics.bloom);

    f.select("ui.settings.category.display");
    restore->activate();
    EXPECT_TRUE(f.editor.pending().display.graphics.bloom);
}
