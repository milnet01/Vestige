// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file tween.h
/// @brief Property animation (tween) system with easing and events.
#pragma once

#include "animation/easing.h"
#include "scene/component.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace Vestige
{

/// @brief Playback mode for tweens.
enum class TweenPlayback : uint8_t
{
    ONCE,       ///< Play once and stop at the end.
    LOOP,       ///< Restart from the beginning on completion.
    PING_PONG   ///< Reverse direction at each end.
};

/// @brief An event that fires at a specific point during tween playback.
struct TweenEvent
{
    float normalizedTime;            ///< When to fire [0,1].
    std::function<void()> callback;  ///< What to call.
};

/// @brief Animates a numeric property from one value to another over time.
///
/// Supports float, vec3, vec4, and quaternion targets. Quaternion tweens
/// use slerp; all others use linear interpolation with easing applied.
class Tween
{
public:
    /// @brief Target value type.
    enum class TargetType : uint8_t { FLOAT, VEC3, VEC4, QUAT };

    Tween(Tween&&) noexcept = default;
    Tween& operator=(Tween&&) noexcept = default;
    ~Tween() = default;

    // --- Factory methods ---

    /// @brief Creates a tween that animates a float value.
    static Tween floatTween(float* target, float from, float to,
                            float duration, EaseType ease = EaseType::LINEAR);

    /// @brief Creates a tween that animates a vec3 value.
    static Tween vec3Tween(glm::vec3* target, const glm::vec3& from, const glm::vec3& to,
                           float duration, EaseType ease = EaseType::LINEAR);

    /// @brief Creates a tween that animates a vec4 value.
    static Tween vec4Tween(glm::vec4* target, const glm::vec4& from, const glm::vec4& to,
                           float duration, EaseType ease = EaseType::LINEAR);

    /// @brief Creates a tween that animates a quaternion value (uses slerp).
    static Tween quatTween(glm::quat* target, const glm::quat& from, const glm::quat& to,
                           float duration, EaseType ease = EaseType::LINEAR);

    // --- Builder configuration ---

    /// @brief Sets the playback mode (once, loop, or ping-pong).
    Tween& setPlayback(TweenPlayback mode);

    /// @brief Sets a delay before the tween starts, in seconds.
    Tween& setDelay(float seconds);

    /// @brief Sets the easing function type.
    Tween& setEase(EaseType ease);

    /// @brief Sets a custom cubic bezier easing curve (CSS-style control points).
    Tween& setCustomEase(float x1, float y1, float x2, float y2);

    /// @brief Registers a callback invoked when the tween finishes.
    Tween& onComplete(std::function<void()> callback);

    /// @brief Registers a callback invoked each time a looping tween restarts.
    Tween& onLoop(std::function<void()> callback);

    /// @brief Adds an event that fires when playback passes a normalized time [0,1].
    Tween& addEvent(float normalizedTime, std::function<void()> callback);

    // --- Playback control ---

    /// @brief Advances the tween by deltaTime seconds.
    void update(float deltaTime);

    /// @brief Pauses playback.
    void pause();

    /// @brief Resumes playback after pause.
    void resume();

    /// @brief Stops and marks the tween as finished.
    void stop();

    /// @brief Resets elapsed time and restarts playback from the beginning.
    void restart();

    // --- Queries ---

    /// @brief Returns true if the tween has finished.
    bool isFinished() const;

    /// @brief Returns true if the tween is paused.
    bool isPaused() const;

    /// @brief Returns the current normalized progress [0,1].
    float getProgress() const;

    /// @brief Returns the target value type.
    TargetType getTargetType() const;

    /// @brief Returns the raw pointer to the animated target.
    void* getTarget() const;

private:
    Tween() = default;

    void applyValue(float easedProgress);
    void fireEvents(float oldProgress, float newProgress);

    TargetType m_targetType = TargetType::FLOAT;
    void* m_target = nullptr;
    float m_from[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float m_to[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    float m_duration = 1.0f;
    float m_elapsed = 0.0f;
    float m_delay = 0.0f;
    float m_progress = 0.0f;

    EaseType m_easeType = EaseType::LINEAR;
    std::unique_ptr<CubicBezierEase> m_customEase;

    TweenPlayback m_playback = TweenPlayback::ONCE;
    int m_direction = 1;
    bool m_finished = false;
    bool m_paused = false;
    bool m_started = false;

    std::vector<TweenEvent> m_events;
    std::function<void()> m_onComplete;
    std::function<void()> m_onLoop;
};

/// @brief Component that manages multiple tweens on an entity.
///
/// Add to any entity that needs property animation. The manager ticks
/// all active tweens each frame and removes finished ones automatically.
class TweenManager : public Component
{
public:
    /// @brief Adds a tween. The manager takes ownership and ticks it.
    /// @return Reference to the stored tween for further configuration.
    Tween& add(Tween tween);

    /// @brief Cancels all tweens targeting a specific pointer.
    void cancelTarget(void* target);

    /// @brief Cancels all active tweens.
    void cancelAll();

    /// @brief Returns the number of active (non-finished) tweens.
    size_t activeTweenCount() const;

    // --- Component interface ---
    void update(float deltaTime) override;
    std::unique_ptr<Component> clone() const override;

private:
    std::vector<Tween> m_tweens;
};

} // namespace Vestige
