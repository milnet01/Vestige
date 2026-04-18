// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_fit_history.cpp
/// @brief Unit tests for the Formula Workbench FitHistory persistence layer.
///
/// Phase 1 §3.1 of the Workbench self-learning design. Covers:
///   - Load/save/round-trip with and without existing files
///   - Corrupt + wrong-schema recovery (start fresh, don't misparse)
///   - record() eviction policy (per-formula cap)
///   - lastExportedCoeffsFor() selection by user_action
///   - hashDataset() determinism and sensitivity
///   - computeMeta() domain/variance correctness
#include <gtest/gtest.h>

#include "fit_history.h"

#include <filesystem>
#include <fstream>

using namespace Vestige;

namespace
{

DataPoint makePoint(float x, float y)
{
    DataPoint p;
    p.variables["x"] = x;
    p.observed = y;
    return p;
}

FitHistoryEntry makeEntry(const std::string& name,
                          const std::string& action = "exported",
                          float rSquared = 0.99f)
{
    FitHistoryEntry e;
    e.timestamp     = "2026-04-17T10:00:00Z";
    e.formula_name  = name;
    e.data_hash     = "abc123";
    e.data_meta.n_points = 10;
    e.coefficients  = {{"a", 1.0f}, {"b", 2.0f}};
    e.r_squared     = rSquared;
    e.rmse          = 0.01f;
    e.aic           = -100.0f;
    e.bic           = -95.0f;
    e.iterations    = 5;
    e.converged     = true;
    e.user_action   = action;
    return e;
}

} // namespace

// ---------------------------------------------------------------------------
// Load / Save / Round-trip
// ---------------------------------------------------------------------------

TEST(FitHistoryLoad, MissingFileReturnsEmpty)
{
    const auto path = std::filesystem::temp_directory_path() / "fh_missing.json";
    std::filesystem::remove(path);
    FitHistory h(path.string());
    EXPECT_TRUE(h.load());
    EXPECT_TRUE(h.entries().empty());
}

TEST(FitHistoryRoundTrip, EntryPersistsAcrossLoadSave)
{
    const auto path = std::filesystem::temp_directory_path() / "fh_roundtrip.json";
    std::filesystem::remove(path);
    {
        FitHistory h(path.string());
        h.record(makeEntry("stokes_drag"));
        EXPECT_TRUE(h.save());
    }

    FitHistory h2(path.string());
    EXPECT_TRUE(h2.load());
    ASSERT_EQ(h2.entries().size(), 1u);
    const auto& e = h2.entries().front();
    EXPECT_EQ(e.formula_name, "stokes_drag");
    EXPECT_EQ(e.user_action,  "exported");
    EXPECT_FLOAT_EQ(e.r_squared, 0.99f);
    EXPECT_EQ(e.coefficients.at("a"), 1.0f);
    std::filesystem::remove(path);
}

TEST(FitHistoryLoad, CorruptJsonReturnsFalseAndClears)
{
    const auto path = std::filesystem::temp_directory_path() / "fh_corrupt.json";
    {
        std::ofstream out(path);
        out << "{not valid json";
    }
    FitHistory h(path.string());
    // Pre-populate so we can assert the clear-on-failure behaviour.
    h.record(makeEntry("placeholder"));
    EXPECT_FALSE(h.load());
    EXPECT_TRUE(h.entries().empty());
    std::filesystem::remove(path);
}

TEST(FitHistoryLoad, WrongSchemaVersionIsRejected)
{
    const auto path = std::filesystem::temp_directory_path() / "fh_wrongver.json";
    {
        std::ofstream out(path);
        out << R"({"schema_version": 999, "entries": []})";
    }
    FitHistory h(path.string());
    EXPECT_FALSE(h.load());
    EXPECT_TRUE(h.entries().empty());
    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// record() eviction policy
// ---------------------------------------------------------------------------

TEST(FitHistoryRecord, CapsPerFormulaAtMaxEntries)
{
    FitHistory h("/tmp/unused_fh.json");
    const size_t cap = FitHistory::MAX_ENTRIES_PER_FORMULA;
    for (size_t i = 0; i < cap + 5; ++i)
    {
        FitHistoryEntry e = makeEntry("stokes_drag");
        e.iterations = static_cast<int>(i);  // distinct marker per entry
        h.record(e);
    }
    const auto entries = h.forFormula("stokes_drag");
    EXPECT_EQ(entries.size(), cap);
    // The most recent (highest iterations=i) should survive; the
    // oldest (i=0..4) got evicted.
    EXPECT_EQ(entries.front().iterations, 5);
    EXPECT_EQ(entries.back().iterations, static_cast<int>(cap + 4));
}

TEST(FitHistoryRecord, DoesNotEvictOtherFormulas)
{
    FitHistory h("/tmp/unused_fh.json");
    h.record(makeEntry("foo"));
    // Fill bar up to cap + extras — only bar should get trimmed.
    for (size_t i = 0; i < FitHistory::MAX_ENTRIES_PER_FORMULA + 3; ++i)
        h.record(makeEntry("bar"));
    EXPECT_EQ(h.forFormula("foo").size(), 1u);
    EXPECT_EQ(h.forFormula("bar").size(), FitHistory::MAX_ENTRIES_PER_FORMULA);
}

// ---------------------------------------------------------------------------
// lastExportedCoeffsFor — §3.2 seed support
// ---------------------------------------------------------------------------

TEST(FitHistoryLastExport, ReturnsEmptyWhenNoMatch)
{
    FitHistory h("/tmp/unused_fh.json");
    h.record(makeEntry("foo", "discarded"));
    EXPECT_TRUE(h.lastExportedCoeffsFor("foo").empty());
    EXPECT_TRUE(h.lastExportedCoeffsFor("nonexistent").empty());
}

TEST(FitHistoryLastExport, SelectsMostRecentExportedOnly)
{
    FitHistory h("/tmp/unused_fh.json");
    // Older exported with a=1, newer discarded with a=999, older-still
    // exported with a=0.5 — the newer "exported" should win, which is
    // the FIRST of these. If that's missing, the next oldest "exported"
    // should be picked (not the "discarded" event).
    FitHistoryEntry e1 = makeEntry("foo", "exported");
    e1.coefficients = {{"a", 0.5f}};
    h.record(e1);

    FitHistoryEntry e2 = makeEntry("foo", "exported");
    e2.coefficients = {{"a", 1.0f}};
    h.record(e2);

    FitHistoryEntry e3 = makeEntry("foo", "discarded");
    e3.coefficients = {{"a", 999.0f}};
    h.record(e3);

    const auto coeffs = h.lastExportedCoeffsFor("foo");
    ASSERT_FALSE(coeffs.empty());
    EXPECT_FLOAT_EQ(coeffs.at("a"), 1.0f);
}

// ---------------------------------------------------------------------------
// hashDataset determinism
// ---------------------------------------------------------------------------

TEST(FitHistoryHash, IdenticalDataSameHash)
{
    const std::vector<DataPoint> d1 = {makePoint(1.0f, 2.0f), makePoint(3.0f, 6.0f)};
    const std::vector<DataPoint> d2 = d1;
    EXPECT_EQ(FitHistory::hashDataset(d1), FitHistory::hashDataset(d2));
}

TEST(FitHistoryHash, DifferentObservedDifferentHash)
{
    const std::vector<DataPoint> d1 = {makePoint(1.0f, 2.0f)};
    const std::vector<DataPoint> d2 = {makePoint(1.0f, 3.0f)};
    EXPECT_NE(FitHistory::hashDataset(d1), FitHistory::hashDataset(d2));
}

TEST(FitHistoryHash, DifferentVariablesDifferentHash)
{
    std::vector<DataPoint> d1 = {makePoint(1.0f, 2.0f)};
    DataPoint p;
    p.variables["y"] = 1.0f;
    p.observed = 2.0f;
    const std::vector<DataPoint> d2 = {p};
    EXPECT_NE(FitHistory::hashDataset(d1), FitHistory::hashDataset(d2));
}

TEST(FitHistoryHash, EmptyDatasetIsStable)
{
    const std::vector<DataPoint> empty;
    const std::string h1 = FitHistory::hashDataset(empty);
    const std::string h2 = FitHistory::hashDataset(empty);
    EXPECT_EQ(h1, h2);
    EXPECT_EQ(h1.size(), 16u);  // 64-bit hex string
}

// ---------------------------------------------------------------------------
// computeMeta
// ---------------------------------------------------------------------------

TEST(FitHistoryMetaCalc, EmptyDatasetHasZeroFields)
{
    const auto m = FitHistory::computeMeta({});
    EXPECT_EQ(m.n_points, 0);
    EXPECT_FLOAT_EQ(m.variance, 0.0f);
    EXPECT_TRUE(m.domain.empty());
}

TEST(FitHistoryMetaCalc, DomainReflectsMinMax)
{
    const std::vector<DataPoint> data = {
        makePoint(1.0f, 0.0f),
        makePoint(5.0f, 0.0f),
        makePoint(3.0f, 0.0f),
    };
    const auto m = FitHistory::computeMeta(data);
    EXPECT_EQ(m.n_points, 3);
    ASSERT_EQ(m.domain.count("x"), 1u);
    EXPECT_FLOAT_EQ(m.domain.at("x").first,  1.0f);
    EXPECT_FLOAT_EQ(m.domain.at("x").second, 5.0f);
}

TEST(FitHistoryMetaCalc, VarianceZeroForConstantObservations)
{
    const std::vector<DataPoint> data = {
        makePoint(0.0f, 5.0f), makePoint(1.0f, 5.0f), makePoint(2.0f, 5.0f),
    };
    EXPECT_FLOAT_EQ(FitHistory::computeMeta(data).variance, 0.0f);
}

TEST(FitHistoryMetaCalc, VariancePopulationForm)
{
    // Population variance of {1, 3, 5} = ((1-3)² + (3-3)² + (5-3)²) / 3
    //                                  = (4 + 0 + 4) / 3 ≈ 2.6667
    const std::vector<DataPoint> data = {
        makePoint(0.0f, 1.0f), makePoint(0.0f, 3.0f), makePoint(0.0f, 5.0f),
    };
    EXPECT_NEAR(FitHistory::computeMeta(data).variance, 8.0f / 3.0f, 1e-5f);
}

// ---------------------------------------------------------------------------
// W6 — similarity() and bestSeedFor()
// ---------------------------------------------------------------------------

namespace
{

FitHistoryMeta makeMeta(int n, float variance,
                        std::map<std::string, std::pair<float, float>> domain)
{
    FitHistoryMeta m;
    m.n_points = n;
    m.variance = variance;
    m.domain   = std::move(domain);
    return m;
}

} // namespace

TEST(FitHistorySimilarity, IdenticalMetaScoresOne)
{
    const auto m = makeMeta(50, 0.5f, {{"x", {0.0f, 10.0f}}});
    EXPECT_NEAR(FitHistory::similarity(m, m), 1.0f, 1e-5f);
}

TEST(FitHistorySimilarity, EmptyMetasMatch)
{
    FitHistoryMeta empty;
    // Both metas empty — the similarity function should treat the
    // absence of features as "indistinguishable" rather than "zero".
    EXPECT_NEAR(FitHistory::similarity(empty, empty), 1.0f, 1e-5f);
}

TEST(FitHistorySimilarity, DisjointDomainsScoreLow)
{
    const auto a = makeMeta(50, 0.5f, {{"x", {0.0f,  10.0f}}});
    const auto b = makeMeta(50, 0.5f, {{"x", {100.0f, 500.0f}}});
    const float s = FitHistory::similarity(a, b);
    // Domain term is 0, remaining 40 % comes from matching n_points
    // and variance → 0.4 ceiling. Must stay under the default 0.5
    // threshold so disjoint-range fits do NOT seed each other.
    EXPECT_LT(s, 0.5f);
    EXPECT_GT(s, 0.35f);
}

TEST(FitHistorySimilarity, PartialDomainOverlap)
{
    // Intervals [0, 10] and [5, 15] — intersection 5, union 15 → 1/3.
    const auto a = makeMeta(50, 0.5f, {{"x", {0.0f,  10.0f}}});
    const auto b = makeMeta(50, 0.5f, {{"x", {5.0f,  15.0f}}});
    const float s = FitHistory::similarity(a, b);
    // 0.6 * (1/3) + 0.2 * 1.0 + 0.2 * 1.0 = 0.6
    EXPECT_NEAR(s, 0.6f, 0.02f);
}

TEST(FitHistorySimilarity, MultipleVariablesAveraged)
{
    const auto a = makeMeta(50, 0.5f,
        {{"x", {0.0f, 10.0f}}, {"y", {0.0f, 10.0f}}});
    const auto b = makeMeta(50, 0.5f,
        {{"x", {0.0f, 10.0f}}, {"y", {5.0f, 15.0f}}});
    // Domain: x → 1.0, y → 1/3; avg = (1 + 1/3) / 2 = 2/3.
    // Composite: 0.6*2/3 + 0.2 + 0.2 = 0.4 + 0.4 = 0.8.
    EXPECT_NEAR(FitHistory::similarity(a, b), 0.8f, 0.02f);
}

TEST(FitHistorySimilarity, PointCountLogRatioFalloff)
{
    const auto a = makeMeta(100, 0.5f, {{"x", {0.0f, 10.0f}}});
    const auto b = makeMeta(200, 0.5f, {{"x", {0.0f, 10.0f}}});
    // 2x n_points mismatch → nSim ≈ 1/(1+log2(2)) = 0.5.
    // Composite: 0.6*1 + 0.2*0.5 + 0.2*1 = 0.9.
    EXPECT_NEAR(FitHistory::similarity(a, b), 0.9f, 0.02f);
}

TEST(FitHistorySimilarity, OneSidedVariableDragsScore)
{
    // a has one variable; b has two — the extra variable on b counts
    // as a 0 overlap for that axis, so the domain average is halved.
    const auto a = makeMeta(50, 0.5f, {{"x", {0.0f, 10.0f}}});
    const auto b = makeMeta(50, 0.5f,
        {{"x", {0.0f, 10.0f}}, {"y", {0.0f, 10.0f}}});
    // Domain average: (1 + 0) / 2 = 0.5. Composite: 0.6*0.5 + 0.2 + 0.2 = 0.7.
    EXPECT_NEAR(FitHistory::similarity(a, b), 0.7f, 0.02f);
}

TEST(FitHistoryBestSeed, PrefersMostSimilarOverMostRecent)
{
    FitHistory h("/tmp/vestige_seed_test_nope.json");
    // Older fit on matching data.
    FitHistoryEntry matching;
    matching.timestamp     = "2026-04-10T10:00:00Z";
    matching.formula_name  = "beer_lambert";
    matching.user_action   = "exported";
    matching.data_meta     = makeMeta(50, 0.5f, {{"depth", {0.0f, 5.0f}}});
    matching.coefficients  = {{"alpha", 0.4f}};
    h.record(matching);

    // Newer fit on totally different data (disjoint domain).
    FitHistoryEntry recent;
    recent.timestamp    = "2026-04-18T10:00:00Z";
    recent.formula_name = "beer_lambert";
    recent.user_action  = "exported";
    recent.data_meta    = makeMeta(50, 0.5f, {{"depth", {100.0f, 500.0f}}});
    recent.coefficients = {{"alpha", 99.0f}};   // obviously wrong for the matching query
    h.record(recent);

    const auto current = makeMeta(50, 0.5f, {{"depth", {0.0f, 5.0f}}});
    const auto seed = h.bestSeedFor("beer_lambert", current);
    ASSERT_FALSE(seed.coefficients.empty());
    EXPECT_FLOAT_EQ(seed.coefficients.at("alpha"), 0.4f);
    EXPECT_EQ(seed.timestamp, "2026-04-10T10:00:00Z");
    EXPECT_GT(seed.similarity, 0.99f);
}

TEST(FitHistoryBestSeed, ReturnsEmptyBelowThreshold)
{
    FitHistory h("/tmp/vestige_seed_test_nope.json");
    FitHistoryEntry farAway;
    farAway.timestamp    = "2026-04-10T10:00:00Z";
    farAway.formula_name = "beer_lambert";
    farAway.user_action  = "exported";
    farAway.data_meta    = makeMeta(5, 1000.0f, {{"depth", {1000.0f, 5000.0f}}});
    farAway.coefficients = {{"alpha", 99.0f}};
    h.record(farAway);

    const auto current = makeMeta(100, 0.1f, {{"depth", {0.0f, 5.0f}}});
    const auto seed = h.bestSeedFor("beer_lambert", current);
    EXPECT_TRUE(seed.coefficients.empty());
    EXPECT_LT(seed.similarity, FitHistory::DEFAULT_SEED_SIMILARITY_THRESHOLD);
}

TEST(FitHistoryBestSeed, IgnoresDiscardedEntries)
{
    FitHistory h("/tmp/vestige_seed_test_nope.json");
    FitHistoryEntry discarded;
    discarded.timestamp    = "2026-04-10T10:00:00Z";
    discarded.formula_name = "beer_lambert";
    discarded.user_action  = "discarded";   // not a seed source per W4
    discarded.data_meta    = makeMeta(50, 0.5f, {{"depth", {0.0f, 5.0f}}});
    discarded.coefficients = {{"alpha", 0.4f}};
    h.record(discarded);

    const auto current = makeMeta(50, 0.5f, {{"depth", {0.0f, 5.0f}}});
    const auto seed = h.bestSeedFor("beer_lambert", current);
    EXPECT_TRUE(seed.coefficients.empty());
}

TEST(FitHistoryBestSeed, IgnoresOtherFormulas)
{
    FitHistory h("/tmp/vestige_seed_test_nope.json");
    FitHistoryEntry other;
    other.timestamp    = "2026-04-10T10:00:00Z";
    other.formula_name = "stokes_drag";
    other.user_action  = "exported";
    other.data_meta    = makeMeta(50, 0.5f, {{"depth", {0.0f, 5.0f}}});
    other.coefficients = {{"alpha", 0.4f}};
    h.record(other);

    const auto current = makeMeta(50, 0.5f, {{"depth", {0.0f, 5.0f}}});
    const auto seed = h.bestSeedFor("beer_lambert", current);
    EXPECT_TRUE(seed.coefficients.empty());
}

TEST(FitHistoryBestSeed, EmptyHistoryReturnsEmpty)
{
    FitHistory h("/tmp/vestige_seed_test_nope.json");
    const auto current = makeMeta(50, 0.5f, {{"depth", {0.0f, 5.0f}}});
    const auto seed = h.bestSeedFor("beer_lambert", current);
    EXPECT_TRUE(seed.coefficients.empty());
    EXPECT_FLOAT_EQ(seed.similarity, 0.0f);
}

TEST(FitHistoryBestSeed, TiesBreakTowardMostRecent)
{
    FitHistory h("/tmp/vestige_seed_test_nope.json");
    // Two exported fits on identical data; seed should come from the
    // newer one (the existing lastExportedCoeffsFor behaviour stays
    // the tiebreaker).
    FitHistoryEntry older;
    older.timestamp    = "2026-04-10T10:00:00Z";
    older.formula_name = "beer_lambert";
    older.user_action  = "exported";
    older.data_meta    = makeMeta(50, 0.5f, {{"depth", {0.0f, 5.0f}}});
    older.coefficients = {{"alpha", 0.4f}};
    h.record(older);

    FitHistoryEntry newer;
    newer.timestamp    = "2026-04-18T10:00:00Z";
    newer.formula_name = "beer_lambert";
    newer.user_action  = "exported";
    newer.data_meta    = makeMeta(50, 0.5f, {{"depth", {0.0f, 5.0f}}});
    newer.coefficients = {{"alpha", 0.42f}};
    h.record(newer);

    const auto current = makeMeta(50, 0.5f, {{"depth", {0.0f, 5.0f}}});
    const auto seed = h.bestSeedFor("beer_lambert", current);
    ASSERT_FALSE(seed.coefficients.empty());
    EXPECT_FLOAT_EQ(seed.coefficients.at("alpha"), 0.42f);
    EXPECT_EQ(seed.timestamp, "2026-04-18T10:00:00Z");
}

TEST(FitHistoryBestSeed, CustomThresholdOverride)
{
    FitHistory h("/tmp/vestige_seed_test_nope.json");
    FitHistoryEntry moderate;
    moderate.timestamp    = "2026-04-10T10:00:00Z";
    moderate.formula_name = "beer_lambert";
    moderate.user_action  = "exported";
    moderate.data_meta    = makeMeta(50, 0.5f, {{"depth", {5.0f, 15.0f}}});
    moderate.coefficients = {{"alpha", 0.4f}};
    h.record(moderate);

    const auto current = makeMeta(50, 0.5f, {{"depth", {0.0f, 10.0f}}});
    // Default threshold 0.5 — this one scores 0.6, should pass.
    const auto highThresh = h.bestSeedFor("beer_lambert", current);
    EXPECT_FALSE(highThresh.coefficients.empty());
    // Raise threshold to 0.9 — same entry now below, should return empty.
    const auto strict = h.bestSeedFor("beer_lambert", current, 0.9f);
    EXPECT_TRUE(strict.coefficients.empty());
}
