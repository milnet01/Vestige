// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file codegen_cpp.h
/// @brief C++ code generator for the Formula Pipeline.
///
/// Walks expression trees and emits inline C++ functions. Coefficients are
/// inlined as compile-time constants. Generated code has zero runtime
/// overhead vs hand-written equivalents.
#pragma once

#include "formula/formula.h"

#include <string>
#include <vector>

namespace Vestige
{

/// @brief Generates C++ inline functions from FormulaDefinition expression trees.
///
/// Usage:
///   auto code = CodegenCpp::generateFunction(formula);
///   auto header = CodegenCpp::generateHeader(formulas);
class CodegenCpp
{
public:
    /// @brief Generate a single C++ inline function from a formula.
    /// @param formula The formula definition.
    /// @param tier Quality tier to generate (falls back to FULL).
    /// @return C++ function source code as a string.
    static std::string generateFunction(const FormulaDefinition& formula,
                                         QualityTier tier = QualityTier::FULL);

    /// @brief Generate a complete header file with all formulas.
    /// @param formulas Pointers to formula definitions.
    /// @param tier Quality tier to generate.
    /// @return Complete C++ header file content.
    static std::string generateHeader(const std::vector<const FormulaDefinition*>& formulas,
                                       QualityTier tier = QualityTier::FULL);

    /// @brief Emit a C++ expression string from an expression node.
    ///
    /// Coefficients in the map are inlined as float literals. Variables not
    /// in the coefficient map are emitted as parameter names.
    /// @param node The expression tree root.
    /// @param coefficients Named coefficients to inline.
    /// @return C++ expression string.
    static std::string emitExpression(const ExprNode& node,
                                      const std::map<std::string, float>& coefficients);

    /// @brief Convert a snake_case formula name to a camelCase C++ function name.
    static std::string toCppFunctionName(const std::string& formulaName);

    /// @brief Convert FormulaValueType to C++ return type string.
    static std::string toCppType(FormulaValueType type);

    /// @brief Convert FormulaValueType to C++ parameter type (const ref for vectors).
    static std::string toCppParamType(FormulaValueType type);

    /// @brief Format a float value as a C++ literal (with 'f' suffix).
    static std::string floatLiteral(float val);
};

} // namespace Vestige
