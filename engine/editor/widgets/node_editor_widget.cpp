// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file node_editor_widget.cpp
/// @brief NodeEditorWidget implementation. Bridges to imgui-node-editor.
#include "editor/widgets/node_editor_widget.h"
#include "core/logger.h"

#include <imgui_node_editor.h>
#include <nlohmann/json.hpp>

#include <cstring>
#include <fstream>
#include <sstream>

namespace Vestige
{

namespace ed = ax::NodeEditor;

namespace
{

/// @brief Parse NodeEditor.json and collect every node id that has a saved
/// location. Keys of the top-level "nodes" object are stringified node ids
/// prefixed with the object-type tag the library emits — e.g. "node:1".
/// Strip the prefix before converting. Malformed or missing files are
/// treated as "no persisted nodes" rather than an error — the caller falls
/// back to seeding from template defaults.
std::unordered_set<uintptr_t> parsePersistedNodeIds(const std::string& data)
{
    std::unordered_set<uintptr_t> ids;
    if (data.empty())
    {
        return ids;
    }
    try
    {
        const auto j = nlohmann::json::parse(data);
        const auto nodesIt = j.find("nodes");
        if (nodesIt == j.end() || !nodesIt->is_object())
        {
            return ids;
        }
        for (const auto& entry : nodesIt->items())
        {
            const std::string& key = entry.key();
            // The library tags ids as "node:<int>" (see
            // Serialization::GenerateObjectName in imgui_node_editor.cpp);
            // older files may use the bare integer form.
            constexpr const char* kPrefix = "node:";
            constexpr size_t kPrefixLen = 5;
            std::string numStr = key;
            if (numStr.rfind(kPrefix, 0) == 0)
            {
                numStr = numStr.substr(kPrefixLen);
            }
            try
            {
                ids.insert(std::stoull(numStr));
            }
            catch (...)
            {
                // Unexpected key format — skip it.
            }
        }
    }
    catch (const std::exception& e)
    {
        Logger::warning(std::string("[NodeEditor] Failed to parse settings "
                                    "JSON (persisted ids set will be "
                                    "empty): ") + e.what());
    }
    return ids;
}

} // namespace

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
    if (!f)
    {
        return false;
    }
    // Merge — don't replace — the persisted-ids set with any new ids in the
    // payload we just wrote. Replacing would wipe the set on the first
    // pre-BeginNode save: the library serializes only nodes that are
    // present in m_Nodes (i.e. already referenced by BeginNode /
    // SetNodePosition), while m_Settings still holds the loaded layout.
    // A save fired at end-of-frame before the panel has rendered any
    // nodes would therefore write an empty "nodes" object and clear the
    // set we just populated from disk at initialize() time.
    const auto newlySeen =
        parsePersistedNodeIds(std::string(data, size));
    for (auto id : newlySeen)
    {
        self->m_persistedNodeIds.insert(id);
    }
    return true;
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

    // Pre-populate the persisted-ids set from the settings file if it
    // exists. Used by hasPersistedPosition() so callers that seed fresh
    // graph layouts (ScriptEditorPanel's template loader) can skip nodes
    // whose positions were already restored by the library's LoadSettings
    // path — without this, force-applying template defaults would stomp
    // every user-dragged position on the next launch.
    m_persistedNodeIds.clear();
    if (!m_settingsFile.empty())
    {
        std::ifstream f(m_settingsFile, std::ios::binary);
        if (f)
        {
            std::ostringstream buf;
            buf << f.rdbuf();
            m_persistedNodeIds = parsePersistedNodeIds(buf.str());
        }
    }

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

void NodeEditorWidget::setNodePosition(uintptr_t nodeId, float x, float y)
{
    ed::SetNodePosition(static_cast<ed::NodeId>(nodeId), ImVec2(x, y));
}

void NodeEditorWidget::navigateToContent(float duration)
{
    ed::NavigateToContent(duration);
}

bool NodeEditorWidget::hasPersistedPosition(uintptr_t nodeId) const
{
    return m_persistedNodeIds.count(nodeId) != 0;
}

} // namespace Vestige
