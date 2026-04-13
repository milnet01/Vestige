/// @file codegen_glsl.h
/// @brief GLSL code generator for the Formula Pipeline.
///
/// Walks expression trees and emits GLSL function snippets that can be
/// injected into shader programs. Generated code compiles into the shader
/// program with zero runtime overhead.
#pragma once

#include "formula/formula.h"

#include <string>
#include <vector>

namespace Vestige
{

/// @brief Generates GLSL function snippets from FormulaDefinition expression trees.
///
/// Usage:
///   auto code = CodegenGlsl::generateFunction(formula);
///   auto file = CodegenGlsl::generateFile(formulas);
class CodegenGlsl
{
public:
    /// @brief Generate a single GLSL function from a formula.
    /// @param formula The formula definition.
    /// @param tier Quality tier to generate (falls back to FULL).
    /// @return GLSL function source code as a string.
    static std::string generateFunction(const FormulaDefinition& formula,
                                         QualityTier tier = QualityTier::FULL);

    /// @brief Generate a complete GLSL include file with all formulas.
    /// @param formulas Pointers to formula definitions.
    /// @param tier Quality tier to generate.
    /// @return Complete GLSL file content.
    static std::string generateFile(const std::vector<const FormulaDefinition*>& formulas,
                                     QualityTier tier = QualityTier::FULL);

    /// @brief Emit a GLSL expression string from an expression node.
    /// @param node The expression tree root.
    /// @param coefficients Named coefficients to inline.
    /// @return GLSL expression string.
    static std::string emitExpression(const ExprNode& node,
                                      const std::map<std::string, float>& coefficients);

    /// @brief Convert a snake_case formula name to a camelCase GLSL function name.
    static std::string toGlslFunctionName(const std::string& formulaName);

    /// @brief Convert FormulaValueType to GLSL type string.
    static std::string toGlslType(FormulaValueType type);

    /// @brief Format a float value as a GLSL literal (no 'f' suffix).
    static std::string floatLiteral(float val);

    /// @brief GLSL prelude declaring safeDiv/safeSqrt/safeLog.
    ///
    /// Prepended automatically by generateFile(). Exposed publicly so
    /// ad-hoc callers (tests, tools) that splice an individual emitted
    /// function into a larger shader can include the same prelude and
    /// keep evaluator/GLSL semantics aligned (AUDIT.md §H12).
    static std::string safeMathPrelude();
};

} // namespace Vestige
