// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file codegen_cpp.cpp
/// @brief C++ code generator implementation.
#include "formula/codegen_cpp.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Expression emission
// ---------------------------------------------------------------------------

std::string CodegenCpp::emitExpression(const ExprNode& node,
                                        const std::map<std::string, float>& coefficients)
{
    switch (node.type)
    {
    case ExprNodeType::LITERAL:
        return floatLiteral(node.value);

    case ExprNodeType::VARIABLE:
    {
        auto it = coefficients.find(node.name);
        if (it != coefficients.end())
        {
            return floatLiteral(it->second);
        }
        return node.name;
    }

    case ExprNodeType::BINARY_OP:
    {
        if (node.children.size() < 2)
            return "0.0f";
        const auto& left = *node.children[0];
        const auto& right = *node.children[1];

        std::string l = emitExpression(left, coefficients);
        std::string r = emitExpression(right, coefficients);

        if (node.op == "+" || node.op == "-" || node.op == "*")
        {
            return "(" + l + " " + node.op + " " + r + ")";
        }
        // AUDIT.md §H12 / FIXPLAN E4: emit safe division to match evaluator
        // semantics (divide-by-zero → 0, not NaN/inf). Without this, the
        // LM fitter validates coefficients under safe math, then the emitted
        // header crashes on degenerate inputs with different semantics.
        if (node.op == "/")
        {
            return "Vestige::SafeMath::safeDiv(" + l + ", " + r + ")";
        }
        if (node.op == "pow")
        {
            return "Vestige::SafeMath::safePow(" + l + ", " + r + ")";
        }
        if (node.op == "min")
        {
            return "std::min(" + l + ", " + r + ")";
        }
        if (node.op == "max")
        {
            return "std::max(" + l + ", " + r + ")";
        }
        if (node.op == "mod")
        {
            return "std::fmod(" + l + ", " + r + ")";
        }
        if (node.op == "dot")
        {
            return "glm::dot(" + l + ", " + r + ")";
        }
        // Validated at ExprNode construction (AUDIT.md §H11); reaching here
        // means the tree was mutated post-construction with a disallowed op.
        throw std::runtime_error(
            "CodegenCpp: unknown binary op '" + node.op +
            "' — not in allowlist (AUDIT.md §H11)");
    }

    case ExprNodeType::UNARY_OP:
    {
        if (node.children.empty())
            return "0.0f";
        std::string arg = emitExpression(*node.children[0], coefficients);

        if (node.op == "negate")
            return "(-" + arg + ")";
        if (node.op == "saturate")
            return "std::clamp(" + arg + ", 0.0f, 1.0f)";
        if (node.op == "sign")
            return "(" + arg + " > 0.0f ? 1.0f : (" + arg + " < 0.0f ? -1.0f : 0.0f))";

        // Safe-math wrappers for ops that the evaluator guards against
        // degenerate inputs (AUDIT.md §H12 / FIXPLAN E4).
        if (node.op == "sqrt")
            return "Vestige::SafeMath::safeSqrt(" + arg + ")";
        if (node.op == "log")
            return "Vestige::SafeMath::safeLog(" + arg + ")";

        // Standard math functions that need no domain guards.
        static const std::unordered_set<std::string> kStdMathFns = {
            "sin", "cos", "abs", "exp", "floor", "ceil"
        };
        if (kStdMathFns.count(node.op) == 0)
        {
            throw std::runtime_error(
                "CodegenCpp: unknown unary op '" + node.op +
                "' — not in allowlist (AUDIT.md §H11)");
        }
        return "std::" + node.op + "(" + arg + ")";
    }

    case ExprNodeType::CONDITIONAL:
    {
        if (node.children.size() < 3)
            return "0.0f";
        std::string cond = emitExpression(*node.children[0], coefficients);
        std::string thenExpr = emitExpression(*node.children[1], coefficients);
        std::string elseExpr = emitExpression(*node.children[2], coefficients);
        return "(" + cond + " != 0.0f ? " + thenExpr + " : " + elseExpr + ")";
    }
    }

    return "0.0f";
}

// ---------------------------------------------------------------------------
// Function generation
// ---------------------------------------------------------------------------

std::string CodegenCpp::generateFunction(const FormulaDefinition& formula,
                                          QualityTier tier)
{
    const ExprNode* expr = formula.getExpression(tier);
    if (!expr)
        return "// No expression for formula: " + formula.name + "\n";

    std::ostringstream out;

    // Comment with formula description
    out << "/// @brief " << formula.description << "\n";
    if (!formula.source.empty())
        out << "/// @note Source: " << formula.source << "\n";

    // Function signature
    std::string funcName = toCppFunctionName(formula.name);
    std::string retType = toCppType(formula.output.type);
    out << "inline " << retType << " " << funcName << "(";

    bool first = true;
    for (const auto& input : formula.inputs)
    {
        if (!first)
            out << ", ";
        out << toCppParamType(input.type) << " " << input.name;
        first = false;
    }
    out << ")\n{\n";

    // Function body — single return statement with inlined expression
    std::string body = emitExpression(*expr, formula.coefficients);
    out << "    return " << body << ";\n";
    out << "}\n";

    return out.str();
}

std::string CodegenCpp::generateHeader(const std::vector<const FormulaDefinition*>& formulas,
                                        QualityTier tier)
{
    std::ostringstream out;

    out << "/// @file formulas.h\n";
    out << "/// @brief Generated formula functions — DO NOT EDIT.\n";
    out << "/// Generated by FormulaCompiler from the Vestige formula library.\n";
    out << "#pragma once\n\n";
    out << "#include <algorithm>\n";
    out << "#include <cmath>\n";
    out << "#include <glm/glm.hpp>\n";
    // AUDIT.md §H12 / FIXPLAN E4: safe-math helpers keep emitted functions
    // behaviourally identical to the tree-walking evaluator the LM curve
    // fitter validates coefficients against.
    out << "#include \"formula/safe_math.h\"\n\n";
    out << "namespace Vestige::Formulas\n{\n\n";

    for (const auto* formula : formulas)
    {
        if (!formula)
            continue;
        out << generateFunction(*formula, tier);
        out << "\n";
    }

    out << "} // namespace Vestige::Formulas\n";
    return out.str();
}

// ---------------------------------------------------------------------------
// Name conversion: snake_case -> camelCase
// ---------------------------------------------------------------------------

std::string CodegenCpp::toCppFunctionName(const std::string& formulaName)
{
    std::string result;
    result.reserve(formulaName.size());

    bool capitalizeNext = false;
    for (char c : formulaName)
    {
        if (c == '_')
        {
            capitalizeNext = true;
        }
        else
        {
            if (capitalizeNext)
            {
                result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                capitalizeNext = false;
            }
            else
            {
                result += c;
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Type conversions
// ---------------------------------------------------------------------------

std::string CodegenCpp::toCppType(FormulaValueType type)
{
    switch (type)
    {
    case FormulaValueType::FLOAT: return "float";
    case FormulaValueType::VEC2:  return "glm::vec2";
    case FormulaValueType::VEC3:  return "glm::vec3";
    case FormulaValueType::VEC4:  return "glm::vec4";
    }
    return "float";
}

std::string CodegenCpp::toCppParamType(FormulaValueType type)
{
    switch (type)
    {
    case FormulaValueType::FLOAT: return "float";
    case FormulaValueType::VEC2:  return "const glm::vec2&";
    case FormulaValueType::VEC3:  return "const glm::vec3&";
    case FormulaValueType::VEC4:  return "const glm::vec4&";
    }
    return "float";
}

// ---------------------------------------------------------------------------
// Float literal formatting
// ---------------------------------------------------------------------------

std::string CodegenCpp::floatLiteral(float val)
{
    // Handle special values
    if (std::isnan(val))
        return "std::numeric_limits<float>::quiet_NaN()";
    if (std::isinf(val))
        return val > 0 ? "std::numeric_limits<float>::infinity()"
                       : "(-std::numeric_limits<float>::infinity())";

    std::ostringstream oss;

    // Use scientific notation for very small or very large values
    if (val != 0.0f && (std::abs(val) < 0.001f || std::abs(val) >= 1e7f))
    {
        oss << std::scientific << std::setprecision(6) << val << "f";
    }
    else
    {
        // Fixed notation with enough precision
        oss << std::setprecision(8) << val;

        std::string s = oss.str();
        // Ensure there's a decimal point
        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
        {
            s += ".0";
        }
        s += "f";
        return s;
    }

    return oss.str();
}

} // namespace Vestige
