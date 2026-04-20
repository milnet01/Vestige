# Vestige Engine Changelog

All notable engine-level changes are documented here. Per-tool changelogs
live alongside each tool (`tools/audit/CHANGELOG.md`,
`tools/formula_workbench/CHANGELOG.md`).

The engine version tracks phase milestones, not SEMVER. Pre-1.0 commits
may change any interface without notice.

## [Unreleased]

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
