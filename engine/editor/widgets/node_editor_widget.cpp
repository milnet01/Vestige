/// @file node_editor_widget.cpp
/// @brief NodeEditorWidget implementation. Bridges to imgui-node-editor.
#include "editor/widgets/node_editor_widget.h"

#include <imgui_node_editor.h>

namespace Vestige
{

namespace ed = ax::NodeEditor;

NodeEditorWidget::~NodeEditorWidget()
{
    shutdown();
}

void NodeEditorWidget::initialize(const std::string& settingsFile)
{
    if (m_context)
    {
        return;
    }
    m_settingsFile = settingsFile;

    ed::Config cfg;
    if (!m_settingsFile.empty())
    {
        cfg.SettingsFile = m_settingsFile.c_str();
    }
    m_context = ed::CreateEditor(&cfg);
}

void NodeEditorWidget::shutdown()
{
    if (m_context)
    {
        ed::DestroyEditor(m_context);
        m_context = nullptr;
    }
}

bool NodeEditorWidget::beginCanvas(const char* idName)
{
    if (!m_context)
    {
        return false;
    }
    ed::SetCurrentEditor(m_context);
    ed::Begin(idName);
    return true;
}

void NodeEditorWidget::endCanvas()
{
    if (!m_context)
    {
        return;
    }
    ed::End();
    ed::SetCurrentEditor(nullptr);
}

void NodeEditorWidget::beginNode(uintptr_t nodeId)
{
    ed::BeginNode(static_cast<ed::NodeId>(nodeId));
}

void NodeEditorWidget::endNode()
{
    ed::EndNode();
}

void NodeEditorWidget::beginInputPin(uintptr_t pinId)
{
    ed::BeginPin(static_cast<ed::PinId>(pinId), ed::PinKind::Input);
}

void NodeEditorWidget::beginOutputPin(uintptr_t pinId)
{
    ed::BeginPin(static_cast<ed::PinId>(pinId), ed::PinKind::Output);
}

void NodeEditorWidget::endPin()
{
    ed::EndPin();
}

void NodeEditorWidget::link(uintptr_t linkId, uintptr_t fromPin, uintptr_t toPin)
{
    ed::Link(static_cast<ed::LinkId>(linkId),
             static_cast<ed::PinId>(fromPin),
             static_cast<ed::PinId>(toPin));
}

} // namespace Vestige
