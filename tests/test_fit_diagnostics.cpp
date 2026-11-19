// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_fit_diagnostics.cpp
/// @brief Tests for what the Workbench does to a finished fit: the
/// bounds clamp and the model-selection statistics (3D_E-0874).
#include <gtest/gtest.h>

#include "fit_diagnostics.h"
#include "formula/expression.h"
#include "formula/formula.h"

#include <cmath>

using namespace Vestige;

namespace
{

/// y = a * x
FormulaDefinition makeLine()
{
    FormulaDefinition def;
    def.name = "line";
    def.category = "test";
    def.inputs = {{"x", FormulaValueType::FLOAT, "m", 1.0f}};
    def.coefficients = {{"a", 1.0f}};
    def.expressions[QualityTier::FULL] =
        ExprNode::binaryOp("*", ExprNode::variable("a"), ExprNode::variable("x"));
    return def;
}

/// Points on y = 2x for x = 1..count.
std::vector<DataPoint> makeLineData(int count)
{
    std::vector<DataPoint> data;
    for (int i = 1; i <= count; ++i)
        data.push_back({{{"x", static_cast<float>(i)}}, 2.0f * static_cast<float>(i)});
    return data;
}

} // namespace

// A clamp that moves a coefficient changes how well the curve fits, so
// the statistics must describe the clamped coefficient, not the one the
// fitter found.
TEST(FitDiagnostics, ClampedFitIsRescored)
{
    const auto def = makeLine();
    const auto data = makeLineData(4);
    FitResult fit = CurveFitter::fit(def, data, {{"a", 1.5f}});
    ASSERT_NEAR(fit.coefficients["a"], 2.0f, 1e-3f);
    ASSERT_NEAR(fit.rmse, 0.0f, 1e-3f);

    std::map<std::string, CoeffBound> bounds;
    bounds["a"] = {0.0f, 1.0f, true};
    EXPECT_TRUE(clampFitToBounds(def, data, bounds, fit));
    EXPECT_FLOAT_EQ(fit.coefficients["a"], 1.0f);

    // With a = 1 the residuals are -1, -2, -3, -4:
    //   SSE = 30, RMSE = sqrt(30 / 4), max error = 4.
    //   Mean of 2, 4, 6, 8 is 5, SS_tot = 20, R² = 1 - 30 / 20 = -0.5.
    EXPECT_NEAR(fit.rmse, std::sqrt(7.5f), 1e-4f);
    EXPECT_NEAR(fit.maxError, 4.0f, 1e-4f);
    EXPECT_NEAR(fit.rSquared, -0.5f, 1e-4f);
    EXPECT_NEAR(fit.finalError, 30.0f, 1e-3f);
}

TEST(FitDiagnostics, ClampInsideBoundsLeavesFitAlone)
{
    const auto def = makeLine();
    const auto data = makeLineData(4);
    FitResult fit = CurveFitter::fit(def, data, {{"a", 1.5f}});
    const FitResult before = fit;

    std::map<std::string, CoeffBound> bounds;
    bounds["a"] = {0.0f, 5.0f, true};
    bounds["unused"] = {0.0f, 1.0f, true};
    EXPECT_FALSE(clampFitToBounds(def, data, bounds, fit));
    EXPECT_EQ(fit.coefficients, before.coefficients);
    EXPECT_EQ(fit.rmse, before.rmse);
    EXPECT_EQ(fit.rSquared, before.rSquared);
}

TEST(FitDiagnostics, DisabledBoundDoesNotClamp)
{
    const auto def = makeLine();
    const auto data = makeLineData(4);
    FitResult fit = CurveFitter::fit(def, data, {{"a", 1.5f}});

    std::map<std::string, CoeffBound> bounds;
    bounds["a"] = {0.0f, 1.0f, false};
    EXPECT_FALSE(clampFitToBounds(def, data, bounds, fit));
    EXPECT_NEAR(fit.coefficients["a"], 2.0f, 1e-3f);
}

TEST(FitDiagnostics, ScoredFitMatchesClosedForm)
{
    // n = 10, k = 2, rmse = 0.1: the reference worked in
    // BenchmarkAicBic.MatchesClosedForm. Adjusted R² =
    // 1 - (1 - 0.9) * 9 / 7.
    FitResult fit;
    fit.coefficients = {{"a", 1.0f}, {"b", 2.0f}};
    fit.rmse = 0.1f;
    fit.rSquared = 0.9f;
    const FitDiagnostics d = computeFitDiagnostics(fit, 10);
    EXPECT_EQ(d.paramCount, 2);
    EXPECT_TRUE(d.adjustedRSquaredDefined);
    EXPECT_NEAR(d.adjustedRSquared, 1.0f - 0.1f * 9.0f / 7.0f, 1e-5f);
    EXPECT_TRUE(d.aicBicDefined);
    EXPECT_NEAR(d.aic, -42.052f, 0.01f);
    EXPECT_NEAR(d.bic, -41.446f, 0.01f);
}

// A fit too small to score must not show the fit before it.
TEST(FitDiagnostics, TooFewPointsCarriesNothingFromEarlierFit)
{
    FitResult scored;
    scored.coefficients = {{"a", 1.0f}, {"b", 2.0f}};
    scored.rmse = 0.1f;
    scored.rSquared = 0.9f;
    (void)computeFitDiagnostics(scored, 10);

    FitResult tiny = scored;
    const FitDiagnostics d = computeFitDiagnostics(tiny, 3);   // n = k + 1
    EXPECT_EQ(d.paramCount, 2);
    EXPECT_FALSE(d.adjustedRSquaredDefined);
    EXPECT_EQ(d.adjustedRSquared, 0.0f);
    EXPECT_FALSE(d.aicBicDefined);
    EXPECT_EQ(d.aic, 0.0f);
    EXPECT_EQ(d.bic, 0.0f);
}

// An exact fit has no AIC or BIC (ln 0), but its adjusted R² is defined.
TEST(FitDiagnostics, ExactFitCarriesNoAicFromEarlierFit)
{
    FitResult scored;
    scored.coefficients = {{"a", 1.0f}, {"b", 2.0f}};
    scored.rmse = 0.1f;
    scored.rSquared = 0.9f;
    (void)computeFitDiagnostics(scored, 10);

    FitResult exact = scored;
    exact.rmse = 0.0f;
    exact.rSquared = 1.0f;
    const FitDiagnostics d = computeFitDiagnostics(exact, 10);
    EXPECT_TRUE(d.adjustedRSquaredDefined);
    EXPECT_FLOAT_EQ(d.adjustedRSquared, 1.0f);
    EXPECT_FALSE(d.aicBicDefined);
    EXPECT_EQ(d.aic, 0.0f);
    EXPECT_EQ(d.bic, 0.0f);
}
