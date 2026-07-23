# Phase 10 — Meadow Realism C: Close-Up Tree Quality (3D_E-0033, slices T8–T10)

**Status:** Draft — awaiting cold-eyes convergence.
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

No new assets, no new subsystem. All three are edits to the existing tree draw path and
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
  3. **Shadow pass has no crossfade at all** — casters hard-switch tier at exactly
     `lodDistance`/`billboardDistance` (`tree_renderer.cpp` ~605-612), so the ground shadow
     silhouette snaps mid-fade.
  4. **Double-draw density pulse** — both tiers with two-sided leaves + blend briefly double
     canopy overdraw, reading as a brightness/thickness pulse.

**AA mode reality (decides the edge-softening path).**
- Vestige ships `AntiAliasMode { NONE, MSAA_4X, TAA, SMAA, FXAA }` — all five are live
  (`taa.h:19-26`; FXAA is a shipping post-process mode, not planned). **A multisample scene FBO
  is allocated only for `MSAA_4X`** (`renderer.cpp:2255`: `wantSamples = (mode==MSAA_4X)?4:1`);
  `NONE`/`TAA`/`SMAA`/`FXAA` render the scene to a *1-sample* FBO. So `GL_SAMPLE_ALPHA_TO_COVERAGE`
  only produces true edge AA in `MSAA_4X` — the design must not depend on it in the other four modes.
- **The AA mode is not on `TreeRenderer` today.** It lives on the renderer
  (`renderer.h:292 getAntiAliasMode()`); the tree pass is invoked from `engine.cpp:1653` where
  `m_renderer` is in scope. T9 plumbs it in as a public `bool msaaActive` field on `TreeRenderer`,
  set at that call site (`m_renderer->getAntiAliasMode()==MSAA_4X`) exactly like the existing
  public `windDirection`/`windAmplitude` fields set at `engine.cpp:1651-1652`.

## 3. Design decisions & rationale

| # | Decision | Why |
|---|----------|-----|
| D1 | **Bind the already-loaded normal map to a new unit and add a TBN to `tree_mesh.*`.** Reuse the `scene.*` TBN + normal-sample pattern; don't invent one. | Normals are on disk and in `Material` for free (§2). This is the single highest-payoff close-up change. Global rule 3 (reuse). |
| D2 | **The sampled normal replaces the geometric `N` inside the *existing* leaf/bark lighting — no viewer-flip is introduced.** | Leaf diffuse is already two-sided `abs(dot(N,L))` (never black on a backlit underside); a per-pixel `N` inherits that guarantee, so the 52f4b4d fix can't regress and no extra flip logic is needed. Simplest correct (rule 2). |
| D3 | **Soft edges = coverage-preservation (all modes) + fwidth sharpen (all modes) + `GL_SAMPLE_ALPHA_TO_COVERAGE` only when `MSAA_4X`.** | Coverage/sharpen are per-pixel math that work everywhere and cost ~nothing; genuine edge AA needs multisample, which only `MSAA_4X` has (§2). TAA/SMAA/FXAA soften the sharpened edge in their own modes. Ben Golus. |
| D4 | **Crossfade becomes a *dithered dissolve* for cutout tiers, driven by the existing `interleavedGradientNoise`.** Discard leaf fragments where `v_alpha < noise(gl_FragCoord)`. | Fixes root cause #1: a fading tier truly thins pixel-by-pixel instead of cross-blending whole silhouettes. The noise function already exists in the frag shader (shadow PCF). |
| D5 | **Apply the same dissolve threshold in `tree_shadow.frag` and give the shadow pass the crossfade alpha.** | Fixes root cause #3 — the ground shadow dissolves in lockstep with the canopy instead of snapping. |
| D6 | **Keep the LOD thresholds and `fadeRange` as-is for now; widen only if the dissolve alone doesn't settle it.** Do not retune blindly. | Global rule 2/11 (shortest correct, stay in lane). The dither dissolve is the mechanism fix; band-width is a tuning knob, changed only with a before/after in hand. |
| D7 | **No PBR metallic/roughness this pass.** Normal + existing half-Lambert/translucency only. | Scope cap. Full PBR for trees is a separate, larger change; the flat-cardboard complaint is a *normal* problem, not a BRDF problem. Rule 2. |

## 4. Architecture

### 4.1 T8 — Normal-mapped leaf cards & bark

**Vertex (`tree_mesh.vert.glsl`):**
- Declare `layout(location = 4) in vec3 a_tangent;` and `layout(location = 5) in vec3 a_bitangent;`.
  These are already enabled and populated in the mesh VAO (`mesh.cpp:199-207` formats loc 4/5
  unconditionally; the tree glTFs carry real `TANGENT` accessors, `gltf_loader.cpp:569-593`), and
  `bindInstanceBuffers` only touches loc 6-9/12 — so this is a **shader-declaration-only** change,
  no C++ VAO work (confirmed precondition, §10 R1).
- Build the world-space tangent/bitangent with the same `nm = mat3(i_model)*mat3(u_nodeMatrix)`
  used for the normal, Gram-Schmidt-orthonormalising `a_tangent` against `a_normal` as `scene.vert`
  does; emit `v_tangent`, `v_bitangent` alongside `v_normal`.

**Fragment (`tree_mesh.frag.glsl`):**
- New uniforms: `uniform sampler2D u_normalMap; uniform bool u_hasNormalMap;` on unit **1**
  (unit 0 diffuse, unit 3 CSM — both untouched; unit 1 is free in the tree path).
- Geometric normal `Ng = normalize(v_normal)`.
- If `u_hasNormalMap`: build the TBN from the geometric basis —
  `mat3 TBN = mat3(normalize(v_tangent), normalize(v_bitangent), Ng)` — sample
  `vec3 nts = texture(u_normalMap, v_texCoord).xyz * 2.0 - 1.0`, and perturb
  `N = normalize(TBN * nts)`. Else `N = Ng` (graceful fallback when the map is absent — R2).
- **No viewer-flip is introduced.** The perturbed `N` drops straight into the *existing* lighting:
  leaf cards keep the two-sided `abs(dot(N, L))` half-Lambert and bark keeps the signed one. Because
  the leaf term is `abs()`-based it is side-independent and never collapses to black regardless of
  which face of the two-sided card is drawn — so a per-pixel `N` inherits the 52f4b4d guarantee by
  construction (D2). The tree shader has no specular or other signed-normal leaf term, so nothing
  else needs the sign.
- Backlit translucency (`V`, `L` only) and the CSM shadow term are unchanged; only the `N` fed into
  the diffuse becomes per-pixel.
- Reduce leaf normal shimmer by biasing the normal-map fetch one mip coarser
  (`texture(u_normalMap, v_texCoord, +1.0)`), per GPU Gems 3 §4 — leaf cards are high-frequency and
  alias otherwise.

### 4.2 T9 — Soft leaf-card edges

All in `tree_mesh.frag.glsl`, guarded to the `u_useAlphaTest` (leaf/impostor) path so bark is
untouched:

- **Coverage preservation** (mip thinning fix): before the cutoff test, rescale
  `texAlpha *= 1.0 + calcMipLevel(v_texCoord * texSize) * 0.25;` where `calcMipLevel` is the
  standard derivative form. Keeps canopy density stable with distance. `_MipScale = 0.25`
  (Ben Golus: "approximates the loss of density almost perfectly") — provisional constant,
  `TODO: revisit via Formula Workbench` if it needs per-species tuning.
- **Edge antialias / sharpen**: compute a one-pixel-wide soft coverage from the cutoff —
  `float cov = clamp((texAlpha - u_alphaCutoff) / max(fwidth(texAlpha), 1e-4) + 0.5, 0.0, 1.0);`.
- **Single alpha-write, mode-selected (resolves the T9/T10 shared-line question):** the leaf path
  writes `fragColor.a` in exactly one place, at the end of `main` —
  `fragColor.a = (u_msaaActive && u_useAlphaTest) ? cov : 1.0;`. In `MSAA_4X` this drives
  `GL_SAMPLE_ALPHA_TO_COVERAGE` (genuine sub-pixel edge AA); in the other four modes the leaf is a
  crisp opaque cutout (α = 1) whose ragged edge TAA/SMAA/FXAA resolve in post. **T10's dissolve is a
  `discard`, not an alpha write** (§4.3), so the two features compose without clobbering each other:
  the dissolve decides *which* fragments survive per tier, and this single write sets the surviving
  leaf's edge coverage. There is no second `fragColor.a = 1.0` statement anywhere in the leaf path.
- **A2C enable/disable** is a `ScopedState` around the leaf-tier draw in `drawMeshTier`, active only
  when `msaaActive` (the `MSAA_4X` case; §2 covers how the flag reaches `TreeRenderer`). It stays
  disabled for bark and for the non-MSAA modes so we never get a hard 50%-cutout regression there.

### 4.3 T10 — Dithered dissolve crossfade (canopy + shadow)

- **Canopy** (`tree_mesh.frag.glsl`): for cutout tiers, add a screen-door dissolve *before* the
  cutout discard: `if (v_alpha < interleavedGradientNoise(gl_FragCoord.xy)) discard;`, then keep the
  existing `texAlpha < cutoff` discard. The dissolve is a **discard only** — it does not write
  `fragColor.a` (that single write lives in §4.2). Surviving fragments are opaque (or A2C-covered in
  `MSAA_4X`), so the two fading tiers interleave stipple-wise instead of cross-blending whole
  silhouettes (D4). The `ScopedBlendState` is then dropped for cutout tiers (kept only if any genuine
  `BLEND` material survives — none in the current species set).
- **Constant coverage through the band (density-pulse fix, root cause #4):** the CPU keeps the
  *existing* complementary `1-t` / `t` alphas (`render()` bucketing, §2) — **no change to the alpha
  curves, so §5's "per-instance `t` unchanged" holds.** Under the screen-door dissolve, complementary
  alphas already give ~constant total coverage: the outgoing tier keeps a `(1-t)` fraction of its
  pixels while the incoming tier keeps `t`, summing to ~one canopy's worth across the band. No
  SpeedTree curve-offset is added — the dissolve *is* the mechanism that removes the double-density
  blend.
- **Impostor symmetry (root cause #2):** the impostor is already a cutout, so it now dissolves on the
  *same* discard mechanism as the mid mesh — the mid→far fade stops being "smooth blend vs hard cutout".
- **Shadow** — the dissolve fix applies to the **LOD0↔mid band only**, the sole band with two casters:
  1. `tree_shadow.vert.glsl` reads `i_alpha` (binding 3 / loc 12) → `v_alpha`; `tree_shadow.frag.glsl`
     gets its **own copy** of `interleavedGradientNoise` (it has none today) and applies the same
     `if (v_alpha < noise) discard;` before its cutout discard.
  2. `TreeRenderer::renderShadow` must compute per-instance crossfade alpha in its bucketing instead
     of hard-pushing `{model, 1.0f}` (`tree_renderer.cpp:607/611`), mirroring `render()`'s band logic,
     and feed **both** LOD0 and mid casters through the 45–60 m band.
  3. `drawShadowTier` must upload that alpha to the binding-3 VBO — today it uploads **only** the
     model-matrix VBO and the comment at `tree_renderer.cpp:505-506` states "@12 is unread"; that
     comment and gap are removed as part of T10.

  At the **mid→far boundary the impostor does not cast** (D4 / parent T4 culls casters past
  `billboardDistance`), so there is no second caster there — the correct behaviour is the mid caster
  **dissolving to nothing** as its `t`→0, *not* a two-tier crossfade and **not** a new billboard caster
  (holds the D4 scope cap). (D5, root cause #3.)

## 5. CPU / GPU placement (project Rule 7)

| Work | Where | Why |
|------|-------|-----|
| TBN construction, normal-map sample & perturb | **GPU** (vertex + fragment) | Per-vertex / per-pixel; mirrors `scene.*`. |
| Coverage-preservation, fwidth edge coverage, A2C | **GPU** (fragment + fixed-function) | Per-pixel; uses screen-space derivatives that only exist on GPU. |
| Dithered dissolve (canopy + shadow) | **GPU** (fragment) | Per-pixel hash vs per-instance alpha. |
| LOD bucketing, main-pass per-instance crossfade `t`, tier selection | **CPU** (`TreeRenderer::render`, already there) | Branching / per-instance / sparse — **unchanged from T1–T7**. |
| `msaaActive` flag (from renderer AA mode) set before `render()` | **CPU** (`engine.cpp` tree call site) | **New in T9** — a one-line read of `m_renderer->getAntiAliasMode()`; gates the A2C `ScopedState`. |
| Shadow-pass per-instance crossfade alpha + alpha-VBO upload | **CPU** (`renderShadow` bucketing + `drawShadowTier`) | **New in T10** — the shadow pass hard-switches tiers today (§4.3); the dissolve needs real per-instance alpha staged to binding 3. |

No new CPU/GPU dual implementation and therefore no new parity test — every change is a GPU-side
shading refinement over data the CPU stages (the two new CPU rows only *stage* data; they compute no
shading maths that a GPU path must match).

## 6. Testing (project TESTING.md + Rule 4 audit)

Rendering-shader changes have no pure-function CPU spec to unit-test; coverage is
build + visual-regression + perf, consistent with how T1–T7 were verified. Items 3–5 are **manual
dev-machine gates** (subjective visual capture on the RX 6600), not automated CI gates — the same
class the parent doc uses for its perf/visual acceptance.

1. **Build/GL-error gate** *(CI)* — `local-ci.sh` (full, no `--quick`): all existing ctest green,
   0 GL errors on the meadow scene at LOD0/mid/impostor distances (the T1–T7 acceptance).
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
   acceptance (subjective, screenshot pair per band): no whole-silhouette pop, no density/brightness
   pulse, and the ground shadow dissolves in step (not a snap).
5. **Per-AA-mode smoke** *(dev-gate)* — verify leaf edges in each of `NONE / MSAA_4X / TAA / SMAA /
   FXAA`: A2C engaged only in `MSAA_4X`, and no hard 50%-cutout regression in the other four.

**Rollback bar (per slice).** Each slice ships behind its gate; if the gate shows a regression, revert
*that slice's* change and reassess before the next slice — do not stack a fix on a regressed base.
Concretely: **T8** reverts if item 3 shows any backlit underside darker than ambient; **T9** reverts (or
gates A2C behind the High/Ultra tree tier, §7) if item 1's perf gate drops below 60 FPS / the Tree pass
exceeds 2.0 ms, or item 5 shows a hard-cutout regression in a non-MSAA mode; **T10** reverts to the
current blend if the dissolve reads "sparkly" and widening `fadeRange` (R4) doesn't resolve it.

## 7. Performance plan (60 FPS hard floor)

- Baseline: T4 reported Tree GPU pass **0.12–0.22 ms** on RX 6600 / Mesa. Budget cap for this pass:
  **≤ 2.0 ms** Tree pass, 60 FPS locked (same gate T7 will formalise).
- T8 adds one texture fetch + a TBN per leaf/bark fragment — cheap; the canopy is already
  texture-bound. T9 adds a `fwidth` + a couple of ALU ops. T10's dissolve replaces a blend with a
  `discard` — dropping the leaf-tier blend can *reduce* overdraw cost during the band (no dst read).
- **Shadow-pass delta (T10):** feeding both LOD0 + mid casters through the 45–60 m band doubles the
  tree depth-only draws *for trees in that band only* (today the pass hard-switches to one tier).
  Depth-only casters are cheap (position + wind + cutout, no shading), but the doubling is real and
  currently unbudgeted — include the shadow pass in the `--profile-log` before/after and keep the
  combined Tree + tree-shadow cost inside the frame budget.
- Risk: the mip-coarser normal fetch (T8) and coverage rescale (T9) both touch every canopy pixel;
  measure with `--profile-log` before/after and hold the ≤ 2.0 ms cap. If A2C in `MSAA_4X` costs
  more than budget on dense stands, gate it behind the High/Ultra tree tier (T7).

## 8. Accessibility

- Reduce-motion: the dissolve dither is *static per frame* (screen-space hash on `gl_FragCoord`,
  not time-animated), so it introduces no new motion. The wind sway is the only tree motion and is
  already reduce-motion-gated upstream. No new photosensitivity surface.
- No colour-only information is added; normal mapping affects shading intensity, not hue.

## 9. Implementation slices

- **T8 — Normal-mapped leaf cards & bark.** Bind `getNormalMap()`→unit 1 in `drawMeshTier` (shadow
  tier stays diffuse-only); add tangent/bitangent + TBN to `tree_mesh.vert`; sample + perturb in
  `tree_mesh.frag` (no flip, D2); mip-bias the leaf fetch. Verify: §6 items 1, 2, 3.
- **T9 — Soft leaf-card edges.** Coverage-preservation + fwidth coverage in `tree_mesh.frag`, single
  mode-selected `fragColor.a` write; A2C `ScopedState` around the leaf draw gated on the new
  `msaaActive` flag. Verify: §6 items 1, 5.
- **T10 — Dithered dissolve crossfade.** Screen-door dissolve `discard` in `tree_mesh.frag`; plumb
  `i_alpha` + a copied `interleavedGradientNoise` + matching dissolve into `tree_shadow.*`; compute
  shadow-pass crossfade alpha in `renderShadow` + upload it in `drawShadowTier`; feed both casters
  through the LOD0↔mid band only (mid fades to nothing at the far boundary — no billboard caster).
  Verify: §6 items 1, 4.

Order is strict: T8 (biggest payoff, self-contained) → T9 (depends on nothing) → T10 (touches the
crossfade + shadow paths T8/T9 leave alone). Each slice ships behind a full `local-ci.sh` + a dev-gate
visual capture before the next starts.

## 10. Risks & scope caps

- **R1 (precondition — verified):** T8 needs tangent (loc 4) / bitangent (loc 5) *enabled* on the VAO
  the tree tiers draw with. **Confirmed enabled:** `Mesh::upload` formats loc 4/5 unconditionally
  (`mesh.cpp:199-207`) and `bindInstanceBuffers` only touches loc 6-9/12, so the layered instance
  buffers don't clobber them; the tree glTFs carry real `TANGENT` accessors (`gltf_loader.cpp:569-593`).
  No C++ VAO change is required — the risk is retired to a checked precondition.
- **R2:** Some LOLIPOP leaf-cluster materials may reference a normal map that is near-flat (a mostly
  (128,128,255) texture). If a species looks unchanged, that's expected — fall back is identity `N`;
  it is not a bug. Note per-species in the T8 capture.
- **R3:** A2C only helps in `MSAA_4X`. Non-MSAA modes get coverage-stability + sharpen but hard
  (aliased-until-post-AA) edges — acceptable and documented (D3), not a defect.
- **R4:** If the dithered dissolve reads as "sparkly" at the default `fadeRange = 15 m`, widen the
  band (D6) rather than switching back to blend. Capture before/after; don't tune blind.
- **Scope caps:** no PBR metallic/roughness (D7); no LOD-threshold changes unless D6 triggers; no
  change to species table, placement, or asset pipeline; birch stays dropped.

## 11. Cited sources

- **NVIDIA GPU Gems 3, Ch. 4 — "Next-Generation SpeedTree Rendering."** Per-pixel leaf lighting via
  tangent-space normal maps; two-sided leaf shading with backlit colour shift; coarser/mip-biased
  leaf normals to kill specular shimmer; alpha-to-coverage LOD crossfade with offset alpha curves.
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
  `1-t`/`t` under the dissolve already gives constant coverage (§4.3, removes the §5 collision); the
  shadow dissolve now spells out the three CPU-side additions (`renderShadow` alpha, `drawShadowTier`
  VBO upload, `tree_shadow.*` noise+read) and is scoped to the LOD0↔mid band only, upholding the D4
  no-billboard-caster cap (§4.3); `msaaActive` plumbing named (§2, §5); normal-map unit test reframed
  to harness reality — GL tests SKIP headless, no mock (§6 item 2); per-slice rollback bar added (§6);
  §11-vs-§10 cross-ref, `§6.N` notation, shadow-cost budget, divider consistency corrected. Dismissed
  (unverified): "no `_lod0` files exist" — they do, under the `gameready/` symlink the engine loads
  from; the reviewer searched only the source pack.
- Loop 2 — pending (cold re-read of the edited doc).
