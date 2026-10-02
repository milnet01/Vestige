// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_ui_system_input.cpp
/// @brief Phase 9C input-routing tests for UISystem (CPU-side flag logic).

#include <gtest/gtest.h>

#include "systems/ui_system.h"
#include "ui/ui_button.h"
#include "ui/ui_checkbox.h"
#include "ui/ui_dropdown.h"
#include "ui/ui_slider.h"

#include <GLFW/glfw3.h>

#include <memory>

using namespace Vestige;

TEST(UISystemInput, DefaultDoesNotCapture)
{
    UISystem sys;
    EXPECT_FALSE(sys.wantsCaptureInput());
    EXPECT_FALSE(sys.isModalCapture());
}

TEST(UISystemInput, ModalCaptureFlagDrivesWantsCaptureInput)
{
    UISystem sys;
    sys.setModalCapture(true);
    EXPECT_TRUE(sys.isModalCapture());
    EXPECT_TRUE(sys.wantsCaptureInput());

    sys.setModalCapture(false);
    EXPECT_FALSE(sys.wantsCaptureInput());
}

TEST(UISystemInput, ThemeAccessorReturnsMutableReference)
{
    UISystem sys;
    sys.getTheme().crosshairLength = 99.0f;
    EXPECT_FLOAT_EQ(sys.getTheme().crosshairLength, 99.0f);
}

TEST(UISystemInput, UpdateMouseHitOnEmptyCanvasDoesNotCapture)
{
    // No interactive elements in the canvas → cursor hit reports false →
    // wantsCaptureInput stays false (assuming modal is also off).
    UISystem sys;
    sys.updateMouseHit({100.0f, 100.0f}, 1920, 1080);
    EXPECT_FALSE(sys.wantsCaptureInput());
}

// -- 3D_E-0746: the mouse reaches the widgets --------------------------------

namespace
{

constexpr int kW = 1920;
constexpr int kH = 1080;

/// Adds @a el at @a pos with @a size to @a canvas; returns the raw pointer.
template <typename T>
T* place(UICanvas& canvas, std::unique_ptr<T> el, glm::vec2 pos, glm::vec2 size)
{
    el->position = pos;
    el->size = size;
    canvas.addElement(std::move(el));
    return static_cast<T*>(canvas.getElementAt(canvas.getElementCount() - 1));
}

/// Opens the Settings modal with a builder that adds one button at
/// {1000, 100}, size {200, 50}.
void openModalWithButton(UISystem& sys)
{
    sys.setScreenBuilder(GameScreen::Settings,
        [](UICanvas& c, const UITheme&, TextRenderer*, UISystem&)
        {
            place(c, std::make_unique<UIButton>(), {1000.0f, 100.0f}, {200.0f, 50.0f});
        });
    sys.pushModalScreen(GameScreen::Settings);
}

} // namespace

TEST(UISystemMouse, PressOnButtonFiresItAndFocusesIt)
{
    UISystem sys;
    auto* btn = place(sys.getCanvas(), std::make_unique<UIButton>(),
                      {100.0f, 100.0f}, {200.0f, 50.0f});
    int clicks = 0;
    btn->onClick.connect([&clicks] { ++clicks; });

    EXPECT_TRUE(sys.handleMousePress({150.0f, 120.0f}, kW, kH));
    EXPECT_EQ(clicks, 1);
    EXPECT_EQ(sys.getFocusedElement(), btn);
}

TEST(UISystemMouse, PressOnEmptySpaceIsNotConsumed)
{
    UISystem sys;
    place(sys.getCanvas(), std::make_unique<UIButton>(), {100.0f, 100.0f}, {200.0f, 50.0f});
    EXPECT_FALSE(sys.handleMousePress({900.0f, 900.0f}, kW, kH));
}

TEST(UISystemMouse, TopmostOfOverlappingElementsTakesThePress)
{
    UISystem sys;
    auto* under = place(sys.getCanvas(), std::make_unique<UIButton>(),
                        {100.0f, 100.0f}, {200.0f, 50.0f});
    auto* over = place(sys.getCanvas(), std::make_unique<UIButton>(),
                       {120.0f, 110.0f}, {200.0f, 50.0f});
    int underClicks = 0;
    int overClicks = 0;
    under->onClick.connect([&underClicks] { ++underClicks; });
    over->onClick.connect([&overClicks] { ++overClicks; });

    sys.handleMousePress({150.0f, 130.0f}, kW, kH);
    EXPECT_EQ(overClicks, 1);
    EXPECT_EQ(underClicks, 0);
}

TEST(UISystemMouse, DisabledButtonIgnoresPressAndEnter)
{
    UISystem sys;
    auto* btn = place(sys.getCanvas(), std::make_unique<UIButton>(),
                      {100.0f, 100.0f}, {200.0f, 50.0f});
    btn->disabled = true;
    int clicks = 0;
    btn->onClick.connect([&clicks] { ++clicks; });

    EXPECT_TRUE(sys.handleMousePress({150.0f, 120.0f}, kW, kH));
    EXPECT_TRUE(sys.handleKey(GLFW_KEY_ENTER, 0));
    EXPECT_EQ(clicks, 0);
}

TEST(UISystemMouse, OpenModalHidesTheRootCanvasFromThePress)
{
    UISystem sys;
    auto* rootBtn = place(sys.getCanvas(), std::make_unique<UIButton>(),
                          {100.0f, 100.0f}, {200.0f, 50.0f});
    int rootClicks = 0;
    rootBtn->onClick.connect([&rootClicks] { ++rootClicks; });
    openModalWithButton(sys);

    EXPECT_FALSE(sys.handleMousePress({150.0f, 120.0f}, kW, kH));
    EXPECT_EQ(rootClicks, 0);
    EXPECT_TRUE(sys.handleMousePress({1050.0f, 120.0f}, kW, kH));
}

TEST(UISystemMouse, CheckboxTogglesOnPressAndOnEnter)
{
    UISystem sys;
    auto* box = place(sys.getCanvas(), std::make_unique<UICheckbox>(),
                      {100.0f, 100.0f}, {180.0f, 20.0f});

    sys.handleMousePress({110.0f, 110.0f}, kW, kH);
    EXPECT_TRUE(box->checked);
    sys.handleKey(GLFW_KEY_ENTER, 0);  // the press focused it
    EXPECT_FALSE(box->checked);
}

TEST(UISystemMouse, SliderFollowsPressAndDragUntilRelease)
{
    UISystem sys;
    // Track width = 296 - 72 (readout) - 24 (gap) = 200 px.
    auto* slider = place(sys.getCanvas(), std::make_unique<UISlider>(),
                         {100.0f, 100.0f}, {296.0f, 44.0f});
    slider->minValue = 0.0f;
    slider->maxValue = 100.0f;
    slider->value = 0.0f;
    float reported = -1.0f;
    slider->onValueChanged.connect([&reported](float v) { reported = v; });

    sys.handleMousePress({150.0f, 120.0f}, kW, kH);
    EXPECT_FLOAT_EQ(slider->value, 25.0f);
    sys.handleMouseMove({250.0f, 400.0f});  // the drag keeps the slider off its row
    EXPECT_FLOAT_EQ(slider->value, 75.0f);
    EXPECT_FLOAT_EQ(reported, 75.0f);
    sys.handleMouseMove({900.0f, 120.0f});
    EXPECT_FLOAT_EQ(slider->value, 100.0f);  // clamped to the track's end

    sys.handleMouseRelease();
    sys.handleMouseMove({100.0f, 120.0f});
    EXPECT_FLOAT_EQ(slider->value, 100.0f);
}

TEST(UISystemMouse, DropdownOpensThenPicksTheRowUnderThePress)
{
    UISystem sys;
    auto* dd = place(sys.getCanvas(), std::make_unique<UIDropdown>(),
                     {100.0f, 100.0f}, {220.0f, 40.0f});
    dd->options = {{"low", "Low"}, {"medium", "Medium"}, {"high", "High"}};
    int picked = -1;
    dd->onSelectionChanged.connect([&picked](int i) { picked = i; });

    sys.handleMousePress({150.0f, 120.0f}, kW, kH);
    EXPECT_TRUE(dd->open);

    // Rows start 4 px under the box and are 36 px tall: row 1 spans
    // y = 100 + 40 + 4 + 36 .. + 36, i.e. 180 .. 216.
    EXPECT_TRUE(sys.handleMousePress({150.0f, 190.0f}, kW, kH));
    EXPECT_FALSE(dd->open);
    EXPECT_EQ(dd->selectedIndex, 1);
    EXPECT_EQ(picked, 1);
}

TEST(UISystemMouse, PressOutsideAnOpenDropdownOnlyClosesIt)
{
    UISystem sys;
    auto* dd = place(sys.getCanvas(), std::make_unique<UIDropdown>(),
                     {100.0f, 100.0f}, {220.0f, 40.0f});
    dd->options = {{"a", "A"}, {"b", "B"}};
    auto* btn = place(sys.getCanvas(), std::make_unique<UIButton>(),
                      {600.0f, 600.0f}, {200.0f, 50.0f});
    int clicks = 0;
    btn->onClick.connect([&clicks] { ++clicks; });

    sys.handleMousePress({150.0f, 120.0f}, kW, kH);
    ASSERT_TRUE(dd->open);
    EXPECT_TRUE(sys.handleMousePress({650.0f, 620.0f}, kW, kH));
    EXPECT_FALSE(dd->open);
    EXPECT_EQ(dd->selectedIndex, 0);
    EXPECT_EQ(clicks, 0);
}

TEST(UISystemMouse, ClickThatChangesScreenLeavesNoPressedElement)
{
    // The handler destroys the button it was dispatched to.
    UISystem sys;
    auto* btn = place(sys.getCanvas(), std::make_unique<UIButton>(),
                      {100.0f, 100.0f}, {200.0f, 50.0f});
    btn->onClick.connect([&sys] { sys.setRootScreen(GameScreen::None); });

    EXPECT_TRUE(sys.handleMousePress({150.0f, 120.0f}, kW, kH));
    EXPECT_EQ(sys.getPressedElement(), nullptr);
    EXPECT_EQ(sys.getFocusedElement(), nullptr);
}

TEST(UISystemMouse, ScreenChangeDropsTheDragAndTheOpenList)
{
    UISystem sys;
    openModalWithButton(sys);
    sys.handleMousePress({1050.0f, 120.0f}, kW, kH);
    ASSERT_NE(sys.getPressedElement(), nullptr);

    sys.popModalScreen();
    EXPECT_EQ(sys.getPressedElement(), nullptr);

    sys.setScreenBuilder(GameScreen::Settings,
        [](UICanvas& c, const UITheme&, TextRenderer*, UISystem&)
        {
            auto dd = std::make_unique<UIDropdown>();
            dd->options = {{"a", "A"}};
            place(c, std::move(dd), {100.0f, 100.0f}, {220.0f, 40.0f});
        });
    sys.pushModalScreen(GameScreen::Settings);
    sys.handleMousePress({150.0f, 120.0f}, kW, kH);  // opens the list
    sys.popModalScreen();

    // The list's dropdown is gone; a press on empty space is not consumed.
    EXPECT_FALSE(sys.handleMousePress({150.0f, 120.0f}, kW, kH));
}
