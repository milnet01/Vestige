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

#include <gtest/gtest.h>

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

TEST(FirstRunWizard, StartEmptyMarksCompleteAndEmitsApplyEmpty)
{
    OnboardingSettings pre;
    auto t = applyFirstRunIntent(FirstRunWizardStep::Welcome, pre,
                                  FirstRunIntent::StartEmpty, kFixedIso);

    EXPECT_EQ(t.step, FirstRunWizardStep::Done);
    EXPECT_EQ(t.sceneOp, FirstRunWizardSceneOp::ApplyEmpty);
    EXPECT_TRUE(t.closed);
    EXPECT_TRUE(t.onboarding.hasCompletedFirstRun);
    EXPECT_EQ(t.onboarding.completedAt, kFixedIso);
}

TEST(FirstRunWizard, ShowDemoMarksCompleteAndEmitsApplyDemo)
{
    OnboardingSettings pre;
    auto t = applyFirstRunIntent(FirstRunWizardStep::Welcome, pre,
                                  FirstRunIntent::ShowDemo, kFixedIso);

    EXPECT_EQ(t.step, FirstRunWizardStep::Done);
    EXPECT_EQ(t.sceneOp, FirstRunWizardSceneOp::ApplyDemo);
    EXPECT_TRUE(t.closed);
    EXPECT_TRUE(t.onboarding.hasCompletedFirstRun);
    EXPECT_EQ(t.onboarding.completedAt, kFixedIso);
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

TEST(FirstRunWizard, FinishWithTemplateMarksCompleteAndEmitsApplyTemplate)
{
    OnboardingSettings pre;
    auto t = applyFirstRunIntent(FirstRunWizardStep::TemplatePicker, pre,
                                  FirstRunIntent::FinishWithTemplate, kFixedIso);

    EXPECT_EQ(t.step, FirstRunWizardStep::Done);
    EXPECT_EQ(t.sceneOp, FirstRunWizardSceneOp::ApplyTemplate);
    EXPECT_TRUE(t.closed);
    EXPECT_TRUE(t.onboarding.hasCompletedFirstRun);
    EXPECT_EQ(t.onboarding.completedAt, kFixedIso);
}

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
