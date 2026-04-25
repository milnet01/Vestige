# Vestige Engine Changelog

All notable engine-level changes are documented here. Per-tool changelogs
live alongside each tool (`tools/audit/CHANGELOG.md`,
`tools/formula_workbench/CHANGELOG.md`).

The engine version tracks phase milestones, not SEMVER. Pre-1.0 commits
may change any interface without notice.

## [Unreleased]

### 2026-04-25 Phase 10.9 — Slice 4 R10 (motion-overlay prev-world cache — unconditional clear)

The third item of Slice 4. The 2026-04-23 ultrareview flagged that
`Renderer::renderScene` updates the per-entity prev-frame world
matrix cache (`m_prevWorldMatrices`) only inside the
`if (isTaa)` end-of-frame block. Non-TAA modes (MSAA / SMAA /
None) therefore never touch the cache: a previously-populated
cache from a TAA session stays in memory across mode switches,
and a future toggle-back to TAA reads those stale matrices on
the per-object motion overlay path for entityIds that may have
been freed and reused by unrelated meshes in between.

**Fix.** Lifted the cache update into a templated free helper:

```cpp
template <typename ItemRange>
void updateMotionOverlayPrevWorld(
    std::unordered_map<uint32_t, glm::mat4>& cache,
    bool isTaa,
    const ItemRange& renderItems,
    const ItemRange& transparentItems);
```

Body: `cache.clear()` runs unconditionally; the per-entity
populate from `renderItems` + `transparentItems` runs only when
`isTaa` is true. Templated on `ItemRange` so production passes
`std::vector<SceneRenderData::RenderItem>` and tests pass a duck-
typed `std::vector<MockRenderItem>` without pulling
`scene/scene.h` into the test target. `Renderer::renderScene`
refactored to call the helper between the existing TAA
`swapBuffers` / `nextFrame` block and the `m_currentRenderData =
nullptr` clear; the original outer `if (isTaa)` was split into
two halves so the helper sits in the middle.

**Why TAA-gated populate, not unconditional populate.** The
cache is read by the per-object motion overlay only on the TAA
path (`renderer.cpp:980` — gated by the same `else if (isTaa)`
branch as line 980's enclosing block). Populating it in non-TAA
modes would be useless work. The clear, however, must be
unconditional — that's the only way to close the cross-mode-
switch staleness window.

**Tests.** 8 new `MotionOverlayPrevWorld.*_R10` cases in
`tests/test_motion_overlay_prev_world.cpp`:

- `NonTaaModeClearsCache_R10` — the headline R10 invariant.
- `NonTaaModeWithItemsStillClears_R10` — non-TAA never populates
  even when current frame has items to track.
- `TaaModeClearsAndPopulatesFromCurrent_R10` — TAA mode clears
  stale entries and brings in current ones.
- `TaaModeIncludesTransparentItems_R10` — both vectors traversed.
- `EntityIdZeroIsSkipped_R10` — preserves the existing sentinel
  behaviour.
- `EmptyRenderDataIsTaaClearsCache_R10` — even TAA with no items
  must clear (the previous frame's entries are no longer current).
- `RepeatedCallsConvergeOnLatestFrame_R10` — subsequent populate
  overwrites prior entries.
- `ModeSwitchTaaToNonTaaWipesCache_R10` — explicit reproduction
  of the cross-mode-switch staleness scenario.

The RED commit (`cea1d96`) shipped an empty stub body; all 8
tests failed. The GREEN commit (`b7d7858`) replaced the body with
`cache.clear()` + the TAA-gated populate; all 8 pass.

**Bump.** VERSION 0.1.31 → 0.1.32. Full suite: 2965 / 2966 pass
(+8 vs R7's 2957; the pre-existing
`MeshBoundsTest.UploadComputesLocalBounds` skip is unchanged).

**Slice 4 status post-R10: R1, R7, R10 shipped; R2, R3, R4, R5,
R6, R8, R9 open.** Next likely candidates: R3 (shadow-pass state
save/restore — same RAII shape as R1), R9 (Bloom Karis weights
math fix — pure-CPU testable like R7), or R6 (Mesa sampler-
binding fallbacks at four sites).

### 2026-04-25 Phase 10.9 — Slice 4 R7 (SH probe grid double cosine-lobe — fix is real)

The second item of Slice 4. The 2026-04-23 ultrareview flagged that
`Renderer::captureSHGrid` runs `projectCubemapToSH` then
`convolveRadianceToIrradiance` on the CPU before upload, and the
shader at `scene.frag.glsl::evaluateSHGridIrradiance` evaluates SH
using Ramamoorthi-Hanrahan 2001 Eq. 13 with constants `c1..c5` that
have the per-band cosine-lobe weights `A_ℓ` already folded in
(`c4 = √π/2 = A_0 × Y_00`). The CPU and shader paths were therefore
each multiplying by `A_ℓ` independently — band-0 ambient came out
exactly π× too bright. Every PBR material since Phase 4 has been
lit through this corrupted ambient.

A preliminary research read flagged R7 as a possible closed-wrong-
premise item (mirror of W10 contact-shadows). That read was wrong.
The RED test exhibited the bug at exactly the predicted magnitude:
input radiance `0.5` produced shader output `1.5707986`, which is
`0.5 × π` to seven significant figures.

**Fix.** Took Option 1 of the two roadmap-recommended patches: drop
the CPU-side `convolveRadianceToIrradiance` call; store radiance-SH;
let the shader's Eq. 13 (already designed for radiance-SH input by
the choice of `c1..c5`) produce irradiance.

**Code shape.**

  * `SHProbeGrid::computeProbeShFromCubemap(cubemap, faceSize, out)`
    — new public static helper that captures the entire CPU
    pipeline. Body is just `projectCubemapToSH`. Replaces the two-
    line inline sequence inside `Renderer::captureSHGrid`, so test
    and production share one code path.

  * `SHProbeGrid::evaluateIrradianceCpu(coeffs, normal)` — new
    public static helper that mirrors `evaluateSHGridIrradiance` in
    `scene.frag.glsl` byte-for-byte. CPU spec + GPU runtime
    (CLAUDE.md Rule 12) pinned by parity tests. Lives in production
    code (not test-only) so any future shader change has to update
    it.

  * `Renderer::captureSHGrid` now calls
    `SHProbeGrid::computeProbeShFromCubemap` instead of inlining
    `projectCubemapToSH` + `convolveRadianceToIrradiance`. No GL
    state changes; pure refactor.

  * `SHProbeGrid::convolveRadianceToIrradiance` stays declared.
    Docstring updated to flag the "legacy / unused" status. The
    helper has no production caller after R7 but its unit tests
    (`ConvolveAppliesRamamoorthiCoefficients`, `ConvolveZeroInputStaysZero`)
    pin the per-band `A_ℓ` constants which remain canonical
    reference material. Removing the helper would lose those tests
    for ~zero gain.

  * `setProbeIrradiance` / `getProbeIrradiance` docstrings now say
    "radiance-SH after R7"; the C++ signatures stay the same for
    API stability. The historical method name is retained because
    the storage-class change is a quiet semantic shift, not an
    interface change.

**Why Option 1 over Option 2.** Option 2 (replace shader `c1..c5`
with pure `Y_ℓm` basis and keep the CPU convolution) is also
mathematically valid — it just inverts which side of the pipeline
folds in `A_ℓ`. The reasons to prefer Option 1:

  1. Ramamoorthi-Hanrahan Eq. 13 is the canonical optimised
     evaluator for cosine-lobe-convolved diffuse SH. Replacing it
     with the unconvolved basis evaluator means we can no longer
     use the famous `c4 = √π/2` shortcut.

  2. The CPU side already runs the cubemap face capture and the SH
     projection per probe; adding a per-probe convolution multiply
     is wasted work when the shader is going to do equivalent work
     anyway. Option 1 deletes work, not moves it.

  3. The R2 follow-on (GPU compute SH projection — also under
     Slice 4) has a cleaner shape if the CPU pipeline is just
     "project," since the GPU compute path can mirror that
     directly without a second `A_ℓ` multiply pass.

**Tests.** 4 new `ShProbeGridTest.*_R7` cases in
`tests/test_sh_probe_grid.cpp`:

  - `UniformCubemapEndToEndEqualsRadiance_R7` (the headline) —
    asserts that for a uniform-radiance cubemap, the production
    CPU pipeline + shader-equivalent evaluator returns the input
    radiance for any normal. Failed RED with `e_x.r = 1.5707986`
    vs expected `0.5` (bug factor exactly π). Passes GREEN with
    `e_x.r ≈ 0.5`.

  - `UniformCubemapShaderEvalDirectionIndependent_R7` — direction
    independence for a uniform input. Passed RED too because π is
    a uniform multiplicative factor, but is now a regression pin
    against any future asymmetry bug.

  - `UniformCubemapEvaluatorIsLinearInRadiance_R7` — doubling the
    input radiance must exactly double the output irradiance. Same
    "passed RED, regression pin GREEN" status as the directional-
    independence test.

  - `ZeroCubemapEndToEndEqualsZero_R7` — zero in produces zero out
    (NaN guard).

**Visual impact.** Every PBR material since Phase 4 has been lit
with band-0 ambient that is π× too bright. Post-fix, the diffuse-
IBL contribution drops to one π-th of its previous value. Auto-
exposure and tone mapping may mask the change in some scenes; the
post-fix value is the scientifically correct one. Expect:

  - Indoor / overcast scenes: visibly darker, more contrasty.
  - Outdoor sunlit scenes: less visible difference because direct
    lighting dominates over the corrupted ambient term.
  - Pre-Phase 6 emissive-only test scenes: roughly the same
    because the SH grid is unused there.

**Bump.** VERSION 0.1.30 → 0.1.31. Full suite: 2957 / 2958 pass
(+4 vs R1's 2953; the pre-existing
`MeshBoundsTest.UploadComputesLocalBounds` skip is unchanged).

**Next.** Slice 4 R3 (shadow-pass state save/restore for
`GL_CLIP_DISTANCE0` + `GL_DEPTH_CLAMP`, extending `ScopedForwardZ`
or a sibling RAII), or R2 (GPU compute SH projection — would now
build on the cleaner radiance-SH CPU pipeline shipped here), or
R6 (Mesa sampler-binding fallbacks at four sites).

### 2026-04-25 Phase 10.9 — Slice 4 R1 (IBL capture paths wrapped in ScopedForwardZ)

The first item of Slice 4 (Rendering correctness). The 2026-04-23
ultrareview flagged that `EnvironmentMap::generate` and
`LightProbe::generateFromCubemap` run cubemap-face render passes
without a `ScopedForwardZ` bracket, plus the init-time first-
generation call at `renderer.cpp:683-692` has no save/restore at
all. `glClipControl` is global state — without an outer guard,
either the IBL passes render against a reverse-Z depth buffer
(corrupting the captured cubemap's depth-tested geometry) or the
engine is left in forward-Z afterward (corrupting every subsequent
scene draw, since the engine's main scene path runs reverse-Z with
`GL_GEQUAL` + clearDepth 0.0). Every PBR material rendered since
Phase 4 has therefore been lit with potentially-corrupted
irradiance / prefilter values when the init-time generate ran
between any prior scene draw and the next.

**Fix.** Lifted the bracket pattern into a shared helper
`engine/renderer/ibl_capture_sequence.{h,cpp}`:

```cpp
template <typename Guard, typename StepsList>
void runIblCaptureSequenceWith(const StepsList& steps)
{
    Guard guard;
    for (const auto& step : steps)
        if (step) step();
}

void runIblCaptureSequence(std::initializer_list<std::function<void()>> steps);
```

The template form is testable without a GL context — tests inject
a `RecordingGuard` whose ctor pushes "BEGIN" and dtor pushes "END"
to a per-test trace. The non-template overload fixes
`Guard = ScopedForwardZ` and is what production callers use. Both
live in the same translation unit so the production caller pulls
in the GL header transitively only when it instantiates the
non-template form.

**Call sites.**

* `EnvironmentMap::generate` — the four sub-passes (capture,
  irradiance convolution, GGX prefilter, BRDF LUT) are now lambdas
  inside one `runIblCaptureSequence({...})` call. Per-pass
  `glGetError()` drains move into each lambda so the existing
  diagnostic path is byte-for-byte preserved. Texture-delete
  cleanup at the top of `generate` runs outside the guard (no GL
  state churn that needs forward-Z; matches how the prior code
  structured the early reset).

* `LightProbe::generateFromCubemap` — irradiance + prefilter
  sub-passes wrapped in the same helper.

* `renderer.cpp:683-692` (init-time first-generation) — no
  caller-side change required. The guard now lives inside
  `EnvironmentMap::generate`, so the init-time call inherits the
  bracket. Closes the ROADMAP R1 note "currently no save/restore
  at all" at that line range.

**Why a helper, not inline `ScopedForwardZ`.** Both IBL entry
points run a sequence of 2-4 sub-calls under one bracket. A bare
inline `ScopedForwardZ guard;` would work but the identical
pattern at two call sites is the textbook case for CLAUDE.md
Rule 3 "Reuse before rewriting". The helper also makes the
bracket contract testable in isolation — the template form
accepts any `Guard` type, so a test can verify "guard opens
before any step, closes after every step has returned" without
needing a real GL context.

**Tests.** 6 new `IblCaptureSequenceTest.*_R1` cases in
`tests/test_ibl_capture_sequence.cpp`:

- `EmptyStepsListStillBracketsGuard_R1` — guard's ctor + dtor run
  even with zero steps (so a `generate()` that early-outs on a
  missing input still restores GL state correctly).
- `GuardOpensBeforeFirstStep_R1` — BEGIN appears before the first
  step in the trace.
- `StepsRunInOrderBetweenBeginAndEnd_R1` — capture / irradiance /
  prefilter / BRDF-LUT order is preserved.
- `GuardDestructsAfterLastStep_R1` — END appears after the last
  step (the load-bearing half: the post-generate scene draw
  needs reverse-Z to be already restored).
- `NullStepIsSkippedWithoutThrowing_R1` — null `std::function`
  entries are skipped without disturbing the bracket.
- `GuardLifetimeContainsEverySingleStep_R1` — strong-form
  invariant: every "STEP_*" entry's index in the trace lies
  strictly between BEGIN's and END's.

The RED commit (`e27e53e`) shipped a stub body that ran the steps
without constructing the guard; all 6 tests failed (no BEGIN, no
END). The GREEN commit (`c570101`) replaced the stub with the
real `Guard guard;` line and refactored the two IBL entry points
to call the helper; all 6 pass.

**Scope boundary.** Production-side parity (rendering a probe
with + without the wrap and diffing the prefilter output, as the
ROADMAP described) requires a GL test harness this project
doesn't yet have. The helper's bracket contract is unit-tested in
isolation; the production call sites inherit the contract by
virtue of using the same template, and the IBL capture itself
(face render loop, mip-chain prefilter, etc.) is unchanged from
its pre-R1 shipped form. Visual verification of the demo scene
post-R1 is the manual confirmation step.

**Bump.** VERSION 0.1.29 → 0.1.30. Full suite: 2953 / 2954 pass
(+6 vs P6's 2947; the pre-existing
`MeshBoundsTest.UploadComputesLocalBounds` skip is unchanged).

**Next.** Slice 4 R2 (GPU compute SH projection replacing per-face
`glReadPixels` + CPU projection in `captureSHGrid` — Editor "Bake
GI" moves from ~1 FPS to full pipeline speed) or R3 (shadow-pass
state save/restore for `GL_CLIP_DISTANCE0` + `GL_DEPTH_CLAMP`,
extending `ScopedForwardZ` or a sibling RAII).

### 2026-04-24 Phase 10.9 — Slice 2 P6 (narrator styling — both paths)

The last open Phase 10.9 Slice 2 item. `PHASE10_7_DESIGN.md §4.2`
originally called for "Narrator — italic white" but the project has
never shipped an italic font file, so the §4.2 compliance claim was
open (neither the spec nor an alternative was actually rendered).
P6 resolves the block by shipping both paths and handing the choice
to the game developer at the integration seam.

**Fix.** `SubtitleNarratorStyle { Italic, Colour }` exposed as a
setter on `SubtitleQueue` alongside `SubtitleSizePreset`:

1. `Italic` — the §4.2 original. White text rendered via the new
   `TextRenderer::renderText2DOblique`, which shears each glyph
   quad's vertices at emit time using a pure helper
   `text_oblique::applyShear(x, y, baselineY, factor)` — no second
   font atlas needed. Standard ~11° typographic oblique
   (`DEFAULT_SHEAR_FACTOR = 0.2f`). The upright path (`factor = 0`)
   stays byte-for-byte identical to the pre-P6 emit. Works on every
   font already loaded by the project.

2. `Colour` — warm-amber body (`{0.784, 0.604, 0.243}`, the theme-
   accent family), upright. The new default — accessibility-first.
   Italic typography is harder to read at low vision than colour
   differentiation; Vestige's primary user is partially sighted, so
   amber-upright is the correct default. Developers who prefer the
   original §4.2 aesthetic opt in via
   `queue.setNarratorStyle(SubtitleNarratorStyle::Italic)`.

**Rendering path.** `styleFor(category, narratorStyle)` gained the
second parameter (defaults to `Colour` to match the queue default —
existing call sites compile unchanged). `SubtitleStyle` and
`SubtitleLineLayout` gained a `bool italic = false` flag;
`computeSubtitleLayout` reads `queue.narratorStyle()` and
propagates the flag. `renderSubtitles` branches per-row between
`renderText2D` (upright) and `renderText2DOblique` (italic). The
branch is closed over as a lambda so the wrapped-row and
fullText-fallback paths share a single call site.

**Shared impl.** `renderText2D` now delegates to private
`renderText2DImpl(..., shearFactor=0.0f)`;
`renderText2DOblique(..., shearFactor)` delegates to the same impl
with a non-zero factor. No duplication, and the fast path (upright,
the overwhelming majority of callers) is unchanged.

**Regression pins.** Dialogue's yellow speaker label and SoundCue's
cyan-grey body are unaffected by the narrator selector — only the
`SubtitleCategory::Narrator` branch of `styleFor` reads
`narratorStyle`. Three dedicated tests pin this. The pre-P6 test
`SubtitleRenderer.NarratorStyleIsWhite` described the old default
and has been updated to `NarratorStyleDefaultsToWarmAmber`; the
italic path's white colour is separately pinned in the new file.

**Updates `PHASE10_7_DESIGN.md` §4.2** to document both paths as
runtime alternatives (not spec vs revision) so future readers see
the shipping shape, not the historical block.

**Tests.** 14 new tests in `tests/test_subtitle_narrator_style.cpp`:
- 10 `SubtitleNarratorStyle.*` — default `Colour`, setter round-
  trip, colour path warm-amber-not-italic, italic path white-and-
  italic, two paths visually distinct, Dialogue unchanged by
  selector, SoundCue unchanged by selector, layout propagates
  italic when selected, colour layout not italic, dialogue never
  italic regardless of selector.
- 4 `TextOblique.*` — pure shear helper: at-baseline identity,
  above-baseline shifts right, below-baseline shifts left, zero
  factor identity.

**Bump.** VERSION 0.1.28 → 0.1.29. Full suite: 2947 / 2948 (+14 vs
S4's 2933; the pre-existing `MeshBoundsTest.UploadComputesLocalBounds`
skip is unchanged).

**Slice 2 status post-P6: 8 of 8 shipped. Slice 3 status: 9 of 9
shipped.** Phase 10.9 Slices 1–3 are now fully complete; next
slice is Slice 4 (Rendering correctness — IBL `ScopedForwardZ`
wrap, GPU SH projection, shadow-pass state save/restore, etc.).

### 2026-04-24 Phase 10.9 — Slice 3 S4 (UI keyboard navigation + focus ring)

The main-menu and pause-menu footer labels in `menu_prefabs.cpp` have
been advertising "UP DOWN NAVIGATE / ENTER SELECT / ESC QUIT" for a
phase, but no handler existed — a keyboard-only user (partially
sighted, trackpad-less, switch-access) could not actually traverse
or activate the menu. Only ESC was wired (through `applyIntent`);
Tab, arrows, Enter, Space all fell through to game bindings. This
is the missing XAG 102 (WCAG 2.1.1 Keyboard + 2.4.7 Focus Visible)
conformance on the menu path.

**Fix.** Four layers:

1. `UIElement::focused` — a public bool parallel to `interactive` /
   `visible`. Widgets read it at render time.

2. `UISystem` gained focus state + key routing:
   - `getFocusedElement()` / `setFocusedElement()` expose /
     manipulate the currently-focused element.
   - `handleKey(key, mods)` consumes GLFW key events:
     - `Tab` / `Down` / `Right`                     → next (wraps)
     - `Shift+Tab` / `Up` / `Left`                  → previous (wraps)
     - `Enter` / `KP_Enter` / `Space`               → fires focused onClick
   - Tab order is computed each keypress by walking the active
     canvas in insertion order, descending into children,
     skipping invisible subtrees and non-interactive elements.
     Modal traversal is trapped to the modal canvas — root
     elements are unreachable while a modal has any interactive
     child.
   - Returns `true` iff the key was consumed (caller swallows).
     Letter keys, F-keys, etc. return `false` so game bindings
     still receive them.

3. `UIButton::render` draws a focus ring outside the button bounds
   when `focused && !disabled`. Theme fields `focusRingOffset` and
   `focusRingThickness` were already defined (Phase 9C) and scaled
   by the accessibility preset; only the render-time opt-in was
   missing. Colour: `theme.accent`.

4. Live wire: `InputManager::keyCallback` now forwards the GLFW
   `mods` bitmask through `KeyPressedEvent` (new `int mods = 0`
   field with a default so existing scripting / subscriber code
   compiles unchanged). `Engine`'s `KeyPressedEvent` subscriber
   routes to `UISystem::handleKey` first when `getCurrentScreen()`
   is a menu screen (MainMenu / Paused / Settings) or any modal
   is on top; if `handleKey` returns true, the event is swallowed
   before game bindings see it. Repeats are allowed through the
   UI path so a held arrow auto-scrolls.

**Scope boundary.** Arrow keys step through tab order (one step
forward / backward) rather than spatial 2D-adjacency. Current menu
layouts are vertical columns where "Down = next below" is
semantically correct; a 2D-adjacency table is a Phase 10.9+
follow-up if side-by-side horizontal button rows come back.

**Tests.** 15 new tests in `tests/test_ui_focus_navigation.cpp`:
- 2 `UIElementFocus.*` — field default + mutability.
- 13 `UISystemFocus.*` — default-null focus, Tab advances / wraps /
  reverses with Shift, Tab skips non-interactive panels, arrow
  keys mirror Tab forward / back, Enter + Space fire focused
  onClick, Enter-without-focus returns false (lets game code
  receive it), advancing focus flips the previous element's flag
  off and the new one's on, modal canvas traps focus, unhandled
  keys (letters, F-keys) return false.

**Bump.** VERSION 0.1.27 → 0.1.28. Full suite: 2933 / 2934 (+15 vs
S2's 2918; the pre-existing `MeshBoundsTest.UploadComputesLocalBounds`
skip is unchanged).

**Slice 3 status post-S4: 7 of 9 shipped.** Remaining: **P6**
(narrator styling — italic atlas vs colour, blocked on asset-source
decision). **S2** (component mutation) and **S4** (keyboard nav)
complete.

### 2026-04-24 Phase 10.9 — Slice 3 S2 (Scene deferred-mutation queue)

An `OnUpdate` script-graph node that calls `SpawnEntity` or
`DestroyEntity` runs *inside* the per-frame scene walk — the node's
entity is reached by `Entity::update`, which calls the script
component, which calls `scene->createEntity` / `scene->removeEntity`.
Both methods used to mutate the walked hierarchy immediately:
`removeEntity` erased from the parent's `m_children` vector while the
outer `forEachEntity` iterator was advancing through it (classic
iterator invalidation = UB); `createEntity` `push_back`-ed into the
root's children vector which could reallocate and invalidate the same
iterator. This hadn't crashed yet only because no Phase 11 AI had
actually run scripts that do this yet — the first frame Phase 11B
enemy AI destroyed an enemy entity or spawned a projectile from a
script, it would.

**Fix.** `Scene` now tracks an update-depth counter (`m_updateDepth`).
`Scene::update` and `Scene::forEachEntity` wrap their traversals in a
new RAII helper `Scene::ScopedUpdate` which increments the counter on
entry and decrements on exit. While the counter is greater than zero,
the three mutation entry points (`createEntity`, `removeEntity`,
`duplicateEntity`) queue their work instead of applying it:

- Adds push a `{std::unique_ptr<Entity> owner, Entity* parent}` pair
  onto `m_pendingAdds`; the entity is indexed eagerly in
  `m_entityIndex` so a subsequent `findEntityById` in the same frame
  resolves (matches the SpawnEntity-writes-output, LaterNode-reads-
  input script-graph shape).
- Removes push the id onto `m_pendingRemovals`; the entity stays
  reachable to the in-flight traversal (the visit does not skip it —
  removal semantics are "apply after the current walk completes", not
  "drop from the walk").

When the outermost `ScopedUpdate` releases (depth → 0),
`drainPendingMutations` applies queued adds first (so a spawned-then-
destroyed entity flows through the normal `unregisterEntityRecursive`
+ `removeChild` path at drain — re-using the S1 active-camera null-out
for free), then coalesced removes (duplicate ids are deduped via a
local `std::unordered_set<uint32_t>` so two handlers destroying the
same target don't double-destroy).

**Immediate path unchanged.** `removeEntity` / `createEntity` called
outside any update pass (editor timeline, deserialiser, unit tests
using the Scene API directly) go down byte-for-byte the same code
path as before — no behavioural change for those callers.

**Tests.** 12 new tests in `tests/test_scene_deferred_mutation.cpp`:
- 3 plumbing: `isUpdating()` default-false, `forEachEntity` auto-
  wraps it true, `ScopedUpdate` nests safely (inner release doesn't
  drop depth to zero).
- 3 removal-during-traversal: self-destroy keeps the rest of the walk
  alive, unvisited-sibling removal is deferred-not-skipped, remove-
  outside-update stays immediate.
- 3 creation-during-traversal: spawn-mid-walk doesn't crash and the
  spawn is NOT seen by the current pass (no infinite loops), the
  spawn is reachable via `findEntityById` immediately, the next pass
  walks it.
- 1 interleaved spawn + destroy: both queue kinds drain on release.
- 1 idempotency: two `removeEntity(sameId)` calls mid-walk drain
  safely (only one destroy).
- 1 hierarchy: destroying a deep child during its own visit is
  deferred-not-immediate.

**Bump.** VERSION 0.1.26 → 0.1.27. Full suite: 2918 / 2919 (+12 vs
S9's 2906; the pre-existing `MeshBoundsTest.UploadComputesLocalBounds`
skip is unchanged).

**Next.** Remaining Slice 3: S4 (keyboard nav + focus ring for XAG
102 conformance). P6 (narrator styling) still blocked on asset-source
decision.

### 2026-04-24 Phase 10.9 — Slice 3 S9 (UITheme WCAG contrast)

The Vellum and Plumbline default palettes shipped in Phase 9C both
fell below WCAG 2.2's contrast floor for two specific fields —
`panelStroke` at ~1.6:1 composited (needs 3:1 per 1.4.11) and
`textDisabled` at ~2.4:1 (needs 4.5:1 per 1.4.3 comfort). The
shortfall had gone uncaught because there was no arithmetic check
on palette correctness — only visual review.

**Fix.** Two parts:

1. New `Vestige::ui_contrast::` free-function namespace with three
   WCAG 2.2 primitives — `relativeLuminance(srgb)`,
   `contrastRatio(a, b)` (symmetric), and `compositeOver(fg, bg)`
   (straight-alpha blend so alpha-modulated fields are measured
   against the rendered pixel rather than the raw source).

2. Palette bumps on both registers:
   - Vellum `textDisabled` (0.361, 0.329, 0.278) → (0.560, 0.520,
     0.440): 2.4 → 4.84:1 against `bgBase`.
   - Vellum `panelStroke.a` 0.22 → 0.48: composited 1.6 → 3.23:1.
   - Vellum `panelStrokeStrong.a` 0.48 → 0.72 to keep
     hover/active visibly louder than at-rest.
   - Plumbline `textDisabled` → (0.570, 0.550, 0.520): 2.1 → 5.82:1.
   - Plumbline `panelStroke.a` 0.12 → 0.45: 1.3 → 3.96:1.
   - Plumbline `panelStrokeStrong.a` 0.36 → 0.68.

The high-contrast register already cleared WCAG 2.2 AAA (7:1 body
text); tests now pin that so future edits can't regress it.

**Visible design shift.** Panel borders are now plainly visible
rather than decorative hairlines. Intentional per ROADMAP S9 —
"load-bearing for partially-sighted primary user". The
hover-louder-than-rest invariant is preserved and pinned by test
on both registers so any future palette tuning keeps the visual
convention.

**Tests.** 16 new tests in `tests/test_ui_theme_accessibility.cpp`:
- 8 `UIContrast.*` helper-math: luminance endpoints (black = 0,
  white = 1), canonical black-on-white = 21, self-contrast = 1,
  order independence, composite alpha-zero / alpha-one /
  half-alpha identities.
- 8 `UIThemeContrast.*` palette-WCAG: textDisabled ≥ 4.5:1 and
  panelStroke ≥ 3:1 on Vellum + Plumbline + HighContrast, plus
  the hover-louder invariant on both non-HC registers.

**Bump.** VERSION 0.1.25 → 0.1.26. Full suite: 2906 / 2907 (+16
vs S8's 2890; the pre-existing
`MeshBoundsTest.UploadComputesLocalBounds` skip is unchanged).

**Next.** Remaining Slice 3 items: S2 (component-mutation-inside-
update contract), S4 (keyboard nav + focus ring). P6 (narrator
styling) remains blocked on asset-source decision.

### 2026-04-24 Phase 10.9 — Slice 3 S8 (NavMeshQuery partial-result surface)

`NavMeshQuery::findPath` used to collapse Detour's `DT_PARTIAL_RESULT`
flag into the returned vector's emptiness — success-but-partial looked
identical to success-but-complete, so an agent routed to a target
inside a disconnected nav-island silently arrived ~20m short with no
hook for AI to notice or re-plan.

**Fix:** `PathResult { waypoints, partial }` struct + new overload
`findPathWithStatus(start, end)` that propagates the partial flag.
Existing `findPath` now forwards to the new overload and drops the
flag, so `NavigationSystem::findPath` and any other legacy call
site compiles unchanged. Phase 11A behaviour trees and Phase 11B
enemy AI can migrate to `findPathWithStatus` and branch on
`result.partial` to re-plan, notify, or give up.

**Testability.** The Detour-status-to-bool translation is extracted
into `detail::isPartialPathStatus(dtStatus)` and exposed so bit
arithmetic is unit-testable without building a live Recast/Detour
nav mesh. The helper treats `DT_FAILURE` as dominant: a failed
query never reports partial even if the partial bit is incidentally
set, because failure means no waypoints and "partial path"
semantically requires a valid-but-short path.

**Tests.** 8 new tests in `tests/test_nav_mesh_query.cpp`:
- 5 `NavMeshPartialStatus.*` pin the helper: success-without-partial
  (false), success-with-partial (true), failure-with-partial
  (false — failure dominates), success-with-unrelated-detail-bit
  `DT_OUT_OF_NODES` (false — not confused with partial),
  bare-partial-bit-without-DT_SUCCESS (false — requires success).
- 3 `NavMeshQueryWithStatus.*` pin the uninitialised-query
  contract (empty waypoints, partial=false) and the legacy
  `findPath` overload backward-compat.

**Bump.** VERSION 0.1.24 → 0.1.25. Full suite: 2890 / 2891 (+8 vs
S6's 2883; the pre-existing
`MeshBoundsTest.UploadComputesLocalBounds` skip is unchanged).

**Next.** Remaining Slice 3 items: S2 (component-mutation-inside-
update contract), S4 (keyboard nav + focus ring), S9 (UITheme
default contrast per WCAG 1.4.3 / 1.4.11). P6 (narrator styling)
remains blocked on asset-source decision.

### 2026-04-24 Phase 10.9 — Slice 3 S1 + S3 + S6 (+ S5, S7 closed as duplicates)

Five Slice 3 safety items shipped together — three TDD red/green
cycles plus two ROADMAP checkboxes that fell out as duplicates or
side effects.

**S1: `Scene::removeEntity` nulls `m_activeCamera` on subtree removal.**
`m_activeCamera` is a raw `CameraComponent*`. Deleting the entity
that owned it left the pointer dangling; the renderer dereferences
it every frame, so "delete camera entity" from the editor was a
guaranteed crash on the next frame. Fix: `unregisterEntityRecursive`
checks `entity->getComponent<CameraComponent>() == m_activeCamera`
and nulls the pointer before destroying the entity. The recursion
handles both direct ownership and subtree ownership in one place.
4 tests: direct removal, descendant removal, unrelated-entity
guard, clearEntities invariant.

**S7 closed — duplicate of S1.** The ROADMAP listed the same
fix twice under different IDs. S1's recursion covers both.

**S3: `UIElement::hitTest` recurses into children.** Pre-fix,
UIElement::hitTest checked only self-bounds and returned — m_children
was never visited, so any widget placed as a child of another was
silently unreachable to mouse input. UICanvas::hitTest walked only
top-level elements and relied on their hitTest to descend (which it
didn't). Fix: restructured hitTest to (1) short-circuit on !visible,
(2) walk m_children first with parent-absolute-position cascading
as each child's parentOffset, (3) gate the self-bounds test on
interactive. Side effects:
- Children render on top of parents, so child-first hit order
  matches visual stacking.
- A non-interactive container (UIPanel's typical use case) whose
  children caught no hit returns false, letting input pass through.
8 tests in new `tests/test_ui_hit_test.cpp`: baseline hit/miss,
three nested cases (overflow parent, non-interactive parent, hidden
subtree), three-level deep cascade, canvas walks nested, canvas
outside all elements.

**S5 closed — shipped as S3 side effect.** UIPanel inherits the
interactive-gated self-bounds logic; no UIPanel-specific override
needed. Pinned by `UIHitTest.NestedInteractiveChildIsReachable-
ThroughNonInteractiveParent_S3`.

**S6: `PressurePlateComponent` overlap query uses world position.**
Pre-fix, the sphere overlap-query centre was computed from
`owner.transform.position` (local-space). Any plate parented under
another entity fired its trigger at the parent's origin, not the
plate's rendered location — elevators, vehicles, and rotating
platforms with pressure-plate triggers silently misbehaved.

Implementation choice: extracted a pure helper
`computePressurePlateCenter(owner, detectionHeight)` that composes
its own world matrix by walking the parent chain
(`for (const Entity* e = &owner; e; e = e->getParent()) world =
e->transform.getLocalMatrix() * world`). Calling
`Entity::getWorldPosition()` directly would have been simpler but
ties the centre to `Entity::update()` call timing — brittle inside
physics steps, editor preview, and unit tests where
`m_worldMatrix` may not be populated yet. Walking the parent chain
is timing-independent.

4 tests: unparented entity, child of translated parent, grandchild
cascading through full hierarchy, zero detectionHeight.

**Totals**

- 16 new tests across `test_scene.cpp`, `test_ui_hit_test.cpp`,
  `test_pressure_plate.cpp`.
- 2882 / 2883 pass (+16 vs T0's 2866; 1 pre-existing skip unchanged).
- Net +82 / -10 lines across 6 source files.

**Doc commit** (this entry) — VERSION 0.1.23 → 0.1.24. ROADMAP
S1, S3, S5, S6, S7 ticked; S5 and S7 entries explicitly flagged
as shipped-by-sibling rather than standalone work.

**Slice 3 status post-S1+S3+S5+S6+S7: 5 of 9 shipped.** Remaining
Slice 3 items: S2 (component-mutation-inside-update contract), S4
(keyboard nav + focus ring), S8 (NavMesh partial-result surfacing),
S9 (UITheme default contrast).

---

### 2026-04-24 Phase 10.9 — Slice 0 T0: ROADMAP truth-up (zombie-feature audit)

Documentation-only slice. Phase 10.9's premise — "Phase 10.7 features
passed tests but delivered subsets of the design" — applies recursively
to the ROADMAP itself: ~17 `[x]` DONE claims in Phases 7 / 8 / 9B / 9F-6
point at classes that compile, have tests, and have zero non-test call
sites. W12, W13, and W14 in Slice 8 are the remediation slices for those
zombies; they needed a grep-verified baseline to trust what they were
being asked to wire or relocate.

**Grep audit result:**

*Confirmed zombies (17):*
- Animation: `LipSyncPlayer`, `FacialAnimator` (orchestrator class —
  morph targets are live independently), `EyeController`, `MotionMatcher`,
  `MotionDatabase`, `MirrorGenerator`, `Inertialization::apply` → W12.
- Physics: `Ragdoll` class + presets + powered-ragdoll, `GrabSystem`,
  `StasisSystem`, `Fracture` / `BreakableComponent::fracture`,
  `Dismemberment` → W13.
- Editor panels: `SpritePanel`, `TilemapPanel` (neither is a member of
  `Editor`; their `draw()` is never invoked) → W14.
- Renderer: SSR pipeline (`m_ssrShader`, `m_ssrFbo`, `ssr.frag.glsl`) → W9,
  `GpuCuller::cull` → W11.

*False positive (1):*
- **Contact shadows** — W10 claimed "same dead-subsystem pattern as SSR"
  but is actually LIVE at `renderer.cpp:1162-1185` (stage 5c of the
  render loop), gated by `m_contactShadowsEnabled && m_contactShadowFbo
  && m_resolveDepthFbo && m_hasDirectionalLight`. W10 amended to
  closed-wrong-premise. The Phase 10.9 intro paragraph's claim of
  "SSR + contact-shadow converged" is half right — SSR is genuinely
  dead (W9), contact-shadow is not.

*Ambiguous (1):*
- `DestructionSystem::update` is a 41-line empty pump. The ISystem is
  registered, so it's not strictly a zombie class, but the subsystems
  it claims to own (Ragdoll, Fracture, GrabSystem, Dismemberment) are
  zombies. W13 still needs to drive them from a non-empty `update()`
  OR relocate the primitives.

**Documentation actions:**

1. Phase 7 header (line 402) — added T0 audit block note listing the
   animation-cluster zombies with a pointer to W12. Phase header
   retained as "(COMPLETE — foundation; animation zombie cluster tracked
   in Phase 10.9 Slice 8 W12)" because glTF skeletal playback, IK,
   skeletal state machines, and morph targets *are* live.
2. Phase 8 header (line 487) — added T0 block note listing the
   destruction/ragdoll/grab/stasis/dismemberment zombies → W13.
3. Phase 9B "Domain Systems to Wrap" header (line 710) — added T0 block
   note noting the wrapping ISystem is live but the wrapped primitives
   include W12/W13 zombies. `Destruction & Physics System` and
   `Character & Animation System` wraps affected.
4. Phase 9F-6 editor-panel bullet (line 883) — appended T0 note stating
   `SpritePanel` + `TilemapPanel` never reach `Editor::drawPanels` → W14.
5. W10 — reframed from "delete contact-shadow" to "closed — premise was
   wrong". No renderer code change.
6. T0 checkbox itself (line 1186) — ticked `[x]` with the full audit
   summary inline.

**Per-line `[x]` → `[ ]` demotion NOT performed.** The original
"class shipped + tests pass" half of each `[x]` claim *is* true; only
the implicit "and it runs at engine runtime" half is false. Demoting
per line would understate the primitive-level work that exists, and
W12/W13/W14 will be the honest resolution point — wire or relocate +
demote together.

**Doc commit** (this entry) — VERSION 0.1.22 → 0.1.23.

**Tests:** none added (documentation-only). Full suite unchanged at
2866/2867 pass, 1 pre-existing skip.

**Slice 0 complete.** Downstream slices (W12, W13, W14) now have a
grep-verified zombie list they can trust; the list will not grow under
them. Slice 1 (F1–F12) and Slice 2 (P1–P5, P7, P8 = 7/8) remain as
already-documented.

---

### 2026-04-24 Phase 10.9 — Slice 1 F12: save-time warning for unregistered components

Twelfth and final Slice 1 item. F3 (2026-04-23) introduced the
`ComponentSerializerRegistry` and migrated 7 built-in component
types (plus AudioSource) off the old fixed allowlist — but the
engine ships ~26 component types. The other ~18 (ClothComponent,
RigidBody, BreakableComponent, CameraComponent, SkeletonAnimator,
TilemapComponent, 2D physics / camera / sprite components,
InteractableComponent, PressurePlateComponent, GPUParticleEmitter,
FacialAnimator, TweenManager, LipSyncPlayer, NavAgentComponent,
CameraMode, etc.) had no registry entry, so every `serializeEntity`
call silently dropped them. A user saving a scene with a ClothComponent
got a JSON file with no `ClothComponent` block, and the next load
rebuilt the entity without it.

**The F12 design choice** — two options were on the table:

1. **Register all 18 types properly.** Correct, but forces 18 new
   round-trip implementations in one slice — each one a new bug
   surface, several depending on subsystems whose own remediation
   is scheduled in later Phase 10.9 slices.
2. **Loud save-time warning.** Make the silent-drop visible without
   adding 18 schemas. Individual types migrate into the registry
   in their own slices as their owning subsystems land.

**Chose option 2.** The drop path already exists — the registry just
needs to know when an entity owned more components than it managed
to serialise, and tell someone about it.

**Red commit** — 4 tests pin the warning contract in
`test_entity_serializer_registry.cpp`:

- Two test-local component subclasses
  (`UnregisteredTestComponent`, `OtherUnregisteredTestComponent`)
  stand in for the 18 real unregistered types.
- `WarnsWhenComponentTypeIsUnregistered` — single unregistered
  component → warning naming the entity.
- `SilentWhenAllComponentsAreRegistered` — entity with only
  registered AudioSource → no warning (false-positive guard).
- `ReportsDropCountForMultipleUnregistered` — two unregistered
  components → warning mentions "2".
- `WarnsForEachAffectedEntityIndependently` — two `serializeEntity`
  calls → two distinct warnings.

3 of 4 fail on shipping code; the negative case passes vacuously
because nothing warned to begin with.

**Green commit** — 16 lines added to `serializeEntity` in
`engine/utils/entity_serializer.cpp`:

- Added a `std::size_t registeredHits` counter, incremented every
  time `entry.trySerialize(entity, resources)` returned non-null
  inside the existing registry-dispatch loop.
- After the loop, compared `entity.getComponentTypeIds().size()`
  against `registeredHits`. When the entity's count is higher, the
  difference is the drop count and a `Logger::warning` fires
  naming the entity, the count, and the remediation path
  ("Register via `ComponentSerializerRegistry::instance().registerEntry(...)`
  or relocate to `engine/experimental/`").
- Child entities are checked via the existing recursion, so a
  parent with zero unregistered components doesn't mask a child
  that has some.

**Doc commit** (this entry) — VERSION 0.1.21 → 0.1.22.

**Tests**: 2866/2867 pass (+4 vs P8's 2862, 1 pre-existing skip
unchanged).

**Scope boundary**:
- The warning names the entity but not the component *types* —
  detection is count-based, not name-based. `Component` has no
  `getTypeName()` virtual today; adding one would force a 26-
  subclass override patch for marginal warning-text improvement.
  The entity name + drop count is sufficient for an operator to
  locate the scene and identify the missing types in a debugger
  via `getComponentTypeIds()`.
- The warning fires *per `serializeEntity` call*. Saving a scene
  with 100 affected entities produces 100 log lines. The F9 ring
  buffer caps at ~1000 entries, so any scene-save batching path
  that holds more than ~10 warnings per entity (none today)
  should consolidate.
- Load-side is unchanged — an unknown `components` key in JSON
  already produces a registry-miss warning in the deserialise
  path (from F3). F12 is symmetric: save now warns as loudly as
  load already did.

**Slice 1 complete** — F1…F12 all shipped.

---

### 2026-04-24 Phase 10.9 — Slice 2 P8: HRTF init-order fix + `HrtfStatusEvent` listener

Eighth and final Slice 2 item. Two latent issues in the HRTF layer
that Slice 1's audit flagged:

1. **Init-order bug**: `AudioEngine::initialize()` called
   `applyHrtfSettings()` *before* setting `m_available = true`.
   `applyHrtfSettings` guards on `!m_available` at the top, so the
   first pass silently short-circuited. A user who called
   `engine.setHrtfMode(Forced)` *before* `initialize()` had their
   preference stored in `m_hrtf` but never applied — the driver
   stayed in its default state until some later mid-session
   `setHrtfMode` call re-triggered the apply.

2. **No way to notice driver downgrades**: the Settings UI could
   display what the user *asked for* (from `getHrtfSettings()`) and
   what the driver *actually did* (from `getHrtfStatus()`), but only
   by polling both every frame. There was no event that said "the
   driver just reset, here's what it decided". A `Forced` request
   silently becoming `UnsupportedFormat` looked identical to a user
   who had never set a preference.

**Red commit** — listener contract pinned by tests:

- Declared `HrtfStatusEvent { HrtfMode requestedMode;
  std::string requestedDataset; HrtfStatus actualStatus; }` and
  `composeHrtfStatusEvent(settings, actualStatus)` in `audio_hrtf.h`.
  Red stub ignores inputs and returns a default `{}`, so the
  composition tests fail on field comparison.
- Declared `AudioEngine::setHrtfStatusListener(HrtfStatusListener)`
  in `audio_engine.h` with `m_hrtfStatusListener` storage, but
  deliberately did **not** wire `applyHrtfSettings()` to call it.
  Setter stores the listener, nothing fires.
- Added 8 tests across `test_audio_hrtf.cpp`:
  - 3x `AudioHrtfStatusEvent.*` — composer forwards mode / dataset
    / status (`Forced+KEMAR+Enabled`, `Forced+UnsupportedFormat`
    downgrade, `Auto+Unknown` for the uninit case).
  - 5x `AudioEngineHrtfStatusListener.*` — fires on set-mode from
    an uninit engine (status reads as Unknown), fires on set-dataset,
    no-fire on unchanged-value early-return, fires-once-per-change
    over a 3-change sequence, no-crash when no listener registered.

5 of 8 fail at runtime (the other 3 pass vacuously — the default
`HrtfStatusEvent{}` already has `actualStatus = Unknown`, and the
"unchanged" + "no listener" tests pass regardless of wiring).

**Green commit** — three changes, two files:

1. `composeHrtfStatusEvent` (audio_hrtf.cpp): now forwards
   `settings.mode → requestedMode`, `settings.preferredDataset →
   requestedDataset`, `actualStatus → actualStatus`. Four lines.
2. `applyHrtfSettings` (audio_engine.cpp): the ALC device-reset
   block stays gated on `m_available + m_alcResetDeviceSOFT`, but
   the listener call is moved *outside* the guard. Pre-init
   set-mode / set-dataset calls fire the listener with
   `actualStatus = Unknown` (from `getHrtfStatus()`'s own
   `!m_available` short-circuit), post-init calls carry the driver's
   real decision.
3. `initialize()` (audio_engine.cpp): `m_available = true` now runs
   *before* `applyHrtfSettings()`. The apply's own guard still
   protects against the extension being absent, but the `m_available`
   short-circuit no longer swallows the first pass.

**Doc commit** (this entry) — VERSION 0.1.20 → 0.1.21.

**Tests**: 2862/2863 pass; +8 vs P7's 2855 total (1 pre-existing
skip unchanged).

**Scope boundary**:
- The listener is a point-in-time notification. Registering *after*
  an `applyHrtfSettings()` call does not see prior events — callers
  subscribe once at panel construction.
- The event does not include the driver's dataset name. The
  Settings UI already reads that from `getHrtfSettings()`; no need
  to duplicate on every call.
- `alcResetDeviceSOFT` failure still emits a `Logger::warning` as
  before. The event still fires; `actualStatus` reflects whatever
  the driver reports after the failed reset (typically unchanged
  from before the attempt).
- Pre-init listener firing is deliberate: the Settings UI surfaces
  a user's requested mode as soon as they change it, even before
  the device opens. The `Unknown` actualStatus during that window
  is the honest answer — the driver hasn't had a say yet.

**Slice 2 status post-P8: 7 of 8 shipped.** P6 (narrator styling —
italic atlas vs. colour differentiation) remains open, blocked on
an asset-source decision.

---

### 2026-04-24 Phase 10.9 — Slice 2 P7: Voice-eviction wiring + priority on `AudioSourceComponent`

Sixth Slice 2 item. The Phase 10.4 `chooseVoiceToEvict` primitive
has existed as a tested pure function since the mixer shipped, but
the `AudioEngine` source-pool exhaustion path never called it.
Until P7, when all 32 slots were in use, every new `playSound*`
returned 0 and the caller got silence — no matter how important
the incoming sound was. Dialogue, boss stingers, and objective
audio (the Critical tier the priority enum was designed for) were
indistinguishable from a background footstep.

**Red commit** — admission-gate primitive + component field:

- Declared `chooseVoiceToEvictForIncoming(voices, incomingPriority)`
  in `audio_mixer.h` with a deliberately-wrong always-return-`-1`
  stub in the cpp. Added 8 spec tests to `test_audio_mixer.cpp`:
  empty-list sentinel across priorities, lower-incoming-loses,
  equal-incoming-loses-to-incumbent, strict-higher-incoming-wins,
  picks-lowest-keep-score-among-eligible, all-Critical-Critical
  ties-to-incumbent, lowest-score-victim-still-ineligible-falls-through,
  mixed-tier-High-evicts-Low.
- Added `SoundPriority priority = SoundPriority::High` (wrong
  default) to `AudioSourceComponent` + 3 tests in
  `test_audio_source_component.cpp` (default-is-Normal,
  assignable, clone-preserves). Clone intentionally not wired so
  the clone-preserves test fails too.
- Extended `test_entity_serializer_registry.cpp` `populateDistinctive`
  + `AudioSourceAllFieldsRoundTrip` with
  `asc.priority = SoundPriority::Critical` — serializer doesn't
  handle priority yet, so the round-trip fails.

6/12 red tests fail at runtime for the right reasons (stubs always
say "no eviction", default is High, clone drops priority,
serializer drops priority). 6 pass incidentally because the stub's
-1 matches the "incoming loses" cases.

**Green commit** — three layers of wiring:

- **Pure admission gate**: `chooseVoiceToEvictForIncoming` delegates
  to `chooseVoiceToEvict` for victim selection, then applies a
  strict-greater priority-tier check. Ties go to the incumbent so
  rapid same-priority bursts (e.g. a Normal footstep cluster)
  don't churn the pool.
- **Component + round-trip**: default restored to `Normal`;
  `AudioSourceComponent::clone()` copies priority;
  `entity_serializer.cpp` adds `soundPriorityToString` /
  `soundPriorityFromString` helpers and round-trips the field as a
  JSON string (`"Low"` / `"Normal"` / `"High"` / `"Critical"`). An
  absent field deserialises as `Normal` — pre-P7 scenes stay
  identical.
- **Engine wiring**: `AudioEngine::SourceMix` grows `priority` +
  `startTime` (std::chrono::steady_clock). `acquireSource` takes a
  `SoundPriority` parameter (default Normal for fire-and-forget
  callers) and on pool exhaustion walks `m_livePlaybacks` into a
  `VoiceCandidate` list (effective gain = `resolveSourceGain(mixer,
  bus, volume, duck)`, age = now − startTime), asks
  `chooseVoiceToEvictForIncoming` for a victim, releases that
  source, and retries the free-slot scan. If no voice qualifies
  (incoming tier ≤ every existing voice), returns 0 with a
  `Logger::warning` explaining why. All four `playSound*` overloads
  gained a trailing `SoundPriority priority = SoundPriority::Normal`
  parameter and pass it through to `acquireSource`; they also
  populate the expanded `SourceMix` with the priority + start
  timestamp. `AudioSystem` passes `comp->priority` when
  auto-acquiring component-driven sources.

12/12 new tests pass. Full suite 2855/2855.

**Scope boundary**: the eviction retry is one-shot — if the first
victim's releaseSource somehow fails to free a slot (pool state
desync), a defensive warning fires and 0 is returned rather than
looping. Retry-on-desync would hide a real bug; the one-shot + log
surfaces it. The victim's current playback state is not saved for
resume; an evicted voice is lost, which matches FMOD / Wwise
eviction semantics — the caller chose priority tiers to say "this
moment is more important than whatever that Low voice was doing".
Pre-P7 scene JSON without the `priority` field deserialises as
`Normal`, preserving existing authoring. No behavioural change for
callers that don't opt into `SoundPriority` — the default Normal +
equal-ties-to-incumbent admission rule means the only observable
difference for shipping code is that an incoming Normal sound no
longer silently drops when the pool happens to be full of a
smaller-tier voice; today that's a near-impossible scenario
because every shipping sound is Normal.

Version bumped 0.1.19 → 0.1.20.

### 2026-04-24 Phase 10.9 — Slice 2 P2: AudioSourceComponent per-frame pass

Fifth Slice 2 item and the biggest one — turns nine dead fields on
`AudioSourceComponent` into live AL state. Before P2, the audio path
only applied mixer × bus × duck × volume to sources acquired via
direct `AudioEngine::playSound*` calls; authored
`AudioSourceComponent` instances in the scene never had their
`pitch` / `velocity` / `attenuationModel` / `minDistance` /
`maxDistance` / `rolloffFactor` / `autoPlay` / `occlusionMaterial` /
`occlusionFraction` fields read, never had AL state pushed, never
even auto-acquired a source when marked `autoPlay=true`. Editor
users who placed a positional sound source in a scene heard nothing
until game code manually called `playSound*` for that clip path.

**Red commit `4c2f1c3`** — pure-compose contract first:

- New `engine/audio/audio_source_state.h`: `AudioSourceAlState`
  struct mirroring the per-frame `alSource*f` set + prototype
  `composeAudioSourceAlState(comp, entityPosition, mixer,
  duckingGain)`.
- New `engine/audio/audio_source_state.cpp`: deliberately-wrong
  stub returning `AudioSourceAlState{}` with position only.
- New `tests/test_audio_source_state.cpp`: 12 spec tests covering
  position / velocity / pitch / attenuation params / spatial flag /
  gain composition / occlusion (Air pass-through, Stone attenuation,
  fraction-zero clear-through) / full-chain regression.
- Wired into `engine/CMakeLists.txt` + `tests/CMakeLists.txt`.

Ten of twelve red tests fail at runtime on the stub; two pass
(position copy that the stub does do, and Air-material transmission
which equals the stub's default gain of 1.0).

**Green commit `a96cccd`** — four layers of green:

- **Pure compose**: full composition now runs through the occlusion
  / mixer / duck pipeline. Occlusion derives from
  `computeObstructionGain(openGain=1, material.transmissionCoefficient,
  fractionBlocked)` and folds into the `volume` input of
  `resolveSourceGain` (P3's 4-arg overload) so the existing
  mixer × bus × duck × clamp pipeline applies uniformly — no new
  clamp site.
- **AL state push**: `AudioEngine::applySourceState(source, state)`
  issues AL_POSITION / AL_VELOCITY / AL_PITCH / AL_GAIN /
  AL_REFERENCE_DISTANCE / AL_MAX_DISTANCE / AL_ROLLOFF_FACTOR /
  AL_SOURCE_RELATIVE. Same call shape as `playSoundSpatial`'s
  initial upload but callable every frame so runtime edits
  (pitch slider moved in editor, moving projectile updates
  velocity, door opens and flips occlusion material) are heard
  live rather than only on the next acquire.
- **Source-alive probe**: `AudioEngine::isSourcePlaying(source)`
  wraps `alGetSourcei(source, AL_SOURCE_STATE, ...)` so the reap
  pass doesn't reach into AudioEngine internals.
- **Per-frame iteration** in `AudioSystem::update`:
  `std::unordered_map<std::uint32_t, unsigned int> m_activeSources`
  + a `scene->forEachEntity` pass that (1) auto-acquires an AL
  source for any `AudioSourceComponent` with `autoPlay=true` and
  non-empty `clipPath` that isn't tracked yet, (2) pushes the
  composed state every frame for tracked entries via
  `applySourceState`, (3) reaps entries whose source has stopped
  or whose entity has disappeared from the scene. Exposed via
  `activeSources()` const accessor for test observation.

**Debt fixes folded into this commit (per "no debt whatsoever"):**

- **playSound* signatures**: every overload (playSound,
  playSoundSpatial×2, playSound2D) now returns `unsigned int`
  (the acquired AL source ID, or 0 on failure). The previous
  void return forced AudioSystem to guess or use a sentinel
  workaround. Fire-and-forget callers discard the return; per-frame
  trackers store it.
- **Latent duck-on-initial-upload bug**: the three `playSound*`
  implementations' initial AL_GAIN compose used the 3-arg
  `resolveSourceGain` and therefore did NOT apply the P3 duck
  snapshot on their first frame — only subsequent `updateGains`
  passes did. All three now pass `m_duckingSnapshot` so a sound
  acquired *during* a duck is audible at the ducked level from
  frame 1 rather than jumping to full gain for one frame before
  the duck catches up.

**Test coverage:**

- 12 pure-compose tests cover the full component → AL state
  mapping.
- 1 source-alive-probe test covers the reap-helper contract.
- 13 new tests total this cycle (2844 / 2844 pass, was 2831).

**Test suite: 2844 / 2844 passing** (1 pre-existing skip unchanged;
+13 new tests vs. P3's baseline of 2831).

**Files changed (green):** new `engine/audio/audio_source_state.{h,cpp}`
(+78 / -3 in green + red), modified `engine/audio/audio_engine.{h,cpp}`
(+61 / -28 — applySourceState, isSourcePlaying, playSound* return
types, duck-on-initial-upload), modified
`engine/systems/audio_system.{h,cpp}` (+110 / -2 — m_activeSources +
per-frame loop), new `tests/test_audio_source_state.cpp` (+248 / -0)
+ modified `tests/test_audio_mixer.cpp` (+14 / -0 for the probe test),
+ CMake wiring in two files.

**Net: +540 / -68 lines across ten files. Every field on
AudioSourceComponent now reaches AL.**

**Next in Slice 2:** **P7** (voice-eviction wiring —
`chooseVoiceToEvict` into `playSound*` retry when the source pool
is exhausted; adds `SoundPriority` to `AudioSourceComponent`),
then **P8** (HRTF init-order fix + `HrtfStatusChanged` device-reset
event). **P6 (narrator styling) is held for your decision.**

### 2026-04-24 Phase 10.9 — Slice 2 P3: ducking fold into `resolveSourceGain`

Fourth Slice 2 item. Closes the "feature ships but never actually
applies" gap for ducking: Phase 10.7's `DuckingState` /
`DuckingParams` / `updateDucking` shipped with full unit-test
coverage, but `resolveSourceGain` never accepted the ducking gain,
`AudioEngine::updateGains` never read a duck snapshot, and the
editor AudioPanel kept a *local* copy of the state that applied only
to its own preview. A user who enabled the Debug-tab "Ducked"
trigger saw the slew animate in the editor — and heard zero dip in
the actual mix. The state machine was a toy. P3 wires it to AL.

**Red commit `389af46` + amend `5fdf618`** — the math contract +
full wiring contract pinned before any green line:

- `resolveSourceGain(mixer, bus, volume, duckingGain)` 4-arg
  overload declared; cpp stub ignores the new parameter so the math
  tests fail at runtime (F10 / F11 / P1 discipline).
- `AudioEngine::setDuckingSnapshot(float)` /
  `::getDuckingSnapshot()` declared; cpp stub silently discards on
  set so `SetStoresClamped_P3` fails.
- `Engine::getDuckingState()` / `::getDuckingParams()` declared with
  `m_duckingState` / `m_duckingParams` members (defaults match
  `DuckingState{1.0f, false}` and `DuckingParams{0.08s, 0.30s, 0.35}`).

Five math tests + two snapshot tests + three panel-wire tests in
three test files: 5 of 10 fail at runtime on the stubs, 5 pass
(unity-defaults, no-op 3-arg compat, and the panel-local fallback
pin that holds with or without the fix).

**Green commit `2eda0ff`** — three-layer wire-up:

- **Math** (`audio_mixer.cpp`): 4-arg overload multiplies
  `clamp01(duckingGain)` after the existing
  `master × bus × volume`, then clamps the product to [0, 1].
- **Storage** (`audio_engine.cpp`): `setDuckingSnapshot` clamps on
  ingest (canonical [0, 1] downstream); `updateGains` threads
  `m_duckingSnapshot` through every `resolveSourceGain` call so
  every live source's `AL_GAIN` upload includes the duck.
- **Publish** (`audio_system.cpp`): `AudioSystem::update` advances
  `updateDucking(m_engine->getDuckingState(), ...)` by the frame
  delta, then publishes `currentGain` to
  `m_audioEngine.setDuckingSnapshot`. Lives in the same pass that
  publishes the mixer snapshot — one paired write per frame.
- **Single source of truth** (`audio_panel.{h,cpp}` +
  `engine.cpp`): `AudioPanel::wireEngineDucking(state*, params*)`
  mirrors the existing `wireEngineMixer` pattern. When wired, the
  panel's `duckingState()` / `duckingParams()` accessors read and
  write through the engine pointers; the Debug tab's trigger
  checkbox + attack/release/floor sliders mutate the authoritative
  state AudioSystem advances. `Engine::initialize` calls the wire
  alongside the mixer wire. The panel keeps a local fallback
  (panel-local `m_duckingState` + `m_duckingParams`) for standalone
  and test usage; `nullptr` wire args select the fallback.

**No debt left standing:**

- No stub cpp implementations remain (`resolveSourceGain` 4-arg
  body does the real math; `setDuckingSnapshot` stores the clamped
  value; Engine accessors return the real members).
- No duplicate state: Engine owns the DuckingState, AudioPanel
  reads through a pointer when wired. The `m_duckingState` on the
  panel is the fallback path explicitly documented as
  "standalone / test only", not a rival store.
- No TODO / FIXME / "wire later" comments added.
- Every new public symbol (`setDuckingSnapshot`,
  `getDuckingSnapshot`, `getDuckingState`, `getDuckingParams`,
  `wireEngineDucking`, 4-arg `resolveSourceGain`) has a doxygen
  comment pinning the contract.

**Test suite: 2831 / 2831 passing** (1 pre-existing skip unchanged;
+10 new tests vs. P4's baseline of 2821).

**Files changed (green):** `engine/audio/audio_mixer.cpp` (+8 / -1),
`engine/audio/audio_engine.cpp` (+9 / -1), `engine/audio/audio_engine.h`
(+22 / -0), `engine/systems/audio_system.cpp` (+13 / -1),
`engine/editor/panels/audio_panel.h` (+32 / -4),
`engine/editor/panels/audio_panel.cpp` (+5 / -5),
`engine/core/engine.h` (+12 / -0), `engine/core/engine.cpp` (+9 / -0),
`tests/test_audio_mixer.cpp` (+60 / -0),
`tests/test_audio_panel.cpp` (+52 / -0).

**Net: +222 / -12 lines across ten files. Phase 10.7's ducking
feature now actually applies to what the user hears.**

**Next in Slice 2:** **P2** — `AudioSystem` per-frame
`AudioSourceComponent` iteration pass (per-entity `m_activeSources`
map, per-frame `AL_POSITION` / `AL_VELOCITY` / `AL_PITCH` /
`finalGain` push). Brings the 11 dead fields on
`AudioSourceComponent` live. P3's duck snapshot is the gain hook
for P2's per-source compose.

### 2026-04-24 Phase 10.9 — Slice 2 P4: caption auto-enqueue on playSound*

Third Slice 2 item. Closes the Phase 10.7 slice B3 promise that the
design doc made and the unit tests never actually verified: *"When
an AudioSourceComponent plays a clip with a matching key, the audio
system auto-enqueues a caption."* `CaptionMap::enqueueFor()` was
implemented and tested in isolation, but nothing called it at
clip-play time — the B3 wiring was never built. Captions shipped
as a feature that only worked when game code manually called
`captionMap.enqueueFor(clip, queue)` alongside its `playSound` —
and no game code did.

**Red commit `e8c2308`** — new `CaptionAnnouncer` callback API on
AudioEngine + eight RED tests in `tests/test_caption_map.cpp` (the
natural home — the end-to-end integration test instantiates both a
CaptionMap and a SubtitleQueue there already):

- `using CaptionAnnouncer = std::function<void(const std::string&)>;`
- `AudioEngine::setCaptionAnnouncer(CaptionAnnouncer)` setter +
  private `m_captionAnnouncer` member (may be empty — purely
  optional hook, no engine-level dependency on the caption
  subsystem).
- Contract pinned by tests: every `playSound*` overload
  (`playSound`, two `playSoundSpatial`, `playSound2D`) must
  invoke the announcer exactly once per call, with the clip path,
  **before** the `!m_available` availability check.

Six of eight tests fail at runtime on the unwired cpp. The two
passing (NoAnnouncerIsSafe_P4, UnmappedClipEnqueuesNothing_P4)
verify safety invariants that hold by default and stay pinned.

**Green commit `5986736`** — caller sites + engine wiring:

- `audio_engine.cpp`: all four `playSound*` overloads gain a
  three-line `if (m_captionAnnouncer) { m_captionAnnouncer(filePath); }`
  at the top, before the `!m_available` short-circuit. Firing
  before the availability check is the accessibility contract:
  a user with broken audio hardware, zero-volume output, or
  deafness / hearing loss still sees the caption when game code
  *intends* to play a sound. Captions are the accessibility
  substitute for the audio itself, not a side-effect of audio
  actually reaching the speakers.
- `engine.cpp`: `Engine::initialize()` installs the announcer
  immediately after `m_captionMap.loadFromFile(...)`:
  ```
  audio->getAudioEngine().setCaptionAnnouncer(
      [this](const std::string& clipPath) {
          m_captionMap.enqueueFor(clipPath, m_subtitleQueue);
      });
  ```
  The captured `this` is stable for the engine lifetime
  (AudioSystem is owned by m_systemRegistry, outlives the wiring).

**No new wire-up needed at individual call sites.** Every existing
`playSound*` call site in the engine and game code now routes
captions as a side-effect — script-graph `PlaySound` nodes,
`AudioSourceComponent` autoplay, ambient emitters, UI clicks,
dialogue triggers — they all go through AudioEngine, which now
all go through the announcer.

**Test suite: 2821 / 2821 passing** (1 pre-existing skip unchanged;
+8 new tests vs. P5's baseline of 2813).

**Files changed:** `engine/audio/audio_engine.h` (+24 / -0 — API
+ member), `engine/audio/audio_engine.cpp` (+20 / -0 — four
three-line fire-before-check blocks), `engine/core/engine.cpp`
(+14 / -0 — startup wiring), `tests/test_caption_map.cpp`
(+162 / -3 — eight spec tests + include block).

**Net: +220 / -3 lines across four files. One design-doc promise
now has an actual wire.**

**Next in Slice 2:** **P2 + P3** (bundled). P2 adds the
`AudioSystem` per-frame `AudioSourceComponent` iteration pass
(+ `m_activeSources` map, per-frame `AL_POSITION` / `AL_VELOCITY`
/ `AL_PITCH` / `finalGain` push) to bring the 11 dead fields on
`AudioSourceComponent` live. P3 folds `DuckingState::currentGain`
into `resolveSourceGain` — P2 is the hook, so they ship together.

### 2026-04-24 Phase 10.9 — Slice 2 P5: subtitles-enabled consumer read path

Second Slice 2 item. Closes the "settings toggle writes a flag nothing
reads" gap for the Accessibility tab's **Subtitles: on / off** switch:
`SubtitleQueueApplySink::setSubtitlesEnabled` stored a local
`m_enabled` bool, but every consumer polled
`Engine::getSubtitleQueue().activeSubtitles()` directly — so when the
user toggled subtitles off, captions kept rendering. The sink test
passed because it only checked `sink.subtitlesEnabled()`; nothing
observed the queue, which is what the renderer actually consults.

**Red commit `2e91605`** — adds the consumer-view API to
`SubtitleQueue` plus seven RED tests, shipping a deliberately-wrong
stub so the new tests fail at runtime (F10 / F11 / P1 discipline):

- `SubtitleQueue::setEnabled(bool)` / `::isEnabled()` public API
  (header declares it, stub returns `m_active` unfiltered).
- `SubtitleQueue::activeSubtitles()`, `::size()`, `::empty()` change
  from inline to out-of-line so they can branch on `m_enabled` in
  green (red stub still returns the raw view).
- Seven tests: three fail at runtime on the stub, four pass because
  they verify the intact tick / enqueue side-effect path rather
  than the hidden-view contract itself.

**Green commit `cd16aa5`** — two minimal changes:

- `subtitle.cpp`: `activeSubtitles()` returns a static empty vector
  when disabled; `size()` returns 0; `empty()` returns true. Internal
  `m_active` keeps ticking so captions still expire in the
  background — re-enabling shows only captions that would still be
  on screen, not stale ones.
- `settings_apply.h`: `SubtitleQueueApplySink::setSubtitlesEnabled`
  forwards to `m_queue.setEnabled(enabled)`. Dropped the local flag
  so there is one source of truth (the queue). `subtitlesEnabled()`
  queries the queue directly.

**Semantics the tests pin:**

| Action | activeSubtitles() | internal m_active |
|--------|-------------------|-------------------|
| `setEnabled(false)` with 2 captions | empty | 2 (still ticking) |
| `tick(1.0)` while disabled | empty | countdowns decrement |
| `setEnabled(true)` after 1 s | 2 (unexpired) | 2 (remaining -1 s) |
| Expire during disabled window | empty on re-enable | dropped at tick |

**No renderer change required** — `UISystem::renderUI` already
early-returns when `activeSubtitles().empty()` is true, so the empty
view is sufficient to stop the overlay pass.

**Test suite: 2813 / 2813 passing** (1 pre-existing skip unchanged;
+7 new tests vs. P1's baseline of 2806).

**Files changed:** `engine/ui/subtitle.{h,cpp}` (+34 / -11 — API doc +
filtering logic), `engine/core/settings_apply.h` (+6 / -3 — sink
delegation + doc), `tests/test_subtitle.cpp` + `tests/test_settings.cpp`
(+98 / -2 — seven new spec tests).

**Net: +138 / -16 lines across four files. One dead settings wire
brought live.**

**Next in Slice 2:** **P4** — Caption auto-enqueue on `playSound*`
entry (closes the Phase 10.7 B3 design doc's declarative caption
routing). Depends on F1's ComponentSerializer registry — F1 shipped,
so P4 is unblocked.

### 2026-04-24 Phase 10.9 — Slice 2 P1: subtitle soft-wrap + multi-line plates

First Slice 2 item. Slice 1 closed the 11 Foundation findings; Slice 2
("Phase 10.7 completion") closes the accessibility / audio / subtitle
gaps where a feature passed its unit tests but delivered a subset of
the design doc. P1 is the subtitle-wrap gap:
`PHASE10_7_DESIGN.md` §4.2 specifies **"soft-wrap at 40 characters;
hard max 2 lines per entry"**, but shipping code did not wrap at all —
any narrator caption or bracketed SoundCue longer than ~35 chars at
1080p quietly overflowed the background plate. Unit tests passed
because they only checked composition ("Moses: Draw near.") and plate
sizing relative to `fullText`; nothing verified the design's readability
ceiling.

**Red commit `23f845a`** — three new header contracts in
`engine/ui/subtitle.h` plus eight wrap / five layout spec-tests:

- `SUBTITLE_SOFT_WRAP_CHARS = 40` (FCC 2024 caption-display rule / GAG).
- `SUBTITLE_MAX_LINES = 2` (BBC caption guidelines; Romero-Fresco 2019).
- `wrapSubtitleText(text, maxChars, maxLines) -> vector<string>` —
  word-boundary-preserving wrap with ellipsis-truncate tail.
- `SubtitleLineLayout::wrappedLines` + `::lineStepPx` fields (added to
  the existing struct — `fullText` preserved for back-compat so
  existing composition tests keep passing unchanged).
- A deliberately-wrong stub in `subtitle.cpp` returns the whole input
  on one line, so the new tests fail at runtime (not link/compile) —
  same F10 / F11 red discipline.

Eight of thirteen new tests fail on the stub as predicted. The five
that pass on the stub are degenerate short-string cases + the
bottom-margin anchor invariant (which holds trivially for any
one-row plate).

**Green commit `3248476`** — `wrapSubtitleText()` implementation
(greedy word packing, hard-break on overlong tokens, ellipsis cap)
and `computeSubtitleLayout()` rewrite to drive plate sizing from
wrapped rows:

- Plate **width** = `max(measureText(row)) × textScale + 2 × padX`.
  Matches the longest rendered row, not pre-wrap total — two-line
  captions get the right-sized plate (no "wide enough for both lines
  end-to-end" dead space).
- Plate **height** = `lineHeightPx + (rows - 1) × (basePx + lineSpacingPx)`.
  Single-row captions keep the existing one-line height exactly so no
  existing test budges; N-row captions grow taller.
- **Y anchor** stays pinned at `screenHeight × (1 - bottomMarginFrac)`
  so taller plates rise UPWARD from that baseline rather than slide
  off the bottom of the viewport.
- `renderSubtitles()` emits one `TextRenderer::renderText2D` call per
  wrapped row, stepped by `lineStepPx`. Falls back to `fullText` when
  `wrappedLines` is empty (defensive — no live caller hits this path
  but it keeps the renderer robust against a future caller that forgets).

**Wrap policy summary:**

| Input | Output |
|-------|-------|
| `""` | `[]` (empty vector, no plate) |
| short text | `[text]` |
| two words that fit | `[joined]` |
| 45 chars natural wrap | `[first-40, rest]` |
| 55-char single token | `[XXXX…XXXX (40), rest (15)]` |
| 3-line input | `[line1, line2 + "…"]` (U+2026 marker) |

**Test suite: 2806 / 2806 passing** (1 pre-existing skip unchanged;
+13 new tests vs. F11's baseline of 2793).

**Files changed:** `engine/ui/subtitle.{h,cpp}` (+138 / -2 — wrap
helper + constants), `engine/ui/subtitle_renderer.{h,cpp}`
(+55 / -35 — wrappedLines field, multi-line plate sizing, per-row
text draw).

**Net: +193 / -37 lines across four files. One design-doc promise
now matched by behaviour.**

**Next in Slice 2:** **P5** — `SubtitleQueueApplySink::setSubtitlesEnabled`
→ consumer read path (the toggle currently writes a flag nothing
reads). Smaller plumbing item; P4 + P2/P3 + P7 follow.

### 2026-04-24 Phase 10.9 — Slice 1 F11: strobe-slider WCAG honesty

Eleventh Slice 1 item. Closes the Accessibility tab's "Max strobe Hz"
slider lie that the partially-sighted user is the direct victim of:
the slider was drawn only when `photosensitiveSafety.enabled` is true,
but was bounded at `[0, 10] Hz` even though
`PhotosensitiveSafety::clampStrobeHz()` runtime-caps effective output
at `min(limits.maxStrobeHz, WCAG_MAX_STROBE_HZ)` — `WCAG_MAX_STROBE_HZ
= 3.0f` per WCAG 2.2 SC 2.3.1 ("Three Flashes or Below Threshold").
So a user could drag the slider to 7.0, watch the settings file
persist 7.0, and never realise every value above 3 Hz was silently
discarded. Accessibility UI that lies to its audience is strictly
worse than no accessibility UI.

**Red commit `c6db868`** — two new tests in `tests/test_settings.cpp`
plus a bridge constant that deliberately encodes the shipping lie:

- `SettingsEditorPanel::SAFE_MODE_STROBE_HZ_SLIDER_MAX` added to
  `engine/editor/panels/settings_editor_panel.h` with value `10.0f`
  (matches shipping slider, so the failure is at RUNTIME not at
  compile time — F10-style proper red). The slider call in
  `settings_editor_panel.cpp:516` refactored to consume the constant
  so the fix has one home.
- `SafeModeStrobeSliderMaxEqualsWcagCeiling_F11` — asserts the
  constant equals `WCAG_MAX_STROBE_HZ`. Red output: "10 vs 3".
- `SafeModeStrobeSliderMaxIs3Hz_F11` — belt-and-braces assertion that
  the constant equals `3.0f` literal. Red output: "10 vs 3". This
  second pin guards against a hypothetical future refactor that
  re-bases `WCAG_MAX_STROBE_HZ` away from 3.0 — if that ever happens
  the test surfaces and both pins get re-reviewed together rather
  than silently realigned.

**Green commit `5d52006`** — one-line change in
`settings_editor_panel.h`:

```
- static constexpr float SAFE_MODE_STROBE_HZ_SLIDER_MAX = 10.0f;
+ static constexpr float SAFE_MODE_STROBE_HZ_SLIDER_MAX = WCAG_MAX_STROBE_HZ;
```

Plus a `#include "accessibility/photosensitive_safety.h"` in the
panel header so the constexpr can reference the WCAG constant
directly. One source of truth for the WCAG ceiling across the panel
and the runtime clamp — no way for the two to drift.

**No Settings schema change.** `validate()` still clamps persisted
`maxStrobeHz` to `[0, 30]` (F8) — that's the reasonable-value fence
for the underlying data structure. F11 closes the narrower
"what can the safe-mode-enabled user move the slider to" gap, which
is strictly tighter. If a caller really wants to author a strobe at
4 Hz (e.g. an editor-time test fixture), they still can via the
JSON path; the F11 contract is that the safe-mode UI does not offer
them the loaded gun.

**Test suite: 2793 / 2793 passing** (1 pre-existing skip
`MeshBoundsTest.UploadComputesLocalBounds`, unchanged; 2 new tests
vs. F10's baseline of 2791).

**Files changed (green):** `engine/editor/panels/settings_editor_panel.h`
(+3 / -1 — include the photosensitive-safety header and swap the
constant's initialiser to `WCAG_MAX_STROBE_HZ`).

**Net: +3 / -1 lines in one header. One accessibility lie closed —
the UI now shows only values the runtime honours.**

**Slice 1 complete.** F1–F11 all shipped. Foundations are in place:
F1–F3 (component / serializer plumbing), F4–F6 (input-validation
perimeter), F7–F8 (settings clamp policy), F9 (Logger
thread-safety), F10 (SystemRegistry partial-init rollback), F11
(accessibility UI honesty). **Next in Phase 10.9: Slice 2 — P1…P10
"Phase 10.7 completion"** (gain-chain ducking application, subtitle
caption wiring, HRTF dataset surface). Begin when Slice 1 has
settled.

### 2026-04-24 Phase 10.9 — Slice 1 F10: `SystemRegistry` partial-init cleanup

Tenth Slice 1 item. Same red / green / doc discipline as F1–F9. Direct
response to the partial-init-leak finding: `SystemRegistry::initializeAll`
returned false on the first `system->initialize()` failure but left every
already-initialized system still holding its engine-owned resources (GL
handles, OpenAL sources, Jolt bodies, etc.). `~SystemRegistry` does not
call `shutdown()`, and `shutdownAll()` early-returns on `!m_initialized`
(which stays false after a failed init) — so the 0..N-1 prefix was
orphaned until process exit, with its destructor-only cleanup running
during `~Engine` after the Renderer / Window / GL context were already
gone (AUDIT.md §H17 territory).

**Red commit `bd6ebe8`** — five new tests in
`tests/test_system_registry.cpp`:

- `InitializeAllShutsDownPrefixInReverseOnFailure_F10` — A, B, C
  registered; B's `initialize()` returns false. Asserts the call log is
  `A::init, B::init, A::shutdown`. Pre-F10 the log stops after
  `B::init` — A never gets its `shutdown()`.
- `InitializeAllShutsDownEveryPrecedingOnFailure_F10` — five-system
  fixture (A, B, C, D, E) with D failing. Asserts all three of C, B, A
  get `shutdown()` in reverse, and E is never touched.
- `InitializeAllFailureOnFirstSystemShutsDownNothing_F10` — edge case:
  the very first system fails. There is no prefix to clean up; call log
  is just `A::init`. Passes on shipping code (nothing to break) and
  pins the invariant for F10.
- `InitializeAllFailureDeactivatesPrefix_F10` — after rollback the
  previously-initialized systems must not still report `isActive()`.
  Without the fix a later `updateAll()` would tick subsystems whose
  resources have been released.
- `InitializeAllFailureLeavesRegistryReInitable_F10` — `m_initialized`
  stays false (so `shutdownAll()` is a no-op), and `clear()` runs
  destructors exactly once. No double-destruction.

Three of the five tests fail on shipping code (the prefix-rollback
invariants). The other two — first-system-failure and re-initable —
pass as-is and serve as regression pins.

**Green commit `840c651`** — 19 lines in
`engine/core/system_registry.cpp`, no header change:

- `initializeAll()` tracks a local `initializedCount` of systems that
  cleanly returned true from `initialize()`.
- On failure, a reverse loop walks
  `[0, initializedCount)` and calls `shutdown()` + `setActive(false)`
  on each before returning false.
- System N itself gets no `shutdown()` — its `initialize()` returned
  false, meaning the resource contract was never established. This
  matches the existing single-system convention (initialize-failed ⇒
  no paired shutdown) so individual subsystems don't need to be
  defensive against shutdown-without-successful-init.
- Each rollback step logs `SystemRegistry: rolling back 'Name'` so
  boot-time failures produce a legible audit trail rather than a
  silent false return.

`m_initialized` stays false on the failure path, so a subsequent
`shutdownAll()` remains a clean no-op (idempotency preserved) and
`clear()` continues to run destructors exactly once.

**Test suite: 2791 / 2791 passing** (1 pre-existing skip
`MeshBoundsTest.UploadComputesLocalBounds`, unchanged; 5 new tests
vs. F9's baseline of 2786).

**Files changed (green):** `engine/core/system_registry.cpp`
(+19 / -0 — `initializedCount` counter + reverse rollback loop +
comments pinning the F10 contract and the "system N gets no shutdown"
boundary). Header unchanged; the fix is internal to the existing
`bool initializeAll(Engine&)` signature.

**Net: +19 / -0 lines in one file. One resource leak closed,
destructor-only cleanup converted into explicit rollback.**

**Next in Slice 1:** **F11** — "Max strobe Hz" slider honesty at
`settings_editor_panel.cpp:516`: drop slider max to WCAG 2.2 SC 2.3.1's
3 Hz ceiling (or render a helper text exposing the cap). The current
0..10 Hz slider persists values the F5 runtime clamp silently discards,
lying to the partially-sighted user it is supposed to serve.

### 2026-04-24 Phase 10.9 — Slice 1 F9: `Logger` thread-safety

Ninth Slice 1 item. Same red / green / doc discipline as F1–F8. Direct
response to the Logger-race finding: `AsyncTextureLoader` already logs
from a worker thread alongside main-thread log calls, and
`Logger::log()` mutates two pieces of shared state without any
synchronisation — `s_entries` (a `std::deque<LogEntry>` ring buffer)
and `s_logFile` (an `ofstream`). Concurrent `deque::push_back` /
`pop_front` is UB by the standard (can crash, drop entries, or corrupt
iterators); concurrent ofstream writes can tear a formatted line
mid-character. Neither is hypothetical — the new RED tests reproduce a
deterministic `SIGSEGV` inside `Logger::log` on the unfixed code.

**Red commit `a113e32`** — two new tests in `tests/test_logger.cpp`:

- `ConcurrentLoggingPreservesAllEntries_F9` — 8 threads × 100
  `Logger::info` calls (800 total, under `MAX_ENTRIES = 1000`), all
  released from a spin-wait to maximise overlap. Asserts
  `Logger::getEntries().size() == 800`. Shipping code SEGVs inside
  `deque::push_back` on most runs.
- `ConcurrentLoggingRespectsRingBufferCap_F9` — 8 threads × 500
  `Logger::info` calls (4000 total, overflows `MAX_ENTRIES`). Asserts
  the buffer keeps exactly `1000`. Stresses the
  `pop_front` + `push_back` trim path, which is the race window that
  corrupts the deque's internal pointers. Also SEGVs on shipping
  code.

**Green commit `87818fa`** — one private static `std::mutex` in
`logger.cpp` (`s_logMutex`) plus a `std::lock_guard` in every
function that touches the shared state:

1. `Logger::log()` — lock after the early level-filter return (so
   filtered messages stay lock-free) and before any console / file /
   deque writes. The single lock covers all three pieces of shared
   state in one critical section, because `std::cerr` / `std::cout`
   formatted writes are themselves not atomic against each other and
   torn lines would defeat half the point of having a logger.
2. `Logger::clearEntries()` — lock around the `s_entries.clear()`.
3. `Logger::getEntries()` — **API change.** Now returns
   `std::deque<LogEntry>` **by value**, not `const std::deque&`. The
   old reference-returning signature was racy by construction: the
   editor console panel reads on the main thread while the worker
   logs, and no amount of internal locking could make an
   externally-held reference safe. Under the lock, `getEntries()`
   copies the deque and returns the snapshot; callers iterate the
   snapshot with no thread-safety concern. Both existing call sites
   in `editor.cpp` already iterate the whole deque for a per-frame
   debug panel, so per-frame copy of ~1000 small entries (`LogLevel`
   + `std::string`) is trivial.

`openLogFile` / `closeLogFile` deliberately left unlocked —
single-threaded startup / shutdown lifecycle calls, no F9 race
lives there. Lock scope was kept minimal (log()'s early-out for
level-filtered messages stays lock-free) so the common case of
trace/debug messages in a release build pays zero synchronisation
cost.

Both F9 RED tests now pass. Full suite green, zero regressions.

**Test suite: 2785 / 2785 passing** (1 pre-existing skip
`MeshBoundsTest.UploadComputesLocalBounds`, unchanged; 2 new tests
vs. F8's baseline of 2783).

**Files changed (green):** `engine/core/logger.h` (+3 / -1 — return
type change + doc block referencing F9); `engine/core/logger.cpp`
(+16 / -2 — `<mutex>` include, `s_logMutex`, three `lock_guard`s,
and the comment that pins the shared-state inventory).

**Net: +19 / -3 lines across two files. One UB race closed.**

**Next in Slice 1:** **F10** — `SystemRegistry::initializeAll`
partial-init cleanup (reverse-shutdown the 0..N-1 prefix on failure
of system N; current behaviour leaks GL/AL/Jolt resources through
destructor-only cleanup).

### 2026-04-23 Phase 10.9 — Slice 1 F8: `SettingsEditor::mutate` validate-before-push

Eighth Slice 1 item. Same red / green / doc discipline as F1–F7. Direct
response to the runtime-UI-bypasses-persistence-policy finding: every
ImGui slider wrapped in `m_editor->mutate(...)` was writing raw values
straight into `m_pending` and pushing through the apply sinks without
running the same `validate()` chain that `Settings::fromJson` uses on
load. Live previews could therefore drive subsystems into states the
persistence layer would have rejected.

**Red commit `357f8a9`** — three tests in `tests/test_settings.cpp`
pin the clamp contract on `SettingsEditor::mutate`:

- `MutateClampsAudioBusGainBeforePushingToSink_F8` — sets
  `busGains[0] = 2.5f` through `mutate()` with an audio sink attached;
  asserts both `editor.pending().audio.busGains[0]` and the sink
  received `1.0f` (the `[0.0, 1.0]` policy). Pre-F8 the `AudioMixer`
  saw raw `2.5` — clipping every sample.
- `MutateClampsRenderScaleBeforePushingToSink_F8` — sets
  `renderScale = 3.5f`; asserts pending reflects `2.0f` (the
  `[0.25, 2.0]` GPU-budget cap).
- `MutateClampsStrobeHzBeforePushingToSink_F8` — sets
  `maxStrobeHz = 120.0f`; asserts pending reflects `30.0f` (the
  persisted ceiling; runtime further clamps to `3 Hz` via
  `clampStrobeHz` when photosensitive safety is enabled — F5 of
  this slice).

All three FAILED against shipping code: raw slider values survived
`mutate()` and reached the sinks unchanged.

**Green commit `92af103`** — two changes, one contract:

1. `validate(Settings&)` lifted from the anonymous namespace in
   `engine/core/settings.cpp` into the `Vestige` namespace, with a
   new declaration in `engine/core/settings.h`. Validation helpers
   (`clamp01`, `isValidScalePreset`, `isValidSubtitleSize`,
   `isValidColorVisionFilter`) stay in the anonymous namespace —
   they're `validate()`'s implementation detail, not part of the
   contract.
2. `SettingsEditor::mutate` now calls `validate(m_pending)` between
   the mutator invocation and `pushPendingToSinks()`. `Settings::fromJson`'s
   existing call to `validate()` on load is unchanged; F8 is the
   symmetric wire-up on the runtime side so live and persisted
   state can never disagree about what the policy is.

All three red tests go green. `pending()` now reflects the clamped
value in every case, and the sinks receive the clamped value.

**Test suite: 2783 / 2783 passing** (1 pre-existing skip
`MeshBoundsTest.UploadComputesLocalBounds`, unchanged).

**Files changed (green):** `engine/core/settings.h` (+13 lines — public
declaration + doc block); `engine/core/settings.cpp` (+/- 3 lines —
move the anon-namespace closer up so `validate()` is at namespace
scope); `engine/core/settings_editor.cpp` (+6 lines — the
`validate(m_pending)` call plus explanatory comment citing the three
clamp classes it covers).

**Net: +22 / -3 lines across three files. One bypass closed.**

**Next in Slice 1:** **F9** — `Logger` thread-safety (`std::mutex`
around `s_logFile` / `s_entries`; `AsyncTextureLoader` already logs
from a worker thread → live race on the log file `ofstream` and the
in-memory entry deque).

### 2026-04-23 Phase 10.9 — Slice 1 F7: atomic-write unification

Seventh Slice 1 item. Same red / green / doc discipline as F1–F6.
Direct response to the duplicated-persistence finding from the second
/indie-review sweep: five save paths existed, only one (`AtomicWrite::
writeFile`) did the full POSIX-safe dance, and the other four each
skipped different steps of it.

**Red commit `26e5e62`** — `tests/test_atomic_write_routing.cpp` pins
the atomic-write-helper routing via the `PrefabSystem::savePrefab`
surface (the narrowest hole — direct `std::ofstream`-to-target, with no
`.tmp` step at all):

- `POSIXRename_PrefabSaveClearsStaleTmpSidecar_Rule3` — plants a stale
  `TestPrefab.json.tmp` next to the target, invokes `savePrefab`, and
  asserts (a) the target was written and (b) the stale `.tmp` is gone.
  Pre-F7 the direct write never opens the `.tmp`, so the stale sidecar
  from a prior crashed save survives indefinitely; post-F7 the helper
  overwrites the `.tmp` in its write step then renames it away.
- `POSIXRename_PrefabSaveLeavesNoTmpArtifact` — companion check that a
  fresh save leaves no `.tmp` residue (rename step completed).

Test names cite POSIX `rename(2)` atomicity and CLAUDE.md Rule 3
("Reuse before rewriting"). Ran against shipping code: first test
FAILED (stale `.tmp` survived); second PASSED (no `.tmp` ever
created). Red behaves exactly as the `ofstream`-direct model predicts.

**Green commit `ea1e133`** — routes every persistence path through
`engine/utils/atomic_write.cpp`, deleting the duplicates in place:

- `engine/editor/scene_serializer.cpp` — the 50-line
  `atomicWriteFile` static that ran tmp+rename *without* `fsync(file)`
  or `fsync(dir)` is deleted. Both scene-save call sites now invoke
  `AtomicWrite::writeFile`; errors surface the `Status` via
  `describe()` in the existing error-message string.
- `engine/editor/prefab_system.cpp` — `savePrefab` goes from a
  truncate-in-place `std::ofstream` to one `AtomicWrite::writeFile`
  call. Prefab library is now durable; stale `.tmp` sidecars get
  reaped on next save.
- `engine/core/window.cpp` — `saveWindowState` was a best-effort
  `ofstream` that silently ignored write errors. Still best-effort
  (window layout is UX convenience), but now logs a warning on failure
  and the half-write window is closed by `rename(2)` atomicity.
- `engine/editor/file_menu.cpp` — `performAutoSave`'s manual
  tmp+rename (50 lines, no `fsync`) collapses to one
  `AtomicWrite::writeFile`. The `.path` breadcrumb sidecar that records
  the original scene path also routes through the helper, so recovery
  state survives the same crash-window as the autosave itself.
- `engine/environment/terrain.cpp` — `saveHeightmap` and `saveSplatmap`
  were raw binary `std::ofstream` writes. Converted to `string_view`
  payloads over the float / vec4 grids (binary-safe — `string_view`
  may hold NULs) + `AtomicWrite::writeFile`. Scene autosave's terrain
  sidecars now share the atomicity guarantee of the `.scene` file.

Net: 134 lines deleted, 62 added (-72 net) in `engine/`. Five
durability bugs closed by removing four copies of a contract the
codebase already had one correct implementation of. All 2781 unit
tests pass (1 pre-existing `MeshBoundsTest.UploadComputesLocalBounds`
skip unchanged).

**Doc commit** — this CHANGELOG entry, ROADMAP F7 tick, VERSION bump
`0.1.9` → `0.1.10`.

**Next in Slice 1**: F8 — `SettingsEditor::mutate` validate-before-push
(one-line `validate(m_pending)` before `pushPendingToSinks()` so the
runtime-UI slider path stops bypassing every clamp policy).

### 2026-04-23 Phase 10.9 — Slice 1 F6: OBJ negative-index support + 1 MiB per-line cap

Sixth Slice 1 item. Same red / green / doc discipline as F1–F5.

**Red commit `371c8bc`** — added three spec-tagged tests to
`tests/test_obj_loader.cpp` pinning the Wavefront OBJ spec Appendix B
negative-index rule ("-1 refers to the most recent vertex") and a
CWE-400 per-line read cap:

- `OBJSpec_AppendixB_NegativeIndicesResolveRelativeToCurrentListEnd` —
  `f -3 -2 -1` after three `v` lines must resolve to v#1 / v#2 / v#3.
  The pre-F6 parser ran `stoi("-1") - 1 = -2`, tripped the
  `key.posIndex >= 0` guard, and silently emitted three vertices at the
  origin — a valid-looking triangle with all corners collapsed.
- `OBJSpec_AppendixB_NegativeIndicesAreRelativeAtParseTime` — negative
  indices must resolve against the list size at the face's parse point,
  not the file's final size. Interleaved `v` + `f` blocks would be
  mis-resolved otherwise.
- `CWE_400_OverLongSingleLineIsRejected` — a file whose first line is a
  1.5 MiB comment must be rejected, not read into an unbounded
  `std::string`. Pre-F6 `std::getline` happily allocated whatever the
  line was.

**Green commit `f6c2375`** — two targeted changes in
`engine/utils/obj_loader.cpp`:

- `resolveObjIndex(const std::string& s, size_t listSize)` replaces the
  old `safeStoi`. Positive → `raw - 1`; negative → `listSize + raw`
  (e.g. -1 → last, -2 → second-last); zero/malformed → -1 (invalid).
  `parseFaceVertex` now takes the three list sizes so each slot
  (position / texcoord / normal) is resolved against its own list.
- `readBoundedLine` replaces `std::getline` with a per-char reader
  capped at `kMaxLineBytes = 1 MiB` (= 1048576 bytes). Over-limit
  returns `LineStatus::TooLong`; the parser logs `"OBJ line N exceeds
  1048576-byte cap"`, clears the output buffers, and returns false.
  The cap bounds worst-case memory growth without touching the legal
  256 MiB overall file-size guard.

**Full suite:** 2778/2779 pass (1 pre-existing skip —
`MeshBoundsTest.UploadComputesLocalBounds`). All 15 ObjLoaderTest cases
pass (12 pre-existing + 3 new).

### 2026-04-23 Phase 10.9 — Slice 1 F5: `clampStrobeHz` hard-caps at WCAG 3 Hz

Fifth Slice 1 item. Same red / green / doc discipline as F1–F4.

**Red commit `559b4bd`** — added four tests to
`tests/test_photosensitive_safety.cpp` (`PhotosensitiveSafety.WCAG_2_3_1_StrobeHz*`)
pinning the WCAG 2.2 SC 2.3.1 3 Hz ceiling as a hard cap on top of any
caller-supplied `PhotosensitiveLimits::maxStrobeHz`. One test failed
against the pre-F5 implementation:

- `StrobeHzHardCapsEvenWhenLimitsRelaxed` — with a mis-tuned
  `PhotosensitiveLimits{.maxStrobeHz = 29.0f}`, `clampStrobeHz(10.0f,
  true)` returned `10.0` (the caller's cap of 29 never pulled it down),
  and `clampStrobeHz(29.0f, true)` returned `29.0`. A 29 Hz strobe
  shipping to a user with safe mode *enabled* violates the whole point
  of photosensitivity safe mode.

The other three tests (`StrobeHzBelowWCAGCeilingStillPassesThrough`,
`StrobeHzTighterCallerCapStillWins`, `StrobeHzHardCapDoesNotApplyWhenDisabled`)
passed against the pre-F5 implementation but are load-bearing
regressions against obvious over-corrections — e.g. implementing the
hard-cap as a global `clamp(x, 0, 3)` would regress the
disabled-is-identity contract from F4.

**Green commit `778fb29`** — published
`WCAG_MAX_STROBE_HZ = 3.0f` as a module-level `inline constexpr` in
`engine/accessibility/photosensitive_safety.h` and applied it inside the
enabled-path arm of `clampStrobeHz`:

```cpp
const float cap = std::min(limits.maxStrobeHz, WCAG_MAX_STROBE_HZ);
return std::min(safe, cap);
```

Semantics:

- **Caller tighter than WCAG** (e.g. `maxStrobeHz = 0.5f` for a horror
  beat) — caller still wins. Safe mode is a one-way tightening; user
  intent to restrict further must not be overridden by a WCAG *floor*
  that doesn't exist.
- **Caller looser than WCAG** (e.g. `maxStrobeHz = 29.0f` from a
  mis-tuned accessibility config or an untrusted mod) — WCAG wins. The
  user's safe-mode toggle is not defeated by config drift.
- **Safe mode disabled** — unchanged. F4's identity contract still
  dominates; a 60 Hz flicker round-trips unmodified because the user
  has explicitly opted out.

After green, re-added `StrobeHzConstantIsThreeHz` to pin the exact
`3.0f` value so a future refactor cannot "round it up to 4 Hz" without
tripping a test. Header docstring on `clampStrobeHz` now names the
hard-cap rule alongside the existing cap/scale semantics.

**Full suite:** 2775/2776 pass (1 pre-existing skip —
`MeshBoundsTest.UploadComputesLocalBounds`). All 31 PhotosensitiveSafety
tests pass.

### 2026-04-23 Phase 10.9 — Slice 1 F4: photosensitive clamp helpers sanitise NaN / ±inf / negatives

Fourth Slice 1 item. Same red / green / doc discipline as F1–F3.

**Red commit `c7023ae`** — added 13 WCAG-tagged tests to
`tests/test_photosensitive_safety.cpp` (`PhotosensitiveSafety.WCAG_2_3_1_*`)
asserting all four helpers (`clampFlashAlpha`, `clampShakeAmplitude`,
`clampStrobeHz`, `limitBloomIntensity`) sanitise NaN / +inf / -inf /
negative inputs to `0.0f` in *both* enabled and disabled paths. Tests
authored from WCAG 2.2 SC 2.3.1 ("Three Flashes or Below Threshold") and
ROADMAP Phase 10.9 Slice 1 F4 — the photosensitive guarantee must not
depend on whether the user has toggled safe mode. 12 of 13 failed
against the pre-F4 implementation:

- Disabled path was a bare `return x;` — all four helpers leaked NaN /
  ±inf / negative values straight through.
- Enabled min-style helpers (flash, strobe): libstdc++ propagates
  `std::min(NaN, cap) = NaN`; `-inf` passes through as `min(-inf, cap) =
  -inf`; `inf` collapses to the cap (`0.25` / `2.0` Hz) and silently
  hides the upstream bug rather than reporting it.
- Enabled scale-style helpers (shake, bloom): `NaN * scale = NaN`,
  `inf * scale = inf`, `-5 * 0.25 = -1.25` — the renderer would have
  received a negative / infinite / NaN brightness multiplier.

The 13th test (`WCAG_2_3_1_DisabledPreservesFinitePositives`) passed at
red and pins the disabled-path pass-through for finite non-negatives, so
a future refactor cannot regress the disabled path into a global ceiling
while "fixing" sanitisation.

**Green commit `50aa1bc`** — factored a private
`sanitiseNonNegative(x)` helper in `photosensitive_safety.cpp`
(non-finite → 0, negative → 0, else x) and applied it at the top of all
four clamp helpers before the enabled/disabled branch runs. Header
docstrings updated to name the sanitisation contract alongside the
existing cap/scale semantics. All 26 `PhotosensitiveSafety.*` tests now
pass; full suite **2771/2771** (1 pre-existing skip, unrelated).

**Effect on shipping behaviour.** A buggy upstream producing NaN / ±inf
/ negative values — e.g. a physics subsystem integrating a divide-by-
zero into shake amplitude, or a custom post-process supplying
`intensity = -1.0f` by mistake — can no longer paint an uncontrolled
brightness on the framebuffer. Well-formed finite non-negative upstream
code is unchanged in both enabled and disabled paths. No ABI change
(pure policy refinement inside the existing free functions).

**Why WCAG 2.3.1 requires sanitisation on both paths.** The safe-mode
toggle is a user preference for *additional* restriction, not an opt-in
to sanitisation. SC 2.3.1 is a conformance gate on the content itself,
and the renderer cannot reason about what a NaN pixel will decode to
after blending/tonemap — so a non-finite value must never reach it,
regardless of the user's safe-mode state.

### 2026-04-23 Phase 10.9 — Slice 1 F3: ComponentSerializerRegistry

Third Slice 1 item. Follows the same red / green / doc discipline
as F1 and F2.

**Red commit `4056ce0`** — added `tests/test_entity_serializer_registry.cpp`
with four spec tests authored from ROADMAP Phase 10.9 Slice 1 F3
and the `AudioSourceComponent::bus` docstring at
`audio_source_component.h:35-49` (which references the Phase 10.7
slice A1 mixer-bus design). The focused test —
`AudioSourceBusRoundTrips_Phase10_7_A1` — exercises the latent
bug: a Music-bus scene entity saved to JSON deserialises as a
Sfx-bus default because the entity serializer's fixed 7-entry
allowlist (MeshRenderer, DirectionalLight, PointLight, SpotLight,
EmissiveLight, ParticleEmitter, WaterSurface) silently drops every
other component type on save. All four tests failed against the
pre-fix serializer.

**Green commit `f640d1c`** — introduced
`engine/utils/component_serializer_registry.{h,cpp}`. The registry
holds `{typeName, trySerialize, deserialize}` tuples; each
concrete type registers itself via `ensureBuiltinsRegistered()` on
first use. `serializeEntity` now walks the registry once and
serialises every component the entity owns; `deserializeEntity`
dispatches each JSON key to the matching entry (unknown keys log a
warning rather than silently dropping).

Registered built-ins (8 types):
- MeshRenderer, DirectionalLight, PointLight, SpotLight,
  EmissiveLight, ParticleEmitter, WaterSurface (pre-F3 allowlist,
  behaviour-preserved)
- AudioSource (NEW) — every user-editable field now round-trips:
  clipPath, volume, bus (enum ↔ string), pitch, minDistance,
  maxDistance, rolloffFactor, attenuationModel (enum ↔ string),
  velocity, occlusionMaterial (enum ↔ string), occlusionFraction,
  loop, autoPlay, spatial. Absent fields hydrate to the
  Phase 10.7-documented defaults (e.g. `bus` → `Sfx`, matching the
  pre-A1 implicit routing per `audio_source_component.h:44-48`).

All four Red-commit tests now pass. Full suite: 2757/2758 (1
pre-existing skip, unrelated).

**Effect on shipping behaviour.** Scene save / load and prefab
instantiation now preserve `AudioSourceComponent` fields across
JSON round-trips; previously every AudioSource on a saved scene
was dropped and re-instantiated from defaults when the scene
loaded. No other serialised behaviour changes — the seven pre-F3
types serialise identically.

**CameraMode persistence hook.** The registry is also the Phase 10.8
CM* / Slice 10 hook for `CameraComponent` / `CameraMode` serialisation:
adding one more registration call adds camera persistence with no
core-file edits. Deferred to the slice that introduces the
serialisable fields.

**F1 checkbox housekeeping.** Ticked the Phase 10.9 Slice 1 F1
checkbox in ROADMAP.md — F1 actually shipped under commits
`7b9f116` (red) / `5572aa5` (green) / `4641124` (nit) on 2026-04-23,
but the checkbox was never flipped. Retroactive correction so the
ROADMAP matches the git history.

### 2026-04-23 Phase 10.9 — Slice 1 F2: Component::clone() pure-virtual + ClothComponent backfill

Second Slice 1 item. Follows the same red / green / reviewer
discipline as F1.

**Red commit `f911aa5`** — added `tests/test_component_clone.cpp`
enumerating every concrete default-constructible `Component`
subclass (22 cases) and asserting `clone()` returns a non-null
instance of the expected dynamic type. The test is authored from
the `Component::clone()` header docstring + ROADMAP Phase 10.9
Slice 1 F2, not from the current code. 21 of 22 cases passed
immediately; `ClothComponent` failed because it inherited the
base's nullptr-returning `clone()` default, which
`Entity::clone()` silently dropped — i.e. copying any entity
carrying a `ClothComponent` (via `Scene::duplicateEntity` or the
editor duplicate action) lost the cloth with no error path.

**Green commit `75b64a3`** — promoted `Component::clone()` to pure
virtual so the compiler enforces the deep-copy contract on every
future subclass. Added `ClothComponent::clone()` following the
same shape as `WaterSurfaceComponent` / `GPUParticleEmitter`:
clones config + backend policy + shader path + material + preset
+ enabled flag, and leaves `initialize()` to rebuild the solver
backend + `DynamicMesh` on the clone (the live solver state owns
non-copyable GPU buffers on one path and trivially rebuildable
CPU arrays on the other, so a live-state copy is not meaningful).
Dropped the now-dead `if (cloned)` guard in `Entity::clone()` per
CLAUDE.md's "no error paths for scenarios that can't happen".
Added trivial `clone()` overrides to the `TestComponent` /
`OtherComponent` stubs in `tests/test_entity.cpp` so they remain
instantiable under the pure-virtual base. All 22 ComponentClone
tests pass; full suite: 2754/2754 (1 pre-existing skip).

**Serializer allowlist audit (F2 side task).** While auditing
`clone()` coverage, surveyed the allowlist in
`engine/utils/entity_serializer.cpp` — it covers seven component
types (MeshRenderer, DirectionalLight, PointLight, SpotLight,
EmissiveLight, ParticleEmitter, WaterSurface). All other
concrete component subclasses — including AudioSourceComponent,
CameraComponent, Camera2DComponent, SpriteComponent,
TilemapComponent, Collider2DComponent, RigidBody2DComponent,
CharacterController2DComponent, PressurePlateComponent,
InteractableComponent, GPUParticleEmitter, ClothComponent,
BreakableComponent, RigidBody, NavAgentComponent, SkeletonAnimator,
FacialAnimator, LipSyncPlayer, TweenManager, and the forthcoming
CameraMode subclasses — are silently dropped when an entity
tree is serialised to a prefab or scene JSON. This is the exact
latent-data-loss shape Slice 1 F3
(`ComponentSerializerRegistry`) is slated to fix; captured here
so F3 lands with a concrete coverage list rather than rediscovering
the gap.

**Effect on shipping behaviour.** Entity duplication (editor
"Duplicate", `Scene::duplicateEntity`, and the prefab
instantiation path that calls `Entity::clone()`) now carries
`ClothComponent` into the copy. No other behaviour change —
every other concrete subclass already had a working `clone()`,
so their entities already duplicated correctly.

### 2026-04-23 Phase 10.9 — Slice 1 F1: caption-path double-concat fix

First ship of Phase 10.9 (post-ultrareview remediation) per the
red/green + independent-reviewer process discipline established
at phase open.

**Red commit `7b9f116`** — extracted the pre-fix join at
`engine.cpp:412` (`m_assetPath + "assets/captions.json"`) verbatim
into a new `engine/core/engine_paths.{h,cpp}` choke-point so the
pre-fix behaviour could be unit-tested in isolation. Added four
spec-driven tests in `tests/test_engine_paths.cpp` authored from
`PHASE10_7_DESIGN.md` §4.2 ("the caption map lives at
`<assetPath>/captions.json`") and the existing engine convention
of `<assetPath>/<sub>` used throughout engine.cpp for fonts,
scenes, shaders. All four tests failed against the extracted
verbatim bug — evidence the tests were capable of failing.

**Green commit `5572aa5`** — fixed the helper to
`stripTrailingSlash(assetPath) + "/captions.json"`, with a
bare-filename short-circuit for empty asset roots. All four tests
pass. Full suite: 2731/2732 (1 pre-existing skip).

**Reviewer pass** — independent subagent given only the two diffs
+ the design-doc clause, no session context. Returned "Accept
with nit": judged the fix correct, judged the red commit
genuinely failing (not a fake red/green), judged the tests
spec-anchored rather than code-mirroring, and judged the helper
extraction justified for its unit-test surface. Nit: document
that `stripTrailingSlash` is POSIX-separator-only because the
fragments we append are also `/`-rooted; addressed in a follow-up
nit commit.

**Effect on shipping behaviour.** `Engine::initialize` now loads
`<assetPath>/captions.json` as PHASE10_7_DESIGN.md §4.2 specified,
rather than `<assetPath>assets/captions.json`. Projects that ship
caption maps will see their captions fire when clips play (once
Slice 2 P4 wires the auto-enqueue call path — today no
`playSound*` overload invokes `CaptionMap::enqueueFor`). Projects
shipping no captions are unchanged — the file is optional; the
loader treats absent as empty.

### 2026-04-23 ROADMAP: Phase 10.9 post-ultrareview remediation phase

Added Phase 10.9 (Post-Ultrareview Remediation) between Phase 10.8
and Phase 11A as the single source of truth for all follow-up work
from the 2026-04-23 independent multi-agent code review.

The review ran fourteen independent reviewer agents in parallel,
each scoped to one subsystem (`audio`, `accessibility`, `animation`,
`core`, `editor`, `environment`, `input`, `physics`, `renderer`,
`resource`, `scene`, `systems`, `ui`, `utils`). Each reviewer saw
only design docs + source — never the test files. This deliberately
broke the "self-marking homework" loop where tests encode what the
code does rather than what the design doc says.

The sweep surfaced 14 CRITICAL and ~47 HIGH findings. Five of the
CRITICAL findings were Phase 10.7 features that passed every test
we wrote but shipped a subset of the design doc — the tests
verified the implementation, not the spec.

Phase 10.9 is organised into 13 slices ordered by dependency:

1. **Foundations** — the cheap, isolated fixes every later slice
   benefits from (caption-path typo, `Component::clone()` pure-
   virtual, serializer registry, clamp-helper NaN guards, WCAG
   hard-cap on strobe Hz, OBJ negative indices).
2. **Phase 10.7 completion** — finish the gain chain, subtitle
   wrap, caption auto-enqueue, voice eviction, ducking
   application, HRTF init order.
3. **Safety surfaces** — dangling active-camera, component
   mutation during update, UI hit-test recursion, keyboard nav,
   pressure-plate world-space query.
4. **Rendering correctness** — IBL ScopedForwardZ wrap (fixes
   silent IBL corruption tainting every PBR material since day
   one), GPU SH projection, shadow-pass state save/restore,
   blend/cull RAII, `GpuCuller` upload consolidation.
5. **Data / asset parsing robustness** — path-sandbox choke-point,
   tinygltf `FsCallbacks`, `.cube` loader hardening, OBJ MTL
   support, portable vertex-hash.
6. **Animation correctness** — skeleton DFS ordering, CUBICSPLINE
   quaternion double-cover, motion-matching frame-of-reference,
   IK pole-vector, inertialisation axis-angle stability.
7. **Physics determinism** (gates Phase 11A replay) — fixed-
   timestep fold for character + breakable, raycast filters,
   `sphereCast` API, rotation-lambda break force, character pair
   filter.
8. **Subsystem wiring / dead-code cleanup** — finish-or-delete
   for `AsyncTextureLoader`, `FileWatcher`, accessibility no-op
   toggles, screen-reader bridge, AudioSystem force-active,
   listener-sync order, mixer-snapshot pointer, buffer-cache
   eviction.
9. **Input spec-vs-code reconciliation** — scancode vs keycode
   (currently contradicts the design doc), serialization
   ownership, axis-binding device, conflict-filter scope,
   re-registration assertion.
10. **Environment / splines** — centripetal Catmull-Rom (research
    doc mandated but not implemented), arc-length spline
    evaluator (Phase 10.8 CM7 cinematic cam needs it), GPU
    foliage culling, chunk-bounds terrain query.
11. **Systems update-order mechanism** — `ISystem::getUpdateOrder`
    or coarse phase tags so registration-order stops being the
    implicit contract.
12. **Editor undo / hygiene** — fix the `IsItemDeactivatedAfterEdit`
    pattern that drops drag-release events, add undo brackets to
    five inspector types that currently bypass `CommandHistory`,
    atomic prefab writes, panel registry.
13. **Performance hygiene** — `TextRenderer` batching, sprite /
    physics-2D per-frame allocations, foliage buffer grow-in-
    place, event-bus reentrancy sentinel.

Three items originally inside Phase 10.8 — `sphereCast`, centripetal
Catmull-Rom, arc-length spline — moved to Phase 10.9 Slices 7 + 10
and cross-referenced from Phase 10.8's header note. Reason: they
turned out to be more general primitives than a single-consumer
Phase 10.8 slice, and they need to land with their own test
coverage before Phase 10.8 CM4 / CM7 consume them.

Process discipline for this phase: every slice ships failing
design-doc-first regression tests as a "red" commit, then the fix
as a "green" commit — evidence that the test could have failed.
Each slice also triggers an independent-reviewer subagent pass
before ship, matching the pattern the ultrareview established.

No code changed in this commit; ROADMAP.md + CHANGELOG.md only.

### 2026-04-23 Phase 10.8 — Slice CM1: CameraMode base types

First slice of Phase 10.8 per the approved
`docs/PHASE10_8_CAMERA_MODES_DESIGN.md` — pure interface, no
concrete modes yet (those are CM2–CM7).

Added:

- `engine/scene/camera_mode.h` — `CameraModeType` enum,
  `CameraViewOutput` POD, `CameraInputs` per-frame context,
  abstract `CameraMode : public Component` base class.
- `engine/scene/camera_mode.cpp` — `blendCameraView(from, to, t)`
  transition-lerp primitive (linear mix on position / FOV / ortho
  / near / far, slerp on orientation, discrete snap at `t = 0.5`
  for projection type; `t` clamped to [0, 1]). Drives the 1st↔3rd
  toggle lerp per design §4.5.
- `tests/test_camera_mode.cpp` — 9 tests covering
  `CameraViewOutput` equality, `blendCameraView` endpoints /
  midpoint interpolation / orientation slerp / projection snap /
  `t` clamping, and a minimal `StubCameraMode` subclass proving
  the base-class clone contract compiles.

Design decisions landed in code:

- **Architecture M3** (ECS activation + blend utility) — `CameraMode`
  inherits `Component`, attaches per-entity. No priority queue; the
  scene's existing `setActiveCamera` pointer stays the single
  selector.
- **FOV authority F1** — `CameraViewOutput.fov` is the authoritative
  field each mode writes; `CameraComponent.fov` becomes display-only
  once CM2 ships.
- **Shake-composition contract (§4.6)** — pre-wired. `computeOutput`
  is a pure function; Phase 11A's shake offset will be stored
  separately on `CameraComponent` and applied at render-matrix
  assembly, never mutating the authoritative state.

Tests: 2727 pass / 0 fail / 1 skip (pre-existing); build clean.
No behaviour change in the shipping engine — the new types have no
call sites yet. CM2 lights up the first consumer by extracting the
existing first-person driving code into `FirstPersonCameraMode`.

### 2026-04-23 ROADMAP: sync Open-Source Release section with actual launch state

The "Still pending before flipping public" checklist in the
Open-Source Release section was stale — every item on it was
completed at the 2026-04-15 launch, but the ROADMAP still read as
if the repo hadn't gone public yet. Replaced that list with the
actual launch note + the four items still genuinely open post-
launch, each with a `docs/PRE_OPEN_SOURCE_AUDIT.md` cross-reference:

- **Pending:** VestigeAssets visibility flip + CI default restore
  (blocked on `milnet01/VestigeAssets` going public at ~v1.0.0)
- **Pending:** Third-party clean-clone build validation (maintainer
  did the 2026-04-15 dry-run; first community PR closes this
  asynchronously)
- **Pending:** Biblical content migration to private `Tabernacle`
  repo (maintainer cross-machine sync; not blocking engine work)
- **Pending:** Trademark decision on the "Vestige" name (deferred
  until there's something worth protecting at scale)

Also added two ongoing-discipline bullets to Post-Release
Commitments (weekly triage, quarterly ROADMAP revisit), both
referenced from `docs/PRE_OPEN_SOURCE_AUDIT.md` §224-225. The
quarterly-revisit bullet calls out today's Phase 10/11/16
restructure as the canonical example of what that revisit should
catch early.

No code changed.

### 2026-04-23 ROADMAP: resolve consumer-before-system dependency inversions

Audited the ROADMAP for features scheduled before their underlying
systems and restructured to make every ordering dependency explicit.
No code changed; this is a planning document rewrite.

**Changes:**

- **New Phase 10.8 — Rendering & Camera Prerequisites.** Pulled
  Camera Modes, Decal System, and Post-Processing Effects Suite
  out of Phase 10's tail bullets into their own phase that gates
  Phase 11B. These three sections were Phase 10 bullets with no
  stated ordering relationship to Phase 11; Phase 11B combat, damage
  feedback, hit decals, and vehicle cameras all depend on them.
  Phase 10 now holds pointers to the moved sections.
- **Phase 11 split into 11A (infrastructure) + 11B (features).**
  Phase 11A contains the runtime subsystems every Phase 11B feature
  consumes: CameraShakeSystem (finally consumes the shipped
  `clampShakeAmplitude`), ScreenFlashSystem (finally consumes
  `clampFlashAlpha`), Save File Compression (zstd — shared with
  Phase 12 asset packaging), Replay Recording Infrastructure
  (recorder / player / `.vreplay` format / determinism contract,
  moved out of the original Phase 11 Replay section), Behavior
  Tree Runtime (moved from Phase 16), and AI Perception System
  (moved from Phase 16). Phase 11B retains the original gameplay
  features (combat, health, inventory, save/checkpoint, hazards,
  vehicle, horror action, replay features) with references updated
  to cite Phase 10.8 and Phase 11A by name.
- **Phase 16 — Behavior Trees + AI Perception stubbed to pointers
  into Phase 11A.** Both sections were load-bearing for Phase 11B
  enemy / traffic / opponent AI; scheduling them in Phase 16 was
  the single biggest consumer-before-system inversion in the
  roadmap.
- **Phase 12 — added explicit ffmpeg pipeline bullet** under Asset
  Pipeline. Previously the Phase 11 replay "Export to MP4 via
  ffmpeg" referenced a pipeline that existed nowhere on the roadmap.
- **Phase 13 — cross-reference notes on duplicate entries** (SSS,
  volumetric lighting). Clarifies that Phase 10 ships the basic
  implementation and Phase 13 is the upgrade path, so readers don't
  mistake the duplication for two separate origins.
- **Fixed factual error in Phase 11B Horror Action Polish.** The
  "Diegetic holographic UI" bullet claimed `UIElement::worldProjection`
  and `engine/ui/ui_in_world.{h,cpp}` already exist. They don't —
  `find` across the engine tree returns no matches. Replaced with
  an explicit "not yet shipped" world-space UI pathway bullet that
  must land before the diegetic-UI features that consume it.
- **Phase 11B Damage feedback, status effects, vehicle cameras**
  updated to cite Phase 10.8 PP / Decal / Camera Modes and Phase 11A
  shake / flash by name, so readers can see the dependency chain
  without reconstructing it.

**Why:** the user asked whether systems are in place before features
that require them. An audit of Phase 10 → Phase 24 surfaced several
inversions — behavior trees / AI perception scheduled in Phase 16
but consumed by Phase 11 AI; camera shake / flash clamp consumers
not scheduled anywhere after Phase 10.7 deferred them; zstd only
mentioned as a Phase 12 asset bullet but needed by Phase 11 save;
ffmpeg referenced by Phase 11 replay but scheduled nowhere. Each
would have surfaced at implementation time as scope creep; the
split makes them named infrastructure bullets with explicit
consumers and predecessors.

### 2026-04-23 Phase 10.7 — Slice A3: AudioPanel unification

Completes Slice A by removing the parallel mixer state in the
editor AudioPanel. Bus-gain sliders now route through
`SettingsEditor::mutate` so edits flow through the Settings
persistence layer and land in the engine-owned mixer via the
existing `AudioMixerApplySink`. Mute / solo / ducking stay
panel-local — they're editor-only affordances, not user
preferences.

**Panel wiring** (`engine/editor/panels/audio_panel.{h,cpp}`).
New `wireEngineMixer(AudioMixer* engine, SettingsEditor* editor)`
hook. Once wired:
- `mixer()` returns the engine mixer instead of the local
  fallback — reads always reflect the authoritative state.
- Bus-gain slider edits call `editor->mutate([idx, g](Settings& s)
  { s.audio.busGains[idx] = g; })` which triggers the existing
  audio apply-sink path.
- `computeEffectiveSourceGain` follows the engine mixer, so the
  panel's meter / mute / solo logic always sees live state.
- Passing nullptr for either pointer keeps the panel on its
  local fallback — tests and standalone usage stay supported
  without a live engine.

**Engine wiring** (`engine/core/engine.cpp`). Right after
`wireSettingsEditorPanel`, the engine calls
`m_editor->getAudioPanel().wireEngineMixer(&m_audioMixer,
m_settingsEditor.get())` so the panel sees the authoritative
state from the first frame the editor is available.

**What stays panel-local** (per design decision B3):
- Mute / solo sets (per-entity editor-only affordances).
- Ducking state + params (authoring knobs, not user prefs).
- Reverb / ambient zone draft lists (editor staging for a
  future scene-owned runtime component).
- Zone overlay toggle.

**Tests.** 5 new `AudioPanelWire` cases in
`tests/test_audio_panel.cpp`:
- Unwired panel uses its local fallback mixer.
- Wiring an engine mixer redirects reads through the pointer.
- Wiring a null engine mixer keeps the local fallback.
- `computeEffectiveSourceGain` tracks the engine mixer's live
  state — external mutation shows up on the next read.
- `SettingsEditor` pointer alone (no engine mixer) is tolerated
  and stays on the local fallback (the slider path requires
  both pointers before it re-routes through Settings).

Full suite: 2718 passing (+5), 1 pre-existing skip. Slice A is
complete; Phase 10.7 now delivers every milestone in the
approved design:
- Subtitles: tick + render + declarative caption map (B1–B3).
- Photosensitive: bloom + flicker retrofits (C1–C2); shake +
  flash deferred to Phase 11 per scope reduction.
- Audio: bus tagging + per-frame gain-chain pass + panel
  unification (A1–A3).

Camera shake and flash overlay are the only deferred items —
and they remain deferred because their *originating subsystems*
don't exist in the codebase yet. When Phase 11 builds them, the
clamp helpers (`clampShakeAmplitude`, `clampFlashAlpha`) are
unit-tested and ready for wiring.

### 2026-04-23 Phase 10.7 — Slice A2: per-frame gain-chain pass

Mid-play Settings slider moves are now audible. `AudioEngine`
maintains a playback registry keyed by OpenAL source ID that
stores each live source's bus + authored volume; a per-frame
`updateGains()` sweep composes `master × bus × sourceVolume`
via `resolveSourceGain` and re-uploads `AL_GAIN` for every
still-playing source. Prior to this slice the initial gain was
set once at acquisition and never revisited — sliding the Music
bus to 0 while a cue played had no effect until the next
acquisition.

**Pure helper** (`engine/audio/audio_mixer.{h,cpp}`). New
`resolveSourceGain(mixer, bus, sourceVolume)` returns the
clamped `master × bus × volume` product. Source volume is
pre-clamped to [0, 1] before the multiply so an authoring bug
cannot push composed gain above 1.0. Kept pure-function so the
gain math is unit-testable without an AL context.

**Registry** (`engine/audio/audio_engine.{h,cpp}`). Adds
`std::unordered_map<ALuint, SourceMix>` where
`SourceMix = { AudioBus bus, float sourceVolume }`. Every
`playSound*` / `playSound2D` now:
- Accepts an optional `AudioBus bus` parameter (Sfx default,
  Ui for `playSound2D`).
- Records the pair into the registry at acquisition.
- Uploads `resolveSourceGain(snapshot, bus, volume)` as the
  *initial* gain instead of the raw `volume`, so bus-gain
  moves take effect on the first played frame, not the second.
- Deregisters on `releaseSource` / `stopAll` /
  `reclaimFinishedSources`.

**Sweep** (`AudioEngine::updateGains`). Reaps `AL_STOPPED`
sources first (via the existing `reclaimFinishedSources`),
then iterates the remaining registry and uploads the
recomposed gain. No-op when audio is unavailable or the
registry is empty.

**Wiring** (`engine/systems/audio_system.cpp`).
`AudioSystem::update` publishes the current engine mixer via
`setMixerSnapshot(m_engine->getAudioMixer())` and calls
`updateGains()` each frame, so the audio engine's snapshot
stays fresh and the registry sweep runs at frame rate.

**Tests.** 7 new `AudioMixerResolve` cases in
`tests/test_audio_mixer.cpp`:
- Default mixer + unity volume → unity gain on every bus.
- `master × bus × volume` composition (0.5 × 0.8 × 0.75 = 0.30).
- Negative source volume clamps to 0.
- Above-unity source volume clamps to 1.
- Master bus does not double-apply its own gain.
- Zero master silences every bus.
- Zero bus silences only that bus.

The registry + AL sweep is not directly unit-tested — testing
it would need a live AL context, which the project policy
excludes from the CPU-unit test suite (see test_audio_hrtf.cpp
for the same trade-off). The sweep's gain values come from
`resolveSourceGain`, which is fully covered.

Full suite: 2713 passing (+7), 1 pre-existing skip. Next:
A3 — AudioPanel unification.

### 2026-04-23 Phase 10.7 — Slice A1: AudioBus field on AudioSourceComponent

Adds `AudioBus bus` to `AudioSourceComponent` so every source can
be routed through one of the 6 mixer buses (Master / Music /
Voice / Sfx / Ambient / Ui). Defaults to `AudioBus::Sfx` — the
implicit routing the engine used before the mixer landed, so
existing scenes sound identical until authors explicitly re-tag
a source.

Component `clone()` preserves the bus. No serializer work
(`entity_serializer.cpp` does not yet handle
`AudioSourceComponent` — that's existing scope, not introduced
by this slice).

Sets up the per-frame gain-chain pass in Slice A2, which will
iterate owned components and compose
`master × bus × volume × occlusion × ducking → AL_GAIN` each
frame so Settings slider moves are heard mid-play rather than
only on the next clip acquisition.

Tests: `tests/test_audio_source_component.cpp` — 5 cases:
- Default bus is Sfx.
- Bus is assignable.
- `clone()` preserves bus and other fields.
- Cloned component has independent bus state (no shared state).
- Every bus in `AudioBusCount` is round-trippable.

Full suite: 2706 passing (+5), 1 pre-existing skip.

### 2026-04-23 Phase 10.7 — Slice C: photosensitive consumer retrofits

Closes the Settings → runtime gap for photosensitive safe mode
on the two consumers that actually exist in the codebase today
(per §4.3 of the design doc). Camera shake and flash overlay
retrofits remain deferred to Phase 11 when the originating
subsystems land.

**C1 — bloom intensity clamp.** `Renderer` gains
`setPhotosensitive(enabled, limits)` and reads the stored state
at the bloom upload site:
```cpp
const float bloomIntensityUpload = limitBloomIntensity(
    m_bloomIntensity,
    m_photosensitiveEnabled,
    m_photosensitiveLimits);
m_screenShader.setFloat("u_bloomIntensity", bloomIntensityUpload);
```
The authored `m_bloomIntensity` is preserved; only the uploaded
value is clamped. Disabling safe mode returns bloom to its
authored look without any per-frame cost (the helper is an
identity pass when `enabled == false`).

`Engine::run()` pushes the engine's current photosensitive state
into the renderer once per frame in the existing
`AccessibilityTick` profiler scope so a mid-session Settings
toggle takes effect on the next drawn frame.

**C2 — particle flicker clamp.** `ParticleEmitterComponent::
getCoupledLight` gains optional `photosensitiveEnabled` +
`limits` parameters (defaults preserve existing behaviour for
any caller that doesn't thread state through). The emitter's
`flickerSpeed` is an angular coefficient; the retrofit converts
it to Hz (`speed / 2π`), runs `clampStrobeHz`, and converts back
— so a `flickerSpeed` of 20 (≈ 3.18 Hz) clamps down to ≈ 12.57
when safe mode is on (2 Hz × 2π). The harmonic 3.1× modulation
stays intact; it sits within the WCAG 10 % ΔL band even when the
base runs at the ceiling.

`Scene::collectRenderData` gains matching optional parameters
and threads the state through `collectRenderDataRecursive` into
every `getCoupledLight` call. The main render path in
`Engine::run` passes the engine's state; offline paths (SH grid
bake, cubemap probe capture) keep the defaults — bakes capture
the authored look, not the safe-mode look, which is the right
call for asset authoring.

**Tests.** `tests/test_photosensitive_retrofit.cpp` — 5 cases:
- Safe mode off + safe mode on with inert limits produce
  identical coupled-light diffuse output (identity preservation).
- Above-ceiling flicker (`flickerSpeed = 20`, ≈ 3.18 Hz) under a
  2 Hz cap produces a *different* diffuse than the unclamped
  version — the clamp is actually doing work.
- Below-ceiling flicker (`flickerSpeed = 10`, ≈ 1.59 Hz) under
  the default 2 Hz cap is an identity pass.
- The coupled light is still emitted when safe mode clamps
  (safe mode slows flicker, never suppresses the light).
- Sanity on the Hz conversion: 20 → 3.18309886 Hz → clamp to
  2 Hz → 12.566371 angular coefficient.

The C1 bloom clamp is validated through the existing
`test_photosensitive_safety.cpp` coverage of `limitBloomIntensity`
— the uniform-upload retrofit itself is a one-line wrap that
would need a live GL context to test end-to-end, outside the
unit-test budget.

Full suite: 2701 passing (+5), 1 pre-existing skip. Slice C
complete; Phase 10.7 now proceeds to Slice A (audio bus tagging
+ gain chain — the largest remaining slice).

### 2026-04-23 Phase 10.7 — Slice B3: declarative caption map

`CaptionMap` is the data layer that lets a project author captions
in JSON once and have them fire automatically when the matching
clip plays. It is a pure lookup primitive — load once from
`assets/captions.json` at engine init, then `enqueueFor(clipPath,
queue)` pushes a `Subtitle` into the engine-owned queue when the
clip has a mapped entry, or no-ops silently when it doesn't.

**Schema** (one-per-game, root object keyed by clip path):
```json
{
  "audio/dialogue/moses_01.wav": {
    "category": "Dialogue",
    "speaker":  "Moses",
    "text":     "Draw near the mountain.",
    "duration": 3.5
  }
}
```

- `category` stringifies to `SubtitleCategory`; unknown values
  default to `Dialogue` (least surprising for authored content).
- `speaker` is optional, empty for non-dialogue.
- `duration` in seconds; ≤ 0 or missing falls back to
  `DEFAULT_CAPTION_DURATION_SECONDS = 3.0`.
- Entries with empty `text` are skipped at load (authoring noise).

**Wiring.** `Engine` gains `m_captionMap` + `getCaptionMap()`.
During `initialize()`, the engine calls `m_captionMap.loadFromFile(
assetPath + "assets/captions.json")`. Missing file = empty map
(silent — not every project ships captions). Malformed JSON or
non-object root logs a warning and leaves the map empty.

**Size-capped load.** Uses the canonical
`JsonSizeCap::loadJsonWithSizeCap` path so a malicious caption
file can't OOM the process (AUDIT H4 / M17–M26 discipline).

**Call-site integration.** For this slice game code invokes
`engine.getCaptionMap().enqueueFor(clipPath, engine.getSubtitleQueue())`
alongside `playSound(clipPath, …)` manually. Auto-trigger on
playback lands naturally in Slice A2, where the new per-frame
AudioSystem pass iterates `AudioSourceComponent`s and can fire the
caption look-up at the same time. Keeping the two decoupled means
B3 can ship before the audio rework and the audio rework doesn't
carry caption-authoring concerns.

**Tests.** `tests/test_caption_map.cpp` — 15 cases covering:
- Defaults: empty map, null lookup.
- Parsing: Dialogue / Narrator / SoundCue categories; unknown →
  Dialogue; missing and non-positive durations → default.
- Authoring hygiene: entries with empty text are skipped.
- Malformed inputs: invalid JSON and non-object roots leave the
  map empty without crashing.
- `reload` clears previous entries.
- `enqueueFor` no-ops for unknown clips; pushes onto the queue
  for mapped clips.
- `clear()` empties the map.
- `parseSubtitleCategory` known + unknown strings.

Full suite: 2696 passing (+15), 1 pre-existing skip. Slice B is
now complete; Phase 10.7 proceeds to Slice C (photosensitive
consumer retrofits — bloom + flicker).

### 2026-04-23 Phase 10.7 — Slice B2: subtitle HUD render pass

Captions now appear on screen. `UISystem::renderUI` picks up
`Engine::getSubtitleQueue().activeSubtitles()` once per frame and
draws them through a new `SubtitleRenderer` as the last pass of
the 2D overlay — on top of both the root canvas and the modal
canvas so system-critical captions are never occluded by gameplay
UI.

**Pure-function layout** (`engine/ui/subtitle_renderer.{h,cpp}`).
`computeSubtitleLayout(queue, params, measure)` emits one
`SubtitleLineLayout` per active caption with every pixel pre-
computed. The measure callable takes the production
`TextRenderer::measureTextWidth`; tests pass a deterministic stub
so the 12 layout tests run without GL. The separation is the same
shape the engine uses for fog + photosensitive safety — pure spec
layer + thin GL dispatch.

**Layout recipe.**
- `basePx = 46 × (viewport_h / 1080) × subtitleScaleFactorOf(preset)`
  — Game Accessibility Guidelines baseline, scaled linearly with
  resolution so a 4K display renders at 92 px before preset scaling.
- Plate: black @ 50 % alpha, 8 px horizontal / 4 px vertical padding.
- Stack: newest caption at bottom, older captions above, 4 px gap.
- Per-category styling: Dialogue (yellow speaker label + white body,
  TLOU2 convention); Narrator (plain white); SoundCue (bracketed,
  cyan-grey — Sea of Thieves convention).

**Draw pass.** Plates go through the existing
`SpriteBatchRenderer`; the batch is flushed; text goes through
`TextRenderer::renderText2D`. Two draw calls per frame at typical
caption volumes (1–3 active). Depth test off, standard alpha
blending, nestled between root-canvas and end-of-overlay cleanup
so subtitles inherit `UISystem`'s GL state save/restore.

**Tests.** `tests/test_subtitle_renderer.cpp` — 16 cases:
- `styleFor` returns expected category colours.
- Empty queue → empty layout.
- Dialogue composes speaker prefix; SoundCue wraps in brackets;
  Narrator is plain body.
- Base pixel scales linearly with viewport height (1080p → 2160p).
- Size preset (Small 1.0× → XL 2.0×) multiplies base exactly.
- Plate width equals measured text width + 2× padding.
- Plate is horizontally centred.
- Newest caption sits at the bottom; gaps are constant.
- Plate bottom edge aligns with `screenHeight × (1 - bottomMarginFrac)`.
- Text baseline sits inside plate padding.
- Null measure callable degrades to padding-only plate.

**Pending** — Slice B3 is the declarative `assets/captions.json`
→ auto-enqueue wiring. Until it lands, captions must be enqueued
through `Engine::getSubtitleQueue().enqueue(...)` by game code.

### 2026-04-23 Phase 10.7 — Slice B1: SubtitleQueue tick wired into run loop

`Engine::run()` now calls `m_subtitleQueue.tick(deltaTime)` each
frame as step 4d (between domain-system update and controller
update). Previously the engine owned `m_subtitleQueue` but never
ticked it — any caption `enqueue()` would sit at its full
duration forever because the countdown was never driven.

Wrapped in a `VESTIGE_PROFILE_SCOPE("AccessibilityTick")` so the
frame profiler attributes the ~sub-microsecond cost correctly
and leaves room for adjacent per-frame store ticks (audio
gain-pass in slice A2, ducking slew when that slice lands).

Render-side consumption arrives in slice B2 — enqueued captions
now expire at the correct time, but aren't visible until B2
lands the 2D HUD pass. No new test: 17 existing
`tests/test_subtitle.cpp` tests already cover `tick(dt)`
semantics across durations, overshoots, and negative deltas;
the wire-up itself is a one-line integration that slice B2
validates visually.

### 2026-04-23 Phase 10.7 — design doc approved, scope reduced

`docs/PHASE10_7_DESIGN.md` drafted + approved on the same day. Six
blocking §6 questions signed off: audio gain chain runs per-frame
(A1); editor `AudioPanel` unifies via `SettingsEditor` with mute /
solo / ducking staying panel-local (B3); renderer bloom takes a
setter (P1) while the subtitle HUD pass takes `Engine&` (P2);
captions load from a single `assets/captions.json` at engine init;
photosensitive Slice C reduced to 2-of-4 consumers.

**Scope honesty for Slice C.** Camera shake and flash overlay
subsystems do not exist in the codebase today — grep finds no
shake accumulator, no hit-flash, no screen-wipe. Their clamp
helpers (`clampShakeAmplitude`, `clampFlashAlpha`) sit idle.
Phase 10.7 does not invent these subsystems just to clamp them
(CLAUDE.md Rule 6); both retrofits are deferred to Phase 11,
which must wire the clamp helper into the originating subsystem
as part of its initial implementation. `ROADMAP.md` Phase 10.7
amended accordingly.

**Slice order:** B (subtitles) → C (photosensitive) → A (audio).
B first for smallest blast radius and most visible progress; A
last as the largest slice.

### 2026-04-22 Phase 10 — Slice 13.5e: remaining live-apply sink wiring

Closes every `SettingsEditor::ApplyTargets` slot. Before this slice,
audio / subtitle / HRTF / photosensitive sinks were abstract-only:
the design was in place but the engine had no central stores for
them to write into. This slice adds those stores as `Engine`
members, introduces the two missing concrete sinks, and routes
all seven sinks through `SettingsEditor` at construction.

**Engine-owned stores** (`engine/core/engine.{h,cpp}`):
- `m_audioMixer` — authoritative bus-gain table. Previously only the
  editor's `AudioPanel` owned an `AudioMixer`; now the engine owns
  one and exposes it via `getAudioMixer()`.
- `m_subtitleQueue` — central caption queue. Game code enqueues
  captions; the not-yet-wired HUD render pass will tick + draw.
- `m_photosensitiveLimits` + `m_photosensitiveEnabled` — central
  photosensitive-safety state. Read via `photosensitiveLimits()` +
  `photosensitiveEnabled()`; consumers pass these to the existing
  `clampFlashAlpha` / `clampShakeAmplitude` / `clampStrobeHz` /
  `limitBloomIntensity` helpers.

**New concrete sinks** (`engine/core/settings_apply.{h,cpp}`):
- `AudioEngineHrtfApplySink` — wraps `AudioEngine::setHrtfMode` so
  the HRTF toggle actually reaches OpenAL. Safe on non-initialized
  AudioEngine (the underlying `applyHrtfSettings` guards with
  `m_available`).
- `PhotosensitiveStoreApplySink` — writes `enabled` + `limits` to
  pointers into the engine's stores. Null-pointer tolerant for test
  cases that only care about orchestration calls.

**Engine wiring** (`Engine::initialize`):
- Constructs `AudioMixerApplySink`, `SubtitleQueueApplySink`, and
  `PhotosensitiveStoreApplySink` unconditionally (the stores always
  exist).
- Constructs `AudioEngineHrtfApplySink` conditionally on the
  `AudioSystem` being present in the registry — HRTF requires
  AudioEngine access.
- All four sinks land in `ApplyTargets` alongside the existing
  display / renderer-accessibility / UI-accessibility / input sinks.

**New tests** (`tests/test_settings.cpp`): 5 additions —
- `PhotosensitiveStoreApplySinkWritesEnabledAndLimits`
- `PhotosensitiveStoreApplySinkTolerantOfNullPointers`
- `PhotosensitiveStoreApplySinkRoundTripsFromSettings`
- `AudioEngineHrtfApplySinkForwardsMode`
- `AudioHrtfApplyPicksAutoWhenEnabled`

**Follow-on scope (Phase 10.7 on ROADMAP.md).** The stores are
authoritative but downstream consumers don't read from them yet:
- AudioSource playback doesn't multiply in `AudioMixer` bus gains
  — the OpenAL gain resolution needs retrofitting.
- `SubtitleQueue::tick` isn't called from the per-frame loop,
  and no HUD render reads `activeSubtitles()`.
- Photosensitive clamps are called at call sites that pass their
  own local `PhotosensitiveLimits` (usually default-constructed),
  not the engine's central store.

These consumer retrofits are a separate phase because each affects
multiple unrelated files and warrants its own testing surface.

Full suite 2666 passing (up from 2661 with the 5 new sink tests;
build clean, 0 warnings).

**Phase 10 settings chain — all 7 ApplyTargets slots now live.**

### 2026-04-22 Post-Phase-10 audit fixes

Mandatory post-phase audit (CLAUDE.md Rule 9) triggered by Phase 10 +
Phase 10.5 completion. Scans: `tools/audit/audit.py -t 1 2 3`, cppcheck,
clang-tidy, semgrep (`p/security-audit` + `p/c`), gitleaks (re-scoped
to source only — `build/` artifacts excluded). 0 critical / 0 high /
~15 actionable out of 1503 raw findings (~99% noise floor as expected
for a mature codebase).

**Real bug fix:**
- `engine/audio/audio_music_stream.cpp` — `bugprone-branch-clone`:
  `if (framesNeeded >= chunk)` and its else-arm both set
  `frames32 = chunk`. Dead branching collapsed to a single assignment.
  Behaviour unchanged; the vestigial branch was cleanup debt.

**Integer-overflow hardening** — feature-index math in motion matching
was computed as `int * int` before widening to `size_t`. Safe for
current motion-DB sizes but fragile for future large DBs. Widened each
operand to `size_t` before the multiply so pointer arithmetic no
longer goes through int32:
- `engine/animation/kd_tree.cpp` — 6 call sites (`reserve`,
  `nth_element` lambda × 2, `splitValue` lookup, `searchRecursive`,
  `bruteForceSearch`).
- `engine/animation/motion_database.cpp` — 7 call sites across
  `build`, `extractFeatures`, `search`'s linear-scan fallback, and
  the mirrored-rebuild path.

**Audit-surfaced perf / style:**
- `engine/animation/sprite_animation.{h,cpp}` — `clipNames()` now
  returns `const std::vector<std::string>&` instead of by-value
  (avoids copy on every call; no callers outside the definition).
- `engine/core/settings_editor.{h,cpp}` — `ApplyTargets` constructor
  arg taken by const reference (was pass-by-value).
- `engine/audio/audio_clip.cpp` — `MAX_AUDIO_FRAMES` constant uses
  `48000ULL * 60 * 30` so the multiplication widens explicitly
  instead of happening in int first.
- `tests/test_first_run_wizard.cpp` — `FilterTmpDir::m_root` moved
  to the constructor initializer list.

**False positives documented in triage (no action):**
`safe_math.h` ternary guard on `std::log`, `system_registry.h` cppcheck
misparse of a function template as a member, `audio_clip.cpp`'s
intentional `stb_vorbis.c` header-only include, 14 × clang-tidy
`init-variables` on C-decoder output params, 100 × `shared_ptr` pattern
hits on motion-matching owned clips, 88 × OpenGL-state unbind calls,
576 × clang-tidy style (readability-math-missing-parentheses etc.
per `feedback_clang_tidy_stop_chasing.md` — advisory, not gates),
and 83 × gitleaks hits in `build/` CMake artifacts.

All 2661 tests pass after fixes (build clean, 0 new warnings).

### 2026-04-22 Phase 10 — Live-apply sink wiring (slice 13.5d)

Closes out Phase 10 settings. Wires the concrete production sinks
to the real engine subsystems so `SettingsEditor::mutate()` calls
from the panel drive subsystems in real-time.

- `engine/core/engine.{h,cpp}` now owns three concrete sink
  instances as `std::unique_ptr` members:
  - `WindowDisplaySink`   — wraps the live `Window`.
  - `RendererAccessibilityApplySinkImpl` — wraps the live `Renderer`.
  - `UISystemAccessibilityApplySink`     — wraps the live `UISystem`
    fetched from the system registry.
- These are constructed right after Settings load and passed into
  `SettingsEditor::ApplyTargets` alongside the already-wired
  `inputMap` pointer. Resolution / fullscreen / vsync / colour
  vision mode / post-process toggles / UI scale / high-contrast /
  reduced-motion now live-update as the user drags sliders in the
  Settings panel.
- `forceLiveApply()` is called once after construction so any
  persisted state (e.g. reducedMotion=true from a previous
  session) is pushed to subsystems immediately on launch, rather
  than waiting for the user to touch a control.
- Audio + subtitle + HRTF + photosensitive sinks remain abstract-only
  — the engine doesn't currently centralise the subsystems they
  target (AudioSystem has no exposed mixer gain surface,
  SubtitleQueue isn't engine-owned, the photosensitive caps are
  consumed at individual call sites rather than from a central
  store). Wiring those lands once each subsystem exposes an
  engine-owned store.

No new tests — sinks are unit-tested at construction in slices
13.3a / 13.3b, the orchestrator is unit-tested with recording mocks
in 13.5a, and the new code in `Engine::initialize` is straight
pointer plumbing. The user-visible acceptance gate is opening
`Help → Settings...` in the editor and observing the live preview.

Full suite 2661 passing.

**Phase 10 settings chain complete end-to-end** (slices 13.1 → 13.5d).

### 2026-04-22 Phase 10 — Click-to-rebind capture (slice 13.5c)

Completes the interactive keybinding surface in the Settings editor.
The Controls tab's three-column binding table now captures real
keyboard / mouse input when a slot is clicked.

- `settings_editor_panel.{h,cpp}` — rebind modal:
  - Each cell in the Action / Primary / Secondary / Gamepad table
    is now a button showing the current binding label. Clicking
    enters capture mode for that action + slot.
  - A modal popup prompts `Press any key or mouse button…` +
    Esc-cancels + Delete-clears.
  - Capture polls `ImGui::IsKeyPressed` for a curated set of
    supported keys (A-Z, 0-9, F1-F12, Space, Enter, Tab, Backspace,
    arrows, Shift/Ctrl/Alt) + `IsMouseClicked` for LMB/RMB/MMB.
  - ImGuiKey → GLFW_KEY_* mapping table keeps the on-disk format
    (GLFW keycodes carried in `InputBinding::code`) consistent
    with the rest of the engine.
  - Conflict detection: the modal shows a warning colour + message
    if the captured binding already fires other actions. Does NOT
    block assignment — intentional double-bindings are rare but valid.
  - Capture is non-blocking for the Settings editor — the modal
    runs as ImGui popup and the rest of the panel stays interactive.
- `Engine` now owns an `InputActionMap m_inputActionMap` with four
  demo actions pre-registered (`ToggleWireframe` → F1,
  `CycleTonemap` → F2, `Screenshot` → F11, `ToggleFullscreen` →
  F12) so the rebind UI has something to exercise out of the box.
  Persisted keybindings from `Settings.controls.bindings` are
  applied on top via `applyInputBindings` right after registration.
- `Editor::wireSettingsEditorPanel` now receives the input map
  pointer, so the Controls tab shows a populated table. Game
  projects that build on Vestige can add / override / clear the
  default actions before calling `Settings::apply`.

Full suite 2661 passing (1 pre-existing skip). Phase 10 settings
chain is now **feature complete end-to-end** through slices 13.1 →
13.5c. Remaining follow-on: slice 13.5d (wiring the live-apply
sinks to the real Renderer / AudioMixer / UISystem instances so
mutations in the panel drive subsystems in real-time).

### 2026-04-22 Phase 10 — Settings editor panel (slice 13.5b)

ImGui editor panel wrapping the `SettingsEditor` orchestrator.
User-facing Settings UI now lives in the editor — reachable via
`Help → Settings...`. Slice 13.5c adds click-to-rebind capture;
slice 13.5d wires the live-apply sinks to the real subsystems.

- `engine/editor/panels/settings_editor_panel.{h,cpp}` — five-tab
  panel (Display / Audio / Controls / Gameplay / Accessibility)
  + footer with per-category Restore + Restore All + Revert +
  Apply buttons and a live dirty indicator.
- Widgets per tab:
  - **Display**: resolution inputs, fullscreen / vsync checkboxes,
    quality preset combo, render-scale slider, Restore button.
  - **Audio**: six bus-gain sliders (Master / Music / Voice / SFX
    / Ambient / UI), HRTF toggle, Restore button.
  - **Controls**: mouse sensitivity, invert Y, gamepad left +
    right deadzone sliders. Three-column keybinding **table**
    (Action / Primary / Secondary / Gamepad) with `(Rebind capture
    lands in slice 13.5c)` placeholder note. Restore button.
  - **Gameplay**: doc-stub; game projects mutate
    `SettingsEditor::pending().gameplay` directly for their own UI.
    Restore button.
  - **Accessibility**: UI scale combo, high-contrast + reduced-motion
    + subtitles-enabled checkboxes, subtitle-size combo, color-vision
    filter combo, post-process accessibility toggles (DoF / motion
    blur / fog + intensity slider), photosensitive safe-mode
    section (shown when enabled: max flash alpha, shake scale,
    max strobe Hz). Restore button.
- Footer: dirty indicator (`All changes saved.` vs `Unsaved changes.`),
  `Restore All Defaults`, `Revert` (disabled when clean), `Apply`
  (disabled when clean, saves via `SettingsEditor::apply`).
- Every widget mutation routes through `SettingsEditor::mutate`
  so the live-apply contract holds once sinks are wired (13.5d).
- `Editor::wireSettingsEditorPanel(editor*, inputMap*, path)` +
  member `m_settingsEditorPanel`. `Help → Settings...` entry opens
  it. `Help → First-Run Wizard` also added so users can rerun
  onboarding without digging through settings.
- `Engine` owns the `SettingsEditor` (std::unique_ptr). Constructs
  it after loading `Settings` from disk; passes to the editor
  panel via `wireSettingsEditorPanel`. Apply targets are null in
  this slice (panel still works end-to-end for persistence, restore,
  revert, apply; live-apply plumbing is the next slice).

No new tests — the orchestrator tests from 13.5a cover the mutation
surface, and ImGui panels are hard to unit-test cleanly without
an ImGui context (the panel is a thin wrapper that forwards widget
events into `SettingsEditor::mutate` calls, which are already
exhaustively tested). Visual verification on the editor's
`Help → Settings...` menu entry is the acceptance gate.

Full suite 2661 passing (1 pre-existing skip).

Next: slice 13.5c — click-to-rebind modal in the Controls tab, +
slice 13.5d — live-apply sink wiring for audio / ui / renderer
subsystems.

### 2026-04-22 Phase 10 — SettingsEditor orchestrator (slice 13.5a)

Final slice of the Phase 10 settings chain begins. Ships the
`SettingsEditor` state machine that sits behind every UI — the
load-bearing piece — and is fully headless-testable.

- `engine/core/settings_editor.{h,cpp}` — `SettingsEditor`:
  - Owns `m_applied` (last-committed, matches disk + subsystems)
    and `m_pending` (user's in-progress edits).
  - **Live-apply semantics**: every `mutate()` pushes the modified
    `m_pending` through every configured sink so users see the
    change immediately. `apply()` performs only the persistence
    step (save to disk + advance `m_applied`); a failed write
    leaves the editor still dirty so the user can retry / revert
    without silent data loss.
  - **Per-category restore granularity**: five dedicated reset
    methods (`restoreDisplayDefaults`, `restoreAudioDefaults`,
    `restoreControlsDefaults`, `restoreGameplayDefaults`,
    `restoreAccessibilityDefaults`) plus `restoreAllDefaults`.
    Granular so a user's `2.0×` scale + `high-contrast` survives
    a single-category reset.
  - **Onboarding + schemaVersion preservation across Restore All**:
    a full reset does NOT clear `onboarding.hasCompletedFirstRun`
    or revert `schemaVersion`, so clicking "Restore All Defaults"
    cannot accidentally re-trigger the first-run wizard or break
    the next load's migration path.
  - `ApplyTargets` struct holds raw pointers to every apply sink
    (display / audio / hrtf / ui-accessibility / renderer / subtitle
    / photosensitive / input map). Any target may be null — caller
    wires only what they want driven by this editor. Non-owning.
  - `revert()` re-pushes `m_applied` through every sink so live
    preview rolls back. `forceLiveApply()` is an escape hatch for
    callers that want the editor to drive subsystems from scratch.
  - `restoreControlsDefaults` / `restoreAllDefaults` also call
    `InputActionMap::resetToDefaults()` when one is attached, so
    the live rebind state follows the struct reset.

Tests: 13 new in `tests/test_settings.cpp`:
- Initial state matches applied, not dirty.
- Mutate diverges pending, marks dirty.
- Mutate pushes through every configured sink once per call.
- Apply commits + persists + clears dirty; reloading from disk
  round-trips the values.
- Apply with failed write keeps editor dirty (retry path).
- Revert restores from applied + re-pushes through sinks.
- Per-category restores are isolated (only their section resets).
- Restore All preserves onboarding + schemaVersion.
- Per-category restore is live-applied through sinks.
- Dirty tracking is correct across mutate/apply/revert cycles.
- RestoreControls resets the attached InputActionMap's bindings.

Full suite 2661 passing (1 pre-existing skip).

Next (slice 13.5b): ImGui `SettingsEditorPanel` wiring the orchestrator
to per-category widgets. Slice 13.5c adds the 3-column keybinding
rebind dialog.

### 2026-04-22 Phase 10 — Input bindings extract + apply (slice 13.4)

Bridges `Settings::controls.bindings` (the on-disk
`std::vector<ActionBindingWire>`) to the in-memory `InputActionMap`.
Second-last slice of the Phase 10 settings chain.

- `extractInputBindings(map) → std::vector<ActionBindingWire>` —
  serialises every registered action in `map.actions()` order.
  Covers all three slots (primary / secondary / gamepad). Device
  → wire-string mapping: Keyboard → `"keyboard"`, Mouse → `"mouse"`,
  Gamepad → `"gamepad"`, None → `"none"`. Empty `map` extracts
  to an empty vector.
- `applyInputBindings(wires, map)` — reverse direction. Enforces
  the init-order contract documented in
  `PHASE10_SETTINGS_DESIGN.md`: game code registers actions before
  `Settings::load`; an id in `wires` that doesn't resolve to a
  registered action is **dropped with a logged warning** (prevents
  typos in a hand-edited `settings.json` from creating ghost
  actions, and protects against stale saves referencing actions
  removed from a newer engine build). Actions registered on the
  map but absent from `wires` keep their current bindings (no
  clobbering to defaults).
- Unbound-binding normalisation: a wire with `device == "none"` or
  `scancode < 0` (either condition) collapses to the fully-unbound
  `InputBinding::none()` on apply, so `isBound()` stays consistent.

Wire-format limitation documented inline: `scancode` currently
carries the in-memory GLFW *key code* rather than a true scancode.
Layout-preserving scancode translation (WASD stable across AZERTY /
Dvorak) needs `glfwGetKeyScancode` + a reverse table and lands in
a follow-on slice. Keep-values-identical means 13.4's round-trip
is testable without a GLFW context and matches what users see when
hand-editing `settings.json`.

Tests: 10 new in `tests/test_settings.cpp` (design-doc target: 10):
- Extract emits every action in insertion order.
- Extract round-trips device strings across all four device enum
  values.
- Extract preserves all three binding slots.
- Apply updates bindings of registered actions (remap flow).
- Apply drops phantom ids without auto-registering — map size
  stays at its registered count.
- Apply preserves actions that are registered but absent from the
  wire list (no clobber).
- Unknown device string falls back to None.
- Negative scancode on an otherwise-valid wire collapses to fully
  unbound.
- Full extract → apply round-trip is lossless across all three
  device kinds.
- End-to-end via `Settings::controls.bindings` — a wire populated
  through the settings struct reaches a registered map's action
  intact.

Full suite 2648 passing (1 pre-existing skip). Slice 13.4 complete.

Next: slice 13.5 — Settings UI wiring (per-category control widgets
into `buildSettingsMenu`) + Apply / Revert / Restore Defaults buttons.

### 2026-04-22 Phase 10 — Renderer + subtitle + HRTF + photosensitive apply (slice 13.3b)

Completes slice 13.3 — adds the four remaining accessibility / audio
apply paths that 13.3a deferred. Every `Settings::accessibility`
field and the `audio.hrtfEnabled` bool now have a typed apply sink
+ production forwarder + pure-function orchestrator.

- `RendererAccessibilityApplySink` — color vision mode +
  post-process (DoF / motion blur / fog + fog intensity + reduce-motion
  fog) pushed through `Renderer` in one call per field group.
  `applyRendererAccessibility` translates the wire-format colour-vision
  string (`"none"` / `"protanopia"` / `"deuteranopia"` / `"tritanopia"`)
  to the typed `ColorVisionMode` enum, maps `PostProcessAccessibilityWire`
  → `PostProcessAccessibilitySettings`.
- `SubtitleApplySink` + `SubtitleQueueApplySink` — `subtitlesEnabled`
  bool + `subtitleSize` string (`"small"` / `"medium"` / `"large"` /
  `"xl"`). The `enabled` flag is held on the sink until slice 14's UI
  wiring queries it at render time (SubtitleQueue doesn't currently
  expose an enable toggle; its owner drains it regardless).
- `AudioHrtfApplySink` — `audio.hrtfEnabled` bool → `HrtfMode`.
  `true` → `HrtfMode::Auto` (driver decides based on headphones
  detection), `false` → `HrtfMode::Disabled` (force off).
- `PhotosensitiveApplySink` — `photosensitiveSafety.enabled` bool +
  `PhotosensitiveLimits` struct (maxFlashAlpha / shakeAmplitudeScale
  / maxStrobeHz / bloomIntensityScale). No production-concrete impl
  yet — the caps are consumed by individual effect-site call sites
  (`clampFlashAlpha`, `clampShakeAmplitude`, …), so "applying" means
  writing to a central engine-side store. The abstract sink + pure
  apply function land now; the engine store lands when the first
  effect that reads from it needs to.

Tests: 9 new in `tests/test_settings.cpp` — every colour-vision
string → enum (4 values), unknown fallback, post-process wire-field
forward, every subtitle size → enum (4 values), subtitle enabled
forward, HRTF bool → HrtfMode (both polarities), photosensitive
enabled + limits forward, end-to-end JSON → fromJson → applyRenderer
round-trip, SubtitleQueue production sink mutates state. Full suite
2638 passing (1 pre-existing skip).

Slice 13.3 is now complete end-to-end. Next: slice 13.4 — input
bindings toJson/fromJson with scancode wire format.

### 2026-04-22 Phase 10 — Audio + UI accessibility apply (slice 13.3a)

Continues the Phase 10 settings chain. Adds runtime apply paths for
the audio block and the UI-side accessibility triad (scale /
contrast / motion). Renderer-side accessibility (color-vision
filter, post-process, photosensitive safety) + HRTF + subtitles are
deferred to a follow-on slice — keeps this change focused on the
most-used knobs.

- `AudioMixer::setBusGain(AudioBus, float)` + `getBusGain(AudioBus)`
  centralise the [0, 1] clamp policy. `audio_panel.cpp` migrated
  off direct `busGain[i]` array access so the clamp is consistent.
- `UISystem::applyAccessibilityBatch(scale, contrast, motion)`
  coalesces the three individual setters (each of which triggers a
  `rebuildTheme`) into one rebuild. Equivalent outcome; one-third
  the work per apply.
- `AudioApplySink` + `AudioMixerApplySink` in `core/settings_apply.{h,cpp}`.
  `applyAudio(AudioSettings, AudioApplySink&)` pushes all six bus
  gains in enum order. HRTF stays on `AudioEngine` and is a
  follow-on; this sink covers only `AudioMixer`.
- `UIAccessibilityApplySink` + `UISystemAccessibilityApplySink`
  sibling. `applyUIAccessibility(AccessibilitySettings, sink)`
  translates the wire-format scale-preset string (`"1.0x"` /
  `"1.25x"` / `"1.5x"` / `"2.0x"`) to the typed `UIScalePreset`
  enum — unknown strings fall back to 1.0× consistent with the
  Settings validation policy.

Tests: 12 new in `tests/test_settings.cpp` (design-doc target: 12):
- AudioMixer API: clamp policy + getBusGain is raw not master-product.
- Audio apply: forwards all 6 buses in order, sink actually mutates
  mixer state, sink clamps out-of-range input, JSON → fromJson →
  apply round-trip preserves values.
- UI accessibility apply: string→enum mapping for each preset,
  unknown-string fallback, contrast/motion pass through verbatim,
  batch call is one rebuild not three, full preset-string table
  is pinned, JSON → fromJson → apply round-trip.

Full suite 2629 passing (1 pre-existing skip).

Next: slice 13.3b — renderer accessibility (color vision, post-process,
photosensitive safety) + HRTF + subtitle size. Or slice 13.4
(input bindings toJson/fromJson) if the renderer coupling turns
out to want more design work first.

### 2026-04-22 Phase 10.5 — First-run wizard engine wiring (slice 14.4)

Fourth and final slice of the first-run wizard work. Wires the
wizard into the live engine and editor loops; Phase 10.5 onboarding
flow is now end-to-end.

- `Settings` now loads from `Settings::defaultPath()` during
  `Engine::initialize`. ParseError / MigrationError falls back to
  in-memory defaults + logs a warning; Ok and FileMissing both
  honour the legacy-flag promotion shipped in 14.1.
- `Engine` owns a `Settings m_settings` member, wires the
  `onboarding` sub-struct into the editor's wizard via
  `Editor::wireFirstRunWizard(onboarding*, assetRoot, applyDemoFn)`.
  `applyDemoFn` is a `std::function` that captures `this` and calls
  the private `setupDemoScene()`, so the wizard's "Show me the Demo"
  option works without promoting the method to public.
- `Editor::drawPanels` dispatches the wizard's returned `SceneOp`:
  `ApplyEmpty` → new `applyEmptyScene(scene, resources)` helper
  (one camera, one directional light, one ground plane — Q2
  resolution); `ApplyDemo` → `m_applyDemoCallback()`; `ApplyTemplate`
  → `TemplateDialog::applyTemplate` with the wizard's selected index
  against `allWizardTemplates()`. Each op clears selection + marks
  the file dirty.
- Edge-triggered persistence: `Editor::consumeWizardJustClosed()`
  returns true on the single frame the wizard transitioned from
  open → closed. The engine's frame loop polls it and calls
  `Settings::saveAtomic(Settings::defaultPath())` exactly once,
  so the frame cost in steady state is zero.
- `WelcomePanel::initialize` no longer auto-opens on first launch
  (Q3 resolution). The keyboard-shortcuts reference is retained
  as `Help → Welcome Screen`.

Tests: 4 new in `tests/test_first_run_wizard.cpp` (design-doc target: 3):
wizard auto-opens when onboarding is incomplete, stays closed when
complete, re-opens via `openFromHelpMenu()` after completion with
Step reset to Welcome, and `WelcomePanel::initialize` no longer
auto-opens on a fresh config dir. Full suite 2617 passing (1
pre-existing skip).

Phase 10.5 onboarding is now feature-complete. Remaining Phase 10.5
items (command palette, contextual help, guided tour, preview
thumbnails, etc.) live in the larger Editor Usability Pass; this
closes out the "first-run welcome dialog" roadmap bullet.

### 2026-04-22 Phase 10.5 — Template visibility filter (slice 14.3)

Third slice of the first-run wizard. Adds the template availability
filter so private-repo-only templates stay hidden in public clones.

- `GameTemplateConfig::requiredAssets` — new `std::vector<std::string>`
  field (default empty = always visible). Paths resolved relative
  to the engine's `assetPath`.
- `filterByAvailability(templates, assetRoot)` free function in
  `first_run_wizard.{h,cpp}`. Kept as a free function so the biblical
  walkthrough template landing in the private sibling repo (Q4
  resolution) surfaces on the maintainer's machine without a code
  change — just file presence on disk. Empty `assetRoot` disables
  filtering (useful for tests + early init before asset path is known).
- `FirstRunWizard::initialize(OnboardingSettings*, assetRoot)` — new
  optional asset-root parameter threaded into the panel state. The
  picker's draw path filters both featured and more buckets by
  availability at render time.
- Design contract pinned by `FirstRunWizardFilter.NonWizardMenuListsAllUnconditionally`:
  the `File → New from Template…` menu path (served by
  `TemplateDialog::getTemplates`) does NOT run the filter. Power
  users always see what exists; wizard users see only what works.

Tests: 4 new in `tests/test_first_run_wizard.cpp` (design-doc target):
empty-required-assets always visible, missing-asset hides the template,
present-asset shows it, non-wizard menu lists all 8 unconditionally.
Full suite 2613 passing (1 pre-existing skip).

Next: slice 14.4 — Engine-level wiring (wizard opens at cold-start
when onboarding.hasCompletedFirstRun is false, scene ops dispatched
to applyEmptyScene / setupDemoScene / TemplateDialog::applyTemplate,
WelcomePanel auto-open stripped, Help menu wiring added).

### 2026-04-22 Phase 10.5 — First-run wizard state machine + panel (slice 14.2)

Second slice of the first-run wizard. Ships the panel class and
its pure-function state machine; engine wiring lands in slice 14.4.

- `engine/editor/panels/first_run_wizard.{h,cpp}` — new panel.
  - Pure-function `applyFirstRunIntent(step, onboarding, intent, nowIso)`
    → `FirstRunTransition{step, onboarding, sceneOp, closed}`.
    Zero ImGui or Scene dependencies; fully headless-testable.
    Injecting `nowIso` keeps tests deterministic and production
    uses a `<chrono>` + `gmtime`/`gmtime_s` wall-clock wrapper.
  - Intents: `PickTemplate`, `StartEmpty`, `ShowDemo`, `SkipForNow`,
    `Back`, `FinishWithTemplate`, `CloseAtWelcome`, `CloseAtPicker`.
  - Scene ops: `None`, `ApplyEmpty`, `ApplyDemo`, `ApplyTemplate`.
    Scene construction is delegated to the UI layer — the pure
    function only tags the op so slice 14.4 can dispatch to
    `TemplateDialog::applyTemplate` / `Engine::setupDemoScene` /
    a minimal empty scene.
  - Q7 skip semantics: `SkipForNow` + `CloseAtWelcome` increment
    `skipCount`; on the second skip the wizard auto-completes
    (hasCompletedFirstRun = true, completedAt stamped). Close-at-picker
    routes to Back without bumping skipCount.
  - Q1 template filter: `featuredTemplates()` surfaces the four
    archetype-coverage picks (First-Person 3D, Third-Person 3D,
    2.5D Side-Scroller, Isometric); `moreTemplates()` holds the
    remaining four (Top-Down, Point-and-Click, 2D Side-Scroller,
    2D Shmup) behind a "More templates" expander.
  - Q8 reduced-motion: no transition animation yet (the two steps
    swap instantly), so the question is satisfied vacuously —
    revisit if 14.4 or a follow-on adds any crossfade.
- `TemplateDialog::applyTemplate` promoted from `private` instance
  method to `public static` — it uses no `this` state, and the
  wizard shares the same scene-construction implementation rather
  than duplicating it. No behaviour change.

Tests: 11 new in `tests/test_first_run_wizard.cpp` (8 state-machine
per design-doc target + 3 template-filter invariants the doc
didn't list but are worth pinning). Full suite 2609 passing
(1 pre-existing skip).

Next: slice 14.3 — `GameTemplateConfig::requiredAssets` filter so
the picker can hide templates whose assets are absent (enables
the private-repo biblical template to surface only when the
maintainer's assets are on disk).

### 2026-04-22 Phase 10.5 — First-run wizard foundation (slice 14.1)

First slice of the Phase 10.5 first-run wizard work
(`docs/PHASE10_5_FIRST_RUN_WIZARD_DESIGN.md`, approved 2026-04-22).
Ships the persistence layer underneath the wizard — no UI yet;
slices 14.2 – 14.4 add the panel, the template visibility filter,
and the engine wiring.

- `OnboardingSettings` section on `Settings`: `hasCompletedFirstRun`,
  `completedAt` (ISO-8601 UTC, empty until completion), `skipCount`
  (bumped by "Skip for now"; after two, `hasCompletedFirstRun`
  auto-flips — Q7 resolution in the design doc).
- Schema version bumped **v1 → v2**. First exercise of the chained
  migration scaffolding shipped in slice 13.1.
  `migrate_v1_to_v2(j)` inserts the `onboarding` block with
  defaults; idempotent on a tree that already carries one.
- Legacy flag promotion in `Settings::loadFromDisk`. Pre-v2 builds
  wrote `<configDir>/welcome_shown` from `WelcomePanel::markAsShown`.
  Upgraders whose first post-upgrade launch predates any Apply
  click have only that signal; loading now promotes it to
  `onboarding.hasCompletedFirstRun = true` and deletes the legacy
  file (best-effort). Lossless: struct mutation happens before
  file deletion, so a crash between them just re-runs on the
  next launch. Runs on both the Ok and FileMissing load paths.
- Tests: 6 new in `tests/test_settings.cpp` — defaults, JSON
  round-trip, v1→v2 migration inserts block, legacy flag
  promotion (file-missing path), promotion deletes the flag
  file, promotion skipped when struct is already complete.
  Full suite 2598 passing (1 pre-existing skip).

Next: slice 14.2 — `FirstRunWizard` panel class wrapping the
existing Phase 9D `TemplateDialog`.

### 2026-04-22 Public default scene + tester onboarding

Two open-source-release drive-bys landed together.

**Default scene is now the neutral demo, not the Tabernacle.** A fresh
public clone opens `Engine::setupDemoScene()` (four CC0 textured blocks
on a grey ground, sky-blue clear colour) instead of
`setupTabernacleScene()`, which referenced assets under
`assets/textures/tabernacle/` that live in a separate private repo and
are gitignored in the public repo. The Tabernacle scene is still
reachable for the maintainer via a new `--biblical-demo` CLI flag
(`EngineConfig::biblicalDemo`, default `false`).

- `EngineConfig::biblicalDemo` — maintainer opt-in; default `false` so
  public clones no longer silently fall back on missing textures.
- `engine.cpp` — `setupDemoScene()` now owns its renderer baseline
  (skybox disabled, sky-blue clear, bloom + SSAO with sane defaults,
  manual exposure 1.0) rather than inheriting Tabernacle-tuned values
  by accident. The Tabernacle path keeps its own overrides.
- `app/main.cpp` — `--biblical-demo` flag + `--help` entry.
- `ASSET_LICENSES.md` — clarifies the default-scene policy.

**Tester onboarding documentation.** The engine is ready for broader
testing but hadn't published a "how to test without writing C++" path.

- `TESTING.md` — 10-minute smoke-test script, 30-minute deeper pass,
  hardware gaps the maintainer cannot cover (NVIDIA, Intel, Windows,
  non-RDNA AMD), and pointers to the release binaries + issue templates.
- `.github/ISSUE_TEMPLATE/tester_feedback.md` — companion to the
  existing `bug_report.md`; captures hardware, version, frame-rate
  observations, and suggestions for "works / rough / would-change" reports.
- `.github/workflows/release.yml` — first-cut tag-triggered release
  workflow. `v*` tag push (or manual dispatch) builds Linux x86_64
  tarball + Windows x86_64 zip, attaches both to a draft GitHub
  Release. AppImage + code-signing are follow-ons.
- `README.md` + `CONTRIBUTING.md` — cross-link `TESTING.md` so
  non-developers arriving at either doc see the tester path.

### 2026-04-22 Phase 10 — Video-mode runtime apply (slice 13.2)

Unblocks the "Window is immutable after construction" blocker
from the settings design (§5 #1). Adds runtime resolution /
fullscreen / vsync changes and wires `DisplaySettings` into a
testable apply layer.

- `Window::setVideoMode(width, height, fullscreen, vsync)` — single
  entry point for runtime video-mode changes. Uses GLFW's
  `glfwSetWindowMonitor` for the windowed ↔ fullscreen toggle
  (preserves the GL context across the transition — no window
  reconstruction needed, contra the design-doc's conservative
  "one-frame GL context validity gap" worry), remembers the prior
  windowed rectangle on entering fullscreen so the reverse toggle
  restores it, and falls back to windowed mode if no primary monitor
  is connected. The framebuffer-size callback fires automatically,
  so renderer framebuffers re-allocate via the existing
  `WindowResizeEvent` subscription — zero extra wiring.
- `Window::isFullscreen()` — queries `glfwGetWindowMonitor`.
- `engine/core/settings_apply.{h,cpp}` — apply layer between
  `Settings` and subsystems. Introduces `DisplayApplySink`
  (abstract base) so tests can supply a recording mock, with
  `WindowDisplaySink` as the production forwarder. `applyDisplay(
  DisplaySettings, DisplayApplySink&)` pushes width/height/fullscreen
  /vsync at the sink in a single call. Render scale and quality
  preset are intentionally deferred — they belong to the Renderer
  and per-subsystem shader-variant wiring, coming in later slices.
- Tests: 4 new `SettingsApply` tests in `tests/test_settings.cpp`
  — forwards all four fields verbatim, handles the default
  windowed case, is idempotent across repeated calls, and survives
  a full JSON → fromJson → apply round-trip. Full suite 2591
  passing (1 pre-existing skip).

Next: slice 13.3 — `AudioMixer::setBusGain` + wiring the `audio`
and `accessibility` blocks into the apply layer.

### 2026-04-22 Phase 10 — Settings primitive + atomic-write + config-path (slice 13.1)

First slice of the Settings system. Ships the persistence primitive
itself — JSON schema v1, load / save / migrate / validate lifecycle,
durable atomic writes, and the shared per-user config-path resolver.
No engine wiring yet; slices 13.2 – 13.5 add per-subsystem apply
paths.

- `engine/utils/config_path.{h,cpp}` — `ConfigPath::getConfigDir()`
  returns the Vestige per-user directory
  (`$XDG_CONFIG_HOME/vestige/` → `$HOME/.config/vestige/` → `/tmp/vestige/`
  on POSIX; `%LOCALAPPDATA%\Vestige\` via `SHGetKnownFolderPath` on
  Windows). Factored out of `editor/recent_files.cpp` so Settings,
  save-games, and any future persistence use one resolver.
  `RecentFiles::getConfigDir` now forwards to the helper.
- `engine/utils/atomic_write.{h,cpp}` — crash-safe file replacement
  via tmp → fsync → rename → fsync-dir on POSIX, `MoveFileExW(MOVEFILE_
  REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` on Windows. A crash
  mid-write leaves either the old file or the new one, never a
  truncated hybrid. Status-code return (`Ok` / `TempWriteFailed` /
  `FsyncFailed` / `RenameFailed` / `DirFsyncFailed`) so callers own
  logging policy.
- `engine/core/settings.{h,cpp}` — root `Settings` struct with five
  sections (`display`, `audio`, `controls`, `gameplay`,
  `accessibility`). `loadFromDisk` parses via
  `JsonSizeCap::loadJsonWithSizeCap` (1 MB cap), runs the migration
  chain, applies validation clamps, and returns a `LoadStatus`
  (`Ok` / `FileMissing` / `ParseError` / `MigrationError`). A
  corrupt file is moved to `<path>.corrupt` so the user can recover
  it manually. `saveAtomic` serialises to JSON, hands off to
  `AtomicWrite::writeFile`, and returns a `SaveStatus`.
  `Settings::defaultPath()` resolves to
  `$XDG_CONFIG_HOME/vestige/settings.json` on Linux.
- `engine/core/settings_migration.{h,cpp}` — chained migration
  scaffolding driven by the root `schemaVersion` integer. v1 is
  current so the chain is a no-op today; the scaffolding is in
  place so v1 → v2 slots in cleanly when the schema evolves.
  Future-version files are refused (we do not downgrade).
  Migrations must be idempotent.
- Schema coverage (see `docs/PHASE10_SETTINGS_DESIGN.md` §4.3 for
  the JSON shape):
  - `display`: windowWidth/Height, fullscreen, vsync,
    qualityPreset (low/medium/high/ultra/custom), renderScale.
  - `audio`: six-bus gain table (master/music/voice/sfx/ambient/ui)
    + HRTF toggle.
  - `controls`: mouse sensitivity, invertY, gamepad deadzones,
    keybinding array (scan-code wire format).
  - `gameplay`: untyped `string→JsonValue` map — per-game values.
  - `accessibility`: UI scale preset, high-contrast, reduced-motion,
    subtitles, color-vision filter, photosensitive-safety caps,
    post-process motion toggles (DoF / motion-blur / fog).
- Validation: render scale clamped to [0.25, 2.0]; bus gains to
  [0, 1]; mouse sensitivity to [0.1, 10.0]; gamepad deadzones to
  [0, 0.9]; non-positive resolutions snap to 1280×720; unknown
  uiScalePreset / subtitleSize / colorVisionFilter strings fall
  back to defaults with a logged warning; malformed keybinding
  entries (missing or wrongly-typed `id`) dropped silently.
- Forward/backward compat: unknown JSON fields ignored on load;
  missing fields get struct-initialiser defaults.
- Tests: `tests/test_settings.cpp` — 36 tests covering
  `ConfigPath` env-var policy, `AtomicWrite` success/parent-dir/
  tmp-cleanup/empty-payload/`describe()`, `Settings` round-trip
  (including partial JSON + unknown fields + gameplay map),
  validation clamps for every bounded field, migration chain
  (no-op / missing-version / future-version), and disk
  load/save (corrupt-file sidecar, parent-dir creation).

Next: slice 13.2 — `Window::setVideoMode` for runtime resolution /
vsync / fullscreen changes + wiring the `display` block into Apply.

### 2026-04-22 Phase 10 — Settings system design approved (slices 13.1–13.5)

Design-review checkpoint. `docs/PHASE10_SETTINGS_DESIGN.md` is
approved; all eight §12 open questions signed off as proposed.
No code ships in this commit — the design doc is the deliverable
and it unblocks slice 13.1 implementation.

Key decisions recorded in the sign-off log (doc tail):

- **Format/location.** Single JSON at
  `$XDG_CONFIG_HOME/vestige/settings.json` (Linux) /
  `%LOCALAPPDATA%\Vestige\settings.json` (Windows), root-level
  `schemaVersion: 1`, nlohmann_json (engine's existing serialiser).
- **Schema.** Five top-level sections: `display` (resolution /
  vsync / fullscreen / quality preset / render scale), `audio`
  (six-bus gains + HRTF), `controls` (mouse sensitivity, invert-Y,
  gamepad deadzones, keybindings as GLFW scancodes), `gameplay`
  (untyped `string→JsonValue` map — per-game), `accessibility`
  (UI scale preset, high-contrast, reduced-motion, subtitles,
  color-vision filter, photosensitive safety, post-process toggles).
- **Lifecycle.** Chained migration functions (not discard-on-error),
  ignore-unknown + default-missing, `.corrupt` sidecar on parse
  failure. Atomic writes via tmp→fsync→rename→fsync-dir (POSIX)
  / `MoveFileExA(MOVEFILE_REPLACE_EXISTING)` (Windows). Apply /
  Revert / Restore Defaults mapped to `m_applied` / `m_pending`
  state copies. No Apply-on-close — explicit Apply required.
- **Accessibility policy.** "Restore All Defaults" spares the
  Accessibility tab; Accessibility gets its own explicit
  "Restore accessibility defaults" button so a partially-sighted
  user doesn't lose their 2.0× UI scale to a reset click.
- **Blockers inventoried.** `Window` is immutable after
  construction (fixed in slice 13.2 via `setVideoMode`);
  `AudioMixer::busGain` has no setter (added in slice 13.3);
  `UITheme` rebuilds clobber overrides (batched in slice 13.5).
- **Slice plan.** 13.1 Settings primitive + atomic-write + config-
  path helper factoring; 13.2 video runtime apply; 13.3 audio +
  accessibility apply; 13.4 input bindings JSON; 13.5 Settings UI
  wiring + Restore Defaults. Five slices, each independently
  testable and commitable. ~68 new tests planned across all five.

### 2026-04-22 Phase 10 — Text rendering bullet (TrueType fonts)

Documentation-only tick. The roadmap's Phase 10 Features →
"Text rendering (TrueType fonts)" bullet is now checked. The
underlying implementation (`engine/renderer/font.{h,cpp}` +
`engine/renderer/text_renderer.{h,cpp}` — FreeType-backed TTF
loader + 2D/3D text rendering + glyph atlas) shipped earlier as
part of Phase 9C / 9F / 10 UI work and is exercised by
`tests/test_text_rendering.cpp` and every menu / HUD / FPS counter
/ interaction prompt built on top of it. The design doc for the
slice-12 UI system called this out (§2 inventory: "The next
roadmap bullet 'Text rendering (TrueType fonts)' is quietly
already done"); this entry formally retires the bullet so the
remaining Phase 10 Features list reflects actual outstanding work
(scene config, settings, loading screens, info plaques).

### 2026-04-21 Phase 10 UI — Toasts + HUD + editor panel (slices 12.3–12.5)

Closes the "In-game UI system" roadmap bullet. Slices 12.1 (pure
`GameScreen` state machine) and 12.2 (`UISystem` screen stack + Engine
integration + menu-prefab signal wiring) landed earlier in the same
day; these three close the remaining gaps identified in
`docs/PHASE10_UI_DESIGN.md` §2 (inventory).

- **Slice 12.3 — Notification toast primitives.** New
  `engine/ui/ui_notification_toast.{h,cpp}`:
  - `NotificationSeverity::{Info, Success, Warning, Error}` plus
    `Notification { title, body, severity, durationSeconds }`.
  - `NotificationQueue` — FIFO with `DEFAULT_CAPACITY = 3`
    (mental-model parity with `SubtitleQueue::DEFAULT_MAX_CONCURRENT`),
    push-newest / drop-oldest eviction, `advance(dt, fadeSeconds)` tick.
  - Pure `notificationAlphaAt(elapsed, duration, fade)` envelope:
    fade-in → plateau → fade-out. `fade ≤ 0` collapses to a
    rectangle (reduced-motion snap); short durations degenerate to
    a triangle so an in-and-out ramp still happens.
  - `UINotificationToast` — `UIElement` subclass rendering one
    `ActiveNotification` as a panel + left-edge severity accent
    strip + title label + optional body. Alpha multiplies every
    drawn colour so the envelope fade is one knob. Accessibility
    role `Label`; `label` carries the title, `description` carries
    the body so a future TTS bridge announces the whole entry in
    one utterance.
  - `UISystem::update` now advances the queue against
    `UITheme::transitionDuration` each frame.

- **Slice 12.4 — Default HUD prefab.** New
  `buildDefaultHud(canvas, theme, textRenderer, uiSystem)` in
  `engine/ui/menu_prefabs.{h,cpp}`:
  - Crosshair at `CENTER` (theme-coloured, small).
  - FPS counter at `TOP_LEFT`, **hidden by default** (debug-only).
  - Interaction-prompt anchor — transparent `UIPanel` at
    `BOTTOM_CENTER`, 4 body-lines above the bottom edge — reserved
    slot for game code's `UIInteractionPrompt` widgets.
  - Notification stack — `UIPanel` container at `TOP_RIGHT` with
    three pre-created `UINotificationToast` children (matching the
    queue cap), all at alpha 0 until populated.
  - `GameScreen::Playing` now has a built-in `ScreenBuilder` default
    that points at `buildDefaultHud`, so `setRootScreen(Playing)`
    yields a working HUD without any game-project plumbing.

- **Slice 12.5 — Editor `UIRuntimePanel`.** New
  `engine/editor/panels/ui_runtime_panel.{h,cpp}`, four tabs
  mirroring the `AudioPanel` discipline:
  - **State** — current root / top-modal readout, a button grid
    firing every `GameScreenIntent`, and a 20-entry scrollback
    recording every `(from, to, intent)` transition with a Clear
    button.
  - **Menus** — combo selector for MainMenu / Paused / Settings
    preview, "Rebuild" button, live element-count readout. The
    offscreen composite FBO path is left as an explicit TODO
    (pending editor-viewport cooperation); the structural readout
    lands now so prefab changes are visible without a game build.
  - **HUD** — four checkboxes (crosshair / FPS counter /
    interaction anchor / notification stack) that write through to
    the live `UISystem` canvas when the root screen is `Playing`.
  - **Accessibility** — scale-preset combo + high-contrast and
    reduced-motion checkboxes that call straight into
    `UISystem::setScalePreset` / `setHighContrastMode` /
    `setReducedMotion`, so a user can compose all three transforms
    and see every menu prefab + the HUD react immediately.
  - `Editor` gains `setUISystem(UISystem*)` and
    `m_uiRuntimePanel` is drawn from the main editor loop; the
    engine wires `setUISystem(m_systemRegistry.getSystem<UISystem>())`
    next to `setAudioSystem`.

- **Tests (new, ~45 cases).** `tests/test_notification_queue.cpp`
  covers severity labels, FIFO eviction, capacity changes,
  negative-duration clamp, the full alpha envelope (ramps /
  plateau / reduced-motion snap / short-duration triangle /
  past-expiry zero), severity → theme colour mapping under default
  and high-contrast palettes, and every `UINotificationToast`
  update path (title-only, title+body, alpha-only no-rebuild fast
  path, alpha clamp). `tests/test_default_hud.cpp` pins the four
  root elements, their expected anchors, the FPS-hidden invariant,
  the transparent interaction anchor, the three-slot notification
  stack, and the `Playing`-defaults-to-HUD screen-builder wiring.
  `tests/test_ui_runtime_panel.cpp` covers panel open/close, the
  screen-log capacity cap, menu-preview rebuild across all three
  menus, HUD toggle round-trips, the `applyHudTogglesTo` clamp to
  `min(canvasCount, HUD_ELEMENT_COUNT)`, and the no-op guard when
  the root screen is not `Playing`.

- **Roadmap.** Phase 10 Features → "In-game UI system (menus, HUD,
  information panels/plaques)" is now checked. Remaining
  unchecked bullets in Phase 10 Features: text rendering,
  scene/level configuration, settings system, loading screens,
  information plaques.

### 2026-04-21 Phase 10 fog — Accessibility transform (slice 11.9)

Third Phase 10 fog slice. The accessibility-settings struct shipped
ahead of the fog feature (`PostProcessAccessibilitySettings`) already
carried `fogEnabled`, `fogIntensityScale`, and `reduceMotionFog`
fields with unit-tested defaults, but nothing *applied* those flags —
the GPU simply uploaded whatever was authored. This slice closes the
gap so a user who opens Settings → Accessibility and toggles the fog
sliders actually sees the render change.

- `engine/renderer/fog.{h,cpp}` — new `FogState` struct (a snapshot of
  every authorable fog parameter: mode + distance params + height-fog
  enable/params + sun-inscatter enable/params) plus the pure-function
  `applyFogAccessibilitySettings(authored, settings) → effective`
  transform. Rules:
  - `fogEnabled = false` is the **master switch** — every layer goes
    off, ignoring intensity + reduce-motion. The one-click
    accessibility toggle is unambiguous regardless of flag order.
  - `fogIntensityScale` scales per-layer:
    - **Linear distance fog** — `end` is pushed outward by
      `start + (end - start) / scale` (scale 0.5 → roughly half the
      perceived density, scale 1 → identity). `scale ≤ 1e-3`
      collapses to `FogMode::None` (avoids a divide-by-zero and
      gives the expected "no fog" experience at the floor).
    - **Exponential / Exponential² distance fog** — density is
      multiplied by scale. scale = 0 produces density 0 which is
      pass-through in `computeFogFactor`.
    - **Height fog** — both `groundDensity` and `maxOpacity` scale
      so the transmittance floor can't pin a ghost layer at scale 0.
    - **Sun inscatter lobe** — colour (not exponent) scales so the
      lobe *shape* stays authored but peak brightness dims.
  - `reduceMotionFog = true` further halves the sun-inscatter colour
    (on top of intensity scale), matching WCAG 2.2 SC 2.3.3 / Xbox
    AG 117 photosensitivity guidance that restricts rapid-onset
    flashing. Distance + height fog are frame-static so the flag is
    a no-op for them today; volumetric fog (slice 11.6+) will
    consult the same flag for temporal reprojection disable.

- `engine/renderer/renderer.{h,cpp}` — adds
  `setPostProcessAccessibility(settings)` / `getPostProcessAccessibility()`
  plus the `m_postProcessAccessibility` member. The composite-pass
  upload now builds a `FogState` from the authored members, runs the
  transform, and uploads the effective values. Authored state is
  **never mutated** — users can toggle accessibility without losing
  their scene-authored look.

- `tests/test_fog.cpp` — 12 new `FogAccessibility` cases:
  `DefaultsArePassThrough`, `MasterDisableCollapsesEveryLayer`,
  `IntensityHalfScalesExponentialDensity`,
  `IntensityZeroTurnsExponentialDensityOff`,
  `IntensityHalfPushesLinearEndOutward`,
  `IntensityZeroCollapsesLinearToNone`,
  `IntensityHalfScalesHeightFogGroundDensityAndMaxOpacity`,
  `IntensityScalesSunInscatterColourNotExponent`,
  `ReduceMotionFogHalvesSunInscatterColour`,
  `ReduceMotionFogDoesNotAffectDistanceOrHeightFog`,
  `SafePresetProducesHalfIntensityReducedMotionFog` (end-to-end via
  `safeDefaults()`), `MasterDisableBeatsIntensityOneAndReduceMotion`
  (flag-precedence guard). All 48 fog-related tests pass.

Corresponds to slice 11.9 in `docs/PHASE10_FOG_DESIGN.md`. Remaining
non-volumetric fog slices: 11.5 (screen-space god rays) and 11.10
(editor FogPanel). Volumetric slices 11.6 – 11.8 are the heavy-lift
remainder of Phase 10 fog.

VESTIGE_VERSION: 0.1.7 → 0.1.8

### 2026-04-21 Phase 10 fog — Shader integration (distance + height + sun inscatter)

Second Phase 10 fog slice. The CPU-side primitives shipped in the
previous slice (`Vestige::computeFogFactor`,
`computeHeightFogTransmittance`, `computeSunInscatterLobe`) are now
evaluated by the final composite shader and produce a visible
contribution on screen. Only the non-volumetric fog stack — volumetric
froxels and god-rays remain deferred per the
`docs/PHASE10_FOG_DESIGN.md` 10-slice rollout plan.

- `assets/shaders/screen_quad.frag.glsl` — adds `u_fogMode`,
  `u_fogColour`, `u_fogStart`, `u_fogEnd`, `u_fogDensity`,
  `u_heightFogEnabled` (+ colour / Y / density / falloff / maxOpacity),
  `u_sunInscatterEnabled` (+ colour / exponent / startDistance),
  `u_sunDirection`, `u_fogDepthTexture` (unit 12, shared with SSAO /
  contact shadows and re-bound in the composite for Mesa declared-
  sampler safety), `u_fogInvViewProj`, `u_fogCameraWorldPos`. GPU
  formula ports `fog.cpp` byte-for-byte. Composition order matches
  `docs/PHASE10_FOG_DESIGN.md` §4: SSAO → contact shadows → **fog
  mix** → bloom add → exposure → tonemap → LUT → colour vision →
  gamma. Bloom therefore samples fogged radiance in linear HDR, which
  is the UE / HDRP convention.

- Reverse-Z world-space reconstruction — `u_fogInvViewProj` takes the
  per-pixel NDC + depth back to world space, so the height-fog ray
  integral uses real world-Y rather than a view-space proxy. Sky
  pixels (reverse-Z depth == 0) skip fog so the skybox colour passes
  through untouched.

- `engine/renderer/fog.{h,cpp}` — new `FogCompositeInputs` struct and
  `composeFog(surfaceColour, inputs, worldPos)` helper that mirror the
  GLSL composite byte-for-byte. Acts as the shared CPU / GPU spec —
  `test_fog.cpp` pins the CPU form, the shader pins the GPU form, and
  the unit tests catch drift between the two.

- `engine/renderer/renderer.{h,cpp}` — new `FogMode` / `FogParams` /
  `HeightFogParams` / `SunInscatterParams` state plus `setFogMode` /
  `setFogParams` / `setHeightFogEnabled` / `setHeightFogParams` /
  `setSunInscatterEnabled` / `setSunInscatterParams` setters (and
  matching getters). Composite uniforms are pushed each frame between
  the contact-shadow and LUT uniform blocks. `m_cameraWorldPosition`
  is cached inside `renderScene()` when not on an override view
  (cubemap-face captures skip fog — the state only matters for the
  main composite).

- 7 new `FogComposite` unit tests — all-disabled identity; distance
  fog at far-end gives pure fog colour; distance fog near camera
  gives pure surface; sun-inscatter warps distance-fog colour to the
  sun tint at `cosθ=1`; height-fog fully obscures surface when
  ground-density is saturated; exact 50/50 two-layer composition
  algebra (0.25, 0.25, 0.5 expected from 0.5·(red,green) mixed with
  0.5·blue); zero-view-distance is a pass-through even when every
  layer is enabled.

All 2436 unit tests pass (2437 total, 1 pre-existing GPU-only skip).

VESTIGE_VERSION: 0.1.6 → 0.1.7.

### 2026-04-21 Roadmap — Add Phase 10.5 Editor Usability Pass

New phase inserted between Phase 10 (Polish and Features) and Phase 11
(Gameplay Systems). Scope principle: no new major features — every
item is about making existing editor functionality findable, obvious,
and fast. Sections:

- Discoverability (command palette, contextual help, searchable
  settings, panel launcher, in-editor glossary).
- Onboarding (first-run dialog, guided tour, sample scenes,
  opt-in local-file telemetry).
- Workflow ergonomics (keyboard parity, chord shortcuts, universal
  undo/redo, unified copy/paste, drag-and-drop, multi-select,
  auto-save, project-relative paths).
- Tooltips & contextual help (every widget, status-bar hints,
  inline warnings, "why is this greyed out?").
- AI assistance integration hooks (editor-exposed command API,
  scene-state snapshot format, optional chat panel, prompt
  templates, keyboard agent invocation). Rule: editor must be
  fully functional without AI.
- Performance & responsiveness (per-panel ms budgets, async
  imports, incremental saves, aggressive panel collapse).
- Editor-side accessibility (scaling presets, high-contrast mode,
  screen-reader labels on ImGui widgets, colourblind-safe gizmos,
  keyboard-only workflow).
- Docs surface (in-editor markdown browser, inline video tooltips,
  troubleshooting decision tree).

Milestone: a person who has never opened Vestige can follow the
first-run tour and produce a buildable scene without reading source
code or external tutorials. Keyboard-only users can drive every
action without a mouse.

### 2026-04-21 Formula Workbench 1.17.0 — Weighted LM, max-abs metric, step sweeps

Three backwards-compatible extensions surfaced by the Phase 10 fog
research (`docs/PHASE10_FOG_RESEARCH.md` §8 /
`docs/PHASE10_FOG_DESIGN.md` §9). All three unlock rendering-formula
fits (phase functions, tonemap curves, BRDF lobes) that the existing
fitter could almost — but not quite — handle. Prepares the Workbench
for the Schlick-to-Henyey-Greenstein phase-function fit in Phase 10's
volumetric-fog slice.

- **Weighted LM** — `CurveFitter::fitWeighted` new overload in
  `engine/formula/curve_fitter.{h,cpp}`. Per-sample weight vector
  parallel to data; minimises `sum(w_i · r_i²)`. Empty / mismatched
  weights degrade to uniform LM. Negative / non-finite weights clamp
  to 0. Reported rmse / maxError / rSquared stay unweighted so the
  numbers remain comparable across fits.
- **`max_abs_error_max`** — new optional expected-block field in
  reference-case JSON. Checks `FitResult::maxError` (already
  computed) and fails when worst-case residual exceeds bound.
  Rendering fits fail on worst-case error, not mean. Default
  `+infinity` keeps existing cases unchanged.
- **Step-based input sweeps** — optional `step` field on
  `InputSweep` alongside `min` / `max` / `count` / `values`. Three
  sweep forms in priority order: explicit values → step → count.
  Endpoint always included (tail sample appended if range isn't an
  integer multiple of step).
- **Documented N-dimensional Cartesian product** — harness already
  supported multi-axis sweeps via multi-key `input_sweep`; now has
  unit-test coverage.
- **Tests** — 5 new curve-fitter tests (weighted path, empty-weights
  parity, mismatch-falls-back-to-uniform, zero-weight-drops-row,
  negative-weights-clamp-to-zero, skewed-data weighted optimum); 7
  new harness tests (step endpoint, non-divisible tail, step=0
  fallback, 2-axis cardinality, max-abs JSON parse, max-abs default
  infinity, weights mismatch failure, uniform weights pass-through).

WORKBENCH_VERSION: 1.16.0 → 1.17.0.

### 2026-04-21 Phase 10 fog — Non-volumetric foundation (distance, height, sun inscatter)

First Phase 10 fog slice. Ships the pure-function primitives for the
three canonical distance-fog modes, the Quílez analytic exponential
height-fog integral, and the UE-style directional sun-inscatter lobe —
plus accessibility toggles so users with motion / contrast sensitivity
can tune the look without losing it entirely.

Follows the Phase 10 audio cadence: ship pure-function primitives
first (exhaustively tested, editor + tests can exercise the math),
wire into the GPU composite in a follow-up slice.

- `docs/PHASE10_FOG_RESEARCH.md` — 2,400-word research report with 20
  citations across UE5 Exponential Height Fog, Unity HDRP, Godot 4,
  Wronski 2014 and Hillaire 2015 (volumetric), Mitchell 2008 (god
  rays), Quílez 2010 (height-fog integral), D3D9 fog-formula spec,
  and accessibility standards (WCAG 2.2 SC 2.3.1 / 2.3.3, Xbox AG
  117/118, Game Accessibility Guidelines).
- `docs/PHASE10_FOG_DESIGN.md` — 10-slice rollout plan, HDR
  composition order, performance budgets per slice, Workbench
  applicability analysis, and three proposed Workbench improvements
  (2D input grids, max-abs-error metric, weighted-LM support).

- `engine/renderer/fog.{h,cpp}` — pure-function primitives:
  * `FogMode` enum (`None` / `Linear` / `Exponential` /
    `ExponentialSquared`) + `FogParams` (colour, start, end,
    density).
  * `computeFogFactor(mode, params, distance)` — canonical
    Linear / GL_EXP / GL_EXP2 formulas (OpenGL Red Book §9; D3D9
    fog-formulas). Returns surface *visibility* in `[0, 1]`. Guards
    degenerate params (start==end, negative density, negative
    distance) with sensible pass-through behaviour.
  * `HeightFogParams` + `computeHeightFogTransmittance` — closed-form
    Quílez integral of `d(y) = a·exp(-b·(y - fogHeight))` along a
    view ray. Uses `expm1` for numerical stability near
    `density·rayDirY·rayLength ≈ 0`. Separate horizontal-ray branch
    collapses to Beer-Lambert when `|rd.y| < 1e-5` so the shader
    doesn't spike at the horizon line. `maxOpacity` clamp matches UE
    `FogMaxOpacity` so the sky doesn't fully vanish on long
    sightlines.
  * `SunInscatterParams` + `computeSunInscatterLobe` — cosine-lobe
    directional scattering (UE "DirectionalInscatteringColor"
    pattern). Zero below `startDistance`, zero on backlit rays.
  * `applyFog(surface, fog, factor)` — CPU mirror of GLSL
    `mix(fog, surface, factor)` with `[0, 1]` clamp.
  * `fogModeLabel(mode)` — stable strings for the editor + tests.

- `tests/test_fog.cpp` — 29 headless unit tests covering:
  * Label stability across every enum value.
  * `None` mode pass-through at any distance.
  * Zero/negative distance pass-through for every mode.
  * Linear knees: unity below `start`, zero at `end`, 0.5 at
    midpoint, zero-span returns unity, clamped past `end`.
  * Exponential: factor = 0.5 when `density·d = ln 2`,
    zero-density returns unity, negative-density clamps to zero,
    monotonic decay across 200 samples.
  * Exponential-squared: factor = `exp(-1)` at `density·d = 1`,
    softer onset than GL_EXP at matching density (the defining
    property of the squared form), monotonic decay.
  * `applyFog` byte-for-byte parity with GLSL `mix()` at
    factor ∈ {0, 0.5, 1} and out-of-range clamp.
  * Height fog: zero-length / zero-density pass-through, monotonic
    decay across distance, horizontal-ray ↔ Beer-Lambert equivalence,
    thinner at altitude, maxOpacity floor, small-angle ↔
    horizontal-branch agreement.
  * Sun inscatter: zero inside start distance, unity when looking
    into sun, zero on backlit, tighter lobe at larger exponent,
    negative exponent defensive clamp.

- `engine/accessibility/post_process_accessibility.{h,cpp}` —
  three new fields: `fogEnabled` (default `true`),
  `fogIntensityScale` (default `1.0`), `reduceMotionFog` (default
  `false`). Rationale documented in the header — disabling fog
  entirely creates a harsh fog-horizon cutoff that's *worse* for
  low-contrast-sensitivity users than keeping fog on at half
  density. `safeDefaults()` therefore keeps fog on, halves density
  (`fogIntensityScale = 0.5`), and enables reduced-motion mode so
  the (future) volumetric temporal reprojection and sun-inscatter
  lobe can't flash on rapid camera pans.

- `tests/test_post_process_accessibility.cpp` — 4 new tests:
  defaults for the three new fields, safe-preset keeps fog on at
  half intensity, per-field equality coverage, per-field
  independence.

**Follow-up slices, explicitly deferred:**
- Shader integration (`screen_quad.frag.glsl` uniforms, depth
  reconstruction, mix-before-bloom HDR composition) — renderer
  surgery, own commit.
- God rays (Mitchell screen-space radial blur).
- Volumetric fog — froxel grid, compute-shader injection,
  Beer-Lambert accumulation, Halton-jitter temporal reprojection.
- Volumetric phase function — Schlick approximation to
  Henyey-Greenstein, **fit via Formula Workbench** per CLAUDE.md
  Rule 11. The Workbench improvements (below) land before this.
- Editor FogPanel (mirror of AudioPanel four-tab pattern).

All formulas in this slice are closed-form / textbook — no
coefficients to fit, Formula Workbench not applicable (the
Workbench-fit slice is the Schlick-to-HG approximation in
volumetric-fog).

### 2026-04-21 Phase 10 audio — Editor AudioPanel closes Phase 10 audio

Tenth Phase 10 audio slice. Ships the last remaining bullet
("Editor integration") and with it, all of Phase 10 audio.

- `engine/editor/panels/audio_panel.{h,cpp}` — new `AudioPanel`
  class following the `NavigationPanel` pattern: non-GL state
  (mixer, ducking, zone lists, mute/solo sets, overlay toggle)
  is exposed through getters so unit tests exercise every
  mutator without an ImGui context.
- Four tabs:
  * **Mixer** — per-bus sliders (Master / Music / Voice / Sfx /
    Ambient / UI), dialogue-duck trigger checkbox, attack /
    release / floor sliders, live current-gain readout.
  * **Sources** — iterates the active scene via
    `Scene::forEachEntity`, displays each `AudioSourceComponent`
    with per-entity Mute + Solo checkboxes, volume / pitch /
    min-max-distance sliders, and the attenuation-model label.
  * **Zones** — editor-draft reverb zones (name + center +
    core-radius + falloff-band + preset combo) and ambient zones
    (name + center + clip path + radii + max volume + priority);
    add / remove / select with selection-shift-on-remove so the
    selected index stays on the intended zone when an earlier
    entry is removed.
  * **Debug** — audio-availability indicator, distance model,
    Doppler factor, speed of sound, HRTF mode + status +
    dataset + available-dataset enumeration, viewport overlay
    toggle for the zone falloff spheres.
- `computeEffectiveSourceGain(entityId, bus)` — panel exposes
  the routing math so the AudioSystem can consult it for
  live playback gain decisions. Rules (matching every DAW's
  convention): mute beats solo (hard kill), solo-exclusive
  routing when any source is soloed, otherwise
  `master · bus · duckGain` clamped to [0, 1].
- Engine wiring: `Editor::setAudioSystem(AudioSystem*)` mirrors
  `setNavigationSystem`. `Engine::initialize` calls
  `setAudioSystem(m_systemRegistry.getSystem<AudioSystem>())`.
  Editor's main draw loop calls `m_audioPanel.draw(m_audioSystem,
  scene)` right after the NavigationPanel.
- `tests/test_audio_panel.cpp` — 18 headless tests: defaults
  (closed / empty zone lists / −1 selections / mixer unity /
  duck untriggered at 1.0), open/close + toggle, reverb zone
  add-returns-index + remove-shifts-selection-down +
  remove-selected-clears-selection + out-of-range no-op, ambient
  zone mirror of same invariants, mute/solo set state
  operations + `hasAnySoloedSource` flag, effective-gain routing
  (muted = 0, solo-exclusive, mute-beats-solo convention, bus ×
  ducking product, [0, 1] clamp), and overlay toggle.

This closes Phase 10 audio. The final audio suite covers:
distance attenuation curves (Phase 10.1), Doppler shift
(10.2), HRTF selection (10.3), material-based occlusion /
obstruction (10.4), reverb zones (10.5), environmental ambient
(10.6), dynamic music (10.7), mixer buses + ducking + voice
eviction (10.8), streaming-music decode state machine (10.9),
and now the editor surface (10.10). Ten slices, 173 new unit
tests, 2384 tests passing overall.

### 2026-04-21 Phase 10 audio — Streaming-music decode state machine

Ninth Phase 10 audio slice. Closes the "Audio engine integration"
parent bullet in ROADMAP.md by landing the last missing sub-item
(streaming playback for music — the one-shot path and the
`AudioSourceComponent` were already in place).

- `engine/audio/audio_music_stream.{h,cpp}` — `MusicStreamState`
  models the decoder-side pipeline independent of OpenAL. Tracks
  `totalFramesDecoded`, `totalFramesConsumed`, `sampleRate`,
  `loopCount`, `maxLoops` (−1 = infinite, 0 = one-shot),
  `minSecondsBuffered` / `maxSecondsBuffered` targets,
  `framesPerChunk`, and an explicit `finished` flag set when the
  consumer has drained everything after EOF.
- `notifyStreamFramesConsumed(state, n)` advances the consumer
  cursor and flips `finished` once `trackFullyDecodedOnce &&
  consumed >= decoded`. `notifyStreamFramesDecoded(state, n,
  eofReached)` advances the decoder cursor and increments
  `loopCount` each time the decoder reports EOF.
- `computeStreamBufferedSeconds(state)` returns
  `(decoded − consumed) / sampleRate`, guarded against zero
  sample rate and consumer overrun (defensive floor at 0 rather
  than negative).
- `planStreamTick(state, decoderAtEof)` is the pure-function
  policy brain that the engine-side MusicPlayer calls once per
  update. Decision tree:
    1. Stream already `finished` → return `trackFinished`.
    2. Buffered seconds ≥ `maxSecondsBuffered` → back-pressure,
       no decode work.
    3. `decoderAtEof` + loops exhausted → mark `finished`.
    4. `decoderAtEof` + loops remaining → `rewindForLoop` +
       request a chunk after the seek.
    5. Otherwise → request one chunk (rounded up to
       `framesPerChunk`) to move toward `minSecondsBuffered`.
  One chunk per tick so a slow frame doesn't avalanche decoder
  work.
- `tests/test_audio_music_stream.cpp` — 16 new tests: buffered
  seconds at start / mid-stream / zero-sample-rate /
  consumer-overrun, notify counters (consumed advance, decoded
  advance + loopCount on EOF, finished after full drain,
  not-finished until full drain, not-finished if EOF not seen),
  plan back-pressure at cap, plan chunk-refill below min, plan
  refill after consumer drain, plan rewinds on EOF under
  infinite policy, plan finishes on EOF when loops exhausted,
  finite-loop policy allows exact N playthroughs, finished
  stream keeps reporting finished.

Also closes the ROADMAP bullet "Audio engine integration":
OpenAL Soft (zlib license) chosen over FMOD for MIT-open-source
compatibility; no runtime licensing concerns for the Steam
launch path. One-shot playback already shipped in Phase 9C via
`AudioEngine::playSound` + `AudioClip::loadFromFile`; streaming
playback lands now via the primitives above; the
`AudioSourceComponent` has been accreting fields across the
Phase 10 audio slices (attenuation / velocity / occlusion).

Per CLAUDE.md Rule 11: ratio / threshold / integer counters —
no coefficients to fit.

### 2026-04-21 Phase 10 audio — Mixer buses, ducking, voice eviction

Eighth Phase 10 audio slice. Ships the three primitives the
engine-side AudioSystem needs to route sources into mixer buses,
duck ambient while dialogue plays, and pick which voice to drop
when the OpenAL source pool is full.

- `engine/audio/audio_mixer.{h,cpp}`:
  * `SoundPriority` enum (Low / Normal / High / Critical) +
    `soundPriorityRank(p)` returning 0..3. Used as the dominant
    axis of the eviction score.
  * `AudioBus` enum (Master / Music / Voice / Sfx / Ambient /
    Ui) + `AudioMixer` struct (per-bus gain table defaulting to
    1.0) + `effectiveBusGain(mixer, bus)` returning `master * bus`
    clamped to [0, 1]. Querying the Master bus returns just the
    Master gain (no Master*Master double-apply). Settings UI
    writes bus gains; the AudioSystem reads `effectiveBusGain`
    when setting `AL_GAIN` on each source.
  * `DuckingState` (currentGain + triggered) + `DuckingParams`
    (attackSeconds + releaseSeconds + duckFactor) +
    `updateDucking(state, params, dt)` — slew `currentGain`
    toward `duckFactor` (triggered) or 1.0 (released) at a rate
    that travels the full swing `(1 − floor)` in the configured
    time. Clamped to [duckFactor, 1]. Zero-duration attack /
    release uses an epsilon so the slew is fast rather than
    inf/nan. Negative dt is a no-op.
  * `VoiceCandidate` (priority + effectiveGain + ageSeconds) +
    `voiceKeepScore(v) = rank·1000 + gain·10 − age` +
    `chooseVoiceToEvict(voices)` returning the index of the
    lowest-score entry (or sentinel `-1` when empty). The 1000×
    priority weight guarantees priority dominates realistic
    gain+age combinations; gain breaks ties within a tier;
    age breaks ties within gain.
- `tests/test_audio_mixer.cpp` — 19 new tests:
  * Labels: priority labels, bus labels, priority ranks
    monotonically increasing.
  * Bus gains: default unity per bus, Master multiplies with
    each bus, Master bus ignores self-double, clamps to
    [0, 1] on both sides.
  * Ducking: attacks toward floor over the configured duration,
    releases toward unity over its own duration, clamps at
    floor (no below-floor overshoot under huge dt), clamps at
    unity, negative dt is a no-op, zero-duration falls back to
    an epsilon-guard that still lands in the valid range.
  * Eviction: empty list returns the sentinel, lower priority
    evicts before higher, within a tier the quieter voice goes
    first, within tier+gain the oldest voice goes first,
    Critical survives against a loud-fresh Low, keep-score
    ordering matches priority rank.

Per CLAUDE.md Rule 11: linear slew + product-of-bus-gains +
integer-weighted score are canonical forms with no coefficients
to fit — Formula Workbench doesn't apply.

### 2026-04-21 Phase 10 audio — Dynamic music system primitives

Seventh Phase 10 audio slice. Three pure-function / pure-data
primitives that the engine-side MusicSystem composes into an
adaptive soundtrack.

- `engine/audio/audio_music.{h,cpp}`:
  * `MusicLayer` enum (Ambient / Tension / Exploration / Combat
    / Discovery / Danger) + `MusicLayerState` (currentGain,
    targetGain, fadeSpeedPerSecond) + `advanceMusicLayer(state,
    dt)` — per-layer slew toward `targetGain` at the fade-speed
    limit, clamped to [0, 1], no overshoot. Callers can write
    `targetGain` every frame; the slew absorbs the twitchiness.
  * `intensityToLayerWeights(intensity, silence)` — maps a
    single [0, 1] gameplay signal to the per-layer mix via
    triangle envelopes with peaks at 0.00 (Ambient) / 0.25
    (Exploration) / 0.50 (Tension + Discovery as subtler bed) /
    0.75 (Combat) / 1.00 (Danger). Adjacent layers meet at
    0.5/0.5 at every midpoint so intermediate intensities are a
    genuine blend rather than one layer winning hard. The
    `silence` parameter multiplicatively scales every layer so
    scripted quiet beats drop the full mix without disturbing
    the intensity routing.
  * `MusicStingerQueue` — FIFO queue with fixed capacity
    (DEFAULT_CAPACITY=8); push-newest / drop-oldest eviction so
    the latest event always wins. `advance(dt)` decrements
    every entry's delay and returns the fired stingers in FIFO
    order. `setCapacity(n)` trims in place; `clear()` discards
    pending; negative delta is a no-op so framerate stalls can't
    mass-fire.
- `tests/test_audio_music.cpp` — 21 new tests:
  * Layer labels stable.
  * Slew: reaches target after sufficient time, no overshoot
    under large fadeSpeed, fades down as well, clamps
    currentGain to [0, 1] defensively, zero delta keeps current.
  * Intensity routing: 0.00 → Ambient only, 1.00 → Danger only,
    0.25 → Exploration peak with zero Ambient (zero crossover),
    0.125 → Ambient+Exploration blend at 0.5/0.5 (mid-envelope),
    Tension peaks at 0.5, Combat peaks at 0.75, clamps at both
    ends.
  * Silence: scales every layer down uniformly, silence=1
    collapses to zero, ratios between layers preserved under
    partial silence.
  * Stinger queue: enqueue + fire after delay, FIFO multi-fire,
    capacity-based eviction of oldest, setCapacity trims in
    place, zero capacity rejects enqueue, clear drops pending,
    negative delta does not cause fire.

Per CLAUDE.md Rule 11: triangle envelope + linear slew +
multiplicative scaling are canonical forms with no coefficients
to fit — Formula Workbench doesn't apply.

### 2026-04-21 Phase 10 audio — Environmental ambient audio primitives

Sixth Phase 10 audio slice. Ships three independent pure-function
primitives that the engine-side AmbientSystem composes into the
full ambient pipeline (no coupling between them, so each is
unit-testable in isolation).

- `engine/audio/audio_ambient.{h,cpp}`:
  * `AmbientZone` struct (clipPath + coreRadius + falloffBand +
    maxVolume + priority) with `computeAmbientZoneVolume(zone,
    distance)` reusing the reverb-zone falloff profile so both
    subsystems share a single sphere-with-linear-falloff curve.
    Priority orders overlapping zones (cave ambience overrides
    outdoor wind in the falloff band).
  * `TimeOfDayWindow` enum (Dawn / Day / Dusk / Night) +
    `TimeOfDayWeights` struct (dawn, day, dusk, night) +
    `computeTimeOfDayWeights(hourOfDay)` — triangle-envelope
    mapping of a 24-hour clock to the four windows with peaks at
    06 / 13 / 20 / 01. Weights are normalised so they sum to 1.0
    at every hour — ensures `clip.volume * weight` budgets stay
    predictable under future peak retuning. Wraps around the 24h
    clock (hour=25 ≡ hour=1, hour=-2 ≡ hour=22).
  * `RandomOneShotScheduler` (minIntervalSeconds,
    maxIntervalSeconds, timeUntilNextFire) +
    `tickRandomOneShot(scheduler, dt, sampleFn)` — cooldown
    scheduler that draws a fresh interval from [min, max] each
    time it fires, using a caller-injected uniform-sample
    function in [0, 1] so tests stay deterministic and the
    engine plugs in `std::uniform_real_distribution`. Fires at
    most once per tick so a framerate stall can't avalanche
    one-shots. Null sampler falls back to the midpoint; negative
    delta treated as zero.
- Weather-driven modulation (rain intensity, thunder, wind howl)
  deliberately *not* wired here — once the Phase 15 weather
  controller publishes its rain/wind intensity outputs, the
  engine-side AmbientSystem applies them as a thin multiplier on
  top of these primitives. Keeping the pure-function layer free
  of Phase-15 dependencies preserves headless testability.
- `tests/test_audio_ambient.cpp` — 17 new tests:
  * AmbientZone volume: inside core returns maxVolume, outside
    falloff returns 0, mid-falloff is linear, maxVolume>1 clamps.
  * TimeOfDay: window labels stable, weights always sum to 1
    (sampled every 15 min across the full day), each peak makes
    its window dominant over all others, wrap-around symmetry on
    both sides of [0, 24], midnight makes night dominate.
  * RandomOneShot: fires when cooldown expires, does not fire
    when cooldown remains, only fires once per tick even under a
    huge delta, uses sampler value to pick interval, clamps
    sampler to [0, 1], null sampler falls back to midpoint,
    negative delta is zero, draws fresh interval per fire from a
    deterministic sampler sequence.

Per CLAUDE.md Rule 11: triangle envelope + linear interpolation
are canonical forms with no coefficients to fit — Formula
Workbench doesn't apply.

### 2026-04-21 Phase 10 audio — Reverb zones with smooth crossfade

Fifth Phase 10 audio slice. Ships the preset / zone-weight / blend
primitives needed to drive EFX reverb across a scene with
continuous transitions as the listener moves between rooms.

- `engine/audio/audio_reverb.{h,cpp}` — new `ReverbPreset` enum
  (`Generic` / `SmallRoom` / `LargeHall` / `Cave` / `Outdoor` /
  `Underwater`) paired with a `ReverbParams` struct that mirrors
  the non-EAX subset of the OpenAL EFX reverb model (`decayTime`,
  `density`, `diffusion`, `gain`, `gainHf`, `reflectionsDelay`,
  `lateReverbDelay`). `reverbPresetParams(preset)` returns values
  adapted from Creative Labs `efx-presets.h` (`EFX_REVERB_PRESET_*`
  entries) — kept to the subset that round-trips through
  `AL_REVERB_*` properties so the engine stays compatible with
  drivers that don't ship EAX reverb.
- `computeReverbZoneWeight(coreRadius, falloffBand, distance)` —
  sphere-with-linear-falloff weight function. Inside `coreRadius`
  returns 1.0; between `coreRadius` and `coreRadius + falloffBand`
  decays linearly to 0.0; outside returns 0.0. `falloffBand == 0`
  gives a hard step at the radius. Negative inputs clamp.
- `blendReverbParams(a, b, t)` — component-wise linear blend across
  every field with `t` clamped to [0, 1]. The engine-side
  ReverbSystem picks the highest-weighted zone and the next-highest
  neighbour, then passes their relative weights to this function so
  the crossfade through doorways / cave mouths is continuous rather
  than stepped.
- Auto-detection of room geometry → decay time is *not* in this
  slice — that step needs physics AABBs / mesh volumes and belongs
  one layer up in the engine-side ReverbSystem. The pure-function
  layer intentionally carries no geometry awareness so tests run
  headless.
- `tests/test_audio_reverb.cpp` — 13 new tests: label stability,
  every preset stays inside sensible EFX ranges (decay [0.1, 20],
  all ratios [0, 1]), ordering invariants (SmallRoom shortest
  decay, Cave longest, Underwater strongest HF damping), weight
  falloff cases (inside core, band=0 hard step, linear mid-band,
  negative clamps), and blend math (t=0/0.5/1 + out-of-range
  clamp, plus exact equality at boundaries).

Per CLAUDE.md Rule 11: the blend is a canonical linear lerp; the
preset values come from an established industry table — no
coefficients to fit, so the Formula Workbench flow doesn't apply.

### 2026-04-21 Phase 10 audio — Material-based occlusion + obstruction

Fourth Phase 10 audio slice. Gives the engine a canonical gain /
low-pass model for sound passing through solid geometry — walls,
doors, windows, water — so the AudioSystem can set final per-source
gain and EFX filter values once the physics raycaster has measured
the obstruction.

- `engine/audio/audio_occlusion.{h,cpp}` — new
  `AudioOcclusionMaterialPreset` enum (Air / Cloth / Wood / Glass /
  Stone / Concrete / Metal / Water) paired with an
  `AudioOcclusionMaterial` struct (`transmissionCoefficient`,
  `lowPassAmount`). Preset values are calibrated for first-person
  walkthroughs (Concrete transmits 0.05 with 0.90 low-pass, Cloth
  transmits 0.70 with 0.30 low-pass, etc.) — relative ordering not
  dB-measured accuracy. `computeObstructionGain(openGain,
  transmission, fraction)` blends open-path and transmitted-path
  gain via the canonical `openGain · (1 − f · (1 − t))` form;
  `computeObstructionLowPass(amount, fraction)` produces the
  matching EFX low-pass target. Both clamp out-of-range inputs.
- `AudioSourceComponent` — new `occlusionMaterial` +
  `occlusionFraction` fields (default `Air` / 0.0 so existing
  sources stay unaffected); `clone` carries them. The
  engine-side raycaster writes these each frame; the AudioSystem
  reads them to compute the final gain + filter values.
- Diffraction explicitly *not* modelled in this layer. The
  engine-side raycaster is responsible for picking a secondary
  source position that hugs the diffraction edge and feeding that
  into the normal attenuation + obstruction path, keeping the
  pure-function layer blind to geometry for testability.
- `tests/test_audio_occlusion.cpp` — 15 new tests: label stability
  for all presets, Air is fully transparent, Concrete is the
  least-transmissive solid, Cloth is the least-muffling non-Air,
  all presets inside [0, 1] on both axes, gain blend math
  (zero-fraction / full-fraction / half-fraction), out-of-range
  clamps on both fraction and transmission, and matching coverage
  for the low-pass path.

Per CLAUDE.md Rule 11: the blend is a canonical linear form with
no coefficients to fit; the numeric preset values are judgement
calls calibrated to listening rather than laboratory measurements,
so the Formula Workbench flow doesn't apply. Values are
deliberately exaggerated over real transmission-loss tables so
material differences stay audible without pushing source gains
into headroom.

### 2026-04-21 Phase 10 audio — HRTF selection closes Spatial audio parent

Third Phase 10 spatial-audio slice. Completes the "Spatial audio"
parent in ROADMAP.md (distance attenuation + Doppler + HRTF all
shipped) and lets players opt into head-tracked stereo-headphone
rendering via the OpenAL Soft `ALC_SOFT_HRTF` extension.

- `engine/audio/audio_hrtf.{h,cpp}` — new `HrtfMode` enum
  (`Disabled` / `Auto` / `Forced`), `HrtfStatus` enum mirroring
  `ALC_HRTF_STATUS_SOFT` values (`Disabled` / `Enabled` / `Denied` /
  `Required` / `HeadphonesDetected` / `UnsupportedFormat` /
  `Unknown`), `HrtfSettings` struct (mode + preferredDataset name),
  and the pure-function `resolveHrtfDatasetIndex(available,
  preferred)` that maps a user-chosen dataset name onto the
  driver-reported list (case-sensitive; empty preferred picks index
  0; unknown name returns -1). Headless — no OpenAL linkage so the
  tests run without an audio device.
- `AudioEngine::setHrtfMode(mode)` / `setHrtfDataset(name)` store
  the desired configuration and call `applyHrtfSettings()` which
  runs `alcResetDeviceSOFT` with the appropriate attribute list
  (`ALC_HRTF_SOFT=false` for `Disabled`, unset for `Auto`,
  `ALC_HRTF_SOFT=true` for `Forced`, plus `ALC_HRTF_ID_SOFT` when a
  valid dataset is named). Extension function pointers are loaded
  via `alcGetProcAddress` after `alcIsExtensionPresent` confirms
  availability — drivers without `ALC_SOFT_HRTF` leave the pointers
  null and every HRTF method becomes a no-op.
- `AudioEngine::getHrtfStatus()` queries `ALC_HRTF_STATUS_SOFT` and
  maps it to the portable `HrtfStatus` enum so settings UI / debug
  overlays can report what the driver actually decided (e.g.
  `Forced` → `Denied` on surround output).
- `AudioEngine::getAvailableHrtfDatasets()` enumerates the driver's
  `ALC_NUM_HRTF_SPECIFIERS_SOFT` + `ALC_HRTF_SPECIFIER_SOFT` pair
  and returns a `std::vector<std::string>` — empty if the extension
  is absent or the driver ships no datasets. Index order is
  driver-defined; index 0 is the default target when the user
  hasn't picked a dataset.
- `tests/test_audio_hrtf.cpp` — 10 new headless tests: mode labels
  stable, status labels stable, default settings (`Auto`, empty
  dataset), equality considers both fields, resolver handles empty
  available list / empty preferred / exact match / unknown name /
  case-sensitivity + trailing whitespace.

Rationale for the policy layer: HRTF is markedly worse than plain
panning on speakers (the listener's own ears double-convolve the
signal), so the engine ships with `Auto` as the default — the
driver's own headphone-detection heuristic flips HRTF on when a
stereo headset is present and leaves it off otherwise. `Forced`
exists for users whose driver doesn't auto-detect headphones
reliably; `Disabled` is the escape hatch for output configurations
where HRTF would degrade rather than improve positioning.

Reference: OpenAL Soft `ALC_SOFT_HRTF` extension specification and
the accompanying `alhrtf.c` example.

### 2026-04-21 Phase 10 audio — Doppler shift for fast-moving sources

Second Phase 10 spatial-audio slice, landing the Doppler sub-bullet
under "Spatial audio" in ROADMAP.md. Gives the engine a canonical
pitch-shift formula that matches what OpenAL evaluates natively, so
CPU-side priority / preview code and GPU-side playback agree.

- `engine/audio/audio_doppler.{h,cpp}` — new `DopplerParams`
  (`speedOfSound` defaults 343.3 m/s for dry air at 20 °C,
  `dopplerFactor` defaults 1.0 matching OpenAL 1.1 defaults) and
  pure-function `computeDopplerPitchRatio(params, srcPos, srcVel,
  listenerPos, listenerVel)`. Implements the OpenAL 1.1 §3.5.2
  formula `f' = f · (SS − DF·vLs) / (SS − DF·vSs)` with velocity
  projections clamped to [−SS/DF, SS/DF]; co-located source and
  listener return unity (no well-defined axis) and `dopplerFactor
  <= 0` disables the effect entirely.
- `AudioEngine::setDopplerFactor(factor)` / `setSpeedOfSound(speed)`
  push the values to `alDopplerFactor` / `alSpeedOfSound` and keep
  the engine's `DopplerParams` in sync with OpenAL's native state.
  `getDopplerParams()` exposes the current settings for CPU-side
  uses (virtual-voice priority, editor preview).
- `AudioEngine::setListenerVelocity(vec3)` — stores listener
  velocity; the next `updateListener` call uploads it as
  `AL_VELOCITY` (previously always hard-zero, which suppressed
  Doppler entirely).
- `AudioEngine::playSoundSpatial(path, position, velocity, params,
  volume, loop)` — new overload that sets per-source `AL_VELOCITY`
  in addition to the existing attenuation parameters. The
  velocity-less overload still zeroes `AL_VELOCITY` so stationary
  one-shots stay unaffected.
- `AudioSourceComponent` — new `glm::vec3 velocity` field (zero
  default so stationary emitters cost nothing to ship). `clone`
  carries it.
- `tests/test_audio_doppler.cpp` — 14 new tests: defaults match
  OpenAL spec, zero-velocity / co-located / disabled-factor /
  non-positive-speed pass-throughs, source-approach and
  source-recede sign conventions, listener-approach and
  listener-recede sign conventions, perpendicular motion producing
  no shift, both-approaching amplifies more than either alone,
  `dopplerFactor` scaling, and the [−SS/DF, SS/DF] velocity clamp
  for supersonic inputs staying finite and sign-correct.

Per CLAUDE.md Rule 11: the Doppler formula is canonical textbook
with no coefficients to fit, so the Formula Workbench flow (author
via fit + export) doesn't apply — the module ships as hand-written
math, matching the same treatment given to the distance-attenuation
curves in the previous slice.

### 2026-04-20 Phase 10 audio — Distance attenuation curves

First Phase 10 audio slice. Adds selectable distance-attenuation
curves for spatial sources, replacing the previous single-curve
(inverse-distance-clamped, hard-coded refDist=1 / maxDist=50)
behaviour with three canonical curves + a pass-through.

- `engine/audio/audio_attenuation.{h,cpp}` — new `AttenuationModel`
  enum (`None` / `Linear` / `InverseDistance` / `Exponential`) +
  `AttenuationParams` (referenceDistance / maxDistance /
  rolloffFactor). Pure-function `computeAttenuation(model, params,
  distance)` reproduces OpenAL's math for CPU-side uses (priority
  sorting, virtual-voice culling, editor preview).
- `alDistanceModelFor(model)` maps each model to the matching
  `AL_*_DISTANCE_CLAMPED` constant, returned as `int` so the header
  doesn't pull in `<AL/al.h>`.
- `AudioEngine::setDistanceModel(model)` swaps the engine-wide
  curve (defaults to `InverseDistance`, matching the Phase 9C
  behaviour — adoption is non-breaking).
- `AudioEngine::playSoundSpatial(path, position, params, volume,
  loop)` — new overload accepting `AttenuationParams`. Sets
  `AL_REFERENCE_DISTANCE`, `AL_MAX_DISTANCE`, `AL_ROLLOFF_FACTOR`
  per-source. The legacy `playSound` overload still ships its
  previous hard-coded values.
- `AudioSourceComponent` — new `attenuationModel` + `rolloffFactor`
  fields; `clone` carries them. Defaults match engine-wide defaults.
- `tests/test_audio_attenuation.cpp` — 15 new tests: model labels;
  model → AL-constant mapping; unity-gain-below-reference invariant
  across every curve; `None` pass-through at any distance; linear
  hits zero at max-distance, halfway point is half gain, clamps
  past max; inverse-distance matches classic formula at d=2 and
  d=5, monotonic falloff, clamps at max; exponential matches power
  formula including inverse-square at rolloff=2; flat at rolloff=0;
  clamps at max; negative-distance safety; zero-span linear safety;
  rolloff=0 flattens inverse-distance.

*Rule 11 note*: These are textbook canonical forms (OpenAL 1.1 spec
§3.4). They have no coefficients to fit against reference data, so
the Formula Workbench rule — use workbench for numerical design —
doesn't apply here. Each formula is documented inline with its
spec-section reference.

Follow-ups within the *"Spatial audio"* parent bullet: HRTF support
(OpenAL Soft ALC_HRTF_SOFT extension) and Doppler effect
(`alDopplerFactor` + per-source velocity).

### 2026-04-20 Phase 10 — DoF + motion-blur accessibility toggles

Final Phase 10 accessibility slice. Closes the last two accessibility
items on the roadmap: *"Depth-of-field toggle (off by default in
accessibility preset)"* and *"Motion-blur toggle (off by default in
accessibility preset)"*.

- `engine/accessibility/post_process_accessibility.{h,cpp}` — new
  `PostProcessAccessibilitySettings` struct with
  `depthOfFieldEnabled` + `motionBlurEnabled` bool fields, both
  defaulting to `true` (normal visual quality). `safeDefaults()`
  factory returns the struct with both flags flipped to `false` —
  the one-click "Accessibility preset" the settings screen applies
  when the user opts for the safest motion configuration.
- `tests/test_post_process_accessibility.cpp` — 5 new tests:
  both-effects-default-on (guards against a silent regression in
  shipped defaults), safeDefaults-disables-both, safeDefaults-
  distinct-from-zero-init (proves the one-click preset is not a
  no-op), equality matches all fields, per-field toggles are
  independent (migraine-from-DoF-only users can disable just one).

*Why ship the toggles before the effects?*  The DoF and motion-blur
effects themselves land in the Phase 10 Post-Processing Effects
Suite. Shipping the canonical toggle home now means (a) the settings
UI + persistence layer can wire the full accessibility preset today,
(b) user preferences survive the moment the effects appear — on
merge day each effect reads a single boolean from a settled location,
and (c) the "Accessibility preset" concept has a real type to hang
off rather than being a loose collection of individual toggles
invented on the fly.

References: WCAG 2.2 SC 2.3.3 ("Animation from Interactions"); Game
Accessibility Guidelines ("Avoid motion blur; allow it to be turned
off"); Xbox / Ubisoft accessibility guidelines (camera-blur
effects should be opt-out).

**Phase 10 accessibility complete** — all eight roadmap items shipped:
UI scale presets, high-contrast mode, colour-vision-deficiency
simulation, photosensitivity safe mode, subtitles, screen-reader
labels, remappable controls, DoF + motion-blur toggles. Suite:
2226 passing + 1 pre-existing GL-context skip.

### 2026-04-20 Phase 10 — Remappable controls (action map)

Sixth Phase 10 accessibility slice. Addresses the roadmap bullet
*"Fully remappable controls (keyboard, mouse, gamepad)"*. Ships the
data model + query path + GLFW integration; persistence and per-
game default loading are follow-ups.

- `engine/input/input_bindings.{h,cpp}` — new action-map
  architecture (Unity Input System / Unreal Enhanced Input / Godot
  InputMap pattern). `InputDevice` enum (None / Keyboard / Mouse /
  Gamepad). `InputBinding` with factory helpers (`key(glfwKey)`,
  `mouse(btn)`, `gamepad(btn)`, `none()`), equality, and
  `isBound()`. `InputAction` with id + label + category + three
  binding slots (primary / secondary / gamepad) + `matches(binding)`
  any-slot predicate.
- `InputActionMap` — insertion-order registry with a parallel
  defaults snapshot. APIs: `addAction` (re-registering an id
  replaces both the live entry and the defaults snapshot, matching
  editor hot-reload expectations), `findAction`,
  `findActionBoundTo` (reverse lookup), `findConflicts(binding,
  excludeSelfId)` (for rebind-UI "already assigned to X" warnings —
  excludes the currently-rebinding action so it doesn't flag
  itself), per-slot setters, `clearSlot(id, slot)`,
  `resetToDefaults()` (map-wide), and
  `resetActionToDefaults(id)` (single action — other user rebinds
  kept).
- `bindingDisplayLabel(binding)` — readable name for every GLFW
  key / mouse button / gamepad button. Gamepad names follow GLFW's
  Xbox layout convention (A / B / X / Y / LB / RB / D-Pad Up …);
  PlayStation users see that vocabulary per GLFW's documented
  translation. Unbound renders as em-dash "—".
- Pure-function `isActionDown(map, id, bindingChecker)` is the
  query path. `bindingChecker` is caller-supplied so tests run
  without a GLFW context. Handles null-checker + unknown-id
  gracefully.
- `engine/core/input_manager.{h,cpp}` — thin GLFW shim:
  `InputManager::isBindingDown` dispatches to
  `glfwGetKey` / `glfwGetMouseButton` / `glfwGetGamepadState` (the
  last polling every connected joystick slot so single-player
  users don't have to pick "player 1" before remaps work).
  `InputManager::isActionDown(map, id)` is a one-liner wrapping
  the free function with its own `isBindingDown` closure.
- `tests/test_input_bindings.cpp` — 30 new tests covering:
  `InputBinding` default / factory / equality; `InputAction`
  `matches()`; map insertion order; lookup; re-registration
  replacement; reverse lookup; conflict detection including
  self-exclusion; per-slot setters returning false for unknown
  ids; `clearSlot` valid + invalid indices; map-wide and single-
  action reset-to-defaults; keyboard / mouse / gamepad display
  labels; unbound em-dash; `isActionDown` true/false paths; any-
  of-three-slots sufficiency; unknown-id; no-slots-bound; null
  binding checker.

Follow-ups (intentionally not in this slice): JSON save/load of
user rebinds (trivial additional I/O layer once Phase 10's
settings-persistence story is chosen), per-game default action-map
bundles, and routing the existing `FirstPersonController` /
engine input paths through the action map (currently they still
call `isKeyDown(GLFW_KEY_W)` directly — a mechanical swap best
done as a dedicated slice so input regressions are easy to
bisect).

### 2026-04-20 Phase 10 — Screen-reader / ARIA-like UI semantics

Fifth Phase 10 accessibility slice. Addresses the roadmap bullet
*"Screen-reader friendly UI labels (ARIA-like semantic tags on
widgets where feasible)"*. Ships the metadata layer and tree-walk
enumeration that a future TTS / screen-reader bridge consumes — the
bridge itself is deferred pending a platform-dependent design
decision (AT-SPI vs. UIAutomation vs. a cross-platform VoiceOver-
style in-engine reader).

- `engine/ui/ui_accessible.{h,cpp}` — new `UIAccessibleRole` enum
  (`Button` / `Checkbox` / `Slider` / `Dropdown` / `KeybindRow` /
  `Label` / `Panel` / `Image` / `ProgressBar` / `Crosshair` /
  `Unknown`) + `UIAccessibleInfo` struct (role + label +
  description + hint + value, mirroring WAI-ARIA 1.2's
  `role / aria-label / aria-describedby / aria-keyshortcuts /
  aria-valuetext`). `uiAccessibleRoleLabel(role)` returns a stable
  human-readable string used by tests and debug panels.
- `engine/ui/ui_element.{h,cpp}` — every `UIElement` now carries
  an `m_accessible` member exposed via `accessible()` getter pair.
  New virtual `collectAccessible(vector<Snapshot>&)` walks the
  subtree and emits an entry per element that has either a non-
  `Unknown` role or a non-empty label. Hidden subtrees are skipped
  entirely (a screen reader must not announce UI the sighted user
  cannot see).
- `engine/ui/ui_canvas.{h,cpp}` — new `UICanvas::collectAccessible()`
  returns the canvas-wide flat snapshot list.
- Every shipping widget sets its role in its constructor:
  `UIButton` → Button, `UICheckbox` → Checkbox, `UISlider` → Slider,
  `UIDropdown` → Dropdown, `UIKeybindRow` → KeybindRow,
  `UILabel` → Label, `UIPanel` → Panel, `UIImage` → Image,
  `UIProgressBar` → ProgressBar, `UICrosshair` → Crosshair.
  Context-specific strings (label / value / hint) stay caller-side:
  menus set `btn->accessible().label = "Play Game"` where the
  widget is wired up.
- `tests/test_ui_accessible.cpp` — 13 new tests covering: role-label
  lookup, per-widget default role, default-empty strings, mutable
  label / description / hint, Unknown-with-empty-label omission,
  role-alone-is-enough, label-alone-is-enough, hidden-subtree
  exclusion, interactive-flag carry-over, child-order walk,
  unlabelled container passthrough, canvas enumeration, empty
  canvas returns empty vector.

Scope note: ImGui editor widgets are a separate surface. They need
per-call-site label attachment rather than per-type constructor-
set roles, so they are deliberately out of scope for this slice.

### 2026-04-20 Phase 10 — Subtitle / closed-caption queue + size presets

Fourth Phase 10 accessibility slice. Addresses the roadmap bullet
*"Subtitle / closed caption system for spatial audio cues, with size
presets (Small / Medium / Large / XL)"*.

- `engine/ui/subtitle.{h,cpp}` — headless `SubtitleQueue` FIFO with
  per-tick countdown and push-newest / drop-oldest overflow. Default
  concurrent cap is 3, matching BBC caption guidelines and the
  2–3-lines-at-once recommendation from Romero-Fresco 2019 reading-
  speed research. `clear()` for scene transitions;
  `setMaxConcurrent(n)` trims in-place if n < current size.
- `Subtitle` authored struct: `text`, `speaker`, `durationSeconds`,
  `category` (`Dialogue` / `Narrator` / `SoundCue`), and
  `directionDegrees` (0 = front, 90 = right, … — ready for spatial
  audio integration to surface a direction hint).
- `SubtitleSizePreset` ladder (`Small` 1.00× / `Medium` 1.25× /
  `Large` 1.50× / `XL` 2.00×) with `subtitleScaleFactorOf(preset)`
  helper. Ratios intentionally mirror `UIScalePreset` so a user who
  knows the UI ladder understands the caption ladder, and the two
  compose: a consumer multiplies `UITheme::typeCaption` by the
  subtitle factor, then by the UI-wide scale factor.
- `tests/test_subtitle.cpp` — 17 new tests: size-preset ladder;
  empty-queue baseline; enqueue / tick countdown; zero-second
  expiry; over-budget long-frame expiry; selective expiry; FIFO
  order; overflow eviction; `setMaxConcurrent` trim-in-place; raising
  the cap no-op; `clear`; category + spatial-direction round-trip;
  dialogue-speaker preservation; negative-duration clamp.

Rendering is deliberately out of scope for this slice — the queue is
headless so it can be unit tested without GL context and reused by any
future UI register (HUD caption strip, log overlay, etc.). Rendering
lands when audio-event wiring is designed, at which point the renderer
reads `queue.activeSubtitles()` and draws each entry at
`typeCaption × subtitleScaleFactor × uiScaleFactor` pixels.

### 2026-04-20 Phase 10 — Photosensitivity safe mode (reduced flashing)

Third Phase 10 accessibility slice. Addresses the roadmap bullet
*"Reduced-flashing / photosensitivity safe mode (caps camera shake,
strobes, muzzle-flash alpha)"*.

- `engine/accessibility/photosensitive_safety.{h,cpp}` — new
  `PhotosensitiveLimits` struct with published, research-grounded caps
  (WCAG 2.2 SC 2.3.1 "Three Flashes or Below Threshold", Epilepsy
  Society photosensitive-games guidance, IGDA GA-SIG / Xbox / Ubisoft
  accessibility best-practice bullets): max flash α 0.25, shake scale
  0.25, max strobe 2 Hz, bloom intensity scale 0.6. Four pure-function
  helpers — `clampFlashAlpha`, `clampShakeAmplitude`, `clampStrobeHz`,
  `limitBloomIntensity` — that subsystems call before handing values
  downstream. Identity pass-through when disabled — zero runtime cost.
  Per-caller override of the defaults (e.g. a horror sequence tightening
  the flash ceiling) via an optional `limits` parameter.
- `engine/ui/ui_theme.{h,cpp}` — new `UITheme::withReducedMotion()`
  pure transform that zeroes `transitionDuration` (the reduce-motion
  hook that was already flagged in the field's doc comment). Palette
  and sizing are left untouched so the transform composes cleanly.
- `engine/systems/ui_system.{h,cpp}` — `setReducedMotion(bool)` /
  `isReducedMotion()` accessibility accessors. `rebuildTheme` now
  composes **scale → high-contrast → reduced-motion**, so users can
  run any combination of the three accessibility toggles
  simultaneously.
- `tests/test_photosensitive_safety.cpp` — 13 new tests covering:
  disabled-is-identity for every helper, published default limits,
  flash-alpha ceiling clamping, shake amplitude scaling, strobe-Hz
  ceiling, bloom intensity scaling, and per-caller override of the
  defaults.
- `tests/test_ui_theme_accessibility.cpp` — 5 new tests covering:
  `withReducedMotion` zeros `transitionDuration`, leaves palette +
  sizes untouched, `UISystem::setReducedMotion` rebuilds the active
  theme, reduced-motion composes with scale + high-contrast, and
  toggling off restores the base transition timing.

Today's accessibility composition surface: UI scale 1.0×/1.25×/1.5×/
2.0× + high-contrast mode + reduced-motion mode + colour-vision-
deficiency simulation. Each stage is an independent pure transform;
all four can run simultaneously. The clamp helpers are ready for
future shake/flash/strobe systems to consult — they currently wire
only into the UI transition duration because that's the only
reduced-motion-sensitive system the engine ships today.

### 2026-04-20 Phase 10 — Colorblind simulation filter (CVD matrices)

Second Phase 10 accessibility slice. Addresses the roadmap bullet
*"Colorblind modes (Deuteranopia, Protanopia, Tritanopia LUT modes
applied post-tonemap)"*.

- `engine/renderer/color_vision_filter.h/.cpp` — new `ColorVisionMode`
  enum (`Normal`, `Protanopia`, `Deuteranopia`, `Tritanopia`) and a
  `colorVisionMatrix(mode)` lookup returning the 3×3 RGB simulation
  matrix. Coefficients are the canonical Viénot/Brettel/Mollon 1999
  dichromat projections — the dataset cited by Unity, Unreal, and the
  IGDA GA-SIG accessibility guidance. `colorVisionModeLabel(mode)`
  provides a stable string for future settings UIs.
- `assets/shaders/screen_quad.frag.glsl` — two new uniforms
  (`u_colorVisionEnabled`, `u_colorVisionMatrix`) applied between the
  artistic color-grading LUT and the sRGB gamma conversion, so the
  simulation reflects the final displayed colour. Clamped to `[0,1]`
  to contain any minor over/undershoot from the matrix multiply.
- `engine/renderer/renderer.{h,cpp}` — added `setColorVisionMode` /
  `getColorVisionMode` and `m_colorVisionMode` (default `Normal`).
  The composite pass sets `u_colorVisionEnabled=false` in the Normal
  case so the multiply is skipped — zero-cost when off.
- `tests/test_color_vision_filter.cpp` — 12 new tests covering:
  identity transform, labelling, Brettel coefficient values per mode,
  row-sum-1 invariant (equivalent to preserving achromatic input),
  characteristic dichromat projections (red→yellow for protanopes,
  green shifted toward red for deuteranopes, blue→cyan-band for
  tritanopes), and black/white fixed-point preservation across all
  three modes.

Composes with the existing UI accessibility state: a partially-sighted
user with colour-vision deficiency can run UI scale 1.5× + high-
contrast + a CVD simulation mode simultaneously; each stage is an
independent transform. The simulation is off by default; enable via
`Renderer::setColorVisionMode(ColorVisionMode::...)` or a future
settings panel.

### 2026-04-20 Phase 10 — UI scaling presets + high-contrast mode

First Phase 10 accessibility slice. Addresses two roadmap bullets
directly: *"UI scaling presets (1.0× / 1.25× / 1.5× / 2.0× — minimum
1.4× recommended for partially-sighted users)"* and *"High-contrast
mode for UI elements"*.

- `engine/ui/ui_theme.h/.cpp` — added `UIScalePreset` enum + free
  function `scaleFactorOf(preset)` returning the numeric multiplier.
  Added two pure-function transforms on `UITheme`:
  - `UITheme::withScale(factor)` — returns a copy with every pixel
    size field (buttons, sliders, checkboxes, dropdowns, keybinds,
    type sizes, crosshair, focus ring, panel borders, progress bars)
    multiplied. Palette, motion timing (`transitionDuration`), and
    font family names are intentionally left untouched.
  - `UITheme::withHighContrast()` — returns a copy with a pure-
    black / pure-white palette, full-alpha panel strokes, and a
    saturated amber accent. Sizes stay untouched so high-contrast
    composes cleanly on top of any scale preset.
- `engine/systems/ui_system.{h,cpp}` — `UISystem` now tracks a base
  theme (`m_baseTheme`), a scale preset, and a high-contrast flag.
  New API: `setBaseTheme`, `getBaseTheme`, `setScalePreset`,
  `getScalePreset`, `setHighContrastMode`, `isHighContrastMode`.
  Each setter triggers an idempotent `rebuildTheme` that composes
  `withScale` then (if enabled) `withHighContrast` onto the base.
- `tests/test_ui_theme_accessibility.cpp` — 14 new tests: preset
  factors, full-field scale coverage at 1.5×, palette-and-motion
  invariants under scale, identity-at-1.0 fixed point, high-contrast
  palette invariants (pure-black bg, pure-white text, full-alpha
  strokes, discriminable disabled text), UISystem defaults, scale
  rebuild, high-contrast rebuild, composition of the two, toggle-off
  round-trip, and `setBaseTheme` preserving active scale. Suite
  2117 → 2131.

### 2026-04-20 ROADMAP housekeeping — Phase 9F-4 checkbox flipped

Line 873 was a stale unchecked *"2D character controller"* item that
had actually been shipped in commit ec62677 (Phase 9F-4) alongside
the 2D camera. Flipped to `[x]` with a backpointer to the
implementation location and the feature set that landed (coyote
time, jump buffering, variable-jump cut, wall slide, ground/air
acceleration, ground friction).

### 2026-04-20 Cursor Bridge — MCP-driven editor tab management

Shipped a two-part local bridge that lets Claude Code (or any MCP
client) drive Cursor / VS Code tab state. A companion to the official
Claude Code extension which already handles inline diffs and
selection-as-context — this adds tab *management* (open, focus, close-
others, list, reveal) that the official extension does not expose.

Lives under `tools/cursor_bridge/`:

- `extension/` — VS Code extension, Cursor-compatible via the standard
  extension API. TypeScript, listens on `127.0.0.1:39801` (loopback
  only — no remote exposure). NDJSON protocol, one request per line:
  `{ id, command, args }` → `{ id, ok, result | error }`. Six commands:
  `ide_open_file`, `ide_focus`, `ide_close_others`,
  `ide_close_all_except`, `ide_get_open_tabs`, `ide_reveal_in_explorer`.
  Non-file tabs (settings, walkthroughs, diff editors) are left alone
  by the close helpers.
- `mcp_server/` — Node MCP server (stdio transport). Registers the six
  tools, forwards each call over TCP to the extension. Short-lived
  per-call connections + 5 s timeout so a reloaded extension doesn't
  leave the server in a bad state.
- `README.md` with install steps (sideload the .vsix, register the MCP
  server in `~/.claude/mcp_config.json`, restart Claude Code).

Both TypeScript projects compile cleanly with `npm run compile` and
ship their own `.gitignore` so `node_modules/` and `dist/` stay out of
the tree.

### 2026-04-20 Phase 9F-6 — Editor 2D panels + template dialog wiring

Shipped the editor-side hooks that let designers work with 2D scenes
without leaving the IDE.

- `engine/editor/panels/sprite_panel.{h,cpp}` — loads a TexturePacker
  JSON atlas, lists its frames, assigns the atlas (and optionally a
  specific frame) to the selected entity's SpriteComponent. Adds the
  component when the selected entity doesn't already have one.
- `engine/editor/panels/tilemap_panel.{h,cpp}` — layer list
  (active-layer picker + add), resize knobs, tile palette picker (keyed
  off `TilemapComponent::tileDefs`), and headless `paintCell` /
  `eraseCell` for the viewport brush (wired from the viewport click
  pipeline — the paint helpers are already public so scripted/tested
  flows can drive them).
- Template dialog: added `GameTemplateType::SIDE_SCROLLER_2D` and
  `SHMUP_2D` (total 6 → 8). Dispatch in `applyTemplate` routes 2D types
  to `createSideScrollerTemplate` / `createShmupTemplate` from Phase
  9F-5 instead of the 3D flow.
- 6 new tests covering atlas load, panel visibility toggles, and
  paint/erase operations. Updated `test_editor_viewers.cpp`'s
  `TemplateCount` test from 6 → 8. Full suite **2120 → 2126 passing**.

Auto-tiling, slicing from a raw PNG, and the viewport-click paint
pipeline are Phase 18 polish per the design doc.

### 2026-04-20 Phase 9F-5 — 2D game-type templates (Side-Scroller + Shmup)

Ship two starter-scene generators designers can instantiate from the
editor (9F-6 wires them into the TemplateDialog). Each template
composes the existing 2D components (SpriteComponent, RigidBody2D,
Collider2D, CharacterController2D, Camera2D, Tilemap) into a wired
scene that Just Works out of the box.

- `engine/scene/game_templates_2d.{h,cpp}`:
  - `createSideScrollerTemplate(scene, config)` — player (capsule,
    fixedRotation, CharacterController2D), ground, two platforms, and a
    smoothed-follow camera clamped to world bounds.
  - `createShmupTemplate(scene, config)` — kinematic gravity-free player,
    scrolling-backdrop tilemap on sorting layer -100, locked
    orthographic camera.
  - Optional atlas binding via `GameTemplate2DConfig`: when provided,
    the template attaches SpriteComponents with the config-specified
    frame names; when omitted the structure ships without sprites so
    designers can drop assets later.
- 9 new unit tests covering entity layout, component presence / types,
  camera configuration, and graceful no-atlas fallback.

### 2026-04-20 Phase 9F-4 — 2D camera + platformer character controller

Shipped the 2D camera (ortho smooth-follow with deadzone + world bounds)
and the platformer character controller (coyote time, jump buffering,
variable jump cut, wall slide, ground friction). Both ship as
component + free-function pairs rather than new ISystem classes —
callers decide when to step them (editor, game loop, scripted sequence)
without paying for an auto-drive in scenes that don't use them.

- `engine/scene/camera_2d_component.{h,cpp}` — orthoHalfHeight, follow
  offset, deadzone, smoothTimeSec, maxSpeed, worldBounds clamp. The
  critical-damped spring integrator is the same formula Unity's
  SmoothDamp uses; first-frame snap avoids a visible sweep-in.
  `updateCamera2DFollow(camera, target, dt)` is the step helper.
- `engine/scene/character_controller_2d_component.{h,cpp}` — tuning
  (maxSpeed, acceleration, airAcceleration, groundFriction,
  jumpVelocity, variableJumpCut, coyoteTimeSec, jumpBufferSec,
  wallSlideMaxSpeed) + runtime state (onGround, onWall,
  timeSinceGrounded, jumpBufferRemaining, jumpingFromBuffer).
  `stepCharacterController2D(ctrl, entity, physics, input, dt)` reads
  the body's current velocity, applies input + timers + friction +
  wall-slide, writes a new velocity, returns true on jump so callers
  can trigger SFX / particles.
- 16 new unit tests: camera deadzone suppression, follow after leaving
  deadzone, bounds clamp, zero-smooth instant snap, clone reset;
  controller acceleration, jump on ground, coyote-time late jump,
  buffered jump on landing, buffer expiry, variable jump cut, wall-slide
  cap, ground friction decel. Full suite **2086 → 2111 passing**.

### 2026-04-20 Phase 9F-3 — Tilemap component + renderer helper

Shipped multi-layer tilemaps with animated tiles. The tilemap is just
another consumer of the sprite atlas — tilemap cells convert into
SpriteInstance records that feed the existing SpriteRenderer, so there
is no dedicated tilemap shader or draw path. This keeps sprites and
tilemaps in a single z-ordered pass.

- `engine/scene/tilemap_component.{h,cpp}` — TilemapLayer (dense grid,
  row-major bottom-first), TileId (uint16, 0 = empty), TilemapTileDef
  (maps an ID to an atlas frame or an animated sequence),
  TilemapAnimatedTile (frame list + framePeriodSec + ping-pong flag).
  Animation time wraps at 1 hour to keep float precision tight in long
  gameplay sessions.
- `engine/renderer/tilemap_renderer.{h,cpp}` — pure helper
  `buildTilemapInstances(tilemap, worldMatrix, depth, outInstances)` —
  no GL, no state. Called by the sprite pass to emit one instance per
  visible cell. Tilemap origin = entity position; column 0 / row 0 at
  the origin.
- 12 new unit tests covering layer resize overlap, out-of-bounds set/get,
  animated-tile time-based resolution, forEachVisibleTile short-circuit,
  clone semantics, and instance-vector construction.

### 2026-04-20 Phase 9F-2 — 2D physics via Jolt Plane2D DOF lock

Shipped the 2D physics subsystem on top of the existing Jolt 5.2.0 build.
No new third-party dependency — per-body `EAllowedDOFs::Plane2D` locks Z
translation and X/Y rotation, so 2D bodies share the same broadphase,
narrowphase, and contact solver as the 3D world. A mixed 2D+3D scene now
works out of the box.

- `engine/scene/rigid_body_2d_component.{h,cpp}` — BodyType2D (Static /
  Kinematic / Dynamic), mass, friction, restitution, damping, gravity
  scale, fixedRotation, collision bits; runtime fields (bodyId,
  linearVelocity, angularVelocity) cached from Jolt each step.
- `engine/scene/collider_2d_component.{h,cpp}` — shape descriptor
  (Box / Circle / Capsule / Polygon / EdgeChain), trigger-mode sensor
  flag, zThickness + zOffset for the extruded-slab representation.
- `engine/systems/physics2d_system.{h,cpp}` — ISystem registered after
  SpriteSystem. Shares the Engine's PhysicsWorld via
  `getPhysicsWorld()`; `ensureBody` / `removeBody` / `applyImpulse` /
  `setLinearVelocity` / `setTransform` expose a 2D-native API that
  hides JPH::Vec3 plumbing. Dedicated `setPhysicsWorldForTesting` test
  seam lets the test suite spin up a standalone PhysicsWorld without
  Engine bootstrap.
- Jolt `cDefaultConvexRadius = 0.05f` collision: authored zThickness
  smaller than 0.12 is silently widened in `makeShape` so designers
  don't have to think about Jolt's internal margin.
- **15 new unit tests** — DOF lock, gravity fall, static-floor rest,
  impulse/velocity, shape coverage (box, circle, capsule, polygon,
  edge chain), degenerate-shape rejection, sensor pass-through,
  fixed-rotation lock. Full suite now **2074 tests**.

### 2026-04-20 Phase 9F-1 — sprite foundation (atlas, animation, instance-rate renderer)

Shipped the 2D-sprite rendering foundation. Sprites now have atlas-backed
frame lookup, Aseprite-compatible per-frame animation, and an instance-rate
batched renderer separate from the UI's `SpriteBatchRenderer`. Game sprites
pack one affine transform + UV rect + tint + depth per instance (80 bytes)
and draw in a single `glDrawArraysInstanced` per (atlas, pass). The
`SpriteSystem` collects, sorts, and batches — all three steps are headless
so tests validate the CPU pipeline without a GL context.

- `engine/renderer/sprite_atlas.{h,cpp}` — TexturePacker JSON loader
  (array + hash forms), pre-normalised UVs, optional per-frame pivots.
- `engine/animation/sprite_animation.{h,cpp}` — per-frame-duration clips,
  forward / reverse / ping-pong direction, loop control.
- `engine/scene/sprite_component.{h,cpp}` — attachable component with
  atlas + frameName + tint + pivot + flips + pixelsPerUnit + sorting
  layer/order + sortByY + isTransparent.
- `engine/renderer/sprite_renderer.{h,cpp}` — instance-rate VBO with a
  static 4-vertex corner quad; depth / blend state restored on `end()` so
  the sprite pass doesn't disturb the 3D pipeline.
- `engine/systems/sprite_system.{h,cpp}` — `ISystem`, registered in
  `Engine::initialize` after `NavigationSystem`. Render path not yet
  wired into the frame loop (waits for Phase 9F-4's 2D camera for a
  proper view-projection).
- `assets/shaders/sprite.vert.glsl` / `sprite.frag.glsl` — shared shader
  pair; vertex shader reconstructs the 2D affine from two packed rows to
  avoid wasted floats per instance.
- **27 new unit tests** across atlas, animation, sort/batch, instance
  packing, depth monotonicity, and component cloning. Full suite now at
  **2059 tests** (was 2032).
- Fixed a move-before-read bug in `SpriteAnimation::addClip` surfaced by
  the replace-clip test: cache the key before `std::move(clip)`.
- Design doc: `docs/PHASE9F_DESIGN.md`.

### 2026-04-20 Post-Phase-9E audit — formula-workbench dangling-temp fix + audit 2.14.1

Ran the full audit stack (cppcheck, semgrep p/security-audit, gitleaks,
custom `tools/audit/audit.py` tiers 2-3) against the post-Phase-9E
working tree. Baseline clean: build 0 warnings / 0 errors, 2032 tests
passing, 0 HIGH/CRITICAL from any tool. Findings breakdown:

- **Fixed — dangling const-ref to ternary temporary**
  (`tools/formula_workbench/formula_node_editor_panel.cpp:196`).
  `const std::string& sweepLabel = cond ? std::string("<auto>") :
  m_preview.sweepVariable;` technically works (the common-type
  materialised temporary gets lifetime-extended through the const ref)
  but is brittle and cppcheck flags it `danglingTemporaryLifetime`.
  Dropped the `&` — now stores by value, same cost under NRVO/elision,
  no lifetime question.
- **Audit-tool 2.14.1 — `c_style_cast` FP filter.** All 19 tier-2
  Memory-Safety matches were FPs (parameter decls with `/*comment*/`
  names that `skip_comments` preprocessed into `(float )`, plus
  function-pointer type syntax like `float (*)(float)` where the
  trailing `(float)` matched the cast regex). Tightened the regex to
  require an operand after the close paren — tier-2 finding total
  dropped 231 → 212 with zero lost signal. See
  `tools/audit/CHANGELOG.md` [2.14.1].
- **Ignored (false positives).** 83 gitleaks hits in
  `build/_deps/imgui-src/` (third-party). 2 cppcheck
  `returnDanglingLifetime` in `engine/scene/entity.cpp` — already have
  inline suppressions, manual cppcheck invocation was missing
  `--inline-suppr` (audit.py passes it correctly). 2
  `duplicateAssignExpression` on Ark-of-the-Covenant dimensions
  (1.5 cubits = 1.5 cubits per Exodus 25:10) and cube-face mip dims.
  3 semgrep hits in `tools/audit/lib/` (dedicated `run_shell_cmd`
  wrapper with explicit contract; NVD API URL uses fixed domain + url-
  encoded query + 16 MB body cap per AUDIT.md §L7).

### 2026-04-20 Phase 9E-3 runtime verification closed — node layout survives restart

Runtime-verified the Script Editor's imgui-node-editor integration end-to-end
(clean shutdown + layout restore), closing the last unchecked box under
Phase 9E-3. The shutdown SEGV fix from the earlier commit already worked,
but dragged node positions reset to the template defaults on every relaunch
because the template-load code force-called `ed::SetNodePosition` for every
node — stomping the positions the library had just restored from
`~/.config/vestige/NodeEditor.json`.

- `NodeEditorWidget` now parses `NodeEditor.json` at init and exposes
  `hasPersistedPosition(nodeId)`. The parse handles the library's
  `"node:<id>"` key format (see `Serialization::GenerateObjectName` in
  `imgui_node_editor.cpp`) and the older bare-integer form. nlohmann/json
  is already a dep; no new externals.
- `ScriptEditorPanel::renderGraph` skips the template-default seed for
  nodes that already have a saved position, so the library's
  `CreateNode` → `UpdateNodeState` path wins and the user's drags
  survive. Nodes that aren't in the settings file (fresh template on a
  clean profile) still get seeded from `ScriptNodeDef::posX/posY`, so
  multi-node templates no longer stack at the origin on first launch.
- The save-callback path merges new ids into the persisted set instead
  of replacing it, because the library serializes only nodes that have
  already been referenced via `BeginNode` / `SetNodePosition`. A save
  fired at end-of-frame before the panel has rendered any nodes would
  otherwise write an empty `"nodes"` object and clear the set we just
  populated from disk.
- Manual test (Door Opens template, node:2 dragged from (220,0) to
  (223,-153), editor closed, relaunched, template re-picked): node
  reappears at the dragged position, all links reroute correctly, no
  crash on shutdown.

### 2026-04-20 Phase 9E-5 — ScriptGraphCompiler (graph → validated IR)

Closes the "Graph compilation to executable logic (beyond expression trees)"
item under Phase 9E. Visual scripting graphs now go through a dedicated
validation + lowering pass at load time, so broken graphs (unknown node
types, dangling connections, pin-type mismatches, pure-data cycles)
surface as a single clear error before any chain runs — instead of a
partial trace mid-dispatch.

- `engine/scripting/script_compiler.h/.cpp` add `ScriptGraphCompiler`,
  `CompiledScriptGraph`, `CompiledNode`, `CompiledInputPin`,
  `CompiledOutputPin`, `CompilationResult`, and `CompileDiagnostic`. The
  compiler is stateless and never throws — even a null-registry / empty
  graph input returns a usable result with an "empty graph" warning.
- Validation passes: node type resolution against the registry, unique
  node ids, connection endpoint resolution (source + target node and pin
  names by string lookup), pin kind match (exec↔exec, data↔data), pin
  data-type compatibility (ANY wildcard, same-type, and whitelisted
  widenings — INT→FLOAT, BOOL→INT/FLOAT, ENTITY→INT, COLOR↔VEC4, and
  all types → STRING, mirroring `ScriptValue` runtime coercions so the
  compile-time check never rejects a connection the interpreter would
  accept), input fan-in ≤ 1, pure-data cycle detection via iterative
  DFS (execution cycles and execution-output fan-out are intentionally
  permitted — loops, re-triggers, and `DoOnce.Then → Anim + Sound`
  templates depend on both), entry-point discovery (event nodes, OnStart,
  OnUpdate, and anything in the `Events` category so stub events like
  OnTriggerEnter / OnCollisionEnter still register as roots), and
  reachability classification that warns on orphaned impure nodes while
  ignoring pure library helpers.
- `ScriptingSystem::registerInstance` runs the compiler and refuses to
  activate instances that produce fatal errors, logging each diagnostic
  against the graph name. Warnings still activate so library-style
  graphs with no entry points don't get silently dropped.
- `CompiledScriptGraph` stores flat index-based wiring
  (`sourceNodeIndex` / `sourceOutputPinIndex` / `targetNodeIndices`) and
  `indexForNodeId()` so future codegen or bytecode back-ends can consume
  the IR without hashing node ids or pin names.
- Tests: 16 new cases in `tests/test_script_compiler.cpp` —
  every shipped gameplay template compiles clean, every error class
  exercised (unknown type, dangling connection, duplicate node id,
  duplicate input connection forged past `addConnection`'s editor-side
  dedupe, pin kind mismatch, pin data-type mismatch, pure-data cycle),
  entry-point discovery (OnUpdate), unreachable impure node warning,
  exec fan-out accepted, exec cycle accepted, full type-compatibility
  matrix, and resolved-wiring round-trip. Full suite: 2032 / 2032 pass.

### 2026-04-20 Phase 9E — Formula Node Editor panel (visual composition, drag-drop, live preview)

Closes the three remaining `Formula Node Editor` roadmap items under
Phase 9E in one panel inside the FormulaWorkbench:

- Visual formula composition UI (ImGui node editor canvas over
  `NodeGraph`, rendering every node's pins / links from the graph's
  own port layout — same `NodeEditorWidget` used by
  `ScriptEditorPanel`, separate `ed::EditorContext` so state cannot
  leak between the two canvases).
- Drag-and-drop from the `PhysicsTemplates` catalog into the graph
  (ImGui `BeginDragDropSource` → `FORMULA_TEMPLATE` payload →
  `AcceptDragDropPayload` on the canvas child). Click-to-load is the
  keyboard-friendly fallback.
- Output-node curve preview rendered via ImPlot under the canvas;
  samples the graph across a user-configurable sweep variable + range
  + sample count and plots the result every frame.

**Files.** `tools/formula_workbench/formula_node_editor_panel.{h,cpp}`
(panel + ImGui / ImPlot rendering) and `formula_node_editor_core.cpp`
(headless state: constructor, `initialize` / `shutdown`, `loadTemplate`,
`recomputePreview`, and the `sampleFormulaCurve()` / `findOutputNodeId()`
free helpers). Split is deliberate — tests link only `core.cpp` +
`vestige_engine` and exercise the full state machine without pulling
in ImGui / ImPlot / imgui-node-editor. `Workbench` owns one instance;
`View → Node Editor` toggles visibility; lifecycle hooked through new
`Workbench::initializeGui()` / `shutdownGui()` so `ed::DestroyEditor`
runs before `ImGui::DestroyContext` (same pattern as `ScriptEditorPanel`).

**Sampler guarantees.** `sampleFormulaCurve()` never throws — all
`ExpressionEvaluator` exceptions funnel into
`FormulaCurveSample::error`. Behaviours: empty graph / missing output
node / broken tree each set a descriptive error string; constant-only
graphs fan out to a flat line instead of a single point; auto-pick
selects the first variable referenced by the tree; unknown explicit
sweep variable silently falls back to auto-pick (so stale UI selection
from a previous template doesn't flash errors); unbound variables
default to 0.0f; `sampleCount` is clamped to `[2, 4096]`.

**Tests.** 13 new tests in `tests/test_formula_node_editor_panel.cpp`
covering the sampler behaviour matrix above plus the panel's state
transitions (load / unload / sweep-range update). Monotonicity check
on `ease_in_sine`, analytical-linearity check on `aerodynamic_drag`
swept on `vDotN`. Test-suite total: 2016 / 2016 (1 pre-existing skip).

`WORKBENCH_VERSION` bumped to `1.16.0`.

### 2026-04-20 Phase 9E-4 — gameplay templates menu in ScriptEditorPanel

Wires the 5 shipped gameplay templates into the Script Editor menu bar
so they're one click away from the canvas rather than buried behind a
C++ call. New `Templates` menu (between `File` and `View`) lists
Door Opens / Collectible Item / Damage Zone / Checkpoint /
Dialogue Trigger; hovering each item shows its one-line description,
clicking replaces the current graph with the template (sets `m_dirty`
and clears `m_currentPath` so the next save prompts for a path).

### 2026-04-20 Phase 9E-4 — pre-built gameplay script templates

Designer-side starter graphs for the five gameplay patterns called out
in the Phase 9E roadmap (door that opens, collectible item, damage
zone, checkpoint, dialogue trigger). New module
`engine/scripting/script_templates.{h,cpp}` exposes:

- `GameplayTemplate` enum (five values above).
- `buildGameplayTemplate(GameplayTemplate)` → fully-wired `ScriptGraph`
  whose `.name` matches the template so loaded instances survive
  round-trip through JSON.
- `gameplayTemplateDisplayName` / `gameplayTemplateDescription` for
  editor palette presentation.

All templates start from `OnTriggerEnter` — the stub event on the
EventBus side is already registered in the node registry with the
correct pin set, so the graphs are valid *now* and fire automatically
the moment trigger / collision events are wired through.

**Template wiring summary** (per-instance property defaults set via
`ScriptNodeDef::properties` so the graph is self-explanatory):

- `DOOR_OPENS` — `OnTriggerEnter` → `DoOnce` fan-outs to `PlayAnimation`
  ("DoorOpen", blend 0.2s) and `PlaySound`
  ("assets/sounds/door_open.ogg", vol 0.8).
- `COLLECTIBLE_ITEM` — `OnTriggerEnter` → `PlaySound`
  ("assets/sounds/pickup.ogg") → `SetVariable` (score ← 1) →
  `DestroyEntity` (self, via entity-input 0 fallback).
- `DAMAGE_ZONE` — `OnTriggerEnter` → `PublishEvent` ("damage",
  payload 10).
- `CHECKPOINT` — `OnTriggerEnter` → `DoOnce` → `SetVariable`
  ("lastCheckpoint" ← piped `otherEntity`) → `PrintToScreen`.
- `DIALOGUE_TRIGGER` — `OnTriggerEnter` → `DoOnce` → `PublishEvent`
  ("dialogue_started", "greeting") → `PrintToScreen`.

Tests: 8 in `tests/test_script_templates.cpp` — each of the 5 templates
validates and every connection's pin names resolve against the
populated `NodeTypeRegistry`; JSON round-trip preserves graph shape;
metadata coverage for all enum values; a sanity invariant that every
shipped template starts from `OnTriggerEnter`. Test-suite total:
2003 / 2003 (1 pre-existing skip).

### 2026-04-20 Phase 9E — CONDITIONAL node type (formula ternary round-trip)

Closes the last lossy conversion path in the Formula Workbench → node
graph round-trip. `ExprNodeType::CONDITIONAL` (ternary `if/then/else`)
now has a dedicated node-graph representation rather than silently
falling back to `literal(0)` with a warning.

**New API.** `NodeGraph::createConditionalNode()` builds an `If` node
with 3 typed float inputs (`Condition`, `Then`, `Else`) and a single
`Result` output, categorised as `INTERPOLATION` alongside lerp /
smoothstep.

**Bidirectional conversion.** `fromExpressionTree` now emits a real
conditional node with its three branch sub-trees wired up, and
`nodeToExpr` dispatches by `operation == "conditional"` before the
generic 1-input / 2-input branches so a 3-input node doesn't fall
through to the `literal(0)` fallback. The old `Logger::warning(...)`
path (AUDIT §M10 note) is deleted; `<core/logger.h>` include in
`node_graph.cpp` removed along with it.

Tests: 4 new in `tests/test_node_graph.cpp`
(`NodeGraph_Factory.CreateConditionalNode`,
`NodeGraph_ExprTree.FromExpressionTreeConditional`,
`RoundTripConditionalExpr`, `RoundTripNestedConditional`). Suite:
1995/1995 passing (1 pre-existing skipped). Unblocks PhysicsTemplates
with ternary saturation curves.

### 2026-04-20 Phase 9C closeout — editor UI layout + theme panel

The 6th (and last) Phase 9C UI/HUD sub-item. New editor panel
`engine/editor/panels/ui_layout_panel.{h,cpp}` registered under
`Window → UI Layout`.

**Two tabs:**

- **Element tree inspector.** Given a `UICanvas*`, shows each root
  element with visibility / interactivity flags + anchor tag, click
  to select; inspector below exposes the selected element's
  `position` / `size` (drag-float), `anchor` (combo), `visible` /
  `interactive` (checkbox). Edits propagate live to the rendered
  canvas.

- **Theme editor.** Full color-picker + size-drag surface over the
  active `UITheme`: backgrounds (bgBase / bgRaised / panelBg /
  panelBgHover / panelBgPressed), strokes/rules, text hierarchy,
  accent+ink, HUD, and component sizes (button / slider / checkbox /
  dropdown / keybind / crosshair / transition duration). "Reset to
  Vellum" / "Reset to Plumbline" buttons for quick-switch between
  the two shipped registers.

Supporting change: `UICanvas::getElementAt(size_t)` added (const +
non-const) so the panel can walk the element list without owning
the canvas.

**Out of scope for this panel (follow-ups):** drag-place widget
palette and JSON canvas serialisation. Both gated on factoring the
editor's ImGui viewport out of `editor.cpp` so the panel can capture
viewport mouse events without fighting the main viewport.

Tests: 5 new (`UILayoutPanel.*` defaults + toggle;
`UICanvasAccessor.*` null-when-empty, out-of-range returns null,
returns added elements in order, const overload). Suite: 1991/1991
passing.

**Phase 9C UI/HUD is now feature-complete across all 6 sub-items.**

### 2026-04-20 Phase 9B Step 12 — ClothComponent cutover to `unique_ptr<IClothSolverBackend>`

The last deferred item from the Phase 9B GPU compute cloth pipeline.
`ClothComponent` now owns its solver polymorphically — either
`ClothSimulator` (CPU XPBD) or `GpuClothSimulator` (GPU compute) —
selected at `initialize()` by `createClothSolverBackend()` per
`GPU_AUTO_SELECT_THRESHOLD` + `GpuClothSimulator::isSupported()`.

**Interface widening.** `IClothSolverBackend` now covers the full
mutator + accessor surface needed by `ClothComponent`,
`inspector_panel.cpp`, and `engine.cpp`:

- Live tuning: `setSubsteps`, `setParticleMass`, `setDamping`,
  `setStretchCompliance`, `setShearCompliance`, `setBendCompliance`.
- Wind: `setWind`, `setDragCoefficient`, `setWindQuality`,
  `getWindVelocity` / `getWindDirection` / `getWindStrength` /
  `getDragCoefficient` / `getWindQuality`.
- Pins / LRA: `pinParticle`, `unpinParticle`, `setPinPosition`,
  `isParticlePinned`, `getPinnedCount`, `captureRestPositions`,
  `rebuildLRA`.
- Diagnostics: `getConstraintCount`, `getConfig`.
- Colliders: `addSphereCollider` / `clearSphereColliders`,
  `addPlaneCollider` / `clearPlaneColliders`, `setGroundPlane` /
  `getGroundPlane`, `addCylinderCollider` / `clearCylinderColliders`,
  `addBoxCollider` / `clearBoxColliders`.

The nested `ClothSimulator::WindQuality` enum was promoted to a
top-level `ClothWindQuality` (declared in `cloth_solver_backend.h`)
so the interface can reference it without dragging in the full CPU
implementation. `ClothSimulator::WindQuality` stays as a
backwards-compat `using` alias.

**Backend coverage:**
- CPU (`ClothSimulator`): implements the full surface — no behaviour
  change; every method now carries `override`.
- GPU (`GpuClothSimulator`): supports sphere/plane/ground colliders
  and the full live-tuning surface. Cylinder/box/mesh colliders are
  CPU-only per the design doc; GPU backend logs a one-time warning
  and drops them so call sites can drive a single code path.
  `captureRestPositions` is a no-op on GPU (rest pose is implicit
  in the CPU position mirror).

**`ClothComponent` changes:**
- `m_simulator` is now `std::unique_ptr<IClothSolverBackend>`.
- `getSimulator()` returns `IClothSolverBackend&` — call sites using
  the old `ClothSimulator&` return type get the polymorphic view
  transparently (every method they called is now on the interface).
- New `setBackendPolicy(AUTO|FORCE_CPU|FORCE_GPU)` and
  `setShaderPath(const std::string&)` setters invoked before
  `initialize()` to override the auto-select or pin GPU for tests.

**Inspector panel** updated to use the new top-level
`ClothWindQuality` enum (was `ClothSimulator::WindQuality`).

Suite: 1986/1986 still passing — the interface widening preserves
every caller's semantics. Phase 9B GPU compute cloth pipeline is
now fully end-to-end.

### 2026-04-20 Phase 9C font swap — Inter Tight / Cormorant Garamond / JetBrains Mono

Asset-side change to back the typography pairing specified in the
`vestige-ui-hud-inworld` Claude Design hand-off.

Three new OFL fonts added under `assets/fonts/`:
- **`inter_tight.ttf`** (variable weight, 568 KB) — UI default,
  rasterises cleaner at small sizes through FreeType than Arimo did.
- **`cormorant_garamond.ttf`** (variable weight, 1.14 MB) — display
  face for the wordmark + modal titles (Vellum register).
- **`jetbrains_mono.ttf`** (variable weight, 183 KB) — mono face
  for captions / micro labels / key-caps / numerics.

`default.ttf` was removed from the engine — its two call sites
(`engine/core/engine.cpp` text renderer init, `engine/editor/editor.cpp`
ImGui font load) now load `inter_tight.ttf` directly. Arimo is
preserved as `assets/fonts/arimo.ttf` for backwards-compatibility
with any external consumer that referenced the old default by path.

`assets/fonts/OFL.txt` rewritten as a consolidated manifest carrying
per-font copyright headers (Arimo, Inter Tight, Cormorant Garamond,
JetBrains Mono) above the single shared OFL 1.1 body. Each font's
Reserved Font Name is called out separately so the OFL clause-3
restriction is unambiguous.

`ASSET_LICENSES.md` and `THIRD_PARTY_NOTICES.md` updated to list all
four fonts with attributions.

**Caveat:** `TextRenderer` is still single-font today — it loads
whichever TTF was passed to `initialize()` and renders everything
through that face. The `UITheme::fontDisplay` / `fontUI` / `fontMono`
logical names are forward-looking metadata. Multi-font support
(routing labels through one face, wordmark through another) is a
separate `TextRenderer` refactor not covered by this commit.

Suite: 1986/1986 still passing (no test depended on the old
`default.ttf` path).

### 2026-04-20 Phase 9C UI batch 4 — menu prefabs (Main / Pause / Settings)

Composes the Phase 9C widget set into the three menu canvases per
the `vestige-ui-hud-inworld` Claude Design layouts.

New module `engine/ui/menu_prefabs.{h,cpp}` with three factory
functions, each taking a fresh `UICanvas` + theme + text renderer
and populating the canvas with positioned widgets:

- `buildMainMenu` — top chrome rule, "VESTIGE" wordmark + 5-item
  button list (New Walkthrough / Continue / Templates / Settings /
  Quit) on the left, continue card on the right, footer with
  keyboard shortcut hints. Quit uses `UIButtonStyle::DANGER`.
- `buildPauseMenu` — tinted scrim, centred 720×760 modal panel with
  4 corner brackets in accent (drawn as 8 thin strips), "PAUSED"
  caption + "The walk is held." headline, 7 buttons (Resume primary,
  Quit-to-Desktop danger, others default), footer line with autosave
  + slot info.
- `buildSettingsMenu` — full-bleed modal (inset 120/80 px), header
  with title + ESC close ghost button, header rule, 300-px-wide
  sidebar with 5 categories (first one accent-highlighted), vertical
  rule separating sidebar from content area, footer with dirty
  indicator + Restore Defaults / Revert / Apply buttons.

**Settings is chrome-only by design.** Per-category controls are
per-game integration — the engine can't know which settings any
given game project exposes. The chrome guarantees every game's
settings menu shares the same framing + footer language.

Builders are safe to call without a `TextRenderer*` (the nullptr
passes through to text elements which skip the draw call). This
lets game projects construct prefabs at startup before the renderer
is wired.

Tests: 6 new (`MenuPrefabs.*` covering element-count bounds for each
prefab, Plumbline-register parity, double-build duplication,
nullptr-safety). Suite: 1986/1986 passing.

**Phase 9C UI/HUD: 5 of 6 done.** Only the editor visual UI layout
editor remains as a separate larger initiative.

### 2026-04-20 Phase 9C UI batch 3 — Claude Design Vellum theme + interactive widget set

Translates the `vestige-ui-hud-inworld` Claude Design hand-off into
native engine widgets. Two visual registers (Vellum primary,
Plumbline alternative) and the full interactive widget family
needed for the menu prefabs.

**`UITheme` widening** — palette now matches the design's Vellum
register: warm bone text on deep walnut-ink, burnished-brass accent.
New fields: `bgBase`, `bgRaised`, `panelStroke`, `panelStrokeStrong`,
`rule`, `ruleStrong`, `accentInk` (text drawn on accent fills).
Component sizing tokens added (`buttonHeight`, `sliderTrackHeight`,
`checkboxSize`, `dropdownHeight`, `keybindKeyMinWidth`, etc.) +
type sizes (display 88, H1 42, body 18, caption 14, etc.) + font
family logical names (`fontDisplay = "Cormorant Garamond"`,
`fontUI = "Inter Tight"`, `fontMono = "JetBrains Mono"` —
asset-side font swap is a follow-up; the engine still renders Arimo
until the OFL fonts ship). `UITheme::plumbline()` static returns the
alternative register with cooler near-black backgrounds and the same
component sizing.

**Five new widgets:**
- `UIButton` — `.btn` family. Variants: `DEFAULT`, `PRIMARY`, `GHOST`,
  `DANGER`. State enum (`NORMAL`/`HOVERED`/`PRESSED`/`DISABLED`)
  drives colour selection. Hover renders a 4 px brass tick on the
  left edge for `DEFAULT`/`DANGER` (matches the design's `.btn::before`).
  Optional `UIButtonShortcut` renders a key-cap on the right edge.
  `small` flag toggles `.btn--sm` height.
- `UISlider` — track + accent fill + 16×16 thumb with 2 px accent
  ring + right-aligned mono value readout. Optional formatter
  callback (defaults to `"N %"`). Optional tick marks across the
  track. `ratio()` accessor exposes the clamped fill fraction.
- `UICheckbox` — 20×20 box; accent-filled with a checkmark drawn in
  `accentInk` when checked, 1.5 px stroked when unchecked. Hover
  brightens the stroke. Inline label drawn 12 px to the right.
- `UIDropdown` — 40 px tall, mono caret indicator, hover/open states
  brighten the border. Open state draws a popup menu with the option
  list (selected option in accent). `currentLabel()` returns the
  display string for the active option.
- `UIKeybindRow` — label / key-cap / CLEAR layout. Listening state
  renders "PRESS KEY..." in accent on accent-bordered key-cap.

Stylistic decisions echo the design verbatim: the accent tick on the
left edge of menu buttons, dropdown caret as an ASCII arrow until
the engine font ships arrow glyphs, key-cap as a bordered mono
fragment, hover-brightened panel-stroke language across all widgets.

**Tests:** 13 new (`UIThemeRegisters.*` covering Vellum warm-bone
text invariant, Plumbline darker-background invariant, accent /
accentInk luma contrast, and shared component sizing across both
registers; `UIButton.*` covering defaults, small-flag height,
without-theme safety; `UISlider.*` for ratio clamping + degenerate
range; `UICheckbox.*` defaults; `UIDropdown.*` `currentLabel()`
out-of-range handling + closed defaults; `UIKeybindRow.*` defaults).
Plus one `UITheme.AccentDimIsDarkerShadeOfAccent` rewrite — old test
asserted dim was translucent (my earlier batch-1 interpretation);
new design uses dim as a darker opaque shade for pressed states, so
the assertion now compares luma instead of alpha. Suite: 1980/1980
passing.

**Still pending in Phase 9C UI/HUD (in-flight):** menu prefab
factories (Main / Pause / Settings) — widgets are in place; next
commit composes them into the three menu canvases per the design's
React layouts. Editor visual UI layout editor remains a separate
larger initiative.

### 2026-04-20 Phase 9C UI batch 2 — in-world UI

Ticks the 4th of 6 remaining Phase 9C UI/HUD sub-items. Two new
elements + one extracted helper.

`ui/ui_world_projection.{h,cpp}` — pure-CPU `projectWorldToScreen()`
helper. Takes a world point + combined view-projection matrix +
viewport size, returns a `WorldToScreenResult` with the top-left-origin
screen pixel coords + NDC depth + a `visible` flag (false when the
point is behind the camera or outside the [-1, 1] NDC clip box).
Extracted as a free function so the projection + frustum-cull logic
is testable without a GL context.

`ui/ui_world_label.{h,cpp}` — `UIWorldLabel`. Anchors to a
`worldPosition`, projects each frame, and draws via
`TextRenderer::renderText2D` at the resulting screen pixel.
`screenOffset` lifts the label above the anchor (e.g. above an
entity's head). The base UIElement's `position` / `anchor` fields
are intentionally ignored — world-space anchoring takes precedence.
Off-screen / behind-camera labels are silently skipped.

`ui/ui_interaction_prompt.{h,cpp}` — `UIInteractionPrompt` extends
`UIWorldLabel`. Two text fields (`keyLabel`, `actionVerb`) compose
into "Press [keyLabel] to actionVerb". Linear distance-based alpha
fade: full opacity at `fadeNear` (default 2.5 m), zero at `fadeFar`
(default 4.0 m). Camera distance is consulted before any projection
work so off-range prompts cost nothing.

Nameplate use case is handled by `UIWorldLabel` directly — game code
calls `nameplate.worldPosition = entity.getWorldPosition() + headOffset`
each frame.

Tests: 11 new (`UIWorldProjection.*` covering behind-camera cull,
centred-when-directly-ahead, off-screen cull, NDC depth bounds,
zero-viewport defensive case; `UIInteractionPrompt.*` covering text
composition, fade-at-bounds, linear midpoint, default interactivity).
Suite: 1967/1967 passing.

**Still pending in Phase 9C UI/HUD (2 of 6):** menu system (best
driven by Claude Design mockups for the visual look first) + editor
visual UI layout editor.

### 2026-04-19 Phase 9C UI batch 1 — theme + input routing + HUD widgets

Ticks 3 of the 6 remaining Phase 9C UI/HUD sub-items.

**UITheme** (`engine/ui/ui_theme.h`) — central style struct consulted by
in-game UI elements. Bg / text / accent palettes, HUD-specific crosshair
+ progress-bar colours, default text scale + crosshair / progress-bar
sizes. `UITheme::defaultTheme()` returns sane neutrals; game projects
override per-field via `UISystem::getTheme()` (mutable ref). Marketing-
facing visual lock-in is best driven by Claude Design mockups before
freezing the final palette.

**Input routing** — `UISystem::setModalCapture(bool)` for sticky modal
capture (pause menu, dialog), `updateMouseHit(cursor, w, h)` for
cursor-over-interactive-element capture. `wantsCaptureInput()` returns
the union — game input handlers consult it each frame and skip
movement / look / fire bindings when true. The pre-existing
`m_wantsCaptureInput` field stays for ABI continuity but the canonical
sources are now the modal flag + the cursor-hit cache.

**HUD widgets** — three `UIElement` subclasses:
- `UICrosshair` — centred plus pattern with configurable arm length,
  thickness, and centre gap. Always renders at viewport centre
  regardless of the base UIElement's anchor / position (matches FPS
  reticle conventions).
- `UIProgressBar` — horizontal bar with `value / maxValue` fill ratio
  (clamped to [0,1]); separate fill / empty colours; skips the fill
  draw call when ratio == 0.
- `UIFpsCounter` — smoothed FPS via exponential moving average (caller
  feeds `tick(dt)` each frame); drawn through `TextRenderer::renderText2D`
  with `"%.0f FPS"` formatting.

**Tests:** 12 new (`UITheme.*`, `UIProgressBar.*`, `UIFpsCounter.*`,
`UICrosshair.*`, `UISystemInput.*`). Suite: 1957/1957 passing.

**Still pending in Phase 9C UI/HUD:** in-world UI (floating text,
interaction prompts), menu system (main menu / pause / settings —
Claude Design candidate for visual mockups), editor visual UI layout
editor (multi-week initiative).

### 2026-04-19 Phase 9B GPU compute cloth pipeline — feature complete

Bundles Steps 7–11 of the Phase 9B GPU cloth migration (Steps 1–6
shipped earlier today). The XPBD cloth solver is now fully
implemented on the GPU as a parallel alternative to the existing
CPU `ClothSimulator`.

**Step 7 — collision (sphere + plane + ground).** New compute shader
`assets/shaders/cloth_collision.comp.glsl`. Per-particle thread loops
over sphere + plane collider arrays (passed as a UBO at
binding 3, std140 layout, capped at 32 spheres + 16 planes). Pushes
particles to `surface + collisionMargin` and zeros inward velocity.
New mutators: `addSphereCollider`, `clearSphereColliders`,
`addPlaneCollider`, `clearPlaneColliders`, `setGroundPlane`,
`setCollisionMargin`. UBO uploaded lazily when collider state changes.
Cylinder + box + mesh colliders deferred per the design doc.

**Step 8 — normals.** New compute shader
`assets/shaders/cloth_normals.comp.glsl`. Per-particle thread walks
the (up to) 6 grid-adjacent triangles, accumulates area-weighted
face normals, normalises. Atomic-free — each particle is the sole
writer of its own normal slot. Runs once per frame (not per substep)
since normals are for rendering, not physics. Render path still goes
through `ClothComponent`'s vertex buffer for now; the SSBO-direct
render path is bundled with the deferred `ClothComponent` cutover.

**Step 9 — pins + LRA tethers.** New compute shader
`assets/shaders/cloth_lra.comp.glsl` — unilateral tethers that
activate only when a free particle has drifted past its rest-pose
distance from its nearest pin. No graph colouring needed (each
thread writes only its own particle). New `GpuLraConstraint` type
+ `generateLraConstraints()` helper. Pin support on
`GpuClothSimulator`: `pinParticle` / `unpinParticle` /
`setPinPosition` / `isParticlePinned` / `getPinnedCount`,
`rebuildLRA()`. CPU position mirror's `w` channel is the source of
truth for pin state; positions SSBO is re-uploaded when pins change.

**Step 10 — auto CPU↔GPU select factory.** New module
`engine/physics/cloth_backend_factory.{h,cpp}` with
`chooseClothBackend()` (pure CPU, testable) and
`createClothSolverBackend()` (constructs the chosen backend). Three
policies: `AUTO`, `FORCE_CPU`, `FORCE_GPU`. Threshold:
`GPU_AUTO_SELECT_THRESHOLD = 1024` particles (≈ 32×32 grid). The
`ClothComponent` swap to `unique_ptr<IClothSolverBackend>` is
intentionally a follow-up commit — the factory is in place and
unit-tested so the cutover is a one-line change at the call site
plus broadening the `IClothSolverBackend` interface to cover the
mutator surface used by `inspector_panel`.

**Step 11 — sweep.**
- `tools/audit/audit_config.yaml` gains a new `shader.ssbo_vec3_array`
  rule that flags `vec3 \w+\[\]` in `*.comp.glsl` files. std430's
  array stride for `vec3` is 16 B on Mesa AMD (and is implementation-
  defined elsewhere); the GPU cloth pipeline uses `vec4` everywhere
  with `w` as padding / inverse mass. The audit rule guards against a
  follow-up commit silently reintroducing the pitfall.
- `ROADMAP.md` Phase 9B "GPU Compute Cloth Pipeline" item ticked,
  with deferred follow-ups documented inline (GPU self-collision,
  GPU mesh-collider, GPU tearing, `ClothComponent` cutover, Vulkan
  port, perf-acceptance gate).

**Test coverage delta across Steps 7–11**: 13 new tests
(`GpuClothSimulator.*` collider defaults / accept / reject / clear /
binding pin; `GpuClothSimulator.*` pin defaults / LRA binding;
`ClothConstraintGraph.*` LRA empty / tether-every-free-particle;
`ClothBackendFactory.*` AUTO / FORCE_CPU / FORCE_GPU / no-context
fallback / threshold pin / CPU-create-and-init). Suite: **1945/1945**
passing across the full engine (up from 1899 at Step 1 entry).

**What's still gated:** the GPU backend is implemented and
unit-tested but is not yet wired into `ClothComponent::m_simulator`.
The factory exists; the call-site swap and the broader
`IClothSolverBackend` interface widening are a follow-up because
they touch every `getSimulator()` caller (especially
`inspector_panel.cpp`). When that lands, the
`docs/PHASE9B_GPU_CLOTH_DESIGN.md` § 7 perf-acceptance gates
(100×100 ≥ 120 FPS on RX 6600, etc.) become the merge criteria.

### 2026-04-19 Phase 9B Step 6: cloth_dihedral.comp.glsl + dihedral constraints

Per-quad-pair angle-based bending lands on the GPU. Different math
from the skip-one *distance* bend (Step 5): a dihedral constraint
binds two adjacent triangles via their shared edge and constrains
the angle between their face normals to a rest angle (Müller et al.
2007 — same formulation the CPU `ClothSimulator::solveDihedralConstraint`
uses, so behaviour matches).

New compute shader `assets/shaders/cloth_dihedral.comp.glsl` —
`local_size_x = 32` (smaller workgroup than the distance shader to
match the larger per-thread register footprint of 4-particle
gradient computation). Reads the dihedral SSBO, computes face
normals for both triangles, the current/rest angle delta, the four
gradient vectors, and writes corrections to all four particles.

New types in `cloth_constraint_graph`:
- `GpuDihedralConstraint` — `uvec4 p` (wing0, wing1, edge0, edge1)
  + `vec4 params` (restAngle, compliance, padding) → 32 B std430.
- `generateDihedralConstraints()` — walks the triangle index buffer,
  hashes each edge `(min(v0,v1), max(v0,v1))` into an
  `unordered_map`, and emits one constraint per edge shared by
  exactly two triangles. Boundary and non-manifold edges are skipped.
- `colourDihedralConstraints()` — same greedy 64-bit-bitset algorithm
  as the distance variant, generalised to 4 endpoints. Within a
  colour no two dihedrals touch any of the same four particles.

`GpuClothSimulator` upgrades:
- New SSBO `BIND_DIHEDRALS = 5` (32 B per constraint).
- New `m_dihedralShader`, loaded alongside the others when shader
  path is set.
- `simulate()` substep loop gains a step 4 (dihedral solve) right
  after the distance solve; same per-colour structure but smaller
  workgroup count (32 vs 64).
- New accessors `getDihedralCount()`, `getDihedralColourCount()`,
  `getDihedralsSSBO()`.
- `destroyBuffers()` and `reset()` clean up the dihedral state.

For a 4×4 grid: 21 dihedral constraints (formula `3MN − M − N` for
M=N=3). For a flat grid every rest angle is 0 — the cloth's neutral
pose is "lay flat", and the constraint pushes back proportional to
how far folded the cloth deviates from that pose.

Tests: 6 new dihedral tests in `test_cloth_constraint_graph.cpp`
(analytical count formula, flat-grid rest angle = 0, single-triangle
yields no dihedrals, the load-bearing "no shared particle within
colour" invariant on a 6×6, partition contract, struct-size pin).
2 new `GpuClothSimulator.*` tests (binding-enum pin, default-state
zero accessors). Suite: 1927/1927 passing.

### 2026-04-19 Phase 9B Step 5: bend constraints (skip-one distance edges)

`generateGridConstraints()` extended with a `bendCompliance`
parameter and now also emits skip-one stretch edges along X and Z
(`(x,z)–(x+2,z)` and `(x,z)–(x,z+2)`). Bend edges share the same
XPBD distance-constraint shader as stretch/shear — only the rest
length and compliance differ — so they slot transparently into the
existing colour partitioning + multi-pass dispatch loop.

`GpuClothSimulator::buildAndUploadConstraints()` now passes
`config.bendCompliance` through. Cloth resists folding rather than
just pulling apart along the grid lines.

Per-interior-particle degree of the constraint graph rises from 8
(stretch+shear) to 12 (+ skip-one in 4 cardinal directions), so
greedy colouring's worst case rises from Δ+1=9 to 13. The
`ColouringIsConservativeForRegularGrid` cap was loosened from 12
to 16 colours to match (still flags any real algorithmic
regression).

Tests: 2 new bend-focused tests (`BendConstraintsHaveSkipOneRestLength`
verifying rest = 2·spacing, `NoBendConstraintsForGridSmallerThanThree`
guarding the 2×N edge case). Existing test counts updated.
Suite: 1919/1919 passing.

### 2026-04-19 Phase 9B Step 4: cloth_constraints + greedy graph colouring

XPBD distance-constraint solver lands on the GPU.

New compute shader `assets/shaders/cloth_constraints.comp.glsl` —
one thread per constraint within a colour group, computes the XPBD
position correction `Δp = -C / (w0 + w1 + α̃) · n` (with
`α̃ = compliance / dt²`), and writes both endpoints back to the
positions SSBO. Within a colour no two constraints share a particle,
so writes are race-free without atomics. Pinned-on-both-ends and
zero-length constraints are short-circuited.

New module `engine/physics/cloth_constraint_graph.{h,cpp}` —
pure-CPU helpers used at `initialize()` time:
- `generateGridConstraints()` builds stretch (W·H structural edges)
  and shear (down-right + down-left diagonals) constraints, mirroring
  the topology of the CPU `ClothSimulator`.
- `colourConstraints()` runs greedy graph colouring over those
  constraints, reorders them in place by colour, and returns
  per-colour `[offset, count]` slices. A 64-bit per-particle bitset
  tracks "colours seen so far"; the lowest unused bit becomes the
  constraint's colour. For a regular grid this lands at ~5 colours
  (well under the Δ+1 = 7 worst case).

`GpuClothSimulator` upgrades:
- New SSBO `BIND_CONSTRAINTS = 4` holds `GpuConstraint[]`
  (i0, i1, restLength, compliance — 16 B each, std430 friendly).
- `simulate()` now runs an XPBD substep loop (default 10 substeps,
  matches the CPU path). Each substep: wind dispatch → barrier →
  integrate → barrier → for each colour { constraint dispatch →
  barrier }. Damping is split across substeps so visual behaviour
  is comparable as substep count varies.
- `setSubsteps()` accessor (clamps to ≥ 1). `getConstraintCount()` /
  `getColourCount()` accessors for telemetry + tests.

The cutover from `ClothSimulator` to `GpuClothSimulator` inside
`ClothComponent` is still gated behind Step 10; until then this
backend is exercised by tests + manual instantiation only.

Tests: 8 new `ClothConstraintGraph.*` tests (counts, rest lengths,
edge cases, the load-bearing "no shared particle within colour"
invariant on an 8×8 grid, conservative colour-count sanity check on
16×16, and the offset/count partition contract); 3 new
`GpuClothSimulator.*` tests (constraint count is zero pre-init,
substep clamping, binding-enum pinning). Suite: 1917/1917 passing.

### 2026-04-19 Phase 9B Step 3: cloth_wind + cloth_integrate compute shaders

First real GPU work. Two compute shaders land:
- `assets/shaders/cloth_wind.comp.glsl` — applies gravity + uniform
  wind-drag force to per-particle velocities. `local_size_x = 64` to
  fit AMD wavefronts. Per-particle noise / per-triangle drag (the CPU
  path's FULL wind tier) is intentionally deferred.
- `assets/shaders/cloth_integrate.comp.glsl` — symplectic Euler with
  velocity damping. Snapshots `prev` then advances `pos += vel · dt`.
  Pinned particles (positions[i].w == 0) are skipped — the inverse-mass
  channel is reserved for Step 9 LRA / pin work; Step 3 leaves every
  particle's w at 1 (free).

`GpuClothSimulator::simulate()` now dispatches: bind velocities → wind
shader → `glMemoryBarrier` → bind positions/prev/velocities → integrate
shader → `glMemoryBarrier` → mark CPU mirror dirty. Free-fall cloth
visibly drops under gravity in the editor.

Loading: `setShaderPath()` must be called pre-`initialize()`. Without
a shader path the SSBOs still allocate but `simulate()` is a no-op
(CPU mirror returns the rest pose), so any caller that forgets to wire
up the shader directory degrades gracefully rather than crashing.

CPU readback: `getPositions()` / `getNormals()` are now lazy — each
calls `glGetNamedBufferSubData` and stages vec4→vec3 only when the
mirror is dirty. The dirty flag is set by `simulate()` and cleared by
the next reader. Per-frame readback while the renderer still uploads
through `ClothComponent`'s vertex buffer; Step 8 will switch the
renderer to read SSBOs directly and skip readback entirely on the hot
path.

`reset()` re-uploads the rest-pose grid into positions/prev and zeros
velocities. Mirror is left clean (it was never moved by simulate; only
mutated by readback).

`Shader::setUInt()` added (just `glUniform1ui`) — used by the cloth
shaders' `uniform uint u_particleCount`. Reusable for future GLSL
unsigned uniforms.

Tests: 2 new unit tests (`HasShadersDefaultsFalse`,
`ParameterSettersCompileAndAccept`); 1906/1906 passing. GPU dispatch
correctness is a visual-launch verification item per the
`tests/test_gpu_particle_system.cpp` precedent.

### 2026-04-19 Phase 9B Step 2: GpuClothSimulator skeleton

New backend `engine/physics/gpu_cloth_simulator.{h,cpp}` — the GPU
half of the IClothSolverBackend dual. Step 2 scope is buffer
plumbing only: SSBO allocation in `initialize()`, teardown in the
destructor, no-op `simulate()`, CPU mirror returned by `getPositions()`
/ `getNormals()`. The compute-shader dispatches land incrementally
in Steps 3–9 per the design doc.

Five SSBOs are allocated up-front using DSA (`glCreateBuffers` /
`glNamedBufferStorage`): positions, prev positions, velocities,
normals, indices. All particle buffers use `vec4` layout (xyz + w
padding / future inverse-mass channel) to dodge std430's vec3-array
padding pitfall — same workaround the GPU particle pipeline already
uses on Mesa AMD. Binding indices are pinned via a
`BufferBinding` enum (0/1/2/6/7) that pairs with the cloth_*.comp.glsl
contract from the design doc.

`isSupported()` is a no-context-safe probe: returns false if no GL
context is current, otherwise checks for GL ≥ 4.5 (DSA + compute +
SSBO). Callers can call it before `initialize()` to decide whether
to construct the GPU backend at all.

Tests: 5 new unit tests in `tests/test_gpu_cloth_simulator.cpp`
covering default state, polymorphic construction via
`unique_ptr<IClothSolverBackend>`, the no-context probe path,
SSBO-handle-zero-pre-init invariants, and a guard against
accidental SSBO-binding-index reordering. Suite: 1904/1904 passing
(no regressions; 6 cloth-backend tests now alongside the 80
existing cloth tests).

### 2026-04-19 Phase 9B Step 1: IClothSolverBackend interface

New header `engine/physics/cloth_solver_backend.h` declaring
`IClothSolverBackend` — the per-frame simulation contract that
both `ClothSimulator` (CPU XPBD) and the upcoming
`GpuClothSimulator` (Phase 9B GPU compute) will satisfy.

Scope is intentionally lean: only the lifecycle + readback methods
are virtual (`initialize`, `simulate`, `reset`, `isInitialized`,
`getParticleCount`, `getPositions`, `getNormals`, `getIndices`,
`getTexCoords`, `getGridWidth/Height`). Configuration mutators
(`setWind`, `addSphereCollider`, `pinParticle`, etc.) remain on
the concrete `ClothSimulator` type during the transitional phase
and will widen as the GPU backend matures — see
`docs/PHASE9B_GPU_CLOTH_DESIGN.md` § 4.

`ClothSimulator` now inherits from `IClothSolverBackend` and
marks the 11 methods `override`. No behavioural change.
`ClothComponent` keeps its concrete embedding (`ClothSimulator
m_simulator`) for now; the cutover to `unique_ptr<IClothSolverBackend>`
lands in a later step once `GpuClothSimulator` exists.

Tests: 4 new unit tests in `tests/test_cloth_solver_backend.cpp`
covering polymorphic construction, initialize-through-interface,
simulate-and-reset round-trip, and virtual-destructor safety.
Suite: 1899/1899 passing (no cloth regressions across 80 existing
cloth tests).

### 2026-04-19 Phase 9C: Navigation editor — visualisation + bake controls

New `NavigationPanel` editor panel
(`engine/editor/panels/navigation_panel.{h,cpp}`) drives the
Navigation domain system from the editor: exposes Recast build
parameters as ImGui `DragFloat`/`DragInt` widgets, fires
`NavigationSystem::bakeNavMesh()` on a button press, reports
last-bake polygon count and wall-clock time, and provides a
"Show polygon overlay" toggle that draws every navmesh polygon's
edges via the engine's `DebugDraw` line renderer (configurable
colour + Y-lift to avoid z-fighting).

Wiring:
- `Editor::setNavigationSystem()` accepts the live system pointer
  during engine init (mirrors `setFoliageManager` / `setTerrain` /
  `setProfiler`).
- `NavMeshBuilder::extractPolygonEdges()` walks Detour tiles via
  the public const `getTile()` overload, skipping
  `DT_POLYTYPE_OFFMESH_CONNECTION` polys, appending segment
  endpoints to a caller-supplied buffer.
- `engine.cpp` calls the extractor + `DebugDraw::line` in the
  existing per-frame debug-draw pass when the panel toggle is on.
- `Window` menu gets a new `Navigation` toggle next to `Terrain`.

Tests: 6 new unit tests in `tests/test_navigation_panel.cpp`
covering panel defaults, toggle behaviour, overlay parameter
sanity, and the `extractPolygonEdges()` empty-mesh + append
contracts. Suite: 1895/1895 passing.

Closes the **Editor: navmesh visualization and bake controls**
item under Phase 9C → AI & Navigation in `ROADMAP.md`. Patrol
path placement remains deferred to Phase 16 (AI behaviour trees).

### 2026-04-19 Phase 9B GPU compute cloth — design doc

New design document `docs/PHASE9B_GPU_CLOTH_DESIGN.md` for the
last-remaining Phase 9B sub-item: migrating the XPBD cloth solver
to a GPU compute pipeline (SSBO storage + 4 compute shaders +
red-black graph colouring + auto CPU↔GPU select). Implementation
gated on maintainer review per CLAUDE.md research-first rule;
covers algorithm, file layout, buffer layout, workgroup sizing,
testing strategy, perf acceptance criteria, risks, and explicit
out-of-scope items.

### 2026-04-19 tooling: CMake compatibility CI matrix

`.github/workflows/ci.yml` gains a separate `cmake-compat` job
exercising the engine's declared minimum (`3.20.6`) and the
latest upstream CMake on every push/PR via
`jwlawson/actions-setup-cmake@v2`. Release-only, build-and-test
(no audit), kept separate from `linux-build-test` so main-CI cost
is unchanged. Catches FetchContent / SOURCE_SUBDIR regressions
before downstream users report them. Closes the
`PRE_OPEN_SOURCE_AUDIT.md` §8 follow-up.

### 2026-04-19 tooling: pretool frugal-output Bash hook

Adds a `PreToolUse` hook (`tools/hook-pretool-bash-frugal.sh`) that
bounces known-noisy commands (`pytest` without `-q`, `cmake --build`
without a tail/redirect, `ctest -V`, `tools/audit/audit.py`) with a
one-line reminder pointing at `| tail -200` / `--quiet` / `> /tmp/
<name>.log`. Bypassed via a trailing `# frugal:ok` marker. Saves
~5–20 k context tokens per accidental verbose run.

`.claude/settings.json` — three read-only allowlist additions
(`gitleaks detect *`, `semgrep --config *`,
`clang-include-cleaner --disable-insert *`). Most observed traffic
was already covered by Claude Code's built-in allowlist or existing
cmake/ctest/cppcheck/clang-tidy entries.

### 2026-04-19 audit tool 2.14.0 — three detectors close out the 30-idea list

Ships the final three queued detectors from the 2026-04-19
"30 consolidated detector ideas" list. The list is now fully shipped.

- **`per_frame_heap_alloc`** (tier 4, MEDIUM in-loop / LOW otherwise)
  — idea **#18**. Flags heap allocations inside per-frame functions
  (`render` / `draw` / `update` / `tick`). Brace-balanced loop
  tracking; honours `// ALLOC-OK` reviewer markers and skips
  `static const` one-shot initialisers.
- **`dead_public_api`** (tier 4, LOW) — idea **#25**. Flags public
  class / free-function declarations with zero external callers via
  word-bounded full-corpus grep.
- **`token_shingle_similarity`** (tier 4, LOW) — idea **#28**. Jaccard
  similarity over hashed K-token windows; complements line-aligned
  `tier4_duplication` by catching reflowed near-duplicates.

Also in this commit:
- `lib/config.py` `DEFAULTS` dict split into per-section module-level
  blocks (`_DEFAULTS_PROJECT` / `_BUILD` / `_TIER4` / …) assembled
  at the bottom — adding a future detector default is now a
  localised edit.
- `lib/config.py` `Config.enabled_tiers` fallback fixed: was
  `[1..5]`, now matches `DEFAULTS["tiers"] = [1..6]`.

45 new unit tests; full audit suite now at 850 passing. Smoke run
against the engine: 63 per-frame allocs / 238 functions, 4 / 2398
dead public APIs, 5 similar pairs / 597 files — all real signal, no
FP flood.

### 2026-04-19 docs: sync ROADMAP / PHASE9E3_DESIGN / ARCHITECTURE §19

Pure documentation-sync pass (no code, no tests). Phases 9A / 9C /
9D had been shipping code without corresponding checkbox /
annotation updates in `ROADMAP.md`; Phase 9E-3's acceptance-criteria
checklist hadn't reflected what actually landed in commits `cffd755`
/ `e0c56c2`.

- **ROADMAP.md** — Phase 9A marked COMPLETE (10 sub-bullets ticked
  with file refs and noted renames); Phase 9C marked FOUNDATIONS
  SHIPPED (3 items ticked, 15 annotated "deferred to Phase 10" or
  "not yet"); Phase 9D marked COMPLETE (all 4 sub-sections ticked,
  game-template enum confirmed covering all 6 variants).
- **docs/PHASE9E3_DESIGN.md** §13 — 5 acceptance-criteria items
  ticked with commit refs (library integration, M9 / M10 / M11,
  L6); progress header added noting Steps 1–3 shipped, Step 4 WIP,
  12 remaining.
- **ARCHITECTURE.md §19** — new "Editor integration (Phase 9E-3)"
  subsection describing the `NodeEditorWidget` / `ScriptEditorPanel`
  split, current Step 4 scope, `CommandHistory` integration plan,
  and hot-reload contract.

### 2026-04-19 L41 follow-up: `-Werror` lock-in

Enables `-Werror` on the `vestige_engine` target now that the 2026-04-19
L41 sweep drove it to zero warnings under the full
`-Wformat=2 / -Wconversion / -Wsign-conversion / -Wshadow /
-Wnull-dereference / -Wdouble-promotion / -Wimplicit-fallthrough` set.
Future regressions now fail the build instead of silently accumulating.

Build clean, 1889/1889 tests pass. If a warning ever needs a justified
suppression in the future, the policy is a narrowly-scoped `#pragma`
at the call site — never a global flag removal.

### 2026-04-19 GI roadmap sync + SH-probe-grid unit tests

Reconciles `docs/GI_ROADMAP.md` with the actual engine state — SH
probe grid (2026-03-29) and radiosity baker (2026-03-30) landed
months ago but were still marked "Planned" in the roadmap. Next GI
step is now **SSGI** (Screen-Space Global Illumination), promoted
from MEDIUM to HIGH priority.

- `docs/GI_ROADMAP.md` — items 2 (SH grid) and 3 (radiosity) marked
  IMPLEMENTED with file pointers and dates; implementation-order
  list struck out the shipped items; item 4 (SSGI) flagged as the
  next priority.
- `tests/test_sh_probe_grid.cpp` — new unit-test file (6 tests, 1889
  total). Covers the pure-math statics that had no coverage:
  `projectCubemapToSH` (uniform-colour/zero/clamped-HDR cases) and
  `convolveRadianceToIrradiance` (Ramamoorthi-Hanrahan 2001 cosine
  coefficients, and the combined pipeline). GPU upload/bind paths
  remain covered by the live scene-renderer capture path (need a GL
  context).

### 2026-04-19 post-launch: gitleaks CI + pre-commit, Dependabot

Closes two of the four post-launch maintenance items in
`docs/PRE_OPEN_SOURCE_AUDIT.md`:

- `.github/workflows/ci.yml` — new `secret-scan` job runs
  `gitleaks/gitleaks-action@v2` against the full git history on every
  push and PR. Honours the committed `.gitleaks.toml` allowlist
  (rotated-and-scrubbed NVD key, documented in SECURITY.md).
- `.pre-commit-config.yaml` — added `gitleaks@v8.30.1` hook so
  contributors' `pre-commit install` catches staged secrets before
  they ever reach a remote.
- `.github/dependabot.yml` — new, weekly cadence on
  `github-actions` and `pip` (audit tool) ecosystems, max 5 open PRs
  per ecosystem, Monday 06:00 UTC. Tracks CI action CVEs without
  depending on a human to remember to bump.

Local sweep verified clean: 255 commits, ~11.75 MB, 0 leaks.

### 2026-04-19 audit tool 2.13.0 — three detectors + copyright-header backfill

Ships three new tier-4 detectors that were deferred from audit 2.12.0
(they need multi-line windows or cross-file grep logic). All three
produce **zero findings** against the current Vestige tree after a
small copyright-header backfill in this same commit. 801+ audit-tool
unit tests pass (+35 new, +4 extra during FP tightening).

- **`file_read_no_gcount`** (tier 4, medium) — flags `stream.read(buf,
  N)` calls with no `.gcount() / .good() / .fail() / .eof()` check in
  the next N-line window. Also excludes `.read(` tokens inside
  double-quoted string literals so embedded Python snippets (e.g.
  `sys.stdin.read()` in `tests/test_async_driver.cpp`) don't false
  fire.
- **`dead_shader`** (tier 4, low) — flags `.glsl` files whose basename
  (or stem) does not appear as a substring anywhere in the source
  corpus. Substring-not-regex is deliberate to avoid the 2026-04-19
  `ssr.frag.glsl` FP caused by runtime-constructed shader paths.
- **`missing_copyright_header`** (tier 4, low) — per-file check that
  the first 3 lines (shebang-adjusted) contain a `Copyright (c) YEAR
  NAME` line and an `SPDX-License-Identifier` line. Covers `//`, `#`,
  `--` comment tokens.

Copyright backfill for the five files the new detector caught:

- `app/CMakeLists.txt`, `engine/CMakeLists.txt`, `tests/CMakeLists.txt`
- `engine/utils/json_size_cap.h`, `engine/utils/json_size_cap.cpp`

### 2026-04-19 manual audit — L41 clean-warning-flag sweep + include-cleaner pass

Closes the last deferred item from the 2026-04-19 audit backlog (L41,
``-Wformat=2 -Wshadow -Wnull-dereference -Wconversion -Wsign-conversion``)
and does an engine-wide unused-include pass. All 1883 tests pass; engine
and full build compile with **zero warnings, zero errors** under the
hardened warning set.

#### L41 — warning-flag sweep

The flags were already in ``engine/CMakeLists.txt`` (added by a prior
audit commit) but had been producing 633 warnings — mostly third-party
cascade from vendored GLM headers and ``-Wmissing-field-initializers``
noise in the visual-scripting node tables. Root cause fixed in the
three highest-leverage places, then the remaining 148 in-project
``-Wsign-conversion`` warnings cleaned file-by-file.

- ``engine/CMakeLists.txt`` — promoted ``glm-header-only`` to SYSTEM
  via ``set_target_properties(glm-header-only PROPERTIES SYSTEM TRUE)``
  (CMake 3.25+). Earlier ``target_include_directories(vestige_engine
  SYSTEM ...)`` was no-op because the original ``-I`` from
  ``glm-header-only``'s ``INTERFACE_INCLUDE_DIRECTORIES`` still preceded
  any re-export. Removed 346 cascaded GLM warnings.
- ``engine/scripting/node_type_registry.h`` — added ``= {}``
  default-member-initializers to ``NodeTypeDescriptor::inputIndexByName``
  and ``outputIndexByName``. Removed 136 ``-Wmissing-field-initializers``
  warnings from the six ``*_nodes.cpp`` aggregate registration sites
  without touching every call site.
- ``engine/physics/ragdoll.cpp:491`` — default-initialized
  ``glm::vec3/vec4/quat`` out-params before ``glm::decompose`` so the
  compiler can prove definite-assignment even though the function always
  overwrites; silences ``-Wmaybe-uninitialized``.
- ``engine/editor/panels/texture_viewer_panel.cpp:608`` — ``PbrRole
  role`` initialised to ``ALBEDO`` before a non-pass-through
  out-parameter helper that may leave it untouched when no suffix
  matches.
- ``engine/audio/audio_engine.h`` — ``std::vector<bool>
  m_sourceInUse`` → ``std::vector<uint8_t>`` to sidestep the
  specialised-bitvector proxy-reference weirdness and the GCC 15
  libstdc++ ``-Warray-bounds`` false positive in ``stl_bvector.h``
  ``resize()``. Corresponding ``assign(MAX_SOURCES, 0u)`` in the cpp.
- 26 source files touched for 148 ``-Wsign-conversion`` fixes —
  ``size_t`` loop aliases in hot paths (``particle_emitter.cpp``, etc.)
  and ``static_cast<size_t>/GLuint/GLsizeiptr/GLenum`` at call sites
  elsewhere. Hot loops (particle kill/update/spawn) use a local
  ``const size_t u = static_cast<size_t>(i);`` alias to keep indexing
  readable.

#### Include-cleaner pass

Ran ``clang-include-cleaner --disable-insert`` across all 224
``engine/*.cpp`` files. 82 unused includes removed from 78 files; 14
flagged removals were reverted as false positives (``<glm/gtc/matrix_transform.hpp>``
providing unqualified ``glm::lookAt/perspective/translate`` calls not
visible to the checker, ``<ft2build.h>`` needed for the
``FT_FREETYPE_H`` macro in ``font.cpp``). Report preserved at
``.unused_includes_report.txt`` for reproducibility.

#### clang-side fix

- ``editor/panels/model_viewer_panel.cpp:670-671`` — explicit
  ``static_cast<float>`` on the ``(int) / 10.0f`` truncate-to-one-decimal
  expression. Silences clang ``-Wimplicit-int-float-conversion`` while
  keeping the intentional truncation semantics.

### 2026-04-19 manual audit — low-item close-out + research update

Finishes the remaining Low-severity items from the 2026-04-19 audit
backlog and folds in a GDC 2026 / SIGGRAPH 2025 shader-research survey.
All 1883 tests pass.

#### Dead code / small correctness (L11, L13, L23, L24, L25, L30, L31, L33, L35, L39, L40)

- `FileWatcher::m_onChanged` (and its dispatch branch in ``rescan()``)
  deleted — the setter had already gone in L5 so every `if (m_onChanged)`
  site was unreachable.
- ``engine.cpp:1471,1478`` — Jolt ``BoxShape* new`` results marked
  ``const`` (cppcheck ``constVariablePointer``).
- ``editor.cpp`` — ``tonemapNames`` / ``aaNames`` moved into their
  branch scope (dead outside ``BeginMenu``). Three ``ImGuiIO&`` locals
  that never mutate the returned reference converted to
  ``const ImGuiIO&``.
- ``command_history.cpp`` — dirty-tracking arithmetic was not
  closed-form correct: undo-then-execute-new could land
  ``m_version == m_savedVersion`` even though the saved state lived on
  the discarded redo branch. Added explicit ``m_savedVersionLost``
  update on redo-branch discard and tightened the trim off-by-one. Two
  new regression tests (``DirtyAfterUndoThenNewExecute``,
  ``DirtyAfterDeepUndoThenNewExecute``) cover both paths.
- ``memory_tracker.cpp::recordFree`` — added compare-exchange loop that
  clamps at zero instead of letting a double-free or
  free-without-alloc wrap both atomics to ``SIZE_MAX``. Two regression
  tests added.
- ``pure_nodes.cpp::MathDiv`` — div-by-zero warning was firing every
  frame when a node graph fed a persistent zero. Rate-limited to the
  first occurrence per ``nodeId`` via a mutex-guarded
  ``std::unordered_set``.
- ``markdown_render.cpp`` — dead ``if (cells.empty())`` branch removed;
  ``splitTableRow`` always returns at least one cell.
- ``workbench.cpp:2189`` — inner ``VariableMap vars`` that shadowed the
  outer loop variable was rebuilt every iteration of a 100-sample
  loop. Reuse the main curve's map.
- ``workbench.cpp:249`` — ``static char csvPath[256]`` bumped to
  ``[4096]`` (PATH_MAX). 256 silently truncated deeply-nested paths.

#### DRY refactors (L12, L13, L14)

- New ``engine/renderer/ibl_prefilter.h`` — extracted the mip×face
  prefilter loop shared by ``EnvironmentMap`` and ``LightProbe`` into
  ``runIblPrefilterLoop()``. ~35 lines of identical code collapsed to
  a single call site in each class.
- New ``engine/utils/deterministic_lcg_rng.h`` — ``DeterministicLcgRng``
  class replaces the byte-for-byte duplicated LCG
  (``state * 1664525 + 1013904223``) in ``ClothSimulator`` and
  ``EnvironmentForces``. Preserves the exact output sequence for both
  callers.
- New ``engine/renderer/scoped_forward_z.h`` — RAII helper that saves
  the current clip/depth state, switches to forward-Z
  (``GL_NEGATIVE_ONE_TO_ONE`` + ``GL_LESS`` + ``clearDepth(1.0)``),
  and restores on destruction. Replaces four manual save/switch/restore
  triples in ``renderer.cpp`` (light-probe capture, SH-grid capture,
  directional CSM shadow pass, point-shadow pass) so a thrown
  exception or early-return never leaves the reverse-Z pipeline in a
  corrupt state.

#### Shader hardening (L15-L20)

- Added local ``safeNormalize(v, fallback)`` to ``scene.vert.glsl`` and
  ``scene.frag.glsl`` (``vec3 = dot-and-inversesqrt`` guarded by a
  ``1e-12`` length-squared floor). Applied to the TBN basis
  (``scene.vert.glsl:161-163``), shadow-bias ``lightDir`` in point
  shadow sampling (``scene.frag.glsl:448``), and camera view direction
  (``scene.frag.glsl:989``). Per Rule 11 the epsilon carries a ``TODO:
  revisit via Formula Workbench once reference data is available``
  comment.
- Added ``safeClipDivide(clip)`` to ``motion_vectors_object.frag.glsl``
  so a vertex on the camera plane (``w ≈ 0``) can't produce NaN motion
  vectors that later leak through TAA bilinear sampling.
- ``scene.vert.glsl`` morph-target loop now iterates
  ``min(u_morphTargetCount, MAX_MORPH_TARGETS)`` so a stale/corrupt
  uniform can never index past the 8-element ``u_morphWeights`` array.
- ``particle_simulate.comp.glsl`` gradient / curve loops likewise
  capped at compile-time ``MAX_COLOR_STOPS - 1`` / ``MAX_CURVE_KEYS -
  1``.

#### Roadmap update — GDC 2026 / SIGGRAPH 2025 research survey

Added ``ROADMAP.md`` § "2026-04 Research Update" under Phase 13 listing
newly identified techniques (spatiotemporal blue noise, SSILVB,
two-level BVH compute RT, hybrid SSR → RT fallback, physical camera
post stack) and priority hints for existing roadmap items (volumetric
froxel fog, FSR 2.x, sparse virtual shadow maps, GPU-driven MDI,
radiance cascades). Cites primary sources for each.

#### Phase 24 — Structural / Architectural Physics (design doc)

Draft design document for the attachment-physics phase:
``docs/PHASE24_STRUCTURAL_PHYSICS_DESIGN.md``. Cross-referenced from
``ROADMAP.md``. Covers:

- XPBD cloth particle ↔ Jolt rigid body kinematic attachment (pattern
  used by Chaos Cloth, Obi Cloth, PhysX FleX).
- Tagged-union tether constraint (particle / rigid body / static
  anchor endpoints) with one-sided distance-max XPBD projection.
- Slider-ring authoring on top of the existing
  ``Jolt::SliderConstraint`` wrapper.
- Editor attachment panel + vertex-picker gizmo.
- Full Tabernacle structural rigging spec: 48 boards + 5 bars/side +
  21 curtains + 2 coverings + veil + screen + 60 outer pillars +
  linen walls + tent-pegs-and-cords.
- Formula Workbench entries for every new tuning coefficient.

Rationale added to ``ROADMAP.md``: Phase 24 must land alongside the
rendering-realism pass, because photoreal curtains floating in mid air
are *worse* than the current lower-fidelity floating curtains.

### 2026-04-19 manual audit — Batch 4 delegated sweep (L2-L10, L21, L22)

Mechanical cleanup sourced from the 2026-04-19 audit report. Delegated
to a subagent so the main thread stayed focused on structural work.
All 1878 tests pass.

#### Dead public API (L2-L7) — 6 methods deleted after cross-repo grep

- `ResourceManager::loadTextureAsync` / `getAsyncPendingCount` /
  `getModelCount` — zero callers anywhere (engine, tests, tools, app).
- `FileWatcher::setOnFileChanged` / `getTrackedFileCount` — zero
  callers. ``m_onChanged`` is now permanently default-constructed;
  the dispatch branch in ``rescan()`` is unreachable and flagged for
  a future follow-up removal.
- `Benchmark::runDriverCaptured` — superseded by the W1 async-worker
  path (workbench 1.10.0). Doc references in `async_driver.*` and
  `SELF_LEARNING_ROADMAP.md` kept as historical context.

#### Dead shaders (L8-L10) — 4 of 5 deleted

- Deleted: ``bloom_blur.frag.glsl``, ``bloom_bright.frag.glsl``,
  ``basic.vert.glsl``, ``basic.frag.glsl``.
- **Kept**: ``ssr.frag.glsl`` — the audit entry was wrong; it's
  loaded at ``engine/renderer/renderer.cpp:397``. Flagged in the
  audit-tool improvements doc as a FP risk for detector #26
  (dead-shader grep).

#### L21 — `const Entity*` sweep (14 sites)

Converted non-mutating ``Entity*`` locals to ``const Entity*`` across
``editor.cpp`` (10 sites) and ``engine.cpp`` (4 sites). ~16 other
sites (``EntityFactory::createXxx``/``scene->createEntity`` results)
were skipped — those pointers are mutated immediately after creation.

#### L22 — `static` (10 functions)

Marked the listed member functions that never touch ``this`` as
``static``: ``AudioAnalyzer::computeFFT``, ``Window::pollEvents``,
``Editor::setupTheme``, ``FoliageManager::worldToGrid``,
``BVH::findBestSplit`` (was ``const``, now ``static``),
``Shader::compileShader``, three ``unbind()`` variants (``Mesh``,
``MeshPool``, ``DynamicMesh``), ``GPUParticleSystem::nextPowerOf2``,
``GPUParticleSystem::drawIndirect``.

### Editor launcher — CLI, wrapper, .desktop

Makes the editor discoverable to downstream users of the engine: a
stable ``vestige-editor`` entry point, a proper ``--help``, and a
Linux desktop-menu integration.

- **`app/main.cpp`** — reworked CLI parser. New flags: ``--editor``
  (explicit), ``--play`` (start in first-person mode with editor UI
  hidden), ``--scene PATH`` (load a saved scene instead of the demo),
  ``--assets PATH`` (override the asset directory), and ``-h``/
  ``--help`` (prints a full usage summary with examples). Unknown
  arguments produce a helpful error and exit code 2. The existing
  ``--visual-test`` and ``--isolate-feature`` flags are preserved.
- **`engine/core/engine.h` / `engine.cpp`** — ``EngineConfig`` gained
  ``startupScene`` and ``startInPlayMode``. ``Engine::initialize``
  calls ``SceneSerializer::loadScene`` after the built-in scene is
  set up (so a failed load falls back to the demo without crashing);
  paths are resolved against CWD first, then ``<assets>/scenes/``.
  When ``startInPlayMode`` is set the editor is flipped to
  ``EditorMode::PLAY`` and the cursor is captured at startup.
- **`packaging/vestige-editor.sh`** — thin bash wrapper that ``exec``s
  the sibling ``vestige`` binary. Lets desktop launchers reference a
  stable, obviously-named entry point without us having to ship two
  distinct binaries.
- **`packaging/vestige-editor.desktop`** — standard XDG desktop entry.
  Categories ``Graphics;3DGraphics;Development;Game``, MIME type
  ``application/x-vestige-scene`` (reserved for future scene-file
  registration), icon name ``vestige``.
- **`app/CMakeLists.txt`** — new ``editor_launcher`` custom target
  copies the wrapper + ``.desktop`` into ``build/bin/`` every build,
  and a Linux-only ``install()`` block places them at
  ``${prefix}/bin/vestige-editor`` and
  ``${prefix}/share/applications/vestige-editor.desktop``.
- **`README.md`** — new "Launching the editor" section with CLI
  examples and a controls table. Fixes the stale ``./build/Vestige``
  path (actual: ``./build/bin/vestige``).

### 2026-04-19 manual audit — batch 4/5 deferred fixes

Close-out pass over the Medium-severity items deferred from batch 1/2/3
(commit `676ab34`). All 1878 tests pass; one GTest case added for
``safePow`` emission plus new EXPECT_* assertions folded into three
existing cases (``HelpersMatchEvaluatorPrecisely``,
``CodegenGlslEmitsSafeDivAndHelpers``, ``GlslPreludeDefinesAllFourHelpers``).

#### Medium severity (7)

- **`engine/utils/json_size_cap.h` + `.cpp` (new)** — shared
  ``JsonSizeCap::loadJsonWithSizeCap`` + ``loadTextFileWithSizeCap``
  helpers. Replaces the hand-rolled ``ifstream + json::parse`` pattern
  at every JSON/text loader site listed below. Default 256 MB cap
  matches obj_loader / gltf_loader / scene_serializer. **(AUDIT M17–M26.)**
- **`engine/formula/formula_library.cpp`,
  `engine/formula/formula_preset.cpp`,
  `engine/utils/material_library.cpp`,
  `engine/editor/recent_files.cpp`,
  `engine/editor/prefab_system.cpp`,
  `engine/animation/lip_sync.cpp`** — routed all six JSON/text loaders
  through the new helpers. RecentFiles uses a 1 MB cap (tiny file);
  LipSync keeps an inline 16 MB cap (Rhubarb tracks). **(AUDIT M17–M23.)**
- **`engine/formula/lut_loader.cpp`** — hard 64 M-sample
  (``MAX_LUT_SAMPLES = 256 MB``) ceiling above the pre-existing
  SIZE_MAX / streamsize overflow guards. A 2000³-axis header would
  otherwise authorise an 8 GB float allocation. **(AUDIT M24.)**
- **`engine/renderer/shader.cpp`** — ``loadFromFiles`` / ``loadCompute``
  now go through ``loadTextFileWithSizeCap`` with an 8 MB shader-source
  ceiling. **(AUDIT M26.)**
- **`engine/renderer/skybox.cpp::loadEquirectangular`** — 512 MB
  equirect on-disk cap before handing off to stb_image; a hostile HDR
  header would otherwise drive stbi into multi-GB allocations.
  **(AUDIT M26.)**
- **`engine/editor/widgets/animation_curve.cpp::fromJson`** — 65 536
  keyframe ceiling on the ``push_back`` loop. A malicious ``.scene``
  carrying a 10M-element curve array used to allocate gigabytes here.
  **(AUDIT M26.)**
- **`engine/renderer/text_renderer.{h,cpp}`** — batched glyph upload.
  Both ``renderText2D`` and ``renderText3D`` now build one vertex array
  for the whole string, issue one ``glNamedBufferSubData`` + one
  ``glDrawArrays``, and truncate strings above
  ``MAX_GLYPHS_PER_CALL = 1024`` (≈ 96 KB vertex data). Previously the
  loop issued one upload + one draw per glyph. **(AUDIT M29.)**

#### Medium — Formula Pipeline (1)

- **`engine/formula/safe_math.h`,
  `engine/formula/expression_eval.cpp`,
  `engine/formula/codegen_cpp.cpp`,
  `engine/formula/codegen_glsl.cpp`** — new
  ``Vestige::SafeMath::safePow(base, exp)`` + matching GLSL prelude
  definition. Integer exponents pass through unchanged (``pow(-2, 3)
  = -8``); fractional exponents on negative bases project to 0 instead
  of returning NaN. All three evaluation paths (tree-walking
  evaluator, C++ codegen, GLSL codegen) now route ``pow`` through the
  shared helper so LM-fitter R² / AIC / BIC scores no longer diverge
  from the runtime. 7 new GTest cases; ``CodegenCpp.EmitBinaryOps``
  and ``CodegenGlsl.GenerateFunction`` updated for the new emission.
  **(AUDIT M11; CLAUDE.md Rule 11.)**

#### High severity (1)

- **`engine/renderer/renderer.cpp` (bloom FBO + 2× capture FBOs),
  `engine/renderer/light_probe.cpp::generateIrradiance`** — added
  ``glCheckNamedFramebufferStatus`` with a placeholder colour
  attachment at creation time for each of the 4 FBOs that previously
  had no completeness verification. Matches the pattern already used
  in ``cascaded_shadow_map.cpp``, ``environment_map.cpp``,
  ``framebuffer.cpp``, ``water_fbo.cpp``, ``text_renderer.cpp``.
  **(AUDIT M15.)**

#### Low severity (4, safe subset)

- **`engine/editor/panels/welcome_panel.cpp`** — dropped unused
  ``#include "core/logger.h"``. **(AUDIT L36.)**
- **`engine/formula/formula_preset.cpp::loadFromJson`** — renamed local
  ``count`` → ``loaded`` so it no longer shadows the
  ``FormulaPresetLibrary::count()`` member. **(AUDIT L37.)**
- **`engine/editor/panels/inspector_panel.cpp`** — removed the dead
  ``before = cfg;`` assignment; the variable goes out of scope at the
  following ``ImGui::TreePop()``. **(AUDIT L38.)**
- **`engine/core/engine.cpp`** — explicit ``default: break;`` on the
  keyboard-event switch (L28), and ``const Exclusion exclusions[]``
  for the foliage exclusion table (L26).

#### Housekeeping

- **`.gitignore`** — ignore ``/audit_rule_quality.json`` (raw
  per-rule-hit dump emitted by ``tools/audit/`` into the repo root).

### 2026-04-19 manual audit — batch 1/2/3 fixes

29 files touched, +490 / −170. All 1878 tests pass. Findings report in
`docs/AUDIT_2026-04-19.md` (gitignored per `docs/AUDIT_[0-9]*.md`).

#### High severity (12)

- **`engine/renderer/renderer.cpp` (per-object motion-vector overlay):**
  switched `glDepthFunc(GL_LESS)` → `GL_GREATER` so the pass writes
  under the engine's reverse-Z convention. The motion FBO is cleared
  with `glClearDepth(0.0)` (= far in reverse-Z); under `GL_LESS` no
  fragment ever passed, leaving TAA on camera-only motion for all
  dynamic objects. Fixes the 2026-04-13 visual regression flagged in
  the source comment at line 938.
- **`engine/utils/gltf_loader.cpp::readFloatAccessor`:** added
  `accessor.count > SIZE_MAX / componentsPerElement` overflow check
  before `result.resize(...)` — a malicious glTF could otherwise size
  the output to a tiny vector and have the subsequent `memcpy` walk
  off the end.
- **`engine/utils/entity_serializer.cpp` (6 texture-slot sites):** new
  file-scope `sanitizeAssetPath` rejects absolute paths and `..`
  components in scene-JSON-sourced texture paths (`diffuseTexture`,
  `normalMap`, `heightMap`, `metallicRoughnessTexture`,
  `emissiveTexture`, `aoTexture`). Scene loading no longer trusts
  untrusted paths directly to `ResourceManager::loadTexture`.
- **`engine/editor/scene_serializer.cpp`:** new static helper
  `openAndParseSceneJson` with a 256 MB file-size cap (matches
  obj_loader / gltf_loader). Replaces 4 separate `json::parse(file)`
  call sites that previously had no ceiling — a 10 GB `.scene` would
  OOM-kill the process.
- **`tools/audit/web/app.py::/api/detect`:** added `_is_safe_path(root)`
  403 guard that every sibling endpoint was already enforcing. The
  endpoint could previously be used to probe arbitrary filesystem
  directories via the web UI.
- **`engine/renderer/shader.h` + `.cpp`:** all `set*` setters now take
  `std::string_view` (was `const std::string&`). The uniform cache is
  now `std::map<std::string, GLint, std::less<>>` (transparent) so
  `const char*` / string-literal callers cost zero heap allocations on
  cache hits. Was ~250 temporary `std::string` allocations per frame
  per `renderScene` call, most ≥16 chars and therefore past libstdc++
  SSO.
- **`engine/renderer/renderer.cpp::drawMesh` (morph path):**
  pre-cached the `u_morphWeights[0..7]` uniform names as
  `static const std::array<std::string, 8>` so we don't rebuild the
  indexed name via `std::to_string` + concat on every morph-targeted
  draw.
- **`engine/renderer/renderer.cpp::renderScene` (MDI material
  grouping):** `m_materialGroups` now clears each inner vector
  (preserving capacity) instead of clearing the outer map — was
  destroying every per-material vector's buffer and re-allocating on
  every frame's `push_back` chain.
- **`engine/environment/foliage_manager.{h,cpp}`:** added out-param
  overloads of `getAllChunks` and `getVisibleChunks` so the shadow
  pass (up to 4 cascades per frame) and main foliage render reuse
  scratch vectors (`Renderer::m_scratchFoliageChunks`,
  `Engine::m_scratchVisibleChunks`) instead of allocating a fresh
  `std::vector<const FoliageChunk*>` per call.
- **`engine/renderer/renderer.cpp::captureIrradiance`:** deleted the
  unused `std::vector<float> facePixels(faceSize²·3)` — allocation was
  never read or written; only `cubemapData` at line 2042 was the
  actual read target. Also promoted `faceSize * faceSize * 3` to
  `size_t` arithmetic for overflow safety.
- **`engine/utils/gltf_loader.cpp::loadGltf` (POSITION read):** removed
  the unreachable `if (!hasPositions) continue;` defensive check; every
  path that fails to populate positions already `continue`s earlier.
- **`engine/editor/entity_factory.cpp::createParticlePreset`:** removed
  the dead `std::string entityName = "Particle Emitter"` initializer —
  every `if`/`else if`/`else` branch overwrote it, making the literal
  suggest a fallback that never activated.

#### Medium severity (15)

- **`tools/formula_workbench/async_driver.cpp`:** narrowed the
  PID-reuse TOCTOU race by clearing `m_childPid` to -1 *before*
  `waitpid()`. Linux only frees the PID on `waitpid`, so the stale-pid
  window is now zero inside normal cancel/poll flows. Full pidfd-based
  fix deferred to a future glibc 2.36+ upgrade.
- **`tools/formula_workbench/async_driver.cpp::start`:** try/catch
  around `std::thread` construction — on OOM-throw the orphaned child
  is now reaped via `SIGKILL` + `waitpid`, and pipe fds are closed.
  Previously leaked both on a vanishingly rare but possible failure.
- **`tools/formula_workbench/pysr_parser.cpp`:** added
  `MAX_PARSE_DEPTH = 256` via RAII `DepthGuard` in
  `parseAdd` / `parseUnary` / `parsePrimary`. Closes a stack-overflow
  DoS on deeply nested expressions (same pattern as CVE-2026-33902
  ImageMagick FX parser, CVE-2026-40324 Hot Chocolate GraphQL parser).
- **`tools/formula_workbench/pysr_parser.cpp`:** swapped `std::strtof`
  for `std::from_chars` — the former is locale-aware and would misparse
  `"1.5"` as `1` under a German locale. `from_chars` is locale-free
  (C++17, libstdc++ 11+).
- **`tools/formula_workbench/fit_history.cpp::toHex64`:** format string
  `%016lx` + `unsigned long` cast silently truncated the high 32 bits
  of a `uint64_t` on Windows (LLP64 — `unsigned long` is 32-bit).
  Swapped to `%016llx` + `unsigned long long`.
- **`engine/editor/entity_actions.cpp` (align + distribute):** two
  use-after-move bugs — `entries.size()` was read *after*
  `std::move(entries)` on the preceding line, so "N entities" always
  logged as 0. Captured `size_t count = entries.size()` before the
  move.
- **`engine/animation/motion_database.cpp::getFrameInfo / getPose`:**
  added empty-database guards — `std::clamp(x, 0, size()-1)` is UB
  when the database is empty (hi < lo). Now returns a static empty
  `FrameInfo` / `SkeletonPose` in that case.
- **`engine/renderer/renderer.{h,cpp}`:** stored the
  `WindowResizeEvent` subscription token and unsubscribed it in
  `~Renderer`. Engine owns both `Renderer` and `EventBus`, and
  `~Renderer` runs first — a resize event published during teardown
  would previously call into a half-destroyed `Renderer`.
- **`engine/editor/tools/ruler_tool.h::isActive`:** now includes the
  `MEASURED` state. `processClick` still consumes clicks in `MEASURED`
  (restarts the measurement); callers that gated viewport-click
  routing on `isActive()` were double-routing those clicks.
- **`engine/utils/gltf_loader.cpp::resolveUri`:** path-prefix check now
  appends a separator before comparing, so `base=/assets/foo` no
  longer accepts `/assets/foo_evil/x.png`.
- **`engine/environment/terrain.cpp::loadHeightmap` / `loadSplatmap`:**
  added `file.gcount()` checks after `file.read(...)`. A truncated
  terrain file was previously leaving `m_heightData` / `m_splatData`
  with partial fresh + partial stale contents.
- **`tools/audit/web/app.py::/api/config` (GET):** extension gate now
  restricts to `.yaml` / `.yml` (mirroring the PUT sibling). Was
  previously able to read any file inside allowed roots.
- **`engine/core/first_person_controller.cpp::applyDeadzone`:** added
  `std::isfinite(v)` + `std::clamp(v, -1.0f, 1.0f)` on gamepad axis
  input. A faulty HID report / driver bug could produce NaN or
  out-of-range values that propagated into camera rotation.
- **`engine/renderer/foliage_renderer.cpp`:** hoisted the
  `m_visibleByType[typeId]` map lookup out of the per-instance loop.
  Thousands of `unordered_map::operator[]` hash probes per frame
  become tens.
- **`engine/renderer/renderer.h::m_frameArena`:** added `{}`
  value-initialization to silence the recurring cppcheck
  `uninitMemberVar` — the pmr arena overwrites this storage anyway,
  but cppcheck needed an explicit initializer to stop re-flagging it
  every audit run.

#### Low (4)

- **`VERSION`**: synced `0.1.4 → 0.1.5` to match CMakeLists.txt (drift
  introduced in commit `200d75f`; `scripts/check_changelog_pair.sh`
  expects these to match).
- **`engine/renderer/renderer.cpp`**: removed dead
  `setVec2("u_texelSize", ...)` call — the matching shader uniform
  was never declared in `motion_vectors.frag.glsl`, so the set was a
  silent no-op.
- **`app/main.cpp`**, **`tools/formula_workbench/main.cpp`**: added the
  standard `// Copyright (c) 2026 Anthony Schemel` + SPDX-License
  headers that every other `.cpp`/`.h` in the repo carries.
- **`engine/formula/node_graph.cpp`**: collapsed the redundant
  `else if (abs|sqrt|negate)` branch into the final `else` — both
  already assigned `MATH_ADVANCED` (clang-tidy
  `bugprone-branch-clone`).

### Documentation

- **`ROADMAP.md`**: 6 sections updated from the GDC 2026 /
  SIGGRAPH 2025 research pass. WishGI SH-fit lightmaps, Brixelizer SDF
  GI (primary RDNA2-feasible software-RT path), HypeHype + MegaLights
  stochastic lighting split, GPU-driven MDI + Hi-Z flagged as the
  highest-ROI OpenGL 4.5 item, Slang language unification added,
  partitioned TLAS annotated with the VK_KHR watch note, tonemapping
  policy (ACES 1.3 default, 2.0 opt-in), accessibility section
  expanded with 4 new items. References linked to slide PDFs.
- **`SECURITY.md`**: added CVE-2026-23213 (AMD amdgpu kernel SMU-reset
  flaw — RDNA2/RDNA3) and Mesa 26.0.x regression notes. New Linux
  support matrix: minimum kernel 6.9+, minimum Mesa 26.0.4.
- **`.claude/settings.json`**: added a read-only permission allowlist
  (15 entries — cmake/ctest/make/cppcheck/clang-tidy plus
  MCP filesystem read tools) to reduce per-turn prompts during audit
  sessions.

## [0.1.5] - 2026-04-18

### Fixed — completes 2026-04-16 strict-aliasing sweep

One actionable finding from the 2026-04-18 full audit (5,149 raw
findings, 1 actionable = 0.02% post-triage). Closes out the morph-
target sites that were missed in engine 0.1.4 (commit `1f6fd24`).

- **`engine/utils/gltf_loader.cpp` (lines 770, 790, 810): strict-
  aliasing UB in morph-target delta loading.** The 0.1.4 sweep fixed
  the matching pattern in `nav_mesh_builder.cpp` but left three
  identical sites in the glTF morph-target POSITION/NORMAL/TANGENT
  loops unpatched. glTF `byteStride` is not required to preserve
  4-byte alignment, and `reinterpret_cast<const float*>` on an
  `unsigned char*` is a strict-aliasing violation regardless of
  alignment — `-O2` is free to reorder or elide the loads. Replaced
  each cast with `float fp[3]; std::memcpy(fp, data + stride*i,
  sizeof(fp));` — same AMD64 codegen, portable under strict-aliasing.
  cppcheck: `invalidPointerCast` (portability) × 3.

### Tooling

- **`.gitleaksignore`**: added `docs/AUTOMATED_AUDIT_REPORT_*` so
  gitleaks stops re-emitting 3,500+ false-positive `generic-api-key`
  hits on every audit. The hits are our own audit tool's JSON
  `results` sidecars — rule IDs and short hashes tripping the
  generic-API-key regex. No real secrets in repo.

## [0.1.4] - 2026-04-17

### Fixed — cppcheck audit cycle

Eight actionable cppcheck findings from the 2026-04-16 audit run (1
portability bug, 7 performance hits) against a noise baseline of ~300
raw findings. Triage kept local per `AUDIT_STANDARDS.md`.

- **`engine/navigation/nav_mesh_builder.cpp`: strict-aliasing UB in
  scene-geometry collection.** `reinterpret_cast<const float*>` on a
  `uint8_t*` VBO buffer violated the strict-aliasing rule; `-O2` is
  free to reorder or elide such loads, so the UB was latent rather
  than visibly buggy. Switched to `std::memcpy` into a local
  `float[3]` — the standard-blessed way to reinterpret bytes as a
  different trivially-copyable type. Compiles to the same load on
  AMD64; portable under strict-aliasing. cppcheck:
  `invalidPointerCast` (portability).

- **`engine/formula/lut_generator.cpp`: redundant map lookup in
  default-variable insertion.** `vars.find(name) == end()` followed
  by `vars[name] = default` is now `vars.try_emplace(name, default)`
  — one traversal instead of two. cppcheck: `stlFindInsert`.

- **`engine/formula/node_graph.cpp`: redundant set probe in
  cycle-detection frontier.** `visited.count(target) == 0` followed
  by `visited.insert(target)` is now
  `if (visited.insert(target).second)` — `insert` returns
  `{iter, inserted}`, so a single call replaces the count-then-insert
  pair. cppcheck: `stlFindInsert`.

- **`engine/utils/cube_loader.cpp` + `tests/test_color_grading.cpp`:
  `line.find(x) == 0` → `line.rfind(x, 0) == 0`.** The
  `rfind(x, 0)` overload only searches at position 0 so it
  short-circuits as soon as the prefix matches or fails; the
  `find(x) == 0` form scans the whole string before reporting the
  position. C++17-compatible equivalent of `starts_with()` (which is
  C++20). Seven call sites updated. cppcheck: `stlIfStrFind`.

## [0.1.3] - 2026-04-15

### Changed — launch-prep: `VESTIGE_FETCH_ASSETS` default → OFF

- **Default changed** in `external/CMakeLists.txt`: fresh clones no
  longer attempt to pull the `milnet01/VestigeAssets` CC0 asset pack.
  The sibling repo stays private until ~v1.0.0 pending a final
  redistributability audit of every 4K texture and `.blend.zip`
  archive. The engine's demo scene renders correctly against the
  in-engine 2K CC0 set shipped in `assets/` (Poly Haven plank /
  brick / red_brick, glTF sample models, Arimo font) — no asset
  download is required. Maintainers with access to the private
  sibling repo can opt in with `-DVESTIGE_FETCH_ASSETS=ON`.

- Public docs updated to reflect the new default and the
  private-assets-repo status: `README.md`, `ASSET_LICENSES.md`,
  `SECURITY.md`, `THIRD_PARTY_NOTICES.md`, `ROADMAP.md`. CI comments
  in `.github/workflows/ci.yml` rewritten: the `-DVESTIGE_FETCH_ASSETS=OFF`
  flag is now an explicit default-match (testing the fresh-public-clone
  path), not a temporary stopgap.

- Launch-sweep script (`scripts/final_launch_sweep.sh`) end-of-run
  message updated: no "remove the flag" step, single-repo flip only.

When VestigeAssets later goes public the flip is a single commit
that sets the default back to `ON`, drops the explicit flag from
CI, and re-links the sibling repo in `README.md`.

### Changed — launch-prep: Timer → `std::chrono::steady_clock`

- **`engine/core/timer.cpp` no longer depends on GLFW.** Switched the
  time source from `glfwGetTime()` to `std::chrono::steady_clock` via a
  private `elapsedSecondsSince(origin)` helper. Public API is unchanged
  — callers still see the same `update()` / `getDeltaTime()` /
  `getFps()` / `getElapsedTime()` semantics. The only observable
  behavioural difference is `getElapsedTime()`'s epoch: previously
  "seconds since GLFW init", now "seconds since Timer construction".
  The sole non-test caller (wind animation in `engine::render`) only
  uses rate-of-change, so the epoch shift is invisible.

- **`tests/test_timer.cpp` is now a pure unit test.** Removed
  `glfwInit()` / `glfwTerminate()` from `SetUp`/`TearDown`. Added two
  new tests: `ElapsedTimeAdvancesWithWallClock` (verifies monotonic
  advance across a 10 ms `sleep_for`) and `FrameRateCapRoundTrip`
  (verifies the uncapped/capped/uncapped setter round-trip).

- **Root cause for the test-suite flakiness surfaced by
  `scripts/final_launch_sweep.sh`.** `glfwInit()` pulled in
  libfontconfig / libglib global caches, which LeakSanitizer flagged
  as an 88-byte leak at process exit under parallel `ctest -j nproc`.
  The test logic itself always passed, but the LSan leak tripped the
  harness's exit code — giving flaky 1-3 test failures across
  back-to-back sweep runs, which in turn cascaded into launch-sweep
  "regressions" (`tests_failed` contributes to the audit HIGH count).
  Removing the GLFW init removes the whole libglib/libfontconfig
  lifecycle from `vestige_tests` (it was the only test that called
  `glfwInit`). Five consecutive parallel runs now pass 100%.

## [0.1.2] - 2026-04-13

### Fixed — §H18 + §H19 divergent SH grid radiosity bake

Post-audit follow-up. User reported every textured surface looked
like an emissive light after the §H14 SH basis correction landed;
bisection via the new `--isolate-feature` CLI flag localised it to
the IBL diffuse path, specifically the SH probe-grid contribution.
Two real bugs, neither addressed by §H14 / §M14:

- **§H19 SH grid irradiance was missing the /π conversion** — the
  *real* cause. `evaluateSHGridIrradiance` returns Ramamoorthi-Hanrahan
  irradiance E (`∫ L(ω) cos(θ) dω`); the diffuse-IBL formula at the
  call site is `kD * irradiance * albedo`, which assumes the
  *pre-divided* value E/π that LearnOpenGL's pre-filtered irradiance
  cubemap stores (PI is multiplied in during the convolution, then
  implicitly divided back out via `(1/nrSamples) * PI`). Without the
  /π division, the SH grid path produced a diffuse contribution
  π × the correct value, so the radiosity transfer factor became
  `π × albedo`. For any albedo ≥ 1/π ≈ 0.318 — i.e. all common
  materials — that's > 1, and the multi-bounce bake series diverged
  instead of converging. Observed energy growth ~1.7× per bounce
  matched `π × scene-average-albedo ≈ π × 0.54` exactly. Fix: divide
  the SH evaluation result by π so it matches the cubemap convention.
  Bake now converges geometrically (Tabernacle scene: 5.47 → 6.16 →
  6.49, deltas 0.69 → 0.33).

- **§H18 skybox vertex shader was Z-convention-blind** — masked the
  §H19 bug below the surface. The shader hard-coded
  `gl_Position.z = 0`, which is the far plane in reverse-Z (main
  render path) but the *middle* of the depth buffer in forward-Z
  (capture passes used by `captureLightProbe` and `captureSHGrid`).
  Without this fix, the §M14 workaround had to gate the skybox out
  of capture passes entirely, leaving the SH probe-grid bake without
  any sky direct contribution and forcing it to feed off pure
  inter-geometry bounce — the exact configuration where §H19's
  missing /π factor blew up. The shader now reads `u_skyboxFarDepth`
  and emits `z = u_skyboxFarDepth * w`, so z/w = u_skyboxFarDepth
  after the perspective divide. The renderer sets the uniform per
  pass: 0 for reverse-Z main render, 0.99999 for forward-Z capture
  (close-but-not-equal-to-1.0 so GL_LESS still passes against the
  cleared far buffer). The §M14 `&& !geometryOnly` gate is removed
  since the skybox now draws correctly in both Z conventions. Sky
  direct light is back in the SH grid bake.

- **Diagnostic CLI flag `--isolate-feature=NAME`** retained for
  future regression bisection. Recognised values: `motion-overlay`,
  `bloom`, `ssao`, `ibl`, `ibl-diffuse`, `ibl-specular`, `sh-grid`.
  Each disables one specific renderer feature so a `--visual-test`
  run's frame reports can be diff-mechanically compared against a
  baseline to identify the offending subsystem. Used to find
  §H18+§H19 in 5 short visual-test passes — without it the bisection
  would have required either reverting commits or interactive shader
  editing.

## [0.1.1] - 2026-04-13

### Fixed — §H17 SystemRegistry destruction lifetime

Post-audit follow-up to the 0.1.0 audit cycle. The §H16 fix
(gated `SaveSettings` during `ed::DestroyEditor`) closed one
shutdown SEGV but a second, independent SEGV remained, surfacing as
ASan "SEGV on unknown address (PC == address)" + nested-bug abort
immediately after the "Engine shutdown complete" log line.

- **§H17 SystemRegistry destruction lifetime**: root cause was
  structural, not the §H16 ImGui-node-editor race:
  `SystemRegistry::shutdownAll()` called each system's `shutdown()`
  but left the `unique_ptr<ISystem>` entries in the vector. The
  systems' destructors therefore ran during `~Engine` member
  cleanup — *after* `m_renderer.reset()` and `m_window.reset()` had
  already destroyed the renderer and torn down the GL context — so
  any system dtor that touched a cached Renderer*/Window* or freed a
  GL handle dereferenced freed memory or called a dead driver
  function pointer. New `SystemRegistry::clear()` destroys the
  systems in reverse registration order; `Engine::shutdown()` calls
  it immediately after `shutdownAll()` so destruction happens while
  shared infrastructure is still alive. Closes the §H16
  runtime-verification deferral — §H16 (ed::DestroyEditor
  SaveSettings race) was correct as far as it went; §H17 was the
  second, independent shutdown path that masked the §H16 fix's
  success. Six new unit tests in `tests/test_system_registry.cpp`
  pin the contract: destructors run in reverse order inside
  `clear()`, the registry empties, `clear()` is idempotent, and the
  canonical `shutdownAll()` → `clear()` sequence produces the
  expected eight-event log.

## [0.1.0] - 2026-04-13

Initial changelog entry. Prior history captured in `ROADMAP.md` Phase
notes and `docs/PHASE*.md` design documents.

Subsystems in place as of this release:
- Core (engine/window/input/event-bus/system-registry)
- Renderer (OpenGL 4.5 PBR forward, IBL, TAA, SSAO, bloom, shadows, SH probe grid)
- Animation (skeleton, IK, morph, motion matching, lip sync)
- Physics (rigid body, constraints, character controller, cloth)
- Scripting (Phase 9E visual scripting, 60+ node types)
- Formula (template library, Levenberg-Marquardt curve fitter, codegen)
- Editor (ImGui dock-based; Phase 9E-3 node editor panel in progress)
- Scene / Resource / Navigation / Profiler / UI / Audio

### Security — audit cycle
- Flask web UI of the audit tool hardened against path-traversal and shell-injection (affects local-dev setups that ran the web UI only; no public deployment). Details in `tools/audit/CHANGELOG.md` v2.0.1–2.0.6.
- **Formula codegen injection hardened** (AUDIT.md §H11). `ExprNode::variable/binaryOp/unaryOp` factories + `fromJson` now validate identifiers against `[A-Za-z_][A-Za-z0-9_]*` and operators against an allowlist. Codegen (C++ + GLSL) throws on unknown op instead of raw-splicing. A crafted preset JSON like `{"var": "x); system(\"rm -rf /\"); float y("}` is now rejected at load time, well before any generated header is compiled.

### Fixed — audit cycle

**Scripting (§H5–§H9, §M5–§M8)**
- **§H5 UB `&ref == 0` guards**: `Engine& m_engine` → `Engine*`. `reinterpret_cast<uintptr_t>(&m_engine) == 0` was undefined behavior that `-O2` could fold to false, crashing release builds on paths that rely on the guard.
- **§H6 Blackboard `fromJson` cap bypass**: `fromJson` now routes through `set()`, enforcing `MAX_KEYS = 1024` and `MAX_STRING_BYTES = 256`.
- **§H7/§H8 pure-node memoization opt-out**: `NodeTypeDescriptor::memoizable` flag; `GetVariable`, `FindEntityByName`, `HasVariable` are marked non-memoizable so loop bodies see fresh reads after `SetVariable`. `WhileLoop.Condition` now works — previously froze at its first value.
- **§H9 latent-action generation token**: `ScriptInstance::generation()` bumps on every `initialize()`; Timeline onTick lambdas capture and validate, dropping stale callbacks across the editor test-play cycle instead of dereferencing nodeIds from a rebuilt graph.
- **§M5** `ScriptGraph::addConnection` now `clampString`s pin names (matches the on-disk load path).
- **§M6** `isPathTraversalSafe` rejects absolute paths, tilde paths, and empty strings, not just `..` components.
- **§M7** `subscribeEventNodes` warns on unknown `eventTypeName` (known-not-yet-wired types exempted), surfacing typos that used to produce silent non-firing nodes.
- **§M8** Quat JSON order documented as `[w, x, y, z]` on both serializer and deserializer.

**Formula pipeline (§H12, §H13, §M9, §M10, §M12)**
- **§H12 Evaluator↔codegen safe-math parity**: new `engine/formula/safe_math.h` centralises `safeDiv`, `safeSqrt`, `safeLog`. Evaluator, C++ codegen, and GLSL codegen (via a prelude) all share the same semantics, so the LM fitter's coefficients no longer validate against one set of math and ship a different one.
- **§H13 curve-fitter non-finite residuals** (already landed in `d007349`): LM bails on NaN/Inf initial residuals with an explanatory message; rejects non-finite trial steps; accumulators are `double`.
- **§H14 SH basis constant** (already landed in `553277d`): `assets/shaders/scene.frag.glsl:553` changed from `c3 = 0.743125` to `c1 = 0.429043` on the L_22·(x²−y²) band-2 term (Ramamoorthi-Hanrahan Eq. 13). Removes a ~1.73× over-weight that tilted chromatic response on indoor ambient bakes.
- **§M9** `NodeGraph::fromJson` throws on duplicate node IDs; `m_nextNodeId` is recomputed as `max(id)+1` regardless of the serialised counter.
- **§M10** `fromExpressionTree` CONDITIONAL now logs a warning on the import-time logic loss (collapse to `literal(0)` with orphaned branch sub-trees). Root-cause fix tracked in ROADMAP.md §Phase 9E "Deferred".
- **§M12** `ExprNode::toJson` guards against null children so malformed in-memory trees emit a null placeholder instead of crashing the save path.

**Renderer + shaders (§H15, §M13–§M18, §L4, §L5)**
- **§H15 per-object motion vectors**: new overlay pass after the full-screen camera-motion pass. New shaders `motion_vectors_object.{vert,frag}.glsl` take per-draw `u_model` / `u_prevModel` matrices; Renderer tracks `m_prevWorldMatrices` keyed by entity id. TAA reprojection on dynamic / animated objects now reproduces their real motion instead of ghosting. TAA motion vector FBO gained a depth attachment so the overlay depth-tests against its own geometry.
- **§M13** BRDF LUT left column (NdotV=0) clamped to 1e-4 at entry so the LUT is Fresnel-peaked, not black. Fixes dark rim on rough dielectrics under IBL.
- **§M14** Skybox pass explicitly gated on `!geometryOnly` + hardening comment. Light-probe / SH-grid capture paths use forward-Z; skybox's `gl_FragDepth = 0` would have passed GL_LESS if the gate ever went away.
- **§M15** Bloom bright-extraction epsilon raised to 1e-2 and output clamped to `vec3(256.0)`. Saturated-hue pixels no longer amplify up to 10000× into the blur chain.
- **§M16/§M18** SSR and contact-shadow normals use four-tap central differences with gradient-disagreement rejection. No more silhouette halos at depth discontinuities.
- **§M17** Point-shadow slope-scaled bias scaled by `farPlane` so the same shader behaves correctly at Tabernacle-scale (~5m) and outdoor-scale (~100m) farPlanes — no more Peter-Panning indoors.
- **§L4** SH probe grid 3D-sampler fallbacks (units 17–23) rebound after both `captureLightProbe` and `captureSHGrid`. Prevents stale 3D texture reads on subsequent captures.
- **§L5** TAA final resolve `max(result, 0.0)` — cheap NaN clamp before history accumulation.

**Editor (§H16, §M1–§M4)**
- **§H16** imgui-node-editor shutdown SEGV root-caused. `NodeEditorWidget` routes `Config::SaveSettings`/`LoadSettings` through free-function callbacks gated on an `m_isShuttingDown` flag so `ed::DestroyEditor` no longer runs a save that dereferences freed ImGui state. Canvas layout persistence re-enabled at `~/.config/vestige/NodeEditor.json`.
- **§M1** `ScriptEditorPanel::makePinId` widened from 16-bit to 32-bit nodeId and 31-bit pinIndex. Eliminates collisions on generated/procedural graphs. Static-asserts 64-bit uintptr_t.
- **§M2** `ScriptEditorPanel::open(path)` preserves the existing graph on load failure (matches the header contract).
- **§M3** `NodeTypeDescriptor::inputIndexByName`/`outputIndexByName` populated at `registerNode` time; editor renders connections in O(1) instead of linear scans per frame.
- **§M4** Unknown pin names → skip connection + `Logger::warning` instead of silent "draw to pin 0".

**Misc (§L1–§L9 tail)**
- **§L1** `ScriptingSystem::initialize/shutdown` idempotency contract documented on the header.
- **§L2** Formula Workbench file-dialog `popen` now reads full output (was truncated at 512 bytes).
- **§L3** Curve fitter RMSE accumulators already moved to `double` in §H13 — noted in the CHANGELOG for audit completeness.
- **§L8** `NodeEditor.json` added to `.gitignore` (runtime layout artifact).
- **§L9** Audit tool `--keep-snapshots N` flag; `docs/trend_snapshot_*.json` gitignored.

### Added — infrastructure
- **§I1** GitHub Actions CI (`.github/workflows/ci.yml`): Linux Debug+Release matrix, build + ctest under xvfb, separate audit-tool Tier 1 job.
- **§I2** `.clang-format` at repo root mirroring CODING_STANDARDS.md §3 (Allman braces, 4-space indent, 120-column limit, pointer-left alignment). Not a hard gate — opportunistic enforcement.
- **§I3** Repo-level `VERSION` (0.1.0) + this `CHANGELOG.md` (already in place).
- **§I4** `.pre-commit-config.yaml` + `scripts/check_changelog_pair.sh`. Local git hook that fails commits touching `tools/audit/`, `tools/formula_workbench/`, or `engine/` without also touching the respective `CHANGELOG.md` (and `VERSION` if present). Bypassable with `--no-verify` for trivial fixes.

### Deferred to ROADMAP
Three known gaps documented in ROADMAP.md:
- Per-object motion vectors via MRT (eliminates the overlay pass; enables skinned/morphed motion) — Phase 10 rendering enhancements.
- `NodeGraph` CONDITIONAL node type (preserves `ExprNode` conditional round-trip) — Phase 9E visual scripting.
- imgui-node-editor shutdown SEGV visual confirmation — Phase 9E-3 step-4 acceptance (code-level race is closed; needs one editor launch to verify).
