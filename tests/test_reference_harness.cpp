// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_reference_harness.cpp
/// @brief Regression harness for the FormulaWorkbench reference cases.
///
/// §3.4 of the self-learning design. Iterates every
/// ``tools/formula_workbench/reference_cases/*.json`` spec, runs
/// ``executeReferenceCase`` against the current library + fitter,
/// and fails the build if any case's R²/RMSE/coefficients breach
/// their declared bounds.
///
/// The suite uses parameterized tests so a new reference case lands
/// as a green ✓ automatically the moment someone drops a spec file
/// into ``reference_cases/``.
#include <gtest/gtest.h>

#include "reference_harness.h"

#include "formula/formula_library.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Vestige;

namespace
{

/// The reference_cases directory lives in the source tree, not the
/// build tree. CMake passes the source dir via the
/// ``VESTIGE_REFERENCE_CASES_DIR`` compile definition so the test
/// can find it regardless of where it's run from.
constexpr const char* kReferenceCasesDir =
#ifdef VESTIGE_REFERENCE_CASES_DIR
    VESTIGE_REFERENCE_CASES_DIR;
#else
    "tools/formula_workbench/reference_cases";
#endif

std::vector<std::string> discoverReferenceCases()
{
    std::vector<std::string> out;
    std::error_code ec;
    if (!std::filesystem::exists(kReferenceCasesDir, ec)) return out;
    for (const auto& entry : std::filesystem::directory_iterator(
             kReferenceCasesDir, ec))
    {
        if (entry.path().extension() == ".json")
            out.push_back(entry.path().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Parameterized test — one instance per discovered reference case
// ---------------------------------------------------------------------------

class ReferenceCaseRun : public ::testing::TestWithParam<std::string>
{
};

TEST_P(ReferenceCaseRun, FitRecoversCoefficientsWithinBounds)
{
    const std::string& path = GetParam();

    std::string err;
    const auto loaded = loadReferenceCase(path, err);
    ASSERT_TRUE(loaded.has_value())
        << "Failed to parse reference case " << path << ": " << err;

    FormulaLibrary library;
    library.registerBuiltinTemplates();

    const auto result = executeReferenceCase(*loaded, library);
    if (!result.passed)
    {
        std::string combined;
        for (const auto& f : result.failures)
            combined += "\n  - " + f;
        ADD_FAILURE() << "Reference case FAILED: " << path
                      << " (formula: " << result.formula_name
                      << ", n_points=" << result.n_points << ")"
                      << combined;
    }
}

INSTANTIATE_TEST_SUITE_P(
    AllReferenceCases,
    ReferenceCaseRun,
    ::testing::ValuesIn(discoverReferenceCases()),
    [](const ::testing::TestParamInfo<std::string>& info)
    {
        // Parameterized test name is the spec file stem — e.g.
        // "beer_lambert" — so a failure line like
        // ``AllReferenceCases/ReferenceCaseRun.FitRecoversCoefficientsWithinBounds/beer_lambert``
        // tells you exactly which case tripped.
        std::filesystem::path p(info.param);
        std::string stem = p.stem().string();
        // GoogleTest requires only alphanumerics + underscore in
        // parameter names — filter anything else (paranoia; our
        // filenames are already well-behaved).
        for (char& c : stem) if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') c = '_';
        return stem;
    }
);

// ---------------------------------------------------------------------------
// Meta-test: directory discovery itself
// ---------------------------------------------------------------------------

TEST(ReferenceCaseDirectory, FindsAtLeastOneCase)
{
    const auto paths = discoverReferenceCases();
    EXPECT_FALSE(paths.empty())
        << "No reference cases discovered under " << kReferenceCasesDir
        << " — regression harness has nothing to check.";
}

// ---------------------------------------------------------------------------
// Unit tests for the harness itself (not a reference case round-trip)
// ---------------------------------------------------------------------------

TEST(ReferenceCaseLoad, RejectsMissingFormulaName)
{
    const auto path = std::filesystem::temp_directory_path() / "bad_case.json";
    std::ofstream(path) << R"({"expected": {}})";
    std::string err;
    const auto c = loadReferenceCase(path.string(), err);
    EXPECT_FALSE(c.has_value());
    EXPECT_FALSE(err.empty());
    std::filesystem::remove(path);
}

TEST(ReferenceCaseLoad, RejectsMalformedJson)
{
    const auto path = std::filesystem::temp_directory_path() / "malformed.json";
    std::ofstream(path) << "{not valid json";
    std::string err;
    const auto c = loadReferenceCase(path.string(), err);
    EXPECT_FALSE(c.has_value());
    std::filesystem::remove(path);
}

TEST(ReferenceCaseExec, UnknownFormulaFailsCleanly)
{
    ReferenceCase c;
    c.formula_name = "nonexistent_formula";
    FormulaLibrary library;
    library.registerBuiltinTemplates();
    const auto r = executeReferenceCase(c, library);
    EXPECT_FALSE(r.passed);
    ASSERT_FALSE(r.failures.empty());
    EXPECT_NE(r.failures[0].find("not found"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Workbench 1.17.0 — step-based sweeps
// ---------------------------------------------------------------------------

TEST(ReferenceCaseSweep, StepBasedSweepIncludesBothEndpoints)
{
    // Formula: `exponential_fog` — f = 1 - exp(-density * distance).
    // Step-sized sweep from 0 to 100 step 10 → 11 points inclusive.
    FormulaLibrary library;
    library.registerBuiltinTemplates();

    ReferenceCase c;
    c.formula_name = "exponential_fog";
    c.canonical_coefficients["density"] = 0.01f;
    InputSweep s;
    s.min = 0.0f;
    s.max = 100.0f;
    s.step = 10.0f;
    c.input_sweep["distance"] = s;

    const auto data = synthesizeDataset(c, library);
    EXPECT_EQ(data.size(), 11u);

    // First sample at `distance = 0` (observed f = 0).
    EXPECT_FLOAT_EQ(data.front().variables.at("distance"), 0.0f);
    // Last sample at `distance = 100` exactly.
    EXPECT_FLOAT_EQ(data.back().variables.at("distance"), 100.0f);
}

TEST(ReferenceCaseSweep, StepBasedSweepAppendsEndpointIfStepDoesNotDivideRange)
{
    // Range 0.0 → 0.1 with step 0.03 → samples {0, 0.03, 0.06, 0.09}
    // then we append the endpoint 0.1 as the tail sample so the
    // upper bound is never clipped off.
    FormulaLibrary library;
    library.registerBuiltinTemplates();

    ReferenceCase c;
    c.formula_name = "exponential_fog";
    c.canonical_coefficients["density"] = 1.0f;
    InputSweep s;
    s.min = 0.0f;
    s.max = 0.1f;
    s.step = 0.03f;
    c.input_sweep["distance"] = s;

    const auto data = synthesizeDataset(c, library);
    // 4 step points + 1 endpoint = 5.
    EXPECT_EQ(data.size(), 5u);
    EXPECT_FLOAT_EQ(data.back().variables.at("distance"), 0.1f);
}

TEST(ReferenceCaseSweep, StepZeroFallsThroughToCountBased)
{
    // Sweep with step=0 and count=5 exercises the existing count path.
    FormulaLibrary library;
    library.registerBuiltinTemplates();

    ReferenceCase c;
    c.formula_name = "exponential_fog";
    c.canonical_coefficients["density"] = 0.01f;
    InputSweep s;
    s.min = 0.0f;
    s.max = 100.0f;
    s.step = 0.0f;
    s.count = 5;
    c.input_sweep["distance"] = s;

    const auto data = synthesizeDataset(c, library);
    EXPECT_EQ(data.size(), 5u);
}

// ---------------------------------------------------------------------------
// Workbench 1.17.0 — N-dimensional Cartesian product
// ---------------------------------------------------------------------------

TEST(ReferenceCaseSweep, TwoAxisCartesianProductHasExpectedPointCount)
{
    // Use `buoyancy` (two inputs: fluidDensity, submergedVolume) to
    // probe the N-dimensional sweep machinery. 5 × 7 = 35 combinations
    // regardless of the physics of the formula — we're testing the
    // Cartesian product generator, not the fit.
    FormulaLibrary library;
    library.registerBuiltinTemplates();

    ReferenceCase c;
    c.formula_name = "buoyancy";

    auto* f = library.findByName("buoyancy");
    ASSERT_NE(f, nullptr);
    for (const auto& [k, v] : f->coefficients)
        c.canonical_coefficients[k] = v;

    InputSweep rho;
    rho.min = 900.0f;
    rho.max = 1050.0f;
    rho.count = 5;
    c.input_sweep["fluidDensity"] = rho;

    InputSweep vol;
    vol.min = 0.1f;
    vol.max = 1.3f;
    vol.count = 7;
    c.input_sweep["submergedVolume"] = vol;

    const auto data = synthesizeDataset(c, library);
    EXPECT_EQ(data.size(), 35u);
}

// ---------------------------------------------------------------------------
// Workbench 1.17.0 — max_abs_error_max enforcement
// ---------------------------------------------------------------------------

TEST(ReferenceCaseExec, MaxAbsErrorBoundPassesWhenFitIsExact)
{
    // Synthesize from canonical coefficients with no noise → max-abs
    // error is machine-epsilon small; a tight bound (1e-3) should
    // still pass.
    FormulaLibrary library;
    library.registerBuiltinTemplates();

    ReferenceCase c;
    c.formula_name = "exponential_fog";
    c.canonical_coefficients["density"] = 0.01f;
    InputSweep s;
    s.min = 0.0f;
    s.max = 500.0f;
    s.count = 25;
    c.input_sweep["distance"] = s;
    c.r_squared_min     = 0.999f;
    c.rmse_max          = 1e-3f;
    c.max_abs_error_max = 1e-3f;
    c.must_converge     = true;
    auto r = executeReferenceCase(c, library);
    EXPECT_TRUE(r.passed) << (r.failures.empty() ? "" : r.failures.front());
    EXPECT_LT(r.fit.maxError, 1e-3f);
}

TEST(ReferenceCaseLoad, MaxAbsErrorMaxFieldParsesFromJson)
{
    // JSON round-trip: the loader must populate max_abs_error_max
    // when `expected.max_abs_error_max` is present and leave it at
    // +infinity otherwise. Without a JSON-level test a future
    // refactor could silently drop the parse and every reference
    // case would stop enforcing the new bound.
    const auto path = std::filesystem::temp_directory_path()
                    / "max_abs_error_case.json";
    std::ofstream(path) << R"({
        "formula_name": "exponential_fog",
        "canonical_coefficients": { "density": 0.01 },
        "input_sweep": {
            "distance": { "min": 0.0, "max": 100.0, "count": 10 }
        },
        "expected": {
            "r_squared_min":     0.999,
            "rmse_max":          0.005,
            "max_abs_error_max": 0.02,
            "must_converge":     true
        }
    })";

    std::string err;
    const auto c = loadReferenceCase(path.string(), err);
    ASSERT_TRUE(c.has_value()) << err;
    EXPECT_FLOAT_EQ(c->max_abs_error_max, 0.02f);
    EXPECT_FLOAT_EQ(c->r_squared_min,     0.999f);
    EXPECT_FLOAT_EQ(c->rmse_max,          0.005f);
    EXPECT_TRUE(c->must_converge);
    std::filesystem::remove(path);
}

TEST(ReferenceCaseLoad, MaxAbsErrorMaxDefaultsToInfinity)
{
    // When the field is absent, executeReferenceCase must never
    // reject on max-abs grounds (backwards compat with every
    // existing reference case).
    const auto path = std::filesystem::temp_directory_path()
                    / "no_max_abs.json";
    std::ofstream(path) << R"({
        "formula_name": "exponential_fog",
        "canonical_coefficients": { "density": 0.01 },
        "input_sweep": {
            "distance": { "min": 0.0, "max": 100.0, "count": 10 }
        },
        "expected": { "r_squared_min": 0.999 }
    })";

    std::string err;
    const auto c = loadReferenceCase(path.string(), err);
    ASSERT_TRUE(c.has_value()) << err;
    EXPECT_TRUE(std::isinf(c->max_abs_error_max));
    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// Workbench 1.17.0 — weighted fit via reference case
// ---------------------------------------------------------------------------

TEST(ReferenceCaseExec, WeightsSizeMismatchSurfacesAsFailure)
{
    // Silent truncation / padding of a mismatched weight vector
    // would mask real bugs — document the contract by failing.
    FormulaLibrary library;
    library.registerBuiltinTemplates();

    ReferenceCase c;
    c.formula_name = "exponential_fog";
    c.canonical_coefficients["density"] = 0.01f;
    InputSweep s;
    s.min = 0.0f;
    s.max = 100.0f;
    s.count = 10;
    c.input_sweep["distance"] = s;
    c.weights = {1.0f, 1.0f, 1.0f};  // length 3 vs dataset length 10

    auto r = executeReferenceCase(c, library);
    EXPECT_FALSE(r.passed);
    bool found = false;
    for (const auto& f : r.failures)
        if (f.find("weights vector length") != std::string::npos) { found = true; break; }
    EXPECT_TRUE(found);
}

TEST(ReferenceCaseExec, WeightsMatchingSizePassesThroughToWeightedFitter)
{
    FormulaLibrary library;
    library.registerBuiltinTemplates();

    ReferenceCase c;
    c.formula_name = "exponential_fog";
    c.canonical_coefficients["density"] = 0.01f;
    InputSweep s;
    s.min = 0.0f;
    s.max = 100.0f;
    s.count = 10;
    c.input_sweep["distance"] = s;
    c.weights = std::vector<float>(10, 1.0f);  // uniform, so result ≡ unweighted
    c.r_squared_min = 0.999f;
    c.must_converge = true;

    auto r = executeReferenceCase(c, library);
    EXPECT_TRUE(r.passed) << (r.failures.empty() ? "" : r.failures.front());
}
