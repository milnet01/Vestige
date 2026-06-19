# Phase 10 — Rendering Enhancements (Design Doc)

**Status:** ✅ Slice R1 signed off for implementation (2026-06-19). Cold-eyes looped to clean — 4 loops, converged Loop 4 with no CRITICAL/HIGH/MEDIUM (polish-only), sign-off delegated per session standing instruction. Sequencing approved by user ("Foundation first"). R2–R4 remain planning-fidelity (each gets its own design-of-record + cold-eyes before implementation). See the Cold-eyes loop log at the foot of this doc.
**Research:** Inline below (per-slice Sources sections). No separate research doc — the techniques (MRT velocity buffers, pre-integrated SSS, SSGI) are well-established; citations are given per slice.
**Scope:** The three ROADMAP "Rendering Enhancements" bullets — motion vectors from the geometry pass via MRT, subsurface scattering (SSS), and screen-space global illumination (SSGI). This doc is the **design-of-record for Slice R1 (motion-vector MRT, rigid-body parity)** in full (§4); the later slices (R2–R4) are sketched at planning fidelity (§3) and each get their own design-of-record section before implementation.

---

## 0. What exists today (reality check, 2026-06, verified against source)

The engine is a **forward renderer** (not deferred — `scene.frag.glsl` has a single `out vec4 fragColor`, verified `assets/shaders/scene.frag.glsl:23`). Motion vectors for TAA are produced **only in the TAA path**, by two passes in `engine/renderer/renderer.cpp`:

1. **Full-screen camera-motion pass** (`renderer.cpp:953-970`, shader `assets/shaders/motion_vectors.frag.glsl`). Reconstructs world position from the resolved depth (`m_resolveDepthFbo`) and reprojects through `m_prevViewProjection`. Sky (reverse-Z depth ≤ 0.0001) → motion `(0,0)`. Writes **every** pixel.
2. **Per-object overlay pass** (`renderer.cpp:972-1023`, shaders `motion_vectors_object.{vert,frag}.glsl`). Re-renders **every opaque `renderItem`** with per-draw `u_model` / `u_prevModel`, depth-tested (`GL_GREATER`, reverse-Z), **overwriting** the camera-motion result where scene-shader geometry sits. This is the redundant full geometry re-draw this bundle removes.

**Key facts that shape R1 (all verified):**

| Fact | Source | Consequence |
|------|--------|-------------|
| Motion is needed **only** when TAA is on, and in TAA mode the scene renders into the **non-MSAA** `m_taaSceneFbo` (the MSAA `m_msaaFbo` is used only when TAA is *off*). | `renderer.cpp:2989-2999`, FBO config `renderer.cpp:666-673` | An MRT motion attachment on `m_taaSceneFbo` is **non-MSAA** → directly sampleable, **no multisample-resolve of the motion buffer needed**. This removes the hardest part of the refactor. |
| The scene pass's `gl_Position` uses the **jittered** VP; `m_lastViewProjection = projection(jittered) * m_lastView` (`renderer.cpp:3110`), and `m_prevViewProjection = m_lastViewProjection` from the previous frame (`renderer.cpp:1490`) — so prev-VP carries the previous frame's jitter. The overlay uses exactly these matrices. | `renderer.cpp:3104-3110`, `:1490`, `motion_vectors_object.vert.glsl:29` | Emitting motion from the scene pass reproduces the overlay's **current-clip term** with the same jitter convention. Parity holds for the common case (opaque, non-cutout, single-layer) — §4.3 scopes the exact guarantee. |
| The overlay covers only **opaque** `m_currentRenderData->renderItems`. But the main scene pass (`m_sceneShader`) *also* draws the **cloth** pass (`renderer.cpp:3295+`, `u_model` only) and the **transparent** pass (`renderer.cpp:3389+`, blend on, depth-write off) into the same FBO — these are **not** in the overlay. Terrain / water / particles / skybox are drawn by other passes. | `renderer.cpp:1000-1017` (overlay), `:3149` (opaque), `:3295` (cloth), `:3389` (transparent) | All non-overlay depth geometry (cloth, terrain, water, particles) gets motion from the **camera-motion fallback** today; transparent geometry writes no depth so it inherits the underlying pixel's camera motion. R1 **must keep** that fallback, and must **gate motion-write per-pass** (on only for opaque `renderItems`) so cloth/transparent/skybox do not corrupt the motion attachment — see §4.1/§4.4. |
| Per-object previous world matrix is already tracked: `RenderItem::prevWorldMatrix` (`scene.h:60`) and the `m_prevWorldMatrices` cache keyed by `entityId`, repopulated end-of-frame by `updateMotionOverlayPrevWorld()`. | `scene.h:38-61` (struct), `motion_overlay_prev_world.h:50-77`, `renderer.cpp:1505-1509` | The rigid-body prev matrix R1 needs already exists — no new history machinery for R1. |
| Motion math (both shaders): `motion = currUV − prevUV`, where `UV = ndc*0.5+0.5`, with a `|w|>1e-6` divide guard. Sky → `(0,0)`. | `motion_vectors_object.frag.glsl:23-37`, `motion_vectors.frag.glsl` | R1's scene-pass motion output reuses this exact math → identical values. |
| Skinned/morph meshes currently get **rigid-body motion only** — the overlay vertex shader projects the **raw `a_position`** (`u_model * a_position`, *not* the skinned position) for both terms (`motion_vectors_object.vert.glsl:25-27`). Previous-frame bone matrices / morph weights are **not retained**. | `renderer.cpp:977-981` (comment), `motion_vectors_object.vert.glsl:25-26`, `skeleton_animator.cpp:300-332`, `drawMesh()` skinning-bind region `renderer.cpp:1661-1714` | Correct animated-mesh motion is **out of scope for R1** (it needs a prev-pose history) → Slice R2. R1 keeps byte-identical rigid-body motion for skinned meshes by computing the motion term from the **base position** (§4.1 #2), matching the overlay — no regression. |
| No previous-frame normal buffer is retained. | Explore (forward renderer; normals computed in `scene.frag.glsl`) | The `V_mask = α(1 − n_cur·n_prev)` disocclusion signal needs a normal + history → also Slice R2 (it pairs naturally with the prev-pose work). |

---

## 1. Goals

- **R1 (this doc's design-of-record):** emit motion vectors for opaque `renderItem` geometry directly from the main scene pass via an MRT attachment, and **delete the per-object overlay re-draw**. For the common case (opaque, non-cutout, single-layer geometry) the result is **visually identical** to today (rigid-body parity, pinned by a parity test); for cutout/interpenetrating geometry the motion now matches the *visible* fragment (same depth/alpha/cull state as the colour pixel) rather than the overlay's independent re-draw — a correctness improvement, not a regression (§4.3). The win is **one full opaque-geometry pass removed** per TAA frame, plus it lays down the geometry-pass MRT buffer that R2 (animated motion + prev-normal) and R4 (SSGI temporal accumulation) build on.
- **Shared prerequisite:** R1 requires extending the `Framebuffer` class to support a **second colour attachment** (it currently supports exactly one — `framebuffer.cpp:157-235`, single `bindColorTexture`). This is bundle-wide infrastructure: R2's prev-normal buffer and R4's SSGI normal/radiance buffers need the same MRT support. Specified as a prerequisite work item in §4.0.
- **R2:** correct motion for skinned & morph-target meshes (prev-pose history), plus the previous-frame normal buffer and the `V_mask` disocclusion signal.
- **R3:** subsurface scattering — pre-integrated wrap-lighting approximation (no ray marching), per-material thickness/transmission, for the Tabernacle's dyed-linen curtains.
- **R4:** screen-space global illumination — single-bounce indirect light from the scene color + depth + normal buffers, temporally accumulated using the R1/R2 motion buffer.
- **60 FPS hard floor** preserved at every slice (RX 6600, 1080p). R1 is a net *reduction* in GPU work, so the floor is structurally safe; R3/R4 get measured budgets in their own design sections.

---

## 2. Scope decision — foundation-first, parity-first (Rule 9, "simpler path")

This bundle is core-render-path surgery, so it is sliced to keep each step independently shippable and low-risk:

- **R1 is a behaviour-preserving rewrite, not a feature.** Its acceptance bar is *"the motion buffer is bit-identical to today for static scenes, and no worse for dynamic rigid bodies"* — enforced by a parity test (§8) and a visual check. It deliberately does **not** fix skinned-mesh motion (that would smuggle a risky animation-system change into a plumbing slice). Splitting parity (R1) from the animated-motion feature (R2) means a regression in either is unambiguous about which slice caused it.
- **The camera-motion fallback stays.** It is the only motion source for non-scene-shader depth geometry (terrain/water/cloth/particles). Removing it would regress their TAA. R1 removes *only* the redundant overlay re-draw.
- **SSS (R3) before SSGI (R4)** within the "looks" half: SSS is self-contained (a lighting-model addition, no history buffers), SSGI is the heaviest feature and benefits from R1+R2's motion buffer being solid first.

---

## 3. Slice plan

| Slice | Title | Complexity | Depends on | Status |
|-------|-------|------------|------------|--------|
| **R1** | `Framebuffer` MRT support + motion vectors via geometry-pass MRT (rigid-body parity; drop overlay) | M/L | — | 📝 designed below (§4) |
| **R2** | Correct skinned/morph motion (prev-pose history) + prev-normal buffer + `V_mask` disocclusion | L | R1 | ⬜ design-of-record TBD |
| **R3** | Subsurface scattering (pre-integrated wrap BRDF; per-material thickness/transmission) | M | — | ⬜ design-of-record TBD |
| **R4** | Screen-space global illumination (single-bounce, temporally accumulated) | L | R1 (R2 ideal) | ⬜ design-of-record TBD |

R2–R4 each get a full architecture section (matching §4's fidelity) + their own cold-eyes loop before implementation. This doc ships R1's design first so the foundation is reviewed and built before the features that lean on it.

---

## 4. Slice R1 — Motion vectors via geometry-pass MRT (design-of-record)

### 4.0 Prerequisite — extend `Framebuffer` for a second colour attachment

`Framebuffer` today (`framebuffer.cpp:157-235`) creates exactly one colour attachment (single `GLuint m_colorAttachment`, always `GL_COLOR_ATTACHMENT0`, no `glDrawBuffers`), the only float format is `GL_RGBA16F` (`framebuffer.cpp:163`), and `bindColorTexture(unit)` binds only attachment 0 (`framebuffer.cpp:95-104`). MRT support must be added first. This is **bundle-wide infrastructure** (R2 prev-normal, R4 SSGI buffers reuse it), so it is built generically, not bolted onto `m_taaSceneFbo`:

1. `FramebufferConfig` gains an optional `secondColorAttachment` flag (default off). When set, `create()` allocates a second texture (new member `GLuint m_colorAttachment1`) and attaches it at `GL_COLOR_ATTACHMENT1`, then calls `glNamedFramebufferDrawBuffers(fbo, 2, {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1})`. Single-attachment FBOs are byte-for-byte unchanged (no `glDrawBuffers` call added to them).
2. **Format:** the motion attachment reuses the existing `GL_RGBA16F` float path (there is no RG16F path, and adding one is unnecessary). Motion goes in `.rg`; the `.b` channel is a **coverage flag** (§4.4); `.a` unused. Two wasted channels at 1080p ≈ 16 MB — negligible, and avoids a new format enum.
3. New accessor `bindColorTexture(unit, attachmentIndex)` (default `attachmentIndex = 0`, so all existing call-sites are unchanged) to bind attachment 1 as a sampleable texture for the combine pass.
4. `resize()`, `cleanup()`, **and the move-constructor / move-assignment** (`framebuffer.cpp:25-56`, which today copy only `m_colorAttachment`) updated to cover the second texture — omitting the move-ctor would leak or double-free `m_colorAttachment1`.
5. **Per-attachment clear (load-bearing — see §4.4).** A single `glClear(GL_COLOR_BUFFER_BIT)` writes the *one* global `glClearColor` into **every** draw buffer, so a 2-attachment FBO would clear the motion attachment to the scene clear-colour, not 0. `Framebuffer` therefore gains a `clearSecondAttachment()` (or `create()`/`bind()` issues it): `glClearNamedFramebufferfv(fbo, GL_COLOR, 1, vec4(0,0,0,0))`, called each frame **after** the existing colour+depth clear so the motion attachment (and its `.b` coverage flag) always starts at 0 regardless of the scene clear colour. The attachment-0 clear path is unchanged.

This step is independently testable (FBO completeness check `GL_FRAMEBUFFER_COMPLETE`, draw-buffer enumeration, and a clear-to-zero check that is independent of the scene clear colour) and lands as the first commit of R1.

### 4.1 What changes (and what does not)

**Changes:**
1. `m_taaSceneFbo` is constructed with `secondColorAttachment = true` → a non-MSAA `GL_RGBA16F` motion attachment at `GL_COLOR_ATTACHMENT1`. (`m_taaSceneFbo` is genuinely non-MSAA — `samples = 1`, `renderer.cpp:669` — and is what the scene pass binds in TAA mode, `renderer.cpp:2994`. So the motion attachment needs **no** multisample resolve.)
2. `scene.vert.glsl` / `scene.frag.glsl` gain a motion output. **Critically, the motion terms use the raw, pre-skin/pre-morph base object-space position** (`position`), *not* the skinned/morphed position that feeds `gl_Position` — because the overlay computed motion from the raw `a_position` for both terms (`motion_vectors_object.vert.glsl:25-27`). So the vertex shader emits a dedicated `v_currentClip_motion = u_projection * u_view * (model * position)` and `v_prevClip_motion = u_prevViewProjection * (prevModel * position)` (both from the base position; note `scene.vert.glsl` binds **separate** `u_view`/`u_projection` at `:48-49`, not a combined `u_viewProjection` — the product equals the overlay's jittered `m_lastViewProjection`, and `u_prevViewProjection` is a new combined uniform set from `m_prevViewProjection`), and the fragment writes `vec4(currUV − prevUV, coverageFlag, 0)` to `out` location 1, reusing the **exact** math of `motion_vectors_object.frag.glsl` (same `safeClipDivide` 1e-6 guard). For non-skinned/non-morph meshes the base position equals the shaded position, so `v_currentClip_motion == gl_Position`; for skinned/morph meshes the base position reproduces the overlay's rigid-body motion exactly. The motion write is gated by a uniform `u_writeMotion` (§4.2). **Implementation note:** the motion output must be written **before any early `return`** in `scene.frag.glsl` (e.g. the wireframe path, `:967-968`) so attachment 1 is never left undefined for an opaque pixel; the late `fragColor.rgb` edits (water caustics `:1265`, cascade-debug `:1279`) touch only attachment 0 and need no change.
3. The per-object overlay pass (`renderer.cpp:972-1023`) and shaders `motion_vectors_object.{vert,frag}.glsl` are **deleted**.
4. The camera-motion full-screen pass is **retained but extended** into a *combine* pass: it samples the scene-pass motion attachment and prefers it where the coverage flag says opaque geometry wrote it (§4.4).

**Does not change:** the colour image (attachment 0 written exactly as today), the MSAA path (TAA-off still uses `m_msaaFbo`, untouched), the TAA resolve shader (`taa_resolve.frag.glsl` still samples one motion texture, same RGBA16F format/semantics), skinned/morph motion (still **rigid-body-only**, byte-identical to the overlay because motion uses the base position per change #2 — R2 is what adds animated-pose motion), and all non-TAA rendering.

### 4.2 Per-pass motion-write gate + previous-model matrix into the instanced/MDI scene pass

**The scene shader (`m_sceneShader`) drives three passes inside `renderScene`:** opaque `renderItems` (`renderer.cpp:3149`), cloth (`:3303`), transparent (`:3389`, via `drawMesh`, blend on + depth-write off). Only the **opaque** pass has a per-instance model stream and is what the overlay covered. So R1 introduces a uniform **`u_writeMotion`**, set **true only for the opaque `renderItems` draws** and **false for the cloth and transparent passes**. When false, the fragment shader writes `coverageFlag = 0` to attachment 1 (it must still write *something* — see masking note below), so those pixels fall through to the camera-motion fallback — exactly today's behaviour (cloth → camera motion; transparent → underlying pixel's camera motion). This is the load-bearing correctness fix from the cold-eyes review: a scene-wide motion write would otherwise corrupt cloth (no prev matrix) and blend garbage into the transparent pass (blending on).

**Other passes drawing into `m_taaSceneFbo` must leave `.b = 0` (masking note).** While `m_taaSceneFbo` has two draw buffers, every pass that runs against it must not write garbage to attachment 1. The **skybox** (`m_skyboxShader.use()` at `renderer.cpp:3360` — a *different* shader, so not gated by `u_writeMotion`) and any terrain/water/particle pass that uses its own shader must either declare a location-1 `out` that writes `vec4(0)`, or have attachment 1 masked via `glColorMaski(1, …, GL_FALSE)` for the duration of those draws (a fragment shader that declares no location-1 output leaves that attachment's value *undefined* per GL spec). Simplest robust rule: **enable the attachment-1 colour mask only around the opaque `renderItems` draws; disable it for every other pass.** This subsumes the `u_writeMotion=false` cases and guarantees `.b` stays at its cleared 0 everywhere except opaque geometry.

**Prev-model stream (opaque pass only).** The overlay set `u_model`/`u_prevModel` per draw; the opaque scene pass has **three** model-matrix sources, each needing a parallel prev-model path. R1 sources prev matrices from the same `m_prevWorldMatrices` cache (keyed by `entityId`) the overlay used. Concrete touch-points (enumerated so the work isn't understated):

- `InstanceBatch` (`renderer.h:529-536`) gains a `std::vector<glm::mat4> prevModelMatrices` field, populated in lock-step with `modelMatrices` in both the **live `buildInstanceBatches` (`renderer.cpp:2771,2786,2797`)** and its **static unit-test mirror `buildInstanceBatchesStatic` (`renderer.cpp:2738,2748`)** — each entry is `m_prevWorldMatrices[entityId]` or, if absent (first frame / new spawn), the current matrix (→ zero motion, matching the overlay fallback `renderer.cpp:1005-1007`).
- **MDI path** — the current model matrices live in `IndirectBuffer::m_matrixSsbo` (bound to **binding 0**, `indirect_buffer.cpp:102`, assembled via `addCommand(entry, modelMatrices)` + `upload()`, called `renderer.cpp:3185-3188`). The prev-model parallel stream is assembled **in `IndirectBuffer`** (`indirect_buffer.{h,cpp}`) in lock-step with `addCommand`, uploaded to a **new SSBO at binding 4** (binding 0 is taken; 2/3 are bones/morph — verified `scene.vert.glsl:27,33,43`).
- **Legacy `glDrawElementsInstanced` path** — current model is per-instance vertex attributes 6–9 (`scene.vert.glsl:21-24`) fed by the shared `m_instanceBuffer` (VAO binding point 1, stride `sizeof(mat4)`, `mesh.cpp:259`). Its prev-model is a parallel per-instance attribute stream at **locations 12–15** (10–11 are bones) — which needs a **second instance VBO on a new VAO binding point** with its own divisor (not literally "the same buffer"), set up by a `setupInstanceAttributes`-style call.
- **Per-entity `drawMesh` path** — prev-model is a uniform `u_prevModel`, set exactly as the overlay did.
- `scene.vert.glsl` reads the per-instance prev-model indexed identically to the current model (SSBO binding 4 for MDI, attribs 12–15 for legacy, `u_prevModel` uniform otherwise) and computes `v_prevClip_motion = u_prevViewProjection * (prevModel * position)` from the **base** position (§4.1 #2).
- **Shadow-pass reuse.** `buildInstanceBatches` runs for shadow casters too (`renderer.cpp:2941,3485`), so the new `prevModelMatrices` field gets populated on shadow batches as well — harmless (the shadow shader never reads it), but the prev-model SSBO/attribute upload + bind happens **only on the main opaque MDI/legacy draws**, never wired into the shadow path.
- **SMAA shares `m_taaSceneFbo`.** The scene pass binds `m_taaSceneFbo` whenever `isTAA || isSMAAScene` (`renderer.cpp:2990-2994`), so with `secondColorAttachment=true` the motion attachment is allocated and written (under the §4.2 mask) on SMAA frames too. This is **benign** — SMAA never samples it, and the mask + per-attachment clear keep it well-defined — but it is marginal extra bandwidth on SMAA frames (noted in §4.5).
- **Dummy prev-model SSBO (Mesa constraint).** Once `scene.vert.glsl` *declares* `layout(std430, binding = 4)`, that declaration is live for **every** `m_sceneShader` draw — opaque, cloth, transparent, and non-TAA frames — and the engine has a documented hard constraint that *all declared SSBOs must have a valid buffer bound at draw time* on Mesa (`renderer.cpp:760-764`, the reason `m_dummyModelSSBO`/`m_dummyMorphSSBO` exist). So R1 adds a `m_dummyPrevModelSSBO` bound at frame start at binding 4 (mirroring `m_dummyModelSSBO`), and the real prev-model SSBO is bound over it only for the MDI opaque draws. Non-TAA frames keep the dummy bound and pay no upload. (The per-entity `u_prevModel` uniform path is unaffected — it is a uniform, not an SSBO.)
- **Vertex-attribute budget:** the legacy prev-model at locations 12–15 fills the VAO's attribute slots 0–15 exactly (`GL_MAX_VERTEX_ATTRIBS` guaranteed minimum is 16). It fits on the RX 6600 dev target, but the budget is now fully consumed — a future per-vertex attribute on this VAO must reclaim or pack a slot. Noted so it isn't a silent ceiling.

### 4.3 Jitter / parity guarantee (scoped)

For the **common case — opaque, non-cutout, single-layer geometry with the colour pass's own cull state — R1 reproduces the overlay's output**: the motion current term `v_currentClip_motion = m_lastViewProjection * (model * basePosition)` equals the overlay's `v_currentClip` (both use the jittered `m_lastViewProjection` and the raw base position, §0/§4.1), the prev term uses the same `m_prevViewProjection` and the same per-entity prev-model, and the fragment math is copied verbatim. This holds for **skinned/morph meshes too**, because the motion term uses the base (un-skinned) position exactly as the overlay did — the animated pose affects only `gl_Position`/shading, not the motion output. §8's parity test pins the CPU-mirrored motion math, and a launch-time visual check confirms no TAA ghosting regression.

**Deliberate, documented deviations (improvements, not regressions):**
- **Cutout / alpha-tested materials:** the scene pass applies `scene.frag.glsl`'s alpha discard (`:1148-1151`, `:1201-1204`); the overlay did not. At discarded fragments the motion attachment stays uncovered → the combine resolves them to the **camera-motion of whatever depth-writing geometry (or sky) is behind the hole**. For the common static-background case this is correct (the hole tracks the background, not the leaf); it does not synthesise object motion for a dynamic object glimpsed *through* the hole (that object's coverage was written at its own pixel) — but that matches the camera-fallback behaviour cloth/terrain already get, and is better than the overlay painting leaf motion across the hole.
- **Per-material cull state:** the overlay re-drew with GL's default cull state, ignoring per-material double-sided/cull toggles; R1 writes motion with the **same** cull state as the colour pass, so motion and colour always agree on which face is visible. Where a material's cull state differs from the overlay's default, coverage differs — and R1's is the correct one (it matches the shaded pixel).

(The earlier draft also claimed a generic "interpenetrating geometry" improvement; that is overstated — for opaque-vs-opaque interpenetration the overlay re-rasterised the same opaque set into its own depth buffer and usually picked the same winner. The genuine improvements are the cutout-discard and cull-state cases above, where motion now provably matches the shaded fragment.) These cases are why the guarantee is "parity for the common case, correctness improvement otherwise," not a blanket "byte-for-byte."

### 4.4 The combine pass — coverage flag, not magnitude sentinel

The motion buffer's final contents must be, exactly as today: **object motion** where opaque `renderItems` are visible, **camera motion** on other depth-writing geometry (cloth, terrain, water, particles), **(0,0)** on sky, and **the underlying pixel's camera motion** behind depth-write-off transparent geometry.

- Per the cold-eyes review, R1 uses an explicit **coverage flag** in the spare `.b` channel rather than a magnitude sentinel (a magnitude test like `> -1.5` is fragile — fast camera rotation near the near plane can push a legitimate UV delta past ±1). The opaque pass writes `.b = 1.0` (under the attachment-1 mask, §4.2); every other pixel must read `.b = 0`.
- **The clear is load-bearing and needs the per-attachment path from §4.0 step 5.** A plain `glClear(GL_COLOR_BUFFER_BIT)` writes the global `glClearColor` into *both* attachments, so attachment 1's `.b` would clear to the scene clear-colour's blue — and a scene with `clearColor.b ≥ 0.5` (settable via `setClearColor`, `renderer.cpp:1756-1757`) would make every uncovered pixel falsely read "covered." So attachment 1 is cleared **separately** to `(0,0,0,0)` via `glClearNamedFramebufferfv(fbo, GL_COLOR, 1, …)` each frame — independent of the scene clear colour. Coverage is then the unambiguous test `b > 0.5`.
- The retained full-screen pass (renamed conceptually to *motion combine*; same dispatch slot as the old camera pass, `renderer.cpp:953-970`) becomes:
  ```glsl
  vec4 scene = texture(u_sceneMotion, uv);          // attachment 1 of taaSceneFbo (RGBA16F)
  float depth = texture(u_depthTexture, uv).r;      // resolved scene depth (m_resolveDepthFbo)
  vec2 motion;
  if (scene.b > 0.5)        { motion = scene.rg; }              // opaque renderItem wrote object motion
  else if (depth <= 0.0001) { motion = vec2(0.0); }            // sky (reverse-Z far) — unchanged
  else                      { motion = /* camera reprojection */; } // cloth/terrain/etc. + behind transparent
  ```
  The camera-reprojection branch is the **existing** inline math from `motion_vectors.frag.glsl:16-46` (reconstruct world pos from depth via `u_currentInvViewProjection`, reproject through `u_prevViewProjection`) — there is no `cameraMotionFromDepth()` function today; that logic is carried verbatim into the combine shader. This writes into the existing `m_taa` motion FBO that TAA resolve already samples — **TAA resolve is unchanged**. The expensive overlay geometry re-draw is gone; the combine is a single full-screen quad (the camera pass already existed; it now samples one extra texture and branches on the coverage flag).

### 4.5 GPU cost

Per TAA frame, R1 **removes** one full re-rasterisation of every opaque `renderItem` (the overlay, `renderer.cpp:1000-1017`). It **adds**: (a) an RGBA16F attachment write during the opaque scene pass — that pass already rasterises the geometry, so this is extra bandwidth, not extra geometry work; (b) one per-frame prev-model SSBO upload sized to the instanced opaque set (CPU→GPU copy of N mat4s, same magnitude as the model SSBO already uploaded); (c) one extra texture sample + branch in the combine pass. Cloth/transparent passes also write attachment 1, but with `u_writeMotion = false` they write only the cleared coverage flag (no prev-matrix work). **Net: a reduction** — a whole opaque-geometry pass removed in exchange for one buffer upload and some attachment bandwidth. The 60 FPS floor is structurally safe; the fog/HUD GPU benchmarks must stay green and a frame-time spot-check at launch confirms no regression (measured, not assumed).

---

## 5. CPU / GPU placement (Rule 7)

| Work | Placement | Reason |
|------|-----------|--------|
| Motion-vector computation (per-pixel `currUV−prevUV`) | **GPU** (scene vertex+fragment, combine fragment) | Per-vertex / per-pixel, data-parallel — textbook GPU work. |
| Previous-model matrix bookkeeping (`m_prevWorldMatrices`, end-of-frame repopulate, per-instance prev stream assembly) | **CPU** | Sparse, keyed map lookups + buffer assembly; branchy, not per-pixel. Already CPU today. |
| Parity verification of the motion math | **CPU mirror** (`computeMotionVectorUV()` free function) pinned against the GLSL | Dual CPU-spec/GPU-runtime impl with a parity test, per project Rule 7. |

No new dual runtime path is introduced — R1 reuses the existing CPU prev-matrix bookkeeping and adds only a CPU mirror of the (tiny) motion math for the parity test.

---

## 6. Accessibility

R1 is parity-preserving for the common case (§4.3), so it introduces **no new motion on screen** and needs no new accessibility hook. The existing `PostProcessAccessibilitySettings` (and the `--isolate-feature=motion-overlay` diagnostic) are noted for R2, where animated-mesh motion is genuinely new. The `motion-overlay` isolation flag (`m_objectMotionOverlayEnabled` — gated at `renderer.cpp:987`, set by the diagnostic at `renderer.cpp:1945`) is **repurposed** in R1: since the overlay pass is deleted, the flag instead gates `u_writeMotion` for the opaque pass (true → scene-pass object motion; false → the combine falls back to camera-only motion everywhere), preserving the existing bisection diagnostic.

---

## 7. Test contract (R1)

`tests/test_motion_vectors_mrt.cpp` (new), GL-free where possible:

1. **`MotionUVMatchesOverlayMath`** — CPU mirror `computeMotionVectorUV(model, prevModel, vp, prevVp, objSpacePos)` reproduces the overlay's `currUV−prevUV` (incl. the 1e-6 `safeClipDivide` guard) to within FP tolerance over a battery of transforms. *Parity guard for the common case.*
2. **`StaticObjectMotionIsZeroUnderStaticCamera`** — model==prevModel, vp==prevVp ⇒ motion `(0,0)`.
3. **`StaticObjectUnderCameraMotionEqualsCameraReprojection`** — model==prevModel, vp≠prevVp ⇒ scene-pass object motion equals the camera-motion-pass reprojection for the same world point (proves the combine's two branches agree on static geometry → no seam between opaque-geometry motion and the camera fallback).
4. **`CoverageFlagSelectsBranch`** — the combine's `.b > 0.5` selector: `b=1` ⇒ `scene.rg` returned; `b=0, depth>far` ⇒ camera reprojection; `b=0, depth==far` ⇒ `(0,0)`. Pins the §4.4 coverage-flag logic (replaces the rejected magnitude-sentinel approach — a flag is robust to arbitrarily large legitimate motion under fast camera rotation).
5. **`TransparentAndClothDoNotWriteMotion`** — with the attachment-1 mask off for non-opaque passes (and `u_writeMotion=false`), attachment 1 keeps `b=0` ⇒ those pixels resolve to the camera fallback, matching today (cloth → camera motion; transparent → underlying pixel's camera motion). Guards the H1/H2/M-B coverage fix.
6. **`MissingPrevMatrixFallsBackToCurrent`** — entity absent from the prev cache ⇒ prev=current ⇒ zero motion (matches overlay fallback).
7. **`SkinnedMeshMotionUsesBasePositionMatchingOverlay`** — for a skinned/morph mesh, the CPU mirror fed the **base** (un-skinned) position reproduces the overlay's rigid-body motion exactly, and is independent of the bone/morph state. Pins the §4.1 #2 base-position rule (the behaviour R2 will deliberately change).

Plus a `Framebuffer` MRT unit test (`GL_FRAMEBUFFER_COMPLETE` with two attachments + draw-buffer enumeration, **and a clear-to-zero check on attachment 1 that holds for a non-zero scene clear colour** — guards C-A, §4.0 step 5), the existing TAA/regression suite staying green, and the fog/HUD GPU benchmarks not regressing (frame-time floor).

---

## 8. Sources (R1)

- **MRT velocity buffers / motion vectors for TAA:** the standard geometry-pass velocity-buffer approach is described in Karis, "High Quality Temporal Supersampling" (SIGGRAPH 2014 Advances in Real-Time Rendering); and Jimenez et al., "Filmic SMAA / Temporal" course notes. The engine's existing `taa_resolve.frag.glsl` already consumes a `currUV−prevUV` motion buffer — R1 only changes *which pass produces it*.
- **Reverse-Z / jitter handling:** existing engine convention (`glClipControl ZERO_TO_ONE`, Halton jitter `taa.cpp`), preserved unchanged.
- **`V_mask = α(1 − n_cur·n_prev)` disocclusion (R2, cited here for the bundle):** nVidia GDC 2024 "rain puddles" technique, per the ROADMAP bullet.

---

## 9. Cold-eyes loop log

### Loop 1 — 2026-06-19 (R1 design, fresh reviewer, no authoring context)

Reviewer verified every citation and the core logic against source. Findings and dispositions (all actionable severities fixed before Loop 2, per global Rule 14):

- **C1 (CRITICAL) — `Framebuffer` has no MRT support** (single attachment, no `glDrawBuffers`). *Verified* (`framebuffer.cpp:157-235`). **Fixed:** added §4.0 prerequisite (second-attachment support as bundle-wide infrastructure) + a slice-complexity bump (M → M/L) + a `Framebuffer` MRT unit test.
- **C2 (CRITICAL) — no RG16F path; only RGBA16F.** *Verified* (`framebuffer.cpp:163`). **Fixed:** motion now specified as RGBA16F (`.rg` motion, `.b` coverage flag); RG16F references removed throughout.
- **H1 (HIGH) — cloth & transparent are also `m_sceneShader` draws into the same FBO.** *Verified* (`renderer.cpp:3295`, `:3389`). **Fixed:** §0 table corrected; §4.2 adds the per-pass `u_writeMotion` gate (opaque-only).
- **H2 (HIGH) — transparent parity / sentinel misfire.** **Fixed:** §4.2/§4.4 disable motion-write for the transparent pass; new test #5.
- **H3 (HIGH) — parity proof ignored cutout/cull/interpenetration coverage differences.** *Valid.* **Fixed:** §4.3 reframed from "byte-for-byte" to "parity for the common case; correctness improvement for cutout/interpenetrating/cull cases," each documented.
- **H4 (HIGH) — no API to bind attachment 1 as a texture.** *Verified* (`framebuffer.cpp:95-104`). **Fixed:** §4.0 step 3 adds `bindColorTexture(unit, attachmentIndex)`.
- **M1 (MEDIUM) — magnitude sentinel fragile under fast rotation.** *Valid.* **Fixed:** replaced with explicit `.b` coverage flag (`b > 0.5`); §4.4 + test #4.
- **M2 (MEDIUM) — prev-model stream work understated.** *Valid.* **Fixed:** §4.2 enumerates the `InstanceBatch.prevModelMatrices` field, the ~5 population sites, the new SSBO binding, and the vert-shader read.
- **L1/L2/L4 (LOW citations).** **Fixed:** `scene.frag.glsl:23` (was :22); `m_objectMotionOverlayEnabled` gate `:987` / setter `:1945` (was :986); `motion_vectors_object.vert.glsl:29`.

Items the reviewer explicitly **verified correct** (kept as-is): `m_taaSceneFbo` non-MSAA (`:669`) and bound by the scene pass in TAA mode (`:2994`); the jitter/prev-VP reasoning (`:3110`, `:1490`); the prev-world cache machinery (`scene.h:60`, `motion_overlay_prev_world.h:50-77`); skinned/morph deferral to R2; camera-fallback-via-resolved-depth for terrain/water/particles.

### Loop 2 — 2026-06-19 (re-reviewed cold, no briefing on Loop-1 fixes)

The Loop-1 items did **not** resurface (the fixes held). The fresh reviewer found new, deeper defects:

- **C-A (CRITICAL) — "cleared to (0,0,0,0)" is unachievable with a single `glClear`.** *Verified:* `beginFrame` clears with one global `glClearColor` (`renderer.cpp:752,755,938`), which writes the scene clear-colour into *both* attachments; a scene with `clearColor.b ≥ 0.5` makes every uncovered pixel falsely "covered." **Fixed:** §4.0 step 5 + §4.4 now specify a per-attachment `glClearNamedFramebufferfv(fbo, GL_COLOR, 1, …)` to zero attachment 1 independent of the scene clear colour; test added (clear-to-zero under a non-zero clear colour).
- **H-A (HIGH) — "no regression for skinned meshes" was false.** *Verified:* the overlay projects the **raw `a_position`** (`motion_vectors_object.vert.glsl:25-26`), but the scene pass's `gl_Position` is skinned/morphed — so emitting motion from `gl_Position` would change the skinned current term. **Fixed:** §4.1 #2 + §4.3 + §0 now compute the motion term from the **base** (un-skinned/un-morphed) position, exactly as the overlay did → byte-identical rigid-body motion for skinned meshes; test #7 rewritten to pin this.
- **H-B (HIGH) — prev-model SSBO "alongside binding 0" impossible; MDI owner not named.** *Verified:* MDI model matrices live in `IndirectBuffer::m_matrixSsbo` at binding 0 (`indirect_buffer.cpp:102`, `renderer.cpp:3185-3188`); bones/morph occupy bindings 2/3. **Fixed:** §4.2 now assigns the prev-model SSBO **binding 4**, names `IndirectBuffer` as the assembly site, and adds the **legacy instanced** path's prev-model as vertex attributes **12–15** (current model is 6–9; bones 10–11) — all three model sources enumerated.
- **M-A (MEDIUM)** — push-site citations conflated `buildInstanceBatches` (live, `:2771,2786,2797`) with `buildInstanceBatchesStatic` (test mirror, `:2738,2748`). **Fixed:** split in §4.2.
- **M-B (MEDIUM) — non-`m_sceneShader` passes can write garbage to attachment 1.** *Verified:* skybox uses `m_skyboxShader` (`renderer.cpp:3380-3383`), not the gated scene shader; a shader that declares no location-1 `out` leaves attachment 1 undefined. **Fixed:** §4.2 masking note — enable the attachment-1 colour mask only around the opaque `renderItems` draws, disabled for every other pass (subsumes the `u_writeMotion=false` cases). Skybox dropped from the scene-shader gate list.
- **M-C (MEDIUM)** — the "interpenetrating geometry" improvement was overstated (the overlay re-rasterised the same opaque set into its own depth and usually picked the same winner). **Fixed:** §4.3 narrows the genuine improvements to the cutout-discard and cull-state cases.
- **L-A/L-B (LOW citations)** — `drawMesh()` starts `:1661` (range relabelled "skinning-bind region `:1661-1714`"); `RenderItem` struct `scene.h:38-61` (field `:60`). **Fixed.**
- **L-C (LOW)** — `cameraMotionFromDepth()` is not a real function; **Fixed:** §4.4 notes the camera reprojection is the existing inline math from `motion_vectors.frag.glsl:16-46`, carried into the combine shader.
- **§4.0 must-do (from the reviewer's "verified" list)** — the `Framebuffer` move-ctor/assignment (`framebuffer.cpp:25-56`) copies only `m_colorAttachment`; **Fixed:** §4.0 step 4 now includes the move-ctor/assignment for `m_colorAttachment1` (else leak/double-free).

Items the reviewer **verified correct** (kept): `m_taaSceneFbo` non-MSAA + bound by scene pass in TAA (`:669`,`:2994`); TAA motion FBO RGBA16F + resolve samples `.rg` (`taa.cpp:32-40`, `taa_resolve.frag.glsl:108`); jitter/prev-VP chain (`:3106-3110`,`:1490`,`:997`); cloth/transparent use `m_sceneShader` (`:3303`,`:3413-3415`); prev-world cache machinery; `m_objectMotionOverlayEnabled` `:987`/`:1945`; motion math + 1e-6 guard + sky test; `Framebuffer` single-attachment premise; the §4.0 MRT plan otherwise complete. The reviewer also flagged that the motion `out` must be written before any early `return` in `scene.frag.glsl` (e.g. wireframe `:967-968`) and that late `fragColor.rgb` edits (caustics `:1265`, cascade-debug `:1279`) only touch attachment 0 — folded into the implementation notes (write the motion output unconditionally/early).

### Loop 3 — 2026-06-19 (re-reviewed cold, no briefing on Loop-2 fixes)

Loop-1 and Loop-2 fixes all **held** (none resurfaced). Reviewer confirmed the R1 design is fundamentally sound; remaining findings:

- **HIGH-1 — binding-4 prev-model SSBO needs a dummy fallback (Mesa constraint).** *Verified:* `scene.vert.glsl`'s `layout(binding=4)` declaration is live for *every* `m_sceneShader` draw, and the engine hard-requires all declared SSBOs bound at draw time on Mesa (`renderer.cpp:760-764`, the reason `m_dummyModelSSBO`/`m_dummyMorphSSBO` exist). "Bound only in the TAA path" would leave binding 4 unbound on cloth/transparent/non-TAA draws. **Fixed:** §4.2 adds `m_dummyPrevModelSSBO` at frame start (binding 4), with the real SSBO bound over it for the MDI opaque draws only.
- **MEDIUM-1 — cutout-hole "more correct" claim overstated.** The hole resolves to the camera-motion of whatever depth-writing geometry/sky is *behind* it; it does not synthesise object motion for a dynamic object seen through the hole. **Fixed:** §4.3 cutout bullet reworded to the static-background framing (+ cited the alpha-discard sites `:1148-1151`,`:1201-1204`).
- **LOW-1 — VAO attribute budget.** Prev-model at attribs 12–15 fills slots 0–15 exactly (16-attrib minimum). **Fixed:** §4.2 notes the budget is now fully consumed.
- **LOW-2 — two off-by-a-block citations.** Overlay prev-term is `motion_vectors_object.vert.glsl:27` (range now `:25-27`); skybox shader use is `renderer.cpp:3360` (was `:3380-3383`, the cubemap-bind block). **Fixed** in §0/§4.1/§4.2.

Reviewer **verified correct** (kept): the entire §4.0 Framebuffer MRT plan incl. the move-ctor leak fix; the per-attachment clear reasoning (C-A) and `setClearColor` blue-≥-0.5 risk; base-position skinned parity (H-A); the resolve-ordering (color/depth blits read attachment 0 / depth only, `:812-816`,`:840-844`, so attachment 1 survives for the combine); all three model-matrix sources + live/static batch-build split; the attachment-1 masking rule (no bad interaction with the per-attachment clear or transparent blending); the combine math + coverage flag; the wireframe-early-return implementation note (`:967-968`) and alpha-discard sites; §5/§6/§7 internal consistency and that the tests pin their claims.

### Loop 4 — 2026-06-19 (re-reviewed cold, no briefing on Loop-3 fixes) — CONVERGED

Loop-1/2/3 fixes all **held**. Reviewer's explicit verdict: **no CRITICAL/HIGH/MEDIUM defect — R1 is ready to implement.** Remaining items were polish only:

- **LOW-1 — `u_viewProjection` notation names a uniform the scene shader lacks.** *Verified:* `scene.vert.glsl` binds separate `u_view`/`u_projection` (`:48-49`), not a combined matrix (the overlay had the combined one). **Fixed:** §4.1 #2 now writes `u_projection * u_view * (model * position)`.
- **LOW-2 — SMAA also binds `m_taaSceneFbo`** (`:2990-2994`), so the motion attachment is written (masked) on SMAA frames too — benign, marginal bandwidth. **Fixed:** noted in §4.2/§4.5.
- **LOW-3 — `buildInstanceBatches` also runs for shadow casters** (`:2941,3485`), so `prevModelMatrices` is populated there too — harmless. **Fixed:** §4.2 notes prev upload/bind happens only on the main opaque draws.
- **INFO — legacy prev-model needs a *separate* instance VBO + binding point**, not the shared `m_instanceBuffer`. **Fixed:** §4.2 clarified.

Per the session standing convergence directive (move on when a loop returns only verified, implemented polish — no structural/mechanical/architectural fixes), the loop is **converged**. R1 is signed off for implementation.
