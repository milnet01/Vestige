// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file fit_history.h
/// @brief Persistence layer for past Formula Workbench fits.
///
/// Phase 1 §3.1 of the Workbench self-learning design
/// (docs/FORMULA_WORKBENCH_SELF_LEARNING_DESIGN.md). Writes one
/// JSON record per exported fit into ``.fit_history.json`` at the
/// project root. Downstream mechanisms read from this file:
///   - §3.2 learned initial guesses — seed LM from the most recent
///     fit for the selected formula.
///   - §3.3 ``--self-benchmark`` — rank library formulas by historical
///     AIC/BIC on data with similar meta-features.
///
/// Entries are per-formula-capped (see ``MAX_ENTRIES_PER_FORMULA``)
/// so the file stays a fixed size rather than growing with every
/// fit. Cumulative statistics (total exports etc.) are not yet
/// persisted — this is the raw-event store; Phase 2 layers aggregation
/// on top.
///
/// File location: the workbench writes to ``.fit_history.json`` in
/// its current working directory, which in the typical invocation is
/// the project root. The file is gitignored (developer-local).
///
/// Schema: single top-level object with ``schema_version`` and
/// ``entries[]``. Bump ``SCHEMA_VERSION`` on any non-additive change
/// so old files surface a clear error instead of silently misparsing.
#pragma once

#include "formula/curve_fitter.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Vestige
{

/// @brief Summary statistics of a fit's input dataset.
///
/// Used by the ranking layer (§3.1) as a coarse "is this the same
/// shape as before?" fingerprint. Two datasets with similar
/// ``n_points``, ``domain``, and ``variance`` on the same formula
/// can expect similar fit quality — so their AIC/BIC rankings
/// transfer across.
struct FitHistoryMeta
{
    int n_points = 0;                                               ///< Dataset size.
    std::map<std::string, std::pair<float, float>> domain;          ///< var -> (min, max) across the dataset.
    float variance = 0.0f;                                          ///< Observed-value variance.
};

/// @brief One persisted fit event — the atomic unit of fit history.
struct FitHistoryEntry
{
    std::string timestamp;                      ///< ISO-8601 UTC, to the second.
    std::string formula_name;                   ///< FormulaDefinition.name.
    std::string data_hash;                      ///< Deterministic dataset hash.
    FitHistoryMeta data_meta;                   ///< Dataset fingerprint.
    std::map<std::string, float> coefficients;  ///< Fitted coefficient values.
    float r_squared = 0.0f;                     ///< Coefficient of determination.
    float rmse = 0.0f;                          ///< Root mean square error.
    float aic = 0.0f;                           ///< Akaike information criterion.
    float bic = 0.0f;                           ///< Bayesian information criterion.
    int iterations = 0;                         ///< LM iterations performed.
    bool converged = false;                     ///< True if the LM algorithm converged.
    std::string user_action;                    ///< "exported" | "discarded" | "iterated".
};

/// @brief Result of a data-shape-aware seed lookup.
///
/// Returned by ``FitHistory::bestSeedFor``. ``coefficients`` is
/// empty when no prior entry met the similarity threshold — callers
/// should then fall back to library defaults. ``similarity`` is the
/// normalized score in ``[0, 1]`` that earned this entry the win,
/// useful for the UI badge (e.g. "seeded from fit @ 2026-04-12
/// (data similarity 0.82)").
struct SeedMatch
{
    std::map<std::string, float> coefficients;
    std::string                  timestamp;
    float                        similarity = 0.0f;
};

/// @brief Load/save/query interface over ``.fit_history.json``.
class FitHistory
{
public:
    /// Schema version embedded in the JSON file. Bump on any
    /// non-additive change; loaders that see a mismatched version
    /// start with an empty history rather than misparse.
    static constexpr int SCHEMA_VERSION = 1;

    /// Cap on per-formula entry count. When recording would exceed
    /// this, the oldest entry for that formula is evicted. Global
    /// cap is therefore ``MAX_ENTRIES_PER_FORMULA × library_size``,
    /// which keeps the file under a few hundred KB even for busy
    /// workflows.
    static constexpr size_t MAX_ENTRIES_PER_FORMULA = 20;

    /// Default similarity threshold for ``bestSeedFor``. Entries
    /// below this are not considered — library defaults seed the
    /// fit instead. 0.5 is calibrated so that same-domain /
    /// same-scale datasets pass, while wildly different scales fall
    /// through to a cold start (which is the whole point of W6:
    /// a mismatched seed is worse than a library default).
    static constexpr float DEFAULT_SEED_SIMILARITY_THRESHOLD = 0.5f;

    /// Construct pointing at the given file path. Does not read
    /// from disk — call ``load()`` explicitly (lets tests construct
    /// without side effects).
    explicit FitHistory(std::string path);

    /// Read the file at the configured path. Returns ``true`` on
    /// successful parse (including empty/missing file, which
    /// populates an empty history). Returns ``false`` only when the
    /// file exists but is corrupt or has an unknown schema version —
    /// in that case the in-memory history is cleared so the caller
    /// starts fresh.
    bool load();

    /// Write the current entries to the configured path. Returns
    /// ``true`` on success, ``false`` on I/O failure.
    bool save() const;

    /// Append an entry. If the formula already has
    /// ``MAX_ENTRIES_PER_FORMULA`` entries, the oldest one for that
    /// formula is evicted. Newest entry is always last in ``entries()``.
    void record(const FitHistoryEntry& entry);

    /// All entries across all formulas, oldest first.
    const std::vector<FitHistoryEntry>& entries() const { return m_entries; }

    /// Subset filter by formula name. Preserves chronological order.
    std::vector<FitHistoryEntry> forFormula(const std::string& name) const;

    /// Coefficients from the most recent ``exported`` fit for
    /// ``name``. Empty map when no such entry exists. Used by §3.2
    /// to seed LM with the last successful fit.
    ///
    /// Data-shape-agnostic: the newest exported fit wins regardless
    /// of whether its dataset resembles the current one. Prefer
    /// ``bestSeedFor`` when the caller has current ``FitHistoryMeta``
    /// available; this method is still correct for paths that do
    /// not (CLI tools, offline analysis).
    std::map<std::string, float> lastExportedCoeffsFor(const std::string& name) const;

    /// @brief W6 — data-shape-aware seed lookup.
    ///
    /// Finds the exported entry for ``name`` whose ``data_meta``
    /// is most similar to ``currentMeta``, subject to
    /// ``similarity >= threshold``. When no entry clears the
    /// threshold, the returned ``SeedMatch`` has empty
    /// ``coefficients`` — callers should then seed from library
    /// defaults instead. Ties are broken by recency.
    ///
    /// This is the seeding interface the Workbench uses once the
    /// user has loaded data: it prevents the case where fitting
    /// two very different datasets on the same formula causes the
    /// newer fit to seed the older one badly. See
    /// ``docs/FORMULA_WORKBENCH_SELF_LEARNING_DESIGN.md`` §3.1
    /// advanced.
    SeedMatch bestSeedFor(const std::string& name,
                          const FitHistoryMeta& currentMeta,
                          float threshold = DEFAULT_SEED_SIMILARITY_THRESHOLD) const;

    /// @brief Similarity between two meta-feature fingerprints,
    ///        normalized to ``[0, 1]`` (1.0 = identical).
    ///
    /// Composite of three terms:
    ///   - 60 % domain-overlap ratio (intersection ÷ union of each
    ///     variable's [min, max] range, averaged across variables
    ///     that appear in both metas — variables present only on
    ///     one side count as 0 overlap).
    ///   - 20 % ``n_points`` similarity via log2-ratio falloff
    ///     (equal = 1.0, 2× different ≈ 0.5, 4× ≈ 0.33).
    ///   - 20 % variance similarity via the same log2-ratio
    ///     falloff, with a small additive offset so that zero
    ///     variance doesn't blow up.
    ///
    /// Domain dominates because it is the strongest predictor of
    /// "would the old coefficients even make sense on this data?".
    /// A fit at x ∈ [0, 5] tells you almost nothing about the
    /// coefficients that best fit the same formula over x ∈
    /// [100, 500].
    static float similarity(const FitHistoryMeta& a,
                            const FitHistoryMeta& b);

    /// Clear all entries (testing helper; also useful when a library
    /// rewrite makes prior entries meaningless).
    void clear() { m_entries.clear(); }

    /// Deterministic dataset hash. Identical data → identical hash
    /// across runs, platforms, and compiler versions. 64-bit FNV-1a
    /// is collision-resistant enough for the "did we see this data
    /// before?" use case without needing crypto-grade security.
    static std::string hashDataset(const std::vector<DataPoint>& data);

    /// Compute the meta-features fingerprint of a dataset. Exposed
    /// for callers constructing a FitHistoryEntry outside the
    /// Workbench (e.g. tests, future CLI).
    static FitHistoryMeta computeMeta(const std::vector<DataPoint>& data);

private:
    std::string m_path;
    std::vector<FitHistoryEntry> m_entries;
};

} // namespace Vestige
