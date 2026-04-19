// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "fit_history.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Vestige
{

namespace
{

// Deterministic hex-string encoder for a 64-bit value. Avoids locale
// sensitivity and std::hex stream state leakage.
// AUDIT M6: use ``unsigned long long`` (64-bit everywhere) + ``%llx``.
// ``unsigned long`` is only 32-bit on Windows LLP64, so the previous
// format truncated the high 32 bits of any post-2^32 hash there.
std::string toHex64(std::uint64_t v)
{
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
    return std::string(buf);
}

// FNV-1a 64-bit. Standard constants. Collision-resistant enough for
// dataset fingerprinting; we're not signing anything.
std::uint64_t fnv1a64(const std::string& s)
{
    constexpr std::uint64_t OFFSET = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t PRIME  = 0x100000001b3ULL;
    std::uint64_t h = OFFSET;
    for (unsigned char c : s)
    {
        h ^= c;
        h *= PRIME;
    }
    return h;
}

nlohmann::json metaToJson(const FitHistoryMeta& m)
{
    nlohmann::json j;
    j["n_points"] = m.n_points;
    j["variance"] = m.variance;
    nlohmann::json dom = nlohmann::json::object();
    for (const auto& [k, v] : m.domain)
    {
        dom[k] = {v.first, v.second};
    }
    j["domain"] = dom;
    return j;
}

FitHistoryMeta metaFromJson(const nlohmann::json& j)
{
    FitHistoryMeta m;
    m.n_points = j.value("n_points", 0);
    m.variance = j.value("variance", 0.0f);
    if (j.contains("domain") && j["domain"].is_object())
    {
        for (const auto& item : j["domain"].items())
        {
            if (item.value().is_array() && item.value().size() == 2)
            {
                m.domain[item.key()] = {
                    item.value()[0].get<float>(),
                    item.value()[1].get<float>(),
                };
            }
        }
    }
    return m;
}

nlohmann::json entryToJson(const FitHistoryEntry& e)
{
    nlohmann::json j;
    j["timestamp"] = e.timestamp;
    j["formula_name"] = e.formula_name;
    j["data_hash"] = e.data_hash;
    j["data_meta"] = metaToJson(e.data_meta);
    nlohmann::json coeffs = nlohmann::json::object();
    for (const auto& [k, v] : e.coefficients)
    {
        coeffs[k] = v;
    }
    j["coefficients"] = coeffs;
    j["r_squared"] = e.r_squared;
    j["rmse"] = e.rmse;
    j["aic"] = e.aic;
    j["bic"] = e.bic;
    j["iterations"] = e.iterations;
    j["converged"] = e.converged;
    j["user_action"] = e.user_action;
    return j;
}

FitHistoryEntry entryFromJson(const nlohmann::json& j)
{
    FitHistoryEntry e;
    e.timestamp = j.value("timestamp", "");
    e.formula_name = j.value("formula_name", "");
    e.data_hash = j.value("data_hash", "");
    if (j.contains("data_meta"))
        e.data_meta = metaFromJson(j["data_meta"]);
    if (j.contains("coefficients") && j["coefficients"].is_object())
    {
        for (const auto& item : j["coefficients"].items())
        {
            if (item.value().is_number())
                e.coefficients[item.key()] = item.value().get<float>();
        }
    }
    e.r_squared = j.value("r_squared", 0.0f);
    e.rmse = j.value("rmse", 0.0f);
    e.aic = j.value("aic", 0.0f);
    e.bic = j.value("bic", 0.0f);
    e.iterations = j.value("iterations", 0);
    e.converged = j.value("converged", false);
    e.user_action = j.value("user_action", "");
    return e;
}

} // namespace

// ---------------------------------------------------------------------------

FitHistory::FitHistory(std::string path) : m_path(std::move(path)) {}

bool FitHistory::load()
{
    m_entries.clear();

    if (!std::filesystem::exists(m_path))
        return true;  // absent file is a valid empty-history state

    std::ifstream in(m_path);
    if (!in)
        return false;

    nlohmann::json j;
    try
    {
        in >> j;
    }
    catch (const std::exception&)
    {
        return false;  // corrupt — start fresh
    }

    // Reject files from a future schema version rather than
    // misparse. The caller's in-memory history stays empty.
    const int version = j.value("schema_version", 0);
    if (version != SCHEMA_VERSION)
        return false;

    if (j.contains("entries") && j["entries"].is_array())
    {
        for (const auto& e : j["entries"])
        {
            m_entries.push_back(entryFromJson(e));
        }
    }
    return true;
}

bool FitHistory::save() const
{
    nlohmann::json j;
    j["schema_version"] = SCHEMA_VERSION;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : m_entries)
        arr.push_back(entryToJson(e));
    j["entries"] = arr;

    std::ofstream out(m_path);
    if (!out)
        return false;
    out << j.dump(2);
    return static_cast<bool>(out);
}

void FitHistory::record(const FitHistoryEntry& entry)
{
    m_entries.push_back(entry);

    // Cap per-formula entries. Walk backwards, keep the newest
    // MAX_ENTRIES_PER_FORMULA for each formula name, drop the rest.
    // Oldest-for-this-formula is evicted; other formulas are
    // untouched. Stable across the remaining entries.
    std::map<std::string, size_t> keep_count;
    std::vector<FitHistoryEntry> kept;
    kept.reserve(m_entries.size());
    for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it)
    {
        auto& count = keep_count[it->formula_name];
        if (count < MAX_ENTRIES_PER_FORMULA)
        {
            kept.push_back(*it);
            ++count;
        }
    }
    // kept is newest-first; reverse to restore chronological order.
    std::reverse(kept.begin(), kept.end());
    m_entries = std::move(kept);
}

std::vector<FitHistoryEntry> FitHistory::forFormula(const std::string& name) const
{
    std::vector<FitHistoryEntry> out;
    for (const auto& e : m_entries)
    {
        if (e.formula_name == name)
            out.push_back(e);
    }
    return out;
}

std::map<std::string, float> FitHistory::lastExportedCoeffsFor(
    const std::string& name) const
{
    // Reverse iterate — most recent matching entry wins.
    for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it)
    {
        if (it->formula_name == name && it->user_action == "exported")
            return it->coefficients;
    }
    return {};
}

// ---------------------------------------------------------------------------
// W6 — data-shape-aware seeding
// ---------------------------------------------------------------------------

namespace
{

// Symmetric log-ratio similarity for strictly-positive scalar
// features (n_points, variance). Equal values → 1.0. A factor-of-two
// mismatch → 0.5. Monotonically decreasing; floors at 0.
//
// The "+1" offset in the variance path is to prevent log2(0/anything)
// from going to -infinity. For n_points this isn't needed because the
// value is always ≥ 1 on any non-empty dataset, but we reuse the
// function for symmetry.
float logRatioSim(float a, float b)
{
    if (a <= 0.0f && b <= 0.0f)
        return 1.0f;   // both zero — indistinguishable at this axis
    const float lo = std::min(a, b);
    const float hi = std::max(a, b);
    if (lo <= 0.0f)
    {
        // One zero, the other not — treat as mismatch.
        return 0.0f;
    }
    const float ratio = hi / lo;            // >= 1
    const float logR  = std::log2(ratio);   // >= 0
    return 1.0f / (1.0f + logR);
}

// Intersection-over-union for two closed intervals. Returns 0 when
// the intervals don't overlap, 1 when they are identical, and a
// ratio in between for partial overlap. Handles the degenerate
// "width-zero" case by falling back to "equal endpoints → 1, else 0".
float intervalIoU(float aMin, float aMax,
                  float bMin, float bMax)
{
    const float intersectLo = std::max(aMin, bMin);
    const float intersectHi = std::min(aMax, bMax);
    if (intersectHi < intersectLo)
        return 0.0f;   // disjoint

    const float unionLo = std::min(aMin, bMin);
    const float unionHi = std::max(aMax, bMax);
    const float unionW  = unionHi - unionLo;
    if (unionW <= 0.0f)
    {
        // Both intervals are points (width zero). Same point → 1,
        // different points → 0 (already handled above).
        return (aMin == bMin) ? 1.0f : 0.0f;
    }
    const float interW = intersectHi - intersectLo;
    return interW / unionW;
}

} // namespace

float FitHistory::similarity(const FitHistoryMeta& a, const FitHistoryMeta& b)
{
    // 60 % weight: domain overlap per variable, averaged over
    // variables that appear in BOTH metas. Variables present only on
    // one side count as 0 contribution — they drag the average down.
    float  domainAccum     = 0.0f;
    size_t domainDenom     = 0;

    // Union of variable keys.
    std::vector<std::string> allKeys;
    allKeys.reserve(a.domain.size() + b.domain.size());
    for (const auto& [k, _] : a.domain) allKeys.push_back(k);
    for (const auto& [k, _] : b.domain)
    {
        if (std::find(allKeys.begin(), allKeys.end(), k) == allKeys.end())
            allKeys.push_back(k);
    }
    for (const auto& k : allKeys)
    {
        auto itA = a.domain.find(k);
        auto itB = b.domain.find(k);
        if (itA != a.domain.end() && itB != b.domain.end())
        {
            domainAccum += intervalIoU(itA->second.first,  itA->second.second,
                                       itB->second.first,  itB->second.second);
        }
        // one-sided variables contribute 0 to the numerator
        ++domainDenom;
    }
    const float domainSim = (domainDenom > 0)
                                ? domainAccum / static_cast<float>(domainDenom)
                                : 1.0f;   // both metas empty → call it identical

    // 20 % weight each: point count and variance.
    const float nSim = logRatioSim(static_cast<float>(a.n_points),
                                   static_cast<float>(b.n_points));
    const float vSim = logRatioSim(a.variance, b.variance);

    return 0.6f * domainSim + 0.2f * nSim + 0.2f * vSim;
}

SeedMatch FitHistory::bestSeedFor(const std::string& name,
                                    const FitHistoryMeta& currentMeta,
                                    float threshold) const
{
    SeedMatch best;
    // Newest-first scan so ties break toward the most recent entry.
    for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it)
    {
        if (it->formula_name != name || it->user_action != "exported")
            continue;
        const float s = similarity(currentMeta, it->data_meta);
        if (s >= threshold && s > best.similarity)
        {
            best.similarity   = s;
            best.coefficients = it->coefficients;
            best.timestamp    = it->timestamp;
        }
    }
    return best;
}

std::string FitHistory::hashDataset(const std::vector<DataPoint>& data)
{
    // Deterministic canonical form:
    //   <var_1>=<v_1>|<var_2>=<v_2>|...|obs=<observed>\n
    // Variables sorted alphabetically per-point; points emitted in
    // input order. The ordering guarantees the same data produces
    // the same hash regardless of std::map iteration quirks.
    std::ostringstream oss;
    oss.imbue(std::locale::classic());  // decimal point, not locale's
    oss.precision(9);
    for (const auto& p : data)
    {
        std::vector<std::string> keys;
        keys.reserve(p.variables.size());
        for (const auto& [k, _] : p.variables)
            keys.push_back(k);
        std::sort(keys.begin(), keys.end());
        for (const auto& k : keys)
        {
            oss << k << '=' << p.variables.at(k) << '|';
        }
        oss << "obs=" << p.observed << '\n';
    }
    return toHex64(fnv1a64(oss.str()));
}

FitHistoryMeta FitHistory::computeMeta(const std::vector<DataPoint>& data)
{
    FitHistoryMeta m;
    m.n_points = static_cast<int>(data.size());
    if (data.empty())
        return m;

    // Per-variable domain (min/max) across the dataset.
    for (const auto& p : data)
    {
        for (const auto& [k, v] : p.variables)
        {
            auto it = m.domain.find(k);
            if (it == m.domain.end())
                m.domain[k] = {v, v};
            else
            {
                it->second.first  = std::min(it->second.first,  v);
                it->second.second = std::max(it->second.second, v);
            }
        }
    }

    // Variance of observed values (population form, not sample —
    // this is a fingerprint, not an inferential statistic).
    double sum = 0.0;
    for (const auto& p : data) sum += p.observed;
    const double mean = sum / static_cast<double>(data.size());
    double ssd = 0.0;
    for (const auto& p : data)
    {
        const double d = p.observed - mean;
        ssd += d * d;
    }
    m.variance = static_cast<float>(ssd / static_cast<double>(data.size()));
    return m;
}

} // namespace Vestige
