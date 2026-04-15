// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_selection.cpp
/// @brief Unit tests for the Selection class (editor entity selection management).
#include "editor/selection.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace Vestige;

TEST(SelectionTest, DefaultStateIsEmpty)
{
    Selection sel;
    EXPECT_FALSE(sel.hasSelection());
    EXPECT_TRUE(sel.getSelectedIds().empty());
    EXPECT_EQ(sel.getPrimaryId(), 0u);
}

TEST(SelectionTest, SelectSingleEntity)
{
    Selection sel;
    sel.select(42);

    EXPECT_TRUE(sel.hasSelection());
    EXPECT_EQ(sel.getSelectedIds().size(), 1u);
    EXPECT_EQ(sel.getPrimaryId(), 42u);
    EXPECT_TRUE(sel.isSelected(42));
}

TEST(SelectionTest, SelectReplacesExisting)
{
    Selection sel;
    sel.select(1);
    sel.select(2);

    EXPECT_EQ(sel.getSelectedIds().size(), 1u);
    EXPECT_FALSE(sel.isSelected(1));
    EXPECT_TRUE(sel.isSelected(2));
    EXPECT_EQ(sel.getPrimaryId(), 2u);
}

TEST(SelectionTest, SelectZeroClearsSelection)
{
    Selection sel;
    sel.select(10);
    EXPECT_TRUE(sel.hasSelection());

    sel.select(0);
    EXPECT_FALSE(sel.hasSelection());
    EXPECT_TRUE(sel.getSelectedIds().empty());
    EXPECT_EQ(sel.getPrimaryId(), 0u);
}

TEST(SelectionTest, AddToSelectionMultipleEntities)
{
    Selection sel;
    sel.select(1);
    sel.addToSelection(2);
    sel.addToSelection(3);

    EXPECT_EQ(sel.getSelectedIds().size(), 3u);
    EXPECT_TRUE(sel.isSelected(1));
    EXPECT_TRUE(sel.isSelected(2));
    EXPECT_TRUE(sel.isSelected(3));
}

TEST(SelectionTest, AddToSelectionIgnoresDuplicate)
{
    Selection sel;
    sel.select(5);
    sel.addToSelection(5);

    EXPECT_EQ(sel.getSelectedIds().size(), 1u);
}

TEST(SelectionTest, AddToSelectionIgnoresZero)
{
    Selection sel;
    sel.addToSelection(0);

    EXPECT_FALSE(sel.hasSelection());
    EXPECT_TRUE(sel.getSelectedIds().empty());
}

TEST(SelectionTest, ToggleAddsWhenNotSelected)
{
    Selection sel;
    sel.toggleSelection(7);

    EXPECT_TRUE(sel.isSelected(7));
    EXPECT_EQ(sel.getSelectedIds().size(), 1u);
}

TEST(SelectionTest, ToggleRemovesWhenAlreadySelected)
{
    Selection sel;
    sel.select(7);
    EXPECT_TRUE(sel.isSelected(7));

    sel.toggleSelection(7);
    EXPECT_FALSE(sel.isSelected(7));
    EXPECT_FALSE(sel.hasSelection());
}

TEST(SelectionTest, ToggleWithMultipleEntities)
{
    Selection sel;
    sel.select(1);
    sel.addToSelection(2);
    sel.addToSelection(3);

    // Toggle off entity 2
    sel.toggleSelection(2);
    EXPECT_FALSE(sel.isSelected(2));
    EXPECT_EQ(sel.getSelectedIds().size(), 2u);
    EXPECT_TRUE(sel.isSelected(1));
    EXPECT_TRUE(sel.isSelected(3));

    // Toggle entity 2 back on
    sel.toggleSelection(2);
    EXPECT_TRUE(sel.isSelected(2));
    EXPECT_EQ(sel.getSelectedIds().size(), 3u);
}

TEST(SelectionTest, ToggleIgnoresZero)
{
    Selection sel;
    sel.select(1);
    sel.toggleSelection(0);

    EXPECT_EQ(sel.getSelectedIds().size(), 1u);
    EXPECT_TRUE(sel.isSelected(1));
}

TEST(SelectionTest, ClearSelection)
{
    Selection sel;
    sel.select(1);
    sel.addToSelection(2);
    sel.addToSelection(3);
    EXPECT_EQ(sel.getSelectedIds().size(), 3u);

    sel.clearSelection();
    EXPECT_FALSE(sel.hasSelection());
    EXPECT_TRUE(sel.getSelectedIds().empty());
    EXPECT_EQ(sel.getPrimaryId(), 0u);
}

TEST(SelectionTest, ClearOnAlreadyEmptyIsHarmless)
{
    Selection sel;
    sel.clearSelection();
    EXPECT_FALSE(sel.hasSelection());
}

TEST(SelectionTest, IsSelectedReturnsFalseForUnselected)
{
    Selection sel;
    sel.select(10);

    EXPECT_FALSE(sel.isSelected(11));
    EXPECT_FALSE(sel.isSelected(0));
    EXPECT_FALSE(sel.isSelected(999));
}

TEST(SelectionTest, GetSelectedIdsReturnsCorrectList)
{
    Selection sel;
    sel.select(10);
    sel.addToSelection(20);
    sel.addToSelection(30);

    const auto& ids = sel.getSelectedIds();
    ASSERT_EQ(ids.size(), 3u);

    // Verify all expected IDs are present
    EXPECT_NE(std::find(ids.begin(), ids.end(), 10u), ids.end());
    EXPECT_NE(std::find(ids.begin(), ids.end(), 20u), ids.end());
    EXPECT_NE(std::find(ids.begin(), ids.end(), 30u), ids.end());
}

TEST(SelectionTest, PrimaryIdIsMostRecentlyAdded)
{
    Selection sel;
    sel.select(1);
    EXPECT_EQ(sel.getPrimaryId(), 1u);

    sel.addToSelection(2);
    EXPECT_EQ(sel.getPrimaryId(), 2u);

    sel.addToSelection(3);
    EXPECT_EQ(sel.getPrimaryId(), 3u);
}

TEST(SelectionTest, PrimaryIdAfterToggleOffLast)
{
    Selection sel;
    sel.select(1);
    sel.addToSelection(2);

    // Toggle off the last (primary) entity
    sel.toggleSelection(2);

    // Primary should now be the remaining back element
    EXPECT_EQ(sel.getPrimaryId(), 1u);
}

TEST(SelectionTest, SelectAfterClearWorks)
{
    Selection sel;
    sel.select(1);
    sel.clearSelection();
    sel.select(42);

    EXPECT_TRUE(sel.hasSelection());
    EXPECT_EQ(sel.getPrimaryId(), 42u);
    EXPECT_EQ(sel.getSelectedIds().size(), 1u);
}
