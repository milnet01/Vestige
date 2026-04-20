// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_formula_node_editor_panel.cpp
/// @brief Headless tests for the Phase 9E FormulaNodeEditorPanel — covers
/// the sampler pipeline (graph → ExprNode → evaluated curve) and the
/// template-loading round-trip. UI rendering is not exercised (no ImGui
/// context), which is why the sampler + `findOutputNodeId` live in their
/// own translation unit (see `formula_curve_sampler.cpp`).
///
/// These tests guard three roadmap behaviours in Phase 9E (Formula Node
/// Editor section):
///   1. Drag/click-loading a PhysicsTemplates entry produces a non-trivial
///      NodeGraph.
///   2. The output-node curve preview evaluates correctly over a sweep of
///      the first input variable (monotonic for monotonic templates).
///   3. Broken inputs (empty graph, no output node, undeclared variable)
///      never throw — they surface as `FormulaCurveSample::error`.
#include "formula_node_editor_panel.h"

#include "formula/node_graph.h"
#include "formula/physics_templates.h"

#include <gtest/gtest.h>

using namespace Vestige;

namespace
{

/// @brief Build a NodeGraph for a template by its factory's `name` field.
NodeGraph graphForTemplate(const std::string& templateName)
{
    auto all = PhysicsTemplates::createAll();
    for (const auto& def : all)
    {
        if (def.name == templateName)
        {
            const ExprNode* expr = def.getExpression(QualityTier::FULL);
            if (!expr) return {};
            return NodeGraph::fromExpressionTree(*expr);
        }
    }
    return {};
}

std::unordered_map<std::string, float>
bindingsFor(const std::string& templateName)
{
    std::unordered_map<std::string, float> out;
    auto all = PhysicsTemplates::createAll();
    for (const auto& def : all)
    {
        if (def.name == templateName)
        {
            for (const auto& in : def.inputs)
            {
                out[in.name] = in.defaultValue;
            }
            for (const auto& [name, value] : def.coefficients)
            {
                out[name] = value;
            }
            break;
        }
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// findOutputNodeId
// ---------------------------------------------------------------------------

TEST(FormulaNodeEditorSampler, OutputNodeIsPresentAfterFromExpressionTree)
{
    NodeGraph g = graphForTemplate("ease_in_sine");
    ASSERT_GT(g.nodeCount(), 1u)
        << "ease_in_sine should produce >1 node via fromExpressionTree";
    EXPECT_NE(findOutputNodeId(g), 0u);
}

TEST(FormulaNodeEditorSampler, EmptyGraphHasNoOutputNode)
{
    NodeGraph g;
    EXPECT_EQ(findOutputNodeId(g), 0u);
}

// ---------------------------------------------------------------------------
// sampleFormulaCurve — behaviour
// ---------------------------------------------------------------------------

TEST(FormulaNodeEditorSampler, EmptyGraphReportsError)
{
    NodeGraph g;
    auto sample = sampleFormulaCurve(g, "", 0.0f, 1.0f, 16, {});
    EXPECT_TRUE(sample.xs.empty());
    EXPECT_TRUE(sample.ys.empty());
    EXPECT_FALSE(sample.error.empty());
}

TEST(FormulaNodeEditorSampler, MissingOutputNodeReportsError)
{
    // Build a graph with a single variable node but no output node — this
    // is an unusual state (fromExpressionTree always adds one), but tests
    // the error path for code that mutates the graph directly.
    NodeGraph g;
    g.addNode(NodeGraph::createVariableNode("x"));

    auto sample = sampleFormulaCurve(g, "", 0.0f, 1.0f, 16, {});
    EXPECT_TRUE(sample.xs.empty());
    EXPECT_FALSE(sample.error.empty());
}

TEST(FormulaNodeEditorSampler, EaseInSineProducesMonotonicCurve)
{
    // ease_in_sine is 1 - cos(x * pi/2). Over x in [0,1], dy/dx >= 0 and
    // y(0) = 0, y(1) = 1 — a reliable monotonicity check.
    NodeGraph g = graphForTemplate("ease_in_sine");
    ASSERT_GT(g.nodeCount(), 0u);

    const auto binds = bindingsFor("ease_in_sine");
    auto sample = sampleFormulaCurve(g, "", 0.0f, 1.0f, 32, binds);

    ASSERT_TRUE(sample.error.empty()) << sample.error;
    ASSERT_EQ(sample.xs.size(), 32u);
    ASSERT_EQ(sample.ys.size(), 32u);

    // First and last samples match the analytical endpoints.
    EXPECT_NEAR(sample.ys.front(), 0.0f, 1e-4f);
    EXPECT_NEAR(sample.ys.back(),  1.0f, 1e-4f);

    // Monotone non-decreasing.
    for (std::size_t i = 1; i < sample.ys.size(); ++i)
    {
        EXPECT_GE(sample.ys[i], sample.ys[i - 1] - 1e-5f)
            << "ease_in_sine non-monotonic at i=" << i
            << " (" << sample.ys[i - 1] << " → " << sample.ys[i] << ")";
    }
}

TEST(FormulaNodeEditorSampler, AutoPicksFirstVariableWhenNoneRequested)
{
    // aerodynamic_drag has three inputs; first-occurrence order in the
    // expression tree should pick Cd (coefficient used as leftmost
    // variable in the multiplicative chain in physics_templates.cpp).
    NodeGraph g = graphForTemplate("aerodynamic_drag");
    ASSERT_GT(g.nodeCount(), 0u);

    const auto binds = bindingsFor("aerodynamic_drag");
    auto sample = sampleFormulaCurve(g, "", 0.0f, 2.0f, 8, binds);

    ASSERT_TRUE(sample.error.empty()) << sample.error;
    EXPECT_FALSE(sample.sweepVariable.empty());
}

TEST(FormulaNodeEditorSampler, HonoursExplicitSweepVariable)
{
    NodeGraph g = graphForTemplate("aerodynamic_drag");
    ASSERT_GT(g.nodeCount(), 0u);

    const auto binds = bindingsFor("aerodynamic_drag");
    // Sweep vDotN (an actual input, not a coefficient) and confirm the
    // sampler reports the exact name back.
    auto sample = sampleFormulaCurve(g, "vDotN", 0.0f, 10.0f, 8, binds);

    ASSERT_TRUE(sample.error.empty()) << sample.error;
    EXPECT_EQ(sample.sweepVariable, "vDotN");

    // F = 0.5 * Cd * rho * A * vDotN → y is linear in vDotN. Check linearity.
    ASSERT_EQ(sample.ys.size(), 8u);
    const float slope = (sample.ys.back() - sample.ys.front()) /
                        (sample.xs.back() - sample.xs.front());
    for (std::size_t i = 0; i < sample.ys.size(); ++i)
    {
        const float expected = sample.ys.front() +
                               slope * (sample.xs[i] - sample.xs.front());
        EXPECT_NEAR(sample.ys[i], expected, 1e-3f);
    }
}

TEST(FormulaNodeEditorSampler, UnknownSweepVariableFallsBackToAuto)
{
    // An explicit sweep variable that doesn't appear in the tree should
    // silently fall back to auto-picking the first variable rather than
    // erroring — the UI's sweep combo can hold a stale selection from a
    // previous template.
    NodeGraph g = graphForTemplate("ease_in_sine");
    ASSERT_GT(g.nodeCount(), 0u);

    const auto binds = bindingsFor("ease_in_sine");
    auto sample = sampleFormulaCurve(g, "nonexistent_var",
                                     0.0f, 1.0f, 8, binds);

    ASSERT_TRUE(sample.error.empty()) << sample.error;
    EXPECT_FALSE(sample.sweepVariable.empty());
    EXPECT_NE(sample.sweepVariable, "nonexistent_var");
}

TEST(FormulaNodeEditorSampler, SampleCountIsClamped)
{
    NodeGraph g = graphForTemplate("ease_in_sine");
    ASSERT_GT(g.nodeCount(), 0u);

    const auto binds = bindingsFor("ease_in_sine");

    // 0 samples → clamped to minimum 2.
    auto tiny = sampleFormulaCurve(g, "", 0.0f, 1.0f, 0, binds);
    EXPECT_EQ(tiny.xs.size(), 2u);

    // Huge sample count → clamped to 4096 upper bound.
    auto huge = sampleFormulaCurve(g, "", 0.0f, 1.0f, 100000, binds);
    EXPECT_EQ(huge.xs.size(), 4096u);
}

TEST(FormulaNodeEditorSampler, UnboundVariableFallsBackToZero)
{
    // Skip the `bindings` map entirely — variables the evaluator needs
    // but aren't provided must default to 0 instead of throwing. Without
    // this guarantee the panel would flash errors on every template that
    // has coefficients the user hasn't populated yet.
    NodeGraph g = graphForTemplate("ease_in_sine");
    ASSERT_GT(g.nodeCount(), 0u);

    auto sample = sampleFormulaCurve(g, "", 0.0f, 1.0f, 8, {});

    EXPECT_TRUE(sample.error.empty()) << sample.error;
    EXPECT_EQ(sample.xs.size(), 8u);
}

// ---------------------------------------------------------------------------
// Panel state — loadTemplate / access
// ---------------------------------------------------------------------------

TEST(FormulaNodeEditorPanelState, LoadTemplatePopulatesGraphAndInputs)
{
    FormulaNodeEditorPanel panel;  // No initialize() — widget stays dormant.

    ASSERT_TRUE(panel.loadTemplate("ease_in_sine"));
    EXPECT_EQ(panel.loadedTemplate(), "ease_in_sine");
    EXPECT_GT(panel.graph().nodeCount(), 1u);
    EXPECT_FALSE(panel.inputs().empty());
    EXPECT_NE(findOutputNodeId(panel.graph()), 0u);

    // recomputePreview should have run during loadTemplate and left a
    // valid curve for the default sweep range.
    const auto& sample = panel.previewSample();
    EXPECT_TRUE(sample.error.empty()) << sample.error;
    EXPECT_FALSE(sample.xs.empty());
}

TEST(FormulaNodeEditorPanelState, LoadUnknownTemplateReturnsFalse)
{
    FormulaNodeEditorPanel panel;
    EXPECT_FALSE(panel.loadTemplate("definitely_not_a_template"));
    EXPECT_TRUE(panel.loadedTemplate().empty());
    EXPECT_EQ(panel.graph().nodeCount(), 0u);
}

TEST(FormulaNodeEditorPanelState, SweepRangeUpdatePropagatesToPreview)
{
    FormulaNodeEditorPanel panel;
    ASSERT_TRUE(panel.loadTemplate("ease_in_sine"));

    panel.setSweepRange(-2.0f, 3.0f);
    panel.recomputePreview();

    const auto& sample = panel.previewSample();
    ASSERT_FALSE(sample.xs.empty());
    EXPECT_FLOAT_EQ(sample.xs.front(), -2.0f);
    EXPECT_FLOAT_EQ(sample.xs.back(),   3.0f);
}
