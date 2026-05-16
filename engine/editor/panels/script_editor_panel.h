// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file script_editor_panel.h
/// @brief Dockable editor panel for visual scripts (Phase 9E-3).
///
/// Hosts a NodeEditorWidget over a ScriptGraph asset, with a File menu for
/// New / Open / Save / Save As. Step 4 of the 9E-3 plan delivers the canvas +
/// menu only — palette, properties, breakpoints land in later steps.
#pragma once

#include "editor/panels/i_panel.h"
#include "editor/widgets/node_editor_widget.h"
#include "scripting/script_graph.h"

#include <imgui.h>
#include <imfilebrowser.h>

#include <string>

namespace Vestige
{

class NodeTypeRegistry;

/// @brief Editor panel that opens, edits, and saves a single ScriptGraph.
///
/// Multiple instances allowed (side-by-side script comparison).
class ScriptEditorPanel : public IPanel
{
public:
    ScriptEditorPanel();

    const char* displayName() const override { return "Script Editor"; }


    /// @brief Initialise file dialogs and the underlying NodeEditorWidget.
    /// @param settingsFile Optional path the node-editor uses to persist
    ///   per-canvas layout (zoom, pan, node positions).
    void initialize(const std::string& settingsFile = {});

    /// @brief Tear down the underlying widget. Safe to call multiple times.
    void shutdown();

    /// @brief Set the registry used to look up node descriptors when
    /// rendering. Required before draw() so input/output pin labels render.
    void setRegistry(const NodeTypeRegistry* registry) { m_registry = registry; }

    /// @brief Show the panel.
    void open() { m_open = true; }

    /// @brief Hide the panel.
    void close() { m_open = false; }

    /// @brief IPanel toggle — bridges to open()/close().
    void setOpen(bool open) override { m_open = open; }

    /// @brief Whether the panel is currently visible (and will render).
    bool isOpen() const override { return m_open; }

    /// @brief Render the panel for one frame. Call from inside the editor's
    /// per-frame ImGui pass.
    void draw();

    // -- Document operations (also wired to the File menu) --

    /// @brief Reset the in-memory graph.
    void newGraph();

    /// @brief Load a graph from the given path. Does nothing if the load
    /// fails (the existing graph is preserved).
    bool open(const std::string& path);

    /// @brief Write the current graph back to its source path. Returns false
    /// if the graph has no source path yet (caller should use saveAs).
    bool save();

    /// @brief Write the current graph to a new path and adopt it as the
    /// source path.
    bool saveAs(const std::string& path);

    /// @brief Direct access to the underlying graph (mostly for tests).
    ScriptGraph& graph() { return m_graph; }
    const ScriptGraph& graph() const { return m_graph; }

    /// @brief Whether the graph has unsaved changes.
    bool isDirty() const { return m_dirty; }

private:
    void drawMenuBar();
    void drawCanvas();
    void renderGraph();

    bool m_open = false;
    bool m_dirty = false;
    /// @brief Set every time the in-memory graph is replaced (New, Open,
    /// Templates). The next renderGraph() applies each node's posX/posY via
    /// NodeEditorWidget::setNodePosition, then clears the flag so user
    /// drags + the persisted settings file take over. Without this step
    /// imgui-node-editor piles every newly-loaded node at its default
    /// spawn point, making multi-node templates appear as a single stacked
    /// box.
    bool m_needsLayout = false;
    /// @brief Set one frame after m_needsLayout, consumed on the frame after
    /// that to call ed::NavigateToContent and zoom-to-fit. Fresh graphs
    /// default to a view that's so zoomed in a single pin fills the canvas;
    /// fitting only works on the frame after the nodes were first measured,
    /// so we defer by one frame.
    bool m_needsFitView = false;
    NodeEditorWidget m_widget;
    ScriptGraph m_graph;
    std::string m_currentPath;
    const NodeTypeRegistry* m_registry = nullptr;
    ImGui::FileBrowser m_openBrowser;
    ImGui::FileBrowser m_saveBrowser;
};

} // namespace Vestige
