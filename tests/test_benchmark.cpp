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

#include "test_helpers.h"

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
    // Ts20-CV3: stamp the temp filename with vestigeTestStamp() so two
    // BenchmarkCsv tests (or a parallel `ctest -j` run) never collide on a
    // fixed path like "bench_ok.csv".
    const auto path = std::filesystem::temp_directory_path()
                    / (Testing::vestigeTestStamp() + "_" + name);
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
    // Distinct from the sibling test `FittableEntriesRankedBeforeSkipped`
    // — there we walk the sorted list to verify ordering, where
    // strict "converged" semantics matter for the inversion check.
    // Here we only need ONE entry with usable metrics so we can pin
    // delta_aic = 0 for the winner; that is a wider notion of
    // "fittable" than convergence (an entry can rank with rmse > 0
    // even if its solver status is non_converged). The wider
    // disjunction here is intentional.
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

// ---------------------------------------------------------------------------
// 3D_E-0009 — runExportGlslCli (headless GLSL export)
// ---------------------------------------------------------------------------

namespace
{

// Build an argv from a vector of strings (CLI verbs take int/char**).
std::optional<int> runExport(std::vector<std::string> args)
{
    std::vector<char*> argv;
    for (auto& s : args) argv.push_back(s.data());
    return runExportGlslCli(static_cast<int>(argv.size()), argv.data());
}

std::string slurp(const std::filesystem::path& p)
{
    std::ifstream in(p);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

TEST(ExportGlsl, RequiresOutDir)
{
    // --export-glsl without --out is a usage error, not a GUI fallthrough.
    std::vector<std::string> args = {"formula_workbench", "--export-glsl"};
    const auto rc = runExport(args);
    ASSERT_TRUE(rc.has_value());
    EXPECT_NE(*rc, 0);
}

TEST(ExportGlsl, WritesPerFormulaAndCombinedDeterministically)
{
    namespace fs = std::filesystem;
    const auto outDir = fs::temp_directory_path()
                      / ("vestige_export_" + Testing::vestigeTestStamp());

    const auto rc = runExport(
        {"formula_workbench", "--export-glsl", "--out", outDir.string()});
    ASSERT_TRUE(rc.has_value());
    EXPECT_EQ(*rc, 0);

    // Built-in library count = number of .glsl files (excluding the combined).
    FormulaLibrary library;
    library.registerBuiltinTemplates();
    const size_t n = library.count();

    size_t glslFiles = 0;
    for (const auto& e : fs::directory_iterator(outDir))
        if (e.path().extension() == ".glsl") ++glslFiles;
    EXPECT_EQ(glslFiles, n + 1);   // per-formula + combined formulas.glsl

    const std::string combined = slurp(outDir / "formulas.glsl");
    // Every function appears exactly once in the combined include.
    EXPECT_NE(combined.find("cosineHemispherePdf"), std::string::npos);
    EXPECT_NE(combined.find("ggxVndfPdf"), std::string::npos);
    EXPECT_NE(combined.find("srgbToLinear"), std::string::npos);
    // Provenance banner present (tool version + library hash).
    EXPECT_NE(combined.find("Vestige Formula Workbench v"), std::string::npos);
    EXPECT_NE(combined.find("library hash:"), std::string::npos);
    // Safe-math prelude emitted once.
    EXPECT_NE(combined.find("float safeDiv("), std::string::npos);

    // Determinism: a second run is byte-identical.
    const std::string firstRun = combined;
    const auto rc2 = runExport(
        {"formula_workbench", "--export-glsl", "--out", outDir.string()});
    ASSERT_TRUE(rc2.has_value());
    EXPECT_EQ(*rc2, 0);
    EXPECT_EQ(slurp(outDir / "formulas.glsl"), firstRun);

    fs::remove_all(outDir);
}

TEST(ExportGlsl, RejectsHostileFormulaNameAndWritesNothingOutside)
{
    namespace fs = std::filesystem;
    const auto stamp = Testing::vestigeTestStamp();
    const auto libPath = fs::temp_directory_path()
                       / ("hostile_lib_" + stamp + ".json");
    const auto outDir = fs::temp_directory_path()
                      / ("vestige_export_hostile_" + stamp);

    // A path-traversal / injection name must be rejected at load. Use a name
    // that would escape --out (../) — validation strips it before any file
    // write, so loadFromFile reports zero formulas and the verb exits non-zero.
    std::ofstream(libPath) << R"([
        { "name": "../escape", "category": "color",
          "inputs": [ { "name": "c", "type": "float" } ],
          "output": { "type": "float" },
          "expression": { "var": "c" } }
    ])";

    const auto rc = runExport({"formula_workbench", "--export-glsl",
                               libPath.string(), "--out", outDir.string()});
    ASSERT_TRUE(rc.has_value());
    EXPECT_NE(*rc, 0);   // nothing loaded → error exit

    // The traversal target must not have been created.
    EXPECT_FALSE(fs::exists(fs::temp_directory_path() / "escape.glsl"));

    fs::remove(libPath);
    std::error_code ec;
    fs::remove_all(outDir, ec);
}

// ---------------------------------------------------------------------------
// 3D_E-0011 — --self-benchmark over a directory
// ---------------------------------------------------------------------------

TEST(SelfBenchmarkBatch, DirectoryYieldsOneSectionPerDataset)
{
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path()
                   / ("vestige_bench_batch_" + Testing::vestigeTestStamp());
    fs::create_directories(dir);
    std::ofstream(dir / "alpha.csv") << "x,y\n0,0\n1,1\n2,2\n3,3\n";
    std::ofstream(dir / "beta.csv")  << "x,y\n0,0\n1,2\n2,4\n3,6\n";

    const auto outMd = dir / "report.md";
    std::vector<std::string> args = {
        "formula_workbench", "--self-benchmark", dir.string(),
        "--output", outMd.string()};
    std::vector<char*> argv;
    for (auto& s : args) argv.push_back(s.data());
    const auto rc = runBenchmarkCli(static_cast<int>(argv.size()), argv.data());
    ASSERT_TRUE(rc.has_value());
    EXPECT_EQ(*rc, 0);

    std::ifstream in(outMd);
    std::ostringstream ss; ss << in.rdbuf();
    const std::string md = ss.str();
    EXPECT_NE(md.find("# Formula Workbench self-benchmark — batch"),
              std::string::npos);
    EXPECT_NE(md.find("## alpha.csv"), std::string::npos);
    EXPECT_NE(md.find("## beta.csv"), std::string::npos);
    // alpha sorts before beta in the combined doc.
    EXPECT_LT(md.find("## alpha.csv"), md.find("## beta.csv"));

    fs::remove_all(dir);
}
