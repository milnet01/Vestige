// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file fit_diagnostics.h
/// @brief What the Workbench does to a fit after CurveFitter returns it:
/// clamp the coefficients to the user's bounds, and work out the
/// model-selection statistics shown beside R² and RMSE.
///
/// Pure non-GUI module, so the tests can link it without ImGui.
#pragma once

#include "formula/curve_fitter.h"

#include <map>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief A user-set range for one coefficient. Ignored unless enabled.
struct CoeffBound
{
    float lower = -1e6f;
    float upper =  1e6f;
    bool  enabled = false;
};

/// @brief Clamps `result.coefficients` to their enabled bounds. When a
/// value moved, re-scores `result` (CurveFitter::rescore), so its
/// R², RMSE and max error describe the clamped coefficients.
/// @return True when at least one coefficient moved.
bool clampFitToBounds(const FormulaDefinition& formula,
                      const std::vector<DataPoint>& data,
                      const std::map<std::string, CoeffBound>& bounds,
                      FitResult& result);

/// @brief Model-selection statistics for one fit.
struct FitDiagnostics
{
    int paramCount = 0;
    /// Meaningful only when adjustedRSquaredDefined; otherwise 0.
    float adjustedRSquared = 0.0f;
    /// False when there are too few points (n ≤ k+1).
    bool adjustedRSquaredDefined = false;
    /// Meaningful only when aicBicDefined; otherwise 0.
    float aic = 0.0f;
    float bic = 0.0f;
    /// False when there are too few points (n ≤ k+1), or the fit is
    /// exact (SSE 0); computeAicBic's two degenerate cases.
    bool aicBicDefined = false;
};

/// @brief Works out the diagnostics for `result` over `nPoints` data
/// points. Every field is set from this fit alone.
FitDiagnostics computeFitDiagnostics(const FitResult& result, int nPoints);

} // namespace Vestige
