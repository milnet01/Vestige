# Vestige Engine Changelog

All notable engine-level changes are documented here. Per-tool changelogs
live alongside each tool (`tools/audit/CHANGELOG.md`,
`tools/formula_workbench/CHANGELOG.md`).

The engine version tracks phase milestones, not SEMVER. Pre-1.0 commits
may change any interface without notice.

## [Unreleased]

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
