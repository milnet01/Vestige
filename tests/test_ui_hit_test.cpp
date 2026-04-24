// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_ui_hit_test.cpp
/// @brief Phase 10.9 Slice 3 S3 — UIElement::hitTest must recurse into
///        children; UICanvas::hitTest must walk nested trees.
///
/// Before S3, `UIElement::hitTest` checked only its own bounds and
/// returned — `m_children` was never visited. Any widget placed as a
/// child of another (tooltip inside panel, button inside group,
/// slider thumb inside track) was silently unreachable to mouse
/// input because `UICanvas::hitTest` only walks top-level elements
/// and relies on their `hitTest` to descend.
///
/// These tests pin the recursion contract: given a nested tree, a
/// point falling on a descendant must return true regardless of
/// where in the tree the descendant sits.

#include <gtest/gtest.h>

#include "ui/ui_canvas.h"
#include "ui/ui_element.h"

#include <memory>

using namespace Vestige;

namespace
{

/// Test-only minimal UIElement subclass. `UIElement::render` is
/// pure-virtual, so a concrete subclass is needed even if rendering
/// itself is not exercised here.
class BoxElement : public UIElement
{
public:
    void render(SpriteBatchRenderer& /*batch*/,
                const glm::vec2& /*parentOffset*/,
                int /*screenWidth*/,
                int /*screenHeight*/) override
    {
        // Not exercised in hit-test unit tests.
    }
};

std::unique_ptr<BoxElement> makeBox(glm::vec2 position,
                                     glm::vec2 size,
                                     bool interactive)
{
    auto box = std::make_unique<BoxElement>();
    box->position    = position;
    box->size        = size;
    box->anchor      = Anchor::TOP_LEFT;
    box->visible     = true;
    box->interactive = interactive;
    return box;
}

constexpr int W = 800;
constexpr int H = 600;

} // namespace

// ---------------------------------------------------------------------------
// Baseline — existing single-element behaviour must be preserved.
// ---------------------------------------------------------------------------

TEST(UIHitTest, RootInteractiveElementHitsPoint)
{
    auto box = makeBox({10.0f, 10.0f}, {100.0f, 100.0f}, /*interactive=*/true);
    EXPECT_TRUE(box->hitTest({50.0f, 50.0f}, glm::vec2(0.0f), W, H));
}

TEST(UIHitTest, RootInteractiveElementMissesPoint)
{
    auto box = makeBox({10.0f, 10.0f}, {100.0f, 100.0f}, /*interactive=*/true);
    EXPECT_FALSE(box->hitTest({500.0f, 500.0f}, glm::vec2(0.0f), W, H));
}

// ---------------------------------------------------------------------------
// Phase 10.9 Slice 3 S3 contract:
//   UIElement::hitTest recurses into m_children. A point that falls on
//   a descendant's bounds must return true from the root.
// ---------------------------------------------------------------------------

TEST(UIHitTest, NestedInteractiveChildIsReachableThroughInteractiveParent_S3)
{
    // Parent 100×100 at (0,0). Child 20×20 at parent-local (40,40) →
    // absolute (40,40).
    auto parent = makeBox({0.0f, 0.0f}, {100.0f, 100.0f}, /*interactive=*/true);
    parent->addChild(makeBox({40.0f, 40.0f}, {20.0f, 20.0f}, /*interactive=*/true));

    // Hit on child bounds — today the test fails because the parent
    // returns true on its own bounds (hiding the bug). Hit a point
    // outside the parent to prove the child is actually being
    // descended into... wait, the child is inside the parent, so any
    // point on the child is also on the parent. We need a construct
    // where the child overflows the parent.
    auto overflowParent = makeBox({0.0f, 0.0f}, {10.0f, 10.0f}, /*interactive=*/true);
    overflowParent->addChild(makeBox({50.0f, 50.0f}, {20.0f, 20.0f}, /*interactive=*/true));

    // Point (60, 60) is outside the 10×10 parent but inside the
    // 20×20 child at (50, 50). Pre-S3 this returns false because
    // UIElement::hitTest short-circuits on !self-bounds without
    // visiting children.
    EXPECT_TRUE(overflowParent->hitTest({60.0f, 60.0f}, glm::vec2(0.0f), W, H))
        << "child is reachable only if hitTest recurses into m_children";
}

TEST(UIHitTest, NestedInteractiveChildIsReachableThroughNonInteractiveParent_S3)
{
    // A non-interactive container (typical UIPanel use case) must not
    // swallow the input that a child wants to catch. Parent at (0,0)
    // 100×100 non-interactive, child at (50,50) 20×20 interactive.
    auto parent = makeBox({0.0f, 0.0f}, {100.0f, 100.0f}, /*interactive=*/false);
    parent->addChild(makeBox({50.0f, 50.0f}, {20.0f, 20.0f}, /*interactive=*/true));

    // Point (55, 55) sits inside the child. Pre-S3 the root returns
    // false immediately because its own `!interactive` guard fires.
    EXPECT_TRUE(parent->hitTest({55.0f, 55.0f}, glm::vec2(0.0f), W, H))
        << "non-interactive container must still let its interactive "
           "child catch the hit";
}

TEST(UIHitTest, HiddenSubtreeSkipsEntireTree_S3)
{
    // `visible = false` on the parent must suppress the subtree
    // wholesale — a child should not catch input that the sighted
    // user cannot see a parent for.
    auto parent = makeBox({0.0f, 0.0f}, {100.0f, 100.0f}, /*interactive=*/false);
    parent->visible = false;
    parent->addChild(makeBox({50.0f, 50.0f}, {20.0f, 20.0f}, /*interactive=*/true));

    EXPECT_FALSE(parent->hitTest({55.0f, 55.0f}, glm::vec2(0.0f), W, H));
}

TEST(UIHitTest, DeeplyNestedChildIsReachable_S3)
{
    // Three levels deep: outer (0,0) 10×10 non-interactive, mid
    // (50,50) 10×10 non-interactive, inner (100,100) 20×20 interactive.
    // Absolute inner position = (0+50+100, 0+50+100) = (150, 150).
    auto outer = makeBox({0.0f, 0.0f}, {10.0f, 10.0f}, /*interactive=*/false);
    auto mid   = makeBox({50.0f, 50.0f}, {10.0f, 10.0f}, /*interactive=*/false);
    auto inner = makeBox({100.0f, 100.0f}, {20.0f, 20.0f}, /*interactive=*/true);
    mid->addChild(std::move(inner));
    outer->addChild(std::move(mid));

    EXPECT_TRUE(outer->hitTest({160.0f, 160.0f}, glm::vec2(0.0f), W, H))
        << "recursion must propagate the parent offset down the tree";
}

// ---------------------------------------------------------------------------
// UICanvas::hitTest walks nested trees through the recursion above.
// ---------------------------------------------------------------------------

TEST(UICanvasHitTest, PointOnNestedChildReturnsTrue_S3)
{
    UICanvas canvas;
    auto container = makeBox({0.0f, 0.0f}, {10.0f, 10.0f}, /*interactive=*/false);
    container->addChild(makeBox({50.0f, 50.0f}, {20.0f, 20.0f}, /*interactive=*/true));
    canvas.addElement(std::move(container));

    EXPECT_TRUE(canvas.hitTest({60.0f, 60.0f}, W, H))
        << "canvas hit-test must reach nested children via "
           "UIElement::hitTest recursion";
}

TEST(UICanvasHitTest, PointOutsideAllElementsReturnsFalse_S3)
{
    UICanvas canvas;
    canvas.addElement(makeBox({0.0f, 0.0f}, {10.0f, 10.0f}, /*interactive=*/true));

    EXPECT_FALSE(canvas.hitTest({500.0f, 500.0f}, W, H));
}
