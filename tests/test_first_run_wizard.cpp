// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_first_run_wizard.cpp
/// @brief Phase 10.5 slice 14.2 — first-run wizard state-machine tests.
///
/// Every test exercises the pure `applyFirstRunIntent` function —
/// no ImGui context is constructed, no Scene is built. The draw()
/// method is covered indirectly (it is a thin wrapper that forwards
/// user clicks as intents), but slice 14.4 is where the
/// Engine-level integration is exercised.
///
/// Fixed ISO timestamp so tests don't depend on wall-clock; matches
/// the design doc §5 timestamp note.

#include "editor/panels/first_run_wizard.h"
#include "core/settings.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace Vestige;

namespace
{

constexpr const char* kFixedIso = "2026-04-22T10:00:00Z";

} // namespace

TEST(FirstRunWizard, WelcomeToPickerTransition)
{
    OnboardingSettings pre;  // defaults: not completed, 0 skips
    auto t = applyFirstRunIntent(FirstRunWizardStep::Welcome, pre,
                                  FirstRunIntent::PickTemplate, kFixedIso);

    EXPECT_EQ(t.step, FirstRunWizardStep::TemplatePicker);
    EXPECT_EQ(t.sceneOp, FirstRunWizardSceneOp::None);
    EXPECT_FALSE(t.closed);
    // Advancing to the picker must NOT complete the wizard.
    EXPECT_FALSE(t.onboarding.hasCompletedFirstRun);
    EXPECT_EQ(t.onboarding.skipCount, 0);
}

// Slice 18 Ts4: the three "intent X marks complete + emits sceneOp Y"
// tests (StartEmpty/ShowDemo/FinishWithTemplate) collapsed into one
// table-driven test. All three exercise the same wizard-state-machine
// "terminal intent transitions to Done + sets completedAt" contract.
TEST(FirstRunWizard, TerminalIntentsMarkCompleteAndEmitMatchingSceneOp)
{
    struct Case {
        FirstRunWizardStep   startStep;
        FirstRunIntent       intent;
        FirstRunWizardSceneOp expectedOp;
        const char* name;
    };
    const Case cases[] = {
        { FirstRunWizardStep::Welcome,        FirstRunIntent::StartEmpty,
          FirstRunWizardSceneOp::ApplyEmpty,    "StartEmpty"           },
        { FirstRunWizardStep::Welcome,        FirstRunIntent::ShowDemo,
          FirstRunWizardSceneOp::ApplyDemo,     "ShowDemo"             },
        { FirstRunWizardStep::TemplatePicker, FirstRunIntent::FinishWithTemplate,
          FirstRunWizardSceneOp::ApplyTemplate, "FinishWithTemplate"   },
    };
    for (const Case& c : cases)
    {
        OnboardingSettings pre;
        auto t = applyFirstRunIntent(c.startStep, pre, c.intent, kFixedIso);
        EXPECT_EQ(t.step, FirstRunWizardStep::Done) << c.name;
        EXPECT_EQ(t.sceneOp, c.expectedOp) << c.name;
        EXPECT_TRUE(t.closed) << c.name;
        EXPECT_TRUE(t.onboarding.hasCompletedFirstRun) << c.name;
        EXPECT_EQ(t.onboarding.completedAt, kFixedIso) << c.name;
    }
}

TEST(FirstRunWizard, BackFromPickerReturnsToWelcomeWithoutBumpingSkip)
{
    OnboardingSettings pre;
    auto t = applyFirstRunIntent(FirstRunWizardStep::TemplatePicker, pre,
                                  FirstRunIntent::Back, kFixedIso);

    EXPECT_EQ(t.step, FirstRunWizardStep::Welcome);
    EXPECT_EQ(t.sceneOp, FirstRunWizardSceneOp::None);
    EXPECT_FALSE(t.closed);
    // Back is not a skip — must not pollute the skipCount threshold.
    EXPECT_EQ(t.onboarding.skipCount, 0);
    EXPECT_FALSE(t.onboarding.hasCompletedFirstRun);
}

// Slice 18 Ts4: `FinishWithTemplateMarksCompleteAndEmitsApplyTemplate`
// rolled into `TerminalIntentsMarkCompleteAndEmitMatchingSceneOp`
// above.

TEST(FirstRunWizard, FirstSkipIncrementsCountButDoesNotComplete)
{
    OnboardingSettings pre;
    auto t = applyFirstRunIntent(FirstRunWizardStep::Welcome, pre,
                                  FirstRunIntent::SkipForNow, kFixedIso);

    // Closes for this session but the wizard will re-open next launch.
    EXPECT_TRUE(t.closed);
    EXPECT_EQ(t.onboarding.skipCount, 1);
    EXPECT_FALSE(t.onboarding.hasCompletedFirstRun);
    EXPECT_TRUE(t.onboarding.completedAt.empty());
}

TEST(FirstRunWizard, SecondSkipHitsThresholdAndCompletesSilently)
{
    // Simulate two consecutive sessions where the user hit Skip.
    OnboardingSettings pre;
    auto t1 = applyFirstRunIntent(FirstRunWizardStep::Welcome, pre,
                                   FirstRunIntent::SkipForNow, kFixedIso);
    ASSERT_EQ(t1.onboarding.skipCount, 1);
    ASSERT_FALSE(t1.onboarding.hasCompletedFirstRun);

    auto t2 = applyFirstRunIntent(FirstRunWizardStep::Welcome, t1.onboarding,
                                   FirstRunIntent::SkipForNow, kFixedIso);

    EXPECT_EQ(t2.step, FirstRunWizardStep::Done);
    EXPECT_EQ(t2.onboarding.skipCount, 2);
    EXPECT_TRUE(t2.onboarding.hasCompletedFirstRun);
    EXPECT_EQ(t2.onboarding.completedAt, kFixedIso);
    EXPECT_TRUE(t2.closed);
}

TEST(FirstRunWizard, CloseAtWelcomeRoutesLikeSkip_CloseAtPickerRoutesLikeBack)
{
    // Close-at-welcome should be indistinguishable from SkipForNow.
    OnboardingSettings pre;
    auto tw = applyFirstRunIntent(FirstRunWizardStep::Welcome, pre,
                                   FirstRunIntent::CloseAtWelcome, kFixedIso);
    EXPECT_EQ(tw.onboarding.skipCount, 1);
    EXPECT_TRUE(tw.closed);
    EXPECT_FALSE(tw.onboarding.hasCompletedFirstRun);

    // Close-at-picker should return the user to Welcome without
    // bumping skipCount and without closing the wizard.
    auto tp = applyFirstRunIntent(FirstRunWizardStep::TemplatePicker, pre,
                                   FirstRunIntent::CloseAtPicker, kFixedIso);
    EXPECT_EQ(tp.step, FirstRunWizardStep::Welcome);
    EXPECT_EQ(tp.onboarding.skipCount, 0);
    EXPECT_FALSE(tp.closed);
}

// ===== Template filter (Q1 — 4 featured + 4 under expander) ================

TEST(FirstRunWizardTemplates, FeaturedFourAreTheArchetypeCoverageSet)
{
    const auto featured = featuredTemplates();
    ASSERT_EQ(featured.size(), 4u);
    EXPECT_EQ(featured[0].type, GameTemplateType::FIRST_PERSON_3D);
    EXPECT_EQ(featured[1].type, GameTemplateType::THIRD_PERSON_3D);
    EXPECT_EQ(featured[2].type, GameTemplateType::TWO_POINT_FIVE_D);
    EXPECT_EQ(featured[3].type, GameTemplateType::ISOMETRIC);
}

TEST(FirstRunWizardTemplates, MoreFourContainsRemainingArchetypes)
{
    const auto more = moreTemplates();
    ASSERT_EQ(more.size(), 4u);
    // Ordering matters for UI stability but not for coverage — pin by set.
    bool hasTopDown      = false;
    bool hasPointAndClick = false;
    bool hasSide2D        = false;
    bool hasShmup         = false;
    for (const auto& cfg : more)
    {
        if (cfg.type == GameTemplateType::TOP_DOWN)          hasTopDown = true;
        if (cfg.type == GameTemplateType::POINT_AND_CLICK)   hasPointAndClick = true;
        if (cfg.type == GameTemplateType::SIDE_SCROLLER_2D)  hasSide2D = true;
        if (cfg.type == GameTemplateType::SHMUP_2D)          hasShmup = true;
    }
    EXPECT_TRUE(hasTopDown);
    EXPECT_TRUE(hasPointAndClick);
    EXPECT_TRUE(hasSide2D);
    EXPECT_TRUE(hasShmup);
}

TEST(FirstRunWizardTemplates, AllTemplatesEqualsFeaturedFollowedByMore)
{
    const auto featured = featuredTemplates();
    const auto more     = moreTemplates();
    const auto all      = allWizardTemplates();

    ASSERT_EQ(all.size(), featured.size() + more.size());
    for (std::size_t i = 0; i < featured.size(); ++i)
    {
        EXPECT_EQ(all[i].type, featured[i].type);
    }
    for (std::size_t i = 0; i < more.size(); ++i)
    {
        EXPECT_EQ(all[featured.size() + i].type, more[i].type);
    }
}

// ===== Slice 14.3 — requiredAssets availability filter (Q4) =================

namespace
{

/// RAII scratch dir for the filter tests. Mirrors the TmpDir in
/// test_settings.cpp but deliberately independent so this test
/// file stays self-contained.
class FilterTmpDir
{
public:
    FilterTmpDir()
        : m_root(fs::temp_directory_path()
                 / ("vestige_wizard_filter_" + Testing::vestigeTestStamp()))
    {
        std::error_code ec;
        fs::remove_all(m_root, ec);
        fs::create_directories(m_root, ec);
    }
    ~FilterTmpDir()
    {
        std::error_code ec;
        fs::remove_all(m_root, ec);
    }
    const fs::path& root() const { return m_root; }

    void touch(const std::string& relPath)
    {
        fs::path full = m_root / relPath;
        std::error_code ec;
        fs::create_directories(full.parent_path(), ec);
        std::ofstream f(full);
        f << "stub";
    }

private:
    fs::path m_root;
};

GameTemplateConfig makeDummyTemplate(
    GameTemplateType type,
    std::vector<std::string> required = {})
{
    GameTemplateConfig c;
    c.type           = type;
    c.displayName    = "test";
    c.requiredAssets = std::move(required);
    return c;
}

} // namespace

TEST(FirstRunWizardFilter, EmptyRequiredAssetsAlwaysVisible)
{
    FilterTmpDir tmp;
    std::vector<GameTemplateConfig> input = {
        makeDummyTemplate(GameTemplateType::FIRST_PERSON_3D, {}),
        makeDummyTemplate(GameTemplateType::ISOMETRIC,       {}),
    };

    auto filtered = filterByAvailability(input, tmp.root());
    EXPECT_EQ(filtered.size(), 2u);
}

TEST(FirstRunWizardFilter, MissingAssetHidesTemplate)
{
    FilterTmpDir tmp;
    // No file touched — the required path doesn't exist.
    std::vector<GameTemplateConfig> input = {
        makeDummyTemplate(GameTemplateType::FIRST_PERSON_3D,
                          {"textures/biblical/goegap.hdr"}),
    };

    auto filtered = filterByAvailability(input, tmp.root());
    EXPECT_TRUE(filtered.empty());
}

TEST(FirstRunWizardFilter, PresentAssetShowsTemplate)
{
    FilterTmpDir tmp;
    tmp.touch("textures/biblical/goegap.hdr");
    tmp.touch("textures/biblical/linen_diff.jpg");

    std::vector<GameTemplateConfig> input = {
        makeDummyTemplate(GameTemplateType::FIRST_PERSON_3D,
                          {"textures/biblical/goegap.hdr",
                           "textures/biblical/linen_diff.jpg"}),
    };

    auto filtered = filterByAvailability(input, tmp.root());
    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].type, GameTemplateType::FIRST_PERSON_3D);
}

TEST(FirstRunWizardFilter, NonWizardMenuListsAllUnconditionally)
{
    // The File → New from Template… path is served by
    // TemplateDialog::getTemplates() which does NOT run the filter.
    // Confirming the non-wizard menu is unfiltered means the full
    // 8-template set always surfaces there regardless of asset
    // availability. This is the design contract in §6.
    const auto full = TemplateDialog::getTemplates();
    EXPECT_EQ(full.size(), 8u);
}

// ===== Slice 14.4 — Engine wiring behaviour (headless) ======================

#include "editor/panels/welcome_panel.h"

TEST(FirstRunWizardWiring, AutoOpensWhenOnboardingIncomplete)
{
    // Fresh user: defaults say !hasCompletedFirstRun → wizard should
    // be open immediately after initialize().
    OnboardingSettings ob;  // defaults
    ASSERT_FALSE(ob.hasCompletedFirstRun);

    FirstRunWizard w;
    w.initialize(&ob);
    EXPECT_TRUE(w.isOpen());
    EXPECT_EQ(w.currentStep(), FirstRunWizardStep::Welcome);
}

TEST(FirstRunWizardWiring, DoesNotAutoOpenAfterCompletion)
{
    OnboardingSettings ob;
    ob.hasCompletedFirstRun = true;
    ob.completedAt = "2026-04-22T10:00:00Z";

    FirstRunWizard w;
    w.initialize(&ob);
    EXPECT_FALSE(w.isOpen());
}

TEST(FirstRunWizardWiring, OpenFromHelpMenuReopensAfterCompletion)
{
    OnboardingSettings ob;
    ob.hasCompletedFirstRun = true;

    FirstRunWizard w;
    w.initialize(&ob);
    ASSERT_FALSE(w.isOpen());

    w.openFromHelpMenu();
    EXPECT_TRUE(w.isOpen());
    // Help-menu re-open should always start at Welcome regardless
    // of where the user left off previously.
    EXPECT_EQ(w.currentStep(), FirstRunWizardStep::Welcome);
}

TEST(WelcomePanelSlice14_4, NoLongerAutoOpensOnFirstLaunch)
{
    // Slice 14.4 strips WelcomePanel's auto-open behaviour — first-run
    // onboarding is now owned by FirstRunWizard, and WelcomePanel
    // is a Help-menu-invoked keyboard-shortcut reference. Construct
    // a panel with a config dir that does NOT contain welcome_shown
    // and confirm isOpen() stays false after initialize().
    FilterTmpDir tmp;  // reuses the RAII scratch dir from 14.3 tests
    ASSERT_FALSE(fs::exists(tmp.root() / "welcome_shown"));

    WelcomePanel p;
    p.initialize(tmp.root().string());
    EXPECT_FALSE(p.isOpen())
        << "WelcomePanel should no longer auto-open on first launch "
           "(the first-run wizard owns that duty now).";

    // Explicit open via Help menu still works.
    p.open();
    EXPECT_TRUE(p.isOpen());
}
