// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "benchmark.h"

#include "formula/formula.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Information criteria
// ---------------------------------------------------------------------------

InformationCriteria computeAicBic(float rmse, int nPoints, int paramCount)
{
    InformationCriteria ic;
    // Same degeneracy guard as workbench.cpp's in-line AIC calc:
    // n must exceed k+1 (otherwise adjusted R² is also undefined),
    // and SSE must be strictly positive so ln(SSE/n) is finite.
    if (nPoints <= paramCount + 1)
    {
        ic.degenerate = true;
        return ic;
    }
    const double sse = static_cast<double>(rmse) * rmse * nPoints;
    if (sse <= 0.0)
    {
        ic.degenerate = true;
        return ic;
    }
    ic.aic = static_cast<float>(
        nPoints * std::log(sse / nPoints) + 2.0 * paramCount);
    ic.bic = static_cast<float>(
        nPoints * std::log(sse / nPoints)
        + paramCount * std::log(static_cast<double>(nPoints)));
    return ic;
}

// ---------------------------------------------------------------------------
// CSV loader — factored from Workbench::importCsv (same RFC 4180 rules)
// ---------------------------------------------------------------------------

namespace
{

std::vector<std::string> splitCsvLine(const std::string& line)
{
    std::vector<std::string> out;
    std::string cell;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i)
    {
        char c = line[i];
        if (inQuotes)
        {
            if (c == '"')
            {
                if (i + 1 < line.size() && line[i + 1] == '"')
                {
                    cell.push_back('"');
                    ++i;
                }
                else
                {
                    inQuotes = false;
                }
            }
            else
            {
                cell.push_back(c);
            }
        }
        else
        {
            if (c == '"' && cell.empty())
                inQuotes = true;
            else if (c == ',')
            {
                out.push_back(std::move(cell));
                cell.clear();
            }
            else
                cell.push_back(c);
        }
    }
    out.push_back(std::move(cell));
    return out;
}

void trim(std::string& s)
{
    auto firstNonWs = s.find_first_not_of(" \t\r\n");
    if (firstNonWs == std::string::npos) { s.clear(); return; }
    s.erase(0, firstNonWs);
    auto lastNonWs = s.find_last_not_of(" \t\r\n");
    if (lastNonWs != std::string::npos)
        s.erase(lastNonWs + 1);
}

} // namespace

std::optional<std::vector<DataPoint>>
loadCsvDataset(const std::string& path, std::string& errorOut)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        errorOut = "Cannot open CSV file: " + path;
        return std::nullopt;
    }

    std::string headerLine;
    if (!std::getline(file, headerLine))
    {
        errorOut = "CSV file is empty.";
        return std::nullopt;
    }

    std::vector<std::string> headers = splitCsvLine(headerLine);
    for (auto& h : headers) trim(h);

    if (headers.size() < 2)
    {
        errorOut = "CSV must have at least 2 columns "
                   "(at least one variable + observed).";
        return std::nullopt;
    }

    std::vector<DataPoint> out;
    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty()) continue;

        std::vector<std::string> cells = splitCsvLine(line);
        std::vector<float> values;
        values.reserve(cells.size());
        bool row_ok = true;
        for (auto& cell : cells)
        {
            trim(cell);
            try
            {
                values.push_back(std::stof(cell));
            }
            catch (const std::exception&)
            {
                row_ok = false;
                break;
            }
        }
        if (!row_ok || values.size() < headers.size())
            continue;

        DataPoint dp;
        for (size_t i = 0; i + 1 < headers.size(); ++i)
            dp.variables[headers[i]] = values[i];
        dp.observed = values.back();
        out.push_back(std::move(dp));
    }

    if (out.empty())
    {
        errorOut = "CSV produced no usable rows (all rows skipped).";
        return std::nullopt;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Benchmark runner
// ---------------------------------------------------------------------------

namespace
{

// Does `formula` have a float input that `data` also supplies? A
// library formula requiring variable `t` can't be fit against a
// dataset whose only input is `x`. Returning false is a clean skip
// rather than a fit failure.
bool dataCoversFormulaInputs(const FormulaDefinition& formula,
                             const std::vector<DataPoint>& data)
{
    if (data.empty()) return false;
    const auto& sample = data.front().variables;
    bool any_float_input = false;
    for (const auto& inp : formula.inputs)
    {
        if (inp.type != FormulaValueType::FLOAT) continue;
        any_float_input = true;
        if (sample.find(inp.name) == sample.end())
            return false;
    }
    // Formulas with zero float inputs can't be fit against a dataset
    // in any meaningful way — they're constants or structural.
    return any_float_input;
}

BenchmarkEntry skipEntry(const std::string& name, const std::string& reason)
{
    BenchmarkEntry e;
    e.formula_name = name;
    e.status = reason;
    return e;
}

} // namespace

std::vector<BenchmarkEntry>
runBenchmark(const FormulaLibrary& library,
             const std::vector<DataPoint>& data)
{
    std::vector<BenchmarkEntry> out;
    const auto all = library.getAll();
    for (const auto* formula : all)
    {
        if (!formula) continue;
        if (formula->coefficients.empty())
        {
            out.push_back(skipEntry(formula->name,
                                    "no coefficients to fit"));
            continue;
        }
        if (!formula->getExpression(QualityTier::FULL))
        {
            out.push_back(skipEntry(formula->name,
                                    "no FULL-tier expression"));
            continue;
        }
        if (!dataCoversFormulaInputs(*formula, data))
        {
            out.push_back(skipEntry(formula->name,
                                    "dataset lacks required input variables"));
            continue;
        }

        FitResult fr = CurveFitter::fit(
            *formula, data, formula->coefficients,
            QualityTier::FULL, {});

        BenchmarkEntry e;
        e.formula_name = formula->name;
        e.r_squared    = fr.rSquared;
        e.rmse         = fr.rmse;
        e.iterations   = fr.iterations;
        e.converged    = fr.converged;
        e.param_count  = static_cast<int>(fr.coefficients.size());
        const auto ic = computeAicBic(fr.rmse,
                                      static_cast<int>(data.size()),
                                      e.param_count);
        if (ic.degenerate)
        {
            e.status = "AIC/BIC degenerate (n ≤ k+1 or SSE ≤ 0)";
            e.aic = 0.0f;
            e.bic = 0.0f;
        }
        else
        {
            e.aic = ic.aic;
            e.bic = ic.bic;
            e.status = fr.converged ? "converged" : fr.statusMessage;
        }
        out.push_back(std::move(e));
    }

    // Sort entries that could be fit by AIC ascending. Skipped
    // entries (status != converged / not-converged) sort to the
    // bottom; their aic is 0 so a raw sort would mis-rank them.
    auto was_fit = [](const BenchmarkEntry& e)
    {
        // "Fittable" here = the LM fitter actually produced numbers,
        // whether or not it reached the convergence tolerance. Skip
        // entries have empty rmse/r_squared and a status explaining
        // the skip reason — they sort to the bottom.
        return e.status == "converged"
            || e.rmse > 0.0f
            || e.r_squared > 0.0f;
    };
    std::sort(out.begin(), out.end(),
              [&was_fit](const BenchmarkEntry& a, const BenchmarkEntry& b)
    {
        const bool a_fit = was_fit(a);
        const bool b_fit = was_fit(b);
        if (a_fit != b_fit) return a_fit;            // fittable first
        if (!a_fit) return a.formula_name < b.formula_name;
        return a.aic < b.aic;                        // lower AIC better
    });

    // Compute delta_aic relative to the best (first) fittable entry.
    for (auto& e : out)
    {
        if (e.status == "converged" || e.rmse > 0.0f || e.r_squared > 0.0f)
        {
            // Find the first fittable entry and use its AIC as the baseline.
            for (const auto& ref : out)
            {
                if (ref.status == "converged" || ref.rmse > 0.0f
                    || ref.r_squared > 0.0f)
                {
                    e.delta_aic = e.aic - ref.aic;
                    break;
                }
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Markdown renderer
// ---------------------------------------------------------------------------

namespace
{

// Helper: is this entry one that got actually fit (vs. skipped)?
bool wasFit(const BenchmarkEntry& e)
{
    return e.rmse > 0.0f || e.r_squared > 0.0f || e.status == "converged";
}

} // namespace

std::string renderBenchmarkMarkdown(const std::vector<BenchmarkEntry>& entries)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "# Formula Workbench self-benchmark\n\n";

    std::vector<const BenchmarkEntry*> fitted, skipped;
    for (const auto& e : entries)
    {
        (wasFit(e) ? fitted : skipped).push_back(&e);
    }

    out << "**Fitted:** " << fitted.size()
        << "  /  **Skipped:** " << skipped.size() << "\n\n";

    if (!fitted.empty())
    {
        out << "## Leaderboard (sorted by AIC ascending)\n\n";
        out << "| Rank | Formula | R² | RMSE | AIC | BIC | ΔAIC | Iter | Converged |\n";
        out << "|-----:|---------|----:|------:|------:|------:|------:|-----:|:---------:|\n";
        int rank = 1;
        out << std::fixed;
        for (const auto* e : fitted)
        {
            out << "| " << rank++
                << " | `" << e->formula_name << "`"
                << " | " << std::setprecision(4) << e->r_squared
                << " | " << std::setprecision(4) << e->rmse
                << " | " << std::setprecision(1) << e->aic
                << " | " << std::setprecision(1) << e->bic
                << " | " << std::setprecision(1) << e->delta_aic
                << " | " << e->iterations
                << " | " << (e->converged ? "yes" : "no")
                << " |\n";
        }
        out << "\n";
        out << "Rule of thumb: **ΔAIC < 2** = indistinguishable from "
               "best; **ΔAIC > 10** = decisively worse. Convergence=no "
               "means the fitter hit max iterations before reaching "
               "the tolerance — the R²/RMSE numbers are still the best "
               "model found, but consider increasing maxIterations or "
               "revisiting initial guesses.\n\n";
    }

    if (!skipped.empty())
    {
        out << "## Skipped formulas\n\n";
        for (const auto* e : skipped)
            out << "- `" << e->formula_name << "` — " << e->status << "\n";
        out << "\n";
    }

    if (fitted.empty() && skipped.empty())
        out << "*No formulas in the library — nothing to benchmark.*\n";

    return out.str();
}

// ---------------------------------------------------------------------------
// CLI entry — parsed by main.cpp before GLFW init
// ---------------------------------------------------------------------------

std::optional<int> runBenchmarkCli(int argc, char** argv)
{
    std::string csvPath;
    std::string outputPath;
    bool requested = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--self-benchmark" && i + 1 < argc)
        {
            csvPath = argv[++i];
            requested = true;
        }
        else if (a == "--output" && i + 1 < argc)
        {
            outputPath = argv[++i];
        }
        else if (a == "--help" || a == "-h")
        {
            std::fprintf(stderr,
                "Usage: formula_workbench [--self-benchmark <csv>] "
                "[--output <md>]\n"
                "  --self-benchmark <csv>   Fit every library formula to "
                "<csv> and emit a markdown leaderboard.\n"
                "  --output <md>            Write report to <md> "
                "(default: stdout).\n"
                "  No flags                 Start the GUI as usual.\n");
            return 0;
        }
    }

    if (!requested)
        return std::nullopt;

    std::string err;
    auto data = loadCsvDataset(csvPath, err);
    if (!data)
    {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }

    FormulaLibrary library;
    library.registerBuiltinTemplates();
    const auto entries = runBenchmark(library, *data);
    const std::string md = renderBenchmarkMarkdown(entries);

    if (outputPath.empty())
    {
        std::fwrite(md.data(), 1, md.size(), stdout);
    }
    else
    {
        std::ofstream f(outputPath);
        if (!f)
        {
            std::fprintf(stderr, "error: cannot write to %s\n",
                         outputPath.c_str());
            return 1;
        }
        f << md;
    }
    return 0;
}

} // namespace Vestige
