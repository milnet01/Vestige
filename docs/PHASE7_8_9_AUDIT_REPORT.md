# Phases 7, 8, 9 (Formula Pipeline) — Post-Phase Audit Report

**Date:** 2026-04-03
**Scope:** Phase 7 (Animation), Phase 8 (Physics), Phase 9/Formula Pipeline
**Auditors:** 4 parallel subagents + automated tools

---

## Tier 1 — Automated Tools

| Tool | Result |
|------|--------|
| Compiler warnings | 0 (clean build with -Wall -Wextra) |
| Test suite | 1305/1305 passing, 1 skipped (GPU-only) |
| AddressSanitizer | Only known fontconfig/glib system leaks (suppressed) |

---

## Summary

| Severity | Count |
|----------|-------|
| Critical | 6 |
| High | 10 |
| Medium | 16 |
| Low | 12 |

---

## Critical Findings

1. **engine/physics/dismemberment.cpp:121** — Wrong split parameter formula: `t = da / denom` produces incorrect interpolation for edge split points, resulting in wrong mesh geometry during dismemberment.
   *Tier: Subagent A (Bugs)*

2. **engine/physics/dismemberment.cpp:129-130** — Duplicate vertices added to both body and limb meshes unconditionally. Lines 133-148 then add original vertices again, creating duplicates in both output meshes.
   *Tier: Subagent A (Bugs)*

3. **engine/formula/lut_loader.cpp:98** — Integer overflow in `totalSamples *= axis.size` for 3D LUTs with large resolutions. No bounds check before multiplication; may cause heap buffer overflow.
   *Tier: Subagent E (Security)*

4. **engine/formula/lut_generator.cpp:80** — Same integer overflow risk in `totalSamples *= axis.resolution` during LUT generation.
   *Tier: Subagent E (Security)*

5. **engine/formula/formula_preset.cpp:31-32** — `j.at("formula")` throws unhandled exception if JSON field is missing. No try-catch in loadFromJson loop — crashes on malformed preset files.
   *Tier: Subagent E (Security)*

6. **engine/formula/lut_loader.cpp:103** — `static_cast<std::streamsize>(totalSamples * sizeof(float))` can overflow on 32-bit streamsize, silently truncating file reads.
   *Tier: Subagent E (Security)*

---

## High Findings

7. **engine/physics/ragdoll.cpp:79, 161** — Raw `new`/`delete` for Jolt skeleton. Should use `JPH::Ref` for automatic lifetime management.
   *Tier: Subagent B (Memory)*

8. **engine/physics/ragdoll.cpp:477-575** — Multiple raw `new` allocations for Jolt shapes/constraints in `buildSettings()` without `JPH::Ref` wrapper.
   *Tier: Subagent B (Memory)*

9. **engine/physics/physics_world.cpp:85** — Raw `new JPH::Factory()` without RAII wrapper.
   *Tier: Subagent B (Memory)*

10. **engine/animation/motion_matcher.cpp:143-147** — Frame boundary check doesn't validate clip containment. `newFrame` computed from `m_currentFrameTime` may exceed current clip bounds.
    *Tier: Subagent A (Bugs)*

11. **engine/animation/lip_sync.cpp:407** — Returns without applying weights when no cue found, causing lip sync weights to become stale.
    *Tier: Subagent A (Bugs)*

12. **engine/physics/spatial_hash.cpp:127** — Hard-coded visited bucket array (size 64) can overflow for large-radius queries.
    *Tier: Subagent A (Bugs)*

13. **engine/animation/skeleton_animator.cpp:61** — Array index used after bounds test but without verifying idx < container size in local scope.
    *Tier: Subagent E (Security)*

14. **engine/animation/animation_sampler.cpp:52-53** — `readVec3(values, keyIndex)` reads `values[base+2]` without validating vector size.
    *Tier: Subagent E (Security)*

15. **engine/animation/lip_sync.cpp:145** — Unchecked JSON array iteration; `cue["start"].get<float>()` throws if field missing.
    *Tier: Subagent E (Security)*

16. **engine/physics/physics_character_controller.h:92** — Non-const public getter `getConfig()` exposes mutable internal state.
    *Tier: Subagent D (Quality)*

---

## Medium Findings

17. **engine/animation/trajectory_predictor.cpp:93-94** — Hardcoded pi values lack precision; should use `glm::pi<float>()`.
18. **engine/animation/facial_animation.cpp:123** — Division by zero risk if `m_transitionDuration` is zero.
19. **engine/physics/cloth_simulator.cpp:265** — Missing epsilon guard before sqrt in velocity clamping.
20. **engine/formula/lut_loader.cpp:189** — Float precision loss in LUT index calculation for large indices.
21. **engine/formula/curve_fitter.cpp:99** — Singular matrix threshold `1e-15` too strict for float (should be ~`1e-6`).
22. **engine/formula/expression_eval.cpp:56** — Division by zero returns 0.0f silently; masks errors in formula chains.
23. **engine/animation/motion_matcher.cpp:164** — Full pose copy per search; should cache between searches.
24. **engine/physics/rigid_body.cpp:86** — Mesh shape validation message says "1 triangle" but allows < 3 indices.
25. **engine/animation/motion_database.cpp:235** — Frame index clamp occurs after clipped frame used in comparisons.
26. **assets/shaders/terrain.frag.glsl:128** — Division by zero in triplanar weights when geometry is flat.
27. **assets/shaders/water.frag.glsl:230** — Epsilon 0.00001 insufficient for depth division guard.
28. **engine/formula/formula_preset.cpp:176** — JSON parse failure silently returns 0; no log message.
29. **engine/formula/lut_generator.cpp:137** — Result data indexed without allocation failure check.
30. **engine/physics/cloth_simulator.cpp:37** — Division by `config.particleMass` without near-zero warning.
31. **engine/formula/codegen_cpp.cpp:238** — NaN/Inf literal generation without validation of output.
32. **assets/shaders/terrain.frag.glsl:277** — `depthBelowWater` subtraction can produce large positive value.

---

## Low Findings

33. **engine/animation/kd_tree.cpp:82** — Uneven partition for odd-sized ranges (`count/2` vs `(count+1)/2`).
34. **engine/animation/eye_controller.cpp:151** — Asymmetric blink relies on unvalidated `BLINK_CLOSE_POINT` constant.
35. **engine/environment/environment_forces.cpp:172** — Vector normalization without epsilon guard for small lengths.
36. **engine/animation/audio_analyzer.cpp:57** — Potential divide-by-zero if FFT_SIZE were 1 (currently constexpr 512).
37. **engine/formula/expression_eval.cpp:80** — `log(0)` silently returns 0 instead of -infinity.
38. **Multiple files** — Duplicated `std::unordered_map<std::string, float>` pattern could use type alias.
39. **engine/physics/cloth_simulator.h:332** — Private method declared before private members (style).
40. **engine/formula/formula.h, formula_library.h** — Missing explicit #include for nlohmann/json (relies on transitive include).
41. **engine/physics/breakable_component.cpp:39** — Triple string concatenation in logging path.
42. **engine/formula/expression_eval.cpp:77** — `sqrt(fabs(arg))` loses sign information (expected behavior).
43. **engine/animation/animation_sampler.cpp:40** — Zero duration returns 0.0f (acceptable).
44. **assets/shaders/scene.frag.glsl:539** — SH grid evaluation lacks NaN check on L coefficients.

---

## Shader Uniform Synchronization — PASS

All quality-tier uniforms properly synchronized:
- `u_waterQualityTier` — set at `water_renderer.cpp:101`
- `u_causticsQuality` (scene) — set at `renderer.cpp:2549`
- `u_causticsQuality` (terrain) — set at `terrain_renderer.cpp:181`

No missing or unset uniforms detected.

---

## Code Quality Assessment — EXCELLENT

- Zero naming convention violations
- Zero TODO/FIXME/HACK comments
- Zero dead code detected
- Consistent #pragma once, Allman bracing, 4-space indentation
- Proper const correctness in 99%+ of APIs

---

## Performance Issues Found This Session

During this audit session, a critical runtime performance issue was discovered and fixed:

1. **83 cloth simulators** running per frame for courtyard fence panels — replaced with static mesh quads (200ms → 0ms CPU savings)
2. **`std::sin`-based hashNoise** on CPU — replaced with integer bit-mixing hash (~6x faster per call)
3. **16-substep cloth presets** — reduced to 6 (curtains) and 4 (veil)
4. **Dynamic loop in water.frag.glsl** — split into fixed-count `waterFbm2`/`waterFbm3` (GPU unroll fix)
5. **Jolt BoxShape assertion crash** — half-extent minimum raised to 0.05 (convex radius)

---

## Recommendations

### Immediate (Critical fixes)
1. Fix dismemberment split formula and duplicate vertex bug
2. Add integer overflow checks in LUT loader/generator
3. Add try-catch for JSON preset parsing
4. Add streamsize overflow guard in LUT loader

### High Priority
5. Standardize Jolt allocations on `JPH::Ref` — remove raw `new`/`delete`
6. Fix motion matcher clip boundary validation
7. Fix lip sync stale weights on missing cue
8. Replace fixed-size visited bucket array in spatial_hash with dynamic vector

### Medium Priority
9. Add epsilon guards for float divisions (shaders + CPU)
10. Use `glm::pi<float>()` instead of hardcoded pi values
11. Cache motion matcher pose between searches
12. Improve curve fitter singular matrix threshold

### Deferred (Low)
13. Type alias for `WeightMap`
14. Explicit nlohmann/json includes
15. NaN guards in SH grid shader evaluation
