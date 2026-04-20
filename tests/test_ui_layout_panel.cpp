// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_ui_layout_panel.cpp
/// @brief CPU-side tests for the UI layout panel state + canvas accessor.
///
/// The panel's ImGui draw() requires a live ImGui context (editor runtime),
/// so these tests cover the non-ImGui state: default visibility, toggle
/// behaviour, and the new `UICanvas::getElementAt()` accessor that backs
/// the panel's element list.

#include <gtest/gtest.h>

#include "editor/panels/ui_layout_panel.h"
#include "ui/ui_canvas.h"
#include "ui/ui_panel.h"

#include <memory>

using namespace Vestige;

TEST(UILayoutPanel, DefaultsToClosed)
{
    UILayoutPanel panel;
    EXPECT_FALSE(panel.isOpen());
}

TEST(UILayoutPanel, SetOpenToggles)
{
    UILayoutPanel panel;
    panel.setOpen(true);
    EXPECT_TRUE(panel.isOpen());
    panel.setOpen(false);
    EXPECT_FALSE(panel.isOpen());
}

TEST(UICanvasAccessor, GetElementAtReturnsNullWhenEmpty)
{
    UICanvas canvas;
    EXPECT_EQ(canvas.getElementAt(0), nullptr);
    EXPECT_EQ(canvas.getElementAt(99), nullptr);
}

TEST(UICanvasAccessor, GetElementAtReturnsAddedElements)
{
    UICanvas canvas;
    auto p1 = std::make_unique<UIPanel>();
    UIPanel* p1Raw = p1.get();
    auto p2 = std::make_unique<UIPanel>();
    UIPanel* p2Raw = p2.get();
    canvas.addElement(std::move(p1));
    canvas.addElement(std::move(p2));

    EXPECT_EQ(canvas.getElementAt(0), p1Raw);
    EXPECT_EQ(canvas.getElementAt(1), p2Raw);
    EXPECT_EQ(canvas.getElementAt(2), nullptr);
}

TEST(UICanvasAccessor, GetElementAtConstOverload)
{
    UICanvas canvas;
    canvas.addElement(std::make_unique<UIPanel>());
    const UICanvas& cref = canvas;
    const UIElement* e = cref.getElementAt(0);
    EXPECT_NE(e, nullptr);
}
