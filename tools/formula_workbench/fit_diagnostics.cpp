// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file fit_diagnostics.cpp
/// @brief Bounds clamp and model-selection statistics for a finished fit.
#include "fit_diagnostics.h"

#include "benchmark.h"

#include <algorithm>

namespace Vestige
{

bool clampFitToBounds(const FormulaDefinition& formula,
                      const std::vector<DataPoint>& data,
                      const std::map<std::string, CoeffBound>& bounds,
                      FitResult& result)
{
    bool moved = false;
    for (auto& [name, val] : result.coefficients)
    {
        auto boundIt = bounds.find(name);
        if (boundIt == bounds.end() || !boundIt->second.enabled)
            continue;
        const float clamped = std::max(boundIt->second.lower,
                                       std::min(val, boundIt->second.upper));
        if (clamped != val)
        {
            val = clamped;
            moved = true;
        }
    }
    if (moved)
        CurveFitter::rescore(formula, data, result);
    return moved;
}

FitDiagnostics computeFitDiagnostics(const FitResult& result, int nPoints)
{
    FitDiagnostics d;
    d.paramCount = static_cast<int>(result.coefficients.size());
    if (nPoints > d.paramCount + 1)
    {
        d.adjustedRSquaredDefined = true;
        d.adjustedRSquared = 1.0f - (1.0f - result.rSquared)
                             * static_cast<float>(nPoints - 1)
                             / static_cast<float>(nPoints - d.paramCount - 1);
    }
    const InformationCriteria ic = computeAicBic(result.rmse, nPoints, d.paramCount);
    if (!ic.degenerate)
    {
        d.aicBicDefined = true;
        d.aic = ic.aic;
        d.bic = ic.bic;
    }
    return d;
}

} // namespace Vestige
