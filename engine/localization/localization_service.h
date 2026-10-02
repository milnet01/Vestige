// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file localization_service.h
/// @brief Engine-wide language state (Phase 10 Localization L4).
///        See docs/phases/phase_10_localization_design.md § 5.6.
#pragma once

#include "core/i_system.h"
#include "localization/string_table.h"

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace Vestige
{

class Engine;

/// @brief Engine-wide language state. Wired as an ISystem so it participates
/// in the same lifecycle as audio / physics / etc. Does no per-frame work.
///
/// Fallback order is locked: active → English → key (design § 5.6, § 12 Q1).
class LocalizationService final : public ISystem
{
public:
    /// Clears the static engine handle the free tr() resolves through if it
    /// still points at this service's engine. shutdown() does the same for the
    /// normal lifecycle; the destructor covers the path where a SystemRegistry
    /// destroys the service without a prior shutdown() (e.g. a test that calls
    /// initialize() directly), preventing a dangling-pointer use in tr().
    ~LocalizationService() override;

    // ---- ISystem (four pure virtuals) -------------------------------
    const std::string& getSystemName() const override { return s_name; }
    bool initialize(Engine& engine) override; ///< Loads the reference + default "en".
    void shutdown() override;
    void update(float /*deltaTime*/) override {} ///< No per-frame work.
    // getUpdatePhase() not overridden — default UpdatePhase::Update is correct
    // (setLanguage is driven from menu/input, not update()).

    /// @brief Active language code (BCP 47 short tag: "en", "he", "el", "la").
    const std::string& languageCode() const { return m_languageCode; }

    /// @brief Switch language. Loads `<dir>/<code>.json`, swaps the active
    /// table, and publishes a LanguageChangedEvent on the engine event bus so
    /// panels can re-fetch their strings. Returns false (keeping the old
    /// language, no event) if the file is missing or malformed.
    bool setLanguage(const std::string& code);

    /// @brief Lookup a key in the active language, falling back to the
    /// reference language ("en") and finally to the key itself. The returned
    /// view aliases table storage on a hit (valid until the next setLanguage)
    /// or the caller's `key` on a miss — materialise to std::string at the
    /// call site (design § 5.5 / § 5.6).
    std::string_view tr(std::string_view key) const;

    /// @brief Keys present in the reference language ("en") but absent from the
    /// active language, sorted. Backs the editor "missing keys" overlay (L6,
    /// design § 5.7): these render the English fallback at runtime, so they are
    /// the translator's worklist. Empty when the active language is the
    /// reference itself. Not on any hot path.
    std::vector<std::string> missingKeys() const;

    /// @brief Override the directory localization JSON files load from
    /// (default "assets/localization"). Call before initialize(). Mirrors the
    /// configurable fonts dir; lets tests point at the source-tree seed files.
    void setLocalizationDir(std::string dir) { m_dir = std::move(dir); }

private:
    std::string filePathFor(const std::string& code) const;

    static inline const std::string s_name = "Localization";
    static constexpr const char* kReferenceCode = "en";

    Engine*     m_engine = nullptr;
    std::string m_dir = "assets/localization";
    std::string m_languageCode;
    StringTable m_active;
    StringTable m_reference; ///< English, loaded once at boot for fallback.
};

/// @brief Free-function convenience. Resolves the registered LocalizationService
/// via SystemRegistry against the engine pointer captured at initialize() time.
/// Returns the key itself when no service is registered (safe in unit tests
/// that don't spin up the registry). Materialise the result at the call site.
std::string_view tr(std::string_view key);

/// @brief One named value for `trf` (3D_E-0024).
struct TrArg
{
    std::string_view name;
    std::string      value;
};

/// @brief Replaces each `{name}` in `pattern` with the value of the argument
///        of that name. A placeholder with no argument stays as written;
///        `{{` and `}}` write a single brace. Pure, so it is testable alone.
std::string substituteTrArgs(std::string_view pattern, std::initializer_list<TrArg> args);

/// @brief `tr(key)` with its `{name}` slots filled from `args` — for text
///        whose word order differs between languages ("Press [{key}] to
///        {action}"). Same fallback as `tr`.
std::string trf(std::string_view key, std::initializer_list<TrArg> args);

#ifdef VESTIGE_TEST_HOOKS
/// @brief Test hook: while alive, `tr` and `trf` look a key up in `table`
///        first, before any registered service. One at a time; not for
///        production code.
class ScopedStringTableOverride
{
public:
    explicit ScopedStringTableOverride(const StringTable& table);
    ~ScopedStringTableOverride();
    ScopedStringTableOverride(const ScopedStringTableOverride&) = delete;
    ScopedStringTableOverride& operator=(const ScopedStringTableOverride&) = delete;

private:
    const StringTable* m_previous = nullptr;
};
#endif

} // namespace Vestige
