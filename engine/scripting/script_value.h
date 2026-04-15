// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

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
    // Single-arg constructors are explicit so values only enter the variant
    // via deliberate `ScriptValue(x)` calls — no implicit conversions from
    // raw primitives at call sites (CODING_STANDARDS §7; audit M12).
    explicit ScriptValue(bool v)               : m_value(v) {}
    explicit ScriptValue(int32_t v)            : m_value(v) {}
    explicit ScriptValue(float v)              : m_value(v) {}
    explicit ScriptValue(const std::string& v) : m_value(v) {}
    explicit ScriptValue(const char* v)        : m_value(std::string(v)) {}
    explicit ScriptValue(glm::vec2 v)          : m_value(v) {}
    explicit ScriptValue(glm::vec3 v)          : m_value(v) {}
    explicit ScriptValue(glm::vec4 v)          : m_value(v) {}
    explicit ScriptValue(glm::quat v)          : m_value(v) {}

    /// @brief Create a ScriptValue holding an entity ID.
    static ScriptValue entityId(uint32_t id)
    {
        ScriptValue sv;
        sv.m_value = id;
        sv.m_isEntityId = true;
        return sv;
    }

    // -- Type queries --

    /// @brief Returns the canonical ScriptDataType of the held value.
    ScriptDataType getType() const;
    /// @brief True if the held value matches the given type (accounts for
    /// entity-ID tagging on uint32_t).
    bool isType(ScriptDataType type) const;

    // -- Typed access (returns default if wrong type) --

    /// @brief Coerce to bool. Returns false for non-bool types; non-zero
    /// numeric types return true.
    bool        asBool() const;
    /// @brief Coerce to int32. Returns truncated integer for float, parsed
    /// value for numeric strings, 0 otherwise.
    int32_t     asInt() const;
    /// @brief Coerce to float. Returns 0.0f for non-numeric types.
    float       asFloat() const;
    /// @brief Stringify. Returns a human-readable representation for every
    /// held type. Vectors use "(x, y, z)" form.
    std::string asString() const;
    /// @brief Coerce to vec2. Returns (0,0) if not a vec2.
    glm::vec2   asVec2() const;
    /// @brief Coerce to vec3. Returns (0,0,0) if not a vec3.
    glm::vec3   asVec3() const;
    /// @brief Coerce to vec4. Returns (0,0,0,0) if not a vec4/color.
    glm::vec4   asVec4() const;
    /// @brief Coerce to quaternion. Returns identity if not a quat.
    glm::quat   asQuat() const;
    /// @brief Get the held entity ID. Returns 0 if this value is not tagged
    /// as an entity.
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
