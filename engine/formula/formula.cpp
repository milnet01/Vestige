// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file formula.cpp
/// @brief FormulaDefinition implementation with JSON serialization.
#include "formula/formula.h"

namespace Vestige
{

// ---------------------------------------------------------------------------
// Enum string conversion
// ---------------------------------------------------------------------------

const char* qualityTierToString(QualityTier tier)
{
    switch (tier)
    {
    case QualityTier::FULL:        return "full";
    case QualityTier::APPROXIMATE: return "approximate";
    case QualityTier::LUT:         return "lut";
    }
    return "full";
}

QualityTier qualityTierFromString(const std::string& str)
{
    if (str == "approximate") return QualityTier::APPROXIMATE;
    if (str == "lut")         return QualityTier::LUT;
    return QualityTier::FULL;
}

const char* formulaValueTypeToString(FormulaValueType type)
{
    switch (type)
    {
    case FormulaValueType::FLOAT: return "float";
    case FormulaValueType::VEC2:  return "vec2";
    case FormulaValueType::VEC3:  return "vec3";
    case FormulaValueType::VEC4:  return "vec4";
    }
    return "float";
}

FormulaValueType formulaValueTypeFromString(const std::string& str)
{
    if (str == "vec2") return FormulaValueType::VEC2;
    if (str == "vec3") return FormulaValueType::VEC3;
    if (str == "vec4") return FormulaValueType::VEC4;
    return FormulaValueType::FLOAT;
}

// ---------------------------------------------------------------------------
// FormulaDefinition
// ---------------------------------------------------------------------------

FormulaDefinition FormulaDefinition::clone() const
{
    FormulaDefinition copy;
    copy.name = name;
    copy.category = category;
    copy.description = description;
    copy.inputs = inputs;
    copy.output = output;
    copy.coefficients = coefficients;
    copy.source = source;
    copy.accuracy = accuracy;

    for (const auto& [tier, expr] : expressions)
    {
        if (expr)
        {
            copy.expressions[tier] = expr->clone();
        }
    }

    return copy;
}

bool FormulaDefinition::hasTier(QualityTier tier) const
{
    return expressions.count(tier) > 0;
}

const ExprNode* FormulaDefinition::getExpression(QualityTier tier) const
{
    auto it = expressions.find(tier);
    if (it != expressions.end() && it->second)
    {
        return it->second.get();
    }

    // Fall back to FULL
    if (tier != QualityTier::FULL)
    {
        it = expressions.find(QualityTier::FULL);
        if (it != expressions.end() && it->second)
        {
            return it->second.get();
        }
    }

    return nullptr;
}

// ---------------------------------------------------------------------------
// JSON serialization
// ---------------------------------------------------------------------------

nlohmann::json FormulaDefinition::toJson() const
{
    nlohmann::json j;
    j["name"] = name;
    j["category"] = category;
    j["description"] = description;

    // Inputs
    nlohmann::json inputsArr = nlohmann::json::array();
    for (const auto& inp : inputs)
    {
        nlohmann::json ij;
        ij["name"] = inp.name;
        ij["type"] = formulaValueTypeToString(inp.type);
        if (!inp.unit.empty())
        {
            ij["unit"] = inp.unit;
        }
        if (inp.defaultValue != 0.0f)
        {
            ij["default"] = inp.defaultValue;
        }
        inputsArr.push_back(ij);
    }
    j["inputs"] = inputsArr;

    // Output
    nlohmann::json oj;
    oj["type"] = formulaValueTypeToString(output.type);
    if (!output.unit.empty())
    {
        oj["unit"] = output.unit;
    }
    j["output"] = oj;

    // Expressions per tier
    for (const auto& [tier, expr] : expressions)
    {
        if (expr)
        {
            j["expressions"][qualityTierToString(tier)] = expr->toJson();
        }
    }

    // Coefficients
    if (!coefficients.empty())
    {
        j["coefficients"] = coefficients;
    }

    if (!source.empty())
    {
        j["source"] = source;
    }
    if (accuracy < 1.0f)
    {
        j["accuracy"] = accuracy;
    }

    return j;
}

FormulaDefinition FormulaDefinition::fromJson(const nlohmann::json& j)
{
    FormulaDefinition def;
    def.name = j.value("name", "");
    def.category = j.value("category", "");
    def.description = j.value("description", "");

    // Inputs
    if (j.contains("inputs") && j["inputs"].is_array())
    {
        for (const auto& ij : j["inputs"])
        {
            FormulaInput inp;
            inp.name = ij.value("name", "");
            inp.type = formulaValueTypeFromString(ij.value("type", "float"));
            inp.unit = ij.value("unit", "");
            inp.defaultValue = ij.value("default", 0.0f);
            def.inputs.push_back(inp);
        }
    }

    // Output
    if (j.contains("output") && j["output"].is_object())
    {
        def.output.type = formulaValueTypeFromString(j["output"].value("type", "float"));
        def.output.unit = j["output"].value("unit", "");
    }

    // Expressions — support both "expression" (single) and "expressions" (per-tier)
    if (j.contains("expressions") && j["expressions"].is_object())
    {
        for (const auto& [key, val] : j["expressions"].items())
        {
            def.expressions[qualityTierFromString(key)] = ExprNode::fromJson(val);
        }
    }
    else if (j.contains("expression"))
    {
        // Single expression defaults to FULL tier
        def.expressions[QualityTier::FULL] = ExprNode::fromJson(j["expression"]);
    }

    // Coefficients
    if (j.contains("coefficients") && j["coefficients"].is_object())
    {
        for (const auto& [key, val] : j["coefficients"].items())
        {
            def.coefficients[key] = val.get<float>();
        }
    }

    def.source = j.value("source", "");
    def.accuracy = j.value("accuracy", 1.0f);

    return def;
}

} // namespace Vestige
