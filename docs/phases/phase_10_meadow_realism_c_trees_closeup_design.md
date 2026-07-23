# Phase 10 — Meadow Realism C: Close-Up Tree Quality (3D_E-0033, slices T8–T10)

**Status:** Signed off — cold-eyes converged at loop 6 (2026-07-23). Ready to implement T8 → T9 → T10.
**Parent:** `phase_10_meadow_realism_c_trees_plants_design.md` (T1–T7). This doc adds a
close-up quality pass on the already-shipped TreeRenderer. It does **not** change the
LOD bucketing, species table, placement, or asset pipeline — those are done and verified.

**Roadmap:** extends 3D_E-0033. The parent bullet already lists the open polish this
targets: *"LOD crossfade fade-in/out still perceptible (user OK'd shipping into rc.1)."*
User request (2026-07-23): trees still look flat/cardboard up close and LODs still pop.

---

## 1. Goal (plain terms)

Three things the eye catches when you walk up to a tree today, and what each fix does:

1. **Leaves look like flat cardboard.** The leaf cards are lit as if they were dead-flat
   sheets, so light slides across a whole cluster uniformly instead of catching individual
   leaves. **Fix:** feed the leaf/bark *normal maps that already ship with the models* into
   the tree shader, so light responds to the fine surface shape (a "bump texture") — the
   same trick the main scene shader already uses for everything else.

2. **Leaf edges look like cut paper.** Each leaf card has a hard, aliased outline and the
   canopy thins/sparkles as you move. **Fix:** two cheap, standard alpha-cutout tricks —
   keep leaf density stable at distance, and antialias the cutout edge (real edge smoothing
   where the hardware allows it).

3. **Trees visibly "pop" between detail levels.** As you approach, a tree jumps from its
   far/mid stand-in to its full model. **Fix:** make the swap a *dissolve* (leaves fade
   away pixel-by-pixel) instead of the current cross-blend that swaps whole silhouettes,
   and apply the same dissolve to the tree's ground shadow so the shadow stops snapping.

In slice terms: goal 1 → T8, goal 2 → T10, goal 3 → T9 (build order T8 → T9 → T10, for the
dependency reason in §9). No new assets, no new subsystem. All three are edits to the existing tree draw path and
its two shader pairs.

## 2. Current state (verified against source 2026-07-23)

Files: `engine/renderer/tree_renderer.{h,cpp}`, `assets/shaders/tree_mesh.{vert,frag}.glsl`,
`assets/shaders/tree_shadow.{vert,frag}.glsl`.

**Normal maps are on disk and already loaded — just never bound.**
- Every LOLIPOP gameready glTF references `*_normal.png` via `normalTexture` (bark +
  leaf-cluster + billboard variants; verified on maple lod0). Packs also ship
  `*_metallicRoughness.png`.
- The glTF loader already parses them: `gltf_loader.cpp` calls `material->setNormalMap(...)`;
  `Material` stores `m_normalMap` with `hasNormalMap()`/`getNormalMap()` (`material.h`).
- **But the tree draw path binds diffuse only.** `TreeRenderer::drawMeshTier` and
  `drawShadowTier` bind `getDiffuseTexture()` → texture unit 0; unit 3 is the CSM array.
  No normal map is bound and neither tree shader declares a normal sampler.

**Tangent basis is available in the mesh VAO but not consumed by the tree shader.**
- Mesh vertex layout: `0=pos, 1=normal, 2=color, 3=texCoord, 4=tangent, 5=bitangent`
  (`scene.vert.glsl:9-14`, `mesh.*`). `scene.vert/frag` already build and use a TBN.
- `tree_mesh.vert.glsl:10-16` declares only `0,1,3` plus instance attrs `i_model` @6–9 and
  `i_alpha` @12. Locations 4/5 are free of collision — adding a TBN is a *declaration + bind*
  change, not a vertex-format change. It emits geometric `v_normal` only.

**Leaf vs bark is a per-material property, not a flag.**
- `drawMeshTier` sets `u_useAlphaTest = (getAlphaMode()==MASK)` and disables face culling for
  `isDoubleSided()` materials. The frag shader (`tree_mesh.frag.glsl:121-129`) already uses
  two-sided `abs(N·L)` half-Lambert + backlit translucency for leaf cards and signed
  half-Lambert for bark. The far impostor's `BLEND` material is force-converted to alpha
  cutout at cutoff **0.4** to avoid halos (`tree_renderer.cpp` ~349-355, 490).

**Crossfade today is a two-tier alpha *blend*, and that is the popping.**
- `render()` draws both tiers in a `fadeRange = 15 m` band with complementary `v_alpha`
  (`1-t` / `t`), under `ScopedBlendState{GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA}`
  (`tree_renderer.cpp` ~429-463, 484). Thresholds: `lodDistance = 45`, `billboardDistance
  = 180`, `maxDistance = 350`.
- Root causes of the visible pop, verified:
  1. **Leaf cutout `discard` happens before crossfade alpha is used** (`frag:102-103`), so a
     fading leaf tier is drawn at *full silhouette* then blended — a cross-blend of two whole
     shapes, never a dissolve.
  2. **Impostor is a hard cutout while the mid mesh blends** — the mid→far fade is asymmetric,
     so impostor cards appear/disappear abruptly against a smoothly-fading mid mesh.
  3. **Shadow pass has no crossfade at all** — casters hard-switch LOD0→mid at `lodDistance`
     (`tree_renderer.cpp:605`) and hard-cull to nothing at `billboardDistance` (`:594`), so the ground
     shadow silhouette snaps at both boundaries.
  4. **Double-draw density pulse** — both tiers with two-sided leaves + blend briefly double
     canopy overdraw, reading as a brightness/thickness pulse.

**AA mode reality (decides the edge-softening path).**
- Vestige ships `AntiAliasMode { NONE, MSAA_4X, TAA, SMAA, FXAA }` — all five are live
  (`taa.h:19-27`; FXAA is a shipping post-process mode, not planned). **A multisample scene FBO
  is allocated only for `MSAA_4X`** (`renderer.cpp:2255`: `wantSamples = (mode==MSAA_4X)?4:1`);
  `NONE`/`TAA`/`SMAA`/`FXAA` render the scene to a *1-sample* FBO. So `GL_SAMPLE_ALPHA_TO_COVERAGE`
  only produces true edge AA in `MSAA_4X` — the design must not depend on it in the other four modes.
- **The AA mode is not on `TreeRenderer` today.** It lives on the renderer
  (`renderer.h:292 getAntiAliasMode()`); the tree pass is invoked from `engine.cpp:1653` where
  `m_renderer` is in scope. **T10** plumbs it in as a public `bool msaaActive` field on `TreeRenderer`,
  set at that call site (`m_renderer->getAntiAliasMode()==MSAA_4X`) exactly like the existing
  public `windDirection`/`windAmplitude` fields set at `engine.cpp:1651-1652`. (T10, not T9 — the flag
  exists only to gate T10's A2C; §5.)

## 3. Design decisions & rationale

| # | Decision | Why |
|---|----------|-----|
| D1 | **Bind the already-loaded normal map to a new unit and add a TBN to `tree_mesh.*`.** Reuse the `scene.*` TBN + normal-sample pattern; don't invent one. | Normals are on disk and in `Material` for free (§2). This is the single highest-payoff close-up change. Global rule 3 (reuse). |
| D2 | **The sampled normal replaces the geometric `N` inside the *existing* leaf/bark lighting — no viewer-flip is introduced.** | Leaf diffuse is already two-sided `abs(dot(N,L))` (never black on a backlit underside); a per-pixel `N` inherits that guarantee, so the 52f4b4d fix can't regress and no extra flip logic is needed. Simplest correct (rule 2). |
| D3 | **Soft edges = coverage-preservation (all modes) + fwidth sharpen (all modes) + `GL_SAMPLE_ALPHA_TO_COVERAGE` only when `MSAA_4X`.** Sequenced as T10, *after* the dissolve (D4). | Coverage/sharpen are per-pixel math that work everywhere and cost ~nothing; genuine edge AA needs multisample, which only `MSAA_4X` has (§2). TAA/SMAA/FXAA soften the sharpened edge in their own modes. A2C needs the crossfade blend gone first (it claims the alpha channel), so this **depends on D4's dissolve** landing first. Ben Golus. |
| D4 | **Crossfade becomes a *dithered dissolve* for cutout tiers, driven by the existing `interleavedGradientNoise`.** Discard leaf fragments where `v_alpha < noise(gl_FragCoord)`. | Fixes root cause #1: a fading tier truly thins pixel-by-pixel instead of cross-blending whole silhouettes. The noise function already exists in the frag shader (shadow PCF). |
| D5 | **Apply the same dissolve threshold in `tree_shadow.frag` and give the shadow pass the crossfade alpha.** | Fixes root cause #3 — the ground shadow dissolves in lockstep with the canopy instead of snapping. |
| D6 | **Keep the LOD thresholds and `fadeRange` as-is for now; widen only if the dissolve alone doesn't settle it.** Do not retune blindly. | Global rule 2/11 (shortest correct, stay in lane). The dither dissolve is the mechanism fix; band-width is a tuning knob, changed only with a before/after in hand. |
| D7 | **No PBR metallic/roughness this pass.** Normal + existing half-Lambert/translucency only. | Scope cap. Full PBR for trees is a separate, larger change; the flat-cardboard complaint is a *normal* problem, not a BRDF problem. Rule 2. |

## 4. Architecture

### 4.1 T8 — Normal-mapped leaf cards & bark

**Vertex (`tree_mesh.vert.glsl`):**
- Declare `layout(location = 4) in vec3 a_tangent;` and `layout(location = 5) in vec3 a_bitangent;`.
  These are already enabled and populated in the mesh VAO (`mesh.cpp:199-207` formats loc 4/5
  unconditionally). The loaded gameready tree meshes carry **no artist `TANGENT`**, so the loader
  synthesizes tangents from position + UV via `calculateTangents` (`gltf_loader.cpp:921-923`); either
  way loc 4/5 are filled. `bindInstanceBuffers` only touches loc 6-9/12 — so this is a
  **shader-declaration-only** change, no C++ VAO work (confirmed precondition, §10 R1).
- Build the world-space tangent/bitangent with the same `nm = mat3(i_model)*mat3(u_nodeMatrix)`
  used for the normal: `safeNormalize` each of `nm * a_tangent` and `nm * a_bitangent` with a
  world-axis fallback, mirroring `scene.vert`'s TBN build (`scene.vert.glsl:223-226`, which
  independently normalizes T/B/N — no Gram-Schmidt — with a fallback so a degenerate synthesized
  tangent can't produce NaN lighting); emit `v_tangent`, `v_bitangent` alongside `v_normal`. Note
  `safeNormalize` must be **copied into** `tree_mesh.vert` — there is no shared GLSL `#include`; the
  helper is duplicated per shader today (`scene.vert.glsl:96`), the same copy the shadow dissolve makes
  for `interleavedGradientNoise` (§4.2).

**Fragment (`tree_mesh.frag.glsl`):**
- New uniforms: `uniform sampler2D u_normalMap; uniform bool u_hasNormalMap;` on unit **1**
  (unit 0 diffuse, unit 3 CSM — both untouched; unit 1 is free in the tree path).
- Geometric normal `Ng = normalize(v_normal)`.
- If `u_hasNormalMap`: build the TBN from the geometric basis —
  `mat3 TBN = mat3(normalize(v_tangent), normalize(v_bitangent), Ng)` — sample the map with a
  leaf-only mip bias to curb high-frequency shimmer (GPU Gems 3 §4):
  `vec3 nts = texture(u_normalMap, v_texCoord, u_useAlphaTest ? 1.0 : 0.0).xyz * 2.0 - 1.0`
  (bias `+1.0` for leaf cards, `0.0` for bark), and perturb `N = normalize(TBN * nts)`. Else
  `N = Ng` (graceful fallback when the map is absent — R2).
- **No viewer-flip is introduced.** The perturbed `N` drops straight into the *existing* lighting:
  leaf cards keep the two-sided `abs(dot(N, L))` half-Lambert and bark keeps the signed one. Because
  the leaf term is `abs()`-based it is side-independent and never collapses to black regardless of
  which face of the two-sided card is drawn — so a per-pixel `N` inherits the 52f4b4d guarantee by
  construction (D2). The tree shader has no specular or other signed-normal leaf term, so nothing
  else needs the sign.
- Backlit translucency (`V`, `L` only) and the CSM shadow term are unchanged; only the `N` fed into
  the diffuse becomes per-pixel.

### 4.2 T9 — Dithered dissolve crossfade (canopy + shadow)

**T9 must precede T10.** It converts the LOD crossfade from an alpha *blend* (which reads the shader's
`fragColor.a = v_alpha` write at `tree_mesh.frag:143`) into a screen-door *dissolve*, which frees the
alpha channel so T10 can put A2C edge-coverage there. A2C and the crossfade blend cannot share the
alpha channel, so the blend has to go first — this is the real dependency direction (T10 depends on
T9), and it is why the dissolve is slice 9, not slice 10.

- **Canopy** (`tree_mesh.frag.glsl`): for cutout tiers, add a screen-door dissolve *before* the
  cutout discard: `if (v_alpha < interleavedGradientNoise(gl_FragCoord.xy)) discard;`, then keep the
  existing `texAlpha < u_alphaCutoff` discard. **Drop the `ScopedBlendState`** for cutout tiers (kept only if
  a genuine `BLEND` material survives — none in the current species set) and **write
  `fragColor.a = 1.0`** (opaque) for the surviving fragments: the dissolve, not the alpha channel, now
  carries the crossfade, replacing the pre-existing `fragColor.a = v_alpha` blend dependency. The two
  fading tiers interleave stipple-wise instead of cross-blending whole silhouettes (D4).
- **Constant coverage through the band (density-pulse fix, root cause #4):** the CPU keeps the
  *existing* complementary `1-t` / `t` alphas (`render()` bucketing, §2) — **no change to the alpha
  curves, so §5's "main-pass per-instance `t` unchanged" holds.** Under the dissolve, complementary
  alphas already give ~constant total coverage: the outgoing tier keeps a `(1-t)` fraction of its
  pixels while the incoming tier keeps `t`, summing to ~one canopy's worth across the band. No
  SpeedTree curve-offset is added — the dissolve *is* the mechanism that removes the double-density
  blend.
- **Impostor symmetry (root cause #2):** the impostor is already a cutout, so it now dissolves on the
  *same* discard mechanism as the mid mesh — the mid→far fade stops being "smooth blend vs hard cutout".
- **Shadow** — the ground shadow snaps at **both** tier boundaries today (§2 root cause #3: the caster
  hard-switches LOD0→mid at `lodDistance` and hard-culls at `billboardDistance`), so the dissolve is
  applied at both, using the same `v_alpha`-vs-noise discard:
  1. *(shader)* `tree_shadow.vert.glsl` reads `i_alpha` (binding 3 / loc 12) → `v_alpha`;
     `tree_shadow.frag.glsl` gets its **own copy** of `interleavedGradientNoise` (it has none today)
     and applies the same `if (v_alpha < noise) discard;` before its cutout discard.
  2. *(CPU)* `TreeRenderer::renderShadow` computes a per-instance fade alpha in its bucketing instead of
     hard-pushing `{model, 1.0f}` (`tree_renderer.cpp:607/611`), mirroring `render()`'s band logic:
     - **45–60 m (LOD0↔mid): a two-caster crossfade** — feed **both** LOD0 (alpha `1-t`) and mid
       (alpha `t`) through the band. (A mid-less species inherits `render()`'s handling — both buckets
       are LOD0, so the near band stays ~constant-coverage LOD0+LOD0, benign.)
     - **180–195 m (near-tier→nothing): a single-caster fade-out** — feed the active near-tier caster
       (mid for most species; LOD0 for a species whose mid tier collapsed, `sp.midPrims.empty()` at
       `:605`) alpha `1-t` so it **dissolves to nothing** by 195 m. This requires extending the caster
       cull from the current hard `dist > billboardDistance` cut (`tree_renderer.cpp:594`) out to
       `billboardDistance + fadeRange`. The impostor still does **not** cast (D4 / parent T4) — no
       billboard caster is added; the far band is a single-caster fade-out, not a two-tier crossfade
       (holds the D4 scope cap).
  3. *(CPU)* `drawShadowTier` uploads that alpha to the binding-3 VBO — today it uploads **only** the
     model-matrix VBO and the comment at `tree_renderer.cpp:505-506` states "@12 is unread"; that comment
     and gap are removed here.

  (D5, root cause #3 — now fixed at both boundaries, matching the §6 item 4 acceptance.)

### 4.3 T10 — Soft leaf-card edges

All in `tree_mesh.frag.glsl`, guarded to the `u_useAlphaTest` (leaf/impostor) path so bark is
untouched. **Lands only after T9 has removed the crossfade blend** (above), so the alpha channel is
free for A2C coverage.

- **Coverage preservation** (mip thinning fix): before the cutoff test, rescale
  `texAlpha *= 1.0 + calcMipLevel(v_texCoord * texSize) * 0.25;` with `texSize = textureSize(u_texture, 0)`
  and the standard derivative mip form
  `float calcMipLevel(vec2 uv){ vec2 dx=dFdx(uv), dy=dFdy(uv); return max(0.0, 0.5*log2(max(dot(dx,dx), dot(dy,dy)))); }`.
  Keeps canopy density stable with distance. `_MipScale = 0.25` (Ben Golus: "approximates the loss of
  density almost perfectly") — provisional constant, `TODO: revisit via Formula Workbench` if it needs
  per-species tuning.
- **Edge antialias coverage**: compute a one-pixel-wide soft coverage from the cutoff —
  `float cov = clamp((texAlpha - u_alphaCutoff) / max(fwidth(texAlpha), 1e-4) + 0.5, 0.0, 1.0);`.
- **Change T9's opaque write to the mode-selected one:** the leaf path's single `fragColor.a` write
  becomes `fragColor.a = (u_msaaActive && u_useAlphaTest) ? cov : 1.0;`. In `MSAA_4X` this drives
  `GL_SAMPLE_ALPHA_TO_COVERAGE` (genuine sub-pixel edge AA); in the other four modes the leaf stays a
  crisp opaque cutout (α = 1) whose ragged edge TAA/SMAA/FXAA resolve in post. This composes with T9's
  dissolve `discard` (which never writes `fragColor.a`): the dissolve decides *which* fragments survive
  per tier, this write sets the surviving leaf's edge coverage.
- **A2C enable/disable** is a `ScopedState` around the leaf-tier draw in `drawMeshTier`, active only
  when `msaaActive` (§2: how the CPU field reaches `TreeRenderer` from the call site; §5: the
  `u_msaaActive` uniform upload). It stays disabled for bark and for the non-MSAA modes so we never get
  a hard 50%-cutout regression there.

## 5. CPU / GPU placement (project Rule 7)

| Work | Where | Why |
|------|-------|-----|
| TBN construction, normal-map sample & perturb | **GPU** (vertex + fragment) | Per-vertex / per-pixel; mirrors `scene.*`. |
| Coverage-preservation, fwidth edge coverage, A2C | **GPU** (fragment + fixed-function) | Per-pixel; uses screen-space derivatives that only exist on GPU. |
| Dithered dissolve (canopy + shadow) | **GPU** (fragment) | Per-pixel hash vs per-instance alpha. |
| LOD bucketing, main-pass per-instance crossfade `t`, tier selection | **CPU** (`TreeRenderer::render`, already there) | Branching / per-instance / sparse — **unchanged from T1–T7**. |
| Shadow-pass per-instance crossfade alpha + alpha-VBO upload | **CPU** (`renderShadow` bucketing + `drawShadowTier`) | **New in T9** — the shadow pass hard-switches tiers today (§4.2); the dissolve needs real per-instance alpha staged to binding 3. |
| `msaaActive` flag (from renderer AA mode) set before `render()` + uploaded to `u_msaaActive` | **CPU** (`engine.cpp` tree call site) | **New in T10** — a one-line read of `m_renderer->getAntiAliasMode()`; gates the A2C `ScopedState`. |

No new CPU/GPU dual implementation and therefore no new parity test — every change is a GPU-side
shading refinement over data the CPU stages (the two new CPU rows only *stage* data; they compute no
shading maths that a GPU path must match).

## 6. Testing (project TESTING.md + Rule 4 audit)

Rendering-shader changes have no pure-function CPU spec to unit-test; coverage is
build + visual-regression + perf, consistent with how T1–T7 were verified. Items 3–6 are **manual
dev-machine visual gates** (subjective capture on the RX 6600) and item 7 is the **objective
dev-machine perf gate** — none are automated CI gates, the same class the parent doc uses for its
perf/visual acceptance.

1. **Build + existing-suite gate** *(CI)* — `local-ci.sh` (full, no `--quick`): builds clean and all
   existing ctest green. The **meadow GL-error walk** (0 GL errors at LOD0/mid/impostor distances) is a
   *dev-machine* step, not CI: the `gameready/` LOLIPOP assets are git-ignored (CI has nothing to load)
   and GL tests `GTEST_SKIP()` headless — so the new normal-mapped path is only exercised locally, same
   as the parent's T1–T7 visual/GL acceptance.
2. **Normal-map presence check** *(dev-local, SKIP on CI)* — the harness has no GL-mock and
   `drawMeshTier` is private with no injectable seam, so a "verify the bind call" unit test is not
   feasible; GL tests derive from `GLTestFixture`, which opens a real context and `GTEST_SKIP()`s
   headless. Instead: a `GLTestFixture`-based render test that draws one normal-mapped species under
   a raking light and asserts the leaf-card region shows shading **variance** above a threshold
   (`glReadPixels`), versus a near-flat result with `u_hasNormalMap=false`. Guards D1 on the dev
   machine; on CI it skips like every other real-context test.
3. **Two-sided-leaf guard** *(dev-gate)* — confirm a backlit leaf underside is still ≥ ambient
   (never black) after normal mapping — the 52f4b4d invariant. Before/after screenshot at the known
   backlit spot, same method used for the canopy fix.
4. **Crossfade dissolve** *(dev-gate)* — walk the camera through both bands (45–60 m, 180–195 m);
   acceptance (subjective, screenshot pair per band): no whole-silhouette pop and no density/brightness
   pulse in either band. In the **45–60 m band only** the ground shadow dissolves in step with the
   canopy (not a snap); at the **180–195 m band** the impostor doesn't cast (D4), so the acceptance
   there is the mid caster's shadow fading smoothly to nothing, not a two-caster crossfade.
5. **Per-AA-mode smoke** *(dev-gate)* — verify leaf edges in each of `NONE / MSAA_4X / TAA / SMAA /
   FXAA`: A2C engaged only in `MSAA_4X`, and no hard 50%-cutout regression in the other four.
6. **Coverage stability** *(dev-gate)* — walk the camera from near to far past a stand; acceptance
   (screenshot pair, coverage-preservation on vs off): the canopy holds its leaf density into the
   distance and doesn't thin/sparkle (the T10 mip-thinning fix, goal 2). Distinct from item 4's
   crossfade-band pulse — this is steady-state distance thinning, a different mechanism.
7. **Perf gate** *(dev-machine, objective)* — `--profile-log` before/after **each** slice (cross-cutting,
   like item 1): 60 FPS locked and the combined Tree + tree-shadow GPU pass ≤ 2.0 ms on the RX 6600
   (§7 budget). This is the enumerated home of the perf criterion the rollback bar keys on.

**Rollback bar (per slice).** Each slice ships behind its gate; if the gate shows a regression, revert
*that slice's* change and reassess before the next slice — do not stack a fix on a regressed base.
Concretely: **T8** reverts if item 3 shows any backlit underside darker than ambient; **T9** reverts to
the current blend if the dissolve reads "sparkly" and widening `fadeRange` (R4) doesn't resolve it;
**T10** reverts (or gates A2C behind the High/Ultra tree tier, §7) if the item-7 perf gate drops below
60 FPS / the Tree pass exceeds 2.0 ms, or item 5 shows a hard-cutout regression in a non-MSAA mode —
and drops just the coverage rescale if item 6 shows the canopy thinning with distance.

## 7. Performance plan (60 FPS hard floor)

- Baseline: parent T4 reported Tree GPU pass **0.12–0.22 ms** on RX 6600 / Mesa. Budget cap for this
  pass: **≤ 2.0 ms** Tree pass, 60 FPS locked (same gate parent T7 will formalise).
- T8 adds one texture fetch + a TBN per leaf/bark fragment — cheap; the canopy is already
  texture-bound. T9's dissolve replaces a blend with a `discard` — dropping the leaf-tier blend can
  *reduce* overdraw cost during the band (no dst read). T10 adds a `fwidth` + coverage rescale + a
  couple of ALU ops per canopy pixel.
- **Shadow-pass delta (T9):** two small additions — (a) both LOD0 + mid casters drawn through the
  45–60 m band doubles the tree depth-only draws *for trees in that band only* (today it hard-switches
  to one tier); (b) the far fade-out extends the mid-caster cull from `billboardDistance` to
  `billboardDistance + fadeRange`, adding the mid casters in the thin 180–195 m ring that are culled
  today. The caster fragments are cheap RSM-flux writes (position + wind + cutout + a single N·L flux
  write, `tree_shadow.frag:43-46` — no CSM sampling or light loop), but both deltas are real and
  currently unbudgeted — include the shadow pass in the `--profile-log` before/after and keep the
  combined Tree + tree-shadow cost inside the frame budget.
- Risk: the mip-coarser normal fetch (T8) and coverage rescale (T10) both touch every canopy pixel;
  measure with `--profile-log` before/after and hold the ≤ 2.0 ms cap. If A2C in `MSAA_4X` costs
  more than budget on dense stands, gate it behind the High/Ultra tree tier (parent T7).

## 8. Accessibility

- Reduce-motion: the dissolve dither is *static per frame* (screen-space hash on `gl_FragCoord`,
  not time-animated), so it introduces no new motion. The wind sway is the only tree motion and is
  already reduce-motion-gated upstream. No new photosensitivity surface.
- No colour-only information is added; normal mapping affects shading intensity, not hue.

## 9. Implementation slices

- **T8 — Normal-mapped leaf cards & bark.** Bind `getNormalMap()`→unit 1 in `drawMeshTier` (shadow
  tier stays diffuse-only); add tangent/bitangent + TBN to `tree_mesh.vert`; sample + perturb in
  `tree_mesh.frag` (no flip, D2); leaf-only mip-bias the fetch. Verify: §6 items 1, 2, 3, 7.
- **T9 — Dithered dissolve crossfade.** Screen-door dissolve `discard` in `tree_mesh.frag`; drop the
  cutout-tier blend and write opaque `fragColor.a`; plumb `i_alpha` + a copied
  `interleavedGradientNoise` + matching dissolve into `tree_shadow.*`; compute shadow-pass crossfade
  alpha in `renderShadow` + upload it in `drawShadowTier`; feed both casters through the LOD0↔mid band
  only (mid fades to nothing at the far boundary via extending the caster cull to
  `billboardDistance + fadeRange` — no billboard caster). Verify: §6 items 1, 4, 7.
- **T10 — Soft leaf-card edges.** Coverage-preservation + fwidth coverage in `tree_mesh.frag`;
  change T9's opaque `fragColor.a` write to the mode-selected `cov`/`1.0`; A2C `ScopedState` around
  the leaf draw gated on the new `msaaActive` flag. Verify: §6 items 1, 5, 6, 7.

Order is strict: T8 (biggest payoff, self-contained) → T9 (dissolve — removes the crossfade blend,
freeing the alpha channel) → T10 (soft edges — claims that alpha channel for A2C, so it **depends on
T9**). Each slice ships behind a full `local-ci.sh` + a dev-gate visual capture before the next starts.

## 10. Risks & scope caps

- **R1 (precondition — verified):** T8 needs tangent (loc 4) / bitangent (loc 5) *enabled* on the VAO
  the tree tiers draw with. **Confirmed enabled:** `Mesh::upload` formats loc 4/5 unconditionally
  (`mesh.cpp:199-207`) and `bindInstanceBuffers` only touches loc 6-9/12, so the layered instance
  buffers don't clobber them. The loaded gameready tree meshes carry **no artist `TANGENT`**, so the
  loader synthesizes tangents from position + UV via `calculateTangents` (`gltf_loader.cpp:921-923`,
  taken because the meshes are indexed with no `TANGENT`); loc 4/5 are filled either way. No C++ VAO
  change is required. *Quality caveat:* leaf-card TBN quality therefore rests on synthesized (not
  artist) tangents — fine for the UV-mapped atlas cards, but if a species' leaf normals look wrong,
  suspect degenerate synthesized tangents before the shader (note per-species in the T8 capture).
- **R2:** Some LOLIPOP leaf-cluster materials may reference a normal map that is near-flat (a mostly
  (128,128,255) texture). If a species looks unchanged, that's expected — fall back is identity `N`;
  it is not a bug. Note per-species in the T8 capture.
- **R3:** A2C only helps in `MSAA_4X`. Non-MSAA modes get coverage-stability + sharpen but hard
  (aliased-until-post-AA) edges — acceptable and documented (D3), not a defect.
- **R4:** If the dithered dissolve reads as "sparkly" at the default `fadeRange = 15 m`, widen the
  band (D6) rather than switching back to blend. Capture before/after; don't tune blind.
- **Scope caps:** no PBR metallic/roughness (D7); no LOD-threshold changes unless D6 triggers; no
  change to species table, placement, or asset pipeline; birch stays dropped. The far-fade shadow-caster
  cull extension to `billboardDistance + fadeRange` (§4.2) is a mechanism byproduct derived from existing
  constants — **not** a retune of `lodDistance`/`billboardDistance`/`maxDistance`.

## 11. Cited sources

- **NVIDIA GPU Gems 3, Ch. 4 — "Next-Generation SpeedTree Rendering."** Per-pixel leaf lighting via
  tangent-space normal maps; two-sided leaf shading with backlit colour shift; coarser/mip-biased
  leaf normals to kill specular shimmer; alpha-to-coverage LOD crossfade with offset alpha curves
  (the offset-curve variant is *described by the source*; this design instead keeps complementary
  `1-t`/`t` alphas under a dissolve — see §4.2, D4).
  https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch04.html
- **Ben Golus — "Anti-aliased Alpha Test: The Esoteric Alpha To Coverage."** fwidth edge-sharpen
  `(a-cutoff)/max(fwidth(a),1e-4)+0.5`; mip coverage-preservation `a *= 1 + mipLevel*0.25`
  (`_MipScale ≈ 0.25`); A2C requires MSAA.
  https://bgolus.medium.com/anti-aliased-alpha-test-the-esoteric-alpha-to-coverage-8b177335ae4f
- **DigitalRune — "Screen-Door Transparency."** Dithered/screen-door dissolve for LOD fade and
  fade-out, avoiding the sort/double-blend problems of true alpha blending.
  https://digitalrune.github.io/DigitalRune-Documentation/html/fa431d48-b457-4c70-a590-d44b0840ab1e.htm
- **In-repo reference patterns:** `assets/shaders/scene.vert.glsl` / `scene.frag.glsl` (TBN +
  normal-map sampling to reuse); `tree_mesh.frag.glsl` (existing two-sided leaf lighting +
  `interleavedGradientNoise` to reuse for the dissolve).

## 12. Cold-eyes loop log

_(each loop dispatched cold, no prior-loop briefing — global rule 14 / project rule 9)_

- **Loop 1 (2 lanes: doc-vs-code accuracy + consistency/implementability).**
  Tally: CRITICAL 0 · HIGH 3 · MEDIUM 7 · LOW 3 · INFO 0 (verified 12 / unverified 1).
  All core gating claims confirmed true against source (tangent/bitangent enabled in the tree VAO,
  normal maps already parsed into `Material`, unit 1 free, MSAA-only-in-`MSAA_4X`). Fixed:
  FXAA is a shipping AA mode not "planned" (§2, §6 item 5); the T8 viewer-flip was over-engineered —
  the existing `abs()` leaf diffuse is already two-sided, so the sampled normal drops in with no flip
  (D2, §4.1); T9/T10 shared-`fragColor.a` collision resolved to a single mode-selected write + a
  discard-only dissolve (§4.2/§4.3); redundant SpeedTree "curve offset" dropped — complementary
  `1-t`/`t` under the dissolve already gives constant coverage (dissolve section, removes the §5
  collision); the
  shadow dissolve now spells out its three additions (two CPU — `renderShadow` alpha + `drawShadowTier`
  VBO upload; one shader — `tree_shadow.*` noise+read) and is scoped to the LOD0↔mid band only,
  upholding the D4 no-billboard-caster cap; `msaaActive` plumbing named (§2, §5); normal-map unit test
  reframed to harness reality — GL tests SKIP headless, no mock (§6 item 2); per-slice rollback bar
  added (§6); §11-vs-§10 cross-ref, `§6.N` notation, shadow-cost budget, divider consistency corrected.
  Dismissed (unverified): "no `_lod0` files exist" — they do, under the `gameready/` symlink the engine
  loads from; the reviewer searched only the source pack.
- **Loop 2 (2 lanes, cold re-read of the edited doc).**
  Tally: CRITICAL 0 · HIGH 2 · MEDIUM 1 · LOW 5 · INFO 3 (verified 11 / unverified 0). All cited
  `file:line` re-verified exact. Two substantive fixes: (1) **slice-ordering regression** — the old T9
  (soft edges) changed the `fragColor.a` write and enabled A2C while the crossfade blend (removed only
  in the old T10) still read that channel, so T9-in-isolation broke the crossfade and put A2C + blend
  on one channel; slices reordered so **T9 = dissolve** (removes the blend, frees the channel) precedes
  **T10 = soft edges** (§4.2/§4.3, §5, §6, §7, §9). (2) **tangent provenance** — the loaded gameready
  meshes carry *no artist* `TANGENT`; loc 4/5 are filled by the loader's `calculateTangents` synthesis
  (`gltf_loader.cpp:921-923`), not the cited read-path — corrected in §4.1/§10 R1 with a TBN-quality
  caveat. Polish: §6 item 1 split into a true-CI part vs the git-ignored-asset dev walk; item 4 far-band
  acceptance clarified (single caster); normal-fetch shown once with an explicit leaf-only mip bias;
  `calcMipLevel`/`texSize` expressions given; `u_msaaActive` upload step named; §11 offset-curve line
  marked described-not-adopted; `taa.h` line range 19-26→19-27; loop-1 log "CPU-side" mislabel fixed.
- **Loop 3 (2 lanes, cold re-read of the reordered doc).**
  Tally: CRITICAL 0 · HIGH 1 · MEDIUM 1 · LOW 2 · INFO 3 (verified 7 / unverified 0). The doc-vs-code
  lane returned **SOUND** — every cited `file:line` re-verified exact, no substantive defect. The
  implementability lane caught the reorder's two residues: (1) §2 still attributed the `msaaActive`
  plumbing to T9 (the one section the loop-2 reorder missed) — every other section says T10; fixed. (2)
  the shadow dissolve was scoped to the LOD0↔mid band only, but §6 item 4 (and §2 root cause #3) require
  the **far** boundary fixed too — the mid caster hard-culls at `billboardDistance` today; §4.2 now
  applies the dissolve at both boundaries (two-caster crossfade near, single-caster fade-out far with the
  cull extended to `billboardDistance + fadeRange`), §7 budgets the extra ring. Polish: D3 dependency
  note, §1 goal→slice map, `texAlpha < u_alphaCutoff` naming, §4.2 write/read phrasing.
- **Loop 4 (2 lanes, cold re-read).**
  Tally: CRITICAL 0 · HIGH 0 · MEDIUM 2 · LOW 3 · INFO 2 (verified 7 / unverified 0). No HIGH — the
  two-boundary shadow fix, msaaActive→T10 attribution, and every cited `file:line` all re-verified
  clean. Two substantive fixes: (1) **§4.1 Gram-Schmidt misattribution** — `scene.vert:223-226`
  independently `safeNormalize`s T/B/N (no Gram-Schmidt), so "as scene.vert does" was false; §4.1 now
  matches the real safeNormalize-with-fallback pattern. (2) **missing coverage-preservation gate** —
  T10 bundles coverage-preservation (distance density, goal 2) with A2C, but only A2C was gated; added
  §6 item 6 (coverage-stability dev-gate) + rollback trigger + §9 T10 verify map. Polish: far-band
  fade generalized for mid-less species (`sp.midPrims.empty()` → LOD0 caster); §2 root-cause-#3 cite
  split (`:605` switch / `:594` cull); §4.3 `msaaActive` pointer split §2-field/§5-upload; §9 notes the
  cull extension. INFO left: "no artist TANGENT" is verifiable only on local git-ignored assets
  (conclusion holds either way; TBN-quality caveat already present).
- **Loop 5 (2 lanes, cold re-read).**
  Tally: CRITICAL 0 · HIGH 0 · MEDIUM 1 · LOW 2 · INFO 3 (verified 6 / unverified 0). Both lanes
  returned essentially sound ("substantially sound" / "implementable as-is") — every cited `file:line`
  re-verified exact, no new code-claim errors. Fixed: (1) the T10 rollback cited an "item-1 perf gate"
  but item 1 (CI build) holds no FPS/ms criterion — added §6 **item 7** (objective dev-machine perf
  gate, `--profile-log` 60 FPS / ≤2.0 ms) as the enumerated home and re-pointed the rollback; item 7
  added to all §9 verify maps. (2) §4.1 told the implementer to call `safeNormalize` in `tree_mesh.vert`
  without noting it must be **copied in** (no shared GLSL `#include`; helper duplicated per shader) —
  added, matching the `interleavedGradientNoise` copy note. (3) §7 "no shading" corrected — the caster is
  an RSM-flux pass doing N·L (`tree_shadow.frag:43-46`). Polish: intro range 3–5→3–6+item 7; near-band
  no-mid-species clause (LOD0+LOD0, benign); scope-cap disclaimer that the cull extension isn't a
  threshold retune.
- **Loop 6 (single combined convergence lane, cold).** Tally: CRITICAL 0 · HIGH 0 · MEDIUM 0 · LOW 0 ·
  INFO 0 — **CONVERGED.** Zero substantive defects and no polish nits; every spot-checked `file:line`
  exact (including the two most error-prone: `tree_shadow.frag` does N·L RSM flux not depth-only, and
  `scene.vert:223-226` uses independent normalize not Gram-Schmidt), all cross-references resolve, every
  slice gated. Decay across loops: HIGH 3 → 2 → 1 → 0 → 0 → 0.

**Signed off** (cold-eyes converged, 2026-07-23) — per the delegated gate (cold-eyes convergence, then
implement). Build order T8 → T9 → T10, each behind a full `local-ci.sh` + dev-gate capture.
