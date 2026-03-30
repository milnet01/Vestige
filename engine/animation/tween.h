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

    static Tween floatTween(float* target, float from, float to,
                            float duration, EaseType ease = EaseType::LINEAR);

    static Tween vec3Tween(glm::vec3* target, const glm::vec3& from, const glm::vec3& to,
                           float duration, EaseType ease = EaseType::LINEAR);

    static Tween vec4Tween(glm::vec4* target, const glm::vec4& from, const glm::vec4& to,
                           float duration, EaseType ease = EaseType::LINEAR);

    static Tween quatTween(glm::quat* target, const glm::quat& from, const glm::quat& to,
                           float duration, EaseType ease = EaseType::LINEAR);

    // --- Builder configuration ---

    Tween& setPlayback(TweenPlayback mode);
    Tween& setDelay(float seconds);
    Tween& setEase(EaseType ease);
    Tween& setCustomEase(float x1, float y1, float x2, float y2);
    Tween& onComplete(std::function<void()> callback);
    Tween& onLoop(std::function<void()> callback);
    Tween& addEvent(float normalizedTime, std::function<void()> callback);

    // --- Playback control ---

    void update(float deltaTime);
    void pause();
    void resume();
    void stop();
    void restart();

    // --- Queries ---

    bool isFinished() const;
    bool isPaused() const;
    float getProgress() const;
    TargetType getTargetType() const;
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
