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
