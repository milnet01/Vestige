// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

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

/// @brief Power with degenerate-math guard.
///
/// ``std::pow(negative, non-integer)`` returns NaN, which then propagates
/// through the LM fitter residuals and poisons R² / AIC / BIC. Mirror the
/// safeDiv/Sqrt/Log convention: degenerate math is projected to 0 so the
/// optimiser stays in finite-value space. Integer exponents (including 0)
/// are passed through unchanged — `pow(-2, 3)` remains defined. (AUDIT
/// M11; authored alongside the Workbench harness so the three evaluation
/// paths never drift again.)
inline float safePow(float base, float exp)
{
    if (base < 0.0f)
    {
        // Integer exponent (within float precision) → standard behaviour.
        const float rounded = std::round(exp);
        if (std::fabs(exp - rounded) < 1e-6f && std::isfinite(rounded))
        {
            return std::pow(base, exp);
        }
        // Fractional negative base → degenerate; project to 0.
        return 0.0f;
    }
    return std::pow(base, exp);
}

/// @brief GLSL-source prelude that defines the same four helpers.
///
/// CodegenGlsl prepends this to every generated file so the emitted
/// shader behaves identically to the evaluator for all four guarded
/// operations. Kept as a function returning a constant string (rather
/// than a string_view constant) so the definition lives alongside the
/// C++ implementation.
inline const char* glslPrelude()
{
    return
        "// Vestige SafeMath prelude — matches engine/formula/safe_math.h.\n"
        "// See AUDIT.md §H12 / FIXPLAN E4 and AUDIT M11 for motivation.\n"
        "float safeDiv(float l, float r) { return (r == 0.0) ? 0.0 : (l / r); }\n"
        "float safeSqrt(float x) { return sqrt(abs(x)); }\n"
        "float safeLog(float x) { return (x > 0.0) ? log(x) : 0.0; }\n"
        "float safePow(float b, float e) {\n"
        "    if (b < 0.0) {\n"
        "        float r = floor(e + 0.5);\n"
        "        if (abs(e - r) < 1e-6) { return pow(b, e); }\n"
        "        return 0.0;\n"
        "    }\n"
        "    return pow(b, e);\n"
        "}\n";
}

} // namespace Vestige::SafeMath
