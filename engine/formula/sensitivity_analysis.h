/// @file sensitivity_analysis.h
/// @brief Sensitivity analysis for formula coefficients using finite differences.
///
/// Computes partial derivatives of formula output with respect to each coefficient,
/// producing dimensionless sensitivity metrics to rank which coefficients matter most.
#pragma once

#include "formula/expression_eval.h"
#include "formula/formula.h"

#include <string>
#include <vector>

namespace Vestige
{

/// @brief Sensitivity metrics for a single coefficient.
struct CoefficientSensitivity
{
    std::string name;               ///< Coefficient name
    float baseValue;                ///< Original coefficient value
    float derivative;               ///< d(output)/d(coefficient) at base point
    float normalizedSensitivity;    ///< |derivative * baseValue / baseOutput| — dimensionless
    float minEffect;                ///< Output at coefficient * 0.5
    float maxEffect;                ///< Output at coefficient * 1.5
};

/// @brief Full sensitivity report for a formula.
struct SensitivityReport
{
    std::string formulaName;
    std::vector<CoefficientSensitivity> coefficients;
    float baseOutput;               ///< Output with original coefficients

    /// @brief Sort by normalized sensitivity (most sensitive first).
    void sortByImpact();
};

/// @brief Analyzes how each coefficient affects formula output.
///
/// Uses central finite differences to compute partial derivatives:
/// df/dc ≈ (f(c+h) - f(c-h)) / (2h)
/// where h = max(|c| * epsilon, epsilon) for numerical stability.
class SensitivityAnalyzer
{
public:
    /// @brief Analyze all coefficients of a formula at given input values.
    /// @param formula The formula to analyze.
    /// @param inputValues Variable bindings for formula inputs.
    /// @param tier Quality tier to evaluate.
    /// @param epsilon Step size for finite differences.
    /// @return Report with sensitivity metrics for each coefficient.
    SensitivityReport analyze(
        const FormulaDefinition& formula,
        const ExpressionEvaluator::VariableMap& inputValues,
        QualityTier tier = QualityTier::FULL,
        float epsilon = 1e-4f) const;
};

} // namespace Vestige
