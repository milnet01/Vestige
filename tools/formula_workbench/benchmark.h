// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file benchmark.h
/// @brief Headless batch-fit benchmark — §3.3 of the self-learning design.
///
/// Given a dataset and a formula library, fit every compatible formula
/// to the *same* dataset and rank the results by AIC ascending. Lets
/// the user surface "which library formula is the best model for my
/// data" without clicking through N fits in the GUI.
///
/// Pure non-GUI module — no ImGui, no GLFW. `main.cpp` branches into
/// `runBenchmarkCli` before the GLFW window is created when the
/// ``--self-benchmark`` flag is present, so the executable can serve
/// as both a GUI tool and a headless CLI.
///
/// Statistical model:
///   - Fit quality: R² and RMSE from ``CurveFitter::fit``.
///   - Model selection: AIC and BIC computed from SSE (= rmse² × n)
///     and parameter count (k = # coefficients). Lower is better.
///     Standard formulas:
///        AIC = n · ln(SSE/n) + 2k
///        BIC = n · ln(SSE/n) + k · ln(n)
///   - Ranking: entries sorted by AIC ascending. ``delta_aic``
///     records each entry's gap from the best — gaps < 2 are
///     indistinguishable, > 10 are decisive (standard AIC reading,
///     per Burnham & Anderson 2004).
#pragma once

#include "formula/curve_fitter.h"
#include "formula/formula_library.h"

#include <optional>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief One formula's fit quality summary, ranked among others.
struct BenchmarkEntry
{
    std::string formula_name;
    float r_squared = 0.0f;
    float rmse = 0.0f;
    float aic = 0.0f;
    float bic = 0.0f;
    int iterations = 0;
    int param_count = 0;
    bool converged = false;
    std::string status;   ///< Short explanation, e.g. "no float variable", "fit failed".
    float delta_aic = 0.0f;
};

/// @brief Compute AIC and BIC from a fit result.
///
/// Returns ``{0, 0}`` when the model is degenerate (n ≤ k+1 or SSE ≤ 0)
/// rather than producing NaN/Inf. Pure function; exposed separately so
/// tests can assert the formula independently of the full benchmark path.
struct InformationCriteria
{
    float aic = 0.0f;
    float bic = 0.0f;
    bool degenerate = false;
};
InformationCriteria computeAicBic(float rmse, int nPoints, int paramCount);

/// @brief Parse an RFC-4180 CSV into a vector of DataPoint.
///
/// First row is a header. Last column is the observed value; the
/// rest are input variables. Returns ``std::nullopt`` on read / parse
/// errors, with an error message in *errorOut*. Factored out of the
/// Workbench's ``importCsv`` so benchmark mode doesn't need the GUI.
std::optional<std::vector<DataPoint>>
loadCsvDataset(const std::string& path, std::string& errorOut);

/// @brief Fit every library formula against the given data. Entries
/// are sorted by AIC ascending. Formulas that can't be fit (no float
/// variable, empty expression, LM failed) still appear with ``status``
/// explaining why, so the leaderboard is a complete picture of the
/// attempt, not just the successes.
std::vector<BenchmarkEntry>
runBenchmark(const FormulaLibrary& library,
             const std::vector<DataPoint>& data);

/// @brief Render a markdown leaderboard for ``entries``.
///
/// Shape matches the design doc §3.3 example — Formula, R², RMSE,
/// AIC, BIC, ΔAIC columns, sorted-winners-first. Not-fit entries
/// are grouped in a trailing "Skipped" section with the reason.
std::string renderBenchmarkMarkdown(const std::vector<BenchmarkEntry>& entries);

/// @brief Parse argv for the ``--self-benchmark`` flag and run the
/// CLI path if present. Returns the process exit code if the flag was
/// handled (caller should exit with that code), or ``std::nullopt`` if
/// the GUI should start normally.
///
/// Usage:
///   formula_workbench --self-benchmark data.csv
///   formula_workbench --self-benchmark data.csv --output report.md
std::optional<int> runBenchmarkCli(int argc, char** argv);

/// @brief §3.5 — shell out to ``scripts/pysr_driver.py`` for symbolic
/// regression. Returns the driver's exit code when the flag is handled.
///
/// Usage:
///   formula_workbench --symbolic-regression data.csv
///
/// The driver is optional: when PySR isn't installed the driver
/// prints an install hint on stderr and exits 2, which the CLI
/// forwards verbatim. No PySR dependency on the Workbench build.
std::optional<int> runSymbolicRegressionCli(int argc, char** argv);

/// @brief §3.6 — shell out to ``scripts/llm_rank.py`` for
/// LLM-guided formula ranking. Returns the driver's exit code.
///
/// Usage:
///   formula_workbench --suggest-formulas data.csv
///
/// The C++ side dumps the built-in FormulaLibrary as JSON and pipes
/// it into the driver's stdin; the driver constructs a prompt and
/// calls the Anthropic API. Exits 2 on missing ANTHROPIC_API_KEY or
/// anthropic SDK.
std::optional<int> runSuggestFormulasCli(int argc, char** argv);

/// @brief Emit the built-in FormulaLibrary as JSON to stdout.
/// Used by ``--dump-library`` and by ``--suggest-formulas`` as the
/// metadata piped into the LLM driver.
void dumpLibraryJson(const FormulaLibrary& library);
std::optional<int> runDumpLibraryCli(int argc, char** argv);

} // namespace Vestige
