// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file node_editor_widget.cpp
/// @brief NodeEditorWidget implementation. Bridges to imgui-node-editor.
#include "editor/widgets/node_editor_widget.h"
#include "core/logger.h"

#include <imgui_node_editor.h>

#include <cstring>
#include <fstream>
#include <sstream>

namespace Vestige
{

namespace ed = ax::NodeEditor;

NodeEditorWidget::~NodeEditorWidget()
{
    shutdown();
}

// ---------------------------------------------------------------------------
// SaveSettings / LoadSettings callbacks (AUDIT.md §H16 / FIXPLAN H1)
// ---------------------------------------------------------------------------
//
// imgui-node-editor's default SettingsFile path writes settings through
// ImGui-resident state at DestroyEditor time, which races with
// ImGui::DestroyContext in our shutdown sequence and SEGVs. By providing
// our own SaveSettings callback gated on m_isShuttingDown we:
//
//   1. Preserve runtime saves (node positions, pan/zoom, selection) —
//      every edit round-trips to disk immediately.
//   2. Swallow the final shutdown save so the teardown path cannot
//      dereference freed ImGui state.
//   3. Keep Load working normally; layout is restored on next launch.

// Free-function callbacks matching the library's ConfigSaveSettings /
// ConfigLoadSettings signatures exactly. Friends of NodeEditorWidget
// (see header) so they can read the widget's private members.
bool nodeEditorWidget_saveSettings(const char* data, size_t size,
                                    ed::SaveReasonFlags /*reason*/,
                                    void* userPointer)
{
    auto* self = static_cast<NodeEditorWidget*>(userPointer);
    if (!self) return true;
    if (self->m_isShuttingDown)
    {
        // Final save during DestroyEditor — suppress to avoid the race
        // with ImGui context teardown.
        return true;
    }
    if (self->m_settingsFile.empty())
    {
        return true;  // caller disabled persistence
    }
    std::ofstream f(self->m_settingsFile, std::ios::binary | std::ios::trunc);
    if (!f)
    {
        Logger::warning(std::string("[NodeEditor] Failed to open settings "
                                    "for write: ") + self->m_settingsFile);
        return false;
    }
    f.write(data, static_cast<std::streamsize>(size));
    return static_cast<bool>(f);
}

size_t nodeEditorWidget_loadSettings(char* data, void* userPointer)
{
    auto* self = static_cast<NodeEditorWidget*>(userPointer);
    if (!self || self->m_settingsFile.empty()) return 0;

    std::ifstream f(self->m_settingsFile, std::ios::binary);
    if (!f) return 0;  // no saved settings yet

    std::ostringstream buf;
    buf << f.rdbuf();
    const std::string& s = buf.str();

    // First call: data==nullptr → return required size.
    // Second call: data != nullptr → copy payload.
    if (data) std::memcpy(data, s.data(), s.size());
    return s.size();
}

void NodeEditorWidget::initialize(const std::string& settingsFile)
{
    if (m_context)
    {
        return;
    }
    m_settingsFile = settingsFile;

    ed::Config cfg;
    // Route all persistence through our callbacks so we can suppress
    // the shutdown save (see nodeEditorWidget_saveSettings above).
    cfg.SettingsFile = nullptr;
    cfg.UserPointer = this;
    cfg.SaveSettings = &nodeEditorWidget_saveSettings;
    cfg.LoadSettings = &nodeEditorWidget_loadSettings;
    m_context = ed::CreateEditor(&cfg);
}

void NodeEditorWidget::shutdown()
{
    if (m_context)
    {
        // Suppress the final SaveSettings call during DestroyEditor.
        // Any unsaved runtime edits have already round-tripped via the
        // SaveSettings callback fired by user interactions earlier.
        m_isShuttingDown = true;
        ed::DestroyEditor(m_context);
        m_context = nullptr;
        m_isShuttingDown = false;
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
