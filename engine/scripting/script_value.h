/// @file script_value.h
/// @brief Type-erased value type for visual scripting data pins.
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <variant>

namespace Vestige
{

/// @brief Data types that can flow through script data pins.
enum class ScriptDataType
{
    BOOL,
    INT,
    FLOAT,
    STRING,
    VEC2,
    VEC3,
    VEC4,
    QUAT,
    ENTITY, ///< Entity ID (uint32_t)
    COLOR,  ///< RGBA color (vec4)
    ANY     ///< Wildcard, accepts any type
};

/// @brief A type-erased value that can hold any script data type.
///
/// Internally uses std::variant for small types. Supports conversion between
/// compatible types (e.g. int -> float, float -> string).
class ScriptValue
{
public:
    using Variant = std::variant<
        bool,
        int32_t,
        float,
        std::string,
        glm::vec2,
        glm::vec3,
        glm::vec4,
        glm::quat,
        uint32_t  // Entity ID
    >;

    ScriptValue() = default;
    ScriptValue(bool v)               : m_value(v) {}
    ScriptValue(int32_t v)            : m_value(v) {}
    ScriptValue(float v)              : m_value(v) {}
    ScriptValue(const std::string& v) : m_value(v) {}
    ScriptValue(const char* v)        : m_value(std::string(v)) {}
    ScriptValue(glm::vec2 v)          : m_value(v) {}
    ScriptValue(glm::vec3 v)          : m_value(v) {}
    ScriptValue(glm::vec4 v)          : m_value(v) {}
    ScriptValue(glm::quat v)          : m_value(v) {}

    /// @brief Create a ScriptValue holding an entity ID.
    static ScriptValue entityId(uint32_t id)
    {
        ScriptValue sv;
        sv.m_value = id;
        sv.m_isEntityId = true;
        return sv;
    }

    // -- Type queries --

    ScriptDataType getType() const;
    bool isType(ScriptDataType type) const;

    // -- Typed access (returns default if wrong type) --

    bool        asBool() const;
    int32_t     asInt() const;
    float       asFloat() const;
    std::string asString() const;
    glm::vec2   asVec2() const;
    glm::vec3   asVec3() const;
    glm::vec4   asVec4() const;
    glm::quat   asQuat() const;
    uint32_t    asEntityId() const;

    // -- Conversion --

    /// @brief Attempt to convert this value to the target type.
    /// @return Converted value, or a default-constructed value if conversion fails.
    ScriptValue convertTo(ScriptDataType targetType) const;

    // -- Comparison --

    bool operator==(const ScriptValue& other) const;
    bool operator!=(const ScriptValue& other) const { return !(*this == other); }

    // -- Serialization --

    nlohmann::json toJson() const;
    static ScriptValue fromJson(const nlohmann::json& j);

    // -- Internal --

    const Variant& variant() const { return m_value; }

private:
    Variant m_value = false;
    bool m_isEntityId = false; ///< Disambiguates uint32_t as entity ID vs int
};

// -- Enum string conversions --
const char* scriptDataTypeToString(ScriptDataType type);
ScriptDataType scriptDataTypeFromString(const std::string& str);

} // namespace Vestige
