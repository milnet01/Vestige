# Meadow Pond — Physical Correctness & Natural Shoreline (Design)

**Status:** Draft — cold-eyes loop 1 applied
**Program:** 3D_E-0027 (meadow benchmark scene), Phase 10 terrain/water
**Scope:** Fix the meadow pond so it reads as a real body of water: a level
surface that fills a contained basin, with a natural contour shoreline instead
of a floating opaque square. No water-system rewrite; the shoreline itself is
produced by machinery already in the water shader.

## Contents
- §1 Problem (reproduced)
- §2 Physical model (target)
- §3 Fix (3.1 fill · 3.2 sizing · 3.3 shoreline · 3.4 shore paint · 3.5 foliage)
- §4 CPU / GPU placement
- §5 Reuse
- §6 Risks / edge cases
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
  (logic at `engine/environment/meadow_terrain.cpp:83-93`, using values set at
  `engine/core/engine.cpp:2253-2255`): `pondRadiusGrid = 0.11` (≈ 28 m) and
  `bowlDepth01 = 0.09` (≈ 4.5 m dip at centre) over the 50 m height scale. Note
  `pondRadiusGrid` is the **carve boundary** — `falloff → 0` exactly at that
  radius (`meadow_terrain.cpp:88-92`), beyond which the surface is pure fbm hill.
  It is *not* guaranteed to be the containment rim (see §3.1).
- The water plane is a flat square of `POND_SIZE = 14.0 m`
  (`engine/core/engine.cpp:2279`; assigned to `c.width`/`c.depth` at
  `engine.cpp:2365-2366`), placed at `waterLevelY = bowlFloorY + 1.5f`
  (`engine.cpp:2357-2358`).
- The water shader **already** fades the surface to transparent at the shoreline
  via depth: `waterThickness = max(linearRefract - linearWater, 0.0)`
  (`assets/shaders/water.frag.glsl:235`) drives `edgeFade = smoothstep(0.0,
  u_softEdgeDistance, waterThickness)` on the surface alpha (`:287-288`), with
  `u_softEdgeDistance = 1.0 m` (`engine/renderer/water_renderer.cpp:181`).

Because the plane half-extent (7 m) is far smaller than the flooded radius (the
ground reaches the waterline ≈ 10 m out — working the smoothstep falloff for
water at 3 m below the base gives `t ≈ 0.36` → ~10 m before fbm), the plane's
straight edges sit ≈ 1.5 m underwater. There `waterThickness ≈ 1.5 > 1.0`, so
`edgeFade = 1.0` (fully opaque) — a hard, opaque square edge hanging over the
sunken floor. The true shoreline is never reached by the mesh. Both symptoms
share one cause: **the water sheet is much smaller than its basin.**

Containment is a second, latent issue: `waterLevelY` is `floor + 1.5 m` with no
check that 1.5 m stays below the lowest rim. The rolling fbm relief (octave amps
0.045 + 0.025 + 0.012 ≈ 0.082 **one-sided**, i.e. `±0.082` → ~8 m peak-to-peak
over the 50 m scale) can pull one side of the rim down, so a fixed offset is not
guaranteed to be contained — physically, water at that level would spill out the
low side.

## 2. Physical model (target)

A real pond is a **level (horizontal) surface** filling a closed depression up to
its **spill height** — the lowest point on the surrounding rim water must cross to
escape. Above that height water runs downhill. The waterline is the contour where
that level plane meets the rising ground.

Three invariants:

- **INV-1 (level):** the water surface is a single horizontal plane at `waterY`.
  (Already true — the plane is axis-aligned and flat.)
- **INV-2 (contained):** `waterY < spillHeight`, where `spillHeight` is the
  lowest terrain height on a closed ring fully encircling the basin (§3.1). For
  the single authored radial bowl this ring-min **is** the spill point; a general
  multi-basin watershed is out of scope (§9).
- **INV-3 (contour shoreline):** the pond is visible only where the sheet is over
  submerged ground; its outline is the terrain contour at `waterY`, never a
  straight mesh edge. This is achieved by (a) sizing the sheet so every straight
  edge lands on dry ground, where the existing depth-fade already makes it
  transparent (§3.2/§3.3), and (b) painting the shore band and skipping foliage on
  the same contour (§3.4/§3.5).

## 3. Fix

All but the shoreline itself is CPU-side scene setup in `finalizeMeadowTerrain()`
(`engine/core/engine.cpp:2236`). The shoreline is emergent from the existing
shader `edgeFade` once the plane is sized correctly (§3.3) — **no shader change.**

### 3.1 Contained fill level (INV-2) — CPU, scene setup
After the heightfield is written, compute a conservative spill height by scanning
**every grid cell** on a closed ring around the pond (not a sparse sample): take
the minimum terrain height over all cells whose distance from the pond centre is
within a thin annulus `[R_rim, R_rim + 1 cell]`, where `R_rim` is the bowl radius
in world units (§3.2). Scanning at grid resolution (not N=64 discrete points)
closes the "step over a drainage notch" gap. Then:

```
waterLevelY = min(bowlFloorY + DESIRED_DEPTH, spillHeight - RIM_MARGIN)
waterLevelY = max(waterLevelY, bowlFloorY + MIN_DEPTH)   // degenerate guard
```

The first line keeps the pond below the spill point; the second guards the
degenerate case where a low fbm saddle would push `spillHeight - RIM_MARGIN`
below the floor (which would yield a zero/negative-depth pond). This **reduces
the risk of** a spilling pond to the residual case of a basin that is not simply
connected around `R_rim` — which the authored single bowl is not (the 4.5 m carve
dominates the ±4 m fbm, leaving one central depression). It is not a general
watershed solver; §9.

### 3.2 Water sheet sized to the actual flood (INV-3, M1) — CPU, scene setup
Do **not** size the plane from a magic 14 m, nor from `2·bowlRadius` (which is
both too large — ~18× the area, wasteful on a benchmark — and, being symmetric,
can still leave a submerged straight edge on the low-rim side). Instead measure
the real flooded extent and size to it:

1. `R_rim = pondRadiusGrid · (W-1) · spacingX` (world units; circular because the
   grid is square — §1).
2. Cast `N_RAYS` rays from the pond centre outward; on each, march until
   `terrainHeight ≥ waterLevelY` (the shoreline crossing) and record that radius.
   `floodRadius = max` over all rays (handles an asymmetric waterline).
3. `POND_SIZE = 2 · (floodRadius + EDGE_PAD)`.

`EDGE_PAD` (a few metres) guarantees every straight edge of the square sheet lands
on **dry** ground (terrain above `waterY`) on all sides, including the
farthest-flooded direction. The sheet is then only marginally larger than the
water it shows, so the transparent dry rim (§3.3) is thin — minimal overdraw.

### 3.3 Shoreline is emergent — no shader change
At any fragment over dry ground (terrain above the water plane), the terrain is
nearer the camera than the surface, so `linearRefract ≤ linearWater` →
`waterThickness = 0` → `edgeFade = smoothstep(0, 1, 0) = 0` → surface alpha 0
(`water.frag.glsl:235,287-288`). With §3.2 placing every straight edge on dry
ground, the whole square boundary is already transparent; the visible edge is the
depth-feathered contour just inside it (and the existing foam band at
`water.frag.glsl:270-281`). **No `discard` is added:** it would defeat early-Z for
the draw, and the tight sizing (§3.2) makes the transparent rim small enough that
plain alpha-blend is cheaper than paying the early-Z penalty across the pass.

**Refraction dependency (H2), stated honestly:** this shoreline relies on the
refraction depth texture being bound (`u_hasRefractionTex`). For the meadow pond
that is unconditional — the refraction pass runs every frame water is present
(`engine.cpp:1596-1682`), gated by no quality tier, and the FBO is created at
init. The `!u_hasRefractionTex` fallback (`water.frag.glsl:243-249`, thickness
defaults to 1.0) is reachable only as a ≤1-frame startup transient before the FBO
is first populated (documented at `water_renderer.cpp:155-159`); during it the
enlarged sheet would show as a momentary larger square. This is a bounded,
single-frame startup artifact, accepted as negligible. If a future path disables
refraction persistently for this surface, the shoreline would not apply — so the
dependency is a stated precondition, not an assumption.

### 3.4 Shore band follows the contour (A-M2) — CPU, scene setup
`Terrain::applyBankBlend` (`engine/environment/terrain.cpp:1054-1110`) paints the
damp/mud band against the water **AABB** (a box test + signed-distance-to-
rectangle), so it produces a square-edged mud ring. Keeping it would leave a
straight-edged brown band on the banks even after the water becomes a contour —
contradicting INV-3. Replace it, for the pond, with a **contour band**: paint the
dirt channel (splat channel 2) with weight `1 - smoothstep(0, BAND_WIDTH,
abs(terrainHeight(x,z) - waterLevelY))` over cells near the pond, following the
same splatmap-write pattern `applyBankBlend` already uses. The damp edge then
hugs the waterline contour on all sides. `applyBankBlend` itself is unchanged (it
stays for editor-authored rectangular water bodies); only the meadow pond stops
calling it.

### 3.5 Foliage keyed to the contour (A-M3) — CPU, scene setup
Grass currently skips a disc of `POND_SIZE·0.5 + 3` (`pondSkipRadius`,
`engine.cpp:2395`) and erases a final disc via `eraseAllFoliage(center, radius)`
(`engine.cpp:2428-2429`; `foliage_manager.cpp:160` — a disc, no height
predicate). With §3.2's larger sheet a `POND_SIZE`-derived disc would strip grass
from dry banks. Split the two by capability:

- **Stamp skip → contour:** skip a grass stamp when its centre sits below the
  waterline, `terrainHeight(cx,cz) < waterLevelY + SHORE_MARGIN` — a per-centre
  height test the stamp loop can do directly. This keeps grass growing up to the
  damp shore and none on the water.
- **Final erase → disc at the flood radius:** the erase API is disc-only, so pass
  `eraseAllFoliage(center, floodRadius + SHORE_MARGIN)`. A disc slightly larger
  than the flooded region is correct here (it only removes spill inside the water
  footprint, which is convex for the single bowl); no per-instance contour erase
  is needed.

## 4. CPU / GPU placement

| Work | Where | Why |
|------|-------|-----|
| Spill-height ring scan, fill level, flood-radius rays, plane size, contour shore paint, contour foliage skip, disc erase | **CPU**, one-time in `finalizeMeadowTerrain()` | Branching / sparse / decision, runs once at scene build — not per frame. |
| Per-fragment shoreline fade + foam | **GPU**, `water.frag.glsl` (unchanged) | Per-pixel; already on the GPU via `waterThickness`/`edgeFade`. |

No per-frame CPU cost is added (all new work runs once at scene setup). No new
GPU work is added — the sheet is larger but sized tightly (§3.2), and the dry rim
fades via the existing `edgeFade` with no `discard` (§3.3), so early-Z is
preserved. No dual CPU/GPU impl exists (the shoreline lives only on the GPU; CPU
only positions/sizes the mesh once), so no parity test is required. Performance is
still verified by measurement, not assertion (§7), because this is a benchmark
scene.

## 5. Reuse (no rewrite)

- Shoreline fade + foam: existing `waterThickness`/`edgeFade`
  (`water.frag.glsl:235,270-281,287-288`) — unchanged.
- Terrain sampling: existing `Terrain::getHeight()` (already called at
  `engine.cpp:2357,2419,2427`).
- Contour shore paint: same splatmap-write pattern as
  `Terrain::applyBankBlend`/`generateAutoTexture`.
- Basin carve: existing `meadowHeight01()` bowl — unchanged.
- New code is CPU-only and small: ring-scan + flood-ray + size math, a contour
  splat loop, and a height-test in the existing grass stamp loop. No shader edit.

## 6. Risks / edge cases

- **Containment residual (INV-2):** the ring-min is the spill point only for a
  simply-connected basin around `R_rim`. The authored bowl qualifies (§3.1); a
  hand edit that carves a second dip or a channel through `R_rim` would break it.
  The `MIN_DEPTH` guard prevents a degenerate empty pond but not a mis-authored
  double basin — out of scope (§9).
- **Refraction dependency:** see §3.3 — a ≤1-frame startup square, accepted.
- **Caustics bounds** (`engine.cpp:1519-1537`) derive `halfExtent` from
  `waterCfg.width`; the larger sheet enlarges the caustic projection area. Terrain
  caustics are gated by `waterY`, so they still only show on submerged ground —
  verify no caustics bleed onto dry banks after the change.
- **Wave amplitude at the shoreline:** waves displace the surface by ≤ 0.25 m
  here; the 1 m `softEdgeDistance` feather absorbs that, so the waterline should
  not shimmer — confirm visually.
- **Distorted-UV sampling:** the thickness/foam use `screenUV + totalDistortion`
  (`water.frag.glsl:227`), so the contour is sub-pixel jittered by the wave normal
  map — cosmetic and generally desirable; noted, not a defect.
- **Benchmark intent:** the sheet grows but is sized tightly and adds no
  `discard`; confirm 60 FPS is unaffected with a measured Release delta (§7).

## 7. Verify plan

1. **INV-2 unit test (headless):** in `tests/test_meadow_terrain.cpp` (which
   already exercises `meadowHeight01`), assert `waterLevelY < min(terrainHeight)`
   over a dense rim ring for the shipped `MeadowShape`, and `waterLevelY ≥
   bowlFloorY + MIN_DEPTH`. Locks containment as a regression with no GPU.
2. **Flood-radius sanity (headless):** assert the measured `floodRadius` is finite
   and `< R_rim` for the shipped shape (the pond is contained within the bowl).
3. **Visual — no square / no float:** Release meadow → pond sits in the basin,
   waterline follows the ground contour, no straight edges, no floating lip.
4. **Visual — no spill:** walk the low rim → water does not clip through or run
   over the bank.
5. **Visual — banks:** grass grows to the damp shore, the mud band hugs the
   contour (not a square), no grass floats on the water.
6. **`local-ci.sh --windows` green** (build + tests + audit).
7. **Performance (Release):** record meadow FPS before/after; acceptance is
   **≥ 60 FPS maintained** with no regression beyond noise on the RX 6600.

## 8. Constants (hand-authored)

`DESIRED_DEPTH`, `RIM_MARGIN`, `MIN_DEPTH`, `EDGE_PAD`, `N_RAYS`, `BAND_WIDTH`,
`SHORE_MARGIN` are art-/geometry-directed with no reference dataset to fit
against, matching the neighbouring meadow constants that already carry the note
(`engine.cpp:2243-2245`). Each gets a `// TODO: revisit via Formula Workbench`
comment at its definition per project Rule 6.

## 9. Out of scope

- General multi-basin / watershed spill solving (§3.1 handles the single authored
  bowl only).
- Arbitrary/multiple ponds, rivers, or flow simulation.
- Editor-authored water bodies (`applyBankBlend` stays for those).
- A bespoke shaped water mesh (the contour-from-depth fade makes it unnecessary).
- Any change to the shared water shader or other water surfaces.
