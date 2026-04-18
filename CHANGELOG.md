# Vestige Engine Changelog

All notable engine-level changes are documented here. Per-tool changelogs
live alongside each tool (`tools/audit/CHANGELOG.md`,
`tools/formula_workbench/CHANGELOG.md`).

The engine version tracks phase milestones, not SEMVER. Pre-1.0 commits
may change any interface without notice.

## [Unreleased]

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
