/// @file script_value.cpp
/// @brief ScriptValue implementation.
#include "scripting/script_value.h"

#include <array>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Enum string conversions
// ---------------------------------------------------------------------------
//
// The three 11-case switch statements in this file (toString/fromString and
// the fromJson dispatcher) are flagged by audit tooling as "large switch
// chains" (audit L4). They are retained by design: the ScriptDataType enum
// IS the discriminator for the ScriptValue variant, so there is no non-switch
// form that's clearer or safer. Any new ScriptDataType value must be added in
// exactly these three sites, which a switch makes mechanically enforceable.
// A std::array<const char*> lookup by enum value would be equally valid for
// to/from string; a table-dispatched fromJson would actually be worse.
// ---------------------------------------------------------------------------

const char* scriptDataTypeToString(ScriptDataType type)
{
    switch (type)
    {
    case ScriptDataType::BOOL:   return "bool";
    case ScriptDataType::INT:    return "int";
    case ScriptDataType::FLOAT:  return "float";
    case ScriptDataType::STRING: return "string";
    case ScriptDataType::VEC2:   return "vec2";
    case ScriptDataType::VEC3:   return "vec3";
    case ScriptDataType::VEC4:   return "vec4";
    case ScriptDataType::QUAT:   return "quat";
    case ScriptDataType::ENTITY: return "entity";
    case ScriptDataType::COLOR:  return "color";
    case ScriptDataType::ANY:    return "any";
    }
    return "any";
}

ScriptDataType scriptDataTypeFromString(const std::string& str)
{
    if (str == "bool")   return ScriptDataType::BOOL;
    if (str == "int")    return ScriptDataType::INT;
    if (str == "float")  return ScriptDataType::FLOAT;
    if (str == "string") return ScriptDataType::STRING;
    if (str == "vec2")   return ScriptDataType::VEC2;
    if (str == "vec3")   return ScriptDataType::VEC3;
    if (str == "vec4")   return ScriptDataType::VEC4;
    if (str == "quat")   return ScriptDataType::QUAT;
    if (str == "entity") return ScriptDataType::ENTITY;
    if (str == "color")  return ScriptDataType::COLOR;
    return ScriptDataType::ANY;
}

// ---------------------------------------------------------------------------
// Type queries
// ---------------------------------------------------------------------------

ScriptDataType ScriptValue::getType() const
{
    if (m_isEntityId)
    {
        return ScriptDataType::ENTITY;
    }

    return std::visit([](const auto& val) -> ScriptDataType
    {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, bool>)        return ScriptDataType::BOOL;
        if constexpr (std::is_same_v<T, int32_t>)     return ScriptDataType::INT;
        if constexpr (std::is_same_v<T, float>)       return ScriptDataType::FLOAT;
        if constexpr (std::is_same_v<T, std::string>)  return ScriptDataType::STRING;
        if constexpr (std::is_same_v<T, glm::vec2>)   return ScriptDataType::VEC2;
        if constexpr (std::is_same_v<T, glm::vec3>)   return ScriptDataType::VEC3;
        if constexpr (std::is_same_v<T, glm::vec4>)   return ScriptDataType::VEC4;
        if constexpr (std::is_same_v<T, glm::quat>)   return ScriptDataType::QUAT;
        if constexpr (std::is_same_v<T, uint32_t>)    return ScriptDataType::ENTITY;
        return ScriptDataType::ANY;
    }, m_value);
}

bool ScriptValue::isType(ScriptDataType type) const
{
    if (type == ScriptDataType::ANY)
    {
        return true;
    }
    return getType() == type;
}

// ---------------------------------------------------------------------------
// Typed access
// ---------------------------------------------------------------------------

bool ScriptValue::asBool() const
{
    return std::visit([](const auto& val) -> bool
    {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, bool>)        return val;
        if constexpr (std::is_same_v<T, int32_t>)     return val != 0;
        if constexpr (std::is_same_v<T, float>)       return val != 0.0f;
        if constexpr (std::is_same_v<T, std::string>)  return !val.empty();
        if constexpr (std::is_same_v<T, uint32_t>)    return val != 0;
        return false;
    }, m_value);
}

int32_t ScriptValue::asInt() const
{
    return std::visit([](const auto& val) -> int32_t
    {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, bool>)        return val ? 1 : 0;
        if constexpr (std::is_same_v<T, int32_t>)     return val;
        if constexpr (std::is_same_v<T, float>)       return static_cast<int32_t>(val);
        if constexpr (std::is_same_v<T, uint32_t>)    return static_cast<int32_t>(val);
        return 0;
    }, m_value);
}

float ScriptValue::asFloat() const
{
    return std::visit([](const auto& val) -> float
    {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, bool>)        return val ? 1.0f : 0.0f;
        if constexpr (std::is_same_v<T, int32_t>)     return static_cast<float>(val);
        if constexpr (std::is_same_v<T, float>)       return val;
        if constexpr (std::is_same_v<T, uint32_t>)    return static_cast<float>(val);
        return 0.0f;
    }, m_value);
}

std::string ScriptValue::asString() const
{
    return std::visit([this](const auto& val) -> std::string
    {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, bool>)
            return val ? "true" : "false";
        if constexpr (std::is_same_v<T, int32_t>)
            return std::to_string(val);
        if constexpr (std::is_same_v<T, float>)
            return std::to_string(val);
        if constexpr (std::is_same_v<T, std::string>)
            return val;
        if constexpr (std::is_same_v<T, glm::vec2>)
            return "(" + std::to_string(val.x) + ", " + std::to_string(val.y) + ")";
        if constexpr (std::is_same_v<T, glm::vec3>)
            return "(" + std::to_string(val.x) + ", " + std::to_string(val.y) +
                   ", " + std::to_string(val.z) + ")";
        if constexpr (std::is_same_v<T, glm::vec4>)
            return "(" + std::to_string(val.x) + ", " + std::to_string(val.y) +
                   ", " + std::to_string(val.z) + ", " + std::to_string(val.w) + ")";
        if constexpr (std::is_same_v<T, glm::quat>)
            return "(w=" + std::to_string(val.w) + ", x=" + std::to_string(val.x) +
                   ", y=" + std::to_string(val.y) + ", z=" + std::to_string(val.z) + ")";
        if constexpr (std::is_same_v<T, uint32_t>)
        {
            if (m_isEntityId)
                return "Entity#" + std::to_string(val);
            return std::to_string(val);
        }
        return "";
    }, m_value);
}

glm::vec2 ScriptValue::asVec2() const
{
    if (auto* v = std::get_if<glm::vec2>(&m_value))
        return *v;
    if (auto* v = std::get_if<float>(&m_value))
        return glm::vec2(*v);
    return glm::vec2(0.0f);
}

glm::vec3 ScriptValue::asVec3() const
{
    if (auto* v = std::get_if<glm::vec3>(&m_value))
        return *v;
    if (auto* v = std::get_if<float>(&m_value))
        return glm::vec3(*v);
    return glm::vec3(0.0f);
}

glm::vec4 ScriptValue::asVec4() const
{
    if (auto* v = std::get_if<glm::vec4>(&m_value))
        return *v;
    if (auto* v = std::get_if<glm::vec3>(&m_value))
        return glm::vec4(*v, 1.0f);
    if (auto* v = std::get_if<float>(&m_value))
        return glm::vec4(*v);
    return glm::vec4(0.0f);
}

glm::quat ScriptValue::asQuat() const
{
    if (auto* v = std::get_if<glm::quat>(&m_value))
        return *v;
    return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

uint32_t ScriptValue::asEntityId() const
{
    if (auto* v = std::get_if<uint32_t>(&m_value))
        return *v;
    if (auto* v = std::get_if<int32_t>(&m_value))
        return static_cast<uint32_t>(*v);
    return 0;
}

// ---------------------------------------------------------------------------
// Conversion
// ---------------------------------------------------------------------------

ScriptValue ScriptValue::convertTo(ScriptDataType targetType) const
{
    switch (targetType)
    {
    case ScriptDataType::BOOL:   return ScriptValue(asBool());
    case ScriptDataType::INT:    return ScriptValue(asInt());
    case ScriptDataType::FLOAT:  return ScriptValue(asFloat());
    case ScriptDataType::STRING: return ScriptValue(asString());
    case ScriptDataType::VEC2:   return ScriptValue(asVec2());
    case ScriptDataType::VEC3:   return ScriptValue(asVec3());
    case ScriptDataType::VEC4:   return ScriptValue(asVec4());
    case ScriptDataType::QUAT:   return ScriptValue(asQuat());
    case ScriptDataType::ENTITY: return ScriptValue::entityId(asEntityId());
    case ScriptDataType::COLOR:  return ScriptValue(asVec4());
    case ScriptDataType::ANY:    return *this;
    }
    return *this;
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

bool ScriptValue::operator==(const ScriptValue& other) const
{
    if (getType() != other.getType())
    {
        return false;
    }
    return m_value == other.m_value;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

nlohmann::json ScriptValue::toJson() const
{
    nlohmann::json j;
    j["type"] = scriptDataTypeToString(getType());

    std::visit([&j, this](const auto& val)
    {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, bool>)
            j["value"] = val;
        else if constexpr (std::is_same_v<T, int32_t>)
            j["value"] = val;
        else if constexpr (std::is_same_v<T, float>)
            j["value"] = val;
        else if constexpr (std::is_same_v<T, std::string>)
            j["value"] = val;
        else if constexpr (std::is_same_v<T, glm::vec2>)
            j["value"] = {val.x, val.y};
        else if constexpr (std::is_same_v<T, glm::vec3>)
            j["value"] = {val.x, val.y, val.z};
        else if constexpr (std::is_same_v<T, glm::vec4>)
            j["value"] = {val.x, val.y, val.z, val.w};
        else if constexpr (std::is_same_v<T, glm::quat>)
            // AUDIT.md §M8: quat JSON order is [w, x, y, z] — NOT the
            // vec4 [x, y, z, w] order. Symmetric with the fromJson reader
            // below, which calls glm::quat(f[0], f[1], f[2], f[3]) = the
            // glm::quat(w, x, y, z) constructor. Keep in sync if changing.
            j["value"] = {val.w, val.x, val.y, val.z};
        else if constexpr (std::is_same_v<T, uint32_t>)
        {
            j["value"] = val;
            if (m_isEntityId)
                j["type"] = "entity";
        }
    }, m_value);

    return j;
}

ScriptValue ScriptValue::fromJson(const nlohmann::json& j)
{
    std::string typeStr = j.value("type", "float");
    ScriptDataType type = scriptDataTypeFromString(typeStr);

    if (!j.contains("value"))
    {
        return ScriptValue(0.0f);
    }

    const auto& val = j["value"];

    // Helper to safely extract N floats from a JSON array. Returns a default
    // value on malformed input (short array, wrong type) instead of throwing
    // into callers that may not have wrapped us in try/catch.
    auto readFloats = [&val](size_t n, std::array<float, 4>& out) -> bool
    {
        if (!val.is_array() || val.size() < n)
        {
            return false;
        }
        for (size_t i = 0; i < n; ++i)
        {
            if (!val[i].is_number())
            {
                return false;
            }
            out[i] = val[i].get<float>();
        }
        return true;
    };

    std::array<float, 4> f{0.0f, 0.0f, 0.0f, 0.0f};

    switch (type)
    {
    case ScriptDataType::BOOL:
        return val.is_boolean() ? ScriptValue(val.get<bool>())
                                 : ScriptValue(false);
    case ScriptDataType::INT:
        return val.is_number() ? ScriptValue(val.get<int32_t>())
                                : ScriptValue(int32_t{0});
    case ScriptDataType::FLOAT:
        return val.is_number() ? ScriptValue(val.get<float>())
                                : ScriptValue(0.0f);
    case ScriptDataType::STRING:
        return val.is_string() ? ScriptValue(val.get<std::string>())
                                : ScriptValue(std::string{});
    case ScriptDataType::VEC2:
        return readFloats(2, f) ? ScriptValue(glm::vec2(f[0], f[1]))
                                 : ScriptValue(glm::vec2(0.0f));
    case ScriptDataType::VEC3:
        return readFloats(3, f) ? ScriptValue(glm::vec3(f[0], f[1], f[2]))
                                 : ScriptValue(glm::vec3(0.0f));
    case ScriptDataType::VEC4:
    case ScriptDataType::COLOR:
        return readFloats(4, f) ? ScriptValue(glm::vec4(f[0], f[1], f[2], f[3]))
                                 : ScriptValue(glm::vec4(0.0f));
    case ScriptDataType::QUAT:
        // AUDIT.md §M8: JSON order is [w, x, y, z] — matches the glm::quat
        // constructor signature and the toJson writer above. Keep in sync.
        return readFloats(4, f) ? ScriptValue(glm::quat(f[0], f[1], f[2], f[3]))
                                 : ScriptValue(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    case ScriptDataType::ENTITY:
        return val.is_number_unsigned()
                   ? ScriptValue::entityId(val.get<uint32_t>())
                   : ScriptValue::entityId(0);
    case ScriptDataType::ANY:
        if (val.is_boolean()) return ScriptValue(val.get<bool>());
        if (val.is_number_integer()) return ScriptValue(val.get<int32_t>());
        if (val.is_number_float()) return ScriptValue(val.get<float>());
        if (val.is_string()) return ScriptValue(val.get<std::string>());
        return ScriptValue(0.0f);
    }

    return ScriptValue(0.0f);
}

} // namespace Vestige
