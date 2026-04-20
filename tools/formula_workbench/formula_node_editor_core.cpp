// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file formula_node_editor_core.cpp
/// @brief Headless core of the FormulaNodeEditorPanel (Phase 9E).
///
/// Holds the graph → ExprNode → sampled-curve pipeline **and** the panel
/// methods that don't depend on ImGui / ImPlot (constructor, initialize,
/// shutdown, loadTemplate, recomputePreview). The ImGui-dependent draw
/// methods live in `formula_node_editor_panel.cpp`. Split this way so
/// unit tests can link the panel class and drive its state without
/// pulling in ImGui / ImPlot / imgui-node-editor — the test target links
/// only the engine library + this one translation unit.
#include "formula_node_editor_panel.h"

#include "core/logger.h"
#include "formula/expression_eval.h"
#include "formula/physics_templates.h"

#include <algorithm>
#include <exception>

namespace Vestige
{

namespace
{

/// @brief Cached list of every PhysicsTemplates definition.
///
/// Built once per process on first access — createAll() allocates
/// expression trees for every entry, and designers can click through
/// templates rapidly, so caching keeps loadTemplate() amortised to a
/// linear-by-name scan instead of rebuilding the whole catalog.
const std::vector<FormulaDefinition>& templateCatalogCached()
{
    static const std::vector<FormulaDefinition> cache = []
    {
        return PhysicsTemplates::createAll();
    }();
    return cache;
}

const FormulaDefinition* findTemplateByName(const std::string& name)
{
    for (const auto& def : templateCatalogCached())
    {
        if (def.name == name)
        {
            return &def;
        }
    }
    return nullptr;
}

} // namespace

NodeId findOutputNodeId(const NodeGraph& graph)
{
    for (const auto& [id, node] : graph.getNodes())
    {
        if (node.category == NodeCategory::OUTPUT ||
            node.operation == "output")
        {
            return id;
        }
    }
    return 0;
}

FormulaCurveSample sampleFormulaCurve(
    const NodeGraph& graph,
    const std::string& sweepVariable,
    float sweepMin,
    float sweepMax,
    std::size_t sampleCount,
    const std::unordered_map<std::string, float>& bindings)
{
    FormulaCurveSample out;

    if (graph.nodeCount() == 0)
    {
        out.error = "Graph is empty — load a template or build a graph first.";
        return out;
    }

    const NodeId outId = findOutputNodeId(graph);
    if (outId == 0)
    {
        out.error = "Graph has no OUTPUT node.";
        return out;
    }

    // Convert to expression tree. toExpressionTree returns nullptr if the
    // graph is malformed (cycles, missing connections).
    auto expr = graph.toExpressionTree(outId);
    if (!expr)
    {
        out.error = "Graph failed to convert to an expression tree "
                    "(disconnected output or cycle).";
        return out;
    }

    // Collect variables referenced by the tree, preserving first-occurrence
    // order so the auto-pick sweep variable is stable.
    std::vector<std::string> usedVars;
    expr->collectVariables(usedVars);

    std::vector<std::string> uniqVars;
    uniqVars.reserve(usedVars.size());
    for (const auto& name : usedVars)
    {
        if (std::find(uniqVars.begin(), uniqVars.end(), name) == uniqVars.end())
        {
            uniqVars.push_back(name);
        }
    }

    if (uniqVars.empty())
    {
        // Graph is a constant — sample once and fan out to a flat line so
        // the UI still shows something meaningful.
        out.sweepVariable.clear();
        const std::size_t n = std::clamp<std::size_t>(sampleCount, 2, 4096);
        out.xs.reserve(n);
        out.ys.reserve(n);
        try
        {
            ExpressionEvaluator eval;
            const float y = eval.evaluate(*expr, {});
            for (std::size_t i = 0; i < n; ++i)
            {
                const float t = static_cast<float>(i) /
                                static_cast<float>(n - 1);
                out.xs.push_back(sweepMin + (sweepMax - sweepMin) * t);
                out.ys.push_back(y);
            }
        }
        catch (const std::exception& ex)
        {
            out.xs.clear();
            out.ys.clear();
            out.error = std::string("Evaluator error: ") + ex.what();
        }
        return out;
    }

    // Choose sweep variable: explicit request if valid, else first discovered.
    std::string chosen = sweepVariable;
    if (chosen.empty() ||
        std::find(uniqVars.begin(), uniqVars.end(), chosen) == uniqVars.end())
    {
        chosen = uniqVars.front();
    }
    out.sweepVariable = chosen;

    ExpressionEvaluator::VariableMap vars;
    vars.reserve(uniqVars.size());
    for (const auto& name : uniqVars)
    {
        auto it = bindings.find(name);
        vars[name] = (it != bindings.end()) ? it->second : 0.0f;
    }

    const std::size_t n = std::clamp<std::size_t>(sampleCount, 2, 4096);
    out.xs.reserve(n);
    out.ys.reserve(n);

    ExpressionEvaluator eval;
    try
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            const float t = static_cast<float>(i) /
                            static_cast<float>(n - 1);
            const float x = sweepMin + (sweepMax - sweepMin) * t;
            vars[chosen] = x;
            const float y = eval.evaluate(*expr, vars);
            out.xs.push_back(x);
            out.ys.push_back(y);
        }
    }
    catch (const std::exception& ex)
    {
        out.xs.clear();
        out.ys.clear();
        out.error = std::string("Evaluator error: ") + ex.what();
    }

    return out;
}

// ---------------------------------------------------------------------------
// Panel — state-only methods (no ImGui / ImPlot dependency).
//
// draw / drawCatalog / drawCanvas / drawPreviewControls / renderGraph live
// in formula_node_editor_panel.cpp so the unit-test target can link the
// class without the full UI stack.
// ---------------------------------------------------------------------------

FormulaNodeEditorPanel::FormulaNodeEditorPanel() = default;

void FormulaNodeEditorPanel::initialize(const std::string& settingsFile)
{
    m_widget.initialize(settingsFile);
}

void FormulaNodeEditorPanel::shutdown()
{
    m_widget.shutdown();
}

bool FormulaNodeEditorPanel::loadTemplate(const std::string& templateName)
{
    const FormulaDefinition* def = findTemplateByName(templateName);
    if (!def)
    {
        Logger::warning("[FormulaNodeEditor] Unknown template: " + templateName);
        return false;
    }

    const ExprNode* expr = def->getExpression(QualityTier::FULL);
    if (!expr)
    {
        Logger::warning("[FormulaNodeEditor] Template has no FULL expression: "
                        + templateName);
        return false;
    }

    m_graph = NodeGraph::fromExpressionTree(*expr);
    m_inputs = def->inputs;
    m_coefficients.clear();
    m_coefficients.reserve(def->coefficients.size());
    for (const auto& [name, value] : def->coefficients)
    {
        m_coefficients.emplace(name, value);
    }
    m_loadedTemplateName = templateName;

    // Reset sweep to the template's first input so the preview picks up a
    // meaningful X axis immediately after loading.
    m_sweepVariable.clear();
    if (!m_inputs.empty())
    {
        m_sweepVariable = m_inputs.front().name;
    }

    recomputePreview();
    return true;
}

void FormulaNodeEditorPanel::recomputePreview()
{
    std::unordered_map<std::string, float> bindings = m_coefficients;
    for (const auto& in : m_inputs)
    {
        // Input defaults override coefficient-name collisions — matches
        // ExpressionEvaluator's (vars, coeffs) precedence.
        bindings[in.name] = in.defaultValue;
    }

    m_preview = sampleFormulaCurve(m_graph, m_sweepVariable,
                                   m_sweepMin, m_sweepMax,
                                   static_cast<std::size_t>(m_sampleCount),
                                   bindings);
}

} // namespace Vestige
