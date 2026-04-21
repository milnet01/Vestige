// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "reference_harness.h"

#include "formula/expression_eval.h"
#include "formula/formula.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

namespace Vestige
{

namespace
{

InputSweep parseInputSweep(const nlohmann::json& j)
{
    InputSweep s;
    if (j.contains("values") && j["values"].is_array())
    {
        for (const auto& v : j["values"])
            s.values.push_back(v.get<float>());
    }
    if (j.contains("min"))   s.min   = j["min"].get<float>();
    if (j.contains("max"))   s.max   = j["max"].get<float>();
    if (j.contains("step"))  s.step  = j["step"].get<float>();
    if (j.contains("count")) s.count = j["count"].get<int>();
    return s;
}

CoefficientBound parseCoefficientBound(const nlohmann::json& j)
{
    CoefficientBound b;
    b.expected = j.value("expected", 0.0f);
    if (j.contains("tolerance") && j["tolerance"].is_number())
        b.absolute_tolerance = j["tolerance"].get<float>();
    if (j.contains("relative_tolerance") && j["relative_tolerance"].is_number())
        b.relative_tolerance = j["relative_tolerance"].get<float>();
    return b;
}

} // namespace

// ---------------------------------------------------------------------------
// loadReferenceCase
// ---------------------------------------------------------------------------

std::optional<ReferenceCase>
loadReferenceCase(const std::string& path, std::string& errorOut)
{
    std::ifstream in(path);
    if (!in)
    {
        errorOut = "cannot open: " + path;
        return std::nullopt;
    }

    nlohmann::json j;
    try
    {
        in >> j;
    }
    catch (const std::exception& e)
    {
        errorOut = std::string("parse error: ") + e.what();
        return std::nullopt;
    }

    ReferenceCase c;
    c.formula_name = j.value("formula_name", "");
    if (c.formula_name.empty())
    {
        errorOut = "missing formula_name";
        return std::nullopt;
    }
    c.notes = j.value("notes", "");

    if (j.contains("canonical_coefficients") &&
        j["canonical_coefficients"].is_object())
    {
        for (const auto& it : j["canonical_coefficients"].items())
            c.canonical_coefficients[it.key()] = it.value().get<float>();
    }

    if (j.contains("input_sweep") && j["input_sweep"].is_object())
    {
        for (const auto& it : j["input_sweep"].items())
            c.input_sweep[it.key()] = parseInputSweep(it.value());
    }

    if (j.contains("expected") && j["expected"].is_object())
    {
        const auto& e = j["expected"];
        c.r_squared_min     = e.value("r_squared_min",     0.0f);
        c.rmse_max          = e.value("rmse_max",          std::numeric_limits<float>::infinity());
        c.max_abs_error_max = e.value("max_abs_error_max", std::numeric_limits<float>::infinity());
        c.must_converge     = e.value("must_converge",     false);
        if (e.contains("coefficients") && e["coefficients"].is_object())
        {
            for (const auto& it : e["coefficients"].items())
                c.coefficient_bounds[it.key()] = parseCoefficientBound(it.value());
        }
    }

    if (j.contains("weights") && j["weights"].is_array())
    {
        for (const auto& v : j["weights"])
            c.weights.push_back(v.get<float>());
    }
    return c;
}

// ---------------------------------------------------------------------------
// synthesizeDataset
// ---------------------------------------------------------------------------

namespace
{

// Recursively walk the sweep maps to generate a cartesian product of
// input points. Each full combination becomes one DataPoint after
// evaluating the formula at the canonical coefficients.
void sweepRecurse(
    const std::vector<std::pair<std::string, InputSweep>>& sweeps,
    size_t idx,
    std::map<std::string, float>& current,
    std::vector<std::map<std::string, float>>& out)
{
    if (idx >= sweeps.size())
    {
        out.push_back(current);
        return;
    }
    const auto& [name, sweep] = sweeps[idx];
    if (!sweep.values.empty())
    {
        // Form 1: explicit value list.
        for (float v : sweep.values)
        {
            current[name] = v;
            sweepRecurse(sweeps, idx + 1, current, out);
        }
    }
    else if (sweep.step > 0.0f && sweep.max > sweep.min)
    {
        // Form 2: step-based sweep across [min, max]. Both endpoints
        // included; uses integer step count to avoid float drift so
        // the final sample lands exactly on `max` when the range is
        // an integer multiple of `step`.
        const double span = static_cast<double>(sweep.max)
                          - static_cast<double>(sweep.min);
        const int n = static_cast<int>(std::floor(span
                       / static_cast<double>(sweep.step))) + 1;
        for (int i = 0; i < n; ++i)
        {
            current[name] = static_cast<float>(
                static_cast<double>(sweep.min) + static_cast<double>(sweep.step) * i);
            sweepRecurse(sweeps, idx + 1, current, out);
        }
        // Include max explicitly if the last generated point fell
        // short (e.g. range 0.1 step 0.03 → samples at 0.0, 0.03, 0.06, 0.09
        // and we want to also hit 0.1 as the tail sample).
        const float lastSampled = static_cast<float>(
            static_cast<double>(sweep.min) + static_cast<double>(sweep.step) * (n - 1));
        if (lastSampled < sweep.max - 1e-6f * std::max(1.0f, std::fabs(sweep.max)))
        {
            current[name] = sweep.max;
            sweepRecurse(sweeps, idx + 1, current, out);
        }
    }
    else if (sweep.count >= 2 && sweep.max > sweep.min)
    {
        // Form 3: count-based equally-spaced sweep across [min, max].
        const float step = (sweep.max - sweep.min)
                           / static_cast<float>(sweep.count - 1);
        for (int i = 0; i < sweep.count; ++i)
        {
            current[name] = sweep.min + step * static_cast<float>(i);
            sweepRecurse(sweeps, idx + 1, current, out);
        }
    }
    else
    {
        // Malformed sweep — produce the default value so the case
        // still runs (and fails loudly via RMSE/R²).
        current[name] = sweep.min;
        sweepRecurse(sweeps, idx + 1, current, out);
    }
}

} // namespace

std::vector<DataPoint>
synthesizeDataset(const ReferenceCase& c, const FormulaLibrary& library)
{
    std::vector<DataPoint> out;
    const FormulaDefinition* formula = library.findByName(c.formula_name);
    if (!formula) return out;

    const ExprNode* expr = formula->getExpression(QualityTier::FULL);
    if (!expr) return out;

    // Stable iteration order for sweep unrolling.
    std::vector<std::pair<std::string, InputSweep>> sweeps(
        c.input_sweep.begin(), c.input_sweep.end());

    std::vector<std::map<std::string, float>> combos;
    std::map<std::string, float> current;
    sweepRecurse(sweeps, 0, current, combos);

    ExpressionEvaluator eval;
    for (const auto& combo : combos)
    {
        // Inputs + canonical coefficients in the same map — the
        // evaluator treats variables and coefficients identically.
        ExpressionEvaluator::VariableMap vars;
        for (const auto& [k, v] : combo) vars[k] = v;
        for (const auto& [k, v] : c.canonical_coefficients) vars[k] = v;

        DataPoint dp;
        // DataPoint::variables is std::unordered_map; `combo` is
        // std::map. Copy key-by-key rather than relying on an
        // implicit conversion.
        for (const auto& [k, v] : combo) dp.variables[k] = v;
        dp.observed = eval.evaluate(*expr, vars);
        out.push_back(std::move(dp));
    }
    return out;
}

// ---------------------------------------------------------------------------
// executeReferenceCase
// ---------------------------------------------------------------------------

ReferenceResult
executeReferenceCase(const ReferenceCase& c, const FormulaLibrary& library)
{
    ReferenceResult r;
    r.formula_name = c.formula_name;

    const FormulaDefinition* formula = library.findByName(c.formula_name);
    if (!formula)
    {
        r.failures.push_back("formula not found in library: " + c.formula_name);
        return r;
    }

    const auto data = synthesizeDataset(c, library);
    r.n_points = static_cast<int>(data.size());
    if (data.empty())
    {
        r.failures.push_back("synthesized dataset is empty — "
                             "check input_sweep spec");
        return r;
    }

    // If explicit weights were declared but don't line up with the
    // synthesized dataset, it's a spec bug — surface the mismatch as
    // a failure rather than silently dropping the weights.
    std::vector<float> weights;
    if (!c.weights.empty())
    {
        if (c.weights.size() != data.size())
        {
            std::ostringstream oss;
            oss << "weights vector length " << c.weights.size()
                << " does not match synthesized dataset length "
                << data.size();
            r.failures.push_back(oss.str());
            return r;
        }
        weights = c.weights;
    }

    // Start the fit from the formula library's default coefficients
    // (NOT from the canonical ones — that would be tautological).
    // The fitter's job is to recover the canonical values from
    // cold initial guesses; starting from the answer would pass
    // even if LM were completely broken.
    const auto fit = weights.empty()
        ? CurveFitter::fit(*formula, data, formula->coefficients,
                           QualityTier::FULL, {})
        : CurveFitter::fitWeighted(*formula, data, weights,
                                   formula->coefficients,
                                   QualityTier::FULL, {});
    r.fit = fit;

    if (c.must_converge && !fit.converged)
    {
        r.failures.push_back("did not converge: " + fit.statusMessage);
    }

    if (fit.rSquared < c.r_squared_min)
    {
        std::ostringstream oss;
        oss << "r_squared " << fit.rSquared << " < "
            << c.r_squared_min << " (minimum)";
        r.failures.push_back(oss.str());
    }

    if (fit.rmse > c.rmse_max)
    {
        std::ostringstream oss;
        oss << "rmse " << fit.rmse << " > " << c.rmse_max << " (maximum)";
        r.failures.push_back(oss.str());
    }

    if (fit.maxError > c.max_abs_error_max)
    {
        std::ostringstream oss;
        oss << "max_abs_error " << fit.maxError << " > "
            << c.max_abs_error_max << " (maximum)";
        r.failures.push_back(oss.str());
    }

    for (const auto& [name, bound] : c.coefficient_bounds)
    {
        auto it = fit.coefficients.find(name);
        if (it == fit.coefficients.end())
        {
            r.failures.push_back("fit missing coefficient: " + name);
            continue;
        }
        const float actual = it->second;
        const float diff = std::abs(actual - bound.expected);
        bool ok = true;
        if (bound.absolute_tolerance.has_value())
            ok = ok && diff <= *bound.absolute_tolerance;
        if (bound.relative_tolerance.has_value())
        {
            const float rel = bound.expected != 0.0f
                ? diff / std::abs(bound.expected)
                : diff;
            ok = ok && rel <= *bound.relative_tolerance;
        }
        if (!ok)
        {
            std::ostringstream oss;
            oss << "coefficient " << name
                << " recovered as " << actual
                << " (expected " << bound.expected;
            if (bound.absolute_tolerance.has_value())
                oss << ", abs_tol " << *bound.absolute_tolerance;
            if (bound.relative_tolerance.has_value())
                oss << ", rel_tol " << *bound.relative_tolerance;
            oss << ")";
            r.failures.push_back(oss.str());
        }
    }

    r.passed = r.failures.empty();
    return r;
}

} // namespace Vestige
