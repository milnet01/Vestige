# Phase 7C Design: Object Animation

## Overview

Four deliverables:
1. **Easing functions** — 31 Penner equations + cubic bezier custom curves
2. **Tween system** — component that animates numeric properties over time
3. **Playback modes** — one-shot, loop, ping-pong
4. **Animation events** — callbacks at specific times during tween playback

---

## 1. Easing Functions

### File: `engine/animation/easing.h` / `easing.cpp`

```cpp
enum class EaseType : uint8_t
{
    LINEAR,
    STEP,
    // Quad
    EASE_IN_QUAD, EASE_OUT_QUAD, EASE_IN_OUT_QUAD,
    // Cubic
    EASE_IN_CUBIC, EASE_OUT_CUBIC, EASE_IN_OUT_CUBIC,
    // Quart
    EASE_IN_QUART, EASE_OUT_QUART, EASE_IN_OUT_QUART,
    // Quint
    EASE_IN_QUINT, EASE_OUT_QUINT, EASE_IN_OUT_QUINT,
    // Sine
    EASE_IN_SINE, EASE_OUT_SINE, EASE_IN_OUT_SINE,
    // Expo
    EASE_IN_EXPO, EASE_OUT_EXPO, EASE_IN_OUT_EXPO,
    // Circ
    EASE_IN_CIRC, EASE_OUT_CIRC, EASE_IN_OUT_CIRC,
    // Elastic
    EASE_IN_ELASTIC, EASE_OUT_ELASTIC, EASE_IN_OUT_ELASTIC,
    // Back
    EASE_IN_BACK, EASE_OUT_BACK, EASE_IN_OUT_BACK,
    // Bounce
    EASE_IN_BOUNCE, EASE_OUT_BOUNCE, EASE_IN_OUT_BOUNCE,

    COUNT
};

/// @brief Evaluates an easing function at normalized time t [0,1].
/// @return Progress value (usually [0,1], may overshoot for Elastic/Back).
float evaluateEasing(EaseType type, float t);

/// @brief Returns the name string for an EaseType (for serialization/debug).
const char* easeTypeName(EaseType type);
```

**Implementation:** A static lookup table of `float(*)(float)` function pointers indexed by `EaseType`. Each function is a pure, branchless formula from the Penner equations.

### Cubic Bezier Custom Curve

```cpp
class CubicBezierEase
{
public:
    CubicBezierEase(float x1, float y1, float x2, float y2);
    float evaluate(float t) const;

private:
    float m_cx, m_bx, m_ax;  // Precomputed polynomial coefficients
    float m_cy, m_by, m_ay;
};
```

Uses Newton-Raphson iteration (3-5 iterations) to solve `x(t) = input` for `t`, then evaluates `y(t)`. Coefficients precomputed in constructor.

---

## 2. Tween System

### File: `engine/animation/tween.h` / `tween.cpp`

### Playback Mode Enum

```cpp
enum class TweenPlayback : uint8_t
{
    ONCE,       // Play once and stop
    LOOP,       // Restart from beginning
    PING_PONG   // Reverse direction at each end
};
```

### Animation Event

```cpp
struct TweenEvent
{
    float normalizedTime;           // When to fire [0,1]
    std::function<void()> callback; // What to do
};
```

### Tween Class

```cpp
class Tween
{
public:
    // --- Factory methods (static) ---
    static Tween floatTween(float* target, float from, float to,
                            float duration, EaseType ease = EaseType::LINEAR);
    static Tween vec3Tween(glm::vec3* target, const glm::vec3& from, const glm::vec3& to,
                           float duration, EaseType ease = EaseType::LINEAR);
    static Tween vec4Tween(glm::vec4* target, const glm::vec4& from, const glm::vec4& to,
                           float duration, EaseType ease = EaseType::LINEAR);
    static Tween quatTween(glm::quat* target, const glm::quat& from, const glm::quat& to,
                           float duration, EaseType ease = EaseType::LINEAR);

    // --- Configuration (builder pattern) ---
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
    bool isFinished() const;
    bool isPaused() const;
    float getProgress() const;  // [0,1]

private:
    enum class TargetType { FLOAT, VEC3, VEC4, QUAT };

    TargetType m_targetType;
    void* m_target;                 // Non-owning pointer to animated value
    float m_from[4];                // Start value (up to 4 components)
    float m_to[4];                  // End value (up to 4 components)
    float m_duration;
    float m_elapsed;
    float m_delay;
    EaseType m_easeType;
    std::unique_ptr<CubicBezierEase> m_customEase;
    TweenPlayback m_playback;
    int m_direction;                // +1 forward, -1 backward (ping-pong)
    bool m_finished;
    bool m_paused;

    // Events
    std::vector<TweenEvent> m_events;
    float m_lastProgress;           // For detecting event crossings
    std::function<void()> m_onComplete;
    std::function<void()> m_onLoop;
};
```

### TweenManager Component

```cpp
class TweenManager : public Component
{
public:
    // Add a tween — TweenManager owns it and ticks it each frame
    Tween& add(Tween tween);

    // Cancel all tweens targeting a specific pointer
    void cancelTarget(void* target);

    // Cancel all tweens
    void cancelAll();

    // Query
    size_t activeTweenCount() const;

    // Component interface
    void update(float deltaTime) override;
    std::unique_ptr<Component> clone() const override;
};
```

### Update Logic

```
update(dt):
    if paused or finished: return
    elapsed += dt
    if elapsed < delay: return

    activeTime = elapsed - delay
    rawProgress = clamp(activeTime / duration, 0, 1)

    switch playback:
        ONCE: progress = rawProgress; if rawProgress >= 1.0 → finished, fire onComplete
        LOOP: progress = fmod(activeTime, duration) / duration; fire onLoop at wrap
        PING_PONG: cycle = fmod(activeTime, duration*2); if cycle > duration → progress = 1-(cycle-duration)/duration else progress = cycle/duration

    easedProgress = evaluateEasing(easeType, progress)  // or customEase

    // Apply to target based on type
    FLOAT: *target = lerp(from, to, easedProgress)
    VEC3: *target = mix(from, to, easedProgress)
    VEC4: *target = mix(from, to, easedProgress)
    QUAT: *target = slerp(from, to, easedProgress)

    // Fire events that were crossed this frame
    for each event: if lastProgress < event.time <= progress (or reverse): fire callback
    lastProgress = progress
```

---

## 3. File Structure

| File | Purpose |
|------|---------|
| `engine/animation/easing.h` | EaseType enum, evaluateEasing(), CubicBezierEase |
| `engine/animation/easing.cpp` | All 31 easing function implementations |
| `engine/animation/tween.h` | Tween, TweenPlayback, TweenEvent, TweenManager |
| `engine/animation/tween.cpp` | Tween update logic, TweenManager |
| `tests/test_easing.cpp` | Easing function boundary/midpoint tests |
| `tests/test_tween.cpp` | Tween playback, modes, events tests |

---

## 4. Testing Plan

### Easing Tests (~15 tests)
- All functions return 0 at t=0, 1 at t=1 (boundary correctness)
- Linear returns t (identity)
- Step returns 0 for t<1, 1 at t=1
- Quad/Cubic/Sine midpoint values verified against known results
- EaseIn starts slow (f(0.5) < 0.5 for convex curves)
- EaseOut starts fast (f(0.5) > 0.5 for concave curves)
- Bounce stays within [0,1] range
- CubicBezier linear equivalent (0.25, 0.25, 0.75, 0.75) ≈ linear
- CubicBezier ease-in (0.42, 0, 1, 1) produces expected curve shape

### Tween Tests (~15 tests)
- Float tween reaches target after duration
- Vec3 tween interpolates correctly at midpoint
- Quat tween uses slerp (not lerp)
- Delay postpones start
- Pause/resume preserves progress
- OneShot marks finished, doesn't loop
- Loop wraps and continues
- PingPong reverses direction
- Events fire at correct progress points
- Events fire even when frame skips past event time
- onComplete fires exactly once for OneShot
- onLoop fires each cycle
- TweenManager ticks all active tweens
- TweenManager removes finished tweens
- cancelTarget removes specific tweens

---

## 5. Performance Considerations

- Easing functions are pure math — no allocations, branch-friendly
- Tween storage: ~80 bytes per tween (fits in two cache lines)
- TweenManager uses `std::vector` with swap-and-pop removal for O(1) finished tween cleanup
- Typical scene: <100 active tweens — no concern at 60 FPS
- No heap allocation per-frame; callbacks allocated once at tween creation
