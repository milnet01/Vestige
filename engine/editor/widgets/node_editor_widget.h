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

namespace ax::NodeEditor
{
struct EditorContext;
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

private:
    ax::NodeEditor::EditorContext* m_context = nullptr;
    std::string m_settingsFile;
};

} // namespace Vestige
