// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file formula_node_editor_panel.h
/// @brief Visual composition UI for NodeGraph-based formulas (Phase 9E).
///
/// Closes three roadmap items under "Formula Node Editor" in Phase 9E:
///   - Output node with real-time curve preview (ImPlot inline)
///   - Drag-and-drop from PhysicsTemplates catalog into the node graph
///   - Visual formula composition UI (ImGui node editor rendering)
///
/// The panel is tool-side (FormulaWorkbench) — it does not ship in the engine
/// runtime. The graph edit loop reuses `NodeEditorWidget` (engine/editor/
/// widgets) so the wrapping around imgui-node-editor is shared with
/// `ScriptEditorPanel`.
///
/// A headless helper `sampleFormulaCurve()` produces the (x, y) arrays that
/// back the preview; tests exercise it without an ImGui / ImPlot context.
#pragma once

#include "editor/widgets/node_editor_widget.h"
#include "formula/formula.h"
#include "formula/node_graph.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vestige
{

/// @brief Result of a headless graph → curve sampling operation.
///
/// `xs` / `ys` are empty iff `error` is non-empty. Callers treat the presence
/// of `error` as "draw a warning instead of a line".
struct FormulaCurveSample
{
    std::vector<float> xs;
    std::vector<float> ys;
    std::string sweepVariable;  ///< Variable name plotted on the X axis.
    std::string error;          ///< Human-readable failure reason; empty on success.
};

/// @brief Sweep the output of a NodeGraph across one variable's range.
///
/// Resolves the graph's output node (the unique node with `operation ==
/// "output"`), converts to an ExprNode via `NodeGraph::toExpressionTree()`,
/// then evaluates at `sampleCount` points with `sweepVariable` interpolated
/// over [sweepMin, sweepMax]. Other variables referenced by the tree are
/// bound from `inputDefaults` (by name) or fall back to 0.0f.
///
/// Exceptions from `ExpressionEvaluator` are swallowed and reported via
/// `FormulaCurveSample::error` so the UI can render a warning without
/// aborting the frame.
///
/// @param graph          Graph to evaluate.
/// @param sweepVariable  Name of the variable to sweep on the X axis. If
///                       empty, the first variable discovered in the
///                       expression tree is chosen; if the tree references
///                       no variables, returns an error.
/// @param sweepMin       Start of the sweep range (inclusive).
/// @param sweepMax       End of the sweep range (inclusive).
/// @param sampleCount    Number of samples; clamped to [2, 4096].
/// @param bindings       Per-variable fallback values (union of the
///                       template's input defaults and coefficient values).
///                       Variables not present fall back to 0.0f.
/// @return Populated `FormulaCurveSample`. On failure, `error` describes the
///         cause and `xs`/`ys` are empty.
FormulaCurveSample sampleFormulaCurve(const NodeGraph& graph,
                                      const std::string& sweepVariable,
                                      float sweepMin,
                                      float sweepMax,
                                      std::size_t sampleCount,
                                      const std::unordered_map<std::string, float>& bindings);

/// @brief Locate the single OUTPUT-category node in a graph.
///
/// The converter `NodeGraph::fromExpressionTree` always creates exactly one,
/// so tests and the panel both rely on this invariant. Returns 0 if none
/// exists.
NodeId findOutputNodeId(const NodeGraph& graph);

/// @brief Dockable panel that owns one NodeGraph + one NodeEditorWidget.
///
/// Usage (owner drives render each frame):
/// @code
///   FormulaNodeEditorPanel panel;
///   panel.initialize();           // after ImGui context exists
///   panel.open();
///   // ... each frame ...
///   panel.draw();
///   // ... on shutdown (BEFORE ImGui::DestroyContext) ...
///   panel.shutdown();
/// @endcode
class FormulaNodeEditorPanel
{
public:
    FormulaNodeEditorPanel();

    /// @brief Create the node-editor context. Requires a live ImGui context.
    /// @param settingsFile Optional file the library uses to persist pan /
    ///   zoom / node positions.
    void initialize(const std::string& settingsFile = {});

    /// @brief Destroy the node-editor context. Safe to call multiple times.
    /// MUST be called before `ImGui::DestroyContext()` (see NodeEditorWidget).
    void shutdown();

    void open() { m_open = true; }
    void close() { m_open = false; }
    bool isOpen() const { return m_open; }

    /// @brief Render one frame. Safe to call while closed (no-op).
    void draw();

    /// @brief Replace the current graph with the expression tree of the
    /// named PhysicsTemplates factory (e.g. "ease_in_sine"). Preserves the
    /// template's typed inputs so the sweep variable picker defaults to
    /// meaningful ranges.
    ///
    /// Returns false if `templateName` doesn't match a known template.
    bool loadTemplate(const std::string& templateName);

    // -- Access (mostly for tests) -----------------------------------------
    NodeGraph& graph() { return m_graph; }
    const NodeGraph& graph() const { return m_graph; }
    const std::vector<FormulaInput>& inputs() const { return m_inputs; }
    const std::string& loadedTemplate() const { return m_loadedTemplateName; }

    // -- Preview configuration (exposed for tests) -------------------------
    void setSweepRange(float minV, float maxV) { m_sweepMin = minV; m_sweepMax = maxV; }
    void setSweepVariable(const std::string& name) { m_sweepVariable = name; }

    /// @brief Returns the latest preview sample. Recomputed by `draw()` each
    /// frame; tests can call `recomputePreview()` explicitly.
    const FormulaCurveSample& previewSample() const { return m_preview; }
    void recomputePreview();

private:
    void drawCatalog();
    void drawCanvas();
    void drawPreviewControls();
    void renderGraph();

    bool m_open = false;
    NodeEditorWidget m_widget;
    NodeGraph m_graph;
    std::vector<FormulaInput> m_inputs;   ///< From loaded template (or empty).
    std::unordered_map<std::string, float> m_coefficients;  ///< From loaded template.
    std::string m_loadedTemplateName;

    // Preview state
    FormulaCurveSample m_preview;
    std::string m_sweepVariable;          ///< Empty = auto-pick first variable.
    float m_sweepMin = 0.0f;
    float m_sweepMax = 1.0f;
    int m_sampleCount = 64;
};

} // namespace Vestige
