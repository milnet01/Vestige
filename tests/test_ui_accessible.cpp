// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_ui_accessible.cpp
/// @brief Phase 10 accessibility coverage for the ARIA-like
///        `UIAccessibleInfo` metadata layer and the
///        `UIElement::collectAccessible` tree walk.

#include <gtest/gtest.h>

#include "ui/ui_accessible.h"
#include "ui/ui_button.h"
#include "ui/ui_canvas.h"
#include "ui/ui_checkbox.h"
#include "ui/ui_crosshair.h"
#include "ui/ui_dropdown.h"
#include "ui/ui_image.h"
#include "ui/ui_keybind_row.h"
#include "ui/ui_label.h"
#include "ui/ui_panel.h"
#include "ui/ui_progress_bar.h"
#include "ui/ui_slider.h"

#include <memory>

using namespace Vestige;

// -- Role labels --

TEST(UIAccessibleRole, LabelsAreStableStrings)
{
    EXPECT_STREQ(uiAccessibleRoleLabel(UIAccessibleRole::Unknown),     "Unknown");
    EXPECT_STREQ(uiAccessibleRoleLabel(UIAccessibleRole::Label),       "Label");
    EXPECT_STREQ(uiAccessibleRoleLabel(UIAccessibleRole::Panel),       "Panel");
    EXPECT_STREQ(uiAccessibleRoleLabel(UIAccessibleRole::Image),       "Image");
    EXPECT_STREQ(uiAccessibleRoleLabel(UIAccessibleRole::Button),      "Button");
    EXPECT_STREQ(uiAccessibleRoleLabel(UIAccessibleRole::Checkbox),    "Checkbox");
    EXPECT_STREQ(uiAccessibleRoleLabel(UIAccessibleRole::Slider),      "Slider");
    EXPECT_STREQ(uiAccessibleRoleLabel(UIAccessibleRole::Dropdown),    "Dropdown");
    EXPECT_STREQ(uiAccessibleRoleLabel(UIAccessibleRole::KeybindRow),  "KeybindRow");
    EXPECT_STREQ(uiAccessibleRoleLabel(UIAccessibleRole::ProgressBar), "ProgressBar");
    EXPECT_STREQ(uiAccessibleRoleLabel(UIAccessibleRole::Crosshair),   "Crosshair");
}

// -- Widget defaults (constructor-set roles) --

TEST(UIAccessibleInfo, EveryWidgetSetsItsRoleInTheConstructor)
{
    EXPECT_EQ(UIButton().accessible().role,      UIAccessibleRole::Button);
    EXPECT_EQ(UICheckbox().accessible().role,    UIAccessibleRole::Checkbox);
    EXPECT_EQ(UISlider().accessible().role,      UIAccessibleRole::Slider);
    EXPECT_EQ(UIDropdown().accessible().role,    UIAccessibleRole::Dropdown);
    EXPECT_EQ(UIKeybindRow().accessible().role,  UIAccessibleRole::KeybindRow);
    EXPECT_EQ(UILabel().accessible().role,       UIAccessibleRole::Label);
    EXPECT_EQ(UIPanel().accessible().role,       UIAccessibleRole::Panel);
    EXPECT_EQ(UIImage().accessible().role,       UIAccessibleRole::Image);
    EXPECT_EQ(UIProgressBar().accessible().role, UIAccessibleRole::ProgressBar);
    EXPECT_EQ(UICrosshair().accessible().role,   UIAccessibleRole::Crosshair);
}

TEST(UIAccessibleInfo, StringFieldsDefaultToEmpty)
{
    UIButton b;
    EXPECT_TRUE(b.accessible().label.empty());
    EXPECT_TRUE(b.accessible().description.empty());
    EXPECT_TRUE(b.accessible().hint.empty());
    EXPECT_TRUE(b.accessible().value.empty());
}

TEST(UIAccessibleInfo, LabelAndDescriptionAreMutable)
{
    UIButton b;
    b.accessible().label       = "Play Game";
    b.accessible().description = "Start a new campaign";
    b.accessible().hint        = "press Enter to activate";

    EXPECT_EQ(b.accessible().label,       "Play Game");
    EXPECT_EQ(b.accessible().description, "Start a new campaign");
    EXPECT_EQ(b.accessible().hint,        "press Enter to activate");
}

// -- Single-element enumeration --

TEST(UIElement_CollectAccessible, UnknownRoleWithEmptyLabelIsOmitted)
{
    // A bare UIElement surrogate: the base class defaults to Unknown +
    // empty label, so it should contribute nothing to enumeration
    // unless a subclass sets a role or a caller sets a label.
    UIPanel p;  // Role is Panel by default — override to Unknown.
    p.accessible().role = UIAccessibleRole::Unknown;
    p.accessible().label.clear();

    std::vector<UIAccessibilitySnapshot> out;
    p.collectAccessible(out);
    EXPECT_TRUE(out.empty());
}

TEST(UIElement_CollectAccessible, RoleAloneIsEnoughToBeAnnounced)
{
    UIButton b;  // Button role set by default, no label yet.
    std::vector<UIAccessibilitySnapshot> out;
    b.collectAccessible(out);

    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].info.role, UIAccessibleRole::Button);
    EXPECT_TRUE(out[0].interactive);
}

TEST(UIElement_CollectAccessible, LabelAloneIsEnoughEvenWithUnknownRole)
{
    UIPanel p;
    p.accessible().role = UIAccessibleRole::Unknown;
    p.accessible().label = "Pause Menu";

    std::vector<UIAccessibilitySnapshot> out;
    p.collectAccessible(out);

    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].info.role, UIAccessibleRole::Unknown);
    EXPECT_EQ(out[0].info.label, "Pause Menu");
}

TEST(UIElement_CollectAccessible, HiddenElementsAreSkippedEntirely)
{
    UIPanel outer;
    outer.accessible().label = "visible outer";

    auto labelled = std::make_unique<UILabel>();
    labelled->accessible().label = "child text";
    outer.addChild(std::move(labelled));

    outer.visible = false;

    std::vector<UIAccessibilitySnapshot> out;
    outer.collectAccessible(out);
    EXPECT_TRUE(out.empty())
        << "Hidden subtrees must not leak any entries, including their children";
}

TEST(UIElement_CollectAccessible, InteractiveFlagIsCarriedOver)
{
    UIButton b;     // interactive = true
    UILabel  l;     // interactive = false

    std::vector<UIAccessibilitySnapshot> out;
    b.collectAccessible(out);
    l.accessible().label = "credits";  // give it something to announce
    l.collectAccessible(out);

    ASSERT_EQ(out.size(), 2u);
    EXPECT_TRUE(out[0].interactive);
    EXPECT_FALSE(out[1].interactive);
}

// -- Tree walk --

TEST(UIElement_CollectAccessible, WalksChildrenInOrder)
{
    UIPanel root;
    root.accessible().label = "Main Menu";

    auto play     = std::make_unique<UIButton>();   play->accessible().label     = "Play Game";
    auto settings = std::make_unique<UIButton>();   settings->accessible().label = "Settings";
    auto quit     = std::make_unique<UIButton>();   quit->accessible().label     = "Quit";

    root.addChild(std::move(play));
    root.addChild(std::move(settings));
    root.addChild(std::move(quit));

    std::vector<UIAccessibilitySnapshot> out;
    root.collectAccessible(out);

    ASSERT_EQ(out.size(), 4u);
    EXPECT_EQ(out[0].info.role,  UIAccessibleRole::Panel);
    EXPECT_EQ(out[0].info.label, "Main Menu");
    EXPECT_EQ(out[1].info.label, "Play Game");
    EXPECT_EQ(out[2].info.label, "Settings");
    EXPECT_EQ(out[3].info.label, "Quit");
}

TEST(UIElement_CollectAccessible, SkipsUnlabelledContainersButKeepsTheirChildren)
{
    // An unnamed group (no role, no label) should still let its
    // labelled children be announced — a Vestige HUD might use a
    // bare UIElement as a spacer / layout shim.
    UIPanel group;
    group.accessible().role = UIAccessibleRole::Unknown;
    // label stays empty

    auto hp = std::make_unique<UIProgressBar>();
    hp->accessible().label = "Health";
    hp->accessible().value = "75%";
    group.addChild(std::move(hp));

    std::vector<UIAccessibilitySnapshot> out;
    group.collectAccessible(out);

    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].info.role, UIAccessibleRole::ProgressBar);
    EXPECT_EQ(out[0].info.label, "Health");
    EXPECT_EQ(out[0].info.value, "75%");
}

// -- UICanvas integration --

TEST(UICanvas_CollectAccessible, FlattensAllRootElements)
{
    UICanvas canvas;

    auto title = std::make_unique<UILabel>();
    title->accessible().label = "Vestige";
    canvas.addElement(std::move(title));

    auto play = std::make_unique<UIButton>();
    play->accessible().label = "Play";
    canvas.addElement(std::move(play));

    auto vol = std::make_unique<UISlider>();
    vol->accessible().label = "Master Volume";
    vol->accessible().value = "75";
    canvas.addElement(std::move(vol));

    auto snapshots = canvas.collectAccessible();
    ASSERT_EQ(snapshots.size(), 3u);
    EXPECT_EQ(snapshots[0].info.role,  UIAccessibleRole::Label);
    EXPECT_EQ(snapshots[0].info.label, "Vestige");
    EXPECT_EQ(snapshots[1].info.role,  UIAccessibleRole::Button);
    EXPECT_EQ(snapshots[1].info.label, "Play");
    EXPECT_EQ(snapshots[2].info.role,  UIAccessibleRole::Slider);
    EXPECT_EQ(snapshots[2].info.label, "Master Volume");
    EXPECT_EQ(snapshots[2].info.value, "75");
}

TEST(UICanvas_CollectAccessible, EmptyCanvasReturnsEmptyVector)
{
    UICanvas canvas;
    EXPECT_TRUE(canvas.collectAccessible().empty());
}
