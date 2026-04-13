/// @file safe_math.h
/// @brief Single source of truth for formula-pipeline safe-math semantics.
///
/// The Formula Pipeline has three independent evaluation paths:
///   1. ExpressionEvaluator (tree-walking, used by the LM curve fitter)
///   2. CodegenCpp (emits `.h` consumed by runtime C++)
///   3. CodegenGlsl (emits shader source for runtime GLSL)
///
/// Prior to AUDIT.md §H12 / FIXPLAN E4, these paths diverged on degenerate
/// inputs: the evaluator guarded `/0`, `sqrt(negative)` and `log(<=0)`;
/// codegen emitted bare `/`, `std::sqrt`, `std::log`. The LM fitter thus
/// reported R² = 0.99 on coefficients tuned against safe math, and the
/// runtime C++/GLSL produced NaN / black pixels on the same inputs.
///
/// This header centralises the semantics. The C++ helpers live in this
/// namespace; the GLSL helpers are emitted verbatim by
/// CodegenGlsl::safeMathPrelude() as a prelude to every generated shader
/// source so a single behaviour change lands in all three paths.

#pragma once

#include <cmath>

namespace Vestige::SafeMath
{

/// @brief Division with zero-divisor guard.
///
/// Mirrors ExpressionEvaluator: right==0 → return 0. Intentionally does
/// NOT return NaN — the evaluator's contract is that degenerate math is
/// projected to 0 so the curve fitter stays in finite-value space.
inline float safeDiv(float left, float right)
{
    if (right == 0.0f) return 0.0f;
    return left / right;
}

/// @brief Square root with domain guard.
///
/// Mirrors ExpressionEvaluator: reflect negatives via std::fabs so the
/// result is always finite. Equivalent to std::sqrt(std::fabs(x)).
inline float safeSqrt(float x)
{
    return std::sqrt(std::fabs(x));
}

/// @brief Natural log with domain guard.
///
/// Mirrors ExpressionEvaluator: x <= 0 → return 0 (never -inf / NaN).
/// This keeps log-family fits bounded even when the optimiser wanders
/// into negative territory.
inline float safeLog(float x)
{
    return (x > 0.0f) ? std::log(x) : 0.0f;
}

/// @brief GLSL-source prelude that defines the same three helpers.
///
/// CodegenGlsl prepends this to every generated file so the emitted
/// shader behaves identically to the evaluator for all three guarded
/// operations. Kept as a function returning a constant string (rather
/// than a string_view constant) so the definition lives alongside the
/// C++ implementation.
inline const char* glslPrelude()
{
    return
        "// Vestige SafeMath prelude — matches engine/formula/safe_math.h.\n"
        "// See AUDIT.md §H12 / FIXPLAN E4 for motivation.\n"
        "float safeDiv(float l, float r) { return (r == 0.0) ? 0.0 : (l / r); }\n"
        "float safeSqrt(float x) { return sqrt(abs(x)); }\n"
        "float safeLog(float x) { return (x > 0.0) ? log(x) : 0.0; }\n";
}

} // namespace Vestige::SafeMath
