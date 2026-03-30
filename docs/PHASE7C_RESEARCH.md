# Phase 7C Research: Object Animation

## Scope

Phase 7C covers non-skeletal object-level animation:
1. **Property animation system** — animate any numeric property over time (position, rotation, color, intensity)
2. **Easing functions** — standard easing curves (Penner equations, cubic bezier)
3. **Playback modes** — one-shot, looping, ping-pong
4. **Animation events** — trigger callbacks at specific times during playback

---

## 1. Property Animation (Tween) Systems

### Industry Patterns

Property animation systems ("tweens") are the standard way game engines animate non-skeletal properties. The core concept: **change a specific property from value A to value B over a given duration, using an easing function to shape the transition**.

**Godot Tween** ([docs](https://docs.godotengine.org/en/stable/classes/class_tween.html)):
- `create_tween()` factory — fire-and-forget, auto-cleanup on completion
- Fluent API: `tween.tween_property(node, "position", target, duration).set_ease(EASE_IN_OUT)`
- Sequential by default; `parallel()` makes tweens run simultaneously
- Built-in easing enum selection

**Unity DOTween** pattern (via [GoTween](https://github.com/AhmedGD1/GoTween)):
- Chainable API: `transform.DOMove(target, duration).SetEase(Ease.OutBounce).OnComplete(callback)`
- Object pooling for zero-allocation tweens
- Sequence orchestration (sequential + parallel groups)

**Tweeny C++ library** ([github](https://github.com/mobius3/tweeny)):
- Header-only, C++11, zero dependencies
- `tweeny::from(0).to(100).during(1000).via(easing::cubicInOut)` — fluent builder
- Multi-point keyframe tweening with per-segment easing
- Callbacks via `onStep()` returning bool (true = one-shot, false = persistent)
- Bidirectional stepping + seek to any progress point

### Key Design Decisions

For Vestige, the tween system should be:
- **Component-based** — `TweenComponent` attached to entities, ticked by `Entity::update()`
- **Type-erased targets** — animate `float*`, `vec3*`, `vec4*`, or use a callback
- **Pooled internally** — reuse tween slots to avoid per-tween allocations
- **Non-owning** — tweens reference target properties by pointer, entity lifetime management is external

---

## 2. Easing Functions

### Robert Penner's Equations

The industry standard: 30 functions in 10 families, each with In/Out/InOut variants ([source](https://robertpenner.com/easing/)).

| Family | Math Basis | Character |
|--------|-----------|-----------|
| Quad | `t^2` | Gentle acceleration |
| Cubic | `t^3` | Moderate acceleration |
| Quart | `t^4` | Strong acceleration |
| Quint | `t^5` | Very strong acceleration |
| Sine | `sin(t * PI/2)` | Smooth, natural feel |
| Expo | `2^(10*(t-1))` | Sudden acceleration |
| Circ | `sqrt(1 - t^2)` | Circular motion feel |
| Elastic | `sin(13*PI/2*t) * 2^(10*(t-1))` | Spring overshoot |
| Back | `t^3 - t*sin(t*PI)` | Slight overshoot |
| Bounce | Piecewise polynomial | Physical bounce |

Plus **Linear** (identity) and **Step** (instant jump).

All functions take normalized time `t` in `[0,1]` and return progress in `[0,1]` (with possible overshoot for Elastic/Back/Bounce).

**Implementation pattern** from [AHEasing](https://github.com/warrenm/AHEasing):
```cpp
typedef float (*EasingFunction)(float t);
float QuadraticEaseIn(float t) { return t * t; }
float QuadraticEaseOut(float t) { return -(t * (t - 2.0f)); }
```
Pure functions, no state, O(1) lookup via function pointer array indexed by enum.

### Cubic Bezier Curves

CSS-style `cubic-bezier(x1, y1, x2, y2)` for custom easing ([reference](https://greweb.me/2012/02/bezier-curve-based-easing-functions-from-concept-to-implementation)):
- X axis = time, Y axis = progress
- Control points P0=(0,0), P1=(x1,y1), P2=(x2,y2), P3=(1,1)
- Requires solving cubic equation to find `t` for given `x` — use Newton-Raphson iteration
- 28 bytes storage (4 floats for control points)
- Can reproduce all Penner curves approximately

### Recommendation

Implement all 31 Penner functions as pure `float(float)` functions, plus a `CubicBezierEase` class for custom curves. Store easing selection as an enum for fast dispatch.

---

## 3. Playback Modes

Standard modes across all engines:

| Mode | Behavior | Use Case |
|------|----------|----------|
| **OneShot** | Play once, stop at end | Door opening, explosion |
| **Loop** | Restart from beginning when reaching end | Idle bobbing, rotating beacon |
| **PingPong** | Reverse direction at each end | Pulsing glow, breathing animation |

Implementation: Track a `direction` flag (+1 or -1). On completion:
- OneShot: clamp to 1.0, mark finished
- Loop: wrap `progress -= 1.0f`
- PingPong: reverse direction, reflect progress

---

## 4. Animation Events

### Industry Approaches

**Unity AnimationEvent** ([docs](https://docs.unity3d.com/Manual/script-AnimationWindowEvent.html)):
- Events placed at specific normalized times on an animation clip
- Fire a named method on the target GameObject
- Can carry one parameter (float, string, int, or object)
- Evaluated during animation update — if playback jumps past an event, it still fires

**Spine Events** ([docs](http://esotericsoftware.com/spine-unity-events)):
- Events defined in the animation data, fired during playback
- Callbacks registered per-track: `trackEntry.Event += handler`
- Used for footstep sounds, particle spawning, gameplay triggers

**Godot AnimationPlayer**:
- Method Call Tracks — keyframes that call methods on target nodes at specific times
- Signal-based: `animation_finished`, `animation_started`

### Key Design Decisions

For Vestige:
- Events stored as `{normalizedTime, callback}` pairs on each tween
- Callbacks are `std::function<void()>` — simple and flexible
- Events fire once per playback pass (tracked with a "last fired" time to handle frame skipping)
- Lifecycle callbacks: `onComplete`, `onLoop` as special events

---

## 5. glTF Node Animation

glTF animations can target **any node**, not just skeleton joints ([tutorial](https://github.khronos.org/glTF-Tutorials/gltfTutorial/gltfTutorial_006_SimpleAnimation.html)). The channel/sampler/target architecture:

```
Buffer → BufferView → Accessor → Sampler(input/output) → Channel → Node.property
```

- `path`: "translation", "rotation", "scale", or "weights"
- Each TRS component has its own channel with independent keyframe timing
- When a node is animated, only TRS may be present (no matrix property)
- Interpolation: LINEAR (slerp for quaternions), STEP, CUBICSPLINE

**Our engine already handles this** — the glTF loader reads animation channels and the sampler evaluates them. What's missing is playing these animations on non-skinned entities (e.g., a rotating door node). This is a separate concern from the tween system and maps to a `NodeAnimator` component.

---

## 6. Existing Engine Infrastructure

| Component | Status | Relevance |
|-----------|--------|-----------|
| Entity/Component system | Complete | Tweens will be a Component |
| EventBus | Complete | Can publish animation lifecycle events |
| AnimationSampler | Complete | Reuse for glTF node animation |
| AnimationCurve (editor) | Complete | Particle-only; separate from tween easing |
| SkeletonAnimator | Complete | Orthogonal — skeletal vs. object animation |

---

## Sources

- [Robert Penner's Easing Functions](https://robertpenner.com/easing/)
- [AHEasing C/C++ Library](https://github.com/warrenm/AHEasing/)
- [Tweeny C++ Tween Library](https://github.com/mobius3/tweeny)
- [Easing Functions Cheat Sheet](https://easings.net/)
- [Bezier Curve as Easing Function](https://asawicki.info/news_1790_bezier_curve_as_easing_function_in_c)
- [Bezier Easing — Concept to Implementation](https://greweb.me/2012/02/bezier-curve-based-easing-functions-from-concept-to-implementation)
- [Tom Looman — Animating in C++: Curves and Easing](https://tomlooman.com/unreal-engine-animation-cpp-curves-and-easing-functions/)
- [Godot Tween Documentation](https://docs.godotengine.org/en/stable/classes/class_tween.html)
- [Unity AnimationEvent Manual](https://docs.unity3d.com/Manual/script-AnimationWindowEvent.html)
- [Spine Events and Callbacks](http://esotericsoftware.com/spine-unity-events)
- [glTF Tutorial — Simple Animation](https://github.khronos.org/glTF-Tutorials/gltfTutorial/gltfTutorial_006_SimpleAnimation.html)
- [glTF Animation System — DeepWiki](https://deepwiki.com/KhronosGroup/glTF/2.4-animation-system)
- [Gamedeveloper — Architecting a 3D Animation Engine](https://www.gamedeveloper.com/programming/architecting-a-3d-animation-engine)
