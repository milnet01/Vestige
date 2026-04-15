// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file codegen_glsl.cpp
/// @brief GLSL code generator implementation.
#include "formula/codegen_glsl.h"
#include "formula/safe_math.h"

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

std::string CodegenGlsl::emitExpression(const ExprNode& node,
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
            return "0.0";
        const auto& left = *node.children[0];
        const auto& right = *node.children[1];

        std::string l = emitExpression(left, coefficients);
        std::string r = emitExpression(right, coefficients);

        if (node.op == "+" || node.op == "-" || node.op == "*")
        {
            return "(" + l + " " + node.op + " " + r + ")";
        }
        // AUDIT.md §H12 / FIXPLAN E4: safe division to match evaluator.
        if (node.op == "/")
        {
            return "safeDiv(" + l + ", " + r + ")";
        }
        if (node.op == "pow")
        {
            return "pow(" + l + ", " + r + ")";
        }
        if (node.op == "min")
        {
            return "min(" + l + ", " + r + ")";
        }
        if (node.op == "max")
        {
            return "max(" + l + ", " + r + ")";
        }
        if (node.op == "mod")
        {
            return "mod(" + l + ", " + r + ")";
        }
        if (node.op == "dot")
        {
            return "dot(" + l + ", " + r + ")";
        }
        // Validated at ExprNode construction (AUDIT.md §H11); reaching here
        // means the tree was mutated post-construction with a disallowed op.
        throw std::runtime_error(
            "CodegenGlsl: unknown binary op '" + node.op +
            "' — not in allowlist (AUDIT.md §H11)");
    }

    case ExprNodeType::UNARY_OP:
    {
        if (node.children.empty())
            return "0.0";
        std::string arg = emitExpression(*node.children[0], coefficients);

        if (node.op == "negate")
            return "(-" + arg + ")";
        if (node.op == "saturate")
            return "clamp(" + arg + ", 0.0, 1.0)";
        if (node.op == "sign")
            return "sign(" + arg + ")";

        // AUDIT.md §H12 / FIXPLAN E4: safe-math wrappers prefixed from the
        // generated prelude (see CodegenGlsl::generateFile).
        if (node.op == "sqrt")
            return "safeSqrt(" + arg + ")";
        if (node.op == "log")
            return "safeLog(" + arg + ")";

        // Bare GLSL built-ins that need no domain guards.
        static const std::unordered_set<std::string> kBuiltins = {
            "sin", "cos", "abs", "exp", "floor", "ceil"
        };
        if (kBuiltins.count(node.op) == 0)
        {
            throw std::runtime_error(
                "CodegenGlsl: unknown unary op '" + node.op +
                "' — not in allowlist (AUDIT.md §H11)");
        }
        return node.op + "(" + arg + ")";
    }

    case ExprNodeType::CONDITIONAL:
    {
        if (node.children.size() < 3)
            return "0.0";
        std::string cond = emitExpression(*node.children[0], coefficients);
        std::string thenExpr = emitExpression(*node.children[1], coefficients);
        std::string elseExpr = emitExpression(*node.children[2], coefficients);
        return "(" + cond + " != 0.0 ? " + thenExpr + " : " + elseExpr + ")";
    }
    }

    return "0.0";
}

// ---------------------------------------------------------------------------
// Function generation
// ---------------------------------------------------------------------------

std::string CodegenGlsl::generateFunction(const FormulaDefinition& formula,
                                            QualityTier tier)
{
    const ExprNode* expr = formula.getExpression(tier);
    if (!expr)
        return "// No expression for formula: " + formula.name + "\n";

    std::ostringstream out;

    // Comment with formula description
    out << "// " << formula.description << "\n";

    // Function signature
    std::string funcName = toGlslFunctionName(formula.name);
    std::string retType = toGlslType(formula.output.type);
    out << retType << " " << funcName << "(";

    bool first = true;
    for (const auto& input : formula.inputs)
    {
        if (!first)
            out << ", ";
        out << toGlslType(input.type) << " " << input.name;
        first = false;
    }
    out << ")\n{\n";

    // Function body
    std::string body = emitExpression(*expr, formula.coefficients);
    out << "    return " << body << ";\n";
    out << "}\n";

    return out.str();
}

std::string CodegenGlsl::generateFile(const std::vector<const FormulaDefinition*>& formulas,
                                       QualityTier tier)
{
    std::ostringstream out;

    out << "// Generated formula functions — DO NOT EDIT.\n";
    out << "// Generated by FormulaCompiler from the Vestige formula library.\n\n";

    // AUDIT.md §H12 / FIXPLAN E4: prelude defines safeDiv/safeSqrt/safeLog
    // matching engine/formula/safe_math.h so the emitted shader produces
    // identical output to the tree-walking evaluator the LM fitter uses.
    out << SafeMath::glslPrelude() << "\n";

    for (const auto* formula : formulas)
    {
        if (!formula)
            continue;
        out << generateFunction(*formula, tier);
        out << "\n";
    }

    return out.str();
}

std::string CodegenGlsl::safeMathPrelude()
{
    return SafeMath::glslPrelude();
}

// ---------------------------------------------------------------------------
// Name conversion: snake_case -> camelCase (same logic as C++)
// ---------------------------------------------------------------------------

std::string CodegenGlsl::toGlslFunctionName(const std::string& formulaName)
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

std::string CodegenGlsl::toGlslType(FormulaValueType type)
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

// ---------------------------------------------------------------------------
// Float literal formatting (no 'f' suffix for GLSL)
// ---------------------------------------------------------------------------

std::string CodegenGlsl::floatLiteral(float val)
{
    if (std::isnan(val))
        return "(0.0 / 0.0)";
    if (std::isinf(val))
        return val > 0 ? "(1.0 / 0.0)" : "(-1.0 / 0.0)";

    std::ostringstream oss;

    if (val != 0.0f && (std::abs(val) < 0.001f || std::abs(val) >= 1e7f))
    {
        oss << std::scientific << std::setprecision(6) << val;
    }
    else
    {
        oss << std::setprecision(8) << val;

        std::string s = oss.str();
        // Ensure there's a decimal point (GLSL requires it for float literals)
        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
        {
            s += ".0";
        }
        return s;
    }

    return oss.str();
}

} // namespace Vestige
