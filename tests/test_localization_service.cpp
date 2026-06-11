// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_localization_service.cpp
/// @brief Tests for LocalizationService (Phase 10 Localization L4).
///        See docs/phases/phase_10_localization_design.md § 5.6
///        and § 8 tests 16-17.
#include <gtest/gtest.h>

#include "core/engine.h"
#include "core/settings.h"
#include "core/settings_apply.h"
#include "core/settings_editor.h"
#include "core/system_events.h"
#include "localization/localization_service.h"

#include <string>
#include <vector>

using namespace Vestige;

#ifndef VESTIGE_LOCALIZATION_DIR
#error "VESTIGE_LOCALIZATION_DIR must be defined by the test build (source-tree seed files)."
#endif

namespace
{
// Default-constructed Engine: the EventBus is usable; other subsystems are
// not (same harness as test_scripting_system_bridge.cpp).
LocalizationService makeService(Engine& engine)
{
    LocalizationService svc;
    svc.setLocalizationDir(VESTIGE_LOCALIZATION_DIR);
    EXPECT_TRUE(svc.initialize(engine));
    return svc;
}
} // namespace

// Test 16 — active "he" lacks "ui.menu.quit"; tr() falls back to the English
// value, not the key. A key present in "he" resolves to the Hebrew value.
TEST(LocalizationService, FallbackToEnglish)
{
    Engine engine;
    LocalizationService svc = makeService(engine);

    ASSERT_TRUE(svc.setLanguage("he"));
    EXPECT_EQ(svc.languageCode(), "he");

    // "ui.menu.quit" is absent from he.json → English fallback.
    EXPECT_EQ(svc.tr("ui.menu.quit"), "Quit");
    // "ui.menu.settings" is present in he.json → Hebrew value.
    EXPECT_EQ(svc.tr("ui.menu.settings"), "הגדרות");
    // Unknown key → last-resort passthrough (active → English → key).
    EXPECT_EQ(svc.tr("does.not.exist"), "does.not.exist");
}

// Test 17 — setLanguage publishes a LanguageChangedEvent exactly once.
TEST(LocalizationService, LanguageChangedEvent)
{
    Engine engine;
    LocalizationService svc = makeService(engine);

    int count = 0;
    std::string lastCode;
    engine.getEventBus().subscribe<LanguageChangedEvent>(
        [&](const LanguageChangedEvent& e)
        {
            ++count;
            lastCode = e.languageCode;
        });

    ASSERT_TRUE(svc.setLanguage("he"));
    EXPECT_EQ(count, 1);
    EXPECT_EQ(lastCode, "he");
}

// A failed switch (missing file) keeps the old language and emits no event.
TEST(LocalizationService, FailedSwitchKeepsLanguageAndIsSilent)
{
    Engine engine;
    LocalizationService svc = makeService(engine);

    int count = 0;
    engine.getEventBus().subscribe<LanguageChangedEvent>(
        [&](const LanguageChangedEvent&) { ++count; });

    EXPECT_FALSE(svc.setLanguage("zz")); // no zz.json
    EXPECT_EQ(svc.languageCode(), "en"); // default retained
    EXPECT_EQ(count, 0);
}

// Test 20 — live-apply: mutating the language setting through the editor
// hot-swaps the active table, so the next tr() (a panel rebuild) reads the
// new language. Models "user picks 'he' in the menu → next rebuild reads
// Hebrew" without standing up the ImGui panel.
TEST(LocalizationService, LiveApplyHotSwapsTable)
{
    Engine engine;
    LocalizationService svc = makeService(engine);

    LocalizationServiceApplySink sink(svc);
    SettingsEditor::ApplyTargets targets{};
    targets.localization = &sink;
    SettingsEditor editor(Settings{}, targets);

    // Pre-switch: a UI label built via tr() reads English.
    EXPECT_EQ(svc.tr("ui.menu.settings"), "Settings");

    editor.mutate([](Settings& s) { s.localization.language = "he"; });

    EXPECT_EQ(svc.languageCode(), "he");
    // The next panel rebuild re-fetches and now reads Hebrew.
    EXPECT_EQ(svc.tr("ui.menu.settings"), "הגדרות");
}

// Test 22 — the editor "missing keys" overlay worklist: en.json carries 5
// keys, he.json 3, so two keys (templates, quit) render the English fallback
// and must surface as missing. Sorted output.
TEST(LocalizationService, MissingKeysReport)
{
    Engine engine;
    LocalizationService svc = makeService(engine);

    // Reference is "en"; active "en" → nothing missing.
    EXPECT_TRUE(svc.missingKeys().empty());

    ASSERT_TRUE(svc.setLanguage("he"));
    const std::vector<std::string> missing = svc.missingKeys();
    EXPECT_EQ(missing,
              (std::vector<std::string>{"ui.menu.quit", "ui.menu.templates"}));
}

// The editor re-pushes every sink on every mutation; the localization sink
// must no-op when the code is unchanged, or an unrelated edit would reload
// the table and republish LanguageChangedEvent.
TEST(LocalizationService, ApplySinkNoOpsWhenLanguageUnchanged)
{
    Engine engine;
    LocalizationService svc = makeService(engine);
    ASSERT_TRUE(svc.setLanguage("he"));

    int events = 0;
    engine.getEventBus().subscribe<LanguageChangedEvent>(
        [&](const LanguageChangedEvent&) { ++events; });

    LocalizationServiceApplySink sink(svc);
    SettingsEditor::ApplyTargets targets{};
    targets.localization = &sink;
    Settings start;
    start.localization.language = "he";
    SettingsEditor editor(std::move(start), targets);

    // Mutate an unrelated field; language stays "he".
    editor.mutate([](Settings& s) { s.audio.busGains[0] = 0.5f; });

    EXPECT_EQ(svc.languageCode(), "he");
    EXPECT_EQ(events, 0); // no republish for an unrelated edit
}
