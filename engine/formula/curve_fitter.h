// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file curve_fitter.h
/// @brief Levenberg-Marquardt curve fitter for formula coefficients.
///
/// Fits formula coefficients to observed data points by minimizing the sum
/// of squared residuals. Uses numerical differentiation (central differences)
/// for the Jacobian. Designed for small parameter counts (< 20 coefficients)
/// — uses no external linear algebra library.
#pragma once

#include "formula/expression.h"
#include "formula/expression_eval.h"
#include "formula/formula.h"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vestige
{

/// @brief A single observed data point for curve fitting.
struct DataPoint
{
    ExpressionEvaluator::VariableMap variables;  ///< Input variable values
    float observed = 0.0f;                       ///< Measured output value
};

/// @brief Configuration for the Levenberg-Marquardt algorithm.
struct FitConfig
{
    int maxIterations = 200;             ///< Maximum LM iterations
    float convergenceThreshold = 1e-8f;  ///< Stop when relative error change < this
    float gradientThreshold = 1e-10f;    ///< Stop when gradient norm < this
    float initialLambda = 0.001f;        ///< Initial damping factor
    float lambdaUpFactor = 10.0f;        ///< Lambda multiplier on rejected step
    float lambdaDownFactor = 10.0f;      ///< Lambda divisor on accepted step
    float finiteDiffStep = 1e-5f;        ///< Step size for numerical Jacobian
};

/// @brief Result of curve fitting.
struct FitResult
{
    bool converged = false;                       ///< True if algorithm converged
    std::map<std::string, float> coefficients;    ///< Fitted coefficient values
    float rSquared = 0.0f;                        ///< Coefficient of determination [0,1]
    float rmse = 0.0f;                            ///< Root mean square error
    float maxError = 0.0f;                        ///< Maximum absolute error
    int iterations = 0;                           ///< Iterations performed
    float finalError = 0.0f;                      ///< Final sum of squared residuals
    std::string statusMessage;                    ///< Human-readable status
};

/// @brief Levenberg-Marquardt curve fitter for formula expression trees.
///
/// Usage:
/// @code
///   std::vector<DataPoint> data = {{{"x", 1.0f}, 2.5f}, {{"x", 2.0f}, 5.1f}};
///   std::map<std::string, float> initial = {{"a", 1.0f}, {"b", 0.0f}};
///   FitResult result = CurveFitter::fit(formula, data, initial);
/// @endcode
class CurveFitter
{
public:
    /// @brief Fits formula coefficients to observed data.
    /// @param formula The formula whose coefficients to fit.
    /// @param data Observed data points.
    /// @param initialCoeffs Starting values for each coefficient.
    /// @param tier Quality tier whose expression to use.
    /// @param config Algorithm configuration.
    /// @return Fit result with coefficients, statistics, and convergence info.
    static FitResult fit(const FormulaDefinition& formula,
                         const std::vector<DataPoint>& data,
                         const std::map<std::string, float>& initialCoeffs,
                         QualityTier tier = QualityTier::FULL,
                         const FitConfig& config = {});

    /// @brief Weighted-least-squares fit — minimises `sum(w_i · r_i²)`.
    ///
    /// Takes the same arguments as the unweighted overload plus a parallel
    /// vector of non-negative per-sample weights. Empty `weights` (or any
    /// other size mismatch) falls through to the uniform-weight fit for
    /// backwards compatibility. Reported `rmse` / `maxError` / `rSquared`
    /// are computed on the *unweighted* residuals so the numbers stay
    /// comparable to unweighted fits on the same data.
    ///
    /// Use when certain input regions matter more than others (forward
    /// scatter in phase functions, highlight region in tonemap curves,
    /// grazing angles in BRDFs). Negative weights are clamped to 0.
    static FitResult fitWeighted(const FormulaDefinition& formula,
                                 const std::vector<DataPoint>& data,
                                 const std::vector<float>& weights,
                                 const std::map<std::string, float>& initialCoeffs,
                                 QualityTier tier = QualityTier::FULL,
                                 const FitConfig& config = {});

private:
    /// @brief Evaluates residuals (predicted - observed) for all data points.
    static void computeResiduals(const ExprNode& expr,
                                 const std::vector<DataPoint>& data,
                                 const std::vector<std::string>& coeffNames,
                                 const std::vector<float>& coeffValues,
                                 std::vector<float>& residuals);

    /// @brief Computes the Jacobian matrix via central finite differences.
    /// @details jacobian is stored row-major: jacobian[i * N + j] = dr_i / dc_j.
    static void computeJacobian(const ExprNode& expr,
                                const std::vector<DataPoint>& data,
                                const std::vector<std::string>& coeffNames,
                                const std::vector<float>& coeffValues,
                                float step,
                                std::vector<float>& jacobian);

    /// @brief Solves Ax = b for a small symmetric system via Gaussian elimination.
    /// @return True if the system was solved successfully.
    static bool solveLinearSystem(std::vector<float> A, std::vector<float> b,
                                 int n, std::vector<float>& x);
};

} // namespace Vestige
