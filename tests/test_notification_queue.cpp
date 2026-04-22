// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_notification_queue.cpp
/// @brief Phase 10 slice 12.3 coverage — `NotificationQueue` FIFO
///        behaviour, fade envelope, reduced-motion snap, severity
///        colour mapping, and `UINotificationToast` accessibility
///        metadata.

#include <gtest/gtest.h>

#include "ui/ui_notification_toast.h"
#include "ui/ui_theme.h"

using namespace Vestige;

namespace
{
Notification makeNotice(const std::string& title,
                        float duration = 4.0f,
                        NotificationSeverity severity = NotificationSeverity::Info,
                        const std::string& body = "")
{
    Notification n;
    n.title = title;
    n.body = body;
    n.severity = severity;
    n.durationSeconds = duration;
    return n;
}
}

// -- Severity labels --

TEST(NotificationSeverity, LabelsAreStable)
{
    EXPECT_STREQ(notificationSeverityLabel(NotificationSeverity::Info),    "Info");
    EXPECT_STREQ(notificationSeverityLabel(NotificationSeverity::Success), "Success");
    EXPECT_STREQ(notificationSeverityLabel(NotificationSeverity::Warning), "Warning");
    EXPECT_STREQ(notificationSeverityLabel(NotificationSeverity::Error),   "Error");
}

TEST(NotificationSeverity, LabelsNeverReturnNull)
{
    // Defensive — switch coverage guarantees this but tests should pin it.
    EXPECT_NE(notificationSeverityLabel(NotificationSeverity::Info), nullptr);
    EXPECT_NE(notificationSeverityLabel(NotificationSeverity::Error), nullptr);
}

// -- Empty queue baseline --

TEST(NotificationQueue, DefaultsAreEmptyAndCapThree)
{
    NotificationQueue q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
    EXPECT_EQ(q.capacity(), NotificationQueue::DEFAULT_CAPACITY);
    EXPECT_EQ(NotificationQueue::DEFAULT_CAPACITY, 3u)
        << "Cap of 3 matches SubtitleQueue::DEFAULT_MAX_CONCURRENT — "
           "mental model consistency.";
}

TEST(NotificationQueue, AdvanceOnEmptyIsSafe)
{
    NotificationQueue q;
    q.advance(0.016f);
    q.advance(100.0f);
    EXPECT_TRUE(q.empty());
}

// -- FIFO basics --

TEST(NotificationQueue, PushEnqueuesAndReportsSize)
{
    NotificationQueue q;
    q.push(makeNotice("one"));
    q.push(makeNotice("two"));
    EXPECT_EQ(q.size(), 2u);
    EXPECT_FALSE(q.empty());
    ASSERT_EQ(q.active().size(), 2u);
    EXPECT_EQ(q.active()[0].data.title, "one");
    EXPECT_EQ(q.active()[1].data.title, "two");
}

TEST(NotificationQueue, PushAtCapacityEvictsOldest)
{
    NotificationQueue q;
    q.push(makeNotice("A"));
    q.push(makeNotice("B"));
    q.push(makeNotice("C"));
    q.push(makeNotice("D"));  // Evicts A.

    ASSERT_EQ(q.size(), 3u);
    EXPECT_EQ(q.active()[0].data.title, "B");
    EXPECT_EQ(q.active()[1].data.title, "C");
    EXPECT_EQ(q.active()[2].data.title, "D");
}

TEST(NotificationQueue, PushClampsNegativeDuration)
{
    NotificationQueue q;
    Notification n = makeNotice("bad", -5.0f);
    q.push(n);
    ASSERT_EQ(q.size(), 1u);
    EXPECT_FLOAT_EQ(q.active()[0].data.durationSeconds, 0.0f);
}

TEST(NotificationQueue, ClearDropsEverything)
{
    NotificationQueue q;
    q.push(makeNotice("one"));
    q.push(makeNotice("two"));
    q.clear();
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
}

// -- setCapacity --

TEST(NotificationQueue, SetCapacityTrimsOldestWhenShrinking)
{
    NotificationQueue q;
    q.push(makeNotice("A"));
    q.push(makeNotice("B"));
    q.push(makeNotice("C"));

    q.setCapacity(1);
    EXPECT_EQ(q.capacity(), 1u);
    ASSERT_EQ(q.size(), 1u);
    EXPECT_EQ(q.active()[0].data.title, "C")
        << "Oldest entries are dropped first; newest survives.";
}

TEST(NotificationQueue, SetCapacityZeroDrainsAndBlocksFurtherPush)
{
    NotificationQueue q;
    q.push(makeNotice("A"));
    q.push(makeNotice("B"));

    q.setCapacity(0);
    EXPECT_EQ(q.capacity(), 0u);
    EXPECT_TRUE(q.empty());

    q.push(makeNotice("C"));
    EXPECT_TRUE(q.empty()) << "Pushes against a 0-cap queue are no-ops.";
}

TEST(NotificationQueue, SetCapacityGrowingDoesNotEvict)
{
    NotificationQueue q;
    q.push(makeNotice("A"));
    q.push(makeNotice("B"));
    q.setCapacity(10);
    EXPECT_EQ(q.size(), 2u);
}

// -- advance / expiry --

TEST(NotificationQueue, AdvanceExpiresInFIFOOrder)
{
    NotificationQueue q;
    q.push(makeNotice("short", 1.0f));
    q.push(makeNotice("long",  3.0f));

    q.advance(1.1f, 0.0f);  // Snap mode — short expires.
    ASSERT_EQ(q.size(), 1u);
    EXPECT_EQ(q.active()[0].data.title, "long");

    q.advance(2.0f, 0.0f);  // Long expires.
    EXPECT_TRUE(q.empty());
}

TEST(NotificationQueue, AdvanceWithNegativeDtIsNoOp)
{
    NotificationQueue q;
    q.push(makeNotice("A", 1.0f));
    q.advance(-1.0f, 0.0f);
    ASSERT_EQ(q.size(), 1u);
    EXPECT_FLOAT_EQ(q.active()[0].elapsedSeconds, 0.0f);
}

// -- Fade envelope --

TEST(NotificationAlphaAt, ZeroFadeSnapsToOneAcrossDuration)
{
    EXPECT_FLOAT_EQ(notificationAlphaAt(0.0f,  4.0f, 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(notificationAlphaAt(1.5f,  4.0f, 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(notificationAlphaAt(3.99f, 4.0f, 0.0f), 1.0f);
}

TEST(NotificationAlphaAt, NegativeFadeIsTreatedAsZero)
{
    EXPECT_FLOAT_EQ(notificationAlphaAt(1.0f, 4.0f, -0.5f), 1.0f);
}

TEST(NotificationAlphaAt, RampsFromZeroToOneOverFadeIn)
{
    // fadeSeconds=0.2, duration=4.0 — fade-in spans [0, 0.2)
    EXPECT_NEAR(notificationAlphaAt(0.00f, 4.0f, 0.2f), 0.0f, 1e-5f);
    EXPECT_NEAR(notificationAlphaAt(0.10f, 4.0f, 0.2f), 0.5f, 1e-5f);
    EXPECT_NEAR(notificationAlphaAt(0.20f, 4.0f, 0.2f), 1.0f, 1e-5f);
}

TEST(NotificationAlphaAt, PlateauIsOne)
{
    EXPECT_FLOAT_EQ(notificationAlphaAt(1.0f, 4.0f, 0.2f), 1.0f);
    EXPECT_FLOAT_EQ(notificationAlphaAt(2.5f, 4.0f, 0.2f), 1.0f);
    EXPECT_FLOAT_EQ(notificationAlphaAt(3.5f, 4.0f, 0.2f), 1.0f);
}

TEST(NotificationAlphaAt, RampsFromOneToZeroOverFadeOut)
{
    // fade=0.2, duration=4.0 — fade-out spans [3.8, 4.0)
    EXPECT_NEAR(notificationAlphaAt(3.80f, 4.0f, 0.2f), 1.0f, 1e-5f);
    EXPECT_NEAR(notificationAlphaAt(3.90f, 4.0f, 0.2f), 0.5f, 1e-5f);
    EXPECT_NEAR(notificationAlphaAt(4.00f, 4.0f, 0.2f), 0.0f, 1e-5f);
}

TEST(NotificationAlphaAt, ShortDurationStillGetsInAndOutFade)
{
    // If 2*fade > duration, the envelope should still have a ramp both
    // ways — degenerates to a triangle peaked at the midpoint.
    EXPECT_NEAR(notificationAlphaAt(0.0f,  0.2f, 1.0f), 0.0f, 1e-5f);
    EXPECT_NEAR(notificationAlphaAt(0.1f,  0.2f, 1.0f), 1.0f, 1e-5f);
    EXPECT_NEAR(notificationAlphaAt(0.2f,  0.2f, 1.0f), 0.0f, 1e-5f);
}

TEST(NotificationAlphaAt, PastExpiryIsZero)
{
    EXPECT_FLOAT_EQ(notificationAlphaAt(10.0f, 4.0f, 0.2f), 0.0f);
    EXPECT_FLOAT_EQ(notificationAlphaAt(10.0f, 4.0f, 0.0f), 0.0f);
}

TEST(NotificationAlphaAt, ZeroDurationIsZeroAlpha)
{
    EXPECT_FLOAT_EQ(notificationAlphaAt(0.0f, 0.0f, 0.2f), 0.0f);
}

// -- advance writes alpha onto entries --

TEST(NotificationQueue, AdvanceWritesAlphaOnEntries)
{
    NotificationQueue q;
    q.push(makeNotice("A", 4.0f));

    // Spawn at 0 alpha — advance ramps it up over the fade duration.
    EXPECT_FLOAT_EQ(q.active()[0].alpha, 0.0f);

    q.advance(0.1f, 0.2f);
    EXPECT_NEAR(q.active()[0].alpha, 0.5f, 1e-5f);

    q.advance(0.1f, 0.2f);
    EXPECT_NEAR(q.active()[0].alpha, 1.0f, 1e-5f);
}

// -- Severity colour mapping --

TEST(NotificationSeverityColor, MapsToThemePalette)
{
    const UITheme theme = UITheme::defaultTheme();

    const glm::vec4 info    = notificationSeverityColor(NotificationSeverity::Info,    theme);
    const glm::vec4 success = notificationSeverityColor(NotificationSeverity::Success, theme);
    const glm::vec4 warning = notificationSeverityColor(NotificationSeverity::Warning, theme);
    const glm::vec4 error   = notificationSeverityColor(NotificationSeverity::Error,   theme);

    EXPECT_EQ(glm::vec3(info),    theme.textSecondary);
    EXPECT_EQ(success,            theme.accent);
    EXPECT_EQ(glm::vec3(warning), theme.textWarning);
    EXPECT_EQ(glm::vec3(error),   theme.textError);
    EXPECT_FLOAT_EQ(info.a,    1.0f);
    EXPECT_FLOAT_EQ(warning.a, 1.0f);
    EXPECT_FLOAT_EQ(error.a,   1.0f);
}

TEST(NotificationSeverityColor, SurvivesHighContrastPaletteSwap)
{
    const UITheme hc = UITheme::defaultTheme().withHighContrast();
    const glm::vec4 hcError = notificationSeverityColor(NotificationSeverity::Error, hc);
    // High-contrast palette must still yield a visible, non-default-palette
    // colour — the widget should never rely on the default theme's literal
    // RGB values.
    EXPECT_EQ(glm::vec3(hcError), hc.textError);
}

// -- UINotificationToast widget --

TEST(UINotificationToast, SetsLabelRoleAndIsNotInteractive)
{
    UINotificationToast toast;
    EXPECT_FALSE(toast.interactive);
    EXPECT_EQ(toast.accessible().role, UIAccessibleRole::Label);
}

TEST(UINotificationToast, UpdateCopiesTitleAndBodyIntoAccessibility)
{
    UINotificationToast toast;
    const UITheme theme = UITheme::defaultTheme();

    ActiveNotification entry;
    entry.data.title = "Saved";
    entry.data.body = "Autosave complete.";
    entry.data.severity = NotificationSeverity::Success;
    entry.data.durationSeconds = 4.0f;
    entry.alpha = 1.0f;

    toast.updateFromNotification(entry, theme, nullptr);

    EXPECT_EQ(toast.accessible().label, "Saved");
    EXPECT_EQ(toast.accessible().description, "Autosave complete.");
    EXPECT_EQ(toast.accentColor, theme.accent);
    EXPECT_FLOAT_EQ(toast.alpha, 1.0f);
}

TEST(UINotificationToast, UpdateBuildsTitleAndOptionalBodyChildren)
{
    UINotificationToast toast;
    const UITheme theme = UITheme::defaultTheme();

    ActiveNotification titleOnly;
    titleOnly.data.title = "Objective Updated";
    titleOnly.alpha = 1.0f;
    titleOnly.data.durationSeconds = 4.0f;

    toast.updateFromNotification(titleOnly, theme, nullptr);
    EXPECT_EQ(toast.getChildCount(), 1u) << "title only → 1 child label";

    ActiveNotification withBody;
    withBody.data.title = "Saved";
    withBody.data.body  = "Autosave complete.";
    withBody.alpha = 1.0f;
    withBody.data.durationSeconds = 4.0f;

    toast.updateFromNotification(withBody, theme, nullptr);
    EXPECT_EQ(toast.getChildCount(), 2u) << "title + body → 2 child labels";
}

TEST(UINotificationToast, UpdateClampsAlphaToUnitInterval)
{
    UINotificationToast toast;
    const UITheme theme = UITheme::defaultTheme();

    ActiveNotification entry;
    entry.data.title = "X";
    entry.data.durationSeconds = 4.0f;
    entry.alpha = 2.0f;
    toast.updateFromNotification(entry, theme, nullptr);
    EXPECT_FLOAT_EQ(toast.alpha, 1.0f);

    entry.alpha = -0.5f;
    toast.updateFromNotification(entry, theme, nullptr);
    EXPECT_FLOAT_EQ(toast.alpha, 0.0f);
}

TEST(UINotificationToast, AlphaOnlyChangeDoesNotRebuildChildren)
{
    // Per-frame path: alpha moves every tick but the text is stable, so
    // the widget must not churn its child labels.
    UINotificationToast toast;
    const UITheme theme = UITheme::defaultTheme();

    ActiveNotification entry;
    entry.data.title = "Hello";
    entry.data.body  = "World";
    entry.data.durationSeconds = 4.0f;
    entry.alpha = 0.2f;
    toast.updateFromNotification(entry, theme, nullptr);
    const size_t initial = toast.getChildCount();

    entry.alpha = 0.8f;
    toast.updateFromNotification(entry, theme, nullptr);
    EXPECT_EQ(toast.getChildCount(), initial);
}
