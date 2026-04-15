// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file sensitivity_analysis.cpp
/// @brief Sensitivity analysis implementation using central finite differences.
#include "formula/sensitivity_analysis.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace Vestige
{

// ---------------------------------------------------------------------------
// SensitivityReport
// ---------------------------------------------------------------------------

void SensitivityReport::sortByImpact()
{
    std::sort(coefficients.begin(), coefficients.end(),
        [](const CoefficientSensitivity& a, const CoefficientSensitivity& b)
        {
            return a.normalizedSensitivity > b.normalizedSensitivity;
        });
}

// ---------------------------------------------------------------------------
// SensitivityAnalyzer
// ---------------------------------------------------------------------------

/// @brief Helper to evaluate a formula with modified coefficients.
static float evaluateWithCoeffs(
    const ExpressionEvaluator& evaluator,
    const ExprNode& expr,
    const ExpressionEvaluator::VariableMap& inputValues,
    const std::map<std::string, float>& originalCoeffs,
    const std::string& modifiedCoeff,
    float modifiedValue)
{
    // Build coefficient map with one value overridden
    std::unordered_map<std::string, float> coeffs;
    for (const auto& [name, value] : originalCoeffs)
    {
        if (name == modifiedCoeff)
        {
            coeffs[name] = modifiedValue;
        }
        else
        {
            coeffs[name] = value;
        }
    }
    return evaluator.evaluate(expr, inputValues, coeffs);
}

SensitivityReport SensitivityAnalyzer::analyze(
    const FormulaDefinition& formula,
    const ExpressionEvaluator::VariableMap& inputValues,
    QualityTier tier,
    float epsilon) const
{
    SensitivityReport report;
    report.formulaName = formula.name;

    const ExprNode* expr = formula.getExpression(tier);
    if (!expr)
    {
        throw std::runtime_error(
            "SensitivityAnalyzer: no expression for tier in formula '" +
            formula.name + "'");
    }

    ExpressionEvaluator evaluator;

    // Compute base output with original coefficients
    std::unordered_map<std::string, float> baseCoeffs(
        formula.coefficients.begin(), formula.coefficients.end());
    report.baseOutput = evaluator.evaluate(*expr, inputValues, baseCoeffs);

    // Analyze each coefficient
    for (const auto& [coeffName, coeffValue] : formula.coefficients)
    {
        CoefficientSensitivity cs;
        cs.name = coeffName;
        cs.baseValue = coeffValue;

        // Step size: max(|c| * epsilon, epsilon) for numerical stability
        float h = std::fmax(std::fabs(coeffValue) * epsilon, epsilon);

        // Central finite difference: df/dc ≈ (f(c+h) - f(c-h)) / (2h)
        float fPlus = evaluateWithCoeffs(
            evaluator, *expr, inputValues,
            formula.coefficients, coeffName, coeffValue + h);
        float fMinus = evaluateWithCoeffs(
            evaluator, *expr, inputValues,
            formula.coefficients, coeffName, coeffValue - h);

        cs.derivative = (fPlus - fMinus) / (2.0f * h);

        // Normalized sensitivity: |df/dc * c / f(base)|
        // Skip normalization if base output is zero (avoid division by zero)
        if (std::fabs(report.baseOutput) > 1e-10f)
        {
            cs.normalizedSensitivity =
                std::fabs(cs.derivative * coeffValue / report.baseOutput);
        }
        else
        {
            cs.normalizedSensitivity = 0.0f;
        }

        // Range effects: output at coefficient * 0.5 and * 1.5
        cs.minEffect = evaluateWithCoeffs(
            evaluator, *expr, inputValues,
            formula.coefficients, coeffName, coeffValue * 0.5f);
        cs.maxEffect = evaluateWithCoeffs(
            evaluator, *expr, inputValues,
            formula.coefficients, coeffName, coeffValue * 1.5f);

        report.coefficients.push_back(cs);
    }

    return report;
}

} // namespace Vestige
