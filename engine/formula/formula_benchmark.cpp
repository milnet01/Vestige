// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file formula_benchmark.cpp
/// @brief Performance benchmarking implementation for formula quality tiers.
#include "formula/formula_benchmark.h"
#include "formula/physics_templates.h"

#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace Vestige
{

// ---------------------------------------------------------------------------
// FormulaBenchmark::benchmark
// ---------------------------------------------------------------------------

BenchmarkResult FormulaBenchmark::benchmark(
    const FormulaDefinition& formula,
    const ExpressionEvaluator::VariableMap& inputValues,
    QualityTier tier,
    int warmupIterations,
    int timedIterations) const
{
    const ExprNode* expr = formula.getExpression(tier);
    if (!expr)
    {
        throw std::runtime_error(
            "FormulaBenchmark: no expression for tier in formula '" +
            formula.name + "'");
    }

    ExpressionEvaluator evaluator;
    std::unordered_map<std::string, float> coeffs(
        formula.coefficients.begin(), formula.coefficients.end());

    // Warmup — get cache hot
    volatile float warmupSink = 0.0f;
    for (int i = 0; i < warmupIterations; ++i)
    {
        warmupSink = evaluator.evaluate(*expr, inputValues, coeffs);
    }

    // Timed iterations
    double totalNs = 0.0;
    double minNs = std::numeric_limits<double>::max();
    double maxNs = 0.0;
    float lastOutput = 0.0f;

    for (int i = 0; i < timedIterations; ++i)
    {
        auto start = std::chrono::high_resolution_clock::now();
        lastOutput = evaluator.evaluate(*expr, inputValues, coeffs);
        auto end = std::chrono::high_resolution_clock::now();

        double ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                end - start).count());

        totalNs += ns;
        minNs = std::fmin(minNs, ns);
        maxNs = std::fmax(maxNs, ns);
    }

    BenchmarkResult result;
    result.formulaName = formula.name;
    result.tier = tier;
    result.avgNanoseconds = totalNs / static_cast<double>(timedIterations);
    result.minNanoseconds = minNs;
    result.maxNanoseconds = maxNs;
    result.iterations = timedIterations;
    result.sampleOutput = lastOutput;

    // Suppress unused warning for warmupSink
    (void)warmupSink;

    return result;
}

// ---------------------------------------------------------------------------
// FormulaBenchmark::compare
// ---------------------------------------------------------------------------

BenchmarkComparison FormulaBenchmark::compare(
    const FormulaDefinition& formula,
    const ExpressionEvaluator::VariableMap& baseInputs,
    const std::string& sweepVariable,
    float sweepMin,
    float sweepMax,
    int sweepSamples) const
{
    BenchmarkComparison comp;
    comp.formulaName = formula.name;

    // Benchmark both tiers using base inputs
    comp.fullResult = benchmark(formula, baseInputs, QualityTier::FULL);
    comp.approxResult = benchmark(formula, baseInputs, QualityTier::APPROXIMATE);

    // Speedup ratio
    if (comp.approxResult.avgNanoseconds > 0.0)
    {
        comp.speedupRatio =
            comp.fullResult.avgNanoseconds / comp.approxResult.avgNanoseconds;
    }
    else
    {
        comp.speedupRatio = 1.0;
    }

    // Sweep one variable across range to compute error metrics
    const ExprNode* fullExpr = formula.getExpression(QualityTier::FULL);
    const ExprNode* approxExpr = formula.getExpression(QualityTier::APPROXIMATE);

    if (!fullExpr || !approxExpr)
    {
        comp.maxError = 0.0f;
        comp.avgError = 0.0f;
        return comp;
    }

    ExpressionEvaluator evaluator;
    std::unordered_map<std::string, float> coeffs(
        formula.coefficients.begin(), formula.coefficients.end());

    float maxErr = 0.0f;
    float totalErr = 0.0f;
    int validSamples = 0;

    for (int i = 0; i < sweepSamples; ++i)
    {
        float t = (sweepSamples > 1)
            ? static_cast<float>(i) / static_cast<float>(sweepSamples - 1)
            : 0.5f;
        float sweepVal = sweepMin + t * (sweepMax - sweepMin);

        // Create modified input map with sweep variable
        ExpressionEvaluator::VariableMap sweepInputs = baseInputs;
        sweepInputs[sweepVariable] = sweepVal;

        float fullVal = evaluator.evaluate(*fullExpr, sweepInputs, coeffs);
        float approxVal = evaluator.evaluate(*approxExpr, sweepInputs, coeffs);

        float err = std::fabs(fullVal - approxVal);
        maxErr = std::fmax(maxErr, err);
        totalErr += err;
        ++validSamples;
    }

    comp.maxError = maxErr;
    comp.avgError = (validSamples > 0)
        ? totalErr / static_cast<float>(validSamples)
        : 0.0f;

    return comp;
}

// ---------------------------------------------------------------------------
// FormulaBenchmark::benchmarkAll
// ---------------------------------------------------------------------------

std::vector<BenchmarkComparison> FormulaBenchmark::benchmarkAll() const
{
    auto templates = PhysicsTemplates::createAll();
    std::vector<BenchmarkComparison> results;

    for (const auto& formula : templates)
    {
        // Only compare formulas that have an APPROXIMATE tier
        if (!formula.hasTier(QualityTier::APPROXIMATE))
        {
            continue;
        }

        // Build default input values from the formula's declared inputs
        ExpressionEvaluator::VariableMap inputs;
        for (const auto& inp : formula.inputs)
        {
            inputs[inp.name] = inp.defaultValue;
        }

        // Use the first input variable as the sweep variable
        if (formula.inputs.empty())
        {
            continue;
        }

        const auto& sweepInput = formula.inputs[0];
        float sweepMin = sweepInput.defaultValue * 0.1f;
        float sweepMax = sweepInput.defaultValue * 2.0f;

        // Handle zero default — use a sensible range
        if (std::fabs(sweepInput.defaultValue) < 1e-6f)
        {
            sweepMin = 0.0f;
            sweepMax = 1.0f;
        }

        results.push_back(compare(formula, inputs, sweepInput.name,
                                  sweepMin, sweepMax));
    }

    return results;
}

} // namespace Vestige
