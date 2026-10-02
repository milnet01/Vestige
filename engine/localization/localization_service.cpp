// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "localization/localization_service.h"

#include "core/engine.h"
#include "core/logger.h"
#include "core/system_events.h"

namespace Vestige
{
namespace
{
// Engine pointer captured at initialize() time so the free-function tr() can
// resolve the registered service (design § 5.6). One LocalizationService lives
// per engine, so a single static handle is sufficient.
Engine* s_localizationEngine = nullptr;

#ifdef VESTIGE_TEST_HOOKS
// ScopedStringTableOverride's table, consulted before the service (3D_E-0024).
const StringTable* s_overrideTable = nullptr;
#endif
} // namespace

LocalizationService::~LocalizationService()
{
    // Guard tr() against a dangling engine handle if this service is destroyed
    // without a prior shutdown() (a registry teardown of a directly-initialized
    // service). Harmless when the handle already points elsewhere or is null.
    if (s_localizationEngine == m_engine)
    {
        s_localizationEngine = nullptr;
    }
}

bool LocalizationService::initialize(Engine& engine)
{
    m_engine = &engine;
    s_localizationEngine = &engine;

    // The reference (English) table is loaded once at boot and kept for
    // fallback. A missing reference file is logged but does not fail engine
    // init — existing English-literal call sites keep working (design § 10).
    if (!m_reference.loadFromFile(filePathFor(kReferenceCode)))
    {
        Logger::warning("[Localization] reference language '" +
                        std::string(kReferenceCode) + "' failed to load from " +
                        filePathFor(kReferenceCode));
    }

    // Active language defaults to the reference language.
    m_active = m_reference;
    m_languageCode = kReferenceCode;
    return true;
}

void LocalizationService::shutdown()
{
    if (s_localizationEngine == m_engine)
    {
        s_localizationEngine = nullptr;
    }
    m_engine = nullptr;
}

bool LocalizationService::setLanguage(const std::string& code)
{
    // Load into a fresh table so a failed load never disturbs the active one.
    StringTable next;
    if (!next.loadFromFile(filePathFor(code)))
    {
        Logger::warning("[Localization] language '" + code +
                        "' failed to load; keeping '" + m_languageCode + "'");
        return false; // Keep the old language, publish nothing.
    }

    m_active = std::move(next);
    m_languageCode = code;

    if (m_engine)
    {
        m_engine->getEventBus().publish(LanguageChangedEvent(code));
    }
    return true;
}

std::string_view LocalizationService::tr(std::string_view key) const
{
    // Fallback order is locked: active → English → key (design § 5.6, § 12 Q1).
    // contains() distinguishes a genuine hit from the key-passthrough miss,
    // even for the degenerate case of a value equal to its key.
    if (m_active.contains(key))
    {
        return m_active.get(key);
    }
    if (m_reference.contains(key))
    {
        return m_reference.get(key);
    }
    return key;
}

std::vector<std::string> LocalizationService::missingKeys() const
{
    // m_reference.keys() is already sorted, so the result stays sorted.
    std::vector<std::string> missing;
    for (const std::string& key : m_reference.keys())
    {
        if (!m_active.contains(key))
        {
            missing.push_back(key);
        }
    }
    return missing;
}

std::string LocalizationService::filePathFor(const std::string& code) const
{
    return m_dir + "/" + code + ".json";
}

std::string_view tr(std::string_view key)
{
#ifdef VESTIGE_TEST_HOOKS
    if (s_overrideTable != nullptr && s_overrideTable->contains(key))
    {
        return s_overrideTable->get(key);
    }
#endif
    if (s_localizationEngine != nullptr)
    {
        if (auto* svc = s_localizationEngine->getSystemRegistry()
                            .getSystem<LocalizationService>())
        {
            return svc->tr(key);
        }
    }
    return key;
}

std::string substituteTrArgs(std::string_view pattern, std::initializer_list<TrArg> args)
{
    std::string out;
    out.reserve(pattern.size());
    for (std::size_t i = 0; i < pattern.size(); ++i)
    {
        const char c = pattern[i];
        const bool doubled = i + 1 < pattern.size() && pattern[i + 1] == c;
        if ((c == '{' || c == '}') && doubled)
        {
            out.push_back(c);  // "{{" / "}}" write one brace
            ++i;
            continue;
        }
        if (c == '{')
        {
            const std::size_t close = pattern.find('}', i + 1);
            if (close != std::string_view::npos)
            {
                const std::string_view name = pattern.substr(i + 1, close - i - 1);
                const TrArg* match = nullptr;
                for (const TrArg& a : args)
                {
                    if (a.name == name)
                    {
                        match = &a;
                        break;
                    }
                }
                if (match != nullptr)
                {
                    out += match->value;
                    i = close;
                    continue;
                }
            }
        }
        out.push_back(c);
    }
    return out;
}

std::string trf(std::string_view key, std::initializer_list<TrArg> args)
{
    return substituteTrArgs(tr(key), args);
}

#ifdef VESTIGE_TEST_HOOKS
ScopedStringTableOverride::ScopedStringTableOverride(const StringTable& table)
    : m_previous(s_overrideTable)
{
    s_overrideTable = &table;
}

ScopedStringTableOverride::~ScopedStringTableOverride()
{
    s_overrideTable = m_previous;
}
#endif

} // namespace Vestige
