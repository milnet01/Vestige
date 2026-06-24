# Phase 13 — World-Space Global Illumination (Design Doc)

| Field | Value |
|---|---|
| Phase | 13 — Advanced Rendering (GI feature track) |
| Feature | World-space dynamic GI (DDGI-lite — Dynamic Diffuse Global Illumination) + baked low-end tier |
| Status | `implementing` — G1 (RSM flux attachment) shipped 2026-06-24 (meshes + foliage; terrain flux split to its own slice, see §4). Design cold-eyes converged (6 loops, 2026-06-24), signed off by Claude per the project's delegated-sign-off convention. G2–G5 pending. |
| Owners | milnet01 |
| Start (target) | after the R4 dynamic-GI regression closed (commit `a24a6e4`) |
| Target completion | TBD — sized at sign-off (5 slices, see §4) |
| Roadmap source | `ROADMAP.md` `## Phase 13: Advanced Rendering` (~line 1588), `### Global Illumination` block (~line 1614). Realizes the *Light probes* (~:1616) + *Real-time irradiance probe GI* (~:1619) intent via a lighter **DDGI-lite RSM-injection** technique that diverges from the listed surfel/SDF approaches (no surfel allocator; RDNA2-friendly). The exact ROADMAP bullet is ticked or added at sign-off (§11 Q5, deferred). |
| Relationship to R4 | Adds an off-screen / all-lights **world-space probe** term **alongside** R4 froxel near-field GI (on-screen). Coexist-by-default; R4 may be disabled under Dynamic per the §2.3 rule (they are not guaranteed both-on). See `docs/phases/phase_10_rendering_design.md` §11 (Slice R4, Variant A). |
| Peer docs | `docs/phases/phase_10_rendering_design.md` §11 (R4 dynamic-GI design-of-record — the energy model this doc reconciles with) |

---

## Section index

1. Goal · 2. Scope (in / out / honest notes) · 3. Architecture (subsystem map · code cross-refs · data surfaces) · 4. Steps / slices (G1–G5) · 5. CPU/GPU placement · 6. Performance budget + double-count reconciliation · 7. Accessibility · 8. Testing · 9. Dependencies · 10. References · 11. Open questions (blocking vs deferrable) · 12. Non-goals · 13. Considered alternatives · 14. Degenerate inputs · 15. Change log · 16. Cold-eyes loop log

---

## 1. Goal

Indirect diffuse lighting that:

- (a) captures **all relevant lights, including off-screen ones** — the thing
  on-screen R4 fundamentally cannot do;
- (b) supports **at least one bounce, adjustable**, with cheap multi-bounce;
- (c) runs **inside the 60 FPS floor on the RX 6600** (hard requirement);
- (d) **looks as good as possible across the hardware range** — a baked tier for
  low-end machines and static scenes (Tabernacle / Solomon's Temple walkthroughs),
  a fully-dynamic tier for games.

Diffuse-only, no hardware ray tracing (the engine is GL 4.5; Vulkan/RT is a later
Phase 13 track). Raster / compute GI only. Acronyms used below: **SH** =
spherical harmonics; **RSM** = reflective shadow map; **VPL** = virtual point
light; **EMA** = exponential moving average; **CSM** = cascaded shadow map; **MRT**
= multiple render target. (DDGI = Dynamic Diffuse Global Illumination, expanded in
the header table.)

## 2. Scope

### 2.1 In scope

- **One unified system, two tiers** sharing the same runtime read path:
  - **Baked tier** — bake probes offline (`RadiosityBaker`), sample at runtime,
    ~zero per-frame GPU cost. Low-end HW + static scenes.
  - **Dynamic tier** — the *same* `SHProbeGrid`, refreshed each frame from a
    reflective-shadow-map (RSM) injection + temporal feedback. Games, dynamic
    lights/geometry.
  - **User toggle: Baked / Dynamic / Off.**
- **Off-screen / all-lights capture** via RSM: a **flux** colour attachment on
  each shadow-casting light's existing shadow pass (sun CSM + point/spot cube), so
  the light records what *it* lights regardless of camera. Throughout, *flux*
  names this `albedo · incident-radiance` (outgoing-radiance) attachment — it is
  the RSM term of that name, not radiometric flux in watts.
- **Runtime probe injection**: scatter RSM flux texels (VPLs — virtual point
  lights) into the probe grid's SH coefficients.
- **Cheap multi-bounce** via temporal feedback (this frame's probe irradiance
  feeds next frame's injection; converges over a few frames).
- **Adjustable**: bounce strength / intensity dial; quality presets (grid
  density + RSM resolution + VPL sample count).
- **Light-leak control**: the existing `u_shNormalBias` is the always-on floor;
  G3 adds a per-probe **Chebyshev visibility** weight (DDGI moment test). See §3.4
  for the storage surface, formula, and read-path change.

### 2.2 Out of scope (this slice)

- **Glossy / specular GI** (reflections). Diffuse irradiance only; specular stays
  on the existing IBL / SSR path. A future Phase 13 track.
- **True surface lightmaps** (per-texel baked GI on walls; second-UV unwrap +
  lightmap atlas + per-mesh storage). Decision in §2.3; documented future option.
- **Hardware ray tracing / Vulkan path** — later Phase 13 track.
- **Voxel cone tracing / VXGI** (Approach C, §13) — out, overkill for diffuse-only
  60 FPS.

### 2.3 Honest scope notes

- **R4 is not deleted, and is not "SSGI."** R4 (`phase_10_rendering_design.md`
  §11, Variant A) is **froxel near-field dynamic GI** — a froxel-cached single
  bounce of *on-screen* direct light (`u_dynamicGiEnabled`, the `m_giTex` /
  `m_giHistoryTex` ping-pong cache). It is *on-screen*, not a screen-space
  buffer-gather. This slice adds the **world-space** term that *also* covers
  off-screen / all-lights, via a probe path (distinct from R4's planned Variant B
  froxel-bounce upgrade, `3D_E-0015`).
- **R4 ↔ world-space reconciliation (single normative rule).** R4 froxel
  near-field GI coexists as the on-screen near-field contributor **by default**.
  **If** *either* the G4 no-double-count luminance bound (§6) cannot be met with
  both R4 and the world-space Dynamic tier active, *or* their combined GI cost
  breaches the frame budget (§6), **then** R4 is disabled while the world-space
  Dynamic tier is active (one indirect-diffuse source at a time). This is decided
  at slice G4 and is **blocking for G4** (§11 Q2), not optional.
  R4 already resolves its *own* static-light overlap with the baked floor via the
  `u_giStrength=0.5` scale + diffuse-direct-only injection
  (`phase_10_rendering_design.md` §11.2) — this slice reuses that energy model.
- **Surface lightmaps deliberately cut.** The user deferred the call; decision is
  **probes-only**, raising baked quality via denser grids + higher bake settings
  instead. Rationale (global `~/.claude/CLAUDE.md` Rule 9 / YAGNI — push back when
  a simpler path covers the goal): surface lightmaps roughly double the authoring
  + storage work, mostly benefit fully-static AAA scenes, and dense volumetric
  probes are the industry-pragmatic 60 FPS choice. Recorded as a future option,
  not built now.
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
  RadiosityBaker (baked tier)                  scene.frag.glsl: evaluateSHGridIrradiance()
       │  offline iterative capture                  :599-665 → diffuseIBL term :1254-1281
       │                                                   (Ramamoorthi-Hanrahan A_ℓ eval)
       │
  ═══════════════════════════════ NEW (dynamic tier) ═══════════════════════════════
       │
  Shadow pass (CSM + point/spot)  ──+ RGBA16F FLUX attachment──►  RSM flux+depth+normal targets
       │  (today: depth-only — the one net-new FBO change)               │
       │                                                                 ▼
       │                                            gi_probe_inject.comp.glsl (NEW)
       │                                            scatter VPLs → SH coeff accumulation
       │                                            + temporal EMA vs last frame's probes
       │                                            + per-probe Chebyshev visibility
       └──────────────────────────────────────────────────────►  writes SHProbeGrid (runtime)
```

The decisive reuse: the **per-fragment SH read already ships** — it is the
expensive-to-add half of GI on a forward renderer, and it is already-shipped cost,
not new spend (§6). Both tiers write the same
grid; the shader cannot tell baked from dynamic. The dynamic tier just refreshes
the grid contents each frame instead of once offline.

### 3.2 Cross-references to existing code (verified file:line, current `main`)

All paths are repo-relative.

| Concern | Location | Note |
|---|---|---|
| Probe store + layout | `engine/renderer/sh_probe_grid.h:30-37,53-56` | L2, 27 floats → 7× RGBA16F 3D tex, units 17-23; **radiance-SH** convention |
| Probe write/upload API | `engine/renderer/sh_probe_grid.{h,cpp}` `setProbeIrradiance` / `upload` / `bind` / `isReady` | runtime-updatable — confirmed, not bake-only |
| CPU eval mirror | `engine/renderer/sh_probe_grid.{h,cpp}` `evaluateIrradianceCpu()` | parity-pinned vs shader |
| Baked-tier config struct | `engine/renderer/radiosity_baker.h:18-22` | `RadiosityConfig{maxBounces=4, convergenceThreshold=0.02, normalBias=0.3}` |
| Baked-tier bake loop | `engine/renderer/radiosity_baker.cpp:46-104` | iterative bounce bake, convergence on band-0 energy Δ |
| Shader read site | `assets/shaders/scene.frag.glsl:599-665` (`evaluateSHGridIrradiance`), `:1254-1281` (integration) | returns `E·INV_PI`; replaces cubemap diffuse when `u_hasSHGrid` |
| R4 dynamic read | `assets/shaders/scene.frag.glsl:1297-1303` | `ambient += u_giStrength·gi.a·kD·gi.rgb·albedo·ao`, gated by `u_dynamicGiEnabled` |
| R4 att3 injection source | `assets/shaders/scene.frag.glsl:39-45` | MRT attachment 3 = `albedo·Σ(shadowed direct diffuse)` only |
| CSM shadow FBO | `engine/renderer/cascaded_shadow_map.cpp:30,40-43` | **depth-only** `GL_DEPTH_COMPONENT24` 2D_ARRAY (`:30`), drawbuffer `NONE` (`:42`) → add flux attachment here |
| Point/spot shadow FBO | `engine/renderer/point_shadow_map.cpp:23,33-35` | **depth-only** cube (`:23`), drawbuffer `NONE` (`:34`) → add flux attachment here |
| Froxel/GI dispatch host | `engine/renderer/volumetric_fog_pass.{h,cpp}`; `engine/renderer/renderer.cpp:1373-1398` (dispatch), `:1574-1577` (swap) | pattern to mirror for the probe-inject dispatch + ping-pong |
| Reduced-motion freeze | `engine/renderer/renderer.cpp:1394-1396` | `alpha=0, decay=0` ⇒ cache frozen — the accessibility template |
| Slice-coord parity precedent | CPU `engine/renderer/gi_math.h:48` `giVolumetricSliceCoord`; GLSL `giSliceCoord` in `scene.frag.glsl:170` + `gi_inject.comp.glsl:61` | 3-copy parity pin (CPU name differs from the GLSL pair) |
| GL gotcha | memory `gl-compute-in-composite-unit0-clobber` | inject compute must dispatch **before** composite binds texture units |

### 3.3 Event / data surfaces

- No new event-bus events for the render path (per-frame GPU work, not gameplay).
- One settings surface (`Settings::rendering.giMode` ∈ {Off, Baked, Dynamic} +
  `giStrength`, `giQuality`) → an apply-sink mirroring the existing
  `dynamicGiEnabled` wiring (`renderer.cpp:1394-1396` reduced-motion path is the
  template for the gating flags).

### 3.4 Light-leak visibility (Chebyshev moment test — G3)

The shipped `u_shNormalBias` (applied in `evaluateSHGridIrradiance` at
`assets/shaders/scene.frag.glsl:603`; declared `:224`) is the always-on anti-leak
floor. G3 adds a DDGI-style moment test on top:

- **Storage (new):** one **RG16F 3D texture** co-located with the SH grid, bound
  at **texture unit 25**, holding per-probe depth moments — R = mean depth `E[d]`,
  G = mean-squared depth `E[d²]` — written by the inject pass from the RSM / probe
  depth. (Scene-pass unit map: SH grid = 17-23; R4's `u_giTexture` = 24
  [`scene.frag.glsl:163`]; this moment texture = 25, the first free unit. Unit 24
  is **not** free — R4 coexists under Dynamic by default per §2.3.)
- **Weight:** for a shaded point at distance `t` from the probe,
  `variance = E[d²] − E[d]²`; `p = variance / (variance + max(t − E[d], 0)²)`;
  visibility weight `= (t ≤ E[d]) ? 1 : p`. The weight multiplies that probe's
  trilinear contribution, attenuating irradiance that leaks past an occluder.
- **Read-path change:** `evaluateSHGridIrradiance`
  (`assets/shaders/scene.frag.glsl:599-665`) gains an optional moment sample +
  per-probe weight when Dynamic + leak-fix are active; Baked / Off skip it (zero
  added cost). This is a `scene.frag` edit — pinned by the §5 parity test and the
  G3 leak test (§8).

## 4. Steps / slices

Each slice is independently testable, lands behind a flag, and carries a perf gate
(≤ its budget on the RX 6600). Order chosen so the grid is never left half-written.

| # | Slice | Deliverable | Gate |
|---|---|---|---|
| G1 | **RSM flux attachment** | Add RGBA16F flux (albedo·radiance) colour attachment to CSM + point/spot shadow FBOs; shadow shaders **write** flux alongside depth. Flux is written but **nothing consumes it yet** (G2 is the first consumer). **Shipped 2026-06-24 for the casters already in the shadow pass — meshes + foliage.** | FBO completeness test; existing shadow tests still green; **flux readback matches a CPU reference for one directional light** (readback is testable because G1 writes flux) |
| G1t | **Terrain flux (split from G1)** | Wire `TerrainRenderer::renderShadow` into the shadow pass (terrain does not cast shadows today) and have `terrain_shadow` write flux from the splatmap-blended base albedo. Carries the new behaviour change (terrain shadow-casting) + its perf cost, isolated from G1. | terrain shadow-cast perf gate; flux readback vs CPU reference for terrain; existing shadow tests still green |
| G2 | **Runtime probe injection** | `gi_probe_inject.comp.glsl` — scatter RSM flux VPLs into SH coeffs, write `SHProbeGrid` each frame (single bounce, no temporal yet). | GPU↔CPU parity of the SH projection (readback vs `gi_probe_math.h` mirror); inject-dispatch perf gate — **soft target 0.6 ms, hard fail above 0.8 ms** (the 0.6 ms target is unconfirmed pending the §11 Q1 Workbench fit; the 0.8 ms hard fail is the binding gate; §6) |
| G3 | **Temporal multi-bounce + leak fix** | EMA-blend this frame's injection with last frame's probe irradiance (feedback = extra bounces); per-probe Chebyshev visibility weight (§3.4). | **convergence test** (§8 criterion); **leak test** (§8); CPU/GPU parity (§5) |
| G4 | **Tier toggle + R4 reconciliation** | `Settings::rendering.giMode` (Off/Baked/Dynamic) + apply-sink; apply the §2.3 normative R4 rule. | settings round-trip test; **no-double-count test** (§6 bound) decides whether R4 stays active under Dynamic; full-frame perf gate ≤ 1.5 ms (§6) |
| G5 | **Quality presets + accessibility** | grid-density / RSM-res / VPL-count presets; `giQuality` ∈ {Low, Balanced, High, Auto}; accessibility (reduced-motion freezes the grid; intensity dial; no flashing). | per-preset perf gates; reduced-motion freeze test; each tuning constant (**VPL-count cap, EMA `alpha`, confidence `decay`, Chebyshev bias**) has a Formula-Workbench fit-record or a `TODO: revisit` comment. **Auto** picks a preset from GL renderer string + VRAM (a unit test stubbing the renderer string asserts {RX 6600-class → High, llvmpipe / < 2 GB → Low}, else Balanced); until that mapping is tuned in G5, Auto defaults to Balanced |

## 5. CPU / GPU placement (per project `CLAUDE.md` Rule 7)

| Work | Placement | Reason |
|---|---|---|
| RSM flux render | **GPU** | per-fragment, in the existing shadow pass — free piggyback |
| VPL → SH projection / scatter | **GPU** (compute) | per-probe (and per-VPL-texel), massively parallel; mirrors the R4 inject pattern |
| Temporal EMA blend | **GPU** (compute) | per-probe, per-frame |
| Per-probe Chebyshev visibility | **GPU** (compute) | per-probe sample of RSM depth moments |
| Per-fragment SH evaluation | **GPU** (already shipped) | per-pixel |
| Grid placement / config / mode switch / preset selection | **CPU** | sparse, decision/IO, once per scene or per settings change |
| Offline radiosity bake driver | **CPU** (drives GPU capture) | branching convergence loop; `radiosity_baker.cpp` already this shape |

**Dual-impl parity (Rule 7):** the SH projection + EMA blend get a GL-free CPU
mirror in a new `gi_probe_math.h` (mirroring the existing `gi_math.h`), pinned by
a parity test (GPU readback == CPU within tolerance) — the same discipline that
pins the CPU `giVolumetricSliceCoord` / GLSL `giSliceCoord` pair across its three
copies today.

## 6. Performance budget + double-count reconciliation

Target: total GI cost **≤ 1.5 ms (hard ceiling)** on the RX 6600 at 1080p
(Balanced preset), inside the 16.6 ms frame (60 FPS). Expected spend **~1.1 ms**;
the remaining ~0.4 ms is margin. Per-stage split (estimates, confirmed by the
G2/G3/G4 gates):

| Stage | Budget | Notes |
|---|---|---|
| RSM flux render | ~0.2 ms | piggybacks shadow pass; cost = extra colour writes |
| Probe injection (VPL scatter) | ~0.6 ms (hard fail > 0.8 ms, §4 G2) | scales with grid density × RSM sample count |
| Temporal EMA + visibility | ~0.3 ms | per-probe |
| **Expected spend** | **~1.1 ms** | sum of the above; per-fragment SH read is already-shipped cost, outside this envelope. Remaining ~0.4 ms to the 1.5 ms ceiling is margin |

- **Gate authority:** the full-frame G4 gate (≤ 1.5 ms) is authoritative. The
  per-stage figures are targets; G2's inject hard-fails above 0.8 ms, but a stage
  may flex provided the full-frame gate holds. The per-stage hard-fails do not
  themselves sum to the ceiling — the full-frame gate is the binding contract.
- **Baked tier cost ≈ 0** per frame (read-only) — the low-end answer.
- **R4 co-residency:** R4's froxel inject (≤ 0.4 ms, `phase_10_rendering_design.md`
  §11.6) is a *separate* compute pass, **not** inside this 1.5 ms ceiling. When R4
  coexists under Dynamic (§2.3), combined dynamic-GI cost is ≤ 1.5 + 0.4 ms. If
  either trigger in the §2.3 rule fires (the no-double-count luminance bound is
  unmet, or the combined cost is over budget), the "disable R4 under Dynamic"
  branch resolves it — not loosening either ceiling.
- **Double-count reconciliation (normative).** Three indirect-diffuse sources can
  stack: the baked floor (first bounce), R4 froxel near-field (on-screen), and the
  world-space Dynamic tier. They must sum to plausible indirect, not 2–3× it. The
  **no-double-count test** (G4 gate) asserts, in a fixed reference scene with a
  single dominant light: `L(Off) < L(Baked) ≈ L(Dynamic)`, with the mean indirect
  luminance under Dynamic within **±15%** of Baked-only, and total indirect never
  exceeding **1.5×** the single-bounce reference. If the bound fails with both R4
  and Dynamic active, R4 is disabled under Dynamic per the §2.3 rule. The
  `u_giStrength` scale (R4's existing knob) bounds the residual baked-vs-dynamic
  overlap.
- Benchmark gates follow the project pattern — **and per the GL-version bug fixed
  in commit `a24a6e4`, GPU perf-gate tests must not assume GL 4.6 and must be
  CI-safe under software GL** (Mesa llvmpipe, GLSL 4.50 max). Timing assertions
  gate off software renderers; correctness assertions always run. New GI shaders
  must pass the `ShaderLintStrict` ctest gate (`tests/CMakeLists.txt:324` — GLSL
  4.50 target + ARB-suffixed draw-parameter built-ins).

## 7. Accessibility

- **Motion:** reduced-motion freezes the probe grid (no temporal flicker on
  moving lights) — reuse the R4 reduced-motion path (`renderer.cpp:1394-1396`:
  `alpha=0, decay=0` ⇒ cache frozen). GI must never flash or strobe.
- **Visual:** intensity dial (`giStrength`) doubles as a comfort control; GI off
  is always a valid, fully-lit fallback (baked floor or flat ambient).
- **Cognitive load:** three named modes (Off / Baked / Dynamic), not a wall of
  sliders; quality is a single preset enum (Low / Balanced / High / Auto) with
  Auto defaulting to Balanced until its HW-detection mapping is tuned (§4 G5).

## 8. Testing strategy

- **Per-slice unit/parity (CPU-side, GL-free):** SH projection mirror, EMA blend
  mirror, slice/probe coordinate helpers — `gi_probe_math.h` pinned by parity
  tests, mirroring `gi_math.h` / `test_gi.cpp`.
- **GPU verification (headless 4.5 context):** shaders compile+link (the
  `GiGpuTest` pattern — **authored against GLSL 4.50 + ARB-suffixed built-ins so
  they pass the `ShaderLintStrict` gate and run under CI's software GL**); flux
  readback; probe write readback vs CPU mirror.
- **Behaviour, with falsifiable criteria:**
  - **Convergence:** max per-probe irradiance Δ < 1% between consecutive frames
    within 8 frames of the scene going static.
  - **Off-screen capture:** a light outside the view frustum measurably raises
    indirect irradiance on a surface it lights — `Dynamic ≥ Off + ε` at that probe
    (ε a stated floor, so a near-zero injection does not trivially pass).
  - **Leak:** a probe behind a wall does not lift the wall's shadowed-side
    irradiance more than 2% above the no-GI (Off) baseline —
    `irradiance_leak ≤ 1.02 × irradiance(Off)` at that surface.
  - **No-double-count:** the §6 luminance bound across Off / Baked / Dynamic.
- **Perf gates:** per-slice dispatch budgets + full-frame budget (§6), CI-safe
  (software-GL timing skip, correctness always).
- **Coverage gap acknowledged:** visual quality is judged by eye + reference
  screenshots, not a numeric metric; the tests pin *correctness and budget*, not
  *beauty*. Accepted, given the reference-screenshot fallback.

## 9. Dependencies

- **Existing, consumed:** `SHProbeGrid`, `RadiosityBaker`, `cascaded_shadow_map`,
  `point_shadow_map`, `VolumetricFogPass` dispatch host, `Settings` + apply-sink
  pattern, `scene.frag.glsl` read site. All verified present (§3.2).
- **New, built here:** `gi_probe_inject.comp.glsl`, `gi_probe_math.h` (CPU
  mirror), the RSM flux attachment on the two shadow FBOs, the RG16F per-probe
  depth-moment texture + its `scene.frag` read-path weight (§3.4), the `giMode`
  settings surface.
- **No new external libraries** (project `CLAUDE.md` Rule 8 — none needed).

## 10. References

- Majercik, Guertin, Nowrouzezahrai, McGuire — *Dynamic Diffuse Global
  Illumination with Ray-Traced Irradiance Fields* (DDGI), JCGT 2019 — the probe +
  Chebyshev-visibility model adapted here to a raster RSM injection.
- Dachsbacher & Stamminger — *Reflective Shadow Maps*, I3D 2005 — the off-screen /
  all-lights flux capture.
- Ramamoorthi & Hanrahan — *An Efficient Representation for Irradiance Environment
  Maps*, SIGGRAPH 2001 — the L2 SH irradiance evaluation already in
  `scene.frag.glsl`.
- Unreal Engine — *Volumetric Lightmaps* / indirect lighting cache — the
  conceptual analogue of the baked tier (probes, not surface lightmaps).

## 11. Open questions

**Blocking (each gates a named slice — must resolve before that slice closes):**

1. **RSM injection budget on the RX 6600** (→ G2) — is ~0.6 ms realistic at the
   Balanced grid density, or does VPL count need a Formula-Workbench-fit cap?
2. **R4 coexistence vs disable under Dynamic** (→ G4) — settled by the §2.3
   rule's two triggers (the §6 no-double-count luminance bound and the frame-budget
   ceiling); until then the §2.3 rule is the contract.
3. **Flux attachment VRAM** (→ G1) — RGBA16F on every cascade + cube face; confirm
   the cost is acceptable at the default shadow resolutions before G1 ships it.

**Deferrable (may move to a follow-up slice):**

4. **Grid placement for large scenes** — single grid vs cascaded/clipmap probes
   for the Temple-scale walkthroughs.
5. **ROADMAP bullet mapping** — at sign-off, tick or add the precise
   `### Global Illumination` bullet this slice fulfils (*Light probes* /
   *Real-time irradiance probe GI*), annotating the DDGI-lite RSM-injection
   divergence from the listed surfel/SDF techniques.

## 12. Non-goals (explicitly out)

Glossy GI, surface lightmaps, HW ray tracing, Vulkan path, VXGI/voxel GI,
non-shadow-casting-light injection. Each is a separate future decision, not a
silent omission.

## 13. Considered alternatives

- **B — RSM + Light-Propagation Volumes (generalized "Froxel Bounce",
  `3D_E-0015`).** Cheaper update, blurrier, more leak-prone, and needs a *new*
  per-fragment read path (the LPV grid) — whereas the chosen approach reuses the
  shipped SH read. Kept as a fallback if probe injection proves too costly.
- **C — Voxel cone tracing / VXGI-lite.** Most capable (sharp, glossy,
  fully-dynamic via per-frame voxelization) but ~3–5 ms + heavy VRAM + a large new
  subsystem — overkill for a diffuse-only 60 FPS goal.
- **Chosen: A — DDGI-lite** (dynamic SH probes + RSM injection + temporal
  feedback): reuses the expensive half (the read), adds only the flux attachment +
  a compute pass, and folds the baked tier in for free.

## 14. Degenerate inputs

| Input | Behavior |
|---|---|
| Zero shadow-casting lights | Inject pass **skipped** (no RSM flux to scatter); the baked floor (or flat ambient if no bake) remains. No crash, no black frame. |
| **Dynamic** mode but `SHProbeGrid::isReady() == false` (no grid) | Behave as **Off** (skip inject + read); log once. |
| **Baked** mode but no baked grid present | Fall back to flat ambient (or Off if ambient unset); log once. Mirrors the Dynamic-no-grid row. |
| Mode switched mid-session (e.g. Off → Dynamic) | The grid warms up over the EMA convergence window (§8: < 8 frames). Asserted as **monotone, non-overshooting toward the target**: for a rising target (Off → Dynamic from zero) `irradiance[n] ≥ irradiance[n−1]`; for a falling target (a light dims) `irradiance[n] ≤ irradiance[n−1]`; either way bounded between the start and converged values (an EMA toward a fixed target never overshoots). Asserted with scene lights held static during the warm-up window (as in the §8 convergence test); a simultaneously-moving light relaxes this to per-step convergence, not global monotonicity. Steady-state within the 8-frame window — converging, never garbage or a flash. |
| Quality preset changed mid-session (grid resolution changes) | Grid re-allocated at the new resolution; the re-warm re-projects the old irradiance into the new grid (not a cold zero start). Asserted at a fixed **world-space** sample point P — not per-probe index, which the resolution change invalidates: irradiance at P converges monotonically to the new-resolution value within the 8-frame window, and no frame samples the old grid after re-allocation. |

## 15. Change log

- 2026-06-24 — initial draft; source map (§3.2) verified against `main` and
  re-confirmed accurate through the cold-eyes loops. Loops 1-6 fixes folded in
  (see §16).
- 2026-06-24 — **G1 shipped** (RSM flux attachment). RGBA16F flux colour
  attachment added to the CSM (`cascaded_shadow_map.{h,cpp}`) and point/spot
  (`point_shadow_map.{h,cpp}`) FBOs; mesh (`shadow_depth`), point
  (`point_shadow_depth`) and foliage (`foliage_shadow`) shadow shaders write
  `albedo·radiance·max(0,N·L)` (point adds attenuation). CPU spec
  `engine/renderer/gi_probe_math.h`; tests `test_gi_probe_{gpu,math}.cpp`
  (compile/link + GPU↔CPU flux parity + FBO completeness). **Scope note:**
  during implementation, terrain was found *not* to render into the shadow pass
  (`TerrainRenderer::renderShadow` is unwired), so ground-bounce flux is split to
  a dedicated follow-up slice (enabling terrain shadow-casting first) rather than
  silently turning that behaviour on — user-confirmed 2026-06-24. Foliage flux
  uses a fixed up-normal (grass billboards have no per-fragment normal) and the
  atlas albedo without the per-instance tint — both deliberate simplifications
  for indirect light.

## 16. Cold-eyes loop log

- **Loop 1 (2026-06-24)** — 4 cold reviewers (accuracy / consistency / cross-ref /
  clarity). 1 CRITICAL (R4 supersede-vs-coexist contradiction), 6 HIGH (§3.2
  directory-less paths, budget arithmetic labelling, G1 flux-unused-vs-readback,
  no-double-count undefined, ROADMAP line stale, Rule-9 misnumbered), 11 MEDIUM,
  ~12 LOW — all verified against source and fixed. R4 relabelled froxel near-field
  (not "SSGI"); double-count + convergence tests given numeric bounds; §11 split
  blocking/deferrable; §14 degenerate inputs added; citations directory-prefixed
  and line-corrected; acronyms expanded; TOC added.
- **Loop 2 (2026-06-24)** — Lane A (accuracy) clean. 4 HIGH (ROADMAP-anchor
  mismatch, "Extends R4" overstatement, "flux" naming-vs-definition, **Chebyshev
  visibility under-specified — no storage surface**), several MEDIUM/LOW — fixed.
  §3.4 (Chebyshev: formula + storage + read-path) added; flux disambiguated; R4
  relationship reworded; ROADMAP bullet-mapping deferred to §11 Q5.
- **Loop 3 (2026-06-24)** — no CRITICAL. 2 HIGH (MRT not in roster; per-stage
  gates don't compose into ceiling), MEDIUM/LOW (leak-test epsilon, monotonicity,
  disable-R4 trigger wording, Auto acceptance) — fixed. Gate-authority order
  stated; leak ≤2%; Auto unit-test acceptance added.
- **Loop 4 (2026-06-24)** — 1 CRITICAL **introduced in loop 3 and caught here**:
  Chebyshev moment texture pinned to unit 24, which R4's `u_giTexture` already
  occupies → moved to unit 25 with a scene-pass unit map. MEDIUM (monotone
  assertion was direction-blind → scoped to rising/falling target; preset-change
  edge case added; DDGI double-expansion deduped; §15 commit phrasing). Lane A
  clean.
- **Loop 5 (2026-06-24)** — Lane A clean (unit 25 verified genuinely free in
  source). 1 HIGH (disable-R4 trigger drift: §6's frame-budget clause vs §2.3's
  "single normative rule" → reconciled to two explicit triggers stated once in
  §2.3, referenced by §6/§11 Q2), 2 MEDIUM (§14 monotone needed a static-lights
  precondition; preset-change row's per-probe assertion re-anchored to a fixed
  world-space sample point), 2 LOW (leak as explicit ratio; off-screen ε floor) —
  fixed.
- **Loop 6 (2026-06-24) — CONVERGED.** Lane B clean ("close the loop"); Lane A +
  Lane D each 1 MEDIUM, both labeling/cross-ref polish (the `a24a6e4` anchor
  phrasing read stale though every citation resolved; the §6 table now echoes the
  0.8 ms hard-fail). Findings decayed to micro-nits across loops 1→6 — the
  convergence signal. Both polish items fixed; no CRITICAL/HIGH, no design-level
  finding. **Cold-eyes complete.**
