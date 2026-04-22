// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_notification_toast.h
/// @brief Phase 10 slice 12.3 — transient "toast" notifications stacked
///        at the top-right of the HUD (objective updated, item collected,
///        save completed, autosave notice, etc.).
///
/// Split into two layers:
///
///   - `NotificationQueue` — a headless FIFO with per-tick countdown and
///     a fade-in / fade-out envelope. Pure function semantics: no GL,
///     no allocations outside its `std::vector`, unit-testable without
///     the engine.
///   - `UINotificationToast` — a `UIElement` subclass that renders one
///     `ActiveNotification` as a panel + title label + optional body.
///     Positioned by the HUD prefab (slice 12.4); this widget itself is
///     layout-agnostic.
///
/// The envelope is `fade-in → full → fade-out`, using
/// `UITheme::transitionDuration` as the fade duration. Reduced-motion
/// zeroes `transitionDuration`, so toasts snap in / out instantly; the
/// queue math handles that without a separate branch (both fades become
/// 0 s long).
///
/// The cap is three concurrent toasts — matches `SubtitleQueue::DEFAULT_MAX_CONCURRENT`
/// so a user who has tuned one cadence doesn't get surprised by the other.
#pragma once

#include "ui/ui_element.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace Vestige
{

class TextRenderer;
struct UITheme;

/// @brief Severity / intent tag for a notification. Colours the accent
///        strip; the queue itself treats all severities identically.
enum class NotificationSeverity
{
    Info,      ///< Default. Neutral palette — `theme.textSecondary` accent.
    Success,   ///< `theme.accent` (burnished brass) — "saved", "collected".
    Warning,   ///< `theme.textWarning` (amber) — "low health", "autosave pending".
    Error,     ///< `theme.textError` (red) — "save failed", "connection lost".
};

/// @brief Returns a stable, human-readable label for a severity — used by
///        the editor panel readout and by tests.
const char* notificationSeverityLabel(NotificationSeverity severity);

/// @brief Authored notification passed to `NotificationQueue::push`.
struct Notification
{
    std::string title;                                ///< Bold, one line.
    std::string body;                                 ///< Optional, up to two lines.
    NotificationSeverity severity = NotificationSeverity::Info;
    float durationSeconds = 4.0f;                     ///< Including both fades.
};

/// @brief One queue entry with a countdown. Produced by the queue;
///        consumers read `data`, `elapsedSeconds`, and `alpha` to render.
struct ActiveNotification
{
    Notification data;
    float elapsedSeconds = 0.0f;  ///< 0 at spawn, grows until duration.
    float alpha          = 0.0f;  ///< 0..1 envelope — set by `advance()`.
};

/// @brief FIFO queue of transient notifications with per-tick countdown
///        and a fade-in / fade-out envelope.
///
/// Usage pattern: game events call `push(...)`; the UI system calls
/// `advance(dt)` once per frame; the toast widgets read `active()` and
/// render the list.
///
/// Capacity is capped at `DEFAULT_CAPACITY` (3) by default. `push`
/// evicts the oldest entry when full (push-newest / drop-oldest), so a
/// flurry of events replaces stale lines with fresh ones rather than
/// queuing up a long tail.
class NotificationQueue
{
public:
    /// Default concurrent cap. Three matches `SubtitleQueue`'s default
    /// — users tune both with one mental model.
    static constexpr std::size_t DEFAULT_CAPACITY = 3;

    /// Fade-in / fade-out duration in seconds. Used as a fallback when
    /// `advance()` is called without a `transitionSeconds` argument
    /// (i.e. headless tests that don't have a theme). Matches the
    /// default `UITheme::transitionDuration` (0.14 s).
    static constexpr float DEFAULT_FADE_SECONDS = 0.14f;

    NotificationQueue() = default;

    /// @brief Enqueues a notification. Evicts the oldest entry when the
    ///        queue is at capacity, matching the `SubtitleQueue`
    ///        "push newest, drop oldest" policy.
    void push(const Notification& notification);

    /// @brief Advances every active entry's countdown by @a deltaSeconds
    ///        and recomputes its `alpha` envelope using the supplied
    ///        fade duration. Removes entries whose `elapsedSeconds`
    ///        meets or exceeds `durationSeconds`.
    ///
    /// `fadeSeconds` is read from `UITheme::transitionDuration` by the
    /// `UISystem`; reduced-motion is a value of 0 and collapses the
    /// envelope to a rectangle (alpha jumps to 1 immediately, stays
    /// until expiry). Negative `deltaSeconds` is treated as 0.
    void advance(float deltaSeconds, float fadeSeconds = DEFAULT_FADE_SECONDS);

    /// @brief Returns the current set of active notifications, oldest
    ///        first. The UI renderer iterates this list.
    const std::vector<ActiveNotification>& active() const { return m_active; }

    /// @brief Number of active notifications.
    std::size_t size() const { return m_active.size(); }

    /// @brief True when nothing is active.
    bool empty() const { return m_active.empty(); }

    /// @brief Drops every active notification.
    void clear() { m_active.clear(); }

    /// @brief Current concurrent cap.
    std::size_t capacity() const { return m_capacity; }

    /// @brief Sets the concurrent cap. If the new cap is smaller than
    ///        the current active count, the oldest entries are evicted
    ///        immediately to fit. Passing 0 leaves the queue empty and
    ///        drops all subsequent `push` calls.
    void setCapacity(std::size_t capacity);

private:
    std::vector<ActiveNotification> m_active;
    std::size_t m_capacity = DEFAULT_CAPACITY;
};

/// @brief Computes the fade envelope alpha for a notification whose
///        countdown is `elapsedSeconds` of `durationSeconds` total,
///        using `fadeSeconds` for both the fade-in and fade-out ramps.
///
/// Pure function, exposed for testing. Clamps to [0, 1]. When
/// `fadeSeconds <= 0`, returns 1.0 for the full duration (reduced-motion
/// / accessibility snap).
float notificationAlphaAt(float elapsedSeconds,
                          float durationSeconds,
                          float fadeSeconds);

/// @brief Returns the theme colour a severity maps to, for the toast's
///        accent strip and the queue-readout row in the editor panel.
glm::vec4 notificationSeverityColor(NotificationSeverity severity,
                                    const UITheme& theme);

/// @brief Widget that renders a single `ActiveNotification` as a panel
///        with a severity-coloured accent strip + title + optional body.
///
/// Layout responsibility stays with the caller — the widget anchors
/// itself wherever the HUD prefab places it (top-right, stacked).
/// Width / height are pre-sized by the theme; callers can override
/// `size` before adding children.
class UINotificationToast : public UIElement
{
public:
    UINotificationToast();

    void render(SpriteBatchRenderer& batch, const glm::vec2& parentOffset,
                int screenWidth, int screenHeight) override;

    /// @brief Updates the widget from an `ActiveNotification`. Rebuilds
    ///        cached label text if the notification's text changed, but
    ///        leaves existing children in place when only `alpha` moved
    ///        (per-frame path). Safe to call every frame.
    void updateFromNotification(const ActiveNotification& notification,
                                const UITheme& theme,
                                TextRenderer* textRenderer);

    /// @brief The severity colour used for the left-edge accent strip.
    glm::vec4 accentColor = {1.0f, 1.0f, 1.0f, 1.0f};

    /// @brief The width of the accent strip, in pixels. Matches
    ///        `UITheme::buttonAccentTickWidth` by default.
    float accentWidth = 4.0f;

    /// @brief The background colour, read from `UITheme::panelBg`.
    glm::vec4 backgroundColor = {0.110f, 0.098f, 0.086f, 0.92f};

    /// @brief Envelope alpha in [0, 1] — multiplies every drawn colour.
    ///        Set by `updateFromNotification` from the queue's
    ///        `ActiveNotification::alpha`.
    float alpha = 1.0f;

private:
    std::string m_title;
    std::string m_body;
};

} // namespace Vestige
