// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file ui_notification_toast.cpp
/// @brief `NotificationQueue` and `UINotificationToast` implementation.

#include "ui/ui_notification_toast.h"

#include "renderer/text_renderer.h"
#include "ui/sprite_batch_renderer.h"
#include "ui/ui_label.h"
#include "ui/ui_theme.h"

#include <algorithm>

namespace Vestige
{

const char* notificationSeverityLabel(NotificationSeverity severity)
{
    switch (severity)
    {
        case NotificationSeverity::Info:    return "Info";
        case NotificationSeverity::Success: return "Success";
        case NotificationSeverity::Warning: return "Warning";
        case NotificationSeverity::Error:   return "Error";
    }
    return "Info";
}

float notificationAlphaAt(float elapsedSeconds,
                          float durationSeconds,
                          float fadeSeconds)
{
    // Expired → fully transparent. Keep rendering cheap; the queue will
    // drop the entry on its next `advance` call.
    if (elapsedSeconds >= durationSeconds || durationSeconds <= 0.0f)
    {
        return 0.0f;
    }

    // Reduced-motion collapses the envelope: instant in, instant out,
    // full alpha for the entire active window.
    if (fadeSeconds <= 0.0f)
    {
        return 1.0f;
    }

    // The envelope is "fade-in, plateau, fade-out". If two fades don't
    // fit in the total duration, split the duration in half so the toast
    // still has a meaningful in-and-out (prevents a negative plateau).
    float fade = fadeSeconds;
    if (2.0f * fade > durationSeconds)
    {
        fade = 0.5f * durationSeconds;
    }

    const float fadeOutStart = durationSeconds - fade;

    if (elapsedSeconds < fade)
    {
        return std::clamp(elapsedSeconds / fade, 0.0f, 1.0f);
    }

    if (elapsedSeconds < fadeOutStart)
    {
        return 1.0f;
    }

    const float remaining = durationSeconds - elapsedSeconds;
    return std::clamp(remaining / fade, 0.0f, 1.0f);
}

glm::vec4 notificationSeverityColor(NotificationSeverity severity,
                                    const UITheme& theme)
{
    // Promote text colours (vec3) to full-alpha vec4 so the callsite can
    // multiply by an envelope alpha uniformly.
    switch (severity)
    {
        case NotificationSeverity::Info:
            return glm::vec4(theme.textSecondary, 1.0f);
        case NotificationSeverity::Success:
            return theme.accent;
        case NotificationSeverity::Warning:
            return glm::vec4(theme.textWarning, 1.0f);
        case NotificationSeverity::Error:
            return glm::vec4(theme.textError, 1.0f);
    }
    return glm::vec4(theme.textSecondary, 1.0f);
}

// -- NotificationQueue --------------------------------------------------

void NotificationQueue::push(const Notification& notification)
{
    if (m_capacity == 0)
    {
        return;
    }

    if (m_active.size() >= m_capacity)
    {
        m_active.erase(m_active.begin());
    }

    ActiveNotification entry;
    entry.data = notification;
    entry.data.durationSeconds = std::max(0.0f, notification.durationSeconds);
    entry.elapsedSeconds = 0.0f;
    // Spawn at alpha 0; the first `advance` call will ramp it up. Keeps
    // the first frame consistent with the rest of the envelope.
    entry.alpha = 0.0f;
    m_active.push_back(entry);
}

void NotificationQueue::advance(float deltaSeconds, float fadeSeconds)
{
    const float dt = std::max(0.0f, deltaSeconds);

    for (auto& entry : m_active)
    {
        entry.elapsedSeconds += dt;
        entry.alpha = notificationAlphaAt(entry.elapsedSeconds,
                                          entry.data.durationSeconds,
                                          fadeSeconds);
    }

    m_active.erase(
        std::remove_if(m_active.begin(), m_active.end(),
            [](const ActiveNotification& e)
            {
                return e.elapsedSeconds >= e.data.durationSeconds;
            }),
        m_active.end());
}

void NotificationQueue::setCapacity(std::size_t capacity)
{
    m_capacity = capacity;
    if (m_active.size() > capacity)
    {
        const std::size_t toDrop = m_active.size() - capacity;
        m_active.erase(m_active.begin(),
                       m_active.begin() + static_cast<std::ptrdiff_t>(toDrop));
    }
}

// -- UINotificationToast -------------------------------------------------

UINotificationToast::UINotificationToast()
{
    interactive = false;
    m_accessible.role = UIAccessibleRole::Label;
}

void UINotificationToast::updateFromNotification(const ActiveNotification& notification,
                                                 const UITheme& theme,
                                                 TextRenderer* textRenderer)
{
    accentColor = notificationSeverityColor(notification.data.severity, theme);
    accentWidth = theme.buttonAccentTickWidth;
    backgroundColor = theme.panelBg;
    alpha = std::clamp(notification.alpha, 0.0f, 1.0f);

    // Accessibility description: "<title> — <body>" so a screen-reader
    // bridge announces the whole thing in one utterance, prefixed by the
    // severity label via `uiAccessibleRoleLabel(Label)`.
    m_accessible.label = notification.data.title;
    if (!notification.data.body.empty())
    {
        m_accessible.description = notification.data.body;
    }
    else
    {
        m_accessible.description.clear();
    }

    const bool textChanged = (m_title != notification.data.title)
                          || (m_body  != notification.data.body);
    if (!textChanged && getChildCount() > 0)
    {
        return;
    }

    m_title = notification.data.title;
    m_body  = notification.data.body;

    clearChildren();

    const float padX = theme.buttonPadX * 0.5f;
    const float padY = theme.settingRowVerticalPad * 0.5f;

    auto title = std::make_unique<UILabel>();
    title->text = m_title;
    title->color = theme.textPrimary;
    title->scale = theme.typeBody / theme.typeBody;  // 1.0× baseline
    title->textRenderer = textRenderer;
    title->anchor = Anchor::TOP_LEFT;
    title->position = {padX, padY};
    addChild(std::move(title));

    if (!m_body.empty())
    {
        auto body = std::make_unique<UILabel>();
        body->text = m_body;
        body->color = theme.textSecondary;
        body->scale = theme.typeCaption / theme.typeBody;
        body->textRenderer = textRenderer;
        body->anchor = Anchor::TOP_LEFT;
        body->position = {padX, padY + theme.typeBody + 4.0f};
        addChild(std::move(body));
    }
}

void UINotificationToast::render(SpriteBatchRenderer& batch,
                                 const glm::vec2& parentOffset,
                                 int screenWidth, int screenHeight)
{
    if (!visible || alpha <= 0.0f)
    {
        return;
    }

    const glm::vec2 absPos = computeAbsolutePosition(parentOffset, screenWidth, screenHeight);

    // Background panel — premultiply alpha into the RGBA so the envelope
    // fade respects the panel's own translucency.
    glm::vec4 bg = backgroundColor;
    bg.a *= alpha;
    batch.drawQuad(absPos, size, bg);

    // Severity strip on the leading edge — full-height, `accentWidth` wide.
    const float strip = std::max(1.0f, accentWidth);
    glm::vec4 accent = accentColor;
    accent.a *= alpha;
    batch.drawQuad(absPos, glm::vec2(strip, size.y), accent);

    for (auto& child : m_children)
    {
        child->render(batch, absPos, screenWidth, screenHeight);
    }
}

} // namespace Vestige
