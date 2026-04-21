// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file curve_fitter.cpp
/// @brief Levenberg-Marquardt curve fitter implementation.
#include "formula/curve_fitter.h"

#include <algorithm>
#include <cmath>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void CurveFitter::computeResiduals(const ExprNode& expr,
                                   const std::vector<DataPoint>& data,
                                   const std::vector<std::string>& coeffNames,
                                   const std::vector<float>& coeffValues,
                                   std::vector<float>& residuals)
{
    ExpressionEvaluator eval;
    std::unordered_map<std::string, float> coeffMap;
    for (size_t i = 0; i < coeffNames.size(); ++i)
        coeffMap[coeffNames[i]] = coeffValues[i];

    residuals.resize(data.size());
    for (size_t i = 0; i < data.size(); ++i)
    {
        float predicted = eval.evaluate(expr, data[i].variables, coeffMap);
        residuals[i] = predicted - data[i].observed;
    }
}

void CurveFitter::computeJacobian(const ExprNode& expr,
                                  const std::vector<DataPoint>& data,
                                  const std::vector<std::string>& coeffNames,
                                  const std::vector<float>& coeffValues,
                                  float step,
                                  std::vector<float>& jacobian)
{
    size_t M = data.size();
    size_t N = coeffNames.size();
    jacobian.resize(M * N);

    ExpressionEvaluator eval;

    for (size_t j = 0; j < N; ++j)
    {
        // Build coefficient maps for c_j + h and c_j - h
        std::unordered_map<std::string, float> mapPlus, mapMinus;
        for (size_t k = 0; k < N; ++k)
        {
            float val = coeffValues[k];
            if (k == j)
            {
                mapPlus[coeffNames[k]] = val + step;
                mapMinus[coeffNames[k]] = val - step;
            }
            else
            {
                mapPlus[coeffNames[k]] = val;
                mapMinus[coeffNames[k]] = val;
            }
        }

        for (size_t i = 0; i < M; ++i)
        {
            float fPlus = eval.evaluate(expr, data[i].variables, mapPlus);
            float fMinus = eval.evaluate(expr, data[i].variables, mapMinus);
            jacobian[i * N + j] = (fPlus - fMinus) / (2.0f * step);
        }
    }
}

bool CurveFitter::solveLinearSystem(std::vector<float> A, std::vector<float> b,
                                    int n, std::vector<float>& x)
{
    // Gaussian elimination with partial pivoting
    x.resize(static_cast<size_t>(n));

    for (int col = 0; col < n; ++col)
    {
        // Find pivot
        int pivotRow = col;
        float pivotVal = std::abs(A[static_cast<size_t>(col) * static_cast<size_t>(n)
                                    + static_cast<size_t>(col)]);
        for (int row = col + 1; row < n; ++row)
        {
            float val = std::abs(A[static_cast<size_t>(row) * static_cast<size_t>(n)
                                   + static_cast<size_t>(col)]);
            if (val > pivotVal)
            {
                pivotRow = row;
                pivotVal = val;
            }
        }

        if (pivotVal < 1e-15f)
            return false;  // Singular matrix

        // Swap rows
        if (pivotRow != col)
        {
            for (int k = 0; k < n; ++k)
            {
                std::swap(A[static_cast<size_t>(col) * static_cast<size_t>(n)
                            + static_cast<size_t>(k)],
                          A[static_cast<size_t>(pivotRow) * static_cast<size_t>(n)
                            + static_cast<size_t>(k)]);
            }
            std::swap(b[static_cast<size_t>(col)], b[static_cast<size_t>(pivotRow)]);
        }

        // Eliminate below
        float diag = A[static_cast<size_t>(col) * static_cast<size_t>(n)
                       + static_cast<size_t>(col)];
        for (int row = col + 1; row < n; ++row)
        {
            float factor = A[static_cast<size_t>(row) * static_cast<size_t>(n)
                             + static_cast<size_t>(col)] / diag;
            for (int k = col; k < n; ++k)
            {
                A[static_cast<size_t>(row) * static_cast<size_t>(n)
                  + static_cast<size_t>(k)] -=
                    factor * A[static_cast<size_t>(col) * static_cast<size_t>(n)
                               + static_cast<size_t>(k)];
            }
            b[static_cast<size_t>(row)] -= factor * b[static_cast<size_t>(col)];
        }
    }

    // Back-substitution
    for (int row = n - 1; row >= 0; --row)
    {
        float sum = b[static_cast<size_t>(row)];
        for (int k = row + 1; k < n; ++k)
        {
            sum -= A[static_cast<size_t>(row) * static_cast<size_t>(n)
                     + static_cast<size_t>(k)] * x[static_cast<size_t>(k)];
        }
        float diag = A[static_cast<size_t>(row) * static_cast<size_t>(n)
                       + static_cast<size_t>(row)];
        if (std::abs(diag) < 1e-15f)
            return false;
        x[static_cast<size_t>(row)] = sum / diag;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Main fit routine
// ---------------------------------------------------------------------------

FitResult CurveFitter::fit(const FormulaDefinition& formula,
                           const std::vector<DataPoint>& data,
                           const std::map<std::string, float>& initialCoeffs,
                           QualityTier tier,
                           const FitConfig& config)
{
    // Uniform-weight path — delegates to the weighted implementation
    // with an empty weight vector so there's a single LM loop to
    // maintain. Kept as a separate entry point for callers that
    // don't care about weighting.
    return fitWeighted(formula, data, {}, initialCoeffs, tier, config);
}

FitResult CurveFitter::fitWeighted(const FormulaDefinition& formula,
                                   const std::vector<DataPoint>& data,
                                   const std::vector<float>& weights,
                                   const std::map<std::string, float>& initialCoeffs,
                                   QualityTier tier,
                                   const FitConfig& config)
{
    FitResult result;

    // Weights vector must either be empty (uniform) or match data
    // length. A size mismatch is almost always a caller bug — fall
    // back to uniform rather than silently discarding samples.
    const bool weighted = !weights.empty() && weights.size() == data.size();

    // Validate inputs
    const ExprNode* expr = formula.getExpression(tier);
    if (!expr)
    {
        result.statusMessage = "No expression for requested quality tier";
        return result;
    }

    if (data.empty())
    {
        result.statusMessage = "No data points provided";
        return result;
    }

    if (initialCoeffs.empty())
    {
        result.statusMessage = "No coefficients to fit";
        return result;
    }

    size_t M = data.size();
    size_t N = initialCoeffs.size();

    if (M < N)
    {
        result.statusMessage = "Insufficient data: need at least as many points as coefficients";
        return result;
    }

    // Extract coefficient names and initial values in deterministic order
    std::vector<std::string> coeffNames;
    std::vector<float> coeffValues;
    coeffNames.reserve(N);
    coeffValues.reserve(N);
    for (const auto& [name, val] : initialCoeffs)
    {
        coeffNames.push_back(name);
        coeffValues.push_back(val);
    }

    // Compute initial residuals
    std::vector<float> residuals;
    computeResiduals(*expr, data, coeffNames, coeffValues, residuals);

    // AUDIT.md §H13 / FIXPLAN E1: bail on non-finite initial residuals.
    // Without this check, NaN propagates through the iteration loop,
    // `trialError < NaN` is always false so no step is ever accepted,
    // and we silently run to maxIterations reporting `converged=false`
    // with garbage coefficients.
    for (float r : residuals)
    {
        if (!std::isfinite(r))
        {
            result.statusMessage =
                "Failed: non-finite residual on initial evaluation "
                "(check input domain — inputs may cross a safe-math guard "
                "or the formula may produce NaN/Inf for the given data)";
            result.iterations = 0;
            result.finalError = std::numeric_limits<float>::infinity();
            for (size_t j = 0; j < N; ++j)
                result.coefficients[coeffNames[j]] = coeffValues[j];
            return result;
        }
    }

    // Accumulate error in double precision — matches workbench v1.3.0 fix.
    // Single-precision sums over thousands of squared residuals lose bits
    // in the low end (AUDIT.md §L3). In weighted mode the sum of squares
    // is weighted: sum_i (w_i · r_i²).
    double currentError = 0.0;
    for (size_t i = 0; i < residuals.size(); ++i)
    {
        const double r = static_cast<double>(residuals[i]);
        const double w = weighted ? static_cast<double>(std::max(0.0f, weights[i]))
                                  : 1.0;
        currentError += w * r * r;
    }

    float lambda = config.initialLambda;

    // LM iteration loop
    int iter = 0;
    for (; iter < config.maxIterations; ++iter)
    {
        // Compute Jacobian (M × N)
        std::vector<float> J;
        computeJacobian(*expr, data, coeffNames, coeffValues,
                        config.finiteDiffStep, J);

        // Compute J^T W J (N × N) and J^T W r (N × 1).
        // Weighted normal equations: scaling row i of [J | r] by
        // sqrt(w_i) is algebraically identical to multiplying J^T*J
        // and J^T*r by diag(w). We fold the weight directly into the
        // accumulation so we don't materialise the scaled Jacobian.
        std::vector<float> JtJ(N * N, 0.0f);
        std::vector<float> JtR(N, 0.0f);

        for (size_t i = 0; i < M; ++i)
        {
            const float w = weighted ? std::max(0.0f, weights[i]) : 1.0f;
            if (w == 0.0f) continue;
            for (size_t j = 0; j < N; ++j)
            {
                float Jij = J[i * N + j];
                JtR[j] += w * Jij * residuals[i];
                for (size_t k = j; k < N; ++k)
                {
                    float val = w * Jij * J[i * N + k];
                    JtJ[j * N + k] += val;
                    if (k != j)
                        JtJ[k * N + j] += val;
                }
            }
        }

        // Check gradient convergence
        float gradNorm = 0.0f;
        for (size_t j = 0; j < N; ++j)
            gradNorm += JtR[j] * JtR[j];
        gradNorm = std::sqrt(gradNorm);

        if (gradNorm < config.gradientThreshold)
        {
            result.converged = true;
            result.statusMessage = "Converged (gradient below threshold)";
            break;
        }

        // Add lambda * diag(J^T * J) to the diagonal (Marquardt damping)
        std::vector<float> A = JtJ;
        for (size_t j = 0; j < N; ++j)
        {
            float diagVal = JtJ[j * N + j];
            if (diagVal < 1e-10f)
                diagVal = 1e-10f;  // Prevent zero diagonal
            A[j * N + j] += lambda * diagVal;
        }

        // Solve (J^T*J + lambda*D) * delta = J^T * r
        std::vector<float> delta;
        if (!solveLinearSystem(A, JtR, static_cast<int>(N), delta))
        {
            // System singular — increase lambda and retry
            lambda *= config.lambdaUpFactor;
            if (lambda > 1e15f)
            {
                result.statusMessage = "Failed: singular normal equations";
                break;
            }
            continue;
        }

        // Trial step: new coefficients
        std::vector<float> trialCoeffs(N);
        for (size_t j = 0; j < N; ++j)
            trialCoeffs[j] = coeffValues[j] - delta[j];

        // Evaluate trial error
        std::vector<float> trialResiduals;
        computeResiduals(*expr, data, coeffNames, trialCoeffs, trialResiduals);

        // AUDIT.md §H13: reject trial steps that produce non-finite
        // residuals rather than letting NaN pollute the accumulator.
        bool trialFinite = true;
        for (float r : trialResiduals)
        {
            if (!std::isfinite(r)) { trialFinite = false; break; }
        }
        if (!trialFinite)
        {
            lambda *= config.lambdaUpFactor;
            if (lambda > 1e15f)
            {
                result.statusMessage =
                    "Failed: trial step produced non-finite residuals";
                break;
            }
            continue;
        }

        double trialError = 0.0;
        for (size_t i = 0; i < trialResiduals.size(); ++i)
        {
            const double r = static_cast<double>(trialResiduals[i]);
            const double w = weighted ? static_cast<double>(std::max(0.0f, weights[i]))
                                      : 1.0;
            trialError += w * r * r;
        }

        if (trialError < currentError)
        {
            // Accept step
            coeffValues = trialCoeffs;
            residuals = trialResiduals;

            double relChange = (currentError - trialError) / (currentError + 1e-30);
            currentError = trialError;
            lambda /= config.lambdaDownFactor;

            if (relChange < static_cast<double>(config.convergenceThreshold))
            {
                result.converged = true;
                result.statusMessage = "Converged (error change below threshold)";
                break;
            }
        }
        else
        {
            // Reject step — increase damping
            lambda *= config.lambdaUpFactor;
            if (lambda > 1e15f)
            {
                result.statusMessage = "Failed: lambda exceeded maximum";
                break;
            }
        }
    }

    if (iter >= config.maxIterations && !result.converged)
        result.statusMessage = "Reached maximum iterations";

    // Store final coefficients
    result.iterations = iter;
    result.finalError = static_cast<float>(currentError);
    for (size_t j = 0; j < N; ++j)
        result.coefficients[coeffNames[j]] = coeffValues[j];

    // Compute statistics
    // Recompute residuals with final coefficients
    computeResiduals(*expr, data, coeffNames, coeffValues, residuals);

    // RMSE, max error, and r² accumulators use double precision — matches
    // the workbench v1.3.0 fix; single-precision accumulation over large
    // datasets loses low-order bits (AUDIT.md §L3).
    double sumSqResid = 0.0;
    float maxErr = 0.0f;
    for (size_t i = 0; i < M; ++i)
    {
        sumSqResid += static_cast<double>(residuals[i])
                    * static_cast<double>(residuals[i]);
        maxErr = std::max(maxErr, std::abs(residuals[i]));
    }
    result.rmse = static_cast<float>(
        std::sqrt(sumSqResid / static_cast<double>(M)));
    result.maxError = maxErr;

    // R-squared: 1 - SS_res / SS_tot
    double mean = 0.0;
    for (const auto& dp : data)
        mean += static_cast<double>(dp.observed);
    mean /= static_cast<double>(M);

    double ssTot = 0.0;
    for (const auto& dp : data)
    {
        double diff = static_cast<double>(dp.observed) - mean;
        ssTot += diff * diff;
    }

    if (ssTot > 1e-15)
        result.rSquared = static_cast<float>(1.0 - sumSqResid / ssTot);
    else
        result.rSquared = (sumSqResid < 1e-15) ? 1.0f : 0.0f;

    return result;
}

} // namespace Vestige
