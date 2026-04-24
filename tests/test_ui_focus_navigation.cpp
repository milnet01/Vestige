// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_ui_focus_navigation.cpp
/// @brief Phase 10.9 Slice 3 S4 — keyboard navigation + focus ring pins.
///
/// The footer label rendered by `menu_prefabs.cpp` advertises
/// "UP DOWN NAVIGATE / ENTER SELECT / ESC QUIT" but no handler is
/// wired — partially-sighted users and anyone running keyboard-only
/// cannot actually traverse the menu today. These tests exercise the
/// contract S4 introduces: Tab / arrow / Enter routing on
/// `UISystem`, `UIElement::focused` flag, modal-aware tab traversal,
/// and focus ring render-time opt-in for interactive widgets.
///
/// Key routing mapping (desktop convention):
///   * Tab       → focusNext()
///   * Shift+Tab → focusPrevious()
///   * Down, Right → focusNext()   (vertical menu convention)
///   * Up,   Left  → focusPrevious()
///   * Enter, Space → fires focused element's onClick signal
///
/// Modal trap: when a modal canvas is populated (post-`pushModalScreen`
/// or direct manipulation of `getModalCanvas`), tab order is pulled
/// from the modal canvas only — focus cannot escape to the root
/// canvas elements beneath it.

#include "systems/ui_system.h"
#include "ui/ui_button.h"
#include "ui/ui_panel.h"
#include "ui/sprite_batch_renderer.h"

#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <memory>

using namespace Vestige;

namespace
{

std::unique_ptr<UIButton> makeButton(const std::string& label, const UITheme& theme)
{
    auto btn = std::make_unique<UIButton>();
    btn->label = label;
    btn->theme = &theme;
    btn->interactive = true;
    return btn;
}

std::unique_ptr<UIPanel> makePanel()
{
    auto p = std::make_unique<UIPanel>();
    p->interactive = false;
    return p;
}

} // namespace

// ---------------------------------------------------------------------------
// UIElement::focused field — simple default + mutability.
// ---------------------------------------------------------------------------

TEST(UIElementFocus, FocusedFieldDefaultsFalse_S4)
{
    UIButton btn;
    EXPECT_FALSE(btn.focused);
}

TEST(UIElementFocus, FocusedFieldIsMutable_S4)
{
    UIButton btn;
    btn.focused = true;
    EXPECT_TRUE(btn.focused);
}

// ---------------------------------------------------------------------------
// UISystem: default focus state.
// ---------------------------------------------------------------------------

TEST(UISystemFocus, DefaultHasNoFocus_S4)
{
    UISystem sys;
    EXPECT_EQ(sys.getFocusedElement(), nullptr);
}

// ---------------------------------------------------------------------------
// Tab traversal over the root canvas.
// ---------------------------------------------------------------------------

TEST(UISystemFocus, TabAdvancesThroughInteractive_S4)
{
    UISystem sys;
    auto b1 = makeButton("A", sys.getTheme());
    auto b2 = makeButton("B", sys.getTheme());
    auto b3 = makeButton("C", sys.getTheme());
    UIButton* p1 = b1.get();
    UIButton* p2 = b2.get();
    UIButton* p3 = b3.get();
    sys.getCanvas().addElement(std::move(b1));
    sys.getCanvas().addElement(std::move(b2));
    sys.getCanvas().addElement(std::move(b3));

    EXPECT_TRUE(sys.handleKey(GLFW_KEY_TAB, 0));
    EXPECT_EQ(sys.getFocusedElement(), p1);

    EXPECT_TRUE(sys.handleKey(GLFW_KEY_TAB, 0));
    EXPECT_EQ(sys.getFocusedElement(), p2);

    EXPECT_TRUE(sys.handleKey(GLFW_KEY_TAB, 0));
    EXPECT_EQ(sys.getFocusedElement(), p3);
}

TEST(UISystemFocus, TabWrapsAtEnd_S4)
{
    UISystem sys;
    auto b1 = makeButton("A", sys.getTheme());
    auto b2 = makeButton("B", sys.getTheme());
    UIButton* p1 = b1.get();
    UIButton* p2 = b2.get();
    sys.getCanvas().addElement(std::move(b1));
    sys.getCanvas().addElement(std::move(b2));

    sys.handleKey(GLFW_KEY_TAB, 0);              // p1
    sys.handleKey(GLFW_KEY_TAB, 0);              // p2
    sys.handleKey(GLFW_KEY_TAB, 0);              // wrap to p1
    EXPECT_EQ(sys.getFocusedElement(), p1);
    (void)p2;
}

TEST(UISystemFocus, ShiftTabGoesBackward_S4)
{
    UISystem sys;
    auto b1 = makeButton("A", sys.getTheme());
    auto b2 = makeButton("B", sys.getTheme());
    UIButton* p1 = b1.get();
    UIButton* p2 = b2.get();
    sys.getCanvas().addElement(std::move(b1));
    sys.getCanvas().addElement(std::move(b2));

    sys.handleKey(GLFW_KEY_TAB, 0);                          // p1
    sys.handleKey(GLFW_KEY_TAB, 0);                          // p2
    sys.handleKey(GLFW_KEY_TAB, GLFW_MOD_SHIFT);             // back to p1
    EXPECT_EQ(sys.getFocusedElement(), p1);

    // From first element, Shift+Tab wraps to the last.
    sys.handleKey(GLFW_KEY_TAB, GLFW_MOD_SHIFT);
    EXPECT_EQ(sys.getFocusedElement(), p2);
}

TEST(UISystemFocus, TabSkipsNonInteractive_S4)
{
    UISystem sys;
    auto b1 = makeButton("A", sys.getTheme());
    auto panel = makePanel();
    auto b2 = makeButton("B", sys.getTheme());
    UIButton* p1 = b1.get();
    UIButton* p2 = b2.get();
    sys.getCanvas().addElement(std::move(b1));
    sys.getCanvas().addElement(std::move(panel));       // interactive=false
    sys.getCanvas().addElement(std::move(b2));

    sys.handleKey(GLFW_KEY_TAB, 0);
    EXPECT_EQ(sys.getFocusedElement(), p1);
    sys.handleKey(GLFW_KEY_TAB, 0);
    // The intervening non-interactive panel must be skipped.
    EXPECT_EQ(sys.getFocusedElement(), p2);
}

// ---------------------------------------------------------------------------
// Arrow keys mirror Tab.
// ---------------------------------------------------------------------------

TEST(UISystemFocus, ArrowDownIsTabForward_S4)
{
    UISystem sys;
    auto b1 = makeButton("A", sys.getTheme());
    auto b2 = makeButton("B", sys.getTheme());
    UIButton* p1 = b1.get();
    UIButton* p2 = b2.get();
    sys.getCanvas().addElement(std::move(b1));
    sys.getCanvas().addElement(std::move(b2));

    EXPECT_TRUE(sys.handleKey(GLFW_KEY_DOWN, 0));
    EXPECT_EQ(sys.getFocusedElement(), p1);
    EXPECT_TRUE(sys.handleKey(GLFW_KEY_DOWN, 0));
    EXPECT_EQ(sys.getFocusedElement(), p2);
}

TEST(UISystemFocus, ArrowUpIsTabBack_S4)
{
    UISystem sys;
    auto b1 = makeButton("A", sys.getTheme());
    auto b2 = makeButton("B", sys.getTheme());
    UIButton* p1 = b1.get();
    UIButton* p2 = b2.get();
    sys.getCanvas().addElement(std::move(b1));
    sys.getCanvas().addElement(std::move(b2));

    sys.handleKey(GLFW_KEY_DOWN, 0);
    sys.handleKey(GLFW_KEY_DOWN, 0);
    EXPECT_EQ(sys.getFocusedElement(), p2);
    EXPECT_TRUE(sys.handleKey(GLFW_KEY_UP, 0));
    EXPECT_EQ(sys.getFocusedElement(), p1);
}

// ---------------------------------------------------------------------------
// Enter / Space fires onClick on the focused button.
// ---------------------------------------------------------------------------

TEST(UISystemFocus, EnterFiresFocusedButtonOnClick_S4)
{
    UISystem sys;
    auto btn = makeButton("OK", sys.getTheme());
    UIButton* p = btn.get();
    int clickCount = 0;
    p->onClick.connect([&]() { ++clickCount; });
    sys.getCanvas().addElement(std::move(btn));

    sys.handleKey(GLFW_KEY_TAB, 0);
    EXPECT_EQ(sys.getFocusedElement(), p);

    EXPECT_TRUE(sys.handleKey(GLFW_KEY_ENTER, 0));
    EXPECT_EQ(clickCount, 1);
}

TEST(UISystemFocus, SpaceFiresFocusedButtonOnClick_S4)
{
    UISystem sys;
    auto btn = makeButton("Confirm", sys.getTheme());
    UIButton* p = btn.get();
    int clickCount = 0;
    p->onClick.connect([&]() { ++clickCount; });
    sys.getCanvas().addElement(std::move(btn));

    sys.handleKey(GLFW_KEY_TAB, 0);
    EXPECT_TRUE(sys.handleKey(GLFW_KEY_SPACE, 0));
    EXPECT_EQ(clickCount, 1);
}

TEST(UISystemFocus, EnterWithNoFocusDoesNothing_S4)
{
    UISystem sys;
    auto btn = makeButton("OK", sys.getTheme());
    UIButton* p = btn.get();
    int clickCount = 0;
    p->onClick.connect([&]() { ++clickCount; });
    sys.getCanvas().addElement(std::move(btn));

    // No Tab first — no focus.
    EXPECT_FALSE(sys.handleKey(GLFW_KEY_ENTER, 0));
    EXPECT_EQ(clickCount, 0);
    (void)p;
}

// ---------------------------------------------------------------------------
// Setting focus clears the old element's flag and sets the new one's.
// ---------------------------------------------------------------------------

TEST(UISystemFocus, AdvancingFocusUpdatesFocusedFlag_S4)
{
    UISystem sys;
    auto b1 = makeButton("A", sys.getTheme());
    auto b2 = makeButton("B", sys.getTheme());
    UIButton* p1 = b1.get();
    UIButton* p2 = b2.get();
    sys.getCanvas().addElement(std::move(b1));
    sys.getCanvas().addElement(std::move(b2));

    sys.handleKey(GLFW_KEY_TAB, 0);
    EXPECT_TRUE(p1->focused);
    EXPECT_FALSE(p2->focused);

    sys.handleKey(GLFW_KEY_TAB, 0);
    EXPECT_FALSE(p1->focused);
    EXPECT_TRUE(p2->focused);
}

// ---------------------------------------------------------------------------
// Modal canvas traps focus.
// ---------------------------------------------------------------------------

TEST(UISystemFocus, ModalCanvasTrapsFocus_S4)
{
    UISystem sys;

    // Root canvas has two buttons.
    auto rootA = makeButton("RootA", sys.getTheme());
    auto rootB = makeButton("RootB", sys.getTheme());
    UIButton* rootAp = rootA.get();
    sys.getCanvas().addElement(std::move(rootA));
    sys.getCanvas().addElement(std::move(rootB));

    // Modal has a single button. Tab must stay on the modal.
    auto modalA = makeButton("ModalA", sys.getTheme());
    UIButton* modalAp = modalA.get();
    sys.getModalCanvas().addElement(std::move(modalA));

    // Several tab presses — focus never escapes to the root canvas.
    for (int i = 0; i < 5; ++i)
    {
        sys.handleKey(GLFW_KEY_TAB, 0);
        EXPECT_EQ(sys.getFocusedElement(), modalAp);
        EXPECT_FALSE(rootAp->focused);
    }
    (void)rootAp;
}

// ---------------------------------------------------------------------------
// Key routing: non-navigation keys return false so game code still
// receives them.
// ---------------------------------------------------------------------------

TEST(UISystemFocus, HandleKeyReturnsFalseForUnhandledKey_S4)
{
    UISystem sys;
    auto btn = makeButton("A", sys.getTheme());
    sys.getCanvas().addElement(std::move(btn));

    EXPECT_FALSE(sys.handleKey(GLFW_KEY_A, 0));
    EXPECT_FALSE(sys.handleKey(GLFW_KEY_F5, 0));
}
