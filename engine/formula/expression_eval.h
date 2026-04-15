// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file expression_eval.h
/// @brief Tree-walking expression evaluator for tool-time formula evaluation.
///
/// This evaluator is used at tool time (workbench, editor preview) — NOT at
/// runtime. Runtime formulas are compiled to native C++ / GLSL by the
/// FormulaCompiler (FP-3). The evaluator handles scalar expressions only;
/// vec3 formulas are evaluated component-wise by the caller.
#pragma once

#include "formula/expression.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Vestige
{

struct FormulaInput;  // Forward declaration from formula.h

/// @brief Evaluates expression trees with variable bindings.
///
/// Usage:
/// @code
///   ExpressionEvaluator eval;
///   ExpressionEvaluator::VariableMap vars = {{"x", 3.0f}, {"y", 4.0f}};
///   float result = eval.evaluate(*expr, vars);
/// @endcode
class ExpressionEvaluator
{
public:
    using VariableMap = std::unordered_map<std::string, float>;

    /// @brief Evaluates an expression tree with the given variable bindings.
    /// @param node Root of the expression tree.
    /// @param variables Variable name -> value map.
    /// @return Evaluated scalar result.
    /// @throws std::runtime_error if a variable is not found or operation is unknown.
    float evaluate(const ExprNode& node, const VariableMap& variables) const;

    /// @brief Evaluates with both variables and named coefficients.
    ///
    /// Coefficients are checked after variables — a coefficient can be
    /// overridden by a variable with the same name.
    float evaluate(const ExprNode& node,
                   const VariableMap& variables,
                   const std::unordered_map<std::string, float>& coefficients) const;

    /// @brief Validates that an expression only references declared inputs and coefficients.
    /// @param node Root of the expression tree.
    /// @param inputs Declared formula inputs.
    /// @param coefficients Declared coefficient names.
    /// @param errorOut Receives a human-readable error message on failure.
    /// @return True if valid, false if undeclared variables are used.
    static bool validate(const ExprNode& node,
                         const std::vector<FormulaInput>& inputs,
                         const std::vector<std::string>& coefficients,
                         std::string& errorOut);
};

} // namespace Vestige
