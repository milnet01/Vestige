// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file node_editor_widget.h
/// @brief Thin C++ wrapper around the imgui-node-editor library.
///
/// All `ax::NodeEditor::` (alias `ed::`) calls are isolated here so the rest
/// of the editor doesn't pull in the library headers or care about its
/// internal types. If we ever swap to a different node-editor library
/// (pthom fork, ImNodeFlow, etc. — see docs/PHASE9E3_RESEARCH.md §3.4 for
/// the fallback ranking), only this file changes.
///
/// The widget owns one `ed::EditorContext` per instance, so multiple
/// independent canvases (e.g. one per ScriptEditorPanel + one for a future
/// FormulaWorkbench panel) coexist without state leaks.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

namespace ax::NodeEditor
{
struct EditorContext;
enum class SaveReasonFlags : uint32_t;  // forward-declared to avoid pulling
                                        // the library header into every
                                        // translation unit.
}

namespace Vestige
{

/// @brief Owns and renders one node-editor canvas.
///
/// Usage pattern from a panel:
/// @code
///   if (m_widget.beginCanvas("ScriptEditor"))
///   {
///       // Emit nodes via beginNode/endNode and links via link()
///       m_widget.endCanvas();
///   }
/// @endcode
class NodeEditorWidget
{
public:
    NodeEditorWidget() = default;
    ~NodeEditorWidget();

    NodeEditorWidget(const NodeEditorWidget&) = delete;
    NodeEditorWidget& operator=(const NodeEditorWidget&) = delete;
    NodeEditorWidget(NodeEditorWidget&&) = delete;
    NodeEditorWidget& operator=(NodeEditorWidget&&) = delete;

    /// @brief Create the underlying ed::EditorContext.
    /// @param settingsFile Optional path to a .json file the library uses to
    ///   persist node positions and view state. Empty = no persistence.
    /// Safe to call multiple times; later calls are no-ops.
    void initialize(const std::string& settingsFile = {});

    /// @brief Destroy the ed::EditorContext. Safe to call from a destructor.
    void shutdown();

    /// @brief True once initialize() has succeeded.
    bool isInitialized() const { return m_context != nullptr; }

    /// @brief Open the canvas for rendering. Always pair with endCanvas().
    /// @param idName ImGui ID for this canvas — must be stable across frames.
    /// @return True if rendering should proceed (always true after init).
    bool beginCanvas(const char* idName);

    /// @brief Close the canvas. Must be called if beginCanvas() returned true.
    void endCanvas();

    /// @brief Begin a node block. Pair with endNode(). Pass any stable id.
    void beginNode(uintptr_t nodeId);
    void endNode();

    /// @brief Begin an input pin. Pair with endPin().
    void beginInputPin(uintptr_t pinId);

    /// @brief Begin an output pin. Pair with endPin().
    void beginOutputPin(uintptr_t pinId);

    void endPin();

    /// @brief Draw a connection between two pins.
    void link(uintptr_t linkId, uintptr_t fromPin, uintptr_t toPin);

    /// @brief Set a node's canvas position unconditionally.
    ///
    /// Used by callers that load a graph with explicit layout (e.g.
    /// gameplay templates store per-node posX/posY) — applied once after
    /// load so the persisted node-editor settings file can still take over
    /// user drags afterwards. Calling this every frame would override the
    /// user's moves, so pair it with a "needs layout" latch in the owner.
    void setNodePosition(uintptr_t nodeId, float x, float y);

    /// @brief Pan + zoom the canvas so every node is visible.
    ///
    /// Should be called the frame *after* nodes are first placed, because
    /// imgui-node-editor fits against last-frame measured bounds — fresh
    /// nodes have zero-size bounds on the frame they're placed. A duration
    /// of 0 snaps instantly (default is animated). Must be called inside a
    /// beginCanvas()/endCanvas() pair.
    void navigateToContent(float duration = 0.0f);

    /// @brief True if the settings file already contains a saved position
    /// for this node id.
    ///
    /// Owners that seed fresh-graph layouts (e.g. ScriptEditorPanel applying
    /// per-template posX/posY) should consult this before calling
    /// setNodePosition: if the user previously dragged the node and we saved
    /// it to NodeEditor.json, the library's LoadSettings path will restore
    /// that position on next launch, and a naive force-set would stomp it.
    bool hasPersistedPosition(uintptr_t nodeId) const;

private:
    // AUDIT.md §H16 / FIXPLAN H1: the SaveSettings / LoadSettings callbacks
    // that override the library's default file-based persistence live in
    // the .cpp so we don't need to leak the imgui_node_editor.h types
    // into every translation unit that includes this header. The .cpp
    // passes `this` through Config::UserPointer so the callbacks can
    // read m_settingsFile and m_isShuttingDown.

    ax::NodeEditor::EditorContext* m_context = nullptr;
    std::string m_settingsFile;
    bool m_isShuttingDown = false;  ///< Set for the duration of shutdown().

    /// @brief Node ids that currently have a persisted location in the
    /// settings file. Rebuilt on initialize() and on every successful
    /// SaveSettings flush so in-session drags stay reflected.
    std::unordered_set<uintptr_t> m_persistedNodeIds;

    // Grant the free-function callbacks in node_editor_widget.cpp access
    // to the private members above.
    friend bool nodeEditorWidget_saveSettings(
        const char* data, size_t size,
        ax::NodeEditor::SaveReasonFlags reason, void* userPointer);
    friend size_t nodeEditorWidget_loadSettings(char* data, void* userPointer);
};

} // namespace Vestige
