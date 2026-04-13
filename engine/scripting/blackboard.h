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
    /// @brief Set a variable value. Creates the variable if it does not exist.
    void set(const std::string& key, const ScriptValue& value);

    /// @brief Get a variable value. Returns a default float(0) if not found.
    ScriptValue get(const std::string& key) const;

    /// @brief Check if a variable exists.
    bool has(const std::string& key) const;

    /// @brief Remove a variable.
    bool remove(const std::string& key);

    /// @brief Remove all variables.
    void clear();

    /// @brief Get the number of stored variables.
    size_t size() const { return m_values.size(); }

    /// @brief Read-only access to all stored values.
    const std::unordered_map<std::string, ScriptValue>& values() const
    {
        return m_values;
    }

    // -- Serialization --
    nlohmann::json toJson() const;
    static Blackboard fromJson(const nlohmann::json& j);

private:
    std::unordered_map<std::string, ScriptValue> m_values;
};

} // namespace Vestige
