// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "benchmark.h"

#include "fit_history.h"
#include "formula/formula.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

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

    // W8 — give each formula the benefit of its last exported fit
    // as an initial guess. Matches what the GUI does via §3.2
    // (lastExportedCoeffsFor on selectFormula) so the CLI
    // leaderboard isn't systematically penalising formulas the
    // user has previously fit well. History lookup is best-effort;
    // missing / corrupt / absent file silently falls back to
    // library defaults.
    FitHistory history(".fit_history.json");
    const bool have_history = history.load();

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

        // Seed coefficients: start from library defaults, overwrite
        // with any matching entries from the most recent exported
        // fit. Names that don't match (library evolved) keep the
        // default. See §3.2 for the equivalent GUI path.
        std::map<std::string, float> initial = formula->coefficients;
        if (have_history)
        {
            const auto prior = history.lastExportedCoeffsFor(formula->name);
            for (auto& [k, v] : initial)
            {
                auto it = prior.find(k);
                if (it != prior.end()) v = it->second;
            }
        }

        FitResult fr = CurveFitter::fit(
            *formula, data, initial, QualityTier::FULL, {});

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

// ---------------------------------------------------------------------------
// §3.5 / §3.6 — Python driver shell-outs and library JSON dump.
//
// Both CLI tiers live in Python (PySR and the Anthropic SDK are both
// Python-first), so the C++ side's job is just to locate the driver,
// invoke it with the right argv + stdin, and forward the exit code.
// This keeps PySR/Anthropic as optional dependencies that don't
// touch the default Workbench build.
// ---------------------------------------------------------------------------

namespace
{

// Locate the scripts directory relative to the executable. Tries
// three candidate layouts in order:
//   1. Alongside the running binary (install layout).
//   2. Source tree (development layout: tools/formula_workbench/scripts/).
//   3. Current working directory.
// First hit wins.
std::string findDriverScript(const char* name)
{
    namespace fs = std::filesystem;
    const char* self_exe_candidates[] = {
        "/proc/self/exe",
    };
    fs::path exe;
    for (const char* p : self_exe_candidates)
    {
        std::error_code ec;
        exe = fs::read_symlink(p, ec);
        if (!ec && !exe.empty()) break;
    }

    std::vector<fs::path> search;
    if (!exe.empty())
    {
        // bin/ → parent dir → tools/formula_workbench/scripts/
        search.push_back(exe.parent_path() / "scripts" / name);
        search.push_back(exe.parent_path().parent_path()
                         / "tools" / "formula_workbench" / "scripts" / name);
    }
    search.push_back(fs::path("tools/formula_workbench/scripts") / name);
    search.push_back(fs::path("scripts") / name);

    for (const auto& p : search)
    {
        std::error_code ec;
        if (fs::exists(p, ec)) return p.string();
    }
    return {};
}

// Fork + exec a driver script with optional stdin pipe. Returns the
// child's exit code, or -1 on spawn failure. The parent's stdout/
// stderr are inherited, so the driver's output lands on our TTY
// directly — no buffering.
int runDriver(const std::string& script,
              const std::vector<std::string>& argv,
              const std::string& stdinContents = {})
{
    int pipefd[2] = {-1, -1};
    if (!stdinContents.empty())
    {
        if (pipe(pipefd) != 0)
        {
            std::fprintf(stderr, "error: pipe() failed\n");
            return -1;
        }
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        std::fprintf(stderr, "error: fork() failed\n");
        if (pipefd[0] >= 0) { close(pipefd[0]); close(pipefd[1]); }
        return -1;
    }

    if (pid == 0)
    {
        // Child
        if (pipefd[0] >= 0)
        {
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[0]);
            close(pipefd[1]);
        }
        std::vector<char*> c_argv;
        c_argv.push_back(const_cast<char*>("python3"));
        c_argv.push_back(const_cast<char*>(script.c_str()));
        for (const auto& a : argv) c_argv.push_back(const_cast<char*>(a.c_str()));
        c_argv.push_back(nullptr);
        execvp("python3", c_argv.data());
        std::fprintf(stderr, "error: exec python3 failed\n");
        _exit(127);
    }

    // Parent
    if (pipefd[0] >= 0)
    {
        close(pipefd[0]);
        ssize_t total = 0;
        const char* buf = stdinContents.data();
        const ssize_t need = static_cast<ssize_t>(stdinContents.size());
        while (total < need)
        {
            ssize_t n = write(pipefd[1], buf + total, need - total);
            if (n <= 0) break;
            total += n;
        }
        close(pipefd[1]);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

nlohmann::json formulaMetaToJson(const FormulaDefinition& f)
{
    nlohmann::json j;
    j["name"]        = f.name;
    j["category"]    = f.category;
    j["description"] = f.description;
    nlohmann::json ins = nlohmann::json::array();
    for (const auto& inp : f.inputs)
    {
        nlohmann::json i;
        i["name"]  = inp.name;
        i["unit"]  = inp.unit;
        ins.push_back(i);
    }
    j["inputs"] = ins;
    nlohmann::json coeffs = nlohmann::json::object();
    for (const auto& [k, v] : f.coefficients) coeffs[k] = v;
    j["default_coefficients"] = coeffs;
    j["source"] = f.source;
    return j;
}

} // namespace

void dumpLibraryJson(const FormulaLibrary& library)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto* f : library.getAll())
    {
        if (f) arr.push_back(formulaMetaToJson(*f));
    }
    nlohmann::json root;
    root["formulas"] = arr;
    std::fputs(root.dump(2).c_str(), stdout);
    std::fputc('\n', stdout);
}

std::optional<int> runDumpLibraryCli(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--dump-library")
        {
            FormulaLibrary library;
            library.registerBuiltinTemplates();
            dumpLibraryJson(library);
            return 0;
        }
    }
    return std::nullopt;
}

std::optional<int> runSymbolicRegressionCli(int argc, char** argv)
{
    std::string csv;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--symbolic-regression" && i + 1 < argc)
            csv = argv[++i];
    }
    if (csv.empty()) return std::nullopt;

    const std::string script = findDriverScript("pysr_driver.py");
    if (script.empty())
    {
        std::fprintf(stderr,
            "error: pysr_driver.py not found. Expected at "
            "tools/formula_workbench/scripts/pysr_driver.py relative "
            "to the executable or source tree.\n");
        return 1;
    }
    return runDriver(script, {csv});
}

std::optional<int> runSuggestFormulasCli(int argc, char** argv)
{
    std::string csv;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--suggest-formulas" && i + 1 < argc)
            csv = argv[++i];
    }
    if (csv.empty()) return std::nullopt;

    const std::string script = findDriverScript("llm_rank.py");
    if (script.empty())
    {
        std::fprintf(stderr,
            "error: llm_rank.py not found. Expected at "
            "tools/formula_workbench/scripts/llm_rank.py relative to "
            "the executable or source tree.\n");
        return 1;
    }

    // Dump library to JSON and pipe it in via stdin so the driver
    // can shape the LLM prompt around the current set of formulas.
    FormulaLibrary library;
    library.registerBuiltinTemplates();
    nlohmann::json arr = nlohmann::json::array();
    for (const auto* f : library.getAll())
    {
        if (f) arr.push_back(formulaMetaToJson(*f));
    }
    nlohmann::json root;
    root["formulas"] = arr;
    const std::string libJson = root.dump(2);
    return runDriver(script, {csv}, libJson);
}

// ---------------------------------------------------------------------------
// GUI-side helpers (§3.6 GUI). Public wrappers around the anonymous-
// namespace implementations so workbench.cpp doesn't need to
// duplicate the fork+exec logic.
// ---------------------------------------------------------------------------

std::string findDriverScriptPath(const char* name)
{
    return findDriverScript(name);
}

std::string libraryToJsonString(const FormulaLibrary& library)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto* f : library.getAll())
    {
        if (f) arr.push_back(formulaMetaToJson(*f));
    }
    nlohmann::json root;
    root["formulas"] = arr;
    return root.dump(2);
}

DriverProcess spawnDriverProcess(
    const std::string& script,
    const std::vector<std::string>& argv,
    bool wantStdin)
{
    // Two pipes: one to feed stdin to the child, one to capture its
    // stdout. Stderr is deliberately left inheriting the parent — we
    // want python import errors visible in the launching terminal
    // so users can diagnose missing packages without having to
    // squint at a string field in the GUI.
    DriverProcess proc;
    int in_pipe[2]  = {-1, -1};
    int out_pipe[2] = {-1, -1};
    if (wantStdin && pipe(in_pipe) != 0)
    {
        proc.error = "pipe() for stdin failed";
        return proc;
    }
    if (pipe(out_pipe) != 0)
    {
        if (wantStdin) { close(in_pipe[0]); close(in_pipe[1]); }
        proc.error = "pipe() for stdout failed";
        return proc;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        if (wantStdin) { close(in_pipe[0]); close(in_pipe[1]); }
        close(out_pipe[0]); close(out_pipe[1]);
        proc.error = "fork() failed";
        return proc;
    }

    if (pid == 0)
    {
        if (wantStdin)
        {
            dup2(in_pipe[0], STDIN_FILENO);
            close(in_pipe[0]);
            close(in_pipe[1]);
        }
        dup2(out_pipe[1], STDOUT_FILENO);
        close(out_pipe[0]);
        close(out_pipe[1]);

        std::vector<char*> c_argv;
        c_argv.push_back(const_cast<char*>("python3"));
        c_argv.push_back(const_cast<char*>(script.c_str()));
        for (const auto& a : argv) c_argv.push_back(const_cast<char*>(a.c_str()));
        c_argv.push_back(nullptr);
        execvp("python3", c_argv.data());
        _exit(127);
    }

    // Parent: close the child's ends of each pipe, hand the caller
    // theirs. Caller drives stdin write-and-close + stdout read-to-EOF.
    if (wantStdin)
    {
        close(in_pipe[0]);
        proc.stdin_fd = in_pipe[1];
    }
    close(out_pipe[1]);
    proc.stdout_fd = out_pipe[0];
    proc.pid       = static_cast<int>(pid);
    return proc;
}

} // namespace Vestige
