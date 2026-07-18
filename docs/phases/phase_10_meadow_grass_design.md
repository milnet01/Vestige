# Phase 10 — Meadow Realism B: Realistic Grass (real blade texture + variation)

Roadmap: **3D_E-0038** (`ROADMAP.md`, Rendering Enhancements). Phase B of the
realism overhaul; pairs with the shipped Phase A (PBR ground textures,
3D_E-0031). Fixture: the 3D_E-0027 meadow benchmark scene.

Layman line (from the roadmap): *make the grass look like real grass — thicker,
taller, varied — instead of hand-drawn cards.*

**Sections:** 1 Goal · 2 Research · 3 Current state · 4 Architecture (4.1 texture
wiring · 4.2 quality tier · 4.3 variation) · 5 CPU/GPU placement · 6 Performance ·
7 Testing · 8 Assets & licensing · 9 Slices (B1–B3) · 10 Accessibility · 11 Risks
· 12 Open questions · 13 Cold-eyes log · 14 Sources.

---

## 1. Goal

Replace the crude procedural grass-blade texture with a **real CC0 grass-blade
alpha texture**, wired through the `FoliageTypeConfig.texturePath` slot that the
foliage renderer currently ignores, with a **procedural fallback** when no file
is present. Widen the per-instance **height / colour** variation the meadow
already seeds, and add an A5-style **graphics-quality tier** for grass so the
~40 k-instance field holds 60 FPS on weaker GPUs.

The grass should read as real blades, not cardboard, and cost no more per frame
at High than today's procedural grass (no extra per-fragment cost — see §6).

### 1.1 Non-goals (this phase)

- **No new geometry technique.** The engine's 3-quad star mesh + instancing +
  distance-fade path stays as-is (verified real — §3). No Bézier-blade compute,
  no mesh-shader grass, no GPU-driven blade generation (research §2 lists these
  as escalations, not this phase).
- **No runtime texture-atlas UV indexing.** One texture maps 1:1 onto each quad
  face (as today). If a source card is landscape, it is cropped/oriented to a
  portrait tuft **at asset-prep** (offline), not indexed in-shader.
- **No automatic GPU detection / frame-time watchdog.** The tier is a manual
  graphics `Setting` (default High), identical in spirit to A5. Auto-tier-drop
  stays future work (relates to 3D_E-0035); if ever added it is logged per
  project Rule 5.
- **No change to the paint/erase authoring API or scene-JSON schema.**

---

## 2. Research summary (what current practice recommends)

- **Cross-quad + alpha-tested blade texture is still the standard cheap grass.**
  Three intersecting quads rendered with back-face culling disabled, alpha-tested
  against a texture that contains several blades, condenses what would be many
  blade-vertices into a handful of quad vertices — "more grass, or grass at
  greater distance, without excessive frame budget." This is **exactly Vestige's
  existing star mesh** (§3); Phase B swaps the *texture*, not the technique.
  (NVIDIA GPU Gems ch. 7; Adrian Hasa, "Real-time grass rendering", 2024;
  Daniel Ilett, "Six Grass Rendering Techniques".)
- **A real multi-blade alpha card reads far better than a single procedural
  blade** for the same vertex cost — the win is entirely in the texture.
- **Escalations we are deliberately *not* taking this phase:** Bézier-curve
  per-blade geometry, mesh-shader procedural grass (GPUOpen), and terrain-tile
  LOD0 "painted grass" billboards for the far field. Noted so a later phase can
  slot them in; the star-mesh + distance-fade path already covers the meadow's
  draw distance.

Sources (§14 lists URLs).

---

## 3. Current state (verified against source)

- **`FoliageRenderer::generateTypeTextures()` hardcodes 4 procedural textures**
  (typeId 0–3: short grass / tall grass / flowers / ferns), built at `init()` in
  `generateProceduralTexture()` (32×64 RGBA), and stored in
  `m_typeTextures[typeId]` (`foliage_renderer.cpp:458-618`). **`texturePath` is
  never read** — `FoliageTypeConfig.texturePath` exists in the struct
  (`foliage_instance.h:48`) and is serialized + shown in the inspector, but no
  code path loads it into a texture. *(Confirms the roadmap claim.)*
- **Distance-fade is real:** `foliage.vert.glsl:57-59` computes
  `v_alpha = 1 - smoothstep(maxDistance*0.8, maxDistance, dist)`, applied to the
  output alpha at `foliage.frag.glsl:159`.
- **"LOD-billboard" is NOT real.** The `foliage.vert.glsl:5` header comment says
  "LOD crossfading", but there is **no** billboard-LOD mechanism in the shader or
  renderer — it is the star mesh at every distance plus the distance-fade above.
  This design does **not** depend on an LOD-billboard path (the roadmap 3D_E-0038
  bullet has since been corrected to match).
- **Grass render distance is hardcoded** to `100.0f` at the call site
  (`engine.cpp:1636`), *not* a setting; **`settings_apply.cpp` has zero foliage
  references** — the graphics-quality preset does not currently reach foliage.
- **A `beginPass("Foliage")` GPU scope already exists** (`engine.cpp:1629`) — so
  the named Foliage GPU timing is available to the editor Performance panel's
  per-pass list; no new instrumentation is needed for the perf read (§6).
- **Meadow wire-up:** grass is `typeId 0`, painted in `engine.cpp:2460-2509` with
  a `FoliageTypeConfig grassCfg` (name/scale/tint set; `texturePath` left empty),
  ~40 k instances (`GRASS_DENSITY`/`STAMP_SPACING`). `m_foliageRenderer` is an
  Engine member (`engine.h:304`, bound `engine.cpp:262`) — reachable at the
  meadow builder for the wire-up call.
- **Texture loading to reuse:** `stbi_load` + the GL-upload tail of
  `generateProceduralTexture` (`foliage_renderer.cpp:605-617`) — the same
  `glCreateTextures / glTextureStorage2D / glTextureSubImage2D / mipmap` sequence
  a real-file loader needs. Extract it into a shared helper (§4.1, Rule 3).

---

## 4. Architecture

### 4.1 `FoliageRenderer` — honour `texturePath` with procedural fallback

- **Extract** the GL-upload tail of `generateProceduralTexture`
  (`foliage_renderer.cpp:605-617`) into a private helper
  `GLuint uploadRGBA8(const uint8_t* pixels, int w, int h)` (immutable storage +
  full mip chain + LINEAR_MIPMAP_LINEAR / CLAMP_TO_EDGE, exactly as today). Both
  the procedural generator and the new file loader call it — no duplicated GL
  setup (Rule 3).
- **New public method:**
  ```cpp
  /// Loads a real alpha texture for a foliage type from disk, replacing the
  /// procedural default. On decode failure the procedural texture is kept
  /// (fallback) and a warning is logged. Must be called after init().
  void setTypeTexture(uint32_t typeId, const std::string& path);
  ```
  Implementation: `stbi_load(path, …, 4)` (force RGBA); on a **null** decode,
  **leave the current texture in place** and `Logger::warning`. On a non-null
  decode, upload into a **temporary** handle first — `GLuint next =
  uploadRGBA8(pixels, w, h);` — then `stbi_image_free(pixels)` to release the
  decoded **CPU** buffer. Only if `next != 0` do we `glDeleteTextures` the old
  `m_typeTextures[typeId]` and store `next`; if `uploadRGBA8` returns 0 (GL
  failure), delete `next`, keep the existing texture, and warn. **The old texture
  is never freed before its replacement is known-good**, so neither a decode nor an
  upload failure can strand a type with no texture. `stbi_load` returns straight
  (non-premultiplied) RGBA; alpha is the blade mask the fragment shader already
  alpha-tests at 0.5 — no shader change.
  **Edge-case contract:** an **empty `path`** is treated as decode-failure (keep
  the existing texture, no warning — it just means "no override"); the method
  **must be called after `init()`** so a procedural default already exists to fall
  back to (a debug assert guards `m_initialized`); `typeId` may be any value —
  `m_typeTextures` is a `std::unordered_map`, so a not-yet-present key simply
  registers a new entry (no out-of-bounds), though only painted typeIds render.
  **Ownership stays uniform:** every `m_typeTextures` entry remains a raw `GLuint`
  the renderer owns and `shutdown()` already deletes — no RAII `Texture` object,
  no mixed ownership.
- **No mesh or shader change.** UVs, alpha test, wind, lighting, distance-fade all
  unchanged. The texture is the only moving part on the render side.

### 4.2 Grass quality tier (reuse the A5 graphics-quality `Setting`)

A5 **extended** the existing `RendererQualitySink` (`settings_apply.{h,cpp}`) —
which maps the graphics `QualityPreset` → renderer knobs (the AA/SSAO/bloom/heavy-
post setters predate A5) — with a terrain-ground lever. Phase B adds **one**
foliage lever to that same sink — no parallel system, no per-frame settings read.

- **Lever = grass render distance + grass shadow-casting.** These are the only
  *runtime-tunable* grass perf knobs (per-instance density is baked into chunks at
  scene-build by `paintFoliage`, so it is not a runtime dial). Fewer metres of
  grass drawn = fewer instances = the highest-value meadow perf lever, and grass
  is the single biggest instance count in the scene.
- **New members + setter on `FoliageRenderer`:** `float renderDistance = 100.0f;`
  and `bool castShadows = true;` — **public, no `m_` prefix**, matching the sibling
  public knob `windAmplitude` (`foliage_renderer.h:83`) the engine already mutates
  directly — plus `void setQuality(FoliageQuality q);` mapping the enum to those
  two members. The sink gains `virtual void setFoliageQuality(FoliageQuality) = 0;`,
  and the concrete `RendererQualityApplySinkImpl` forwards it to
  `FoliageRenderer::setQuality`. Wiring:
  - `engine.cpp:1636` passes `m_foliageRenderer->renderDistance` instead of the
    literal `100.0f`.
  - **Shadow gate (committed contract):** `FoliageRenderer::renderShadow`
    early-returns when `!castShadows` — the "Low = no grass shadows" tier is a
    one-line guard at the top of that method, so the decision stays in the
    renderer and the `Renderer::setFoliageShadowCaster` caller is untouched.
- **Tier table (default High):**

  | Preset | Grass distance | Grass shadows | Blade texture |
  |---|---|---|---|
  | **Ultra / High** | 100 m | yes | real |
  | **Medium** | 70 m | yes | real |
  | **Low** | 45 m | no | real (still the real texture — the cost saved is draw distance + shadow pass, not texture fetch) |

  `Custom` leaves foliage untouched (early-return, exactly as A5's terrain row).
- **Free-standing scoped enum** `enum class FoliageQuality { Low, Medium, High };`
  in namespace `Vestige`, **defined in `foliage_renderer.h`** (not nested in the
  class) and **forward-declared** `enum class FoliageQuality;` in
  `settings_apply.h` — exactly how A5 wired `TerrainGroundQuality`
  (`settings_apply.h:42` forward-decl, `:122` bare-name signature), so the sink
  header needs no `#include "foliage_renderer.h"`. No wire contract with a shader
  (unlike A5's `u_groundQuality`) — the enum only drives CPU-side distance/shadow
  members, so its integer values are free to change.

### 4.3 Variation (meadow tuning — no engine code)

Tunes **existing** `FoliageTypeConfig` fields (`minScale` / `maxScale` /
`tintVariation`), which `paintFoliage` bakes into the per-instance vertex
attributes `i_scale` / `i_rotation` / `i_colorTint` (`foliage.vert.glsl:14-16`) at
paint time; this is a tuning change in the meadow builder (`engine.cpp` grass
block), not new rendering code:

- **Height:** widen `grassCfg.minScale/maxScale` (taller, more varied silhouette).
  The star mesh is **0.4 m** tall (`foliage_renderer.cpp:356`); the meadow's
  current **0.7–1.5** scale (`engine.cpp:2475-2476`) → 0.28–0.6 m. Raise the top
  of the range for occasional taller blades. Optionally seed a light **second
  pass of the existing tall-grass type (typeId 1)** for silhouette variety — both
  types get a real texture via §4.1. (Second pass is optional polish; core is one
  type.)
- **Colour:** widen `grassCfg.tintVariation` so blades vary green→yellow-green;
  already multiplied into the fragment colour (`foliage.frag.glsl:118`).
- **Density** stays the 3D_E-0027 benchmark knob (unchanged — it is the perf
  fixture's primary dial; not touched here beyond what the tier distance covers).

---

## 5. CPU / GPU placement (project Rule 7)

| Work | Placement | Reason |
|---|---|---|
| Blade-texture decode (`stbi_load`) + GL upload + mipgen | **CPU**, one-time at meadow build | I/O + allocation; not per-frame. |
| Procedural fallback bake | **CPU**, one-time | Already CPU; sparse/decision logic. |
| Per-instance transform, wind, alpha-test, lighting, distance-fade | **GPU** (vertex/fragment) | Per-vertex / per-pixel, data-parallel; unchanged from today. |
| Tier selection (preset → distance / shadow flags) | **CPU** | Branching decision, one-time on setting change. |

No "CPU now, move later." No new per-frame CPU work — the tier writes two member
fields on a setting change; the render loop already reads them. No fitted formula
this phase, so no Formula-Workbench parity mirror (Rule 6/7 n/a here).

---

## 6. Performance (60 FPS is a hard requirement)

- **Per-fragment cost is unchanged.** Whether the blade texture is the 32×64
  procedural or a real ~256×512 texture, the fragment shader does **one**
  alpha-tested `texture()` sample. The dominant grass cost is instance count and
  overdraw, both unchanged at High. So Phase B should **not** materially regress
  the Foliage pass at High — the honest before/after is "≈ equal at High, cheaper
  at Medium/Low."
- **Frame target:** the full meadow (terrain + **grass** + props + water + CSM)
  holds **≥ 60 FPS at 1080p on the RX 6600** (16.6 ms frame).
- **Per-pass target:** the **hard gate for B3 is ≥ 60 FPS** on the full meadow at
  High; the **Foliage** pass **≤ 2.5 ms** is an **advisory watch-line** (its ~1/7
  share of the 16.6 ms frame), *not* a blocking bound. B3 records the current
  procedural-grass Foliage-pass cost as the baseline at first measurement; a
  reading above 2.5 ms that **still holds ≥ 60 FPS** updates the watch-line rather
  than failing the slice — only dropping below 60 FPS blocks B3.
- **How it's measured:** the existing editor **Performance panel** GPU tab lists
  the named **`beginPass("Foliage")`** scope (`engine.cpp:1629`) and live FPS.
  Read on the 3D_E-0027 meadow against ≤ 2.5 ms / ≥ 60 FPS at High — the same
  **manual maintainer read** A5 used. The automated path's *tooling already
  exists* — the `--profile-log` CSV logger (`engine/profiler/profile_log.{h,cpp}`,
  consumed at `engine.cpp:169`) and the comparator `tools/perf_gate.py`
  (**3D_E-0030 ✅ shipped 2026-07-17**). What is still pending is a **committed
  RX 6600 baseline JSON** to compare against — a real-GPU capture step (CI is
  GPU-less / llvmpipe) the maintainer runs via `--update-baseline`. Until that
  baseline lands, B3 uses the manual panel read. A larger texture costs on the
  order of a few hundred KB more VRAM and slightly more texture-cache pressure —
  bounded by the ≤ 2.5 ms pass gate above, which catches any real regression
  regardless.
- **Tiers are a graphics `Setting`, NOT `FormulaQualityManager`.** Same
  distinction A5 documented: these toggle *render distance / shadow casting*
  (a graphics axis), not a formula's evaluation accuracy (`FULL/APPROXIMATE/LUT`).
  Default **High**; no auto-detection this phase.

---

## 7. Testing

- **File-load + fallback.** The **branch decision** — `(path empty OR decode
  null)` → keep the procedural texture, else adopt the loaded one — is factored as
  a pure predicate and unit-tested with **no GL** (empty path, a missing file, and
  a valid decode-result stub each route correctly). Adopting frees the old handle
  before replacing it (§4.1) — a code invariant, not a runtime assert. The live GL
  path (the real texture actually bound, no leak-into-error) is covered by the
  meadow `--visual-test` frame rendering **GL-error-free** (`glGetError`, the
  objective render check below) — no bespoke GL unit-harness needed.
- **Tier mapping (unit).** Extend the A5 `RecordingRendererQualitySink` test
  pattern (`tests/test_settings.cpp`): each preset maps to the expected
  `FoliageQuality` (Low→Low with shadows off + 45 m; Medium→Medium; High/Ultra→
  High; Custom→untouched). Pure preset→knob mapping, no GL.
- **Objective render check (automated).** The foliage shader links and a meadow
  frame renders **GL-error-free** (`glGetError` returns `GL_NO_ERROR`) with the
  real texture bound — an objective assertion during the `--visual-test` run,
  distinct from the maintainer's visual inspection below. This is the automated GL
  gate the file-load bullet above refers to.
- **Visual (`--visual-test`) — maintainer inspection.** `open_meadow` /
  `pond_shore` viewpoints: grass reads as real blades (not a flat procedural
  cone), varied in height/colour, no aspect squish (portrait texture — §4.1 / §8),
  no popping at the tier distance boundary (distance-fade covers the cut).

---

## 8. Assets & licensing

- **Primary (committed default): a real CC0 grass-blade alpha texture**, portrait
  tuft orientation, alpha = blade mask, ~256×512 (PoT). Source options, all
  **CC0 (no attribution required; credited as courtesy in
  `THIRD_PARTY_NOTICES.md`, row in `ASSET_LICENSES.md`)**:
  - **Verified-available baseline:** OpenGameArt "grass blades alpha card texture
    (side view)", **CC0**, 1024×256 (license confirmed verbatim CC0 at fetch
    time). Landscape → cropped/oriented to a portrait tuft **at asset-prep**
    (offline one-time), committing the portrait result. No runtime atlas logic.
  - Higher-fidelity alternatives if a better portrait tuft is found: ambientCG /
    3DTexel CC0 vegetation cut-outs. Final pick resolved during B2 (§12).
- **Committed under `assets/textures/foliage/grass_blades.png`.** `copy_assets`
  globs `assets/` — no CMake change (same as the terrain/model subdirs).
- **`_local/` override:** the meadow resolves
  `assets/textures/foliage_local/grass_blades.png` first (git-ignored), else the
  committed default — identical to the `terrain_local/` / `nature_local/` pattern
  (A + props precedent), so the maintainer can drop a photoreal texture in without
  touching code.
- **Fallback:** if neither path exists, `setTypeTexture` keeps the procedural
  texture (§4.1). The procedural generator stays in the tree as the guaranteed
  floor.
- **Size:** one ~256×512 RGBA PNG ≈ a few hundred KB — well under the 1 MB soft
  line; no side-repo externalization needed (unlike A's 1K layer set).

---

## 9. Implementation slices

1. **B1 — `texturePath` honoured + fallback.** Extract `uploadRGBA8` helper
   (Rule 3); add `setTypeTexture(typeId, path)` with procedural fallback.
   *Verify:* load/fallback unit test green; foliage frame GL-error-free.
2. **B2 — Real asset + meadow wire-up + variation.** Commit the portrait CC0
   grass texture + `_local/` override resolve; the meadow calls
   `setTypeTexture(0, path)` (and typeId 1 if the second pass is kept); widen
   scale/tint variation. *Verify (subjective — an asset swap has no automated
   gate; the automated coverage is B1's unit test + B3's tier test):*
   `--visual-test` — meadow reads as real varied grass, no aspect squish;
   ASSET_LICENSES + THIRD_PARTY_NOTICES rows added.
3. **B3 — Quality tier + perf.** Add the free-standing `FoliageQuality` enum +
   `renderDistance` / `castShadows` members; wire into `RendererQualitySink`
   (High/Med/Low); pass `renderDistance` at `engine.cpp:1636`; gate the shadow
   pass on `castShadows`. *Verify — automated close criteria:* the preset→tier unit
   test is green **and** the meadow frame renders GL-error-free (§7). *Plus a
   manual perf read* (maintainer, Performance panel **Foliage** GPU scope + FPS on
   the RX 6600 meadow): **≥ 60 FPS at High is the gate**; ≤ 2.5 ms Foliage is the
   advisory watch-line (§6). The perf read is manual because the automated gate
   still needs a committed RX 6600 baseline (§6) — automating it is a tracked
   follow-up, not a B3 blocker. CHANGELOG row added.

Each slice commits locally; the phase pushes when B3 lands green (§ push cadence —
public repo, batch push).

---

## 10. Accessibility

- Grass legibility must not rely on hue alone (colour-blind): value contrast
  between blades and the textured ground (Phase A) carries the read; the tint
  variation stays within a value band that keeps blades distinct from soil.
- No flashing; wind motion is gentle and already amplitude-clamped
  (`engine.cpp:1625`). The Low tier (shorter distance, no grass shadows) doubles
  as the low-end-GPU / reduced-detail path, exposed via the existing quality
  setting.

---

## 11. Risks & mitigations

- **Aspect squish** from mapping a landscape source card onto a portrait quad →
  crop/orient to portrait at asset-prep (§8); visual-test catches it.
- **Texture leak** when `setTypeTexture` replaces the procedural default →
  explicit `glDeleteTextures` of the old GL handle before replacing **and**
  `stbi_image_free` of the decoded CPU buffer after upload (§4.1); uniform
  raw-`GLuint` ownership keeps `shutdown()` correct.
- **Perf regression** at High from a bigger texture → §6 shows the per-fragment
  cost is one sample either way; the tier (default High) + Performance-panel read
  gate it. No auto-clamp, so no Rule-5 clamp-logging obligation this phase.
- **Fallback silently masking a missing asset** → `setTypeTexture` logs a
  `warning` on decode failure so a missing committed asset is visible in the log,
  not silently procedural.

---

## 12. Open questions for review

- **Final asset pick (B2):** the verified OpenGameArt CC0 card (cropped portrait)
  is the guaranteed baseline; a better CC0 portrait tuft (ambientCG/3DTexel) may
  replace it if found during B2. Either way CC0, ≤ a few hundred KB.
- **Second grass type (tall grass, typeId 1):** include a light second pass for
  silhouette variety, or keep Phase B to one type + widened scale variation?
  Leaning **one type** for minimalism (Rule 2); the tall-grass type is already
  wired and can be a fast follow if the single-type read is too uniform.

---

## 13. Cold-eyes loop log

Project Rule 9 / global Rule 14: fresh subagents per loop, no authoring context,
loop until no substantive verified finding remains.

**Loop 1 (2026-07-18)** — 2 cold reviewers (accuracy lane + consistency/
completeness lane). Tally: CRITICAL 0 · HIGH 1 · MEDIUM 5 · LOW 7 · INFO 2
(verified 13 / unverified 0). All 13 verified findings fixed:
- HIGH — `setTypeTexture` edge-case contract (empty path, pre-`init()`, `typeId`
  range) was undefined → added the §4.1 edge-case paragraph.
- MEDIUM — §6/§9 claimed the `--profile-log` CSV gate + 3D_E-0030 "still pending";
  both the logger (`profile_log.{h,cpp}`) and comparator (`tools/perf_gate.py`,
  3D_E-0030 ✅ 2026-07-17) are shipped — only the RX 6600 baseline capture is
  pending → reworded §6 + §9 B3.
- MEDIUM — missing `stbi_image_free` of the decoded CPU buffer → added to §4.1 +
  the §11 leak row.
- MEDIUM — garbled `setFoliageQuality(FoliageRenderer&-driven knobs)` pseudo-sig +
  a `TerrainGroundQuality`-vs-`FoliageQuality` naming slip → §4.2 concrete
  signatures.
- MEDIUM — shadow-cast gate site left open in prose → committed to the
  `FoliageRenderer::renderShadow` early-out contract (§4.2).
- LOW ×7 — "A5 built the sink" → "extended"; added the section index (TOC);
  firmed the §7 fallback-test contract (pure predicate + GL-error-free frame, no
  unverified GL harness claim); "atlas" → "texture" where it meant the single card
  (kept the §1.1 "no texture-atlas UV indexing" non-goal); trimmed a restated
  per-fragment-cost line; leaned the "negligible VRAM" claim on the ≤2.5 ms gate;
  amended the ROADMAP 3D_E-0038 bullet to drop the inaccurate "LOD-billboard".
- INFO (surfaced, not fixed) — the CC0 OpenGameArt asset is B2-scoped (no defect);
  `foliage_renderer.h:89`'s "12 vertices" doc-comment contradicts the impl's 18
  (`createStarMesh`, `foliage_renderer.cpp:353`) — a **code**-comment fix for the
  owner, out of scope for a docs review.

**Loop 2 (2026-07-18)** — 2 cold reviewers (same lanes, briefed identically, not
told what loop 1 changed). Tally: CRITICAL 0 · HIGH 2 · MEDIUM 3 · LOW 6 · INFO 4
(verified 11 / unverified 0). Accuracy lane found only LOW/INFO (doc verified
exact against source); the substantive set came from the consistency lane. All
fixed:
- HIGH — `FoliageQuality` was described as nested "on `FoliageRenderer`" while the
  sink used the bare name (unwritable without an include); made it a free-standing
  scoped enum forward-declared in `settings_apply.h`, mirroring A5's
  `TerrainGroundQuality` exactly (§4.2).
- HIGH — `setTypeTexture` deleted the old texture *before* the new upload was
  known-good → an upload (GL) failure stranded the type with no texture; reordered
  to upload-into-temp, free-old-only-on-success, and added the upload-failure
  branch (§4.1).
- MEDIUM — ≤2.5 ms was both "target, not a bound" and a B3 pass/fail → named
  **≥60 FPS the hard gate, ≤2.5 ms an advisory watch-line** (§6 + §9 B3).
- MEDIUM — B3 had no objective close criterion (only a manual hardware read) →
  stated the automated close = tier unit test + GL-error-free frame; the perf read
  is manual (baseline pending), automation a tracked follow-up (§9 B3).
- MEDIUM — uncited numeric constants (0.4 m mesh, 0.7–1.5 scale, Rule 13) → cited
  `foliage_renderer.cpp:356` + `engine.cpp:2475-2476` (§4.3).
- LOW ×6 — `m_`-prefixed public members → unprefixed to match `windAmplitude`;
  relabelled `i_scale`/`i_rotation`/`i_colorTint` as per-instance attributes (not
  `FoliageTypeConfig` fields); clarified the GL-error check is an objective
  `--visual-test` assertion; softened the unverified Performance-panel-UI claim;
  tightened the "correcting the roadmap" tense (already corrected); B2 verify
  flagged subjective; trivial line-cite nudges (`:89`, `605-617`).

---

## 14. Sources

- NVIDIA GPU Gems, ch. 7 "Rendering Countless Blades of Waving Grass" —
  https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-7-rendering-countless-blades-waving-grass
- Adrian Hasa, "Real-time grass rendering" (2024) —
  https://adrianhasa.blog/rendering/2024-07-realtime-grass/
- Daniel Ilett, "Six Grass Rendering Techniques in Unity" —
  https://danielilett.com/2022-12-05-tut6-2-six-grass-techniques/
- GPUOpen, "Procedural grass rendering — mesh shaders" (escalation, not used) —
  https://gpuopen.com/learn/mesh_shaders/mesh_shaders-procedural_grass_rendering/
- OpenGameArt, "grass blades alpha card texture (side view)", CC0 (asset source) —
  https://opengameart.org/content/grass-blades-alpha-card-texture-side-view
