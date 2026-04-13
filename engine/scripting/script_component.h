/// @file script_component.h
/// @brief Entity component that attaches visual scripts to an entity.
#pragma once

#include "scripting/blackboard.h"
#include "scripting/script_instance.h"

#include <string>
#include <vector>

namespace Vestige
{

/// @brief Component that holds one or more visual script instances on an entity.
///
/// Each entity with gameplay scripting has a ScriptComponent. The component
/// owns ScriptInstance objects (one per attached graph) and an entity-scope
/// Blackboard shared across all scripts on this entity.
class ScriptComponent
{
public:
    /// @brief Add a script by graph asset path.
    /// The graph is not loaded here — ScriptingSystem handles loading
    /// during onSceneLoad().
    void addScript(const std::string& graphAssetPath);

    /// @brief Remove a script by index.
    void removeScript(size_t index);

    /// @brief Get the list of graph asset paths.
    const std::vector<std::string>& scriptPaths() const { return m_scriptPaths; }

    /// @brief Get the runtime instances (populated by ScriptingSystem).
    std::vector<ScriptInstance>& instances() { return m_instances; }
    const std::vector<ScriptInstance>& instances() const { return m_instances; }

    /// @brief Per-entity shared blackboard (Entity scope variables).
    Blackboard& entityBlackboard() { return m_entityBlackboard; }
    const Blackboard& entityBlackboard() const { return m_entityBlackboard; }

private:
    std::vector<std::string> m_scriptPaths;
    std::vector<ScriptInstance> m_instances;
    Blackboard m_entityBlackboard;
};

} // namespace Vestige
