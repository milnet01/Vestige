// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file reference_harness.h
/// @brief Reference-case regression harness — §3.4 of the self-learning design.
///
/// A reference case runs in one of two modes, decided by the spec:
///
///   1. **Fit-regression** (the default). The JSON names a library
///      formula, a canonical coefficient set, an input sweep, and a
///      set of output bounds. The harness synthesizes a dataset at
///      runtime from the canonical coefficients + sweep, runs the
///      Levenberg-Marquardt fitter against it, and asserts the fit's
///      R² / RMSE / recovered coefficients fall inside the expected
///      envelopes. This covers the *fittable* formulas — those that
///      carry at least one coefficient for the fitter to recover.
///
///   2. **Evaluation-regression** (``evaluation_points`` present). Five
///      builtins (``wind_deformation``, ``caustic_depth_fade``,
///      ``water_absorption``, ``ease_in_sine``, ``fast_neg_exp``) have
///      *zero* coefficients, so ``CurveFitter::fit`` early-returns "No
///      coefficients to fit" and the fit path can't cover them. For
///      these, the spec instead commits a handful of input → expected
///      output golden points; the harness evaluates the formula's
///      FULL-tier expression at each point and asserts the result is
///      within tolerance of the committed value. The committed values
///      are derived from each formula's mathematical definition, so a
///      future edit that breaks the expression (wrong exponent, dropped
///      term, swapped literal) is caught — it is *not* a tautological
///      self-comparison. When ``evaluation_points`` is present it wins;
///      the fit path (input_sweep / canonical_coefficients) is skipped.
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
/// Three generation forms, checked in priority order:
///   1. ``values`` — explicit list, used as-is.
///   2. ``step``  — closed-interval ``[min, max]`` with a specific
///                  step size (inclusive of both endpoints; last step
///                  may be shortened to land exactly on ``max``).
///   3. ``count`` — closed-interval ``[min, max]`` divided into
///                  ``count`` equally-spaced points.
/// Multi-axis sweeps (N keys under ``input_sweep``) produce a
/// Cartesian product across all axes.
struct InputSweep
{
    std::vector<float> values;
    float min   = 0.0f;
    float max   = 0.0f;
    float step  = 0.0f;  ///< Step size for step-based generation; 0 = use `count`.
    int   count = 0;
};

/// @brief One evaluation-regression golden point.
///
/// Used by zero-coefficient formulas (see mode 2 in the file header).
/// The formula's FULL-tier expression is evaluated with ``inputs``
/// substituted and the result asserted within ``tolerance`` (absolute)
/// of ``expected_output``. ``expected_output`` is the formula's
/// mathematically-correct value at ``inputs`` — committed as ground
/// truth, not copied from the current code output.
struct EvaluationPoint
{
    std::map<std::string, float> inputs;
    float expected_output = 0.0f;
    /// Absolute tolerance. Defaults to 1e-4 — tight enough to catch a
    /// broken expression (which shifts the output by O(0.1–1)), loose
    /// enough to absorb float/libm platform variation (~1e-6).
    float tolerance = 1e-4f;
};

/// @brief Full parsed contents of a ``reference_cases/*.json`` file.
struct ReferenceCase
{
    std::string formula_name;
    std::map<std::string, float> canonical_coefficients;
    std::map<std::string, InputSweep> input_sweep;
    /// Evaluation-regression golden points (mode 2). When non-empty,
    /// ``executeReferenceCase`` runs evaluation-regression and skips
    /// the fit path entirely.
    std::vector<EvaluationPoint> evaluation_points;
    float r_squared_min = 0.0f;
    float rmse_max = 0.0f;
    /// Maximum absolute residual across the synthetic dataset. Strictly
    /// tighter than ``rmse_max`` for rendering-formula fits where the
    /// user-visible artefact is the worst-case error, not the mean
    /// (e.g. a tonemap or BRDF approximation with good RMSE but a
    /// visible seam at one region of input). Defaults to ``+infinity``
    /// — the field is optional and backwards-compatible.
    float max_abs_error_max = 0.0f;
    bool must_converge = false;
    std::map<std::string, CoefficientBound> coefficient_bounds;
    /// Optional per-sample weights — parallel to the synthesized
    /// dataset. Empty means uniform (unweighted). Populated from the
    /// ``weights`` array in the JSON; if its length doesn't match the
    /// synthesized dataset it's dropped with a failure.
    std::vector<float> weights;
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
