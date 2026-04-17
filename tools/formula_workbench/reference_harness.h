// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file reference_harness.h
/// @brief Reference-case regression harness — §3.4 of the self-learning design.
///
/// Each JSON spec under ``tools/formula_workbench/reference_cases/``
/// names a library formula, a canonical coefficient set, an input
/// sweep, and a set of output bounds. The harness synthesizes a
/// dataset at runtime from the canonical coefficients + sweep,
/// runs the Levenberg-Marquardt fitter against it, and asserts
/// the fit's R² / RMSE / recovered coefficients fall inside the
/// expected envelopes.
///
/// This is the audit-tool's ``tests/audit_fixtures/<rule-id>/``
/// mechanism ported to numerical code. The fixture is the spec;
/// the regression is the fitter; the assertion is the envelope.
///
/// Why synthesize instead of commit CSVs? Two reasons:
///   1. Any floating-point semantics change (compiler, libm) affects
///      the synthesized data AND the expected output in lockstep —
///      fewer false alarms than CSV drift would cause.
///   2. The spec reads like a physics note, not a data dump:
///      "here's the formula, here are its canonical constants, fit
///      should recover within X%".
///
/// The trade-off: we don't exercise the CSV importer. That's OK —
/// the CSV importer is tested separately in ``test_benchmark.cpp``.
/// This harness tests the *fitter* on known-good ground truth.
#pragma once

#include "formula/curve_fitter.h"
#include "formula/formula_library.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief One coefficient's expected value + tolerance.
///
/// Absolute tolerance (``tolerance``) or relative
/// (``relative_tolerance``, expressed as a fraction of ``expected``).
/// Caller picks the appropriate form per spec; specifying neither
/// makes the coefficient pass automatically (useful when only
/// fit-quality matters).
struct CoefficientBound
{
    float expected = 0.0f;
    std::optional<float> absolute_tolerance;
    std::optional<float> relative_tolerance;
};

/// @brief Sweep specification for one input variable.
///
/// ``values`` takes precedence if non-empty (fixed list). Otherwise
/// a closed-interval sweep ``[min, max]`` is generated with ``count``
/// points (count >= 2 for the interval, count >= 1 for the value list).
struct InputSweep
{
    std::vector<float> values;
    float min = 0.0f;
    float max = 0.0f;
    int count = 0;
};

/// @brief Full parsed contents of a ``reference_cases/*.json`` file.
struct ReferenceCase
{
    std::string formula_name;
    std::map<std::string, float> canonical_coefficients;
    std::map<std::string, InputSweep> input_sweep;
    float r_squared_min = 0.0f;
    float rmse_max = 0.0f;
    bool must_converge = false;
    std::map<std::string, CoefficientBound> coefficient_bounds;
    std::string notes;
};

/// @brief Outcome of running one reference case through the harness.
struct ReferenceResult
{
    bool passed = false;
    std::string formula_name;
    std::vector<std::string> failures;   ///< Empty when passed==true.
    FitResult fit;                       ///< Raw fitter output.
    int n_points = 0;                    ///< Size of synthesized dataset.
};

/// @brief Parse a ``reference_cases/*.json`` file.
///
/// Returns ``std::nullopt`` and populates ``errorOut`` on parse or
/// I/O error. On success the returned ``ReferenceCase`` is
/// ready to run through ``executeReferenceCase``.
std::optional<ReferenceCase>
loadReferenceCase(const std::string& path, std::string& errorOut);

/// @brief Generate the synthetic dataset from a case's canonical
/// coefficients + input sweep.
///
/// Exposed so tests can inspect the data shape independently of the
/// fitter path. The formula is evaluated with the canonical coeffs
/// substituted; no numerical noise is added.
std::vector<DataPoint>
synthesizeDataset(const ReferenceCase& c, const FormulaLibrary& library);

/// @brief Run one reference case end-to-end: synthesize → fit → check.
///
/// Never throws; all failures go into ``result.failures`` with a
/// short message. A case that can't locate its formula is a
/// ``failed`` result, not an exception.
ReferenceResult
executeReferenceCase(const ReferenceCase& c, const FormulaLibrary& library);

} // namespace Vestige
