# Phase 10 — Rendering Enhancements (Design Doc)

**Status:** ✅ **Slice R1 IMPLEMENTED (2026-06-19)** — shipped in two commits: R1.0 `Framebuffer` MRT support (+ 5 tests), then the R1 core (geometry-pass motion MRT, three prev-model paths, combine pass, overlay deleted, + 7 GL-free motion-math parity tests). Verified on the RX 6600 (Mesa 26.1.2): scene + combine shaders compile/link with the MRT outputs and 24 live TAA-mode visual-test captures render with zero GL errors; full unit suite green (3480). Design signed off after 4 cold-eyes loops (converged Loop 4, polish-only). Sequencing approved by user ("Foundation first"). **R2 IMPLEMENTED (2026-06-19, §9)** — animated skinned/morph motion + prev-normal buffer + V_mask disocclusion. **R3 IMPLEMENTED (2026-06-19, §10)** — analytic subsurface scattering (wrap + thickness translucency). **R4 (SSGI) remains** planning-fidelity (its own design-of-record + cold-eyes before implementation). See the Cold-eyes loop log at the foot of this doc.
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
| **R1** | `Framebuffer` MRT support + motion vectors via geometry-pass MRT (rigid-body parity; drop overlay) | M/L | — | ✅ implemented (2026-06-19) |
| **R2** | Correct skinned/morph motion (prev-pose history) + prev-normal buffer + `V_mask` disocclusion | L | R1 | ✅ implemented (2026-06-19) — 4 commits; 10 new tests, full suite green; α=1.0 placeholder pending Formula Workbench + launch-time grazing-angle spot-check |
| **R3** | Subsurface scattering (analytic wrap front-scatter + thickness translucency) | M | — | ✅ IMPLEMENTED (2026-06-19) — §10 |
| **R4** | Dynamic global illumination (Variant A: froxel near-field GI v1; Variant B: full Froxel Bounce `3D_E-0015` upgrade) | L | R1/R2 motion + froxel grid | ✅ design-of-record signed off (§11) — Variant A chosen; implementing |

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
2. `scene.vert.glsl` / `scene.frag.glsl` gain a motion output. **Critically, the motion terms use the raw, pre-skin/pre-morph base object-space position** (`position`), *not* the skinned/morphed position that feeds `gl_Position` — because the overlay computed motion from the raw `a_position` for both terms (`motion_vectors_object.vert.glsl:25-27`). So the vertex shader emits a dedicated `v_currentClip_motion = u_projection * u_view * (model * position)` and `v_prevClip_motion = u_prevViewProjection * (prevModel * position)` (both from the base position; note `scene.vert.glsl` binds **separate** `u_view`/`u_projection` at `:63-64`, not a combined `u_viewProjection` — the product equals the overlay's jittered `m_lastViewProjection`, and `u_prevViewProjection` is a new combined uniform set from `m_prevViewProjection`), and the fragment writes `vec4(currUV − prevUV, coverageFlag, 0)` to `out` location 1, reusing the **exact** math of `motion_vectors_object.frag.glsl` (same `safeClipDivide` 1e-6 guard). For non-skinned/non-morph meshes the base position equals the shaded position, so `v_currentClip_motion == gl_Position`; for skinned/morph meshes the base position reproduces the overlay's rigid-body motion exactly. The motion write is gated by a uniform `u_writeMotion` (§4.2). **Implementation note:** the motion output must be written **before any early `return`** in `scene.frag.glsl` (e.g. the wireframe path, `:967-968`) so attachment 1 is never left undefined for an opaque pixel; the late `fragColor.rgb` edits (water caustics `:1265`, cascade-debug `:1279`) touch only attachment 0 and need no change.
3. The per-object overlay pass (`renderer.cpp:972-1023`) and shaders `motion_vectors_object.{vert,frag}.glsl` are **deleted**.
4. The camera-motion full-screen pass is **retained but extended** into a *combine* pass: it samples the scene-pass motion attachment and prefers it where the coverage flag says opaque geometry wrote it (§4.4).

**Does not change:** the colour image (attachment 0 written exactly as today), the MSAA path (TAA-off still uses `m_msaaFbo`, untouched), the TAA resolve shader (`taa_resolve.frag.glsl` still samples one motion texture, same RGBA16F format/semantics), skinned/morph motion (still **rigid-body-only**, byte-identical to the overlay because motion uses the base position per change #2 — R2 is what adds animated-pose motion), and all non-TAA rendering.

### 4.2 Per-pass motion-write gate + previous-model matrix into the instanced/MDI scene pass

**The scene shader (`m_sceneShader`) drives three passes inside `renderScene`:** opaque `renderItems` (`renderer.cpp:3149`), cloth (`:3303`), transparent (`:3389`, via `drawMesh`, blend on + depth-write off). Only the **opaque** pass has a per-instance model stream and is what the overlay covered. So R1 introduces a uniform **`u_writeMotion`**, set **true only for the opaque `renderItems` draws** and **false for the cloth and transparent passes**. When false, the fragment shader writes `coverageFlag = 0` to attachment 1 (it must still write *something* — see masking note below), so those pixels fall through to the camera-motion fallback — exactly today's behaviour (cloth → camera motion; transparent → underlying pixel's camera motion). This is the load-bearing correctness fix from the cold-eyes review: a scene-wide motion write would otherwise corrupt cloth (no prev matrix) and blend garbage into the transparent pass (blending on).

**Other passes drawing into `m_taaSceneFbo` must leave `.b = 0` (masking note).** While `m_taaSceneFbo` has two draw buffers, every pass that runs against it must not write garbage to attachment 1. The **skybox** (`m_skyboxShader.use()` at `renderer.cpp:3360` — a *different* shader, so not gated by `u_writeMotion`) and any terrain/water/particle pass that uses its own shader must either declare a location-1 `out` that writes `vec4(0)`, or have attachment 1 masked via `glColorMaski(1, …, GL_FALSE)` for the duration of those draws (a fragment shader that declares no location-1 output leaves that attachment's value *undefined* per GL spec). Simplest robust rule: **enable the attachment-1 colour mask only around the opaque `renderItems` draws; disable it for every other pass.** This subsumes the `u_writeMotion=false` cases and guarantees `.b` stays at its cleared 0 everywhere except opaque geometry.

**Prev-model stream (opaque pass only).** The overlay set `u_model`/`u_prevModel` per draw; the opaque scene pass has **three** model-matrix sources, each needing a parallel prev-model path. R1 sources prev matrices from the same `m_prevWorldMatrices` cache (keyed by `entityId`) the overlay used. Concrete touch-points (enumerated so the work isn't understated):

- `InstanceBatch` (`renderer.h:529-536`) gains a `std::vector<glm::mat4> prevModelMatrices` field, populated in lock-step with `modelMatrices` in both the **live `buildInstanceBatches` (`renderer.cpp:2771,2786,2797`)** and its **static unit-test mirror `buildInstanceBatchesStatic` (`renderer.cpp:2738,2748`)** — each entry is `m_prevWorldMatrices[entityId]` or, if absent (first frame / new spawn), the current matrix (→ zero motion, matching the overlay fallback `renderer.cpp:1005-1007`).
- **MDI path** — the current model matrices live in `IndirectBuffer::m_matrixSsbo` (bound to **binding 0**, `indirect_buffer.cpp:102`, assembled via `addCommand(entry, modelMatrices)` + `upload()`, called `renderer.cpp:3185-3188`). The prev-model parallel stream is assembled **in `IndirectBuffer`** (`indirect_buffer.{h,cpp}`) in lock-step with `addCommand`, uploaded to a **new SSBO at binding 4** (binding 0 is taken; 2/3 are bones/morph — verified `scene.vert.glsl:47,56` for bones/morph, prev-model 4 at `:41`).
- **Legacy `glDrawElementsInstanced` path** — current model is per-instance vertex attributes 6–9 (`scene.vert.glsl:21-24`) fed by the shared `m_instanceBuffer` (VAO binding point 1, stride `sizeof(mat4)`, `mesh.cpp:259`). Its prev-model is a parallel per-instance attribute stream at **locations 12–15** (10–11 are bones) — which needs a **second instance VBO on a new VAO binding point** with its own divisor (not literally "the same buffer"), set up by a `setupInstanceAttributes`-style call.
- **Per-entity `drawMesh` path** — prev-model is a uniform `u_prevModel`, set exactly as the overlay did.
- `scene.vert.glsl` reads the per-instance prev-model indexed identically to the current model (SSBO binding 4 for MDI, attribs 12–15 for legacy, `u_prevModel` uniform otherwise) and computes `v_prevClip_motion = u_prevViewProjection * (prevModel * position)` from the **base** position (§4.1 #2).
- **Shadow-pass reuse.** `buildInstanceBatches` runs for shadow casters too (`renderer.cpp:2941,3485`), so the new `prevModelMatrices` field gets populated on shadow batches as well — harmless (the shadow shader never reads it), but the prev-model SSBO/attribute upload + bind happens **only on the main opaque MDI/legacy draws**, never wired into the shadow path.
- **SMAA shares `m_taaSceneFbo`.** The scene pass binds `m_taaSceneFbo` whenever `isTAA || isSMAAScene` (`renderer.cpp:2990-2994`), so with `secondColorAttachment=true` the motion attachment is allocated and written (under the §4.2 mask) on SMAA frames too. This is **benign** — SMAA never samples it, and the mask + per-attachment clear keep it well-defined — but it is marginal extra bandwidth on SMAA frames (noted in §4.5).
- **Dummy prev-model SSBO (Mesa constraint).** Once `scene.vert.glsl` *declares* `layout(std430, binding = 4)`, that declaration is live for **every** `m_sceneShader` draw — opaque, cloth, transparent, and non-TAA frames — and the engine has a documented hard constraint that *all declared SSBOs must have a valid buffer bound at draw time* on Mesa (`renderer.cpp:143-156` model/prev-model dummy defs + `:200-201` morph dummy, bound at `:778-782`, the reason `m_dummyModelSSBO`/`m_dummyMorphSSBO` exist). So R1 adds a `m_dummyPrevModelSSBO` bound at frame start at binding 4 (mirroring `m_dummyModelSSBO`), and the real prev-model SSBO is bound over it only for the MDI opaque draws. Non-TAA frames keep the dummy bound and pay no upload. (The per-entity `u_prevModel` uniform path is unaffected — it is a uniform, not an SSBO.)
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

## 9. Slice R2 — Animated motion vectors + previous-frame normal buffer + `V_mask` disocclusion (design-of-record)

**Status:** ✅ **IMPLEMENTED (2026-06-19)** — shipped in four commits: (1) `Framebuffer` third-attachment MRT infra, (2) `SkeletonAnimator` previous-pose history, (3) end-to-end wiring (scene shaders prev-skinning + normal write, renderer prev-pose plumbing, the skinned-batch routing fix, resolve `V_mask` + normal blit), (4) the §9.10 test contract (10 new tests). Full motion/TAA/skeleton/instance/framebuffer suite green. `u_disocclusionAlpha` ships as the conservative `α = 1.0` placeholder (Formula Workbench TODO); the launch-time grazing-angle / ghosting spot-check (§9.7, §9.10 #10) gates the α=1.0 ship-or-block decision and the prev-normal blit's frame-time budget. Design-of-record signed off after cold-eyes converged in 9 loops (sign-off delegated to Claude per the owner's standing directive). See the Cold-eyes loop log at the foot of this doc (§10, R2 loops).

### 9.0 What exists today that R2 builds on (reality check, verified against source)

| Fact | Source | Consequence for R2 |
|------|--------|--------------------|
| **Skinning is a per-entity `drawMesh` path only** — bone matrices upload to SSBO binding 2 and bind inside `drawMesh` (`renderer.cpp:1651-1662`), morph SSBO to binding 3 + per-target weights as `u_morphWeights[i]` uniforms (`:1664-1688`). The MDI/instanced opaque paths carry **no** bone/morph stream. | `renderer.cpp:1630-1688`, `scene.vert.glsl:46-59` | R2's prev-pose plumbing only touches the **per-entity** path — **no** MDI/instanced prev-bone stream, **no** new vertex attributes (prev skinning reuses the same `boneIds`/`boneWeights` at locations 10–11). This is far smaller than R1's three-source model plumbing. |
| **No previous-frame pose is retained anywhere.** `SkeletonAnimator` holds only the current `m_boneMatrices` (`skeleton_animator.h:191`) and current `m_morphWeights` (`:204`); each `update(deltaTime)` (`:42`) recomputes from scratch. | `skeleton_animator.h:155-214` (no previous *pose* snapshot — the existing `m_prevRootPos`/`m_prevRootRot` at `:211-212` are root-motion state, unrelated) | R2 adds prev-pose snapshots **in `SkeletonAnimator`** (one frame of lag), the single new piece of history machinery. |
| **R1's motion uses the raw base position for both terms** (`scene.vert.glsl:222-230`), so skinned/morph meshes emit **rigid-body motion only** — a limb that swings while the root stays put produces zero motion → TAA smears it. | `scene.vert.glsl:228-230` | R2 replaces the base-position prev term with an **animated-pose** prev term (skin with previous bones/weights). The R1 current term already equals the skinned `gl_Position` for the common case once we stop forcing base position. |
| **`scene.frag.glsl` is a 2-attachment MRT** (`fragColor` loc 0 `:27`, `motionOut` loc 1 `:32`); the interpolated **geometric** world normal `v_normal` is available (`:17`, written `scene.vert.glsl:208` as `normalMatrix * skinnedNormal`). No normal G-buffer exists. | `scene.frag.glsl:17,27,32`, `scene.vert.glsl:208` | R2 adds a **third** attachment for the geometric world normal — written from `v_normal` (not the normal-mapped shading normal, which would add high-freq false disocclusion). |
| **TAA resolve already modulates feedback** by off-screen test, motion length, and clip distance (`taa_resolve.frag.glsl:120-135`) but has **no** depth/normal disocclusion test. History is sampled at `historyUV = v_texCoord − motion` (`:108-109`). | `taa_resolve.frag.glsl:108-135` | `V_mask` plugs into exactly this feedback-modulation block — one more multiplicative confidence term, no restructuring. |
| **`m_taaSceneFbo` attachments survive to resolve time** — the resolve blits (`renderer.cpp:830-834` colour, `:860-863` depth) read `m_taaSceneFbo` with `GL_COLOR_BUFFER_BIT` (which copies only the read buffer = attachment 0) / `GL_DEPTH_BUFFER_BIT`, so att1 (motion) survives for the combine pass (asserted in the comment at `:986`). | `renderer.cpp:985-988` | R2's att2 (current normal) likewise survives → the resolve can sample it directly as "current normal", no copy. |
| **TAA already ping-pongs colour** via `m_currentFbo`/`m_historyFbo` + `swapBuffers()` (`taa.h:54-64,76-78`). | `taa.h:54-78` | R2's prev-normal buffer mirrors this idiom (§9.4). |
| **The Mesa all-declared-SSBOs-must-be-bound constraint** forced R1's `m_dummyPrevModelSSBO` at binding 4. | `renderer.cpp:143-156` (dummy defs + Mesa rationale), `:778-782` (frame-start binds) | R2's new prev-bone SSBO needs the same dummy fallback. **Binding 7** is the slot: binding 5 is declared by the particle sort-keys compute + particle GPU vertex shaders and cloth dihedral (`particle_sort.comp.glsl:44`, `particle_gpu.vert.glsl:32`, `cloth_dihedral.comp.glsl:36`), and binding 6 by cloth normals (`cloth_normals.comp.glsl:23`) — all **outside the scene shader**, and the scene-shader frame-start bind block touches only 0/2/3/4 (`renderer.cpp:778-782`) — so 5/6 are not *reserved* for the scene shader, but to avoid relying on bind-point hygiene across pass boundaries R2 takes binding 7, which **no shader declares**. |

### 9.1 What changes (and what does not)

**Changes:**
1. **`SkeletonAnimator` retains a one-frame-lagged pose** — `m_prevBoneMatrices` + `m_prevMorphWeights`, snapshotted each `update()` (§9.2).
2. **The scene vertex shader computes a second, previous-frame skinned position** for the motion prev term, from the previous bones/weights (§9.3). The current motion term becomes the actual skinned `gl_Position` (no longer forced to base position).
3. **A third MRT attachment** on `m_taaSceneFbo` holds the geometric world normal of opaque `renderItems` (§9.4).
4. **A previous-frame normal buffer** is retained (§9.4) and **TAA resolve gains the `V_mask` disocclusion term** (§9.5).

**Does not change:** the colour image (attachment 0 untouched); the MSAA / non-TAA paths; R1's three model-matrix prev paths and the combine pass (camera-motion fallback for cloth/terrain/water/particles is unchanged — those still get no normal and no `V_mask`, §9.4/§9.5); rigid (non-skinned, non-morph) meshes' motion (their base position *is* their shaded position, so the animated-pose term is identical to R1's); the SSBO bindings 0/2/3/4 and vertex attributes 0–15 (R2 adds **zero** vertex attributes — prev skinning reuses `boneIds`/`boneWeights`).

### 9.2 Previous-pose history in `SkeletonAnimator` (CPU)

Two new members mirroring the current ones: `std::vector<glm::mat4> m_prevBoneMatrices;` and `std::vector<float> m_prevMorphWeights;`, plus a `bool m_prevPoseValid = false;`. New const accessors `getPrevBoneMatrices()` / `getPrevMorphWeights()` mirror `getBoneMatrices()` (`skeleton_animator.h:136`) / `getMorphWeights()` (`:145`).

**Snapshot timing (capture-before-recompute):** `update()` has an **early-return guard** at `skeleton_animator.cpp:149-155` (`if (!m_playing || m_paused || m_activeClipIndex < 0 || !m_skeleton) return;`) that runs **before** `computeBoneMatrices()` (`:213`). The snapshot must sit **above** that guard so it runs every frame, copying the still-current-from-last-frame pose into the prev buffers before this frame's pose is (re)computed:

```cpp
void SkeletonAnimator::update(float deltaTime)
{
    // Snapshot last frame's pose as "previous" — ABOVE the early-return guard,
    // so it runs even on the paused/stopped path.
    if (m_prevPoseValid)
    {
        m_prevBoneMatrices = m_boneMatrices;   // last frame's uploaded pose
        m_prevMorphWeights = m_morphWeights;
    }

    if (!m_playing || m_paused || m_activeClipIndex < 0 || !m_skeleton)
    {
        // ... existing root-motion-delta clear ...
        if (!m_prevPoseValid) { /* seed prev = current; set m_prevPoseValid = true */ }
        return;                 // pose frozen ⇒ prev == current ⇒ zero pose motion
    }

    // ... existing advance + computeBoneMatrices() → fills m_boneMatrices / m_morphWeights ...

    if (!m_prevPoseValid)        // first update with a real pose: seed prev = current → zero motion frame 1
    {
        m_prevBoneMatrices = m_boneMatrices;
        m_prevMorphWeights = m_morphWeights;
        m_prevPoseValid = true;
    }
}
```

This gives prev = exactly the pose rendered last frame, so the motion term is a true one-frame delta. **Paused / stopped** (early-return path): the snapshot still runs but the pose is frozen, so prev == current ⇒ **zero pose motion** — correct (a paused character the camera orbits still gets camera-reprojection motion via the combine fallback and rigid motion via `prevModel`; only the pose delta is zero). **First frame:** prev == current ⇒ zero motion (matches R1's missing-prev fallback philosophy). The seed is safe because `m_boneMatrices` is bind-pose-filled by `initializeBuffers()` (`skeleton_animator.cpp:264,272`) when the skeleton is set (`:370`); with no skeleton the early-return seeds an empty vector, which is benign (the mesh then renders unskinned, `u_hasBones=false`). **Crossfade** (`crossfadeTo`, `skeleton_animator.h:107`) needs no special handling — `m_boneMatrices` already holds the blended result, so snapshotting it captures the blend. **Root motion** in `APPLY_TO_TRANSFORM` mode moves the entity transform (→ carried by `prevModel`, already handled by R1); the bone palette's zeroed root is captured consistently in prev.

**Verified (Rule 13) — animator updates are not render-culling-gated.** `Scene::update` (`scene.cpp:49-56`) calls `m_root->update(deltaTime)`; `Entity::update(float deltaTime, const glm::mat4& parentWorldMatrix)` (`entity.cpp:31-53`) returns early only on `!m_isActive` (`:33`), otherwise updates each component guarded by `if (component->isEnabled())` (`:42-48`), then recurses to **all** children (`:51-54`). Visibility / frustum culling (`m_isVisible`, a separate render-time flag) is **not** checked here. So every *active* entity's *enabled* `SkeletonAnimator` advances each frame regardless of whether it is on-screen, and prev is exactly one frame old when the entity becomes visible. The two preconditions are self-consistent: if the entity is inactive **or** the animator component is disabled, `update()` does not run — neither the snapshot nor the recompute fires, the pose stays frozen, and prev == current ⇒ zero pose motion (correct — a non-advancing pose has no motion).

### 9.3 Animated motion in the scene vertex shader (GPU)

`scene.vert.glsl` already computes `skinnedPos` (`:187`) and `worldPosition = model * vec4(skinnedPos,1)` (`:201`). R2:

1. **Current motion term = the skinned clip position:** replace the base-position current term (`:229`) with `v_currentClip_motion = u_projection * u_view * worldPosition` (== `gl_Position`). For non-skinned meshes this is byte-identical to R1 (base == skinned).
2. **Previous motion term = previous-frame skinned position.** Add a parallel previous skinning block reusing the **same** `boneIds`/`boneWeights` (locations 10–11) and the same morph deltas (binding 3), but indexing the **previous** bone palette and previous weights:
   - New SSBO `layout(std430, binding = 7) buffer PrevBoneMatrices { mat4 u_prevBoneMatrices[]; };` (binding 7 — no shader declares it; bindings 5/6 are compute-pass-only, see §9.0)
   - New uniform `uniform float u_prevMorphWeights[MAX_MORPH_TARGETS];`
   - Prev block (positions only — motion needs no normals, so the morph **normal**-delta sum is skipped, making it cheaper than the forward block). It must **mirror the forward block's structure exactly** — same `if (w != 0.0)` weight guard and the same `loopCount = min(u_morphTargetCount, MAX_MORPH_TARGETS)` bound — differing only in reading `u_prevMorphWeights[i]` / `u_prevBoneMatrices[…]`. Since the forward block declares `vid`/`vc`/`loopCount` *inside* its `if (u_morphTargetCount > 0)` scope (`scene.vert.glsl:151-159`), **hoist those three declarations above both blocks** so the prev block indexes `u_morphDeltas` identically (else the snippet below references out-of-scope names):
     ```glsl
     // (vid, vc, loopCount hoisted above the forward morph block and reused here)
     vec3 prevMorphedPos = position;
     if (u_morphTargetCount > 0) {
         for (int i = 0; i < loopCount; i++) {
             float pw = u_prevMorphWeights[i];
             if (pw != 0.0)
                 prevMorphedPos += pw * u_morphDeltas[i * vc + vid].xyz;   // position delta only
         }
     }
     vec3 prevSkinnedPos = prevMorphedPos;
     if (u_hasBones) {
         mat4 pbt = boneWeights.x * u_prevBoneMatrices[boneIds.x]
                  + boneWeights.y * u_prevBoneMatrices[boneIds.y]
                  + boneWeights.z * u_prevBoneMatrices[boneIds.z]
                  + boneWeights.w * u_prevBoneMatrices[boneIds.w];
         prevSkinnedPos = vec3(pbt * vec4(prevMorphedPos, 1.0));
     }
     v_prevClip_motion = u_prevViewProjection * (prevModel * vec4(prevSkinnedPos, 1.0));
     ```
     The CPU parity mirror (§9.10 #5) must follow the same guarded structure so GLSL and mirror agree to FP tolerance.
   The fragment shader is **unchanged** — it still perspective-divides the two clip positions to `currUV − prevUV` (`scene.frag.glsl:42-44` `safeClipDivide`).
3. **CPU side — prev-pose plumbing (enumerated, mirroring R1's §4.2 touch-point discipline):**
   - **`RenderItem`** (`scene.h:47-48`, which today carries the borrowed `boneMatrices`/`morphWeights` pointers) gains parallel `prevBoneMatrices` / `prevMorphWeights` pointer fields, assigned at the producer site `scene.cpp:376,380` (where `item.boneMatrices = &animator->getBoneMatrices()` / `item.morphWeights = &animator->getMorphWeights()`) from the new `animator->getPrevBoneMatrices()` / `getPrevMorphWeights()`.
   - **`InstanceBatch`** (`renderer.h:530-538`) already carries `boneMatrixPtrs`/`morphWeightPtrs` parallel to `modelMatrices` (`:536-537`) — the actual carrier the draw reads from. It gains parallel `prevBoneMatrixPtrs` / `prevMorphWeightPtrs`, pushed in lock-step at the same batch-build `push_back` sites as the existing pointers (`renderer.cpp:2715-2716,2726-2727,2757-2758,2775&2777` — the latter interleaved with `.clear()` at `:2774,2776` in the batch-reuse path — and `:2786-2787`), exactly as R1 added `prevModelMatrices` (`:535`).
   - **`drawMesh`** (`renderer.h:92-97`, `renderer.cpp:1630-1635`) gains two trailing defaulted params **after** the existing R1 `prevModelMatrix` (`renderer.h:97`): `const std::vector<glm::mat4>* prevBoneMatrices = nullptr`, `const float* prevMorphWeights = nullptr` — default `nullptr`, resolved in-body to the current bone/morph pointers (mirroring how `prevModelMatrix` falls back to `modelMatrix` at `:1646`), so an absent prev ⇒ zero motion. When skinned it uploads `prevBoneMatrices` to a new `m_prevBoneMatrixSSBO` (binding 7) alongside the existing bone upload (`:1660-1661`) and sets `u_prevMorphWeights[i]` alongside `u_morphWeights[i]` (`:1682-1686`).
   - **Call-site:** a skinned mesh that stays a **single-instance batch** is drawn through the non-instanced `drawMesh` loop at `renderer.cpp:3299` (the MDI path forces `u_hasBones=false` — `:3170` *"MDI path doesn't support skinning"*), and that loop runs **before** the motion attachment is masked off at `:3306-3311`. So `:3299` is the site that passes the new `batch.prevBoneMatrixPtrs[mi]` / `prevMorphWeightPtrs[mi]` (subject to the routing requirement below). Every other posed draw is outside the motion write and needs nothing: the **transparent** `drawMesh` (`:3442`) runs *after* the `:3306-3311` mask, so its motion output is discarded regardless (prev would default to current anyway — harmless); the **cloth** pass (`:3315-3343`) uses raw `glDrawElements` with `u_hasBones=false` (never `drawMesh`, never skinned); the **shadow / id-buffer / outline** passes (`:3928`, `:4112`, …) use different shaders with no location-1 output.
   - **Routing requirement (load-bearing — verified batcher gap):** `buildInstanceBatches` keys batches on `(item.mesh, item.material)` only (`renderer.cpp:2751`), with `MIN_INSTANCE_BATCH_SIZE = 2` (`renderer.h:549`) — **skinned-ness is not in the key**. So ≥ 2 skinned entities sharing one mesh+material pointer (a crowd of identical characters) batch together, cross the threshold, and take the **instanced** path (`:3247`), which forces `u_hasBones=false` (`:3253` *"Instancing doesn't support skinning"*) → they render at **bind pose** and never reach the `:3299` skinning draw. Under R1 this was invisible (motion was rigid regardless), but it breaks R2's premise — a batched-away skinned mesh gets neither animated shading nor animated motion — and is a latent R1-era correctness bug in its own right. **R2 must exclude skinned/morphed items from instance grouping:** make `item.boneMatrices != nullptr || item.morphWeights != nullptr` force a unique batch key — e.g. widen the `m_batchIndexMap` key from `(mesh, material)` (`:2751`) to `(mesh, material, isSkinnedOrMorphed)`, or simply skip the `m_batchIndexMap` lookup/insert for skinned/morphed items so each lands in its own batch slot — so every skinned/morphed mesh forms a single-instance batch routed through `:3299` with `u_hasBones=true`. This simultaneously repairs the latent bind-pose bug for shared-mesh skinned crowds — which means it changes **shaded** output there (the crowd now animates visibly, not merely its motion vectors), so the visual-regression baseline for any shared-mesh skinned scene must be re-captured, not only the motion-parity test #9. Pinned by §9.10 test #9 (routing) + a visual re-baseline.
4. **Mesa dummy (binding 7):** add `m_dummyPrevBoneSSBO` created alongside the other dummies (`renderer.cpp:143-156` model/prev-model, `:200-201` morph) and bound at frame start in the existing dummy-bind block (`:778-782`, which already binds 0/2/3/4), since `scene.vert.glsl`'s `binding = 7` declaration is live for **every** `m_sceneShader` draw (opaque/cloth/transparent/non-TAA). The real prev-bone SSBO is bound over it only for skinned `drawMesh` calls.

**Cost:** skinned vertices do the bone-blend matrix sum twice (current + prev). Vertex-stage ALU, modest skinned vertex counts → negligible vs the fragment stage. The morph prev sum skips normal deltas (half the work of the forward morph block).

### 9.4 Previous-frame normal buffer

**Prerequisite — extend `Framebuffer` to a third attachment (R2's first commit, the analogue of R1's §4.0).** `Framebuffer` today supports exactly two colour attachments: a `secondColorAttachment` flag + `m_colorAttachment1` (`framebuffer.h:23,105`), `bindColorTexture(unit, attachmentIndex)` (already index-parameterised, `:62`), and `clearSecondAttachment()` (`:68`). R2 generalises to a third by **applying §4.0's steps 1–5 at index 2** — the only deltas: a `thirdColorAttachment` flag + `m_colorAttachment2` at `GL_COLOR_ATTACHMENT2`, the `glNamedFramebufferDrawBuffers` list widened to three, `bindColorTexture(unit, 2)`, a per-attachment clear for index 2 (mirroring `clearSecondAttachment`), and `m_colorAttachment2` carried through the move-ctor / move-assignment / `resize()` / `cleanup()` (else leak/double-free). Single- and two-attachment FBOs stay byte-for-byte unchanged.

**Encoding & attachment.** A third attachment (`GL_COLOR_ATTACHMENT2`, RGBA16F) on `m_taaSceneFbo`. `scene.frag.glsl` adds `layout(location = 2) out vec4 normalOut;` and writes `normalOut = vec4(normalize(v_normal), 1.0)` — the **geometric** interpolated world normal, stored signed (no `*0.5+0.5` remap needed → the §9.5 dot product is direct). RGBA16F half-float is **not** an exact round-trip for [-1,1] (~10–11 mantissa bits), but that is far finer than the disocclusion confidence term needs. Full `xyz` in `.rgb` is chosen over octahedral-in-`.rg` for legibility (six-month test); octahedral packing is noted as a future bandwidth micro-opt if profiling ever demands it. `.a` is **unused in R2** — written as `1.0` but not read anywhere; do not build to a value. (A later slice such as R4 SSGI *could* repurpose it for linear depth / roughness — future work, not part of the R2 contract.)

**Per-pass gating — identical rule to R1's attachment 1.** The normal is written **only** for opaque `renderItems` (the geometry the disocclusion signal is meaningful for). The R1 attachment-1 colour mask (`glColorMaski(1, …)` enabled only around the opaque draws) is **extended to also cover attachment 2** — both are masked off for cloth/transparent/skybox/terrain/water. Those passes leave attachment 2 at its cleared value. The §4.0-step-5 per-attachment clear is extended: `glClearNamedFramebufferfv(fbo, GL_COLOR, 2, vec4(0,0,0,0))` each frame, so an uncovered pixel reads a **zero-length** normal — the §9.5 sentinel that disables `V_mask` there (those pixels keep R1/today's motion-only feedback). This means **no normal/`V_mask` for cloth/terrain/water** — consistent with their camera-motion fallback; a deliberate scope boundary, not a regression.

**Previous-frame retention — blit, not ping-pong (simpler path, Rule 9).** The resolve needs last frame's normal sampled at `historyUV`. Decision: a dedicated persistent single-attachment `m_prevNormalFbo` (RGBA16F), and **after** the resolve pass, one `glBlitNamedFramebuffer` copies `m_taaSceneFbo` attachment 2 → `m_prevNormalFbo` (so it holds this frame's normal as "prev" for next frame). At 1080p RGBA16F this blit is ≈ 0.05–0.08 ms (≈ 0.5 % of the 16.6 ms budget) — a bandwidth estimate, to be confirmed by the launch-time frame-time spot-check (§9.7). A texture-swap ping-pong (re-attaching attachment 2 each frame, zero-copy) was considered and **rejected for R2**: it needs a `Framebuffer` attachment-mutation API and muddies attachment ownership, for a sub-0.1 ms saving. Noted as a deferred micro-opt and the explicit contingency: if the §9.7 frame-time spot-check shows the blit exceeds its estimated budget, switch to the zero-copy ping-pong before sign-off.

**Ordering within the frame:** scene pass writes att2 (current normal) → combine → resolve reads att2 (current) + `m_prevNormalFbo` (last frame) → blit att2 → `m_prevNormalFbo`. The blit lands in the same end-of-frame slot as the existing TAA `swapBuffers()`.

### 9.5 `V_mask` disocclusion in TAA resolve (GPU)

`taa_resolve.frag.glsl` gains two samplers — `u_currentNormal` (m_taaSceneFbo att2) and `u_prevNormal` (m_prevNormalFbo) — bound by the resolve pass at the next free texture units (units 3 and 4; current/history/motion occupy 0/1/2, `renderer.cpp:1000-1005`). These unit numbers are scoped to the **resolve** pass (`:994-1014`); the separate combine pass (`:983-988`, shader `m_motionVectorShader`) reuses units 0/1 on its own shader and is unaffected. The combine pass (§4.4) gains **no** new samplers — `u_currentNormal`/`u_prevNormal` bind only in the resolve pass.

```glsl
vec3 nCur  = texture(u_currentNormal, v_texCoord).xyz;
vec3 nPrev = texture(u_prevNormal,    historyUV).xyz;     // same reprojection as colour history

float vMask = 0.0;
// Only where opaque geometry wrote a real normal this frame (cleared = zero-length sentinel).
if (dot(nCur, nCur) > 0.01)
{
    float ndot = clamp(dot(normalize(nCur), normalize(nPrev)), 0.0, 1.0);
    vMask = clamp(u_disocclusionAlpha * (1.0 - ndot), 0.0, 1.0);   // V_mask = α(1 − n_cur·n_prev)
}
feedback *= (1.0 - vMask);   // disocclusion / strong rotation ⇒ reject history ⇒ no smear
```

This is one more multiplicative confidence term layered onto the existing motion-length and clip-distance reductions (`:129-135`) — when normals agree (`ndot≈1`) `V_mask≈0` and behaviour is unchanged; at a disoccluded edge (a swinging limb revealing background, or a surface rotating in place — the case motion vectors alone can't flag) the normals diverge and history is rejected. Where no normal was written (cloth/terrain/sky/transparent, `nCur` zero-length) `V_mask=0` ⇒ today's behaviour exactly.

**`u_disocclusionAlpha` (α) and the response curve are a Formula Workbench item (Rule 6).** `α` trades ghosting suppression against temporal stability (too high → flicker/loss of AA at grazing normals; too low → residual smear). It is **not** hand-tuned: authored/fit in `tools/formula_workbench/` against captured disocclusion sequences and exported as the uniform default. Until reference captures exist, a conservative placeholder `α = 1.0` ships with a `TODO: revisit u_disocclusionAlpha via Formula Workbench` comment beside the uniform. The capture set (a few short disocclusion sequences — a limb sweeping across background, a head turning in place) is produced by the R2 implementer as the Workbench input. **Ship-or-block gate:** the `α = 1.0` placeholder may ship only if the fog/HUD benchmark and the ghosting/grazing-angle spot-check (§9.7, §9.10 #10) both stay green; if α = 1.0 over-rejects (the "too high → flicker / loss of AA at grazing normals" failure mode), R2 blocks on the Workbench fit before sign-off. A linear `(1 − ndot)` is the v1 curve; the Workbench may replace it with a smoothstep knee if the linear ramp over-rejects at shallow angles.

### 9.6 Behaviour change vs R1 — the base-position parity test is deliberately replaced

R1's test #7 `SkinnedMeshMotionUsesBasePositionMatchingOverlay` (R1 §7) pinned the *interim* base-position rule. **R2 deliberately changes that behaviour** (skinned motion now uses the animated pose), so that test is **rewritten**, not kept — its replacement (§9.10 test #3) asserts the new animated-pose semantics and that the result reduces to R1's base-position value **only** when the pose is static. This is the one place R2 is *not* parity-preserving, and it is the intended fix (correct character motion under TAA). All other R1 guarantees (rigid bodies, camera fallback, coverage flag, sky) are untouched.

### 9.7 GPU cost & 60 FPS floor

Per TAA frame R2 **adds**: (a) a second bone-blend per skinned vertex + a positions-only morph sum (vertex ALU, negligible); (b) one RGBA16F attachment write in the opaque scene pass (bandwidth, no extra geometry); (c) one prev-bone SSBO upload per skinned `drawMesh` (≤ `MAX_BONES`·64 B = 8 KB per skinned mesh, same magnitude as the existing bone upload); (d) the end-of-frame normal blit (≈ 0.05–0.08 ms); (e) two texture samples + a normalized dot in the resolve (cheap). Skinned characters are a small fraction of a Tabernacle-walkthrough scene, so the dominant adds are the attachment write + the blit — together well under the budget. **60 FPS floor:** structurally safe; pinned by the fog/HUD GPU benchmarks staying green and a launch-time frame-time spot-check in a skinned-character scene (measured, not assumed). The resolve must stay correct under all four AA modes — R2's new resolve uniforms only bind in the TAA resolve pass (`renderer.cpp:994-1014`); SMAA/MSAA/none are untouched.

### 9.8 CPU / GPU placement (Rule 7)

| Work | Placement | Reason |
|------|-----------|--------|
| Prev-pose snapshot (`m_prevBoneMatrices`/`m_prevMorphWeights`) | **CPU** (`SkeletonAnimator::update`) | Per-entity, branchy bookkeeping; one `std::vector` copy/frame/animator. Already where the current pose lives. |
| Second (previous) skinning for the motion term | **GPU** (scene vertex shader) | Per-vertex, data-parallel — same class of work as the forward skinning beside it. |
| Geometric-normal write | **GPU** (scene fragment shader) | Per-pixel; the normal is already in hand from `v_normal`. |
| `V_mask` compute + feedback modulation | **GPU** (resolve fragment) | Per-pixel, data-parallel. |
| `α` disocclusion coefficient + response curve | **Formula Workbench (design-time, CPU)** → uniform | Numerical-design constant; Rule 6 forbids hand-coding it. |
| Prev-skinning motion math parity | **CPU mirror** pinned against the GLSL (extends `motion_vector_math.h`) | Dual CPU-spec/GPU-runtime with a parity test, per Rule 7. |

No new dual *runtime* path — only the parity-test CPU mirror is added (a previous-pose variant of R1's `computeMotionVectorUV`).

### 9.9 Accessibility

R2 makes animated-character motion **correct** rather than zero, which is a net **reduction** in on-screen smear (R1 left swinging limbs ghosting under TAA) — an improvement for motion-sensitive users, not new flashing. `V_mask` further reduces disocclusion smear. The R1-repurposed `--isolate-feature=motion-overlay` flag (`m_objectMotionOverlayEnabled`, gating `u_writeMotion`) still bisects the whole object-motion path including R2's animated term. No new accessibility hook required; the existing `PostProcessAccessibilitySettings` motion controls continue to govern any motion-blur consumer of the buffer.

### 9.10 Test contract (R2)

GL-free where possible (CPU mirror in `tests/test_motion_vectors_mrt.cpp` / a new `test_skeleton_prev_pose.cpp`):

1. **`PrevPoseSnapshotLagsByOneFrame`** — drive `SkeletonAnimator::update` twice with distinct poses; `getPrevBoneMatrices()`/`getPrevMorphWeights()` equal the **first** update's pose (true one-frame lag).
2. **`FirstFramePrevPoseEqualsCurrent`** — after a single `update`, prev == current ⇒ the motion mirror yields `(0,0)`.
3. **`SkinnedMotionUsesAnimatedPose`** (replaces R1 #7) — CPU mirror: a vertex skinned with differing prev/current bone palettes yields motion = `proj(currentSkinned) − proj(prevSkinned)`, non-zero; and reduces **exactly** to R1's base-position value when the two palettes are equal (static pose). Pins the §9.6 behaviour change.
4. **`StaticPoseStaticCameraZeroMotion`** — equal palettes, `model==prevModel`, `vp==prevVp` ⇒ `(0,0)`.
5. **`MorphMotionUsesPrevWeights`** — morph-only vertex, `prevWeights ≠ weights` ⇒ motion equals the projected delta-driven displacement; independent of bone state.
6. **`NormalBufferHoldsGeometricWorldNormal`** (GL) — att2 stores `normalize(world geometric normal)` for an opaque pixel and `(0,0,0,0)` for an uncovered pixel under a non-zero scene clear colour (extends the R1 C-A clear-to-zero guard to attachment 2).
7. **`VMaskZeroWhenNormalsAgreeFullWhenOpposed`** — CPU mirror of `α(1−n·n')`: `ndot=1 ⇒ 0`; `ndot=0 ⇒ clamp(α,0,1)`; monotonic in `(1−ndot)`; `feedback*(1−vMask)` monotonically non-increasing.
8. **`VMaskDisabledWhereNoNormal`** — zero-length `nCur` sentinel ⇒ `vMask=0` ⇒ cloth/terrain/sky keep R1 feedback unchanged.
9. **`SkinnedMeshNeverInstanceBatched`** — two render items with the same mesh+material pointer but non-null `boneMatrices` (or `morphWeights`) produce **two** single-instance batches, not one instanced batch ⇒ each routes through the `:3299` skinning draw with `u_hasBones=true`. Pins the §9.3 routing requirement (and guards the latent shared-mesh bind-pose bug).
10. **`GrazingNormalKeepsAntiAliasing`** (visual/metric spot-check) — at the shipped `α`, a surface seen at a grazing angle (low `n·n_prev` from view-dependent interpolation, **not** true disocclusion) does not lose TAA convergence. Guards the §9.5 α-too-high failure mode and gates the `α = 1.0` ship-or-block decision.

Plus: the `Framebuffer` MRT test extended to **three** attachments (completeness + draw-buffer enumeration + att2 clear-to-zero); the existing TAA/regression suite staying green; the fog/HUD GPU benchmarks not regressing.

### 9.11 Sources (R2)

- **Geometry-pass velocity for skinned meshes (re-skinning the previous pose):** the standard "previous bone matrices" approach — e.g. the velocity-buffer treatment in Karis, "High Quality Temporal Supersampling" (SIGGRAPH 2014) and the *Frostbite*/*Unreal* skinned-velocity convention (skin the vertex twice, with last-frame and this-frame palettes). R2 applies it within the engine's existing single-pass MRT.
- **`V_mask = α(1 − n_cur·n_prev)` normal-based disocclusion:** nVidia GDC 2024 "rain puddles" technique, per the ROADMAP bullet. Normal-divergence catches in-place rotation and tangential disocclusion that a depth-only test misses (depth-based disocclusion was considered as the cheaper alternative but also needs a prev-buffer and flags fewer cases).
- **Reverse-Z / Halton jitter / prev-VP chain:** unchanged engine convention (R1 §8), reused verbatim.

---

## 10. Slice R3 — Subsurface scattering (analytic wrap front-scatter + thickness translucency) (design-of-record)

**Status:** ✅ **IMPLEMENTED (2026-06-19)** — material fields + serialization, the two GLSL helpers folded into all three `calc*PBR` functions, CPU mirror `subsurface_math.h`, and renderer uniform plumbing. 14 tests (11 GL-free + 3 GL parity); full suite 3512/3512 green; shader compiles on the RX 6600 (Mesa). Perf benchmark (#8) deferred per Rule 5 (no isolated pass to time — §10.8). Design cold-eyes converged after 4 loops (§11 R3 log); sign-off delegated per the project's spec-sign-off convention.

### 10.0 What exists today that R3 builds on (reality check, verified against source)

- The engine is **forward-rendered** with a hybrid shader: a Cook-Torrance **PBR path** (`scene.frag.glsl` `calcDirectionalLightPBR`/`calcPointLightPBR`/`calcSpotLightPBR`) and a legacy **Blinn-Phong path** (`calcDirectionalLight`/`calcPointLight`/`calcSpotLight`). Per-light diffuse uses a hard terminator clamp `NdotL = max(dot(N, L), 0.0)` (PBR: the three `calc*PBR` functions; BP: the three `calc*` functions). The scene shader's lighting paths have **no** SSS, translucency, thickness, or wrap term (grep: two comments in `engine/core/engine.cpp` noting "true SSS is planned for Phase 9" — both stale, now Phase 10). The one pre-existing translucency term in the codebase is an unrelated ad-hoc grass back-light in `foliage.frag.glsl:132-135` (a separate shader, not the scene PBR path); R3 does not touch it.
- `Material` (`engine/renderer/material.h`/`.cpp`) is a flat field set with clamped setters; PBR fields include `m_albedo`, `m_metallic`, `m_roughness`, `m_ao`, `m_clearcoat`, `m_emissive`. Materials serialize to `assets/materials/<name>.json` via `material_library.cpp` `serializeMat`/`applyMat` (one line per field, both directions).
- Material uniforms upload in `Renderer::uploadMaterialUniforms(const Material&)` (called from `drawMesh` before each mesh draw); light uniforms upload once per frame in `uploadLightUniforms`. Lights are plain **array uniforms** (`u_pointLights_*[]`, `u_spotLights_*[]`), not SSBOs — so R3 needs **no SSBO / binding-point work** (contrast R1/R2).
- Shader parity is tested with the 1×1-FBO readback fixture (`tests/shader_parity_helpers.h`, `gl_test_fixture.h`) + a CPU oracle; `extractGlslFunction` inlines a named GLSL function into a test shader so the **same** source is checked on CPU and GPU.

### 10.1 Scope decision — analytic, no LUT, no extra pass (Rule 9 "simpler path", Rule 2 "shortest correct")

The ROADMAP names R3 as "pre-integrated **wrap-lighting** approximation (no ray marching), per-material **thickness/transmission**, for the Tabernacle's dyed-linen curtains." The full Penner & Borshukov pre-integrated-skin technique drives the wrap width from **mesh curvature** via a 2D LUT indexed by `(N·L, curvature)`. R3 deliberately ships the **analytic** subset, not the curvature-LUT:

- **No LUT texture.** A baked `(N·L, curvature)` LUT would consume a texture unit (the engine is already unit-pressured — see `[[gl-compute-in-composite-unit0-clobber]]` and the fog unit-17 placement) and require a curvature buffer. The analytic wrap is a few ALU ops and needs neither.
- **No screen-space / texture-space diffusion pass.** Those are the deferred-renderer techniques (Jimenez separable SSSS); they need a separate blur pass over a light buffer. R3 stays a per-fragment lighting-model addition with **zero new passes, buffers, or attachments** — the cheapest path for a forward renderer (confirmed by the research survey: wrap models are "the alternative for forward renderers that can't afford separate post-processing passes").
- **Curvature-driven wrap is future work.** The full Penner curvature LUT (for skin) is deferred to Phase 13 / MetaHuman skin (`3D_E-0014`), which already cross-references R3. R3's use case — **backlit dyed-linen curtains** — is dominated by the *transmission* (back-scatter) term, not the curvature front-scatter, so the analytic subset covers it.

This matches the R1/R2 "foundation/parity-first" posture: ship the version that serves the named use case at the lowest cost, with the heavier variant flagged as a later slice.

### 10.2 The two terms (GPU, in the PBR path)

R3 adds two view-/light-dependent terms to each PBR light function, gated so non-SSS materials are byte-identical to today. Both terms are factored into **standalone GLSL helpers** so each can be lifted into a parity test via `extractGlslFunction` and mirrored on the CPU (§10.5, §10.8 #5/#7). The light functions call the helpers; they do not inline the math.

**Tuned constants are file-scope `const`s**, declared above the helpers (not function-local, not `#define`):

```glsl
const float SSS_MAX_WRAP   = 0.5;   // max terminator-wrap width (at strength = 1)
const float SSS_DISTORTION = 0.2;   // normal-perturbation of the back-light direction
const float SSS_POWER      = 4.0;   // back-scatter lobe tightness
const float SSS_SCALE      = 1.0;   // back-scatter master scale (future tuning knob; fixed at 1.0)
```

Because `extractGlslFunction` lifts only the named function body, the parity harness (§10.8) must **prepend these four `const` declarations** to the extracted helper so it compiles standalone — the test file declares them with the same literals.

**(a) Front scatter — colored wrap diffuse.** Soften the terminator and tint the bleed band (NVIDIA GPU Gems 3 ch.16 "wrap lighting"). The helper takes the **raw, signed** `N·L` (NOT the clamped `NdotL` the call sites already hold — the wrap must see `N·L < 0` to bleed past the terminator):

```glsl
vec3 sssFrontScatter(float rawNdotL, float strength, vec3 color)
{
    float lambert = max(rawNdotL, 0.0);                          // the plain Lambert term
    float wrap    = strength * SSS_MAX_WRAP;
    float wrapped = max((rawNdotL + wrap) / (1.0 + wrap), 0.0);  // light bleeds past N·L = 0
    return color * max(wrapped - lambert, 0.0);                  // colored bleed; max() defensive — wrapped ≥ lambert always (test #3)
}
```

For `N·L ∈ [0, 1]` the bleed is `wrapped − lambert = wrap·(1 − N·L)/(1 + wrap)`: **0 only at `N·L = 1`** (the pole, surface facing the light head-on), rising monotonically as `N·L` falls, peaking at the terminator (`N·L → 0`) and **continuing into the shadow side** (for `−wrap < N·L < 0`, decaying to 0 at `N·L = −wrap`). So the front-scatter does **not** leave the lit region untouched — it adds a tinted lift across the *whole* diffuse falloff (strongest near the terminator), which is the softened-falloff look subsurface scattering produces. It is `≥ 0` (never darkens) and gated/tinted per material, so the lift is opt-in, not a global change.

**(b) Back scatter — thickness translucency.** The dominant term for backlit thin surfaces (Barré-Brisebois & Bouchard, GDC 2011 "Approximating Translucency"):

```glsl
vec3 sssBackScatter(vec3 V, vec3 L, vec3 N, float strength, float thickness, vec3 color, vec3 radiance)
{
    vec3  backLightDir = normalize(L + N * SSS_DISTORTION);   // inverted-light scatter dir (NOT a half-vector)
    float backNdV      = pow(clamp(dot(V, -backLightDir), 0.0, 1.0), SSS_POWER) * SSS_SCALE;
    float transmit     = backNdV * strength * (1.0 - thickness);
    return color * radiance * transmit;                       // glows when viewing a backlit thin face
}
```

`dot(V, −backLightDir)` peaks when the viewer looks back through the surface toward the (distorted) light; it is 0 when the viewer is on the lit side (`V` aligned with `backLightDir`). `(1 − thickness)` makes thin material (curtain weave) transmit more; `thickness = 1` ⇒ no transmission. Back-scatter is **not** occluded by the surface's own front-facing shadow (the light is *behind* it) — it uses the raw light radiance, which is the intended look. **Note on the cited model (§10.9):** Barré-Brisebois & Bouchard use the *un-normalized* `dot(V, −(L + N·distortion))` inside the `pow`; R3 normalizes `L + N·distortion` so the pre-`pow` value is a true cosine in `[−1, 1]` independent of `|L + N·distortion|`, keeping `SSS_POWER` stable across lights — an intentional deviation, not a verbatim transcription.

**Exact fold (do not paraphrase — each PBR function differs).** Today each `calc*PBR` builds `baseLighting = (kD*albedo/PI + specular) * radiance * NdotL` (`scene.frag.glsl:786/837/885`), where the existing clamped `NdotL` (`= max(dot(N,L),0)`, `:764/816/872`) multiplies **both** the diffuse and the specular contributions. R3 changes the **diffuse contribution only**: the diffuse term becomes `kD*(albedo*NdotL + frontScatter)/PI * radiance`, leaving the specular contribution `specular * radiance * NdotL` untouched. The minimal-diff form, inside the `if (u_subsurfaceStrength > 0.0)` gate: compute the raw dot once (the functions only hold the clamped value), call the helper, and append it to `baseLighting`. **Insertion point matters:** these three lines go **immediately after the `baseLighting = …` assignment (`:786/837/885`) and before the `if (u_clearcoat > 0.0)` block (`:789/840/888`)** — appending them after the clearcoat block would skip the clearcoat attenuation the design intends:

```glsl
float rawNdotL = dot(N, L);   // signed — the clamped NdotL above would kill shadow-side bleed
vec3  frontScatter = sssFrontScatter(rawNdotL, u_subsurfaceStrength, u_subsurfaceColor);
baseLighting += kD * frontScatter / PI * radiance;   // clearcoat-attenuated + shadowed with the rest
```

This places `frontScatter` **inside** `baseLighting`, so it is correctly (a) attenuated by the clearcoat block `baseLighting *= (1.0 - u_clearcoat*Fc)` (`:795/845/893`) — a clearcoat layer dims the subsurface front-scatter showing through it — and (b) multiplied by `*(1.0 - shadow)` for the directional/point functions where that factor exists. **The spot function applies no shadow** (`:897`), so spot front-scatter is unshadowed — consistent with the rest of the spot contribution, which is also unshadowed today (a pre-existing engine state, not introduced by R3).

`backScatter` is added to the **return value**, after the clearcoat block and outside any shadow multiply, because the three functions return differently:
- `calcDirectionalLightPBR` / `calcPointLightPBR` return `baseLighting * (1.0 - shadow)` (`:799/849`) ⇒ `return baseLighting * (1.0 - shadow) + backScatter;`
- `calcSpotLightPBR` **applies no shadow** and returns bare `baseLighting` (`:897`) ⇒ `return baseLighting + backScatter;`. (The spot path having no shadow term is pre-existing engine state, unchanged by R3; the consequence — spot-lit front-scatter is unshadowed, like the rest of the spot contribution today — is recorded in §10.10.)

For point and spot lights, `frontScatter`/`backScatter` are automatically scaled by the existing distance attenuation (and spot cone) because they multiply `radiance`, which already folds in `attenuation` (`:814`) and the spot `intensity` (`:870`) — no extra multiply needed.

**Gate.** The `rawNdotL`/`frontScatter` lines and the `backScatter` call+add are wrapped in `if (u_subsurfaceStrength > 0.0) { … }`. When strength is 0 (the default for every existing material), the PBR path is **byte-identical** to today — pinned by test #6. The four `SSS_*` values are the file-scope `const`s declared above the helpers (see §10.6 for cost).

**Blinn-Phong path is out of scope.** SSS materials (curtains, candles, skin) are authored as PBR; the legacy Blinn-Phong functions keep their plain Lambert clamp. Stated so the implementer doesn't half-wire it.

### 10.3 Material fields + serialization (CPU)

Three new PBR fields on `Material`. The two **scalar** setters clamp (the if-statement clamp idiom of `setMetallic`/`setRoughness`); the **vec3** color setter is left **unclamped**, matching the existing `setAlbedo`/`setEmissive` vec3 setters (which do not clamp) — a tint > 1 is a legal over-bright, bounded later by tone-mapping (§10.7):

| Field | Type | Clamp | Default | Meaning |
|-------|------|-------|---------|---------|
| `m_subsurfaceStrength` | `float` | [0, 1] (clamped) | **0.0** | Master SSS amount; 0 ⇒ feature off (every existing material) |
| `m_subsurfaceColor` | `glm::vec3` | unclamped (matches `setAlbedo`) | (1, 1, 1) | Tint of scattered + transmitted light |
| `m_subsurfaceThickness` | `float` | [0, 1] (clamped) | 0.5 | Uniform thickness proxy; back-transmission ∝ (1 − thickness) |

Serialized in `material_library.cpp` `serializeMat` (write) + `applyMat` (read): the two **scalars** read via `j.value("subsurfaceStrength", 0.0f)` / `j.value("subsurfaceThickness", 0.5f)`; the **vec3** color reads via the existing `readVec3(j, "subsurfaceColor", glm::vec3(1.0f))` helper (the same helper `setAlbedo`/`setEmissive` use — `nlohmann::json::value` cannot deserialize a `glm::vec3` directly). All three are absent-key-tolerant, so **old material JSON without these keys loads unchanged** (additive, no schema bump — same posture as the fog accessibility fields). A thickness *texture* (per-texel thickness, for hems/folds) is explicit **future work**, not R3 — the scalar is the curtain-scale start.

### 10.4 Uniform plumbing (CPU)

`Renderer::uploadMaterialUniforms` sets three new uniforms beside the existing PBR block: `u_subsurfaceStrength` (float), `u_subsurfaceColor` (vec3), `u_subsurfaceThickness` (float). The un-prefixed `u_subsurface*` naming follows the existing `u_clearcoat` / `u_clearcoatRoughness` PBR-extension uniforms (`renderer.cpp:1620-1621`), which are likewise not `u_pbr*`-prefixed — so this is the established pattern for PBR add-ons, not a new convention. No per-frame light-side change; no SSBO; no new texture unit. The three uniforms are declared in `scene.frag.glsl`; the four `SSS_*` tuned constants are the file-scope `const`s from §10.2 (declared once, there).

### 10.5 CPU / GPU placement (Rule 7)

| Work | Placement | Reason |
|------|-----------|--------|
| Front-scatter wrap + back-scatter transmission | **GPU** (scene fragment shader, PBR path) | Per-pixel, per-light, data-parallel — same class as the BRDF beside it. |
| Material fields + JSON round-trip + uniform upload | **CPU** | Authoring/serialization/I/O; branchy, once per material. |
| SSS math parity | **CPU mirror** (`engine/renderer/subsurface_math.h`) pinned against the GLSL via `extractGlslFunction` | Dual CPU-spec/GPU-runtime with a parity test, per Rule 7 (mirrors `motion_vector_math.h`). |
| `SSS_MAX_WRAP` / `SSS_DISTORTION` / `SSS_POWER` / `SSS_SCALE` look constants | **Hand-coded** with `TODO: revisit via Formula Workbench` | Rule 6 carve-out: these are an **artistic** look with **no ground-truth reference dataset** to fit (true SSS reference would need a path-traced curtain capture, which does not exist). Same disposition as the fog HG/Schlick decision (slice 11.7 dropped for the same "no reference data / no perf need" reason). If a reference capture is ever produced, the front-scatter falloff is the fittable candidate. |

### 10.6 60 FPS floor

R3 adds, **only on materials with `subsurfaceStrength > 0`**, ~8 ALU ops + one `normalize` + one `pow` per light per fragment, and **nothing** (one compare) on every other material. No new passes, attachments, buffers, texture binds, or draw calls. Structurally the cheapest possible SSS. **Floor & acceptance threshold:** worst case is a full-screen SSS curtain quad lit by the directional light + all active point/spot lights. The pass/fail criterion: with that worst-case scene, total frame time must regress **no more than 0.5 ms** versus the same scene with `subsurfaceStrength = 0` (the gated-off path), and must stay **≥ 60 FPS (≤ 16.6 ms/frame)** outright on the RX 6600 at 1080p. Verified by the §10.8 #8 frame-time benchmark comparing strength-on vs strength-off (measured, not assumed); the existing fog/HUD GPU benchmarks must also stay green (they do not exercise SSS, so they guard against an unrelated regression slipping in alongside). The branch is coherent across a draw (whole material is SSS or not), so no warp-divergence concern.

### 10.7 Accessibility

SSS adds a soft, static glow (no temporal flashing, no strobe), so it does not interact with the photosensitive caps (`PostProcessAccessibilitySettings`). The transmitted-light term is view-dependent but changes smoothly with camera motion — no new accessibility hook required. Worth a one-line note that an over-bright `subsurfaceColor·radiance` could raise scene luminance; clamped by the existing tone-map/auto-exposure path, not a new concern.

### 10.8 Test contract (R3)

GL-free where possible (CPU mirror in a new `tests/test_subsurface.cpp`; GL parity in `tests/test_subsurface_parity.cpp`):

1. **`MaterialSubsurfaceFieldsRoundTrip`** — set the three fields, serialize, deserialize ⇒ equal; **scalar** clamps enforced (strength/thickness ∈ [0,1]); the **color is unclamped** (a (1.5, 1, 1) tint round-trips unchanged, matching `setAlbedo`); **old JSON without the keys loads with defaults** (strength 0, color white, thickness 0.5).
2. **`FrontScatterZeroAtStrengthZeroAndAtPole`** — CPU mirror: `subsurfaceStrength = 0` ⇒ `frontScatter = 0` for all `N·L` (so the diffuse term equals the plain `kD*albedo/PI*radiance*max(N·L,0)`). For `strength > 0`, asserting the two branches separately: **(i) lit side `N·L ∈ [0, 1]`** — `frontScatter = subsurfaceColor·wrap·(1 − N·L)/(1 + wrap)`, which is `0` **only at `N·L = 1`** and strictly positive for `N·L ∈ [0, 1)` (pins that the whole lit gradient is lifted, not just a terminator band). **(ii) shadow side `N·L ∈ (−wrap, 0)`** — `lambert = 0`, so `frontScatter = subsurfaceColor·(N·L + wrap)/(1 + wrap)` (a *different* expression — the lit-side closed form does not extend below `N·L = 0`), decaying to `0` at `N·L = −wrap`.
3. **`FrontScatterNeverDarkens`** — CPU mirror: `wrapped ≥ lambert` for all `N·L ∈ [−1, 1]`, all `wrap ∈ [0, 0.5]`. The guard has real content on `N·L ∈ [−wrap, 1]` (below `−wrap` both terms clamp to 0 and the check is the trivial `0 ≥ 0`); the test asserts the lit surface never loses energy across the meaningful range.
4. **`BackTransmissionPeaksWhenViewingTowardBacklitThinFace`** — CPU mirror: transmission is maximal when `V` aligns with `−backLightDir` (viewer looking back through the surface toward the light) and `thickness → 0`; **0** when `thickness = 1`, or `strength = 0`, or the viewer is on the lit side (`V` aligned with `backLightDir`, so `dot(V, −backLightDir) ≤ 0`).
5. **`FrontScatterParityCpuVsGlsl`** (GL) — `extractGlslFunction` pulls the **standalone front-scatter helper** (`vec3 sssFrontScatter(float rawNdotL, float strength, vec3 color)`, factored out of `scene.frag.glsl` for exactly this reason) into the test shader, with the four `SSS_*` `const` declarations prepended so it compiles standalone (§10.2). Matches the CPU mirror within tol over a grid of `(rawNdotL, strength)` that **includes negative `N·L`** (so the shadow-side bleed range is actually exercised).
6. **`SubsurfaceTermsVanishAtStrengthZero`** — the equivalence guard, in two parts that are both achievable with the existing fixture: (a) **CPU mirror** — at `strength = 0`, both `sssFrontScatter(...)` and `sssBackScatter(...)` return exactly `vec3(0)`, so the `baseLighting += …` / `+ backScatter` additions are provably no-ops; (b) **GL parity** — the two extracted helpers return bit-zero at `strength = 0`. Full-shader byte-identity is **not** asserted by extracting `calcDirectionalLightPBR` (it transitively calls `distributionGGX`/`geometrySmith`/`fresnelSchlick`/`calcClearcoatLobe`/`calcShadow` + shadow-map state the 1×1 fixture can't supply); instead it is guaranteed structurally — the additions live behind `if (u_subsurfaceStrength > 0.0)`, so `strength = 0` takes the pre-R3 code path unchanged — and pinned by the existing scene-shader regression/visual suite staying green.
7. **`BackTransmissionParityCpuVsGlsl`** (GL) — same parity check for the standalone `sssBackScatter` helper over a `(dot(V, −backLightDir), thickness)` grid.
8. **`SubsurfaceFrameTimeWithinBudget`** (perf) — **DEFERRED at implementation (logged deviation, Rule 5).** Unlike the fog benchmark (which times an isolated `VolumetricFogPass` compute dispatch), SSS has **no isolated pass** — it is a few gated ALU ops folded into the existing scene fragment shader, so a faithful "render the scene twice and diff frame time" benchmark would require standing up a full `Renderer` + scene + light fixture, and the measured delta would sit below GPU timing noise (well under the ≤ 0.5 ms target). Per Rule 2 (shortest correct) that harness is disproportionate to the signal. The 60 FPS floor is instead gated by (a) the structural argument in §10.6 (no new pass/buffer/bind; gated per-material), (b) the existing fog/HUD GPU benchmarks staying green, and (c) the launch-time frame-time spot-check in a curtain scene that §10.6 already names. If a regression is ever suspected, the deterministic 1 dir + 8 point + 4 spot worst-case scene above is the harness to build. Recorded in CHANGELOG.

### 10.9 Sources (R3)

- **Pre-integrated skin shading / wrap lighting:** Penner & Borshukov, "Pre-Integrated Skin Shading," *GPU Pro 2* (2011); d'Eon & Luebke, "Real-Time Approximations to Subsurface Scattering," *GPU Gems 3* ch. 16 (NVIDIA) — the wrap-diffuse front term and the curvature-LUT (deferred) both come from this lineage.
- **Cheap translucency (back-scatter):** Barré-Brisebois & Bouchard, "Approximating Translucency for a Fast, Cheap and Convincing Subsurface Scattering Look," GDC 2011 — the `pow(saturate(dot(V, -(L + N·distortion))), p)` transmission model R3 follows, with one intentional deviation (the `L + N·distortion` vector is normalized first; §10.2(b)).
- **Forward-renderer suitability:** the technique survey (MJP, "An Introduction to Real-Time Subsurface Scattering"; Chaos "Subsurface scattering explained") confirms wrap + thickness-transmission as the standard forward-path choice when a screen-space diffusion pass is not affordable.

### 10.10 Deferred / knowingly-shipped items

Recorded here so they are decisions, not silent gaps:

- **Spot-light front-scatter is unshadowed.** `calcSpotLightPBR` applies no shadow term today (`scene.frag.glsl:897`), so R3's spot front-scatter (folded into `baseLighting`) is not occluded by cast shadows — the same as the rest of the spot contribution. R3 **ships this knowingly** rather than adding spot shadows (out of scope; a spot-shadow pass is its own work item). Consequence: a spot-lit translucent surface behind an occluder may show a little front-scatter it physically shouldn't. Acceptable for the curtain use case (curtains are directional-sun-lit); filed as a follow-up if spot shadows ever land.
- **`SSS_SCALE = 1.0` is intentionally inert** — a named back-scatter master scale left in the formula so a future Workbench fit (§10.5) has a knob; no behavioral test asserts it (test #7 cannot distinguish `*1.0` from absence).
- **Per-texel thickness** (a thickness map for hems/folds) and the **curvature-driven pre-integrated front-scatter LUT** (Penner skin) are both future work (§10.1, §10.3), deferred to Phase 13 / MetaHuman skin (`3D_E-0014`). R3 ships the uniform-scalar thickness + analytic wrap.

---

## 11. Slice R4 — Dynamic global illumination (design-of-record)

**Status:** ✅ **IMPLEMENTED 2026-06-24** (Variant A) — `gi_inject.comp.glsl` + `gi_math.h` CPU mirror + the att3 injection source / GI read in `scene.frag.glsl` + `VolumetricFogPass` ping-pong cache + the `dynamicGiEnabled`/`reduceMotionGi` accessibility wire. Tests: `test_gi.cpp` (CPU) + `test_gi_gpu.cpp` (GPU: full-scene-shader compile, inject compile, 3-way `giSliceCoord` parity, depth-match gate) + `test_fog_benchmark.cpp` GI-inject ≤ 0.4 ms gate — all green on the RX 6600. Two faithful refinements over the prose below, recorded in CHANGELOG: (1) the scene pass samples the GI **history** texture (the correct ping-pong read — the inject runs *after* the scene pass, so `m_giTex` is mid-write); (2) GI is **TAA/SMAA-only** (att3 + the cache live on the non-MSAA scene FBO, like the R1/R2 attachments). Variant B (`3D_E-0015`) remains the documented upgrade. ✅ design-of-record SIGNED OFF (2026-06-19) — cold-eyes converged after **5 loops** (Loop 5: zero CRITICAL/HIGH, polish-only; see §12 R4 log). Sign-off delegated to Claude per the project's spec-sign-off convention (gate = cold-eyes clean or polish-only). Covers **two variants** (user steer "design both, pick"): **Variant A — froxel-cached near-field dynamic GI** (the chosen, shippable v1 — implement this) and **Variant B — full "Froxel Bounce"** (the ROADMAP `3D_E-0015` upgrade, deferred). A's storage + read path are reused by B unchanged; B adds the RSM off-screen injector + a propagation pass on top (largely additive, with shared numerical re-tuning — §11.1).

### 11.0 What exists today that R4 builds on (reality check, verified against source)

- **Froxel grid (the GI cache home).** `VolumetricFogPass` owns a view-frustum-aligned **160×90×64** froxel grid in two `GL_RGBA16F` `GL_TEXTURE_3D`s — `m_volumeTex` (scratch) and `m_integratedTex` (final), trilinear + clamp-to-edge (`volumetric_fog_pass.cpp:84-90`, `:23-30`), exponential view-space depth slices (`volumetric_fog.h:56-60` for the `resX/Y/Z`/`near=0.5`/`far=200` config; the exponential distribution is the file-header spec `:23-37`). Built by three compute passes (inject → scatter → integrate) via `glBindImageTexture` (`volumetric_fog_pass.cpp:203/238/246-247`), each followed by a memory barrier (inject/scatter use `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT`; integrate also flags texture-fetch/update). **The fog grid is rebuilt fresh every frame — the inject pass writes `GL_WRITE_ONLY` with no prior-frame read and no reprojection — so it does NOT accumulate across frames.** (This matters for R4: a view-aligned grid re-maps to new world positions each frame, so any GI cache reuse MUST reproject — §11.2.) `dispatch(FrameParams)` is called at **`renderer.cpp:1359`**, deliberately **before** the composite binds its textures (`:1289-1295` comment + `:1376` `hdrSourceFbo->bindColorTexture(0)`).
- **Per-pixel froxel read (the sampling pattern R4 mirrors).** The composite reads inscatter from `u_volumetricTexture` (unit **17**, `screen_quad.frag.glsl:70`) via `volumetricSliceCoord(viewDepth, near, far) = clamp(log(vd/near)/log(far/near), 0, 1)` (`:93-96`) — the trilinear read R4's per-fragment indirect-irradiance fetch mirrors. Note this helper lives **only** in `screen_quad.frag.glsl` today; R4 moves it to shared GLSL + the `gi_math.h` CPU mirror (Rule 7), and the per-fragment froxel screen-UV is derived from `gl_FragCoord.xy / u_resolution`, **not** the mesh `v_texCoord`.
- **Last-frame HDR colour (the screen-space injector source).** `Taa` keeps `m_historyFbo` (full-res `GL_RGBA16F`, no depth), swapped each frame (`taa.cpp:15-26`, `:101-104`) — the previous frame's resolved HDR, samplable by a compute pass.
- **Motion + normal (temporal reuse).** Slice R1/R2 give per-pixel motion in `m_taaSceneFbo` attachment 1 and geometric world normal in attachment 2 + the retained `m_prevNormalFbo` (`renderer.cpp:1028/1050/1052`), all `GL_RGBA16F` and compute-samplable.
- **Static GI floor (the no-double-count anchor).** `SHProbeGrid` (7 `RGBA16F` 3D textures, units 17–23) supplies baked diffuse irradiance; the scene shader **sets** `ambient = (diffuseIBL + specularIBL) * ao * u_iblMultiplier` (`scene.frag.glsl:1226`, an **assignment**), where `diffuseIBL = kD * irradiance * albedo * u_iblDiffuseScale` (`:1222`) from `evaluateSHGridIrradiance` (`:570-636`). The clearcoat block later reads `ambient` (`:1242`). **R4 adds its dynamic indirect term *after* the `ambient =` assignment (`:1226`)**, layered on the floor — see §11.2 for the energy model that keeps it from double-counting the baked bounce.
- **CSM cascades (Variant B's RSM source).** `CascadedShadowMap` renders up-to-4 cascades into a **depth-only** FBO (`glNamedFramebufferDrawBuffer(GL_NONE)`, `cascaded_shadow_map.cpp:42-43`); accessors `getCascadeCount`/`getLightSpaceMatrix(i)`/`getCascadeSplit(i)`/`depthTextureArray()` exist. A flux RSM (Variant B) needs a **colour attachment added to the coarsest cascade** — not present today.
- **Composite texture-unit budget.** Taken at composite time: 0 (scene colour), 9 (bloom), 10 (SSAO), 11 (contact), 12 (fog depth), 13 (LUT), 17 (volumetric). Units 14–16 (scene-pass IBL) are free by composite time; **units ≥ 24 are free**. The **unit-0 clobber hazard** (`[[gl-compute-in-composite-unit0-clobber]]`) mandates R4's compute dispatch run in the same pre-composite window as the fog dispatch.
- **Accessibility toggle pattern.** `PostProcessAccessibilitySettings` (`reduceMotionFog` `:89`, `volumetricFogEnabled` `:102`, `godRaysEnabled` `:113` in `engine/accessibility/post_process_accessibility.h`) + the settings wire is the pattern a `dynamicGiEnabled` toggle follows.

### 11.1 The two variants + recommendation

| | **Variant A — Froxel near-field GI (recommended v1)** | **Variant B — Full Froxel Bounce (`3D_E-0015` upgrade)** |
|---|---|---|
| Indirect source | On-screen only: last-frame **diffuse-direct** radiance (att3) splatted into froxels | A **+** off-screen sun bounce via a single-cascade flux RSM |
| New passes | **One** compute (screen→froxel inject) | A **+** RSM colour attachment + **one** propagation compute (LPV-style) |
| Off-screen light | ✗ (SH floor + screen only) | ✓ (RSM captures sunlit surfaces outside the frustum) |
| Est. cost (RX 6600, 1080p) | ~0.3 ms | ~0.7 ms |
| CSM change | none | adds a colour attachment to the coarsest cascade |
| Risk | low — reuses froxel grid + history wholesale | medium — new RSM render target + propagation tuning |

**Recommendation: ship Variant A as R4 v1.** It delivers dynamic, view-reactive indirect light *by default* at the lowest cost and risk, reuses the froxel cache + reprojection wholesale. B reuses A's **storage layout and per-fragment read path unchanged** (same `m_giTex`/`m_giHistoryTex`, same composition site), so it is largely additive (RSM attachment + a second inject sub-pass + a propagation compute). **Caveat (not "zero rework"):** B's propagation spreads radiance between froxels, which changes what a froxel's `.rgb`/`.a` mean after inject, so B may **re-tune** A's `α`/`decay`/confidence — the *code* additions are clean, the *numerical* tuning is shared and may shift. B's distinguishing win (off-screen bounce) is the heaviest, highest-tuning part; gating it behind a v1 that ships the near-field bounce matches the project's "foundation/parity-first, simpler path" posture (Rule 9) and keeps `3D_E-0015` as the tracked upgrade.

### 11.2 Variant A — Froxel near-field dynamic GI (the v1)

**Idea.** Cache **one bounce of dynamic direct light** in a GI channel co-located with the froxel grid. Each frame, splat last-frame surfaces' **diffuse-direct outgoing radiance** into the froxel each visible surface falls into; accumulate temporally with a **reprojected** read of the previous frame's cache; a shaded fragment reads the indirect radiance arriving in its froxel and adds `kD · giIrradiance · albedo` to its lighting. The baked `SHProbeGrid` stays the static floor; Variant A adds the dynamic single-bounce on top.

**Why "diffuse-direct outgoing radiance," not raw HDR (energy model — resolves the double-count).** Injecting the full last-frame HDR (`m_historyFbo`) would feed specular highlights and emissive into a *diffuse* indirect term, and would re-bounce the SH-ambient already in the floor. So the scene pass writes a **dedicated injection source**: a 4th MRT attachment on `m_taaSceneFbo` holding `albedo · Σ(direct diffuse, shadowed)` — direct-diffuse outgoing radiance only, **excluding** ambient/SH, specular, and emissive. Injecting this means the GI term is a true *single bounce of direct light* and never re-adds the SH-ambient floor. **Residual overlap (surfaced, not hidden):** for a *static* light that the `RadiosityBaker` already baked into the SH grid, the dynamic single-bounce of that light's direct term overlaps the bake's first bounce. Variant A bounds this with a `u_giStrength` scale (default **0.5**, Formula-Workbench TODO) on the dynamic term and documents it; the clean removal — baking sky/static-emissive only and letting dynamic GI own moving-light bounce — is a `RadiosityBaker`-scope follow-up, not v1. This is the standard baked-floor + dynamic-delta trade; the design states it rather than claiming zero overlap.

**Prerequisite — Framebuffer 4th attachment.** `m_taaSceneFbo` has exactly 3 colour attachments today (`secondColorAttachment` = R1 motion att1, `thirdColorAttachment` = R2 normal att2; `Framebuffer` has no 4th). The injection source is **att3** (`GL_RGBA16F`), masked opaque-only via `glColorMaski` exactly like the R1/R2 attachments and cleared per-attachment. `Framebuffer` gains `fourthColorAttachment` support **and the matching `clearFourthAttachment()`** helper (the same generic widening R1 §4.0 / R2 §9.4 did for attachments 2 and 3, which added `clearSecondAttachment`/`clearThirdAttachment`) — bundle-consistent infra, not a one-off. att3 is **cleared to `vec4(0,0,0,0)` each frame before the opaque pass** (so uncovered/non-opaque texels inject zero radiance, not stale garbage), exactly as the R1/R2 attachments are.

**Storage (ping-pong — mandatory, not optional).** Two GI froxel textures, **`m_giTex`** (this frame's write target) and **`m_giHistoryTex`** (last frame's cache), both `GL_RGBA16F` `GL_TEXTURE_3D` at the **same 160×90×64 dims/filtering** as the fog volume, owned by `VolumetricFogPass`, **swapped each frame** (`std::swap`, like `Taa::swapBuffers`). `.rgb` = accumulated indirect radiance, `.a` = confidence/accumulation weight in [0,1]. **Both textures are GL-cleared to `vec4(0,0,0,0)` at allocation**, so frame 0 reads history with confidence 0 everywhere (all froxels cold — no undefined first-frame state). Ping-pong is **required** (not the in-place alternative an earlier draft floated): the inject reads history at a *reprojected* coord and writes at the thread's own coord — different texels — which is unsafe in a single image. **Swap timing:** the swap is end-of-frame (after the inject dispatch, like `Taa::swapBuffers`), so the scene pass reads `m_giTex` = the previous frame's completed cache, and the inject writes the post-swap `m_giTex` reading `m_giHistoryTex` = that same previous cache.

**Inject pass (the one new compute).** `assets/shaders/gi_inject.comp.glsl`, dispatched **in the same pre-composite window as the fog passes** (`renderer.cpp` ~`:1359`, after the fog `dispatch`, before the composite binds unit 0). **Timing note:** the inject runs *after* the current frame's scene pass, so `m_taaSceneFbo` att3 already holds **this frame's** diffuse-direct radiance — the injection source is current-frame, sampled at current-frame screen coords. The only *previous*-frame data is the GI cache (`m_giHistoryTex`), read with reprojection. So **no retained previous-frame att3 copy is needed** (unlike `m_prevNormalFbo`). One thread per froxel `[i,j,k]`:
1. Reconstruct the froxel centre's **world** position (this frame's `invView`·`invProjection` + the exponential slice depth — the froxel→world math the fog inject already does).
2. **Reprojected history read (the C1 fix).** Transform that world position through the **previous** frame's camera view-projection — `m_prevViewProjection` (`renderer.cpp:1521`, the snapshot R1's camera reprojection already binds as `u_prevViewProjection` at `:1032`/`:3275`); **note** `m_lastView`/`m_lastProjection` are *this* frame's matrices (used in step 1), **not** the previous frame's. `m_prevViewProjection` is **not** currently in any compute pass's params (the fog `FrameParams` carries only current-frame `invView`/`invProjection`), so the GI inject must add it to its frame-params/uniforms. **Also:** `m_prevViewProjection` is refreshed today only inside the TAA branch (`renderer.cpp:1521`, `if (isTAA)`), so in MSAA/SMAA/None modes it is stale; since R4's GI cache accumulates every frame, this update must be **hoisted unconditional** — a one-line move, noted here as an R4 prerequisite. Reproject → the previous frame's froxel coord (prev-view-space depth → slice via the shared slice fn); sample `m_giHistoryTex` trilinearly **there**. Froxel `[i,j,k]` is a *different* world point each frame (view-aligned grid), so history MUST be read at the reprojected coord, not at `[i,j,k]`. If the reprojection lands outside the previous frustum (`[0,1]³`), it is a **cold** froxel: history contributes nothing (see step 4).
3. **Injection sample (current frame).** Project the world position into **this** frame's screen + view depth; sample the current depth buffer at that screen UV — `valid = true` iff the froxel-centre **view depth ≈ the depth-buffer surface depth within ±½ the froxel's slice thickness** (the exponential slice thickness from the fog spec, so the tolerance tracks the slice — not a fixed epsilon that would mismatch far slices), meaning the froxel sits on the visible, unoccluded surface; then sample **att3** at that screen UV → this frame's injected radiance. Else `valid = false`. (att3 is diffuse-direct only, so reading it back into the same surface's froxel cannot feed back through the GI term — see self-illumination note.)
4. **Confidence-weighted EMA.** Cold froxel (step-2 reprojection out of frustum): `gi.rgb = valid ? injected : 0`, `gi.a = valid ? α : 0` (no blend against undefined history). Warm froxel: `gi.rgb = valid ? mix(historyRgb, injected, α) : historyRgb`, `gi.a = valid ? min(historyA + α, 1.0) : historyA·(1 − decay)`. **α = 0.1**, **decay = 0.05** exactly (shipped defaults; Formula-Workbench TODO per Rule 6 — numerical tuning constants with no reference dataset, same disposition as the fog/SSS constants). Write to `m_giTex`. The `.a` channel is **live**, not dead state: the per-fragment read weights by it (below), so a froxel that loses injection fades out as confidence decays rather than holding stale radiance.

**Reduce-motion (normative).** When `reduceMotionGi` (the accessibility flag), set **`α = 0` AND `decay = 0`** ⇒ both the blend and the confidence bleed are frozen, so a warm froxel's read output (`.rgb · .a`) is byte-stable frame-to-frame for an unchanging scene — no temporal shimmer for motion-sensitive users. (α=0 alone would not freeze: the warm-invalid branch decays `.a` independently of α, so `decay` must be zeroed too.) A defined path (test #2), not optional.

**Per-fragment read + composition.** In `scene.frag.glsl`, gated by `u_dynamicGiEnabled` (which the renderer sets to `dynamicGiActive = dynamicGiEnabled && pass.isInitialized()`, so an uninitialised pass reads as off), sample `m_giTex` at the fragment's froxel coord — screen UV from `gl_FragCoord.xy / u_resolution` (**not** the mesh `v_texCoord`) and slice from the shared `volumetricSliceCoord(v_viewDepth, near, far)` — and add the dynamic indirect term **after** the SH-floor `ambient =` assignment (`scene.frag.glsl:1226`). **New scene-pass inputs this read needs** (call them out like the `m_prevViewProjection` prereq above): a `u_resolution` (vec2) uniform — `scene.frag.glsl` has none today; the renderer sets it from `m_windowWidth/Height` — and the fog `near`/`far` froxel range (reuse the existing packed `u_volNearFar` vec2 name for consistency, or carry it in when `volumetricSliceCoord` moves to shared GLSL). `v_viewDepth` is **already** a scene varying (declared `scene.frag.glsl:20`; written `= -(u_view·worldPos).z` in `scene.vert.glsl:230`), so no new varying is needed. Composition:
```glsl
ambient += u_giStrength * gi.a * kD * giIrradiance * albedo * ao;   // u_giStrength default 0.5; gi.a = confidence
```
Note this lands **before** the clearcoat read-modify-write at `:1242` (`ambient = ambient*(1.0 - u_clearcoat*ccFresnel) + ccIBL*u_clearcoat*ao*u_iblMultiplier`), so on clearcoat materials the GI term is attenuated by the coat alongside the floor — **intended** (GI is base-layer indirect under the coat). `u_iblMultiplier` is **not** applied to the GI term — that is the IBL/SH master scale; the dynamic GI's only magnitude knob is `u_giStrength` (so the §11.2 overlap bound stays governed by `u_giStrength` alone), and `ao` applies because GI is indirect light. `giIrradiance` is the froxel `.rgb`; the read weights by `gi.a` so low-confidence (just-revealed/decaying) froxels fade rather than pop. The GI read binds a free **scene-pass** unit — the scene pass occupies 0–11, 14–16, 17–23, so bind GI at **unit 24** (distinct from the composite's free set).

**Self-illumination (honest bound, not hand-waved).** A flat surface injects its diffuse-direct into froxel F and the same surface later reads F back as indirect — a real self-contribution, the standard screen-space-GI self-term. It is **bounded**, not eliminated: the injected source excludes the GI term (att3 is direct-only), so there is no runaway feedback; the steady-state self-add is at most `u_giStrength · α · albedo ·` (the surface's fill fraction of its froxel) ≈ a few percent of direct at the shipped `u_giStrength = 0.5`, `α = 0.1`. Froxels typically aggregate many surfaces, shrinking the self-fraction further. This is accepted for v1 (the same class of approximation SSGI carries) and tunable via `u_giStrength`; a geometric self-exclusion is a follow-up, not v1.

**60 FPS budget.** One extra `RGBA16F` 160×90×64 image write (the inject compute) ≈ one existing fog pass; one extra opaque MRT attachment write (att3) in the scene pass; the per-fragment add is one trilinear fetch. **Primary gate (what R4 controls): the GI inject dispatch stays ≤ 0.4 ms and the frame stays ≤ 16.6 ms** on the RX 6600 at 1080p (est. ~0.3 ms). The benchmark (#8) times the GI inject in isolation against the 0.4 ms bound and reports the combined fog+GI dispatch alongside the fog's measured baseline (the fog stack currently measures well under its 2.0 ms sub-budget). **When GI is off, the inject dispatch is skipped entirely** (not just the read), so "off" costs zero GPU.

### 11.3 Variant B — Full Froxel Bounce (the `3D_E-0015` upgrade)

Variant B = Variant A **plus** two additions, both pure additions to A's froxel GI channel:

1. **Off-screen sun bounce via a single-cascade flux RSM.** Add **one** `GL_RGBA16F` colour attachment to the **coarsest** CSM cascade's FBO (today depth-only, `cascaded_shadow_map.cpp:42-43`), writing reflected flux `albedo · sunColour · NdotL` per texel. A second GI inject sub-pass treats each RSM texel as a virtual point light: reconstruct its world position from the cascade depth + `getLightSpaceMatrix`, and splat its flux into the froxel it falls in. This captures sun bouncing off surfaces **outside the view frustum** — the thing screen-space (Variant A) fundamentally cannot do.
2. **LPV-style propagation pass.** `assets/shaders/gi_propagate.comp.glsl`, one compute pass spreading injected radiance to neighbouring froxels (1–2 froxel-steps/frame, anisotropy borrowed from the fog phase function), amortised across frames by the temporal accumulation already in A. Grows the effective bounce distance without a per-frame cost spike.

**Cost** ≈ +0.4 ms over A (one coarse-cascade attachment write + the RSM inject sub-pass + one propagate compute) → ~0.7 ms total, still inside the floor. **Known limitation (surfaced):** a view-frustum froxel grid does not cache bounce from directly *behind* the camera; the RSM + SH floor mitigate, acceptable for architectural walkthroughs (a world-space grid would be a later, separate upgrade). **Why deferred from v1:** the RSM render-target change + propagation tuning are the heaviest, highest-iteration parts; A ships the dynamic-GI-by-default goal without them.

### 11.4 Shared concerns

- **Compute ordering (critical — `[[gl-compute-in-composite-unit0-clobber]]`).** Both variants' GI compute dispatches run in the **pre-composite window** alongside the fog dispatch (`renderer.cpp` ~`:1359`), each issuing its own memory barrier, **before** the composite binds scene colour to unit 0 (`:1376`). The per-fragment GI read happens in the **scene pass** (which runs before the composite), sampling `m_giTex` — so it binds a free unit during the scene pass, not the composite; no unit-0 conflict. (If a composite-side read is ever needed, it uses a free unit ≥ 24.)
- **CPU / GPU placement (Rule 7).** **GPU:** froxel→world/screen reconstruction, screen-colour injection, RSM flux write (B), propagation (B), per-fragment trilinear GI read — all per-texel/per-pixel, data-parallel. **CPU:** dispatch orchestration, GI froxel/RSM config, EMA-α and VPL-count config (branchy, sparse). A CPU mirror (`engine/renderer/gi_math.h`) pins any non-trivial shared math (froxel↔world reconstruction reuse, the EMA blend) via a parity test, mirroring `subsurface_math.h`/`motion_vector_math.h`.
- **Accessibility.** A `dynamicGiEnabled` flag on `PostProcessAccessibilitySettings` (default on), gated `dynamicGiActive = dynamicGiEnabled && pass.isInitialized()` like `volumetricFogEnabled`. GI is low-frequency indirect light (no flashing/strobe), so no photosensitive interaction. A `reduceMotionGi` flag drives the **normative** `α = 0` freeze (§11.2) — hold the cache static so motion-sensitive users see no temporal shimmer. When `dynamicGiActive` is false the **inject dispatch is skipped *and* the scene-shader GI read is gated off** ⇒ zero GPU cost and byte-identical to pre-R4.

### 11.5 Decision

Implement **Variant A** as R4 v1 (cold-eyes + sign-off gate per the project's delegated-sign-off convention). Variant B (`3D_E-0015`) remains the documented upgrade — its RSM + propagation are additive to A's froxel GI channel. The ROADMAP `3D_E-0015` bullet stays open as the upgrade tracker; the R4 ROADMAP bullet flips when A ships.

### 11.6 Test contract (R4, Variant A)

GL-free where possible (CPU mirror in `tests/test_gi.cpp`; GL parity / behaviour in `tests/test_gi_gpu.cpp`):

1. **`GiFroxelReconstructionMatchesFogPath`** — CPU mirror (`gi_math.h`): the froxel→view-depth / slice-coord math the GI inject + read share equals the fog spec (`viewDepthToFroxelSlice`/`volumetricSliceCoord`) over a grid, AND the per-fragment **read** coord round-trips with the inject **write** coord for the same froxel (read-coord == write-coord — guards an off-by-half-texel read/write mismatch).
2. **`GiEmaBlendConvergesAndDecays`** — CPU mirror of the EMA at the exact shipped constants (`α = 0.1`, `decay = 0.05`): repeated injection of a constant radiance converges `.rgb` to it within tolerance and drives `.a → 1`; ceasing injection holds `.rgb` but decays `.a` by `(1−decay)` per frame, so the **confidence-weighted read result** (`.rgb · .a`) fades toward zero — pins that `.a` is live, not dead state; cold-froxel start yields `.rgb = injected, .a = α` (no blend against undefined history); **reduce-motion (`α = 0` AND `decay = 0`) ⇒ the read result `.rgb · .a` is invariant across frames** for an unchanging scene, including a warm-invalid froxel (asserts the freeze actually freezes the *output*, not just the blend).
3. **`GiHistoryReadIsReprojected`** (CPU mirror, the C1 guard) — with a non-identity camera delta between frames, the inject reads history at the froxel centre's **previous-frame froxel coord**, not at `[i,j,k]`; a froxel whose reprojection lands outside the previous frustum (`[0,1]³`) reads confidence 0 (cold start, no stale smear). Pins that the cache is reprojected, not index-aliased.
4. **`GiInjectWritesOnlyWhereDepthMatches`** (GL) — a froxel whose reprojected centre matches the previous-depth sample takes the injected radiance; an occluded/off-screen froxel keeps its (reprojected) history; confidence `.a` gates the write.
5. **`GiInjectionSourceExcludesAmbientSpecularEmissive`** (GL/parity, the energy-model guard) — the att3 injection source equals `albedo · Σ(direct diffuse, shadowed)` and contains **no** SH-ambient, specular, or emissive contribution (so the dynamic single-bounce cannot re-add the SH floor). Pins the §11.2 double-count resolution.
6. **`GiReadIsAdditiveOnTopOfFloor`** (GL/parity) — the scene-shader add equals `u_giStrength * gi.a * kD * giIrradiance * albedo * ao` (no `u_iblMultiplier` on the dynamic term), applied **after** the SH-floor `ambient =` assignment (`:1226`); it adds to, never replaces, the floor; `u_giStrength = 0` (or `gi.a = 0`) reduces it to the floor exactly. Also pins that the clearcoat read-modify-write at `:1242` attenuates the GI term together with the floor on clearcoat materials (intended).
7. **`DynamicGiOffIsByteIdentical`** (GL + structural) — `dynamicGiEnabled = false` ⇒ the inject dispatch is **skipped** and the scene-shader GI read is gated off ⇒ scene output unchanged vs pre-R4 (equivalence guard, R3 #6 shape). Also asserts the renderer sets `u_dynamicGiEnabled = dynamicGiActive`, so an **uninitialised pass** (`pass.isInitialized() == false`) reads as off even when the flag is on (the `isInitialized()` guard isn't lost to the flag/active name split).
8. **`GiFrameTimeWithinBudget`** (perf, Release/NDEBUG-gated + software-renderer skip) — extends `test_fog_benchmark.cpp`: the **primary gate** is the GI inject dispatch timed in isolation staying **≤ 0.4 ms** on the RX 6600 at 1080p (the cost R4 controls); it also reports the combined fog+GI dispatch against the fog's measured baseline. (Unlike R3, R4 adds a real compute pass, so the benchmark is concrete.)

### 11.7 Sources (R4)

- **Screen-space GI / colour injection:** the SSGI lineage (last-frame colour as indirect radiance) — see the R4 ROADMAP bullet's references; written-into-a-cache rather than gathered-per-pixel follows the froxel-clustering idea (Wronski, "Volumetric Fog," SIGGRAPH 2014).
- **Reflective shadow maps (Variant B off-screen bounce):** Dachsbacher & Stamminger, "Reflective Shadow Maps" (I3D 2005) — single-cascade flux-only subset.
- **Light-propagation volumes (Variant B propagation):** Kaplanyan, "Light Propagation Volumes in CryEngine 3" (SIGGRAPH 2010).
- **Probe-cached irradiance + temporal reprojection:** DDGI-family + the engine's own `SHProbeGrid` (static floor) and `VolumetricFogPass` (the froxel grid R4 co-locates the cache in — note the *fog* grid is rebuilt fresh each frame; R4 adds the cross-frame **reprojected** accumulation §11.2). Full synthesis rationale + honest-novelty framing in ROADMAP `3D_E-0015`.

---

## 12. Cold-eyes loop log

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

---

### R2 design-of-record — Loops 1–9 — 2026-06-19 (each loop a fresh reviewer, no authoring context, no briefing on prior-loop fixes)

Nine cold loops; every actionable severity (CRITICAL/HIGH/MEDIUM/LOW) verified against source and fixed before the next pass. Convergence: Loops 7–9 returned **zero** architectural/structural/mechanical defects — only citation-precision/wording polish — so per the convergence directive (and the owner's delegated sign-off) the loop is **converged** and R2 is signed off for implementation.

- **Loop 1** — *C (binding 5 free?)* Prev-bone SSBO at binding 5 — **false**: 5 used by particle-sort/cloth-dihedral, 6 by cloth-normals → moved to **binding 7** (no shader declares it). *H (stale Mesa-dummy cite `:760-764`)* → repointed to `:143-156`/`:778-782` (and the inherited R1 §4.2 cite). *M (early-return)* `update()` early-returns at `skeleton_animator.cpp:149-155` before `computeBoneMatrices()` — snapshot placement specified above the guard. *M (culled animators)* — verified **not** culling-gated (`entity.cpp:31-54`, gated by `m_isActive`/`isEnabled()` only).
- **Loop 2** — *C* §9.3 step 3 still said "binding 5" (steps 2/4 already fixed) → 7. *H* `Entity::update` signature + `isEnabled()` gate (`entity.cpp:44`) added to the Rule-13 verification. *M* "no `m_prev*` members" false (`m_prevRoot*` exist `:211-212`) → narrowed to "no previous *pose* snapshot". *M* `drawMesh` touch-points under-enumerated.
- **Loop 3** — *C* call-site mapping **inverted** (I'd labelled `:3442` opaque / `:3299` cloth) — corrected: `:3299` is the opaque single-instance skinned draw (before the `:3306` mask), `:3442` is the masked transparent pass, cloth uses `glDrawElements`. *H* binding-5/6 rationale imprecise. *M* `drawMesh` signature cite `:92-96`→`:92-97`. Added the §9.4 **Framebuffer third-attachment prerequisite** (analogue of R1 §4.0).
- **Loop 4** — 0 C / 0 H. *M* blit-location cite pointed at a comment (`:985-988`) → real blits `:830-834`/`:860-863`. *M* "binding 5/6 compute-only" — 5 also in `particle_gpu.vert.glsl:32`. *L* default-param idiom (`nullptr` + in-body fallback).
- **Loop 5** — *C (NEW, structural)* `buildInstanceBatches` keys on `(mesh,material)` only (`:2751`), `MIN_INSTANCE_BATCH_SIZE=2`, instanced path forces `u_hasBones=false` (`:3253`) → ≥2 skinned meshes sharing a mesh+material render at **bind pose** and never reach `:3299`. Added the **routing requirement** (exclude skinned/morphed items from instance grouping) + test #9; also a latent R1-era shading bug. *H* α-fit ownerless → added capture-set owner + ship-or-block gate. *M* α grazing-angle acceptance (test #10), resolve/combine unit scoping, blit contingency.
- **Loop 6** — 0 C. *H* prev-skinning GLSL snippet referenced block-local `vid`/`vc`/`loopCount` → specified hoist + mirror the forward block's `if (w!=0)` guard. *M* prev-morph mirror exactness; demote att2 `.a` "reserved for R4" to future-work; combine pass gains no samplers.
- **Loop 7** — 0 C / 0 H. *M* stale binding-decl cite `scene.vert.glsl:27,33,43` → `:47,56,41` (R1 §4.2). Softened "losslessly" (RGBA16F half-float); added key-widening mechanism steer.
- **Loop 8** — 0 C. *H* routing fix also changes **shaded** output → added visual-re-baseline note. *M* prev-pose seed precondition (bind-pose fill `:264,272`/`:370`) cited; *M* §9.4 third-attachment prose collapsed to a §4.0 reference. (One LOW — `InstanceBatch` range — **dismissed as unverified**: brace is at `:538`, cite already correct.)
- **Loop 9** — 0 C / 0 design defects. Verdict: *"§9 is a strong, implementable design-of-record."* *H/M* citation-precision only: push-site range spanned a `.clear()` (`:2774-2777` clarified to the `push_back` lines `:2775&2777`); `m_dummyMorphSSBO` def is `:200-201` (not in `143-156`); binding-5/6 grouping reworded. All fixed → **converged**.

### R3 design-of-record — Loops 1–4 — 2026-06-19 (each loop fresh reviewers, no authoring context, no briefing on prior-loop fixes)

Four cold loops; every actionable severity (CRITICAL/HIGH/MEDIUM/LOW) verified against source and fixed before the next pass. Convergence: Loop 4 returned **zero CRITICAL/HIGH/MEDIUM** — one LOW (perf-benchmark light count) + INFO only — so per the convergence directive (and the owner's delegated sign-off) §10 is **converged** and R3 is signed off for implementation.

- **Loop 1** (2 reviewers) — *H (math)* "front-scatter leaves the lit region unchanged / rises only across the terminator band" is **false**: `wrapped − lambert = wrap·(1−N·L)/(1+wrap)` is positive for all `N·L < 1`, so the whole lit gradient is lifted → §10.2 prose + test #2 corrected. *H* fold recipe said "add backScatter after the shadow multiply" but `calcSpotLightPBR` **applies no shadow** (`:897`, vs dir/point `:799/849`) → per-function fold written out. *H* clearcoat block `baseLighting *= (1-clearcoat*Fc)` (`:795/845/893`) interaction unspecified → stated (front-scatter inside baseLighting ⇒ attenuated; back-scatter outside). *M* §10.0 "no translucency anywhere" false (`foliage.frag.glsl:135`) → scoped to the scene PBR path. *M* test #6 "extract the full light function" infeasible — `extractGlslFunction` lifts one body, `calc*PBR` calls 5 helpers + `calcShadow` → reframed to helper-level + structural gate. *M* vec3 color clamp claimed to "match idiom" but `setAlbedo`/`setEmissive` are unclamped → color left unclamped. *L* garbled inline formula (`/PI-equivalent specular path`) deleted; `Ht`→`backLightDir`; "verbatim" softened (normalize deviation); engine.cpp "Phase 9" comment count.
- **Loop 2** (2 reviewers) — Loop-1 fixes held (none resurfaced). *C (NEW)* the front-scatter helper needs the **raw signed `dot(N,L)`**, but every call site's `NdotL` is `max(dot,0)` (`:764/816/872`) → clamped input kills all shadow-side bleed (and tests #2/#3 over the `[−1,1]` range become unreachable); fixed by computing `rawNdotL` in the fold and renaming the helper param. *H* two contradictory "diffuse becomes" formulas (the `:405` comment dropped `kD`/`/PI`) → reconciled. *H* the four `SSS_*` constants are free identifiers `extractGlslFunction` won't pull → declared file-scope `const` + parity harness prepends them. *M* vec3 color must deserialize via the existing `readVec3` helper, not `j.value` (which can't read a `glm::vec3`). *M* test #4 "view faces the light" label backwards → reworded to the precise `dot(V,−backLightDir)≤0` condition. *M* front-scatter shadowing claim inconsistent for the spot path → spot-unshadowed noted. *L* `max(wrapped−lambert,0)` flagged defensive.
- **Loop 3** (2 reviewers) — 0 C / 0 H. Loop-2 raw-`NdotL` CRITICAL did **not** resurface (held). *M* test #2 closed form `wrap·(1−N·L)/(1+wrap)` only valid on `N·L∈[0,1]`; the shadow-side branch is `(N·L+wrap)/(1+wrap)` → test #2 split into two branches. *M* fold insertion point unpinned (a literal "append at end" would skip clearcoat attenuation) → anchored "after `baseLighting=` (`:786/837/885`), before `if (u_clearcoat>0.0)` (`:789/840/888`)". *M* §10.6 perf gate not in the §10.8 test contract → added perf test #8. *M* `u_subsurface*` vs `u_pbr*` naming divergence → noted the `u_clearcoat` unprefixed precedent (`renderer.cpp:1620`). Dangling "see Open question" reference → added §10.10 deferred-items subsection.
- **Loop 4** (1 reviewer) — **0 C / 0 H / 0 M.** Reviewer independently re-derived both front-scatter branches (continuous at the terminator, both → `wrap/(1+wrap)`), verified the fold algebra, and confirmed every cited line number, the `readVec3` idiom, the clamp asymmetry, and `extractGlslFunction` single-body extraction. Verdict: *"implementation-ready."* *L* perf scene "all active point/spot lights" non-deterministic → pinned to a fixed 1 dir + 8 point + 4 spot set, split into a portable ≤0.5 ms delta arm and a dev-hardware ≤16.6 ms arm. All fixed → **converged**.

### R4 design-of-record — Loops 1–5 — 2026-06-19 (each loop fresh reviewers, no authoring context, no briefing on prior-loop fixes)

Five cold loops; every actionable severity verified against source and fixed before the next pass. Convergence: Loop 5 returned **zero CRITICAL/HIGH** — only polish-class MEDIUM (a citation precision + two add-a-sentence completeness items), no structural/mechanical/architectural defect — so per the owner's delegated sign-off (gate = cold-eyes clean *or* polish-only) §11 is **converged** and R4 (Variant A) is signed off for implementation. Five loops (vs R1's 4 / R2's 9 / R3's 4) reflects R4 being the heaviest, most novel slice; each loop's findings got shallower and the architecture was confirmed sound from Loop 2 on.

- **Loop 1** (2 reviewers) — *C (structural)* the froxel grid is **view-aligned** (re-maps to new world positions each frame), so the drafted "EMA-accumulate in the cache across frames" mixed radiance across world locations → added **froxel-cache reprojection** (read history at the froxel centre's previous-frame coord) + **mandated ping-pong**. *C-adjacent (H3)* injecting full last-frame HDR double-counts (it is full outgoing radiance, not a delta) → switched the injection source to a **diffuse-direct att3 MRT output** (excludes ambient/SH/specular/emissive) + honest `u_giStrength` overlap bound. *H* α must be exact + reduce-motion normative. *H-A* SH-floor quote was `+=` not `=` (it's an assignment). *M* test #6 extraction infeasible / read-site `gl_FragCoord` not `v_texCoord` / subset claim / dispatch-skip-when-off. Plus citation fixes.
- **Loop 2** (2 reviewers) — Loop-1 fixes held. Accuracy reviewer 0 C/0 H (all citations verified). *H (timing)* the inject runs **after** the current scene pass, so att3 is **current-frame** — the "project into last frame's screen" was wrong; no retained att3 copy needed. *H* `.a` confidence was computed but never read → wired into the per-fragment read (confidence-weighted). *H* `u_iblMultiplier` shouldn't scale the GI term → dropped. *M* self-illumination hand-waved → honest steady-state bound; clearcoat read-modify-write attenuates the GI term (noted intended); `clearFourthAttachment` named.
- **Loop 3** (2 reviewers) — *C* the Loop-2 rewrite mislabelled the reprojection matrix: `m_lastView`/`m_lastProjection` are **this** frame's; the previous VP is **`m_prevViewProjection`** (`renderer.cpp:1521`) → corrected + noted it's not in `FrameParams` today. *H* reduce-motion `α=0` alone doesn't freeze (the warm-invalid branch decays `.a` independently) → zero **both** α and decay; test #2 asserts read-**output** invariance. *M* depth-match tolerance undefined → ±½ froxel-slice thickness; att3 clear value `vec4(0)`; `u_dynamicGiEnabled = dynamicGiActive` + test #7 uninitialised-pass; clearcoat quoted verbatim (`ccFresnel`).
- **Loop 4** (2 reviewers) — 0 C / 0 architectural. *M* `m_prevViewProjection` is refreshed only inside `if(isTAA)` (`:1521`) → R4 must hoist it unconditional (stale in MSAA/SMAA/None). *H* `u_resolution` is an unlisted prerequisite — `scene.frag.glsl` has none; the read formula needs it added + wired from `m_windowWidth/Height` → called out alongside the other new scene-pass inputs. *L* cascade cite tightened to `:42-43`.
- **Loop 5** (2 reviewers) — **0 C / 0 H.** Accuracy: "otherwise accuracy-clean" — every line/constant/assignment-vs-RMW/unit-budget claim verifies. Design: "implementation-ready on technique, energy model, composition, reprojection, and tests (all source-verified)." Polish only: *M* `v_viewDepth` formula attributed to `.frag` but written in `scene.vert.glsl:230` (declaration stays `:20`); *M* frame-0 `m_giHistoryTex` cold-start (GL-clear both textures to `vec4(0)` at allocation); *M* ping-pong swap timing (end-of-frame, after inject). All three fixed → **converged**.
