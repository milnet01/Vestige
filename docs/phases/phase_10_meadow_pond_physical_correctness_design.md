# Meadow Pond — Physical Correctness & Natural Shoreline (Design)

**Status:** Draft — cold-eyes loop 3 applied
**Program:** 3D_E-0027 (meadow benchmark scene), Phase 10 terrain/water
**Scope:** Fix the meadow pond so it reads as a real body of water: a level
surface that fills a contained basin, with a natural contour shoreline instead
of a floating opaque square. No water-system rewrite; the shoreline is produced
by machinery already in the render path (depth occlusion + the shader's shallow-
band fade).

## Contents
- §1 Problem (reproduced)
- §2 Physical model (target)
- §3 Fix (3.1 fill+spill · 3.2 sizing · 3.3 why edges vanish · 3.4 shore paint · 3.5 foliage)
- §4 CPU / GPU placement
- §5 Reuse
- §6 Preconditions, risks, failure modes
- §7 Verify plan
- §8 Constants
- §9 Out of scope

---

## 1. Problem (reproduced)

User report: the pond "is still a square with straight lines" and "looks like it
is floating in the air"; and, physically, "you can't have a pond on a slope —
the water runs down until it finds a physically correct space."

Reproduced on the meadow scene (`build-release/bin/vestige`, RX 6600). Root
cause is a size mismatch between the water plane and the carved basin — not a
missing feature:

- The terrain grid is **square**: 257×257 vertices at `spacingX = spacingZ =
  1.0` (`engine/environment/terrain.h:25-28`, set in
  `engine/systems/terrain_system.cpp:28-31`) → a 256 m × 256 m world. A circle in
  normalized grid space is therefore a circle in world space.
- The basin is carved by `meadowHeight01()`
  (logic at `engine/environment/meadow_terrain.cpp:83-94`, using values set at
  `engine/core/engine.cpp:2253-2255`): `pondRadiusGrid = 0.11` (≈ 28 m) and
  `bowlDepth01 = 0.09` (≈ 4.5 m dip at centre) over the 50 m height scale. The
  carve `falloff` decreases monotonically to 0 at `pondRadiusGrid`
  (`meadow_terrain.cpp:88-92`), so terrain rises monotonically to that radius
  (plus fbm noise); beyond it the surface is pure fbm hill. `pondRadiusGrid` is
  the **carve boundary**, not necessarily the containment rim (see §3.1).
- The water plane is a flat square of `POND_SIZE = 14.0 m`
  (`engine/core/engine.cpp:2279`; assigned to `c.width`/`c.depth` at
  `engine.cpp:2365-2366`), placed at `waterLevelY = bowlFloorY + 1.5f`
  (`engine.cpp:2357-2358`).

At the 7 m plane edge the carved floor is roughly `t = 0.25` up the bowl
(`falloff ≈ 0.84`), so the surface there is ~0.8 m below the water plane
(estimate). Because water draws with depth-write off (`water_renderer.cpp:106`)
but the depth **test** stays on (renderer-global reverse-Z GEQUAL,
`renderer.cpp:115,121`), those submerged edge fragments are **not** occluded,
and the shader's shore fade (`edgeFade`, §3.3) only feathers the last 1 m of
*view-ray* water thickness — which at grazing angles far exceeds the 0.8 m
vertical depth, so the edge shades opaque. The result is a hard, opaque square
edge over the sunken floor; the true shoreline (≈ 10 m out — estimate from the
smoothstep falloff) is never reached by the 7 m mesh. Both symptoms — "square"
and "floating" — share one cause: **the water sheet is far smaller than its
basin.**

Containment is a second, latent issue: `waterLevelY` is `floor + 1.5 m` with no
check that it stays below the lowest escape saddle. The rolling fbm relief
(octave amps 0.045 + 0.025 + 0.012 ≈ 0.082 **one-sided**, i.e. `±0.082` → ~8 m
peak-to-peak over the 50 m scale) can pull an escape path down, so a fixed offset
is not guaranteed contained — physically, water at that level would spill out the
low side.

## 2. Physical model (target)

A real pond is a **level (horizontal) surface** filling a closed depression up to
its **spill height** — the lowest ridge crest water must climb to escape. Above
that height water runs downhill. The waterline is the contour where that level
plane meets the rising ground.

Three invariants:

- **INV-1 (level):** the water surface is a single horizontal plane at `waterY`.
  (Already true — the plane is axis-aligned and flat.)
- **INV-2 (contained):** `waterY < spillHeight`, where `spillHeight` is the
  minimum over outward directions of the **maximum terrain height along that
  direction** (the ridge crest that direction's escape must cross). For the
  single authored radial bowl (monotone carve, §1) this min-over-rays-of-ridge-max
  is a good estimate of the true spill; a general multi-basin watershed is out of
  scope (§9).
- **INV-3 (contour shoreline):** the pond is visible only where the sheet is over
  submerged ground; its outline is the terrain contour at `waterY`, never a
  straight mesh edge. This is achieved by (a) sizing the sheet so every straight
  edge lands on dry ground, where opaque terrain **depth-occludes** the plane
  (§3.3), and (b) painting the shore band and skipping foliage on the same contour
  (§3.4/§3.5).

## 3. Fix

All but the shoreline itself is CPU-side scene setup in `finalizeMeadowTerrain()`
(`engine/core/engine.cpp:2236`). The shoreline is emergent from the existing
render path — **no shader change.** The numerical core (§3.1–§3.2) is extracted
into a **pure, GL-free helper** in `engine/environment/meadow_terrain.{h,cpp}`
(alongside `meadowHeight01`) so it is unit-testable headlessly (§7):

```
struct PondFill { float waterLevelY; float floodRadius; float spillHeight; };
PondFill computePondFill(HeightSampler sampleHeight, glm::vec2 centreWorld,
                         float rRimWorld, float bowlFloorY, const PondFillParams&);
```

`HeightSampler` is `float(float wx, float wz)`; the scene passes a lambda over
`Terrain::getHeight`, the test passes an analytic sampler / `meadowHeight01`.

The helper marches `N_RAYS` rays **once, storing each ray's sampled height
profile**, then derives the fill from the stored samples in two passes — spill
first (§3.1), flood second (§3.2). A single forward pass is impossible: the flood
radius keys on `waterLevelY`, which needs `spillHeight = min over all rays` and so
is unknown while ray #1 is still marching. Retaining the per-ray samples is what
makes the two passes cheap (no re-march). §8's computation order matches.

### 3.1 Contained fill level (INV-2) — CPU helper, pass 1
- **March (shared):** cast `N_RAYS` rays over `[0, 2π)` out to `R_SCAN =
  SCAN_FACTOR · R_rim` (a bit past the carve boundary), stepping `MARCH_STEP`
  metres; store the sampled terrain height at every step of every ray.
- **Pass 1 (spill):** per ray, `ridgeCrest = max` stored height along the ray;
  `spillHeight = min` over rays of `ridgeCrest` — a single-ring
  min-over-directions-of-max-along-path. This is a **circular-barrier
  approximation**, exact only for a basin whose escape saddles lie within
  `R_SCAN` and whose carve is monotone to the rim (the authored bowl); it is not
  a general watershed solve (§9), and §7.1 validates it against an independent
  flood-fill.

Then:

```
waterLevelY = min(bowlFloorY + DESIRED_DEPTH, spillHeight - RIM_MARGIN)
waterLevelY = max(waterLevelY, bowlFloorY + MIN_DEPTH)   // degenerate guard
```

`RIM_MARGIN` must exceed the **cross-ray** crest-height variation — the difference
in ridge crest between *adjacent rays* (angular undersampling is what lets a low
saddle slip between two rays), which is the containment-dangerous quantity. This
is distinct from the along-ray sampling error, which `MARCH_STEP` controls. See §8
for values. The `MIN_DEPTH` guard covers the degenerate case where a low saddle
would push `spillHeight - RIM_MARGIN` below the floor (a zero/negative-depth
pond); **note the guard *overrides* the containment clamp** — in that degenerate
basin the pond trades INV-2 for a non-empty surface, consistent with the
double-basin case being out of scope (§9).

### 3.2 Water sheet sized to the actual flood (INV-3) — CPU helper, pass 2
- **Pass 2 (flood):** over the **same stored samples**, now that `waterLevelY` is
  known, record per ray the **outermost** radius where terrain is still below
  `waterLevelY` (the last submerged sample before it rises and stays above — not
  merely the first up-crossing, so an fbm bump that pokes above `waterY` then dips
  below again doesn't truncate the flood).
- `floodRadius = max` over rays of that outermost submerged radius. By INV-2,
  `waterY < spillHeight ≤` every ray's crest, so each ray does cross above `waterY`
  within `R_SCAN`; a ray that never rises above `waterY` within `R_SCAN` is capped
  at `R_SCAN` and flagged (should not occur for the contained bowl — asserted,
  §7.2).
- `POND_SIZE = 2 · (floodRadius + EDGE_PAD)`.

`EDGE_PAD` is chosen ≥ the shoreline-radius variation over one ray spacing (or
`N_RAYS` large enough that the variation is sub-`EDGE_PAD`), so every straight
edge of the square sheet lands on **dry** ground on all sides. This assumes a
**star-convex** flooded region about the centre (true for the near-circular
authored bowl); a non-star-convex shoreline is out of scope (§9). The sheet is
then only marginally larger than the water it shows, so the depth-occluded dry
rim (§3.3) is thin — minimal overdraw.

### 3.3 Why the straight edges vanish — depth occlusion, no shader change
Over dry ground the flat water plane at `waterY` sits **below** the opaque terrain
surface. Terrain is drawn (opaque, depth-written) before the water pass
(`engine.cpp` terrain pass precedes the water draw at `:1679`); water draws with
`glDepthMask(GL_FALSE)` but depth-**test** on under reverse-Z `glDepthFunc(GL_GEQUAL)`
(`water_renderer.cpp:106`, `renderer.cpp:121`). A water fragment below the already-
drawn terrain is farther from the camera → smaller reverse-Z depth → **fails
GEQUAL → killed before shading.** So the enlarged sheet's straight edges, landing
on dry ground (§3.2), are simply **not drawn** — no colour, no alpha, regardless
of the shader. This is the operative mechanism, and it does not depend on the
refraction texture.

The shader's `edgeFade` (`water.frag.glsl:287-288`, driven by `waterThickness =
max(linearRefract - linearWater, 0.0)`, `:235`) only feathers the **shallow
submerged band just inside** the true shoreline — there terrain is below `waterY`,
so it *is* included in the refraction FBO (which is clipped to `y ≤ waterY+0.1`,
`engine.cpp:1627-1628`) and yields a small thickness → a soft fade + the foam band
(`:270-281`). Over dry ground the refraction FBO has no geometry (clipped out), so
`waterThickness` would be *large* (cleared far depth → large `linearRefract`) and
`edgeFade → 1` (opaque) — which is exactly why depth occlusion, not `edgeFade`,
must hide the dry edges.

**No `discard` is added.** The dry rim is removed by depth occlusion, not by
shading, so there is nothing to discard; and the pass writes no depth
(`glDepthMask(GL_FALSE)`), so any thin visible transparent sliver cannot occlude
later passes.

### 3.4 Shore band follows the contour — CPU, scene setup
`Terrain::applyBankBlend` (`engine/environment/terrain.cpp:1054-1110`) paints the
damp/mud band against the water **AABB** (a box test + signed-distance-to-
rectangle), producing a square-edged mud ring — which would leave a straight
brown band on the banks even after the water becomes a contour, contradicting
INV-3. Replace it, for the pond, with a **contour band**: paint the dirt channel
(splat channel 2 — verified R=grass/G=rock/B=dirt/A=sand at `terrain.cpp:1012`,
matching `bankChannel = 2` at `engine.cpp:2301`) with weight `1 - smoothstep(0,
BAND_WIDTH, abs(terrainHeight(x,z) - waterLevelY))` over cells near the pond, via
the existing per-channel splat write (`terrain.h:148`). The damp edge then hugs
the waterline contour on all sides. `applyBankBlend` itself is unchanged (it stays
for editor-authored rectangular water bodies); only the meadow pond stops calling
it.

### 3.5 Foliage keyed to the contour — CPU, scene setup
Grass currently skips a disc of `POND_SIZE·0.5 + 3` (`pondSkipRadius`,
`engine.cpp:2396`) and erases a final disc via `eraseAllFoliage(center, radius)`
(`engine.cpp:2429-2430`; `foliage_manager.cpp:160` — a disc, no height predicate).
With §3.2's larger sheet a `POND_SIZE`-derived disc would strip grass from dry
banks. Split the two by capability:

- **Stamp skip → contour:** skip a grass stamp when its centre is below the
  waterline, `terrainHeight(cx,cz) < waterLevelY + SHORE_MARGIN` — a per-centre
  height test the stamp loop can do directly (it already samples `getHeight(cx,cz)`
  at `engine.cpp:2420`). Grass then grows up to the damp shore and none on the
  water.
- **Final erase → disc at the flood radius:** the erase API is disc-only, so pass
  `eraseAllFoliage(center, floodRadius + SHORE_MARGIN)`. A disc slightly larger
  than the star-convex flooded region only removes spill inside the water
  footprint; no per-instance contour erase is needed.

**Computation order** (no circular dependency): `R_rim → (ray-cast) spillHeight →
waterLevelY → (same ray-cast) floodRadius → POND_SIZE`; grass-skip keys on
`waterLevelY`, erase keys on `floodRadius`, shore paint keys on `waterLevelY`.

## 4. CPU / GPU placement

| Work | Where | Why |
|------|-------|-----|
| Ray-cast spill/flood, fill level, plane size, contour shore paint, contour foliage skip, disc erase | **CPU**, one-time in `finalizeMeadowTerrain()` (pure helper for the math) | Branching / sparse / decision, runs once at scene build — not per frame. |
| Per-fragment shore fade + foam (submerged band only) | **GPU**, `water.frag.glsl` (unchanged) | Per-pixel; already on the GPU. |
| Dry-edge removal | **GPU fixed-function** depth test (unchanged) | Occlusion is free fixed-function work; no shader change. |

No per-frame CPU cost is added (new work runs once at scene setup). No new GPU
work is added — the sheet is larger but sized tightly (§3.2), the dry rim is
depth-culled pre-shading, and no `discard` is introduced. No dual CPU/GPU impl
exists (the shoreline lives only on the GPU; CPU only positions/sizes the mesh
once), so no parity test is required. Performance is still verified by measurement
(§7), because this is a benchmark scene.

## 5. Reuse (no rewrite)

- Dry-edge hiding: existing reverse-Z depth test + terrain-before-water draw order
  (`water_renderer.cpp:106`, `renderer.cpp:121`) — unchanged.
- Shore fade + foam: existing `waterThickness`/`edgeFade`
  (`water.frag.glsl:235,270-281,287-288`) — unchanged.
- Terrain sampling: existing `Terrain::getHeight()` (already called at
  `engine.cpp:2357,2420,2428`).
- Contour shore paint: same per-channel splat write as `applyBankBlend`/
  `generateAutoTexture` (`terrain.h:148`).
- Basin carve: existing `meadowHeight01()` bowl — unchanged.
- New code is CPU-only and small: one ray-cast helper (spill + flood), a contour
  splat loop, and a height-test in the existing grass stamp loop. No shader edit.

## 6. Preconditions, risks, failure modes

- **Precondition (dry-edge occlusion):** opaque terrain is drawn before the water
  pass with the depth test enabled under reverse-Z GEQUAL. The renderer runs the
  scene pass with `GL_DEPTH_TEST` enabled and `glDepthFunc(GL_GEQUAL)`
  (`renderer.cpp:115,121`); the water draw (`engine.cpp:1679`) follows the opaque
  terrain pass, after `rebindSceneFbo()`/`restoreViewState()`
  (`engine.cpp:1669-1670`). *Open question for the code owner:* confirm the depth
  test remains enabled through `restoreViewState()` up to the water draw (no
  `glDisable(GL_DEPTH_TEST)` on that path) — this is the mechanism's load-bearing
  precondition.
- **Real opaque-square failure mode:** any part of the enlarged sheet overhanging
  a region with **no opaque occluder at/above `waterY`** (a terrain LOD hole, the
  map edge, terrain disabled) is not depth-culled and shades opaque (refraction-
  clipped). For the meadow the pond centre is world ≈ (0, −15) with
  `floodRadius + EDGE_PAD` (corners ×√2) ≪ the 128 m terrain half-extent (origin
  −128, `terrain_system.cpp:33`) and terrain always enabled, so no overhang — but
  this is the condition to preserve, not an
  assumption to ignore. *Open question:* confirm the sheet stays within terrain
  bounds for the shipped shape.
- **Containment residual (INV-2):** the min-over-rays-of-ridge-max is exact only
  for escape saddles within `R_SCAN` on a monotone-carve basin. A hand edit that
  carves a channel or a second dip breaks it; `MIN_DEPTH` prevents a degenerate
  empty pond but not a mis-authored double basin — out of scope (§9).
- **Caustics bounds** (`engine.cpp:1519-1537`) derive `halfExtent` from
  `waterCfg.width`; the larger sheet enlarges the caustic projection area. Terrain
  caustics are gated by `waterY`, so they still only show on submerged ground —
  verify no caustics bleed onto dry banks after the change.
- **Wave amplitude at the shoreline:** the pond's wave amplitudes sum to ~0.009 m
  (`waves[*].amplitude` = 0.004/0.003/0.002, applied as field `.x` in
  `water.vert.glsl:38`; the `0.25` in the config is `waves[2].speed`, not
  amplitude). This sub-centimetre displacement is negligible. Note the occlusion
  **silhouette** is fixed by the mesh vs. terrain depth, not by the alpha feather
  (`edgeFade` is alpha-only and over dry ground is opaque, not transparent — §3.3);
  so the static `EDGE_PAD` margin, not the fade, is what covers any dynamic reach.
  Confirm visually that the ~9 mm crest does not shimmer at the waterline.
- **Startup fallback (`!u_hasRefractionTex`):** reachable only until the refraction
  FBO is first populated (`water_renderer.cpp:155-159`). In it the `alpha *=
  edgeFade` line is skipped entirely (`water.frag.glsl:285`) and the dry edges stay
  depth-occluded, so there is **no larger square** — only a non-refractive
  submerged disc for ≤ 1 frame. Negligible.

## 7. Verify plan

1. **INV-2 containment test (headless):** call the pure `computePondFill` (§3) with
   the shipped `MeadowShape` and a `meadowHeight01` sampler in
   `tests/test_meadow_terrain.cpp` (headless — file comment line 5). The invariant
   is verified by an **independent flood-fill**, not by restating the construction:
   fill a fine grid from the pond-centre cell, adding any 4-neighbour whose terrain
   height `< waterLevelY`; assert the flooded set **never reaches the grid boundary**
   (`R_SCAN`) — i.e. water at `waterLevelY` cannot escape. This exercises the real
   containment property against a method (flood-fill) *independent* of the
   production min-of-ridge-maxima, so it catches an angular-undersampling error the
   production approximation would miss. Keep `waterLevelY < spillHeight` and
   `waterLevelY ≥ bowlFloorY + MIN_DEPTH` only as **by-construction sanity checks**
   (labelled as such — they cannot fail unless the arithmetic is broken), not as the
   containment proof. (A pre-computed dense-ring min would false-fail here: the outer
   downslope legitimately dips below `waterLevelY` outside the basin, which the
   flood-fill correctly ignores because no monotone sub-`waterY` path reaches it.)
2. **Flood-radius sanity (headless):** assert `computePondFill` returns a finite
   `floodRadius` (no ray hit the `R_SCAN` cap) and `floodRadius < R_rim` for the
   shipped shape.
3. **Visual — no square / no float:** Release meadow → pond sits in the basin,
   waterline follows the ground contour, no straight edges, no floating lip.
4. **Visual — no spill:** walk the low rim → water does not clip through or run
   over the bank.
5. **Visual — banks:** grass grows to the damp shore, the mud band hugs the
   contour (not a square), no grass floats on the water.
6. **`local-ci.sh --windows` green** (build + tests + audit).
7. **Performance (Release):** record meadow FPS before/after; acceptance is
   **≥ 60 FPS maintained** with no regression beyond noise on the RX 6600.

## 8. Constants

Two groups. The **geometric** constants are derived (not art-directed); each is
given with its relationship and a provisional value. The **look** constants are
art-directed and carry the `// TODO: revisit via Formula Workbench` note per
project Rule 6, matching the neighbouring meadow constants (`engine.cpp:2243-2245`).

**Geometric (derived; provisional values for the shipped bowl,
`R_rim ≈ 28 m`, spacing 1 m):**

| Const | Relation | Provisional |
|-------|----------|-------------|
| `MARCH_STEP` | ≤ grid spacing, so no crest is stepped over | `0.5 m` |
| `SCAN_FACTOR` → `R_SCAN` | > 1, far enough to include escape saddles just past the carve boundary | `1.5` → ~42 m |
| `N_RAYS` | large enough that the shoreline-radius change over one angular gap `2π·floodRadius/N_RAYS` is < `EDGE_PAD` | `128` (arc ≈ 0.7 m at 15 m) |
| `EDGE_PAD` | ≥ shoreline-radius variation over one ray gap (above) | `2.0 m` |
| `RIM_MARGIN` | ≥ max **cross-ray** ridge-crest difference between adjacent rays (angular undersampling error) — validated by the §7.1 flood-fill, not assumed | `0.5 m` |

`MIN_DEPTH` is the smallest visually sensible pond depth (`0.5 m`) and only fires
in the degenerate out-of-scope basin (§3.1).

**Look (art-directed, Formula-Workbench TODO):** `DESIRED_DEPTH` (~1.5 m target
depth), `BAND_WIDTH` (mud-band width), `SHORE_MARGIN` (damp margin that keeps
grass off the waterline). These set appearance, have no closed-form derivation,
and each gets the `// TODO: revisit via Formula Workbench` comment at its
definition.

## 9. Out of scope

- General multi-basin / watershed spill solving (§3.1 handles the single authored
  bowl only).
- Non-star-convex flooded regions (§3.2 assumes star-convexity about the centre).
- Arbitrary/multiple ponds, rivers, or flow simulation.
- Editor-authored water bodies (`applyBankBlend` stays for those).
- A bespoke shaped water mesh (depth occlusion + the shallow-band fade make it
  unnecessary).
- Any change to the shared water shader or other water surfaces.
