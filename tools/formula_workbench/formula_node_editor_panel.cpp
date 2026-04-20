// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file formula_node_editor_panel.cpp
/// @brief FormulaNodeEditorPanel — ImGui / ImPlot rendering (Phase 9E).
///
/// Only the UI-dependent methods (draw, drawCatalog, drawCanvas,
/// drawPreviewControls, renderGraph) live here. The headless sampler and
/// the panel's state-only methods (ctor, initialize, shutdown,
/// loadTemplate, recomputePreview) are in `formula_node_editor_core.cpp`
/// so unit tests can construct and drive the panel without linking
/// ImGui / ImPlot / imgui-node-editor.
#include "formula_node_editor_panel.h"

#include "formula/physics_templates.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cstdint>

namespace Vestige
{

namespace
{

constexpr const char* kDragDropPayload = "FORMULA_TEMPLATE";

/// @brief Pack (nodeId, pinIndex, isInput) into a stable imgui-node-editor
/// PinId. Same layout as ScriptEditorPanel::makePinId — 64-bit split between
/// the node and pin index, low bit for direction. NodeId is a uint32_t in
/// the formula NodeGraph, so bits [32..63] hold it losslessly.
uintptr_t makePinId(uint32_t nodeId, std::size_t pinIndex, bool isInput)
{
    static_assert(sizeof(uintptr_t) >= 8,
                  "FormulaNodeEditorPanel requires 64-bit uintptr_t.");
    return (static_cast<uintptr_t>(nodeId) << 32) |
           ((static_cast<uintptr_t>(pinIndex) & 0x7FFFFFFFull) << 1) |
           (isInput ? 1u : 0u);
}

/// @brief Resolve the catalog of PhysicsTemplates once; the list is static
/// for the lifetime of the process so every Workbench instance sees the
/// same entries and we avoid rebuilding ExprNodes per frame.
const std::vector<FormulaDefinition>& templateCatalog()
{
    static const std::vector<FormulaDefinition> cache = []
    {
        return PhysicsTemplates::createAll();
    }();
    return cache;
}

} // namespace

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void FormulaNodeEditorPanel::draw()
{
    if (!m_open)
    {
        return;
    }

    // Recompute every frame — cheap (<=4096 evaluator calls) and means the
    // preview follows graph edits live. If this shows up on a profile we'll
    // mark the graph dirty + recompute on demand instead.
    recomputePreview();

    const std::string title =
        m_loadedTemplateName.empty()
            ? std::string("Formula Node Editor###FormulaNodeEditorPanel")
            : "Formula Node Editor — " + m_loadedTemplateName +
              "###FormulaNodeEditorPanel";

    ImGui::SetNextWindowSize(ImVec2(1100, 700), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(title.c_str(), &m_open))
    {
        // Split: catalog on the left, canvas + preview on the right.
        const float catalogWidth = 240.0f;
        ImGui::BeginChild("##catalog", ImVec2(catalogWidth, 0), true);
        drawCatalog();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##canvas-column", ImVec2(0, 0), false);
        drawPreviewControls();
        drawCanvas();
        ImGui::EndChild();
    }
    ImGui::End();
}

void FormulaNodeEditorPanel::drawCatalog()
{
    ImGui::TextUnformatted("PhysicsTemplates");
    ImGui::Separator();
    ImGui::TextDisabled("Drag onto canvas or click to load");
    ImGui::Spacing();

    // Group entries by category so long lists stay navigable.
    const auto& entries = templateCatalog();
    std::string currentCategory;
    bool categoryOpen = false;

    for (const auto& def : entries)
    {
        if (def.category != currentCategory)
        {
            if (categoryOpen)
            {
                ImGui::Unindent();
            }
            currentCategory = def.category;
            categoryOpen =
                ImGui::CollapsingHeader(currentCategory.c_str(),
                                        ImGuiTreeNodeFlags_DefaultOpen);
            if (categoryOpen)
            {
                ImGui::Indent();
            }
        }

        if (!categoryOpen)
        {
            continue;
        }

        const bool isLoaded = (m_loadedTemplateName == def.name);
        if (ImGui::Selectable(def.name.c_str(), isLoaded))
        {
            loadTemplate(def.name);
        }

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            // Payload: the template's canonical name (fixed-size copy to
            // avoid dangling pointer to `def.name`'s storage after the
            // selectable goes out of scope).
            const std::string& n = def.name;
            ImGui::SetDragDropPayload(kDragDropPayload,
                                      n.c_str(), n.size() + 1);
            ImGui::Text("Load %s", n.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::IsItemHovered() && !def.description.empty())
        {
            ImGui::SetTooltip("%s", def.description.c_str());
        }
    }

    if (categoryOpen)
    {
        ImGui::Unindent();
    }
}

void FormulaNodeEditorPanel::drawPreviewControls()
{
    if (m_loadedTemplateName.empty())
    {
        ImGui::TextDisabled("No template loaded. Click a catalog entry "
                            "or drag one onto the canvas.");
    }
    else
    {
        ImGui::Text("Template: %s", m_loadedTemplateName.c_str());
    }

    // Sweep variable picker — lists every variable the current expression
    // references so non-first variables can be plotted too.
    std::vector<std::string> variables;
    if (const NodeId outId = findOutputNodeId(m_graph); outId != 0)
    {
        if (auto expr = m_graph.toExpressionTree(outId))
        {
            std::vector<std::string> all;
            expr->collectVariables(all);
            for (const auto& v : all)
            {
                if (std::find(variables.begin(), variables.end(), v)
                    == variables.end())
                {
                    variables.push_back(v);
                }
            }
        }
    }

    const std::string sweepLabel =
        m_preview.sweepVariable.empty() ? std::string("<auto>")
                                        : m_preview.sweepVariable;
    if (ImGui::BeginCombo("Sweep variable", sweepLabel.c_str()))
    {
        if (ImGui::Selectable("<auto>", m_sweepVariable.empty()))
        {
            m_sweepVariable.clear();
        }
        for (const auto& v : variables)
        {
            const bool selected = (m_sweepVariable == v);
            if (ImGui::Selectable(v.c_str(), selected))
            {
                m_sweepVariable = v;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SliderFloat("Min", &m_sweepMin, -100.0f, 100.0f);
    ImGui::SameLine();
    ImGui::SliderFloat("Max", &m_sweepMax, -100.0f, 100.0f);
    if (m_sweepMax <= m_sweepMin)
    {
        m_sweepMax = m_sweepMin + 1e-3f;
    }
    ImGui::SliderInt("Samples", &m_sampleCount, 2, 512);
}

void FormulaNodeEditorPanel::drawCanvas()
{
    if (!m_widget.isInitialized())
    {
        ImGui::TextDisabled("Node editor not initialised.");
        return;
    }

    // Make the canvas fill the remaining vertical space — split again so
    // the preview panel sits below the canvas at a fixed height.
    const float previewHeight = 200.0f;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float canvasHeight = std::max(avail.y - previewHeight - 20.0f,
                                        100.0f);

    ImGui::BeginChild("##canvas", ImVec2(0, canvasHeight), true);

    // Drop target wraps the canvas child so dragging from the catalog
    // drops here (imgui-node-editor swallows its own internal drags; the
    // ImGui drag-drop machinery is orthogonal).
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload(kDragDropPayload))
        {
            const char* data = static_cast<const char*>(payload->Data);
            if (data)
            {
                loadTemplate(std::string(data));
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (m_widget.beginCanvas("FormulaNodeEditorCanvas"))
    {
        renderGraph();
        m_widget.endCanvas();
    }

    ImGui::EndChild();

    // Preview plot lives outside the node-editor canvas so ImPlot doesn't
    // need to interleave with imgui-node-editor's transform stack.
    ImGui::BeginChild("##preview", ImVec2(0, previewHeight), true);
    ImGui::TextUnformatted("Output preview");
    ImGui::SameLine();
    if (!m_preview.sweepVariable.empty())
    {
        ImGui::TextDisabled("(sweep: %s)", m_preview.sweepVariable.c_str());
    }
    ImGui::Separator();

    if (!m_preview.error.empty())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s",
                           m_preview.error.c_str());
    }
    else if (m_preview.xs.empty() || m_preview.ys.empty())
    {
        ImGui::TextDisabled("(no data)");
    }
    else if (ImPlot::BeginPlot("##formulaPreview", ImVec2(-1, -1)))
    {
        const char* xlabel = m_preview.sweepVariable.empty()
                                 ? "x"
                                 : m_preview.sweepVariable.c_str();
        ImPlot::SetupAxes(xlabel, "output");
        ImPlot::PlotLine("##curve", m_preview.xs.data(),
                         m_preview.ys.data(),
                         static_cast<int>(m_preview.xs.size()));
        ImPlot::EndPlot();
    }

    ImGui::EndChild();
}

void FormulaNodeEditorPanel::renderGraph()
{
    // Emit one node per graph entry. Pin labels come from Port::name so the
    // formula-side port layout (A/B/Result, Condition/Then/Else, etc.) is
    // honoured without a separate descriptor registry.
    for (const auto& [id, node] : m_graph.getNodes())
    {
        m_widget.beginNode(id);

        ImGui::Text("%s", node.name.c_str());
        if (node.category == NodeCategory::INPUT && node.operation == "literal")
        {
            ImGui::TextDisabled("= %.4g", node.literalValue);
        }
        else if (node.category == NodeCategory::INPUT &&
                 node.operation == "variable")
        {
            ImGui::TextDisabled("var: %s", node.variableName.c_str());
        }

        for (std::size_t i = 0; i < node.inputs.size(); ++i)
        {
            m_widget.beginInputPin(makePinId(id, i, /*isInput*/ true));
            ImGui::Text("-> %s", node.inputs[i].name.c_str());
            m_widget.endPin();
        }

        for (std::size_t i = 0; i < node.outputs.size(); ++i)
        {
            m_widget.beginOutputPin(makePinId(id, i, /*isInput*/ false));
            ImGui::Text("%s ->", node.outputs[i].name.c_str());
            m_widget.endPin();
        }

        m_widget.endNode();
    }

    // Connections — look up port index by id within the owning node so pin
    // ids match the ones emitted above.
    for (const auto& conn : m_graph.getConnections())
    {
        const Node* src = m_graph.getNode(conn.sourceNode);
        const Node* tgt = m_graph.getNode(conn.targetNode);
        if (!src || !tgt)
        {
            continue;
        }

        std::size_t srcIdx = 0;
        for (std::size_t i = 0; i < src->outputs.size(); ++i)
        {
            if (src->outputs[i].id == conn.sourcePort)
            {
                srcIdx = i;
                break;
            }
        }

        std::size_t tgtIdx = 0;
        for (std::size_t i = 0; i < tgt->inputs.size(); ++i)
        {
            if (tgt->inputs[i].id == conn.targetPort)
            {
                tgtIdx = i;
                break;
            }
        }

        m_widget.link(conn.id,
                      makePinId(conn.sourceNode, srcIdx, /*isInput*/ false),
                      makePinId(conn.targetNode, tgtIdx, /*isInput*/ true));
    }
}

} // namespace Vestige
