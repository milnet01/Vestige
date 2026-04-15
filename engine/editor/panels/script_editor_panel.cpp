// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file script_editor_panel.cpp
/// @brief ScriptEditorPanel implementation.
#include "editor/panels/script_editor_panel.h"

#include "core/logger.h"
#include "scripting/node_type_registry.h"

#include <imgui.h>

namespace Vestige
{

namespace
{

constexpr const char* kFileFilter = ".vscript";

/// @brief Pack (nodeId, pinIndex, isInput) into a stable id for ed::PinId.
///
/// AUDIT.md §M1 / FIXPLAN H2 widened packing: the prior layout
/// `(nodeId<<16) | (pinIndex<<1) | isInput` collided whenever nodeId
/// >= 65536 or pinIndex >= 32768 — unlikely for human-authored graphs
/// but plausible for generated/procedural ones. This platform is 64-bit
/// only (checked via static_assert), so we pack:
///   - bits [32..63]: nodeId (full uint32_t range)
///   - bits [1..31]:  pinIndex (2^31 pins per node — effectively unlimited)
///   - bit 0:         isInput flag
/// On 64-bit uintptr_t the id is collision-free for any ScriptGraph.
uintptr_t makePinId(uint32_t nodeId, size_t pinIndex, bool isInput)
{
    static_assert(sizeof(uintptr_t) >= 8,
                  "ScriptEditorPanel requires 64-bit uintptr_t — Vestige "
                  "targets 64-bit only. See AUDIT.md §M1.");
    return (static_cast<uintptr_t>(nodeId) << 32) |
           ((static_cast<uintptr_t>(pinIndex) & 0x7FFFFFFFull) << 1) |
           (isInput ? 1u : 0u);
}

} // namespace

ScriptEditorPanel::ScriptEditorPanel()
    : m_openBrowser(ImGuiFileBrowserFlags_CloseOnEsc)
    , m_saveBrowser(ImGuiFileBrowserFlags_CloseOnEsc |
                    ImGuiFileBrowserFlags_EnterNewFilename |
                    ImGuiFileBrowserFlags_CreateNewDir)
{
    m_openBrowser.SetTitle("Open Script");
    m_openBrowser.SetTypeFilters({kFileFilter});
    m_saveBrowser.SetTitle("Save Script As");
    m_saveBrowser.SetTypeFilters({kFileFilter});
}

void ScriptEditorPanel::initialize(const std::string& settingsFile)
{
    m_widget.initialize(settingsFile);
}

void ScriptEditorPanel::shutdown()
{
    m_widget.shutdown();
}

void ScriptEditorPanel::draw()
{
    if (!m_open)
    {
        return;
    }

    // Build the title — "* " prefix when the graph has unsaved changes.
    const std::string graphLabel =
        m_currentPath.empty() ? std::string("untitled") : m_currentPath;
    const std::string title =
        std::string(m_dirty ? "* " : "") +
        "Script Editor — " + graphLabel + "###ScriptEditorPanel";

    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(title.c_str(), &m_open, ImGuiWindowFlags_MenuBar))
    {
        drawMenuBar();
        drawCanvas();
    }
    ImGui::End();

    // File dialogs are modal — display them outside the panel window so
    // they remain interactive even when the panel is small or undocked.
    m_openBrowser.Display();
    if (m_openBrowser.HasSelected())
    {
        open(m_openBrowser.GetSelected().string());
        m_openBrowser.ClearSelected();
    }

    m_saveBrowser.Display();
    if (m_saveBrowser.HasSelected())
    {
        saveAs(m_saveBrowser.GetSelected().string());
        m_saveBrowser.ClearSelected();
    }
}

void ScriptEditorPanel::drawMenuBar()
{
    if (!ImGui::BeginMenuBar())
    {
        return;
    }

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New", "Ctrl+N"))
        {
            newGraph();
        }
        if (ImGui::MenuItem("Open…", "Ctrl+O"))
        {
            m_openBrowser.Open();
        }
        if (ImGui::MenuItem("Save", "Ctrl+S",
                            /*selected*/ false,
                            /*enabled*/ !m_currentPath.empty()))
        {
            save();
        }
        if (ImGui::MenuItem("Save As…", "Ctrl+Shift+S"))
        {
            m_saveBrowser.Open();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        ImGui::TextDisabled("Nodes: %zu  |  Connections: %zu",
                            m_graph.nodes.size(), m_graph.connections.size());
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

void ScriptEditorPanel::drawCanvas()
{
    if (!m_widget.isInitialized())
    {
        ImGui::TextDisabled("Node editor not initialised.");
        return;
    }
    if (!m_widget.beginCanvas("ScriptEditorCanvas"))
    {
        return;
    }
    renderGraph();
    m_widget.endCanvas();
}

void ScriptEditorPanel::renderGraph()
{
    // Render every node in the graph. Pins use the descriptor's pin defs
    // for labels and ordering. Steps 5+ will add palette-driven creation,
    // properties editing, connection drag, and breakpoints.
    for (const auto& node : m_graph.nodes)
    {
        m_widget.beginNode(node.id);

        const NodeTypeDescriptor* desc =
            m_registry ? m_registry->findNode(node.typeName) : nullptr;
        const std::string& displayName =
            desc ? desc->displayName : node.typeName;
        ImGui::Text("%s", displayName.c_str());

        if (desc)
        {
            // Inputs (left side)
            for (size_t i = 0; i < desc->inputDefs.size(); ++i)
            {
                m_widget.beginInputPin(makePinId(node.id, i, /*isInput*/ true));
                ImGui::Text("→ %s", desc->inputDefs[i].name.c_str());
                m_widget.endPin();
            }

            // Outputs (right side)
            for (size_t i = 0; i < desc->outputDefs.size(); ++i)
            {
                m_widget.beginOutputPin(makePinId(node.id, i, /*isInput*/ false));
                ImGui::Text("%s →", desc->outputDefs[i].name.c_str());
                m_widget.endPin();
            }
        }

        m_widget.endNode();
    }

    // Render connections. Pin ids on both ends must match the ids we used
    // in beginInputPin / beginOutputPin above.
    //
    // AUDIT.md §M3 / FIXPLAN H2: use the NodeTypeDescriptor's pre-computed
    // inputIndexByName / outputIndexByName maps instead of a per-frame
    // linear scan. O(1) lookup regardless of pin count.
    //
    // AUDIT.md §M4 / FIXPLAN H2: if a pin name doesn't resolve, skip the
    // connection and log a warning rather than silently drawing to pin
    // index 0 (which would be visually wrong and might hide a real bug
    // in the graph file).
    for (const auto& conn : m_graph.connections)
    {
        const ScriptNodeDef* srcNodeDef = m_graph.findNode(conn.sourceNode);
        const ScriptNodeDef* tgtNodeDef = m_graph.findNode(conn.targetNode);
        if (!srcNodeDef || !tgtNodeDef)
        {
            Logger::warning("[ScriptEditor] Connection " +
                std::to_string(conn.id) +
                " references missing node — skipping (AUDIT.md §M4)");
            continue;
        }

        const NodeTypeDescriptor* srcDesc = m_registry
            ? m_registry->findNode(srcNodeDef->typeName) : nullptr;
        const NodeTypeDescriptor* tgtDesc = m_registry
            ? m_registry->findNode(tgtNodeDef->typeName) : nullptr;
        if (!srcDesc || !tgtDesc)
        {
            continue;  // Registry lookup failed (logged elsewhere at load).
        }

        auto srcIt = srcDesc->outputIndexByName.find(conn.sourcePin);
        auto tgtIt = tgtDesc->inputIndexByName.find(conn.targetPin);
        if (srcIt == srcDesc->outputIndexByName.end())
        {
            Logger::warning("[ScriptEditor] Connection " +
                std::to_string(conn.id) + " has unknown source pin '" +
                conn.sourcePin + "' on node type '" + srcNodeDef->typeName +
                "' — skipping (AUDIT.md §M4)");
            continue;
        }
        if (tgtIt == tgtDesc->inputIndexByName.end())
        {
            Logger::warning("[ScriptEditor] Connection " +
                std::to_string(conn.id) + " has unknown target pin '" +
                conn.targetPin + "' on node type '" + tgtNodeDef->typeName +
                "' — skipping (AUDIT.md §M4)");
            continue;
        }

        m_widget.link(conn.id,
                      makePinId(conn.sourceNode, srcIt->second, /*isInput*/ false),
                      makePinId(conn.targetNode, tgtIt->second, /*isInput*/ true));
    }
}

// ---------------------------------------------------------------------------
// Document operations
// ---------------------------------------------------------------------------

void ScriptEditorPanel::newGraph()
{
    m_graph = ScriptGraph{};
    m_currentPath.clear();
    m_dirty = false;
}

bool ScriptEditorPanel::open(const std::string& path)
{
    // AUDIT.md §M2 / FIXPLAN H2: match the header contract — preserve the
    // existing graph if the load fails. ScriptGraph::loadFromFile returns
    // an empty graph on failure (and logs the error), so we treat
    // "path is non-empty AND loaded graph has no nodes AND no connections"
    // as failure. Genuinely empty graphs saved by the user will have the
    // "name" field populated via fromJson; we key on name + nodes to
    // avoid false negatives on freshly-created-but-untouched graphs.
    auto loaded = ScriptGraph::loadFromFile(path);
    const bool loadLikelyFailed =
        !path.empty() &&
        loaded.nodes.empty() &&
        loaded.connections.empty() &&
        loaded.name.empty();
    if (loadLikelyFailed)
    {
        Logger::warning("[ScriptEditor] open() failed — preserving "
                        "existing graph (AUDIT.md §M2): " + path);
        return false;
    }

    m_graph = std::move(loaded);
    m_currentPath = path;
    m_dirty = false;
    return true;
}

bool ScriptEditorPanel::save()
{
    if (m_currentPath.empty())
    {
        Logger::warning("[ScriptEditor] Save with no current path — use Save As");
        return false;
    }
    if (!m_graph.saveToFile(m_currentPath))
    {
        return false;
    }
    m_dirty = false;
    return true;
}

bool ScriptEditorPanel::saveAs(const std::string& path)
{
    m_currentPath = path;
    return save();
}

} // namespace Vestige
