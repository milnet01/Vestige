// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_script_value.cpp
/// @brief Unit tests for ScriptValue, script enums, and variable definitions.
#include "scripting/script_value.h"
#include "scripting/script_graph.h"

#include <gtest/gtest.h>

#include <limits>

using namespace Vestige;

TEST(ScriptValue, DefaultIsBool)
{
    ScriptValue v;
    EXPECT_EQ(v.getType(), ScriptDataType::BOOL);
    EXPECT_FALSE(v.asBool());
}

TEST(ScriptValue, BoolType)
{
    ScriptValue v(true);
    EXPECT_EQ(v.getType(), ScriptDataType::BOOL);
    EXPECT_TRUE(v.asBool());
    EXPECT_EQ(v.asInt(), 1);
    EXPECT_FLOAT_EQ(v.asFloat(), 1.0f);
    EXPECT_EQ(v.asString(), "true");
}

TEST(ScriptValue, IntType)
{
    ScriptValue v(42);
    EXPECT_EQ(v.getType(), ScriptDataType::INT);
    EXPECT_EQ(v.asInt(), 42);
    EXPECT_FLOAT_EQ(v.asFloat(), 42.0f);
    EXPECT_TRUE(v.asBool());
}

TEST(ScriptValue, FloatType)
{
    ScriptValue v(3.14f);
    EXPECT_EQ(v.getType(), ScriptDataType::FLOAT);
    EXPECT_FLOAT_EQ(v.asFloat(), 3.14f);
    EXPECT_EQ(v.asInt(), 3);
    EXPECT_TRUE(v.asBool());
}

// ---------------------------------------------------------------------------
// AUDIT Sc4 — asInt() must clamp out-of-range floats and map NaN to 0
// instead of UB-casting.
// ---------------------------------------------------------------------------
TEST(ScriptValue, FloatToIntClampSaturatesAtMax_Sc4)
{
    ScriptValue v(1e30f);
    EXPECT_EQ(v.asInt(), std::numeric_limits<int32_t>::max());
}

TEST(ScriptValue, FloatToIntClampSaturatesAtMin_Sc4)
{
    ScriptValue v(-1e30f);
    EXPECT_EQ(v.asInt(), std::numeric_limits<int32_t>::min());
}

TEST(ScriptValue, FloatToIntPositiveInfinitySaturates_Sc4)
{
    ScriptValue v(std::numeric_limits<float>::infinity());
    EXPECT_EQ(v.asInt(), std::numeric_limits<int32_t>::max());
}

TEST(ScriptValue, FloatToIntNegativeInfinitySaturates_Sc4)
{
    ScriptValue v(-std::numeric_limits<float>::infinity());
    EXPECT_EQ(v.asInt(), std::numeric_limits<int32_t>::min());
}

TEST(ScriptValue, FloatToIntNaNReturnsZero_Sc4)
{
    ScriptValue v(std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(v.asInt(), 0);
}

TEST(ScriptValue, UintAboveIntMaxClamps_Sc4)
{
    auto v = ScriptValue::entityId(0xFFFFFFFFu);
    EXPECT_EQ(v.asInt(), std::numeric_limits<int32_t>::max());
}

TEST(ScriptValue, StringType)
{
    ScriptValue v(std::string("hello"));
    EXPECT_EQ(v.getType(), ScriptDataType::STRING);
    EXPECT_EQ(v.asString(), "hello");
    EXPECT_TRUE(v.asBool()); // non-empty string is truthy
}

TEST(ScriptValue, EmptyStringFalsy)
{
    ScriptValue v(std::string(""));
    EXPECT_FALSE(v.asBool());
}

TEST(ScriptValue, Vec3Type)
{
    ScriptValue v(glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(v.getType(), ScriptDataType::VEC3);
    auto vec = v.asVec3();
    EXPECT_FLOAT_EQ(vec.x, 1.0f);
    EXPECT_FLOAT_EQ(vec.y, 2.0f);
    EXPECT_FLOAT_EQ(vec.z, 3.0f);
}

TEST(ScriptValue, EntityIdType)
{
    auto v = ScriptValue::entityId(42);
    EXPECT_EQ(v.getType(), ScriptDataType::ENTITY);
    EXPECT_EQ(v.asEntityId(), 42u);
    EXPECT_EQ(v.asString(), "Entity#42");
}

TEST(ScriptValue, ConvertIntToFloat)
{
    ScriptValue v(42);
    auto converted = v.convertTo(ScriptDataType::FLOAT);
    EXPECT_EQ(converted.getType(), ScriptDataType::FLOAT);
    EXPECT_FLOAT_EQ(converted.asFloat(), 42.0f);
}

TEST(ScriptValue, ConvertFloatToString)
{
    ScriptValue v(3.14f);
    auto converted = v.convertTo(ScriptDataType::STRING);
    EXPECT_EQ(converted.getType(), ScriptDataType::STRING);
    // std::to_string(3.14f) produces "3.140000"; assert the value actually
    // round-trips through the leading digits rather than merely being non-empty.
    EXPECT_EQ(converted.asString().substr(0, 4), "3.14");
}

TEST(ScriptValue, Equality)
{
    EXPECT_EQ(ScriptValue(42), ScriptValue(42));
    EXPECT_NE(ScriptValue(42), ScriptValue(43));
    EXPECT_NE(ScriptValue(42), ScriptValue(42.0f)); // Different types
    EXPECT_EQ(ScriptValue(true), ScriptValue(true));
    EXPECT_NE(ScriptValue(true), ScriptValue(false));
}

TEST(ScriptValue, JsonRoundTrip)
{
    std::vector<ScriptValue> values = {
        ScriptValue(true),
        ScriptValue(42),
        ScriptValue(3.14f),
        ScriptValue(std::string("hello")),
        ScriptValue(glm::vec3(1.0f, 2.0f, 3.0f)),
        ScriptValue::entityId(99),
    };

    for (const auto& original : values)
    {
        auto json = original.toJson();
        auto restored = ScriptValue::fromJson(json);
        EXPECT_EQ(original.getType(), restored.getType())
            << "Type mismatch for " << original.asString();
    }
}

TEST(VariableDef, JsonRoundTrip)
{
    VariableDef original;
    original.name = "playerHealth";
    original.dataType = ScriptDataType::FLOAT;
    original.defaultValue = ScriptValue(100.0f);
    original.scope = VariableScope::ENTITY;

    auto json = original.toJson();
    auto restored = VariableDef::fromJson(json);

    EXPECT_EQ(restored.name, "playerHealth");
    EXPECT_EQ(restored.dataType, ScriptDataType::FLOAT);
    EXPECT_FLOAT_EQ(restored.defaultValue.asFloat(), 100.0f);
    EXPECT_EQ(restored.scope, VariableScope::ENTITY);
}

TEST(ScriptEnums, DataTypeRoundTrip)
{
    auto types = {ScriptDataType::BOOL, ScriptDataType::INT, ScriptDataType::FLOAT,
                  ScriptDataType::STRING, ScriptDataType::VEC2, ScriptDataType::VEC3,
                  ScriptDataType::VEC4, ScriptDataType::QUAT, ScriptDataType::ENTITY,
                  ScriptDataType::COLOR, ScriptDataType::ANY};

    for (auto type : types)
    {
        auto str = scriptDataTypeToString(type);
        auto restored = scriptDataTypeFromString(str);
        EXPECT_EQ(type, restored) << "Failed for: " << str;
    }
}

TEST(ScriptEnums, VariableScopeRoundTrip)
{
    auto scopes = {VariableScope::FLOW, VariableScope::GRAPH, VariableScope::ENTITY,
                   VariableScope::SCENE, VariableScope::APPLICATION,
                   VariableScope::SAVED};

    for (auto scope : scopes)
    {
        auto str = variableScopeToString(scope);
        auto restored = variableScopeFromString(str);
        EXPECT_EQ(scope, restored) << "Failed for: " << str;
    }
}

TEST(ScriptValueJson, Vec3ShortArrayReturnsZero)
{
    nlohmann::json j;
    j["type"] = "vec3";
    j["value"] = nlohmann::json::array({1.0f, 2.0f}); // missing third element
    auto v = ScriptValue::fromJson(j);
    EXPECT_EQ(v.asVec3(), glm::vec3(0.0f));
}

TEST(ScriptValueJson, QuatWrongTypeReturnsIdentity)
{
    nlohmann::json j;
    j["type"] = "quat";
    j["value"] = "not-an-array";
    auto v = ScriptValue::fromJson(j);
    auto q = v.asQuat();
    EXPECT_FLOAT_EQ(q.w, 1.0f);
    EXPECT_FLOAT_EQ(q.x, 0.0f);
    EXPECT_FLOAT_EQ(q.y, 0.0f);
    EXPECT_FLOAT_EQ(q.z, 0.0f);
}

TEST(ScriptValueJson, BoolWrongTypeReturnsFalse)
{
    nlohmann::json j;
    j["type"] = "bool";
    j["value"] = "not-a-bool";
    EXPECT_FALSE(ScriptValue::fromJson(j).asBool());
}
