/// @file blackboard.cpp
/// @brief Blackboard and VariableDef implementation.
#include "scripting/blackboard.h"

namespace Vestige
{

// ---------------------------------------------------------------------------
// VariableScope string conversions
// ---------------------------------------------------------------------------

const char* variableScopeToString(VariableScope scope)
{
    switch (scope)
    {
    case VariableScope::FLOW:        return "flow";
    case VariableScope::GRAPH:       return "graph";
    case VariableScope::ENTITY:      return "entity";
    case VariableScope::SCENE:       return "scene";
    case VariableScope::APPLICATION: return "application";
    case VariableScope::SAVED:       return "saved";
    }
    return "graph";
}

VariableScope variableScopeFromString(const std::string& str)
{
    if (str == "flow")        return VariableScope::FLOW;
    if (str == "graph")       return VariableScope::GRAPH;
    if (str == "entity")      return VariableScope::ENTITY;
    if (str == "scene")       return VariableScope::SCENE;
    if (str == "application") return VariableScope::APPLICATION;
    if (str == "saved")       return VariableScope::SAVED;
    return VariableScope::GRAPH;
}

// ---------------------------------------------------------------------------
// VariableDef serialization
// ---------------------------------------------------------------------------

nlohmann::json VariableDef::toJson() const
{
    nlohmann::json j;
    j["name"] = name;
    j["dataType"] = scriptDataTypeToString(dataType);
    j["default"] = defaultValue.toJson();
    j["scope"] = variableScopeToString(scope);
    return j;
}

VariableDef VariableDef::fromJson(const nlohmann::json& j)
{
    VariableDef def;
    def.name = j.value("name", std::string{});
    def.dataType = scriptDataTypeFromString(j.value("dataType", std::string{"float"}));
    if (j.contains("default"))
    {
        def.defaultValue = ScriptValue::fromJson(j["default"]);
    }
    def.scope = variableScopeFromString(j.value("scope", std::string{"graph"}));
    return def;
}

// ---------------------------------------------------------------------------
// Blackboard
// ---------------------------------------------------------------------------

void Blackboard::set(const std::string& key, const ScriptValue& value)
{
    m_values[key] = value;
}

ScriptValue Blackboard::get(const std::string& key) const
{
    auto it = m_values.find(key);
    if (it != m_values.end())
    {
        return it->second;
    }
    return ScriptValue(0.0f);
}

bool Blackboard::has(const std::string& key) const
{
    return m_values.count(key) > 0;
}

bool Blackboard::remove(const std::string& key)
{
    return m_values.erase(key) > 0;
}

void Blackboard::clear()
{
    m_values.clear();
}

nlohmann::json Blackboard::toJson() const
{
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [key, value] : m_values)
    {
        j[key] = value.toJson();
    }
    return j;
}

Blackboard Blackboard::fromJson(const nlohmann::json& j)
{
    Blackboard bb;
    if (j.is_object())
    {
        for (auto it = j.begin(); it != j.end(); ++it)
        {
            bb.m_values[it.key()] = ScriptValue::fromJson(it.value());
        }
    }
    return bb;
}

} // namespace Vestige
