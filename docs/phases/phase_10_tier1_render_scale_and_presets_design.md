# Phase 10 — Tier 1: Render-scale + Quality presets (design)

**Status:** Draft — pending `/cold-eyes` convergence, then Claude sign-off (per the
delegated-sign-off convention) and implementation.
**Parent:** `docs/phases/phase_10_performance_scalability_strategy.md` (Tier 1 = T1a render-scale
+ T1b quality-preset wiring). This doc is the implementable spec for that tier's **first wave**.
**Goal (plain):** let weak/handheld hardware hold 60 FPS by (a) rendering the 3D scene smaller
and upscaling to the window, and (b) making the existing Low/Medium/High/Ultra dial actually
switch that scale and the cheap-to-toggle effects.

---

## 1. Scope

**In (first wave):**
- **T1a — `renderScale`:** render the scene + post-process chain at `renderScale × window`
  resolution, present upscaled to the window. Wire the already-persisted, already-UI-exposed,
  already-clamped `DisplaySettings::renderScale` (`settings.h:103–106`) that today has no
  consumer.
- **T1b — quality preset → the setters that already exist:** a `applyQualityPreset()` that maps
  `Low/Medium/High/Ultra` onto `renderScale` + anti-alias mode + SSAO + bloom (all four have
  live setters). Plus a small perf-gate for the two biggest *optional* passes — volumetric fog
  and dynamic GI — so Low can drop them for performance independently of accessibility.

**Deferred to Tier 2 (needs new setters — out of scope here, named so the preset table is
honest):** shadow resolution / cascade count (fixed in `CascadedShadowMap`, no runtime setter),
foliage draw-distance/density (hardcoded `100.0f`, `engine.cpp:1612`), water reflection
resolution (`reflectionResolutionScale` inert). The preset's rows for those land when their
Tier-2 setters exist (the strategy doc's "ships in two waves" note).

**Out of scope:** a temporal (FSR2-class) upscaler — first wave uses the existing linear blit +
an optional spatial sharpen; the temporal path is Tier 3.

---

## 2. Current wiring (verified integration points)

- **The engine already renders at an arbitrary resolution and blit-upscales to the window.**
  Per-frame, `engine.cpp:1226–1248` calls `Renderer::resizeRenderTarget(rw, rh)` with the
  editor-viewport size (editor) or the play-mode resolution (`getPlayModeWidth/Height`, default
  1920; `editor.h:476`). `Renderer::blitToScreen(...)` (`renderer.cpp:2217`) is the sole write to
  the default framebuffer (play mode, `engine.cpp:1900`) and **already contains aspect-preserving
  scale logic** with `GL_LINEAR` when render-res ≠ window-res (`renderer.cpp:2241–2265`). So
  rendering smaller and upscaling is an *existing capability*; `renderScale` only changes the
  numbers fed in.
- **All scene/post render targets are sized from `m_windowWidth × m_windowHeight`** in
  `initFramebuffers` (`renderer.cpp:515`) and `resizeRenderTarget` (`renderer.cpp:4429`) — except
  bloom (½), god-rays (½), luminance (fixed 256²), all already fractional. So scaling the value
  passed to `resizeRenderTarget` scales the whole scene+post chain coherently, MSAA included
  (the MSAA resolve at `renderer.cpp:892` blits src-rect→dst-rect at whatever size the FBOs are).
- **The composite lands in `m_outputFbo` (LDR RGBA8), not framebuffer 0** (`renderer.cpp:1400`);
  editor samples it as a texture via ImGui (`getOutputTextureId`, `renderer.cpp:2208`), play mode
  blits it (`blitToScreen`). This is the natural internal→window scale boundary.
- **`renderScale` is dead-ended:** persisted (`settings.cpp:226/240`), clamped to [0.25, 2.0]
  (`settings.cpp:533`), UI slider present (`settings_editor_panel.cpp:240`), but
  `applyDisplay` explicitly does not apply it (`settings_apply.h:87–91`). No `Renderer`
  render-scale setter exists.
- **The settings→renderer apply path** is `applyRendererAccessibility` (`settings_apply.h:183`),
  today exposing only `setColorVisionMode` + `setPostProcessAccessibility` (the fog/god-rays/
  dynamic-GI/DoF/motion-blur wire, consumed in `endFrame`). AA/SSAO/bloom have setters
  (`renderer.cpp:2181/2170/2081`) driven only by editor UI / keybinds — **no preset drives them.**

---

## 3. T1a — render-scale design

### 3.1 Mechanism
Introduce a single source of truth for the **internal render resolution** = `round(window ×
clamp(renderScale, 0.25, 2.0))`, and feed *that* into the existing `resizeRenderTarget` path
instead of the raw window/play size. Present is unchanged — `blitToScreen` already upscales
`m_outputFbo` to the window.

- **Where the scale is applied:** at the two per-frame `resizeRenderTarget(rw, rh)` call sites in
  `engine.cpp:1234` (editor viewport) and `engine.cpp:1244` (play mode). Multiply `rw,rh` by the
  active `renderScale` before the call. Rationale: keeping the multiply at the engine call sites
  (not inside the renderer) means the renderer stays "render at the size I'm told" and the
  window/viewport→internal mapping stays in one place. A thin `Renderer::setRenderScale(float)` is
  **not** required for wave 1; the engine owns the arithmetic. *(If a later slice needs the
  renderer to know its own scale — e.g. for a sharpen pass' pixel size — add the setter then.)*
- **Present path:** unchanged. Play mode → `blitToScreen` (`renderer.cpp:2217`) linear-upscales
  `m_outputFbo` (now at internal-res) to the window. Editor mode → the ImGui viewport image
  samples `m_outputFbo` at the panel size (GPU bilinear). Both already scale.
- **Aspect ratio:** multiply *both* dimensions by the same `renderScale` → internal aspect ==
  window aspect → `blitToScreen`'s letterbox scale fills exactly, no bars (the min(scaleX,scaleY)
  path degenerates to a uniform scale when aspects match).

### 3.2 Optional spatial sharpen (quality, may defer within the wave)
Linear upscaling is soft. Add one full-screen **RCAS-style sharpen** pass (FSR1's spatial half —
no motion vectors, a portable GLSL kernel [strategy doc §5 T1a, src [2]]) between the composite
and present. *Reuse:* `m_screenQuad` (`renderer.h:611`) + the passthrough `screen_quad.vert.glsl`
(`renderer.cpp:352`); add one new `sharpen.frag.glsl`. If schedule-constrained, ship bilinear
first and add sharpen as a fast follow — the design keeps it isolated to one pass so it doesn't
block the scale itself.

### 3.3 Scope boundary: which buffers scale
Scaling `resizeRenderTarget`'s input scales every window-sized target coherently. The
already-fractional buffers (bloom ½, god-rays ½) become ½ *of the internal res* — correct (they
stay proportional). The fixed 256² luminance and the fixed-res shadow maps are unaffected —
correct (shadow res is a Tier-2 preset knob, not a render-scale concern). **No per-FBO special
casing needed.**

---

## 4. T1b — quality preset wiring

### 4.1 The apply function
Add `applyQualityPreset(QualityPreset, <renderer sink>)` in the settings-apply layer
(`settings_apply.*`), invoked wherever `applyDisplay` / `applyRendererAccessibility` run. First
wave maps only the knobs with existing setters + the render-scale value:

| Preset | renderScale | Anti-alias (`setAntiAliasMode`) | SSAO (`setSsaoEnabled`) | Bloom (`setBloomEnabled`) | Vol-fog + dyn-GI (perf-gate §4.2) |
|---|---|---|---|---|---|
| Low | 0.66 | `NONE` | off | off | off |
| Medium | 0.85 | `SMAA` | on | off | off |
| High | 1.0 | `TAA` | on | on | on |
| Ultra | 1.0 | `TAA` | on | on | on |
| Custom | *(untouched — individual toggles win)* |

`Custom` applies nothing (the player's individual settings stand). Values are a starting point,
tuned against the profiler CSV (§6) — not final.

### 4.2 Volumetric fog + dynamic GI perf-gate (small renderer change)
Today those two passes are gated only by the accessibility wire
(`m_postProcessAccessibility.volumetricFogEnabled`, `renderer.cpp:1307/1380`). To let Low drop
them for *performance* without touching *accessibility* semantics, add a `bool
m_qualityHeavyPostEnabled` (or a two-field perf struct) that is **AND-ed** with the existing gate:
a pass runs iff `accessibilityEnabled && qualityEnabled`. The preset sets the quality side; the
accessibility side stays authoritative (see §4.3). This is the only new renderer state in wave 1.

### 4.3 Accessibility precedence (invariant)
**INV-A11Y:** a preset must never re-enable a pass that accessibility disabled. Because the gate
is an AND (`a11y && quality`), a preset setting `quality=true` cannot override `a11y=false` — the
AND stays false. Apply order: accessibility applied after (or independently of) the preset, and
the runtime gate is the AND, so order doesn't matter. Pinned by a test (§6).

---

## 5. CPU / GPU placement (Rule 7)

| Work | Placement | Reason |
|---|---|---|
| `renderScale` clamp + internal-res arithmetic | **CPU** | one multiply per resize, a decision |
| Preset → setter mapping (`applyQualityPreset`) | **CPU** | branching / config apply |
| Reduced-res scene + post rendering | **GPU** | per-pixel (already GPU; fewer pixels) |
| Upscale blit + optional RCAS sharpen | **GPU** | per-pixel present-time filter |

This is the strategy doc's thesis in miniature: the CPU-side preset/scale decision is nearly
free; the GPU does *less* work (fewer pixels) — the correct direction for a GPU-bound weak part.

---

## 6. Verification plan

1. **Render-scale reduces GPU cost (profiler CSV).** Capture the meadow with `renderScale = 1.0`
   then `0.5`; `gpu,total` at 0.5 should fall to ≈ 25–30% of the 1.0 value (pixel-count scaling).
   *This validates the mechanism; it is not a weak-GPU emulation (strategy §8.1).*
2. **Release re-capture** for trustworthy absolute ms (strategy §8.2) — captured on a Release
   build via `--profile-log`, not the Debug+ASan visual-test.
3. **Preset apply is correct (headless unit test).** Following the existing `settings_apply`
   test pattern (`test_settings.cpp` / `test_atomic_write_routing.cpp`), assert
   `applyQualityPreset(Low, sink)` calls the sink with `AA=NONE, ssao=false, bloom=false,
   heavyPost=false` and the Low `renderScale`; likewise High/Ultra. No GL context needed (mock
   sink), mirroring `RendererAccessibilityApplySink`.
4. **INV-A11Y (headless unit test).** Apply `Ultra` (quality wants fog/GI on) with the
   accessibility wire fog/GI **off**; assert the effective runtime gate is off for both.
5. **Visual (play mode, pinned camera).** At `renderScale = 0.5`, play mode renders and presents
   an upscaled image, aspect preserved, no letterbox bars, no crash; at 1.0 pixel-identical to
   today. Verified under software rendering (llvmpipe) *and* hardware, per the local-ci renderer
   parity fix (2026-07-17).
6. **60 FPS floor unchanged at Ultra/1.0** on the RX 6600 (no regression when scale = 1.0 — the
   `resizeRenderTarget` input is `window × 1.0`, i.e. byte-identical to today's path).

---

## 7. Risks & out of scope

- **Risk — image softness at low `renderScale`.** Mitigated by the RCAS sharpen (§3.2) and the
  existing [0.25, 2.0] clamp. Bilinear-first is acceptable for the initial cut.
- **Risk — `renderScale = 1.0` must be a no-op.** The internal-res arithmetic at scale 1.0 must
  reproduce today's exact target size (round(window × 1.0) == window). Pinned by §6.6.
- **Risk — editor vs play mode.** Render-scale is primarily a play-mode/gamer concern. Editor
  mode *may* also scale its viewport render, but the default should keep the editor at 1.0 for
  authoring clarity; decision recorded here — apply `renderScale` in play mode; editor stays 1.0
  in wave 1.
- **Risk — MSAA + tiny internal res.** At `renderScale = 0.25` on a 4K window, internal res is
  ~1080p — still ample for MSAA. No special casing; the clamp floor protects the degenerate case.
- **Out of scope:** temporal upscaler (Tier 3); shadow/foliage/water preset rows (Tier 2, need
  new setters); dynamic (auto-adjusting) resolution — wave 1 is a *fixed* user/preset scale.

---

## 8. Reuse summary (Rule 3)

| Need | Reused thing |
|---|---|
| Render at reduced resolution | existing `resizeRenderTarget` per-frame drive (`engine.cpp:1234/1244`) |
| Upscale to window | existing `blitToScreen` linear-scale path (`renderer.cpp:2241`) |
| The scale value + UI + clamp + persistence | existing inert `DisplaySettings::renderScale` |
| Preset enum + settings UI | existing `QualityPreset` + `settings_editor_panel.cpp:225` |
| AA / SSAO / bloom control | existing `setAntiAliasMode/setSsaoEnabled/setBloomEnabled` |
| Sharpen pass geometry + vert | existing `m_screenQuad` + `screen_quad.vert.glsl` |
| Apply-layer pattern + mock-sink tests | existing `applyRendererAccessibility` + its sink |

New code is small: the internal-res multiply, `applyQualityPreset`, one AND-gate bool for
heavy post, and (optionally) one sharpen fragment shader.
