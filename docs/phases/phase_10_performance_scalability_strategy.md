# Phase 10 — Performance Scalability Strategy (weak & mainstream gamer hardware)

**Status:** Draft — pending `/cold-eyes` convergence, then user direction on which tiers to implement.
**Author intent:** Answer the user's request (2026-07-17) to "ultrathink on how we can
improve performance considering my hardware and the general hardware of gamers out
there." This is a **strategy / prioritisation** document, not a single-feature spec.
Each tier item, once selected, gets its own detailed design doc before implementation
(project Rule 1).

---

## 1. Problem & goal

The **60 FPS floor is a hard project requirement** (CLAUDE.md). It is currently met with
enormous headroom on the dev machine (RX 6600: ~181 FPS in-editor Release) but the dev
machine is one of the *strongest* GPUs our likely players own. The meadow benchmark scene
(3D_E-0027) exists precisely to expose where the frame budget goes so we can hold 60 FPS on
hardware that is **3–6× weaker** than the RX 6600.

**Goal:** a *scalability system* — one that lets the same scene render at Ultra on an RX 6600
and still hold 60 FPS on a Steam Deck or a GTX 1650, by (a) doing less GPU work per pixel and
(b) rendering fewer pixels, both driven by a quality tier the player (or an auto-detect)
picks. This is deliberately the **opposite** of "push more onto the GPU": on the target
hardware the GPU is the bottleneck, so the wins come from *reducing* GPU load. (Pushing work
CPU→GPU is the right move only in the CPU-bound case — see §7.)

**Non-goal:** raising the *peak* ceiling on strong GPUs. This is about the *floor* on weak
ones.

---

## 2. Hardware targets

Steam's June 2026 hardware survey shows the installed base drifting toward **laptop and
handheld silicon**: the RTX 4060 *Laptop* GPU is now the single most common card (3.81%), and
a long, stubborn tail of weak parts persists — GTX 1650 (~2.73%), GTX 1060 (~1.62%), plus
integrated graphics and handhelds [1]. So "the general hardware of gamers out there" is not a
mid-high desktop card; it is a mid-range laptop GPU with a meaningful weak tail.

| Tier target | Representative GPU | Rough FP32 | Display | Frame budget @60 |
|---|---|---|---|---|
| **Dev / Ultra** | RX 6600 (RDNA2 desktop) | ~8.9 TFLOPS | 1080p+ | 16.6 ms (huge headroom) |
| **Mainstream / High** | RTX 4060 Laptop, RTX 3060 | ~10–15 TFLOPS* | 1080p | 16.6 ms |
| **Low-end / Medium** | GTX 1650, GTX 1060 | ~3–4 TFLOPS | 1080p | 16.6 ms |
| **Handheld / Low** | Steam Deck (RDNA2, 8 CU) | ~1.6 TFLOPS | 1280×800 | 16.6 ms |

*Laptop parts are power-limited, so real throughput is well below the desktop equivalents —
the survey trend toward mobile silicon means we should treat "High" as more constrained than
raw TFLOPS suggest.

The **Steam Deck (~1.6 TFLOPS, ~5.5× weaker than the RX 6600, at 1280×800)** is the concrete
low-end design point. If the meadow holds 60 FPS on the Deck, it holds nearly everywhere.

---

## 3. Measured cost structure (profile before optimise)

Captured with the `--profile-log` CSV logger (built 2026-07-17, commit 9507430) on the RX
6600 via `--visual-test`. **The GPU-timer values are real hardware timings** (GL timestamp
queries are immune to the ASan debug-build slowdown); the CPU-scope values in that build are
ASan-inflated ~13× and are used only for *relative* structure, not absolute cost.

**Per-pass GPU cost, RX 6600, meadow visual-test (steady-state interval average):**

| GPU pass | ms | Share | Note |
|---|---|---|---|
| **Scene** (opaque geometry + lighting) | ~9.9 | ~60% | dominant cost |
| **PostProcess** (SSAO + bloom + composite) | ~3.7 | ~23% | full-screen passes |
| **Terrain** | ~2.8 | ~17% | GGX ground |
| Foliage | ~0.09 | <1% | negligible *in this capture* |
| Water | ~0.02 | <1% | negligible — FBOs already ¼-res |
| **GPU total** | ~16.5 | 100% | ≈ 60 FPS-equivalent on RX 6600 |

**The most important finding is a corrected assumption.** Before measuring, the water
reflection/refraction (two extra scene re-renders) was the presumed villain. Measured, water
is ~0.02 ms — because its FBOs are already quarter-resolution (`w/4 × h/4` = 1/16 the pixels,
`water_system.cpp:24`) and geometry-only. **The real GPU budget is the base scene render +
the post-process stack.** That is what scaling must attack first.

**Caveat (INV-CAVEAT-1):** the `--visual-test` camera path is a fixed fly-through and may not
frame the dense-grass or looking-across-water viewpoints a standing player would. Foliage and
water could be materially heavier from a worst-case player vantage. Before committing Tier-2
foliage/water work, capture from a **worst-case meadow vantage** (dense grass filling the
frame, water plane at a grazing angle) to confirm their true cost — do not conclude "foliage
is free" from the fly-through alone.

**On weak hardware the ranking is what scales, not the absolute ms.** A ~16.5 ms RX 6600 frame
becomes an estimated **~55–90 ms on a Steam Deck** (5.5× weaker, before the resolution
difference helps) — i.e. **11–18 FPS** without scaling. Closing that gap needs both fewer
pixels (resolution scaling) and cheaper pixels (feature gating).

---

## 4. Current scalability infrastructure (what exists vs. what's missing)

Verified by direct source inspection (2026-07-17). This determines effort: **reuse before
rewriting** (Rule 3).

### Already built and wired
- **Per-feature render toggles** on `Renderer`: bloom, SSAO, anti-alias mode
  (`None/MSAA_4X/TAA/SMAA`), SDSM, colour grading, POM, skybox — runtime setters, today driven
  only by dev hotkeys / editor / `--isolate-feature` (`renderer.h:221–298`, `engine.cpp:814`).
- **Accessibility-gated passes** (persisted + applied): volumetric fog, god rays, dynamic
  (froxel) GI, DoF, motion blur, fog — via `PostProcessAccessibilitySettings`
  (`settings.h:278`, `settings_apply.h:212`).
- **Water shader-complexity tiers** via `FormulaQualityManager` (`quality_manager.h`), wired
  for water/caustics only (`engine.cpp:1548`).
- **CPU frustum culling** everywhere (scene, shadows, foliage chunks) with `CullingStats`
  (`frustum.h`, `renderer.cpp:3149`).
- **Water FBOs already at ¼ resolution** (`water_system.cpp:24`) — an optimisation already in
  place, which is why water measured cheap.
- **TAA + motion-vector G-buffer** (`taa.h`, `test_motion_vectors_mrt`) — the prerequisite for
  a *temporal* upscaler (FSR2-class) already exists.

### Built but INERT (highest leverage — scaffolding with no consumer)
- **`QualityPreset { Low, Medium, High, Ultra, Custom }`** persisted in `DisplaySettings`
  (`settings.h:82`), editable in the settings UI (`settings_editor_panel.cpp:225`) — but **no
  subsystem reads it.** Verified: `qualityPreset` appears only in serialize/validate/equality
  and the UI. Zero render consumers.
- **`renderScale` (float, clamped [0.25, 2.0])** persisted, UI slider present
  (`settings.h:106`) — **no consumer.** Its own doc-comment says "applied before upscaling to
  the window size," but no upscaling or render-scale code exists. Verified zero consumers.

### Missing entirely
- No dynamic-resolution / upscaling path (renderScale inert; final blit is a straight copy).
- No runtime-configurable shadow resolution / cascade count (2048×4 hardcoded, `renderer.cpp:577`).
- No configurable water-reflection resolution (`/4` hardcoded) and no water on/off toggle.
- No foliage density scalar, draw-distance setting, or LOD ladder (draw distance hardcoded
  `100.0f`, shadow-cast `30.0f`; `engine.cpp:1612`, `foliage_renderer.cpp`).
- No occlusion culling — a `frustum_cull.comp.glsl` compute shader exists but is unwired
  (`m_mdiEnabled=false`, `renderer.h:774`).

**Consequence:** the two single highest-leverage items (a working quality preset and
resolution scaling) are *mostly plumbing* — the storage, the UI, the clamps, and most of the
per-feature setters already exist. They were designed and then never connected.

---

## 5. Strategy — prioritised tiers

Ordered by **(impact on the weak-HW floor) ÷ (effort)**. Each item names what it reuses.

### Tier 1 — Wire the inert scaffolding (biggest win, least new code)

**T1a. `renderScale` → dynamic/fixed resolution scaling + upscale.**
Render the 3D scene into an internal FBO sized `renderScale × window`, then upscale to the
window on the final blit. `renderScale` is already persisted, clamped, and UI-exposed — this
connects it. At 0.75× a frame costs ~56% of the pixels; at 0.5×, ~25%. This is the **single
biggest lever** for a GPU-bound weak machine because Scene + PostProcess (§3, ~83% of the
budget) both scale with pixel count.
- *First cut:* bilinear upscale + optional RCAS sharpen (FSR1's spatial pass — a portable
  GLSL shader, no motion vectors needed [2]).
- *Reuse:* the existing FBO/blit path (`Renderer::blitToScreen`), the persisted `renderScale`.
- *Later (Tier 3):* swap the spatial upscale for a **temporal** one (FSR2-class) using the
  **already-present TAA motion vectors** — much better image quality at the same cost.

**T1b. `QualityPreset` → a feature-gating profile.**
Make `Low/Medium/High/Ultra` actually set a bundle of the existing knobs. A single applied
mapping, e.g.:

| Feature (existing knob) | Low | Medium | High | Ultra |
|---|---|---|---|---|
| `renderScale` (T1a) | 0.5–0.66 | 0.75 | 1.0 | 1.0 |
| Anti-alias mode | None/SMAA | SMAA | TAA | TAA |
| SSAO | off | on | on | on |
| Bloom | off | on | on | on |
| Volumetric fog | off | off | on | on |
| Dynamic (froxel) GI | off | off | on | on |
| Shadow resolution (T2c) | 1024 | 1536 | 2048 | 4096 |
| Foliage draw distance (T2a) | 40 m | 70 m | 100 m | 140 m |
| Water reflection (T2b) | off/cheap | ¼-res | ¼-res | ½-res |

- *Reuse:* every row maps to an existing setter (bloom/SSAO/AA/fog/GI) or a Tier-2 knob. The
  preset is a small CPU-side apply function; `Custom` leaves the individual toggles free.
- *Interplay:* the accessibility wire (reduced-motion etc.) stays authoritative — a preset
  never re-enables something accessibility turned off (§8).

**T1c. Auto-detect a starting preset** (small, optional). On first run, pick a default preset
from GL renderer string / VRAM (already read by `MemoryTracker`) so a Deck/iGPU user starts at
Low instead of a 12 FPS Ultra. Player can override. *Reuse:* `MemoryTracker::getGpuTotalMB`,
the GL renderer string already logged at startup.

### Tier 2 — Structural knobs the preset needs (moderate new code)

**T2a. Foliage density + draw-distance scaling and a cheap LOD.**
Expose the hardcoded `100.0f` draw distance and add a density multiplier + a distance-based
billboard/reduced-blade LOD. Grass is the classic meadow killer even though the fly-through
capture didn't stress it (INV-CAVEAT-1) — verify from a worst-case vantage first.
*Reuse:* the existing distance-cull + shader distance-fade in `foliage_renderer`.

**T2b. Configurable water reflection resolution + on/off.**
Turn the hardcoded `/4` into a per-preset factor and allow Low to drop planar reflection
entirely (fall back to a cheap approximation). Low structural cost; only matters from
water-facing vantages (verify per INV-CAVEAT-1). *Reuse:* `WaterFbo::init/resize` already take
explicit dims — only the caller's fixed `/4` needs to become a variable.

**T2c. Configurable shadow resolution + cascade count.**
Thread `CascadedShadowConfig` (already parameterised, just constructed with defaults) through a
setter so the preset can pick 1024×3 (Low) … 4096×4 (Ultra). *Reuse:* the config struct
already exists; only the hardcoded default construction blocks it.

### Tier 3 — Advanced (larger, later; each its own design doc)

**T3a. Temporal upscaler (FSR2-class).** Replace T1a's spatial upscale with a temporal one
driven by the existing motion-vector G-buffer + TAA history. Best image quality per rendered
pixel; the motion-vector infrastructure already exists, which is the hard prerequisite [2].
**T3b. GPU occlusion culling (Hi-Z / the unwired `frustum_cull.comp.glsl`).** Cuts the Scene
pass (the §3 dominant cost) when the meadow has occluders; less relevant for open terrain,
more for the biblical-structure interiors that are the project's primary use case.

---

## 6. Why this order (impact ÷ effort)

- **T1a + T1b are first because the scaffolding is already built** (§4 "inert") and they hit
  the dominant costs (§3: Scene + PostProcess scale with resolution; the feature gates remove
  whole passes). Low new code, highest floor impact.
- **T2 items are the knobs T1b's preset table references** — they are pulled in by Tier 1's
  design, not independent.
- **T3 items are high-effort image-quality/scaling refinements** that only pay off once the
  cheap wins are banked and measured.

A weak machine that renders at 0.6× resolution (T1a) with fog/GI/SSAO off and 1024 shadows
(T1b) is doing on the order of **a third of the RX 6600's per-frame GPU work** — the coarse
arithmetic that turns an ~11–18 FPS Deck frame (§3) into a 60 FPS one. Exact numbers must come
from a real Deck-class measurement, or from the RX 6600 *simulating* a weak GPU by forcing a
low `renderScale` and reading the profiler CSV (§8).

---

## 7. CPU / GPU placement (Rule 7)

| Work | Placement | Reason |
|---|---|---|
| Preset → feature apply, auto-detect | **CPU** | branching / config / one-shot decision |
| Resolution-scale FBO sizing, upscale, RCAS sharpen | **GPU** | per-pixel |
| Foliage LOD selection (per chunk) | **CPU** | sparse per-chunk branch (already CPU-culled) |
| Foliage instance draw | **GPU** | per-instance, already instanced |
| Shadow render at chosen resolution | **GPU** | per-texel |
| Occlusion cull (T3b) | **GPU** | per-instance depth test (Hi-Z) |

This is where the user's "push onto the GPU" instinct is correct: when a scene is **CPU-bound**
(draw-call submission, culling, scene traversal — e.g. the water path re-traverses the scene
3× on the CPU), moving that work to the GPU (GPU culling, instanced/indirect draws) is the fix.
Our *measured* meadow is GPU-bound on the RX 6600, so Tier 1 targets GPU load first; the
CPU→GPU offload (T3b) matters more for the dense biblical interiors.

---

## 8. Verification plan

Every tier item is measured with the **profiler CSV** (`--profile-log`, now functional) — this
is the concrete "profile before/after" instrument.

1. **Weak-GPU simulation on the dev machine.** Force `renderScale` low and/or a Low preset,
   capture the meadow CSV, and confirm the per-pass GPU ms drops as predicted. This lets us
   validate scaling *without* a physical Deck. *Check:* `gpu,total` at 0.5× renderScale is
   ≈ 25–30% of the 1.0× value.
2. **Worst-case vantage capture** (INV-CAVEAT-1) before Tier 2: dense grass + grazing water,
   to get foliage/water's true cost, not the fly-through's.
3. **Per-preset acceptance:** each preset holds its target frame budget from the worst-case
   vantage (Low ≤ 16.6 ms on a simulated weak profile; Ultra unchanged on RX 6600).
4. **Regression gate (existing 3D_E-0030):** the meadow benchmark + profiler CSV is already the
   planned automated perf-regression guard-rail. Tier 1 makes it actionable — the CSV writer it
   depends on now exists.
5. **Accessibility non-regression:** presets never override an accessibility-off pass; the
   reduced-motion fog/GI gates stay authoritative (test: apply Ultra with reduce-motion on →
   fog/GI stay off).

---

## 9. Risks & out of scope

- **Risk — image quality at low `renderScale`.** Bilinear upscale is soft; mitigate with RCAS
  sharpen (T1a) and move to temporal (T3a) later. A hard lower clamp (already [0.25, 2.0]) keeps
  it from degenerating.
- **Risk — preset/accessibility precedence bugs.** One apply-order rule (accessibility wins),
  pinned by a test (§8.5).
- **Risk — concluding from the fly-through** that foliage/water are free (INV-CAVEAT-1).
  Mitigated by the worst-case capture gate.
- **Out of scope:** frame generation (FSR3 interpolation) — latency/quality tradeoffs not worth
  it for an exploration engine; raising the strong-GPU peak; Vulkan/RT paths (separate roadmap).

---

## 10. Sources

1. Steam Hardware & Software Survey, June 2026 — GPU distribution (RTX 4060 Laptop #1; GTX
   1650 ~2.73%, GTX 1060 ~1.62%; drift toward mobile/handheld silicon).
   https://store.steampowered.com/hwsurvey/videocard/ ;
   https://www.tomshardware.com/news/gtx-1650-steam-hardware-survey
2. AMD FidelityFX Super Resolution (FSR) — GPUOpen: FSR1 spatial (EASU/RCAS, no motion
   vectors) vs FSR2/3 temporal (reuses TAA motion-vector path); open-source.
   https://gpuopen.com/amd-fsr-upscaling/ ; https://gpuopen.com/fidelityfx-super-resolution-3/
