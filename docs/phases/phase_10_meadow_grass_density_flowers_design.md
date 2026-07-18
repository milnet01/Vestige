# Phase 10 — Meadow Realism B+: Dense Grass Field, Earthy Bare Ground, Clustered Wildflowers

Follow-on refinement to the shipped **3D_E-0038** (realistic grass, Phase B) after
user visual feedback: the grass currently reads as **isolated spiky tufts on a
mowed lawn**, not a real meadow. Reference photos show a **dense continuous grass
sward** with **earthy (not manicured) bare patches** and **clustered wildflowers**.
Touches Phase A terrain auto-texturing (3D_E-0031) for the bare-ground fix.
Fixture: the 3D_E-0027 meadow.

Layman line: *make the field look like a real meadow — thick continuous grass,
bare spots that look like soil not a lawn, and wildflowers in patches — and keep
it at 60 FPS.*

**Sections:** 1 Goal · 2 Research · 3 Current state · 4 Architecture (4.1 earthy
ground · 4.2 dense grass · 4.3 wildflowers · 4.4 perf tier) · 5 CPU/GPU placement
· 6 Performance · 7 Testing · 8 Assets · 9 Slices (C1–C4) · 10 Accessibility · 11
Risks · 12 Open questions · 13 Cold-eyes log · 14 Sources.

---

## 1. Goal

Turn the meadow's isolated-tuft grass into a believable meadow:

1. **Dense continuous grass** near the camera (no spiky-clump-on-lawn look), via
   **wider cards + higher near-field density + more variation**, thinning with
   distance so the instance count — and the overdraw it drives — stays bounded.
2. **Earthy bare ground.** Where grass is sparse, the terrain reads as **soil**,
   not a smooth green lawn — a noise-driven dirt-patch term in the terrain
   auto-texturing (opt-in; non-meadow scenes unchanged). This *also* reduces how
   much grass is needed to look right (a perf win — bare soil is a legitimate,
   pretty part of a meadow, per the user).
3. **Clustered wildflowers** through the grass (lupine/daisy/poppy-style), using
   the never-painted billboard "flower" foliage type, painted in **species
   clusters** (real wildflowers grow in patches, not uniformly spread).
4. **60 FPS at High on the RX 6600** held throughout, with the existing B3 grass
   quality tier as the weak-hardware lever.

### 1.1 Non-goals (this phase)

- **No new grass geometry technique.** Still the instanced 3-quad star mesh +
  distance-fade (`foliage_renderer.cpp:355-461`). No compute/mesh-shader grass, no
  GPU-driven blade generation, no per-blade Bézier — those stay escalations.
- **No terrain-tile "painted grass" far-field LOD** and **no GPU culling rewrite**
  this phase. The distance-fade + wider cards + bounded near-field density are the
  levers; a far-field imposter/LOD is future work if the perf gate demands it.
- **No new authored 3D flower meshes.** Wildflowers are billboards (reusing the
  foliage pipeline) + optionally the *existing* Kenney `.glb` flowers, retuned.
- **No change to the foliage instance/chunk data model or the paint/erase API.**

---

## 2. Research summary (what current practice recommends)

- **Dense grass is overdraw-bound, not vertex-bound.** Thin alpha-tested/blended
  cards waste discarded pixels; the cost scales with on-screen coverage ×
  overdraw depth, so the win is limiting *how much transparent grass fills the
  frame*, not the vertex count. (hexaquo "Grass LOD" series; 80.lv "Next-Gen Grass
  in UE4".)
- **Distance-based density / LOD is the primary perf lever** — dense near the
  camera, thinning to none (or to a flat textured ground) in the distance. Vestige
  already has the distance-alpha fade (`foliage.vert.glsl:57-59`); this phase
  leans on it and adds width instead of far-field count.
- **Wider clumps cover more ground per instance.** Fewer, wider cards give
  continuous coverage at a lower instance count (less overdraw) than many skinny
  ones. (Unity terrain-detail "billboard" clumps; GPU Instancer notes.)
- **Wildflowers cluster by species.** Real meadows have lupine patches, daisy
  drifts — not an even sprinkle. Cluster-centre placement reads more natural than
  a uniform grid.
- **Escalations deliberately deferred:** GPU-culled indirect grass, far-field
  imposters, a compute-driven density field. Noted so a later phase can slot them
  in if the RX 6600 gate ever fails.

Sources: §14.

---

## 3. Current state (verified against source)

- **Terrain auto-texturing paints dirt by *altitude only*.** `Terrain::
  generateAutoTexture` (`terrain.cpp:974-1052`) computes per-texel splat weights
  from slope + altitude + fbm-perturbed thresholds; `grass = 1.0` is the
  unconditional base (`:1011`), rock is slope-gated, sand/dirt are altitude-gated
  (`:1013-1015`), dirt only blends in at **high** normalized height
  (`altitudeDirtStart 0.25`, `terrain.h:250`). On the flat meadow interior none of
  rock/sand/dirt fire, so **the bare ground is uniform grass green** — the
  mowed-lawn look. The meadow builder (`engine.cpp` `finalizeMeadowTerrain`
  ~2380-2420) overrides only the sand thresholds + `noiseAmplitude`. Splat channel
  map: **R=grass, G=rock, B=dirt, A=sand**. Per-texel dirt primitive
  `Terrain::blendBankChannel(x,z,channel,factor)` exists (`terrain.cpp:1144-1171`,
  used by the pond mud contour); there is **no** noise/mask dirt-scatter today.
- **Grass card is 0.15 m × 0.4 m, 3 crossed quads (18 verts).** `createStarMesh`
  `halfWidth = 0.075f`, `height = 0.4f` (`foliage_renderer.cpp:361-362`).
  Per-instance `i_scale` 0.6–1.8 (`engine.cpp` grassCfg ~2478) → on-screen width
  ~0.09–0.27 m.
- **~40 k grass instances, sparse.** Grass block (`engine.cpp` ~2455-2520):
  `STAMP_SPACING 5`, `STAMP_RADIUS 3.5`, `GRASS_DENSITY 0.53`, `GRASS_FALLOFF
  0.30`. `paintFoliage` fills `π·r²·density` uniform points per stamp
  (`foliage_manager.cpp:35-38`). Documented tested range **~10 k–120 k** (raise
  `GRASS_DENSITY` toward ~2.0 for the ceiling) — the deliberate 3D_E-0027 perf
  knob.
- **Foliage is instanced, alpha-tested + alpha-blended, two-sided.**
  `glDrawArraysInstanced(GL_TRIANGLES,0,18,n)` per type
  (`foliage_renderer.cpp:245-247`); `ScopedBlendState{true,SRC_ALPHA,ONE_MINUS…}` +
  `ScopedCullFace{false}` (~228-231); the shader also `discard`s `a<0.5`
  (`foliage.frag.glsl:112-113`). So it generates overdraw.
- **Dense-grass cost is currently UNMEASURED.** The fly-through capture put foliage
  at **~0.09 ms (<1 %)** (`phase_10_performance_scalability_strategy.md:77`) — but
  that doc explicitly warns the fly-through under-samples dense grass and a
  "grass-filling-the-frame" vantage is needed (`:90-99`). Meadow Release ≈ **181
  FPS / ~5.5 ms on the RX 6600** (`:15,64-65`); a Steam-Deck/GTX-1650 estimate is
  already **~25–30 ms (33–40 FPS)**, below 60 (`:98-99`) — so weak HW has no
  headroom for naive density, RX 6600 does.
- **Foliage "flower" type 2 is generated but never painted.**
  `generateProceduralTexture(2)` builds a stem+petal card
  (`foliage_renderer.cpp:540-571`); the meadow paints only `GRASS_TYPE_ID = 0`.
  `paintFoliage(typeId,center,radius,density,falloff,cfg)`
  (`foliage_manager.cpp:21-110`) fills one disc — **one call per cluster centre =
  one cluster**. `setTypeTexture(typeId,path)` (B1) can swap a real flower texture
  into any slot.
- **Flowers today are spread `.glb` props, not clustered.** The `flower`
  `ScatterParams` (`engine.cpp` ~2640) scatters `flower_{purple,red,yellow}A.glb`
  on a `cellSize 14` jittered grid — uniform, not patchy; the comment flags this
  group as "the largest draw-call load of the props" (per-entity, not instanced).
- **B3 grass quality tier drives distance + shadows only.** `FoliageQuality`
  {Low,Medium,High} → `renderDistance` (100/70/45 m) + `castShadows`
  (`foliage_renderer.cpp` `setQuality`; wired via `RendererQualitySink`). No
  density lever.

---

## 4. Architecture

### 4.1 Earthy bare ground (terrain auto-texture, opt-in)

Add a **noise-driven grass→dirt patch term** to `generateAutoTexture`, gated by
new `AutoTextureConfig` fields so existing scenes are byte-unchanged:

- New config (`terrain.h` `AutoTextureConfig`, all default to the no-op 0):
  - `float dirtPatchAmount = 0.0f;` — max fraction of grass a patch converts to
    dirt (meadow sets ~0.5).
  - `float dirtPatchScale = 0.15f;` — patch noise frequency (world-space);
    controls patch size.
  - `float dirtPatchThreshold = 0.55f;` — noise value above which dirt starts
    (higher → fewer/smaller patches).
- In the per-texel loop (`terrain.cpp` ~1011-1024), after the existing weights and
  **only when `dirtPatchAmount > 0`**, sample a second low-frequency fbm
  `p = fbmNoise(wx*dirtPatchScale, wz*dirtPatchScale)` remapped to 0..1, compute
  `patch = smoothstep(dirtPatchThreshold, 1.0, p) * dirtPatchAmount`, then move
  weight from grass to dirt: `dirt += grass * patch; grass *= (1 - patch);` before
  the existing normalize (`:1027-1041`). This uses a **separate** noise lookup from
  the threshold-perturbation noise so patch layout is independent of slope/height.
- The meadow builder sets `autoTex.dirtPatchAmount ≈ 0.45`,
  `dirtPatchScale ≈ 0.08–0.15`, `dirtPatchThreshold ≈ 0.6` so ~15–30 % of the flat
  field reads as soil, in organic patches — earthy bare ground, not lawn.
- **Why in `generateAutoTexture`, not a meadow-only scatter:** it is the single
  writer of `m_splatData` and already the auto-texture policy home; the opt-in flag
  keeps Tabernacle/other scenes at their current behaviour (`dirtPatchAmount`
  defaults 0). The dirt layer already exists (channel 2, `dirt_albedo.jpg`), so no
  new asset.

### 4.2 Dense continuous grass

Three changes, all tuning-scale (no new render tech):

1. **Wider cards.** Raise `createStarMesh` `halfWidth` from `0.075f` to **~0.13f**
   (card ~0.26 m wide) so each clump covers more ground → continuous coverage at a
   *lower* instance count than skinny cards (fewer instances = less overdraw). Keep
   `height 0.4f` (per-instance scale still varies it). This is a **global foliage**
   change — verify it doesn't harm other foliage users (the only painter today is
   the meadow grass; procedural types 1–3 are unused in the shipped scenes), else
   gate width per-type. *(Open question §12.)*
2. **Higher near-field density, bounded.** Raise `GRASS_DENSITY` (0.53 → ~**1.3**)
   and/or drop `STAMP_SPACING` so the near field is continuous, targeting an
   instance budget of **≤ ~120 k** (top of the documented tested range) — *not*
   uniform max density. The existing **distance-alpha fade**
   (`foliage.vert.glsl:57-59`) thins far grass; combined with the tier's
   `renderDistance`, far grass is culled so the count stays bounded. Record the
   actual `getTotalFoliageCount()` and keep it ≤ budget.
3. **More variation.** Widen `grassCfg.minScale/maxScale` (e.g. 0.5–2.0) and
   `tintVariation`; rotation is already random. This breaks the repeated-card read.
   Optionally paint a second, sparser pass of a **shorter** grass (smaller scale)
   for a two-height sward — optional polish (§12).

### 4.3 Clustered wildflowers (billboard foliage)

- **Real CC0 flower textures** on foliage types via `setTypeTexture` (B1 path):
  reuse type **2** for one species and, if a second/third species is wanted, paint
  types **1 and 3** with flower textures too (three unused slots available — types
  1 (tall grass) and 3 (fern) are not painted in the meadow). Each is a portrait
  flower card (stem + bloom, alpha).
- **Cluster placement:** generate **cluster centres** with a seeded scatter (reuse
  `scatterProps` with a coarse `cellSize`, or a small hand-authored/seeded list),
  then `paintFoliage(flowerType, center, clusterRadius≈1.5–2.5 m, clusterDensity,
  falloff, cfg)` per centre — each call fills one disc = one species patch. Vary
  which species per cluster so patches read as lupine drifts / daisy patches. Keep
  total flower instances modest (**≤ ~10 k**, sparse accents) — cheap on the
  instanced path.
- **Existing `.glb` flowers:** reduce their spread density (raise `cellSize`) so
  they become occasional 3D accents rather than a uniform sprinkle, or drop them in
  favour of the billboard clusters — resolved during C3 by visual read (§12).
- Flowers respect the same distance-fade and (optionally) the B3 tier distance.

### 4.4 Performance: measure, then tier

- **Measure the worst case first.** Add/confirm a **grass-filling-the-frame**
  visual-test vantage (ground-level looking across the dense field) and read the
  `beginPass("Foliage")` GPU scope in **Release** on the RX 6600 — the fly-through
  number (~0.09 ms) is not representative (§3).
- **Hold ≥ 60 FPS at High on the RX 6600** (the hard gate). Levers already in hand:
  wider cards (fewer instances), bounded near-field density + distance-fade
  (bounded overdraw), earthy ground (less grass needed). If the worst-case Foliage
  pass still threatens the frame, the escalation is **B3-tier density scaling** (a
  runtime instance-fraction or a shorter dense-radius) — added **only if measured
  necessary** (avoid speculative complexity).
- **Weak-HW path = the existing B3 tier** (Low: 45 m distance + 0.66 render scale)
  plus, if C4 measurement shows it necessary, a density step-down. The Steam-Deck
  60 FPS target remains the broader perf-scalability program's job (3D_E-0028/29),
  not a blocker here.

---

## 5. CPU / GPU placement (project Rule 7)

| Work | Placement | Reason |
|---|---|---|
| Dirt-patch splat term (fbm + weight move) | **CPU**, one-time in `generateAutoTexture` | Runs at terrain build; writes the splatmap once. Not per-frame. |
| Splatmap sample + layer blend (incl. dirt patches) | **GPU** (terrain fragment) | Already GPU; the new dirt just shifts existing per-texel weights. |
| Grass/flower instance generation (paint) | **CPU**, one-time at meadow build | Placement + RNG; sparse/decision logic. |
| Wider-card mesh, per-instance transform, wind, alpha-test, distance-fade | **GPU** (vertex/fragment) | Per-vertex/per-pixel, data-parallel; unchanged pipeline. |
| Tier selection (preset → distance/shadow/■density) | **CPU** | One-time branching decision on setting change. |

No "CPU now, move later." No fitted formula → no Formula-Workbench parity mirror.

---

## 6. Performance (60 FPS is a hard requirement)

- **Hard gate: ≥ 60 FPS on the full meadow at High on the RX 6600**, read in
  **Release** from the editor Performance panel — measured at a **dense,
  ground-level, grass-filling-the-frame** vantage (the true worst case; the
  shipped fly-through under-samples it — §3). The **Foliage** `beginPass` scope
  ≤ **2.5 ms** at High is the advisory watch-line (not a blocking bound); only
  dropping below 60 FPS blocks the phase.
- **The three instance-count levers keep overdraw bounded:** (a) wider cards →
  fewer instances for the same coverage; (b) near-field density capped to a
  **≤ ~120 k** budget (top of the tested range) with the distance-alpha fade
  culling far grass; (c) earthy bare ground means less grass is needed to look
  right. Overdraw — not vertices — is the cost (§2), so bounding on-screen
  transparent coverage is the whole game.
- **Baseline honesty:** the current dense-grass Foliage cost is **unmeasured**
  (§3). C4 records the measured worst-case Foliage-pass ms as the baseline at
  first Release read, then confirms the gate; a runtime **density** step-down for
  the tier is added **only if that measurement requires it**, and if added is
  logged per project Rule 5 (it caps a feature for perf).
- **Weak HW** (Steam Deck / GTX 1650, already ~33–40 FPS with sparse grass — §3)
  rides the existing **B3 tier** (Low = 45 m + 0.66 render scale). Bringing weak
  HW fully to 60 is the perf-scalability program's remit (3D_E-0028/0029), not a
  gate for this visual phase.
- **Tiers are a graphics `Setting`, NOT `FormulaQualityManager`** — same axis
  distinction as A5/B3. Default High; no auto-detection this phase.

---

## 7. Testing

- **Dirt-patch splat term (unit).** Factor the patch math as a pure helper
  `float grassDirtPatchWeight(float noise01, float threshold, float amount)`
  (returns the grass→dirt fraction) and unit-test it: below threshold → 0; at
  noise 1 → `amount`; monotonic between; `amount 0` → 0 (the opt-out). Assert the
  post-move weights still sum to 1 after normalize on a hand case. No GL.
- **Grass card width (unit/behavioural).** A pure check that the star-mesh
  half-width constant feeds the emitted vertices (guard against a silent revert):
  assert the generated quad's width equals `2·halfWidth`. (Mesh build is GL, but
  the vertex positions are computed CPU-side in `createStarMesh` — extract the
  corner math if needed, else assert the constant.) *(Confirm testable seam in
  C2; if not cleanly unit-testable, this is a visual-test-only check.)*
- **Cluster placement (unit).** The cluster-centre generator (seeded) is
  deterministic → assert a fixed seed yields a stable set of centres within the
  region and outside the pond exclusion. Reuse the `scatterProps` test pattern
  (`test_meadow_terrain.cpp`).
- **Objective render check.** Terrain + foliage shaders link and a meadow frame
  renders **GL-error-free** (`glGetError`) with the wider cards + dirt patches +
  flower clusters.
- **Visual (`--visual-test`) — maintainer inspection against the references.**
  Ground-level `open_meadow` + a new dense-vantage viewpoint: grass reads as a
  **continuous field** (no isolated-spike-on-lawn look), bare patches read as
  **soil not lawn**, wildflowers appear in **clusters**, no aspect squish. Plus the
  Release perf read (§6).

---

## 8. Assets & licensing

- **Wildflower textures — CC0**, portrait flower cards (stem + bloom, alpha),
  ~256×256, committed under `assets/textures/foliage/` (e.g.
  `flower_lupine.png`, `flower_daisy.png`, `flower_poppy.png`), each with a
  git-ignored `assets/textures/foliage_local/` override (same hook as
  `grass_blades.png`). Sources: CC0 flower cut-outs (OpenGameArt / ambientCG /
  3DTexel); final pick during C3, license recorded verbatim at fetch time. Rows
  added to `ASSET_LICENSES.md` + `THIRD_PARTY_NOTICES.md`.
- **No new ground texture** — the dirt-patch fix reuses the existing Phase-A dirt
  layer (`assets/textures/terrain/dirt_*`).
- **No new flower meshes** — billboards reuse the foliage pipeline; the existing
  Kenney `.glb` flowers are retuned, not added to.
- Each flower card is a few hundred KB; `copy_assets` globs `assets/` (no CMake
  change).

---

## 9. Implementation slices

1. **C1 — Earthy bare ground.** Add the opt-in `dirtPatchAmount/Scale/Threshold`
   to `AutoTextureConfig` + the noise-driven grass→dirt term in
   `generateAutoTexture` (pure helper `grassDirtPatchWeight`, unit-tested); the
   meadow sets the patch config. *Verify:* unit test green; `--visual-test` bare
   ground reads as soil patches, non-meadow scenes unchanged (Tabernacle spot
   check), GL-error-free.
2. **C2 — Dense continuous grass.** Widen the grass card (`halfWidth`), raise
   near-field density to the budget, widen scale/tint variation; record
   `getTotalFoliageCount()` ≤ ~120 k. *Verify:* `--visual-test` — continuous field,
   no lawn-tuft look; instance count logged within budget; GL-error-free.
3. **C3 — Clustered wildflowers.** Commit CC0 flower card(s); paint billboard
   flower type(s) in seeded species clusters via `paintFoliage`; retune/trim the
   `.glb` flower spread. *Verify:* `--visual-test` — flower clusters read through
   the grass like the references; ASSET_LICENSES + THIRD_PARTY rows; GL-error-free.
4. **C4 — Perf measure + tier.** Add the dense ground-level worst-case vantage;
   Release Performance-panel Foliage read on the RX 6600 at High; confirm ≥ 60 FPS.
   Add a B3-tier **density** step-down **only if** the measurement requires it
   (logged per Rule 5 if so). *Verify:* ≥ 60 FPS at High on RX 6600 at the dense
   vantage; CHANGELOG row.

Each slice commits locally; the phase pushes when C4 lands green (public repo,
batch push).

---

## 10. Accessibility

- Meadow legibility must not rely on hue alone: value contrast between grass,
  soil patches, and flowers carries the read (colour-blind safe). Flowers add
  colour variety but the grass/soil value structure stands without it.
- The denser grass + wind stays gentle (amplitude a small constant scaled by wind
  speed, `engine.cpp:1625`). The Low quality tier (shorter distance, and — if C4
  adds it — reduced density) doubles as the low-end-GPU / reduced-detail path.

---

## 11. Risks & mitigations

- **Dense-grass overdraw blows the RX 6600 frame** → measure worst case first
  (C4); wider cards + bounded density + distance-fade keep overdraw down; escalate
  to tier density scaling only if measured necessary (Rule 5 logged).
- **Wider cards look blocky up close / hurt other foliage** → per-instance scale
  variation + moderate width; if any other foliage type regresses, gate width
  per-type (§12).
- **Dirt patches look muddy / wrong scale** → tunable `dirtPatchScale/Amount/
  Threshold` + visual iteration; opt-in flag keeps other scenes safe.
- **Flower clusters read as spammy or too uniform** → modest total count, species
  variety per cluster, seeded placement tuned by visual read.
- **Instance-count creep breaks the 3D_E-0027/0030 perf fixture baseline** → record
  the new count; the benchmark is *meant* to stress-test, and the perf gate
  (3D_E-0030) re-baselines on the maintainer's hardware capture.

---

## 12. Open questions for review

- **Grass card width — global vs per-type?** Widening `createStarMesh` halfWidth
  changes all foliage types. The meadow only paints grass today (types 1–3 unused
  in shipped scenes), so a global bump is safe now; if flowers reuse types 1/2/3
  (C3) they'd inherit the wider card — acceptable for flower clumps, but confirm
  during C2/C3 or make width per-type if it looks wrong.
- **Keep or drop the `.glb` flowers?** Billboard clusters may fully replace them;
  decide by visual read in C3 (keeping them adds 3D depth at draw-call cost).
- **Second (short) grass pass?** Optional two-height sward — include only if the
  single-height field still reads too uniform after C2.

---

## 13. Cold-eyes loop log

(populated as the loops run — project Rule 9 / global Rule 14: fresh subagent per
loop, no authoring context, loop until no substantive verified finding remains.)

---

## 14. Sources

- hexaquo, "Grass Rendering Series Part 4 — LOD Tricks for Infinite Plains of
  Grass" — https://hexaquo.at/pages/grass-rendering-series-part-4-level-of-detail-tricks-for-infinite-plains-of-grass-in-godot/
- 80.lv, "Creating Next-Gen Grass in UE4" — https://80.lv/articles/creating-next-gen-grass-in-ue4
- giordi91, "Grass Shader" — https://giordi91.github.io/post/grass/
- Codrops, "How to Make the Fluffiest Grass with Three.js" (2025) — https://tympanus.net/codrops/2025/02/04/how-to-make-the-fluffiest-grass-with-three-js/
- Unity Manual, "Grass and other details" — https://docs.unity3d.com/Manual/terrain-Grass.html
