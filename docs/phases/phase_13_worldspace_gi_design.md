# Phase 13 — World-Space Global Illumination (Design Doc)

| Field | Value |
|---|---|
| Phase | 13 — Advanced Rendering (GI feature slice) |
| Feature | World-space dynamic GI (DDGI-lite) + baked low-end tier |
| Status | `draft` — pending cold-eyes convergence + sign-off |
| Owners | milnet01 |
| Start (target) | after the R4 dynamic-GI (SSGI) regression closed (commit a24a6e4) |
| Target completion | TBD — sized post-review (5 slices, see §4) |
| Roadmap source | `ROADMAP.md` Phase 13 — Advanced Rendering (~line 1094); GI feature row |
| Supersedes | R4 SSGI (Variant A, screen-space) for the off-screen / all-lights case — R4 stays as the cheap on-screen fast-path; see §2.3 |
| Peer docs | none — first Phase 13 GI slice; future glossy/RT GI slices reference this |

---

## 1. Goal

Indirect diffuse lighting that (a) captures **all relevant lights, including
off-screen ones** — the thing screen-space R4 fundamentally cannot do; (b)
supports **at least one bounce, adjustable**, with cheap multi-bounce; (c) runs
**inside the 60 FPS floor on the RX 6600** (hard requirement); and (d) **looks as
good as possible across the hardware range** by offering a baked tier for low-end
machines and static scenes (Tabernacle / Solomon's Temple walkthroughs) and a
fully-dynamic tier for games.

Diffuse-only, no hardware ray tracing (the engine is GL 4.5; Vulkan/RT is a later
Phase 13 slice). Raster / compute GI only.

## 2. Scope

### 2.1 In scope

- **One unified system, two tiers** sharing the same runtime read path:
  - **Baked tier** — bake probes offline (`RadiosityBaker`), sample at runtime,
    ~zero per-frame GPU cost. Low-end HW + static scenes.
  - **Dynamic tier** — the *same* `SHProbeGrid`, refreshed each frame from a
    reflective-shadow-map (RSM) injection + temporal feedback. Games, dynamic
    lights/geometry.
  - **User toggle: Baked / Dynamic / Off.**
- **Off-screen / all-lights capture** via RSM: a flux colour attachment on each
  shadow-casting light's existing shadow pass (sun CSM + point/spot cube), so the
  light records what *it* lights regardless of camera.
- **Runtime probe injection**: scatter RSM flux texels (VPLs — virtual point
  lights) into the probe grid's SH coefficients.
- **Cheap multi-bounce** via temporal feedback (this frame's probe irradiance
  feeds next frame's injection; converges over a few frames).
- **Adjustable**: bounce strength / intensity dial; quality presets (grid
  density + RSM resolution + VPL sample count).
- **Light-leak control**: per-probe visibility weighting (DDGI Chebyshev-style
  depth test), reusing the existing `u_shNormalBias` anti-leak as the floor.

### 2.2 Out of scope (this slice)

- **Glossy / specular GI** (reflections). Diffuse irradiance only; specular stays
  on the existing IBL / SSR path. A future Phase 13 slice.
- **True surface lightmaps** (per-texel baked GI on walls; second-UV unwrap +
  lightmap atlas + per-mesh storage). Decision below; documented future option.
- **Hardware ray tracing / Vulkan path** — later Phase 13 slice.
- **Voxel cone tracing / VXGI** (Approach C, §13) — out, overkill for diffuse-only
  60 FPS.

### 2.3 Honest scope notes

- **R4 is not deleted.** R4 SSGI (screen-space, froxel cache, `u_dynamicGiEnabled`)
  stays as a cheap on-screen near-field contributor. This slice adds the
  world-space term that *also* covers off-screen/all-lights. The two must not
  double-count — see §6 and the `u_giStrength` / baked-floor reconciliation. **If
  reconciliation proves fiddly, R4 SSGI is disabled when the world-space dynamic
  tier is active** (one indirect-diffuse source at a time); decided in slice 4.
- **Surface lightmaps deliberately cut.** The user deferred the call; decision is
  **probes-only**, raising baked quality via denser grids + higher bake settings
  instead. Rationale (CLAUDE.md Rule 9 / YAGNI): surface lightmaps roughly double
  the authoring + storage work, mostly benefit fully-static AAA scenes, and dense
  volumetric probes are the industry-pragmatic 60 FPS choice. Recorded as a future
  option, not built now.
- **"All relevant lights"** = all *shadow-casting* lights (they already render a
  shadow pass we extend into an RSM). Non-shadow-casting fill lights do not inject
  GI; acceptable and matches the engine's existing shadow-cost model.

## 3. Architecture overview

### 3.1 Subsystem map

```
                    EXISTING (reused, unchanged read path)
  SHProbeGrid  ──7× RGBA16F 3D tex, units 17-23, L2 radiance-SH (9 coeff/ch)──┐
       ▲  setProbeIrradiance / upload / bind                                  │
       │                                                                      ▼
  RadiosityBaker (baked tier)                         scene.frag.glsl: evaluateSHGridIrradiance()
       │  offline iterative capture                        :599-665 → diffuseIBL term :1254-1281
       │                                                         (Ramamoorthi-Hanrahan A_ℓ eval)
       │
  ═══════════════════════════════ NEW (dynamic tier) ═══════════════════════════════
       │
  Shadow pass (CSM + point/spot)  ──+ RGBA16F FLUX attachment──►  RSM flux+depth+normal targets
       │  (today: depth-only — the one net-new FBO change)               │
       │                                                                 ▼
       │                                            gi_probe_inject.comp.glsl (NEW)
       │                                            scatter VPLs → SH coeff accumulation
       │                                            + temporal EMA vs last frame's probes
       │                                            + per-probe visibility (Chebyshev)
       └──────────────────────────────────────────────────────►  writes SHProbeGrid (runtime)
```

The decisive reuse: the **per-fragment SH read already ships and is free** — it is
the expensive-to-add half of GI on a forward renderer. Both tiers write the same
grid; the shader cannot tell baked from dynamic. The dynamic tier just refreshes
the grid contents each frame instead of once offline.

### 3.2 Cross-references to existing code (verified, file:line)

| Concern | Location | Note |
|---|---|---|
| Probe store + layout | `sh_probe_grid.h:30-37,53-56` | L2, 27 floats → 7× RGBA16F 3D tex, units 17-23; **radiance-SH** convention |
| Probe write/upload API | `sh_probe_grid.{h,cpp}` `setProbeIrradiance` / `upload` / `bind` / `isReady` | runtime-updatable — confirmed, not bake-only |
| CPU eval mirror | `sh_probe_grid` `evaluateIrradianceCpu()` | parity-pinned vs shader |
| Baked tier | `radiosity_baker.cpp:46-104` | iterative bounce bake, `RadiosityConfig{maxBounces,convergenceThreshold,normalBias}` |
| Shader read site | `scene.frag.glsl:599-665` (`evaluateSHGridIrradiance`), `:1254-1281` (integration) | returns `E·INV_PI`; replaces cubemap diffuse when `u_hasSHGrid` |
| R4 dynamic read | `scene.frag.glsl:1297-1303` | `ambient += u_giStrength·gi.a·kD·gi.rgb·albedo·ao`, gated by `u_dynamicGiEnabled` |
| CSM shadow FBO | `cascaded_shadow_map.cpp:38-43` | **depth-only** `GL_DEPTH_COMPONENT24` 2D_ARRAY, drawbuffer `NONE` → add flux attachment here |
| Point/spot shadow FBO | `point_shadow_map.cpp:21-48` | **depth-only** cube, per-face layer attach → add flux attachment here |
| Froxel/GI dispatch host | `volumetric_fog_pass.{h,cpp}`; `renderer.cpp:1373-1398` (dispatch), `:1574-1577` (swap) | pattern to mirror for the probe-inject dispatch + ping-pong |
| GL gotcha | memory `gl-compute-in-composite-unit0-clobber` | inject compute must dispatch **before** composite binds texture units |

### 3.3 Event / data surfaces

- No new event-bus events for the render path (per-frame GPU work, not gameplay).
- One settings surface (`Settings::rendering.giMode` ∈ {Off, Baked, Dynamic} +
  `giStrength`, `giQuality`) → an apply-sink mirroring the existing
  `dynamicGiEnabled` wiring (`renderer.cpp:1394-1396` reduced-motion path is the
  template for the gating flags).

## 4. Steps / slices

Each slice is independently testable, lands behind a flag, and carries a perf gate
(≤ its budget on the RX 6600). Order chosen so the grid is never left half-written.

| # | Slice | Deliverable | Gate |
|---|---|---|---|
| G1 | **RSM flux attachment** | Add RGBA16F flux (albedo·radiance) colour attachment to CSM + point/spot shadow FBOs; shadow shaders write flux alongside depth. Off by default (attachment unused until G2). | FBO completeness test; existing shadow tests still green; flux readback matches a CPU reference for one directional light |
| G2 | **Runtime probe injection** | `gi_probe_inject.comp.glsl` — scatter RSM flux VPLs into SH coeffs, write `SHProbeGrid` each frame (single bounce, no temporal yet). | GPU parity vs `gi_math`-style CPU mirror of the SH projection; perf gate on inject dispatch |
| G3 | **Temporal multi-bounce + leak fix** | EMA-blend this frame's injection with last frame's probe irradiance (feedback = extra bounces); per-probe Chebyshev visibility weight. | convergence test (static scene → stable after N frames); leak test (probe behind wall does not lift shadowed side); CPU/GPU parity (§5) |
| G4 | **Tier toggle + R4 reconciliation** | `Settings::rendering.giMode` (Off/Baked/Dynamic) + apply-sink; reconcile with R4 SSGI + baked floor so indirect diffuse is counted once. | settings round-trip test; no-double-count test (Dynamic vs Baked vs Off luminance bounds); full-frame perf gate ≤ budget |
| G5 | **Quality presets + accessibility** | grid-density / RSM-res / VPL-count presets; accessibility (reduced-motion freezes the grid; intensity dial; no flashing). | preset perf gates; reduced-motion freeze test; Formula-Workbench-fit any tuning constants |

## 5. CPU / GPU placement (per CLAUDE.md Rule 7)

| Work | Placement | Reason |
|---|---|---|
| RSM flux render | **GPU** | per-fragment, in the existing shadow pass — free piggyback |
| VPL → SH projection / scatter | **GPU** (compute) | per-froxel/per-probe, massively parallel; matches the R4 inject pattern |
| Temporal EMA blend | **GPU** (compute) | per-probe, per-frame |
| Per-probe visibility (Chebyshev) | **GPU** (compute) | per-probe sample of RSM depth |
| Per-fragment SH evaluation | **GPU** (already shipped) | per-pixel |
| Grid placement / config / mode switch / preset selection | **CPU** | sparse, decision/IO, once per scene or per settings change |
| Offline radiosity bake driver | **CPU** (drives GPU capture) | branching convergence loop; `radiosity_baker.cpp` already this shape |

**Dual-impl parity (Rule 7):** the SH projection + EMA blend get a GL-free CPU
mirror in a `gi_probe_math.h` (mirroring the existing `gi_math.h`), pinned by a
parity test (GPU readback == CPU within tolerance) — the same discipline that
pins `giSliceCoord` across its three copies today.

## 6. Performance budget

Target: total GI cost **≤ ~1.5 ms** on the RX 6600 at 1080p (balanced preset),
inside the 16.6 ms frame. Rough split (to be confirmed by the G2/G3/G4 gates):

| Stage | Budget (balanced) | Notes |
|---|---|---|
| RSM flux render | ~0.2 ms | piggybacks shadow pass; cost = extra colour writes |
| Probe injection (VPL scatter) | ~0.6 ms | scales with grid density × RSM sample count |
| Temporal EMA + visibility | ~0.3 ms | per-probe |
| Per-fragment SH read | 0 ms (already in budget) | shipped |
| **Headroom / margin** | ~0.4 ms | |

- **Baked tier cost ≈ 0** per frame (read-only) — the low-end answer.
- **Double-count guard:** baked floor (first bounce) + R4 SSGI (on-screen) +
  world-space dynamic must sum to plausible indirect, not 3× it. `u_giStrength`
  and the G4 reconciliation bound this; the no-double-count test is the gate.
- Benchmark gates follow the project pattern — **and per the bug this slice's
  sibling just fixed, GPU perf-gate tests must not assume GL 4.6 and must be
  CI-safe under software GL** (Mesa llvmpipe, GLSL 4.50 max). Timing assertions
  gate off software renderers; correctness assertions always run.

## 7. Accessibility

- **Motion:** reduced-motion freezes the probe grid (no temporal flicker on
  moving lights) — reuse the R4 reduced-motion path (`renderer.cpp:1394-1396`:
  `alpha=0, decay=0` ⇒ cache frozen). GI must never flash or strobe.
- **Visual:** intensity dial (`giStrength`) doubles as a comfort control; GI off
  is always a valid, fully-lit fallback (baked floor or flat ambient).
- **Cognitive load:** three named modes (Off / Baked / Dynamic), not a wall of
  sliders; quality is a single preset enum with an "Auto (detect HW)" default.

## 8. Testing strategy

- **Per-slice unit/parity (CPU-side, GL-free):** SH projection mirror, EMA blend
  mirror, slice/probe coordinate helpers — `gi_probe_math.h` pinned by parity
  tests, mirroring `gi_math.h` / `test_gi.cpp`.
- **GPU verification (headless 4.5 context):** shaders compile+link (the
  `GiGpuTest` pattern — **and these run under CI's software GL, so author them
  against GLSL 4.50, ARB-suffixed builtins, per the new `ShaderLintStrict`
  gate**); flux readback; probe write readback vs CPU mirror.
- **Behaviour:** convergence (static scene stabilises), off-screen capture (a
  light outside the frustum still contributes), leak test (wall blocks bleed),
  no-double-count (luminance bounds across the three modes).
- **Perf gates:** per-slice dispatch budgets + full-frame budget, CI-safe
  (software-GL timing skip, correctness always).
- **Coverage gap acknowledged:** visual quality is judged by eye + reference
  screenshots, not a numeric metric; the tests pin *correctness and budget*, not
  *beauty*.

## 9. Dependencies

- **Existing, consumed:** `SHProbeGrid`, `RadiosityBaker`, `cascaded_shadow_map`,
  `point_shadow_map`, `VolumetricFogPass` dispatch host, `Settings` + apply-sink
  pattern, `scene.frag.glsl` read site. All verified present (§3.2).
- **New, built here:** `gi_probe_inject.comp.glsl`, `gi_probe_math.{h}` (CPU
  mirror), the RSM flux attachment on the two shadow FBOs, the `giMode` settings
  surface.
- **No new external libraries.** No new third-party deps (CLAUDE.md Rule 8 — none
  needed).

## 10. References

- Majercik, Guertin, Nowrouzezahrai, McGuire — *Dynamic Diffuse Global
  Illumination with Ray-Traced Irradiance Fields* (DDGI, 2019) — the probe +
  visibility model adapted here to a raster RSM injection.
- Crassin et al. / NVIDIA — RSM (*Reflective Shadow Maps*, Dachsbacher & Stamminger
  2005) — the off-screen/all-lights flux capture.
- Ramamoorthi & Hanrahan — *An Efficient Representation for Irradiance Environment
  Maps* (2001) — the L2 SH irradiance evaluation already in `scene.frag.glsl`.
- Unreal Lightmass — Volumetric Lightmap / indirect lighting cache — the
  conceptual analogue of the baked tier (probes, not surface lightmaps).
- (Verify exact citations during cold-eyes; INV/claims here are design intent,
  not yet line-checked beyond the §3.2 source map.)

## 11. Open questions (for cold-eyes / sign-off)

1. **RSM injection budget on RX 6600** — is ~0.6 ms realistic at the balanced grid
   density, or does VPL count need a Workbench-fit cap? (Settle at G2.)
2. **R4 SSGI coexistence vs replacement** — keep R4 as on-screen near-field, or
   disable it under Dynamic? (Settle at G4 by the no-double-count test; §2.3.)
3. **Grid placement for large scenes** — single grid vs cascaded/clipmap probes
   for the Temple-scale walkthroughs. (May defer to a follow-up slice.)
4. **Flux attachment memory** — RGBA16F on every cascade + cube face; confirm the
   VRAM cost is acceptable at the default shadow resolutions.

## 12. Non-goals (explicitly out)

Glossy GI, surface lightmaps, HW ray tracing, Vulkan path, VXGI/voxel GI,
non-shadow-casting-light injection. Each is a separate future decision, not a
silent omission.

## 13. Considered alternatives

- **B — RSM + Light-Propagation Volumes (generalized "Froxel Bounce").** Cheaper
  update, blurrier, more leak-prone, and needs a *new* per-fragment read path
  (the LPV grid) — whereas Approach A reuses the shipped SH read. Kept as a
  fallback if probe injection proves too costly.
- **C — Voxel cone tracing / VXGI-lite.** Most capable (sharp, glossy,
  fully-dynamic via per-frame voxelization) but ~3–5 ms + heavy VRAM + a large new
  subsystem — overkill for a diffuse-only 60 FPS goal.
- **Chosen: A — DDGI-lite** (dynamic SH probes + RSM injection + temporal
  feedback): reuses the expensive half (the read), adds only the flux attachment +
  a compute pass, and folds the baked tier in for free.

## 14. Change log

- 2026-06-24 — initial draft; source map (§3.2) verified against current `main`
  (post commit a24a6e4). Pending cold-eyes.

## 15. Cold-eyes loop log

_(to be appended as loops run — fresh reviewer per loop, no authoring context,
loop to zero verified findings per CLAUDE.md Rule 14 / project Rule 9)_
