/// @file blackboard.h
/// @brief Key-value variable store for visual scripting.
#pragma once

#include "scripting/script_value.h"

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>

namespace Vestige
{

/// @brief Variable scope levels (based on Unity's proven 6-level model).
enum class VariableScope
{
    FLOW,        ///< Local to one execution chain
    GRAPH,       ///< Local to one script graph instance
    ENTITY,      ///< Shared across all scripts on one entity
    SCENE,       ///< Shared within the current scene
    APPLICATION, ///< Global, shared across scenes at runtime
    SAVED        ///< Persistent across application restarts
};

const char* variableScopeToString(VariableScope scope);
VariableScope variableScopeFromString(const std::string& str);

/// @brief Definition of a user-declared variable (for the editor).
struct VariableDef
{
    std::string name;
    ScriptDataType dataType = ScriptDataType::FLOAT;
    ScriptValue defaultValue;
    VariableScope scope = VariableScope::GRAPH;

    nlohmann::json toJson() const;
    static VariableDef fromJson(const nlohmann::json& j);
};

/// @brief A string-keyed store of ScriptValues.
///
/// Each variable scope uses its own Blackboard instance. The Blackboard
/// supports serialization for persistence (Saved scope) and editor display.
class Blackboard
{
public:
    /// @brief Soft cap on number of variables per scope. A malicious or buggy
    /// script that grows the blackboard unbounded (e.g. by creating new keys
    /// each frame) will be refused further insertions once this limit is hit.
    /// Existing keys can still be updated.
    static constexpr size_t MAX_KEYS = 1024;

    /// @brief Set a variable value. Creates the variable if it does not exist
    /// and the per-scope cap has not been reached. Updates to existing keys
    /// always succeed.
    void set(const std::string& key, const ScriptValue& value);

    /// @brief Get a variable value. Returns a default float(0) if not found.
    ScriptValue get(const std::string& key) const;

    /// @brief Check if a variable exists.
    bool has(const std::string& key) const;

    /// @brief Remove a variable. Returns true if the key was present.
    bool remove(const std::string& key);

    /// @brief Remove all variables from this blackboard.
    void clear();

    /// @brief Number of stored variables.
    size_t size() const { return m_values.size(); }

    /// @brief Read-only access to all stored values.
    const std::unordered_map<std::string, ScriptValue>& values() const
    {
        return m_values;
    }

    // -- Serialization --

    /// @brief Serialize all variables to a JSON object (keys = variable names).
    nlohmann::json toJson() const;
    /// @brief Build a Blackboard from a JSON object produced by toJson().
    /// Malformed entries are skipped; the per-scope cap still applies.
    static Blackboard fromJson(const nlohmann::json& j);

private:
    std::unordered_map<std::string, ScriptValue> m_values;
};

} // namespace Vestige
