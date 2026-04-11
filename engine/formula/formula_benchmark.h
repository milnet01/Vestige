/// @file formula_benchmark.h
/// @brief Performance benchmarking for formula quality tiers.
///
/// Times formula evaluation at each quality tier and computes speedup ratios
/// and accuracy error metrics for APPROXIMATE vs FULL comparisons.
#pragma once

#include "formula/expression_eval.h"
#include "formula/formula.h"

#include <string>
#include <vector>

namespace Vestige
{

/// @brief Timing results for a single formula benchmark run.
struct BenchmarkResult
{
    std::string formulaName;
    QualityTier tier;
    double avgNanoseconds;      ///< Average time per evaluation
    double minNanoseconds;
    double maxNanoseconds;
    int iterations;
    float sampleOutput;         ///< Output value for correctness check
};

/// @brief Comparison of FULL vs APPROXIMATE tiers for one formula.
struct BenchmarkComparison
{
    std::string formulaName;
    BenchmarkResult fullResult;
    BenchmarkResult approxResult;
    double speedupRatio;        ///< full.avg / approx.avg
    float maxError;             ///< max |full - approx| over sample range
    float avgError;             ///< avg |full - approx| over sample range
};

/// @brief Benchmarks formula evaluation performance across quality tiers.
///
/// Uses std::chrono::high_resolution_clock with warmup iterations to
/// get cache-hot timing measurements.
class FormulaBenchmark
{
public:
    /// @brief Benchmark a single formula at a specific tier.
    /// @param formula The formula to benchmark.
    /// @param inputValues Variable bindings for evaluation.
    /// @param tier Quality tier to evaluate.
    /// @param warmupIterations Number of warmup iterations before timing.
    /// @param timedIterations Number of timed iterations for measurement.
    /// @return Timing results.
    BenchmarkResult benchmark(
        const FormulaDefinition& formula,
        const ExpressionEvaluator::VariableMap& inputValues,
        QualityTier tier = QualityTier::FULL,
        int warmupIterations = 100,
        int timedIterations = 10000) const;

    /// @brief Compare FULL vs APPROXIMATE tiers.
    /// @param formula The formula to compare.
    /// @param baseInputs Base variable bindings.
    /// @param sweepVariable Name of the variable to sweep across range.
    /// @param sweepMin Minimum value for sweep variable.
    /// @param sweepMax Maximum value for sweep variable.
    /// @param sweepSamples Number of samples across the sweep range.
    /// @return Comparison with speedup ratio and error metrics.
    BenchmarkComparison compare(
        const FormulaDefinition& formula,
        const ExpressionEvaluator::VariableMap& baseInputs,
        const std::string& sweepVariable,
        float sweepMin,
        float sweepMax,
        int sweepSamples = 100) const;

    /// @brief Benchmark all formulas from PhysicsTemplates::createAll().
    /// @return Comparisons for templates that have APPROXIMATE tiers.
    std::vector<BenchmarkComparison> benchmarkAll() const;
};

} // namespace Vestige
