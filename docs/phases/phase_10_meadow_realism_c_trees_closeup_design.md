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

---

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
- Vestige has `AntiAliasMode { None, MSAA_4X, TAA, SMAA }` (+ FXAA planned). **MSAA multisampling
  is live only in `MSAA_4X`**; in `TAA`/`SMAA`/`None` the scene renders to a *non-MSAA* FBO
  (`phase_10_rendering_design.md` §, `renderer.cpp` FBO config). So `GL_SAMPLE_ALPHA_TO_COVERAGE`
  only produces true edge AA in `MSAA_4X` — the design must not depend on it in the other modes.

---

## 3. Design decisions & rationale

| # | Decision | Why |
|---|----------|-----|
| D1 | **Bind the already-loaded normal map to a new unit and add a TBN to `tree_mesh.*`.** Reuse the `scene.*` TBN + normal-sample pattern; don't invent one. | Normals are on disk and in `Material` for free (§2). This is the single highest-payoff close-up change. Global rule 3 (reuse). |
| D2 | **Leaf cards flip the geometric normal to face the viewer *before* applying the sampled normal.** Keep the two-sided `abs`/half-Lambert benefit (a backlit underside must never go black). | The canopy black-leaf fix (52f4b4d) depends on two-sided leaf lighting; normal mapping must not regress it. |
| D3 | **Soft edges = coverage-preservation (all modes) + fwidth sharpen (all modes) + `GL_SAMPLE_ALPHA_TO_COVERAGE` only when `MSAA_4X`.** | Coverage/sharpen are per-pixel math that work everywhere and cost ~nothing; genuine edge AA needs multisample, which only `MSAA_4X` has (§2). TAA/SMAA/FXAA soften the sharpened edge in their own modes. Ben Golus. |
| D4 | **Crossfade becomes a *dithered dissolve* for cutout tiers, driven by the existing `interleavedGradientNoise`.** Discard leaf fragments where `v_alpha < noise(gl_FragCoord)`. | Fixes root cause #1: a fading tier truly thins pixel-by-pixel instead of cross-blending whole silhouettes. The noise function already exists in the frag shader (shadow PCF). |
| D5 | **Apply the same dissolve threshold in `tree_shadow.frag` and give the shadow pass the crossfade alpha.** | Fixes root cause #3 — the ground shadow dissolves in lockstep with the canopy instead of snapping. |
| D6 | **Keep the LOD thresholds and `fadeRange` as-is for now; widen only if the dissolve alone doesn't settle it.** Do not retune blindly. | Global rule 2/11 (shortest correct, stay in lane). The dither dissolve is the mechanism fix; band-width is a tuning knob, changed only with a before/after in hand. |
| D7 | **No PBR metallic/roughness this pass.** Normal + existing half-Lambert/translucency only. | Scope cap. Full PBR for trees is a separate, larger change; the flat-cardboard complaint is a *normal* problem, not a BRDF problem. Rule 2. |

---

## 4. Architecture

### 4.1 T8 — Normal-mapped leaf cards & bark

**Vertex (`tree_mesh.vert.glsl`):**
- Declare `layout(location = 4) in vec3 a_tangent;` and `layout(location = 5) in vec3 a_bitangent;`
  (already populated in the mesh VAO — see §11 R1 verify-point).
- Build the world-space TBN from the same `nm = mat3(i_model)*mat3(u_nodeMatrix)` used for the
  normal, orthonormalising T against N (Gram-Schmidt) as `scene.vert` does; emit `v_tangent`,
  `v_bitangent` (or a packed `mat3 v_TBN`).

**Fragment (`tree_mesh.frag.glsl`):**
- New uniforms: `uniform sampler2D u_normalMap; uniform bool u_hasNormalMap;` on a new unit
  (unit **1**; unit 0 diffuse, unit 3 CSM — both untouched).
- Geometric base normal `Ng = normalize(v_normal)`. For **leaf cards** (`u_useAlphaTest`), flip
  to face the viewer first: `if (dot(Ng, V) < 0.0) Ng = -Ng;` — preserves the two-sided
  guarantee (D2). Bark keeps signed `Ng`.
- If `u_hasNormalMap`: sample `n = texture(u_normalMap, v_texCoord).xyz*2-1`, transform through
  the TBN (rebuilt around the possibly-flipped `Ng` for leaves so the perturbation stays on the
  visible side), `N = normalize(TBN * n)`. Else `N = Ng` (graceful fallback).
- Lighting is otherwise **unchanged**: the same `NdotL` (two-sided `abs` for leaves / signed for
  bark), backlit translucency, and CSM shadow term from 52f4b4d. Only the `N` fed into them
  becomes per-pixel.
- Reduce leaf normal shimmer by biasing the normal-map fetch one mip coarser
  (`textureLod`/`bias`) per GPU Gems 3 §4 — leaf cards are high-frequency and alias otherwise.

### 4.2 T9 — Soft leaf-card edges

All in `tree_mesh.frag.glsl`, guarded to the `u_useAlphaTest` (leaf/impostor) path so bark is
untouched:

- **Coverage preservation** (mip thinning fix): before the cutoff test, rescale
  `texAlpha *= 1.0 + calcMipLevel(v_texCoord * texSize) * 0.25;` where `calcMipLevel` is the
  standard derivative form. Keeps canopy density stable with distance. `_MipScale = 0.25`
  (Ben Golus: "approximates the loss of density almost perfectly") — provisional constant,
  `TODO: revisit via Formula Workbench` if it needs per-species tuning.
- **Edge antialias / sharpen**: convert the hard test into a one-pixel soft edge —
  `float aa = (texAlpha - u_alphaCutoff) / max(fwidth(texAlpha), 1e-4) + 0.5;`. In `MSAA_4X`
  this drives `GL_SAMPLE_ALPHA_TO_COVERAGE` (set `fragColor.a = clamp(aa,0,1)` and enable A2C for
  the leaf draw); in non-MSAA modes it still yields a crisp, coverage-stable cutout that TAA/SMAA/
  FXAA then resolve.
- **A2C enable/disable** is a `ScopedState` around the leaf-tier draw in `drawMeshTier`, active
  only when the renderer's current AA mode is `MSAA_4X`. Must be disabled again for bark and for
  non-MSAA modes so we never get a hard 50%-cutout regression.

### 4.3 T10 — Dithered dissolve crossfade (canopy + shadow)

- **Canopy** (`tree_mesh.frag.glsl`): for cutout tiers, add a screen-door dissolve *before* the
  cutout discard: `if (v_alpha < interleavedGradientNoise(gl_FragCoord.xy)) discard;` then keep
  the existing `texAlpha < cutoff` discard. Write `fragColor.a = 1.0` for the dissolved cutout
  path (no more partial blend), so the two fading tiers interleave stipple-wise instead of
  cross-blending whole silhouettes (D4). The `ScopedBlendState` can then be dropped for cutout
  tiers (kept only if any genuine `BLEND` material remains).
- **Impostor symmetry**: because the impostor is already a cutout, it now dissolves on the *same*
  mechanism as the mid mesh — removing the asymmetry (root cause #2). Offset the two tiers' alpha
  curves as SpeedTree does (one fades out as the other fades in) so total coverage stays ~constant
  through the band (kills the density pulse, root cause #4).
- **Shadow** (`tree_shadow.{vert,frag}.glsl` + `drawShadowTier`): pass the per-instance crossfade
  `i_alpha` into the shadow vertex shader (same binding-3 attribute the mesh pass uses) and apply
  the identical `interleavedGradientNoise` dissolve in `tree_shadow.frag` before its alpha-cutout
  discard. Feed both tiers to the shadow pass during the band (matching the visible pass) instead
  of hard-switching at the threshold (D5, root cause #3).

---

## 5. CPU / GPU placement (project Rule 7)

| Work | Where | Why |
|------|-------|-----|
| TBN construction, normal-map sample & perturb | **GPU** (vertex + fragment) | Per-vertex / per-pixel; mirrors `scene.*`. |
| Coverage-preservation, fwidth edge sharpen, A2C | **GPU** (fragment + fixed-function) | Per-pixel; uses screen-space derivatives that only exist on GPU. |
| Dithered dissolve (canopy + shadow) | **GPU** (fragment) | Per-pixel hash vs per-instance alpha. |
| LOD bucketing, per-instance crossfade `t`, tier selection, A2C-enable decision (AA-mode query) | **CPU** (`TreeRenderer::render`, already there) | Branching / per-instance decision / sparse — unchanged from T1–T7. |

No new CPU/GPU dual implementation and therefore no new parity test — every change is a
GPU-side shading refinement over data the CPU already stages.

---

## 6. Testing (project TESTING.md + Rule 4 audit)

Rendering-shader changes have no pure-function CPU spec to unit-test; coverage is
build + visual-regression + perf, consistent with how T1–T7 were verified.

1. **Build/GL-error gate** — `local-ci.sh` (full, no `--quick`): all existing ctest green,
   0 GL errors on the meadow scene at LOD0/mid/impostor distances (the T1–T7 acceptance).
2. **Normal-map bind unit test** (cheap, worth it): a `TreeRenderer`-level test asserting that a
   species whose material `hasNormalMap()` results in unit-1 binding being issued (mock/verify the
   texture-bind call), and that a material without one does not. Prevents silent regression of D1.
3. **Two-sided-leaf guard** (visual + reasoned): confirm a backlit leaf underside is still ≥ ambient
   (never black) after normal mapping — the 52f4b4d invariant. Captured as a `docs/`-referenced
   before/after screenshot at the known backlit spot, same method used for the canopy fix.
4. **Crossfade dissolve** — walk the camera through both bands (45–60 m, 180–195 m); acceptance =
   no whole-silhouette pop, no density/brightness pulse, and the ground shadow dissolves in step
   (not a snap). Screenshot pair per band.
5. **Per-AA-mode smoke** — verify leaf edges in each of `None / MSAA_4X / TAA / SMAA`: A2C engaged
   only in `MSAA_4X`, no hard 50%-cutout regression in the others.

## 7. Performance plan (60 FPS hard floor)

- Baseline: T4 reported Tree GPU pass **0.12–0.22 ms** on RX 6600 / Mesa. Budget cap for this pass:
  **≤ 2.0 ms** Tree pass, 60 FPS locked (same gate T7 will formalise).
- T8 adds one texture fetch + a TBN per leaf/bark fragment — cheap; the canopy is already
  texture-bound. T9 adds a `fwidth` + a couple of ALU ops. T10's dissolve replaces a blend with a
  `discard` — dropping the leaf-tier blend can *reduce* overdraw cost during the band (no dst read).
- Risk: the mip-coarser normal fetch (T8) and coverage rescale (T9) both touch every canopy pixel;
  measure with `--profile-log` before/after and hold the ≤ 2.0 ms cap. If A2C in `MSAA_4X` costs
  more than budget on dense stands, gate it behind the High/Ultra tree tier (T7).

## 8. Accessibility

- Reduce-motion: the dissolve dither is *static per frame* (screen-space hash on `gl_FragCoord`,
  not time-animated), so it introduces no new motion. The wind sway is the only tree motion and is
  already reduce-motion-gated upstream. No new photosensitivity surface.
- No colour-only information is added; normal mapping affects shading intensity, not hue.

## 9. Implementation slices

- **T8 — Normal-mapped leaf cards & bark.** Bind `getNormalMap()`→unit 1 in `drawMeshTier`; add
  tangent/bitangent + TBN to `tree_mesh.vert`; sample + viewer-flip + perturb in `tree_mesh.frag`;
  mip-bias the leaf fetch. Verify: §6.1, §6.2, §6.3.
- **T9 — Soft leaf-card edges.** Coverage-preservation + fwidth sharpen in `tree_mesh.frag`; A2C
  `ScopedState` around the leaf draw when `MSAA_4X`. Verify: §6.1, §6.5.
- **T10 — Dithered dissolve crossfade.** Screen-door dissolve in `tree_mesh.frag`; plumb `i_alpha`
  + matching dissolve into `tree_shadow.*`; feed both tiers to the shadow pass in-band; offset the
  tier alpha curves. Verify: §6.4, §6.1.

Order is strict: T8 (biggest payoff, self-contained) → T9 (depends on nothing) → T10 (touches the
crossfade + shadow paths T8 leaves alone). Each slice ships behind a full `local-ci.sh` + a visual
capture before the next starts.

## 10. Risks & scope caps

- **R1 (verify-before-code):** T8 assumes the tree draw uses the mesh's own VAO with tangent (loc 4)
  / bitangent (loc 5) *enabled*. The mesh VAO carries them, but `TreeRenderer` layers instance
  buffers on top — **confirm loc 4/5 are enabled in the tree VAO** at T8 start (`bindInstanceBuffers`
  / the VAO the tier draws with). If they're disabled, either enable them or reconstruct
  `B = cross(N,T)` from a tangent-only attribute. Do not assume.
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

_(to be filled as loops run — global rule 14 / project rule 9; each loop dispatched cold, no
prior-loop briefing)_

- Loop 1 — pending.
