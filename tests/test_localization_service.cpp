// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_localization_service.cpp
/// @brief Tests for LocalizationService (Phase 10 Localization L4).
///        See docs/phases/phase_10_localization_design.md § 5.6
///        and § 8 tests 16-17.
#include <gtest/gtest.h>

#include "core/engine.h"
#include "core/system_events.h"
#include "localization/localization_service.h"

#include <string>

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
