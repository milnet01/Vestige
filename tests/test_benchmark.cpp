// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_benchmark.cpp
/// @brief Unit tests for the FormulaWorkbench self-benchmark module.
///
/// §3.3 of the self-learning design. Covers:
///   - computeAicBic formulas (including degeneracy guards)
///   - loadCsvDataset happy path + error cases
///   - runBenchmark ranking (fittable first, AIC ascending, ΔAIC)
///   - renderBenchmarkMarkdown structural shape
#include <gtest/gtest.h>

#include "benchmark.h"
#include "formula/formula_library.h"

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace Vestige;

// ---------------------------------------------------------------------------
// computeAicBic
// ---------------------------------------------------------------------------

TEST(BenchmarkAicBic, MatchesClosedForm)
{
    // Hand-computed reference:
    //   n=10, k=2, rmse=0.1  → SSE = 0.01 * 10 = 0.1
    //   AIC = 10·ln(0.1/10) + 2·2 = 10·ln(0.01) + 4
    //       = 10·(-4.6052) + 4 = -42.052
    //   BIC = 10·ln(0.01) + 2·ln(10)
    //       = -46.052 + 4.605 = -41.446
    const auto ic = computeAicBic(0.1f, 10, 2);
    EXPECT_FALSE(ic.degenerate);
    EXPECT_NEAR(ic.aic, -42.052f, 0.01f);
    EXPECT_NEAR(ic.bic, -41.446f, 0.01f);
}

TEST(BenchmarkAicBic, FewerPointsThanParamsDegenerate)
{
    // n=3, k=3 → n > k+1 is false → degenerate.
    const auto ic = computeAicBic(0.1f, 3, 3);
    EXPECT_TRUE(ic.degenerate);
    EXPECT_EQ(ic.aic, 0.0f);
    EXPECT_EQ(ic.bic, 0.0f);
}

TEST(BenchmarkAicBic, ZeroRmseDegenerate)
{
    // Perfect fit → SSE = 0 → ln(0) undefined → degenerate.
    const auto ic = computeAicBic(0.0f, 100, 2);
    EXPECT_TRUE(ic.degenerate);
}

TEST(BenchmarkAicBic, MorePointsMakesBicPenalizeMore)
{
    // As n grows, BIC penalty for the same k widens relative to AIC.
    const auto small = computeAicBic(0.1f, 10,  2);
    const auto large = computeAicBic(0.1f, 100, 2);
    // Both shift, but the difference (BIC-AIC) grows monotonically
    // with n because BIC pays k·ln(n) and AIC pays 2k.
    EXPECT_GT(large.bic - large.aic, small.bic - small.aic);
}

// ---------------------------------------------------------------------------
// loadCsvDataset
// ---------------------------------------------------------------------------

namespace
{

std::string writeTempCsv(const std::string& name, const std::string& contents)
{
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path);
    out << contents;
    return path.string();
}

} // namespace

TEST(BenchmarkCsv, MissingFileErrors)
{
    std::string err;
    const auto data = loadCsvDataset(
        "/tmp/does_not_exist_" __FILE__ ".csv", err);
    EXPECT_FALSE(data.has_value());
    EXPECT_FALSE(err.empty());
}

TEST(BenchmarkCsv, EmptyFileErrors)
{
    const auto path = writeTempCsv("bench_empty.csv", "");
    std::string err;
    const auto data = loadCsvDataset(path, err);
    EXPECT_FALSE(data.has_value());
    std::filesystem::remove(path);
}

TEST(BenchmarkCsv, SingleColumnErrors)
{
    const auto path = writeTempCsv("bench_single.csv",
                                   "y\n1.0\n2.0\n3.0\n");
    std::string err;
    const auto data = loadCsvDataset(path, err);
    EXPECT_FALSE(data.has_value());
    std::filesystem::remove(path);
}

TEST(BenchmarkCsv, ParsesHeaderAndRows)
{
    const auto path = writeTempCsv("bench_ok.csv",
                                   "x,y\n1.0,2.5\n2.0,5.1\n3.0,7.4\n");
    std::string err;
    const auto data = loadCsvDataset(path, err);
    ASSERT_TRUE(data.has_value()) << err;
    ASSERT_EQ(data->size(), 3u);
    EXPECT_FLOAT_EQ((*data)[0].variables.at("x"), 1.0f);
    EXPECT_FLOAT_EQ((*data)[0].observed, 2.5f);
    EXPECT_FLOAT_EQ((*data)[2].observed, 7.4f);
    std::filesystem::remove(path);
}

TEST(BenchmarkCsv, SkipsNonNumericRowsButKeepsOthers)
{
    const auto path = writeTempCsv("bench_mixed.csv",
                                   "x,y\n1.0,2.5\nNaN,bad\n3.0,7.4\n");
    std::string err;
    const auto data = loadCsvDataset(path, err);
    ASSERT_TRUE(data.has_value()) << err;
    // The NaN row is tolerated (std::stof handles "NaN") but "bad"
    // is rejected, which trips our row_ok flag → row skipped.
    // Remaining rows: 2.
    EXPECT_EQ(data->size(), 2u);
    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// runBenchmark
// ---------------------------------------------------------------------------

TEST(BenchmarkRun, EmptyLibraryProducesEmptyResult)
{
    FormulaLibrary empty_library;
    const auto data = std::vector<DataPoint>{};
    const auto out = runBenchmark(empty_library, data);
    EXPECT_TRUE(out.empty());
}

TEST(BenchmarkRun, FittableEntriesRankedBeforeSkipped)
{
    // Use the real library — we just need ordering behaviour to hold.
    FormulaLibrary lib;
    lib.registerBuiltinTemplates();

    // Construct a dataset on variable 'x' mimicking y = sin(x).
    std::vector<DataPoint> data;
    for (int i = 1; i <= 10; ++i)
    {
        DataPoint dp;
        const float x = 0.1f * static_cast<float>(i);
        dp.variables["x"] = x;
        dp.observed = std::sin(x);
        data.push_back(std::move(dp));
    }

    const auto out = runBenchmark(lib, data);
    ASSERT_FALSE(out.empty());

    // Walk the sorted list: once we see a skipped entry, no
    // fittable entry should appear after it.
    // Slice 18 Ts1: pin `fit` strictly on the bench's own
    // "converged" status — the prior `rmse > 0 || r_squared > 0`
    // disjunction classified a perfect-fit formula (rmse=0,
    // r²=1.0) as not-fit if its status was non-"converged",
    // hiding an ordering bug.
    bool seen_skipped = false;
    for (const auto& e : out)
    {
        const bool fit = e.status == "converged";
        if (!fit) seen_skipped = true;
        else if (seen_skipped)
            FAIL() << "Fittable entry '" << e.formula_name
                   << "' appeared after a skipped entry — "
                      "sort order is broken.";
    }
}

TEST(BenchmarkRun, DeltaAicZeroForWinner)
{
    // Same sin(x) dataset as above — the top-ranked (fittable)
    // entry should report delta_aic = 0 by definition.
    FormulaLibrary lib;
    lib.registerBuiltinTemplates();
    std::vector<DataPoint> data;
    for (int i = 1; i <= 20; ++i)
    {
        DataPoint dp;
        const float x = 0.2f * static_cast<float>(i);
        dp.variables["x"] = x;
        dp.observed = std::sin(x);
        data.push_back(std::move(dp));
    }

    const auto out = runBenchmark(lib, data);
    // Find the first fittable entry; its delta_aic should be 0.
    for (const auto& e : out)
    {
        const bool fit = e.rmse > 0.0f || e.r_squared > 0.0f
                      || e.status == "converged";
        if (fit)
        {
            EXPECT_NEAR(e.delta_aic, 0.0f, 1e-3f);
            return;
        }
    }
    // If every formula got skipped, the test data was wrong.
    FAIL() << "No fittable entries — test dataset insufficient.";
}

// ---------------------------------------------------------------------------
// renderBenchmarkMarkdown
// ---------------------------------------------------------------------------

TEST(BenchmarkRender, IncludesHeaderAndSummary)
{
    const auto out = renderBenchmarkMarkdown({});
    EXPECT_NE(out.find("# Formula Workbench self-benchmark"),
              std::string::npos);
    EXPECT_NE(out.find("**Fitted:** 0"), std::string::npos);
    EXPECT_NE(out.find("**Skipped:** 0"), std::string::npos);
}

TEST(BenchmarkRender, GroupsFittedSeparatelyFromSkipped)
{
    BenchmarkEntry fit;
    fit.formula_name = "good_fit";
    fit.r_squared = 0.99f;
    fit.rmse = 0.01f;
    fit.aic = -100.0f;
    fit.bic = -95.0f;
    fit.iterations = 12;
    fit.converged = true;
    fit.status = "converged";
    fit.delta_aic = 0.0f;
    fit.param_count = 2;

    BenchmarkEntry skip;
    skip.formula_name = "needs_var_t";
    skip.status = "dataset lacks required input variables";

    const auto md = renderBenchmarkMarkdown({fit, skip});
    EXPECT_NE(md.find("## Leaderboard"), std::string::npos);
    EXPECT_NE(md.find("## Skipped formulas"), std::string::npos);
    EXPECT_NE(md.find("`good_fit`"), std::string::npos);
    EXPECT_NE(md.find("`needs_var_t`"), std::string::npos);
    EXPECT_NE(md.find("dataset lacks required input variables"),
              std::string::npos);
}
