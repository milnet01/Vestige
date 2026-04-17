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
    std::map<std::string, float> lastExportedCoeffsFor(const std::string& name) const;

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
