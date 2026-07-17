# Phase 10 — Tier 1: Render-scale + Quality presets (design)

**Status:** Signed off — cold-eyes converged (loop 1: two reviewers, factual-spine + design-quality,
caught the renderScale data-path + editor/play + preset gaps, all fixed, and FXAA folded in; loop 2:
confirmation caught + fixed the FXAA single-sample mechanism bug — `NONE`/`FXAA` now reconfigure the
scene FBO to 1 sample; final pass returned **0 substantive findings, "implementable as written"**).
Ready to implement.
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
- **T1c — the budget AA + sharpen stack:** a new **FXAA** anti-alias mode (§3.4) — cheap
  post-process AA for the weak/handheld tiers — and one **CAS** sharpen pass (§3.2) that both
  crisps the render-scale upscale and counters FXAA's mild blur. FXAA becomes the AA the
  Low/Medium presets select.

**Deferred to Tier 2 (needs new setters — out of scope here, named so the preset table is
honest):** shadow resolution / cascade count (fixed in `CascadedShadowMap`, no runtime setter),
foliage draw-distance/density (hardcoded `100.0f`, `engine.cpp:1612`), and the per-surface
water-reflection quality field (`reflectionResolutionScale`, inert). The preset's rows for those
land when their Tier-2 setters exist (the strategy doc's "ships in two waves" note). *(Note: the
water reflection **FBO** already scales with `renderScale` automatically under T1a — §3.1 — since
it is sized from the same resize-input; that is distinct from wiring the inert per-surface
`reflectionResolutionScale` field, which is the Tier-2 item.)*

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
Introduce a single source of truth for the **internal render resolution** = `round(resize-input ×
clamp(renderScale, 0.25, 2.0))`, where the resize-input is the play-mode render size
(`getPlayModeWidth/Height`, default 1920 — *not* necessarily the window). Feed *that* into the
existing `resizeRenderTarget` path instead of the raw size. Present is unchanged — `blitToScreen`
already upscales `m_outputFbo` to the window.

- **Where the scale is applied:** at the **play-mode** resize block only. Multiply `rw,rh` by the
  active `renderScale` once at the top of that block, *before* both the `resizeRenderTarget(rw,rh)`
  call (`engine.cpp:1246`) **and** the water-reflection resize on the next line (`engine.cpp:1247`,
  `m_waterFbo->resize(rw/4, rh/4, rw/4, rh/4)`). One factor therefore scales the whole scene+post chain **and**
  the water reflection FBO coherently — water getting cheaper too is desirable and free (it avoids
  decoupling the water resize from the same input). The **editor** viewport resize
  (`engine.cpp:1237`) is left at ×1.0 in wave 1 so authoring and mouse-picking stay pixel-exact
  (§7). Rationale: keeping the multiply at the engine call site (not inside the renderer) means the
  renderer stays "render at the size I'm told" and the internal-res mapping stays in one place; a
  `Renderer::setRenderScale()` is not required for wave 1. *(A later sharpen pass that needs the
  renderer to know its own scale can add the setter then.)*
- **Present path:** unchanged. Play mode → `blitToScreen` (`renderer.cpp:2217`) linear-upscales
  `m_outputFbo` (now at internal-res) to the window. Editor mode → the ImGui viewport image
  samples `m_outputFbo` at the panel size (GPU bilinear). Both already scale.
- **Aspect ratio:** multiply *both* dimensions by the same `renderScale` → internal aspect ≈
  window aspect → `blitToScreen`'s scale fills the window. Independent per-axis rounding can leave
  at most a ~1px letterbox bar at some scales (the `min(scaleX,scaleY)` path) — negligible.

### 3.2 The budget AA + sharpen stack (FXAA + CAS)
Two cheap full-screen passes on the composited LDR image give the weak/handheld tiers clean edges
*and* crisp texture — the classic budget-tier stack, and they pair with the upscaler (§3.1):
render smaller → upscale → **FXAA** (smooth edges) → **CAS** (claw back sharpness) → present.

- **FXAA pass (§3.4)** — Timothy Lottes' FXAA 3.11 at the max-quality preset. A single dependent
  full-screen pass, ~0.1–0.5 ms, no motion vectors, no multisample buffer — far cheaper than MSAA
  and the natural AA for the cheap tiers (§4.1). Detail in §3.4.
- **CAS sharpen pass** — one full-screen **Contrast-Adaptive-Sharpening** pass (AMD FidelityFX CAS
  / FSR1's RCAS half — a portable GLSL kernel, no motion vectors [strategy doc §5 T1a, src [2]]).
  It both restores the softness of the upscale *and* counters FXAA's mild blur, so the same pass
  serves both features.
- *Reuse:* `m_screenQuad` (`renderer.h:611`) + the passthrough `screen_quad.vert.glsl`
  (`renderer.cpp:352`); add `fxaa.frag.glsl` and `cas.frag.glsl`. Order after the composite
  (`renderer.cpp:1400`), before present. If schedule-constrained, CAS can follow FXAA in a fast
  second slice — the design keeps each isolated to one pass so neither blocks the render-scale.

### 3.3 Scope boundary: which buffers scale
Scaling `resizeRenderTarget`'s input scales every window-sized target coherently. The
already-fractional buffers (bloom ½, god-rays ½) become ½ *of the internal res* — correct (they
stay proportional). The fixed 256² luminance and the fixed-res shadow maps are unaffected —
correct (shadow res is a Tier-2 preset knob, not a render-scale concern). **No per-FBO special
casing needed.**

### 3.4 FXAA anti-alias mode (new `AntiAliasMode::FXAA`)
FXAA is a cheap post-process AA that operates on the final **LDR** image (post-tonemap) rather
than the HDR scene — so, unlike MSAA/TAA/SMAA, it needs **no multisample buffer and no history**.
It is the natural AA for weak/handheld tiers (~0.1–0.5 ms) and pairs with CAS (§3.2).

- **New enum value** `AntiAliasMode::FXAA` — **appended after `SMAA`** (`taa.h:19`) so existing
  serialized AA-mode ints stay stable. `setAntiAliasMode`'s log-name array
  (`renderer.cpp:2184`: `names[] = {"None","MSAA 4x","TAA","SMAA"}`, indexed by the mode int) must
  gain a `"FXAA"` entry in lockstep — else `names[4]` is an out-of-bounds read.
- **Scene path + a required sample-count fix.** FXAA operates on the resolved LDR image, so the
  scene must render **single-sample** for FXAA to be cheap. Today the scene FBO is created at a
  fixed 4× MSAA (`engine.cpp:111` → `initFramebuffers(..., 4)`), and `beginFrame`'s
  `else if (m_msaaFbo)` branch (`renderer.cpp:791–793`) binds it for **both** `NONE` and
  `MSAA_4X` — so `NONE` already *secretly pays full 4× MSAA cost*, and a naive FXAA-on-top would
  too (which would flatly contradict its value prop). Wave 1 therefore makes the scene FBO's
  **sample count follow the AA mode:** `setAntiAliasMode` (`renderer.cpp:2181`) reallocates
  `m_msaaFbo` to **1 sample** for `NONE`/`FXAA` and 4 for `MSAA_4X` whenever the effective count
  changes. This is a small, bounded renderer change — and a free bonus: it removes the latent
  `NONE`-renders-4×-MSAA waste. The resolve (`renderer.cpp:892`) still works at 1 sample (a plain
  blit). The FXAA pass then runs in `endFrame` after the composite (`renderer.cpp:1400`), reading
  the LDR color, writing a pong buffer, before CAS/present. TAA/SMAA are unaffected (they use the
  separate `m_taaSceneFbo`).
- **Algorithm + tuning (FXAA 3.11, Lottes):** the canonical single-pass luma-edge FXAA at
  `FXAA_QUALITY__PRESET 39` (the max edge-search radius). Ship these as the default constants
  (the well-known "sharp FXAA" tuning — avoids the "vaseline" over-blur), authored as named
  `const`s so they can become settings later without a rewrite:
  - `FXAA_QUALITY__SUBPIX = 0.333` — sub-pixel aliasing removal kept low so it smooths **edges**,
    not interior texture/HUD detail (the default 0.75 over-blurs).
  - `FXAA_EDGE_THRESHOLD = 0.125` — minimum local contrast to treat as an edge, so busy micro-
    texture (grass, gravel) isn't mistaken for jaggies.
  - `FXAA_EDGE_THRESHOLD_MIN = 0.0833` — dark-region floor (Lottes' recommended pairing).
  - Luma source: FXAA reads perceived luminance; feed it luma-in-alpha or compute luma in-shader
    from the sRGB LDR color (decide at implementation; the green-as-luma shortcut is acceptable
    for the cheap tier). *Pin the exact luma choice in the shader comment.*
- **CPU/GPU:** the pass is per-pixel → GPU (§5). The preset→mode selection is CPU.
- *Reuse:* `m_screenQuad` + `screen_quad.vert.glsl`; new `fxaa.frag.glsl`.

---

## 4. T1b — quality preset wiring

### 4.1 The apply function
Add `applyQualityPreset(QualityPreset, DisplaySettings&, RendererQualitySink&)` in the
settings-apply layer (`settings_apply.*`). First wave maps only the knobs with existing setters +
the render-scale value:

| Preset | renderScale | Anti-alias (`setAntiAliasMode`) | SSAO (`setSsaoEnabled`) | Bloom (`setBloomEnabled`) | Vol-fog + dyn-GI (perf-gate §4.2) |
|---|---|---|---|---|---|
| Low | 0.66 | `FXAA` | off | off | off |
| Medium | 0.75 | `FXAA` | on | on | off |
| High | 1.0 | `TAA` | on | on | on |
| Ultra | 1.0 | `TAA` | on | on | on |
| Custom | *(nothing applied)* | *(nothing)* | *(nothing)* | *(nothing)* | *(nothing)* |

The cheap tiers use **FXAA** (§3.4) — cheap post-AA ideal for weak/handheld GPUs, paired with CAS
(§3.2); High/Ultra use **TAA** (temporal, best quality). This supersedes the parent strategy
doc's §5 T1b `None/SMAA` for the cheap tiers (reason: FXAA gives Low actual anti-aliasing at ~½ MSAA
cost — the parent table is updated to match). Values are a starting point, tuned against the
profiler CSV (§6) — not final. **High and Ultra render identically in wave 1** — the shadow /
foliage / water rows that differentiate them are Tier-2 setters; until those land, Ultra == High
(not a table error).

- **Data path for `renderScale` (resolves the split across §3.1 and here):** `renderScale` is
  **not** renderer state — `applyQualityPreset` writes the preset's value into
  `DisplaySettings::renderScale` (the persisted setting). The engine's play-mode resize (§3.1)
  reads that value per-frame and does the multiply. AA / SSAO / bloom / heavy-post go to the
  renderer sink. So the function touches **two sinks**: the settings object (renderScale) and the
  renderer (the four toggles).
- **Invocation site + runtime behaviour:** `applyQualityPreset` runs from the settings-apply
  entry point — on **startup load** and on **settings-panel commit** (the same place
  `applyDisplay` / `applyRendererAccessibility` are dispatched). A runtime preset change therefore
  re-runs the apply immediately; the toggle setters take effect next frame, and the new
  `renderScale` takes effect on the next play-mode resize (next frame).
- **`Custom` transition:** `Custom` applies nothing (the player's individual toggles stand).
  Selecting a **named** preset in the settings panel applies that row *and* stores
  `qualityPreset = <that preset>`. Conversely, hand-editing any individual knob (AA / SSAO /
  bloom / renderScale) sets `qualityPreset = Custom`, so a later re-apply on load never silently
  clobbers a manual tweak. (This mirrors how a graphics-settings "Custom" tier is conventionally
  entered.)

### 4.2 Volumetric fog + dynamic GI perf-gate (small renderer change)
Today those two passes are gated only by the accessibility wire
(`m_postProcessAccessibility.volumetricFogEnabled`, `renderer.cpp:1307/1380`). To let Low drop
them for *performance* without touching *accessibility* semantics, add a `bool
m_qualityHeavyPostEnabled` (or a two-field perf struct) that is **AND-ed** with the existing gate:
a pass runs iff `accessibilityEnabled && qualityEnabled`. The preset sets the quality side; the
accessibility side stays authoritative (see §4.3). This bool is the only new *persistent* renderer
state in wave 1 (the §3.4 sample-count change is behavior in `setAntiAliasMode`, not a new member).

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
| Upscale blit + FXAA + CAS sharpen passes | **GPU** | per-pixel present-time filters |

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
   `applyQualityPreset(Low, display, sink)` writes `display.renderScale ==` the Low value **and**
   calls the renderer sink with `AA=FXAA, ssao=false, bloom=false, heavyPost=false` (renderScale
   lands on the `DisplaySettings` object, the four toggles on the sink — §4.1); likewise
   High/Ultra (`AA=TAA`). No GL context needed (mock sink), mirroring `RendererAccessibilityApplySink`.
4. **INV-A11Y (headless unit test).** Apply `Ultra` (quality wants fog/GI on) with the
   accessibility wire fog/GI **off**; assert the effective runtime gate is off for both.
5. **Visual (play mode, pinned camera).** At `renderScale = 0.5`, play mode renders and presents
   an upscaled image, aspect preserved, no letterbox bars, no crash; at 1.0 pixel-identical to
   today. Verified under software rendering (llvmpipe) *and* hardware, per the local-ci renderer
   parity fix (2026-07-17).
6. **60 FPS floor unchanged at Ultra/1.0** on the RX 6600 (no regression when scale = 1.0 — the
   resize-input is `round(resize-input × 1.0)` == its current value, i.e. byte-identical to the
   current path).
7. **FXAA smoke + visual.** `AntiAliasMode::FXAA` produces a stable, edge-smoothed image with no
   crash and no interior-texture over-blur (the low subpix keeps text/HUD crisp); a GPU smoke test
   (like the other shader-parity tests) confirms the `fxaa.frag.glsl` pass runs and a hard vertical
   edge is measurably softened vs `NONE`. Because `NONE`/`FXAA` now render single-sample (§3.4),
   FXAA's GPU cost is a sub-millisecond add while *removing* the old 4× MSAA cost — net cheaper
   than today's `NONE` (profiler CSV).

---

## 7. Risks & out of scope

- **Risk — image softness at low `renderScale`.** Mitigated by the CAS sharpen (§3.2) and the
  existing [0.25, 2.0] clamp. Bilinear-first is acceptable for the initial cut.
- **Risk — `renderScale = 1.0` must be a no-op.** The internal-res arithmetic at scale 1.0 must
  reproduce today's exact target size (round(resize-input × 1.0) == resize-input). Pinned by §6
  item 6.
- **Risk — editor vs play mode.** Render-scale is primarily a play-mode/gamer concern. Editor
  mode *may* also scale its viewport render, but the default should keep the editor at 1.0 for
  authoring clarity; decision recorded here — apply `renderScale` in play mode; editor stays 1.0
  in wave 1.
- **Risk — MSAA + tiny internal res.** At `renderScale = 0.25` on a 4K window (with `MSAA_4X`
  selected), internal res is ~1080p — still ample for MSAA. No special casing; the clamp floor
  protects the degenerate case.
- **Code-side follow-up (not a doc defect).** The `applyDisplay` doc-comment
  (`settings_apply.h:87–91`) frames render-scale as "a Renderer concern" and the quality preset as
  "consumed by individual subsystems (shader variants / LOD bias / shadow resolution)" — a
  different model than this design's engine-owned scale + centralized `applyQualityPreset`. Update
  that comment when this ships so it stops mis-describing the wiring.
- **Out of scope:** temporal upscaler (Tier 3); shadow/foliage/water preset rows (Tier 2, need
  new setters); dynamic (auto-adjusting) resolution — wave 1 is a *fixed* user/preset scale.

---

## 8. Reuse summary (Rule 3)

| Need | Reused thing |
|---|---|
| Render at reduced resolution | existing `resizeRenderTarget` per-frame drive (`engine.cpp:1237` editor / `1246` play) |
| Upscale to window | existing `blitToScreen` linear-scale path (`renderer.cpp:2241`) |
| The scale value + UI + clamp + persistence | existing inert `DisplaySettings::renderScale` |
| Preset enum + settings UI | existing `QualityPreset` + `settings_editor_panel.cpp:225` |
| AA / SSAO / bloom control | existing `setAntiAliasMode/setSsaoEnabled/setBloomEnabled` |
| FXAA + CAS pass geometry + vert | existing `m_screenQuad` + `screen_quad.vert.glsl` |
| Apply-layer pattern + mock-sink tests | existing `applyRendererAccessibility` + its sink |

New code is small: the internal-res multiply, `applyQualityPreset`, one AND-gate bool for
heavy post, the `AntiAliasMode::FXAA` enum value (+ its `names[]` entry), the dynamic scene-FBO
sample-count in `setAntiAliasMode` (which also fixes the latent `NONE`=4×-MSAA waste), and two
fragment shaders (`fxaa.frag.glsl`, `cas.frag.glsl`).
