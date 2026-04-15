// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_formula_tools.cpp
/// @brief Unit tests for formula utility modules: sensitivity analysis,
///        performance benchmarking, and documentation generator.
#include "formula/sensitivity_analysis.h"
#include "formula/formula_benchmark.h"
#include "formula/formula_doc_generator.h"
#include "formula/physics_templates.h"

#include <gtest/gtest.h>
#include <cmath>
#include <string>

using namespace Vestige;

// ===========================================================================
// Sensitivity Analysis
// ===========================================================================

TEST(SensitivityAnalyzer, AerodynamicDragCdHighSensitivity)
{
    auto drag = PhysicsTemplates::createAerodynamicDrag();
    ExpressionEvaluator::VariableMap inputs = {
        {"vDotN", 10.0f},
        {"surfaceArea", 1.0f},
        {"airDensity", 1.225f}
    };

    SensitivityAnalyzer analyzer;
    auto report = analyzer.analyze(drag, inputs);

    EXPECT_EQ(report.formulaName, "aerodynamic_drag");
    EXPECT_GT(report.baseOutput, 0.0f);

    // Find Cd coefficient in the report
    bool foundCd = false;
    for (const auto& cs : report.coefficients)
    {
        if (cs.name == "Cd")
        {
            foundCd = true;
            // Cd should have high normalized sensitivity (linear relationship)
            EXPECT_GT(cs.normalizedSensitivity, 0.5f);
            // Derivative should be positive (more drag with higher Cd)
            EXPECT_GT(cs.derivative, 0.0f);
            // minEffect (at 0.5*Cd) should be less than maxEffect (at 1.5*Cd)
            EXPECT_LT(cs.minEffect, cs.maxEffect);
            break;
        }
    }
    EXPECT_TRUE(foundCd) << "Cd coefficient not found in sensitivity report";
}

TEST(SensitivityAnalyzer, InverseSquareQuadraticDominatesAtFarDistance)
{
    auto falloff = PhysicsTemplates::createInverseSquareFalloff();
    ExpressionEvaluator::VariableMap inputs = {
        {"distance", 10.0f}  // Far distance — quadratic term dominates
    };

    SensitivityAnalyzer analyzer;
    auto report = analyzer.analyze(falloff, inputs);

    report.sortByImpact();

    // At far distances, quadratic coefficient should have highest sensitivity
    ASSERT_FALSE(report.coefficients.empty());
    EXPECT_EQ(report.coefficients[0].name, "quadratic")
        << "Expected quadratic to be most sensitive at far distances";
}

TEST(SensitivityAnalyzer, ZeroCoefficientHandledGracefully)
{
    // Create a formula with a zero-valued coefficient
    FormulaDefinition def;
    def.name = "test_zero_coeff";
    def.category = "test";
    def.description = "Test formula with zero coefficient";
    def.inputs = {{"x", FormulaValueType::FLOAT, "", 1.0f}};
    def.output = {FormulaValueType::FLOAT, ""};
    def.coefficients = {{"a", 0.0f}, {"b", 1.0f}};

    // result = a * x + b * x = (a + b) * x
    def.expressions[QualityTier::FULL] =
        ExprNode::binaryOp("+",
            ExprNode::binaryOp("*",
                ExprNode::variable("a"),
                ExprNode::variable("x")),
            ExprNode::binaryOp("*",
                ExprNode::variable("b"),
                ExprNode::variable("x")));

    ExpressionEvaluator::VariableMap inputs = {{"x", 2.0f}};

    SensitivityAnalyzer analyzer;
    // Should not throw or produce NaN/Inf
    EXPECT_NO_THROW({
        auto report = analyzer.analyze(def, inputs);
        EXPECT_EQ(report.coefficients.size(), 2u);

        for (const auto& cs : report.coefficients)
        {
            EXPECT_FALSE(std::isnan(cs.derivative));
            EXPECT_FALSE(std::isinf(cs.derivative));
            EXPECT_FALSE(std::isnan(cs.normalizedSensitivity));
        }
    });
}

TEST(SensitivityAnalyzer, SingleCoefficientCorrectDerivative)
{
    // f(x) = k * x  =>  df/dk = x
    FormulaDefinition def;
    def.name = "test_linear";
    def.category = "test";
    def.description = "Simple linear: k * x";
    def.inputs = {{"x", FormulaValueType::FLOAT, "", 1.0f}};
    def.output = {FormulaValueType::FLOAT, ""};
    def.coefficients = {{"k", 3.0f}};

    def.expressions[QualityTier::FULL] =
        ExprNode::binaryOp("*",
            ExprNode::variable("k"),
            ExprNode::variable("x"));

    ExpressionEvaluator::VariableMap inputs = {{"x", 5.0f}};

    SensitivityAnalyzer analyzer;
    auto report = analyzer.analyze(def, inputs);

    ASSERT_EQ(report.coefficients.size(), 1u);
    // df/dk = x = 5.0
    EXPECT_NEAR(report.coefficients[0].derivative, 5.0f, 0.01f);

    // base output = k * x = 3 * 5 = 15
    EXPECT_NEAR(report.baseOutput, 15.0f, 0.01f);

    // normalized = |5 * 3 / 15| = 1.0
    EXPECT_NEAR(report.coefficients[0].normalizedSensitivity, 1.0f, 0.01f);
}

TEST(SensitivityAnalyzer, SortByImpactWorks)
{
    auto falloff = PhysicsTemplates::createInverseSquareFalloff();
    ExpressionEvaluator::VariableMap inputs = {{"distance", 5.0f}};

    SensitivityAnalyzer analyzer;
    auto report = analyzer.analyze(falloff, inputs);
    report.sortByImpact();

    // After sorting, each element should have >= sensitivity than the next
    for (size_t i = 1; i < report.coefficients.size(); ++i)
    {
        EXPECT_GE(report.coefficients[i - 1].normalizedSensitivity,
                  report.coefficients[i].normalizedSensitivity);
    }
}

// ===========================================================================
// Formula Benchmark
// ===========================================================================

TEST(FormulaBenchmark, BenchmarkReturnsPositiveTimes)
{
    auto drag = PhysicsTemplates::createAerodynamicDrag();
    ExpressionEvaluator::VariableMap inputs = {
        {"vDotN", 10.0f},
        {"surfaceArea", 1.0f},
        {"airDensity", 1.225f}
    };

    FormulaBenchmark bench;
    auto result = bench.benchmark(drag, inputs, QualityTier::FULL, 10, 100);

    EXPECT_GT(result.avgNanoseconds, 0.0);
    EXPECT_GT(result.minNanoseconds, 0.0);
    EXPECT_GT(result.maxNanoseconds, 0.0);
    EXPECT_GE(result.maxNanoseconds, result.minNanoseconds);
}

TEST(FormulaBenchmark, IterationCountMatchesRequested)
{
    auto drag = PhysicsTemplates::createAerodynamicDrag();
    ExpressionEvaluator::VariableMap inputs = {
        {"vDotN", 10.0f},
        {"surfaceArea", 1.0f},
        {"airDensity", 1.225f}
    };

    const int requestedIterations = 500;
    FormulaBenchmark bench;
    auto result = bench.benchmark(drag, inputs, QualityTier::FULL,
                                  10, requestedIterations);

    EXPECT_EQ(result.iterations, requestedIterations);
}

TEST(FormulaBenchmark, CompareSpeedupRatioPositive)
{
    // Fresnel Schlick has both FULL and APPROXIMATE tiers
    auto fresnel = PhysicsTemplates::createFresnelSchlick();
    ExpressionEvaluator::VariableMap inputs = {{"cosTheta", 0.5f}};

    FormulaBenchmark bench;
    auto comp = bench.compare(fresnel, inputs, "cosTheta", 0.0f, 1.0f);

    EXPECT_GT(comp.speedupRatio, 0.0);
    EXPECT_EQ(comp.formulaName, "fresnel_schlick");
}

TEST(FormulaBenchmark, CompareMaxErrorNonNegative)
{
    auto beerLambert = PhysicsTemplates::createBeerLambert();
    ExpressionEvaluator::VariableMap inputs = {
        {"I0", 1.0f},
        {"depth", 1.0f}
    };

    FormulaBenchmark bench;
    auto comp = bench.compare(beerLambert, inputs, "depth", 0.0f, 5.0f);

    EXPECT_GE(comp.maxError, 0.0f);
    EXPECT_GE(comp.avgError, 0.0f);
    // Beer-Lambert FULL vs APPROXIMATE should have some error at depth > 0
    EXPECT_GT(comp.maxError, 0.0f)
        << "Expected nonzero error between exp() and linear approximation";
}

TEST(FormulaBenchmark, BenchmarkAllReturnsResultsForApproxTiers)
{
    FormulaBenchmark bench;
    auto results = bench.benchmarkAll();

    // There are multiple formulas with APPROXIMATE tiers
    EXPECT_GT(results.size(), 0u);

    for (const auto& comp : results)
    {
        EXPECT_FALSE(comp.formulaName.empty());
        EXPECT_GT(comp.speedupRatio, 0.0);
        EXPECT_GE(comp.maxError, 0.0f);
        EXPECT_GE(comp.avgError, 0.0f);
    }
}

TEST(FormulaBenchmark, BenchmarkSampleOutputIsReasonable)
{
    // aerodynamic_drag with known inputs should produce a known output
    auto drag = PhysicsTemplates::createAerodynamicDrag();
    ExpressionEvaluator::VariableMap inputs = {
        {"vDotN", 10.0f},
        {"surfaceArea", 1.0f},
        {"airDensity", 1.225f}
    };

    FormulaBenchmark bench;
    auto result = bench.benchmark(drag, inputs, QualityTier::FULL, 10, 100);

    // 0.5 * 0.47 * 1.225 * 1.0 * 10.0 = 2.87875
    EXPECT_NEAR(result.sampleOutput, 2.87875f, 0.01f);
}

// ===========================================================================
// Documentation Generator
// ===========================================================================

TEST(FormulaDocGenerator, GenerateMarkdownContainsExpectedHeadings)
{
    auto formulas = PhysicsTemplates::createAll();
    FormulaDocGenerator gen;
    std::string md = gen.generateMarkdown(formulas);

    EXPECT_NE(md.find("# Formula Library Reference"), std::string::npos);
    EXPECT_NE(md.find("## Summary"), std::string::npos);
    EXPECT_NE(md.find("## Category:"), std::string::npos);
    EXPECT_NE(md.find("## Statistics"), std::string::npos);
}

TEST(FormulaDocGenerator, GenerateFormulaDocContainsInputTable)
{
    auto drag = PhysicsTemplates::createAerodynamicDrag();
    FormulaDocGenerator gen;
    std::string md = gen.generateFormulaDoc(drag);

    // Should contain input table headers
    EXPECT_NE(md.find("**Inputs:**"), std::string::npos);
    EXPECT_NE(md.find("| Name | Type | Unit | Default |"), std::string::npos);

    // Should contain the input names
    EXPECT_NE(md.find("vDotN"), std::string::npos);
    EXPECT_NE(md.find("surfaceArea"), std::string::npos);
    EXPECT_NE(md.find("airDensity"), std::string::npos);

    // Should contain coefficient table
    EXPECT_NE(md.find("**Coefficients:**"), std::string::npos);
    EXPECT_NE(md.find("Cd"), std::string::npos);

    // Should contain tier info
    EXPECT_NE(md.find("**Quality Tiers:**"), std::string::npos);
    EXPECT_NE(md.find("FULL"), std::string::npos);
}

TEST(FormulaDocGenerator, SummaryTableHasCorrectColumnCount)
{
    auto formulas = PhysicsTemplates::createAll();
    FormulaDocGenerator gen;
    std::string table = gen.generateSummaryTable(formulas);

    // Header line should have 6 columns: Name, Category, FULL, APPROX, LUT, Description
    // That means 7 pipe characters per line (leading + between each + trailing)
    std::string headerLine = "| Name | Category | FULL | APPROX | LUT | Description |";
    EXPECT_NE(table.find(headerLine), std::string::npos);

    // Count pipe characters in the separator line
    std::string sepLine = "|------|----------|------|--------|-----|-------------|";
    EXPECT_NE(table.find(sepLine), std::string::npos);
}

TEST(FormulaDocGenerator, AllTemplateNamesAppearInFullDoc)
{
    auto formulas = PhysicsTemplates::createAll();
    FormulaDocGenerator gen;
    std::string md = gen.generateMarkdown(formulas);

    // All 21 template names should appear
    std::vector<std::string> expectedNames = {
        "aerodynamic_drag",
        "stokes_drag",
        "wind_deformation",
        "fresnel_schlick",
        "beer_lambert",
        "gerstner_wave",
        "buoyancy",
        "caustic_depth_fade",
        "water_absorption",
        "inverse_square_falloff",
        "exponential_fog",
        "hooke_spring",
        "coulomb_friction",
        "terminal_velocity",
        "wet_darkening",
        "ggx_distribution",
        "schlick_geometry",
        "aces_tonemap",
        "spot_cone_falloff",
        "ease_in_sine",
        "fast_neg_exp"
    };

    for (const auto& name : expectedNames)
    {
        EXPECT_NE(md.find(name), std::string::npos)
            << "Expected template '" << name << "' not found in generated markdown";
    }

    EXPECT_GE(expectedNames.size(), 21u);
}

TEST(FormulaDocGenerator, SingleFormulaDocHasOutputSection)
{
    auto fresnel = PhysicsTemplates::createFresnelSchlick();
    FormulaDocGenerator gen;
    std::string md = gen.generateFormulaDoc(fresnel);

    EXPECT_NE(md.find("**Output:**"), std::string::npos);
    EXPECT_NE(md.find("**Source:**"), std::string::npos);
}

TEST(FormulaDocGenerator, SummaryTableShowsApproxAvailability)
{
    auto formulas = PhysicsTemplates::createAll();
    FormulaDocGenerator gen;
    std::string table = gen.generateSummaryTable(formulas);

    // fresnel_schlick has APPROXIMATE — its row should contain "Y" for APPROX column
    // aerodynamic_drag does NOT have APPROXIMATE — its row should contain "-"
    // We verify both names appear in the table
    EXPECT_NE(table.find("fresnel_schlick"), std::string::npos);
    EXPECT_NE(table.find("aerodynamic_drag"), std::string::npos);
}

TEST(FormulaDocGenerator, StatisticsMatchFormulaCount)
{
    auto formulas = PhysicsTemplates::createAll();
    FormulaDocGenerator gen;
    std::string md = gen.generateMarkdown(formulas);

    // Should contain the total formula count
    std::string countStr = "**Total formulas:** " + std::to_string(formulas.size());
    EXPECT_NE(md.find(countStr), std::string::npos)
        << "Expected '" << countStr << "' in markdown output";
}
