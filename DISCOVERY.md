# Discovery — Full Manual Audit 2026-04-13

**Scope:** Entire Vestige repository, manual pass (no audit-tool shortcut).
**Auditor:** Claude (Opus 4.6, 1M context), acting as senior reviewer.
**Baseline commit:** `01079ac` (main) + uncommitted Phase 9E-3 Step 4 WIP.

---

## 1. Project map

### Languages & toolchain
- **C++17** (primary) — `set(CMAKE_CXX_STANDARD 17)` in root `CMakeLists.txt`
- **C** (secondary, vendored dependencies only — glad, stb, dr_libs)
- **Python 3** (audit tool at `tools/audit/`, ~120 pytest tests)
- **GLSL** (75 shaders in `assets/shaders/`)

### Build system
- **CMake ≥ 3.20**, single root `CMakeLists.txt` calls `add_subdirectory` into `external/`, `engine/`, `app/`, `tools/`, `tests/`
- Build types: Debug (default) / Release via `CMAKE_BUILD_TYPE`
- Options: `VESTIGE_BUILD_TESTS=ON` (default), `VESTIGE_BUILD_TOOLS=ON` (default)
- `compile_commands.json` exported; symlinked at repo root → `build/compile_commands.json`

### Executables produced
| Binary | Size | Path |
|---|---|---|
| `vestige` | 209 MB (debug) | `build/bin/vestige` |
| `vestige_tests` | 359 MB (debug) | `build/bin/vestige_tests` |
| `formula_workbench` | 46 MB (debug) | `build/bin/formula_workbench` |

### Entry points
- **Engine app:** `app/main.cpp` → `engine/core/engine.{cpp,h}`
- **Workbench:** `tools/formula_workbench/main.cpp` → `workbench.{cpp,h}`
- **Tests:** GoogleTest, single combined binary (`vestige_tests`) — 1738 tests registered via `ctest -N`

### External dependencies
**Fetched at build (ignored in git):**
- GLFW, GLM, GoogleTest, Assimp (via `external/CMakeLists.txt` + CMake FetchContent)
- ImGui 1.92.8 (docking branch), imgui-node-editor v0.9.3 (Phase 9E-3)

**Vendored in-tree:**
- `external/glad/` (OpenGL loader)
- `external/stb/` (stb_image, stb_image_write)
- `external/dr_libs/` (dr_wav, dr_mp3 for audio)

### Target runtime
- OpenGL 4.5 core profile
- Linux (openSUSE Tumbleweed dev, Ubuntu tested), Windows (supported but not currently exercised)
- Input: keyboard / mouse / Xbox + PS5 controllers via GLFW

### Data stores & external services
- **None at runtime.** Pure local filesystem for scenes (JSON via nlohmann::json), assets (glTF / OBJ / HDR textures), and scripts.
- Audit tool optionally queries NVD API (CVE lookups) and DuckDuckGo (research) — offline-capable via `--no-research`.

### Secret handling
- No credentials committed. No `.env` files in tree.
- NVD API key stored outside repo per memory note (pending activation ~2026-04-18).

---

## 2. Engine subsystems (LOC counts)

| Subsystem | Files | LOC (cpp+h) | Notes |
|---|---:|---:|---|
| `editor/` | 96 | 19,998 | Largest. Contains panels, widgets, commands, tools. Phase 9E-3 Step 4 WIP lives here. |
| `renderer/` | 76 | 18,556 | PBR, post-processing, IBL, shadows, particles, UI renderer. |
| `physics/` | 46 | 9,471 | RigidBody, constraints, character controller, cloth. |
| `animation/` | 46 | 8,012 | Skeleton, curves, state machines, IK, morph targets, lip sync, motion matching. |
| `formula/` | 32 | 6,822 | Formula library, curve fitter, codegen, node graph. Consumed by workbench. |
| `scripting/` | 32 | 5,643 | Phase 9E visual scripting. 60 node types, EventBus bridge. |
| `core/` | 19 | 5,515 | Engine, Window, Input, Logger, EventBus, SystemRegistry, FirstPersonController. |
| `utils/` | 17 | 4,510 | Memory tracker, async, string utils. |
| `environment/` | 15 | 4,256 | Terrain, water, atmosphere, vegetation. |
| `scene/` | 25 | 3,767 | Entity/component, scene graph, cameras, particle emitters. |
| `resource/` | 8 | 1,317 | Resource manager, async texture loader. |
| `systems/` | 24 | 1,122 | Thin wrappers per system (atmosphere_system, water_system, etc.) |
| `audio/` | 6 | 783 | Audio clips + playback. |
| `ui/` | 13 | 762 | UI render primitives (pre-ImGui). |
| `navigation/` | 7 | 759 | Pathfinding stubs. |
| `profiler/` | 8 | 667 | CPU/GPU profiler. |
| `testing/` | 2 | 334 | Test scaffolding. |
| **Engine total** | **472** | **92,494** | |
| Full repo (per latest audit report) | 629 | 105,198 | Includes tests/, tools/, app/. |

---

## 3. Standards, tooling, and hygiene

### In-tree standards docs
- `CLAUDE.md` — project rules (research before impl, coding standards, audit after every phase)
- `CODING_STANDARDS.md` — naming, formatting, file layout
- `ARCHITECTURE.md` — subsystem + event bus design (27 KB)
- `AUDIT_STANDARDS.md` — 5-tier audit process (18 KB)
- `SECURITY.md` — 16 KB, memory safety + input validation guidelines
- `ROADMAP.md` — 112 KB, phase history + plans

### Linting / static analysis wired to the audit tool
- **cppcheck** — Tier 1, runs via audit tool
- **clang-tidy** — Tier 1, runs via audit tool
- **lizard** — Tier 4 cyclomatic complexity (optional pip)
- **Pattern scans** — Tier 2 regex library (language-specific presets)

### What's NOT wired up
- **No pre-commit hooks** (no `.pre-commit-config.yaml`, no `.husky/`)
- **No CI** (no `.github/`, no `.gitlab-ci.yml`, no `Jenkinsfile`)
- **No formatter config** (no `.clang-format` at root — verify in audit Phase 2)
- **No dedicated SAST beyond cppcheck+clang-tidy** (audit tool's Tier 2 regex patterns fill part of this gap)

### Suppression files present
- `asan_suppressions.txt` — GLFW/GLX + fontconfig leaks (upstream, not Vestige)
- `.audit_suppress` — not verified in Phase 1 (check in Phase 2)

### Version tracking
- **No engine-level VERSION file.** Engine version lives in `CMakeLists.txt` (project VERSION 0.1.0).
- `tools/audit/CHANGELOG.md` → v2.0.0 (2026-04-11)
- `tools/formula_workbench/CHANGELOG.md` → v1.3.0 (2026-04-11)
- `WORKBENCH_VERSION` constant in `workbench.h`
- Per `CLAUDE.md` memory, CHANGELOG + VERSION are mandatory-same-commit for audit tool changes.

---

## 4. Baseline state (automated audit, 2026-04-13 09:14:04)

From `docs/AUTOMATED_AUDIT_REPORT_2026-04-13_091404.md`:
- **Build:** clean (0 warnings, 0 errors)
- **Tests:** 1695 passed, 0 failed (at audit time; `ctest -N` now shows 1738 — delta from Step-3 additions)
- **Findings:** 0 critical, 49 high, 4652 medium, 764 low, 112 info (total 5577)
- **Trend:** worsening (+420 findings since prior audit)
- **Duration:** 401 s

### Known pre-existing HIGH cppcheck findings (will re-verify in Phase 2)
| File:Line | Rule | Status |
|---|---|---|
| `engine/formula/node_graph.cpp:495` | `containerOutOfBounds` | Flagged as real bug in `IMPROVEMENTS_FROM_9E_AUDIT.md §8` |
| `engine/scene/entity.cpp:60` | `returnDanglingLifetime` | Re-verify |
| `engine/scene/entity.cpp:79` | `returnDanglingLifetime` | Re-verify |
| `tests/test_command_history.cpp:244–246` | `containerOutOfBounds` | Likely test-local false positive — re-verify |

### clang-tidy noise
- 4443 medium findings — most are style (modernize-*, readability-*) per `IMPROVEMENTS_FROM_9E_AUDIT.md §1a`. The signal-to-noise is the known pain point.

---

## 5. Git state

### Branch: `main`

### Uncommitted (Phase 9E-3 Step 4 WIP — `NodeEditorWidget` + `ScriptEditorPanel`)
```
 M engine/CMakeLists.txt        (+3 lines)
 M engine/editor/editor.cpp     (+20 lines)
 M engine/editor/editor.h       (+5 lines)
?? NodeEditor.json              (imgui-node-editor runtime layout; not in .gitignore)
?? engine/editor/panels/script_editor_panel.cpp
?? engine/editor/panels/script_editor_panel.h
?? engine/editor/widgets/node_editor_widget.cpp
?? engine/editor/widgets/node_editor_widget.h
```

**Potential Phase 1 finding:** `NodeEditor.json` is a runtime-generated layout file (analog of `imgui.ini`, which IS gitignored) — candidate for gitignore entry.

### Recent history (`git log --oneline`)
```
01079ac Phase 9E-3 step 3: M9 type cache, M11 pure memo, entry-pin field
0e8ff69 Phase 9E audit + 9E-3 prep (lib integration + pin interning)
4928acb Phase 9E-2: Visual scripting EventBus bridge + 60 node types
4799b66 Phase 9E-1: Visual scripting core infrastructure
2574829 Audit tool v2.0.0, Formula Workbench v1.3.0, engine fixes and improvements
f07af24 Full codebase audit: security hardening, bug fixes, dead code cleanup
```

### Tracked audit artifacts
- `docs/trend_snapshot_*.json` — 5 snapshots from 2026-04-11 and 2026-04-13 ARE tracked in git
- `docs/AUTOMATED_AUDIT_REPORT*.md` and `*_results.json` — properly ignored
- Per `.gitignore`: `docs/AUTOMATED_AUDIT_REPORT*.md` line 71

**Potential Phase 1 finding:** trend snapshots may be intentional for cross-run history, but 5 snapshots across 2 days suggests unpruned state — audit tool could gain `--keep-snapshots N` to limit growth.

---

## 6. Known pre-existing improvements documents

Prior audit findings that are not-yet-implemented should be respected (not re-flagged as new):

- `tools/audit/IMPROVEMENTS_FROM_9E_AUDIT.md` — 30 items from Phase 9E audit, ordered by priority. **Reference but do not duplicate.**
- `AUDIT_TOOL_AND_WORKBENCH_IMPROVEMENTS.md` — **this audit's tool-improvement tracking file**, freshly created.

---

## 7. Phase 2 scope (proposed)

With scope confirmed as "everything," Phase 2 will cover all `AUDIT_STANDARDS.md` categories against:

1. **Engine source** (`engine/**/*.{cpp,h}`)
2. **Tests** (`tests/*.cpp`)
3. **Shaders** (`assets/shaders/*.glsl`)
4. **App** (`app/main.cpp`)
5. **Audit tool** (`tools/audit/**/*.py`)
6. **Formula Workbench** (`tools/formula_workbench/*.{cpp,h}`)
7. **Build system** (CMake files across the tree)
8. **Repo hygiene** (`.gitignore`, tracked artifacts, large files)
9. **Docs accuracy vs. code** (sampled, not exhaustive)

**Excluded:**
- Third-party vendored code (`external/glad/`, `external/stb/`, `external/dr_libs/`) — upstream, not ours
- `build/`, `.git/`, `.audit_cache/`, `.cache/`, `logs/`, `Testing/`
- `.claude/worktrees/` — transient agent scratch space

**Manual verification plan:**
- Each HIGH/CRITICAL finding will be cited with `file:line` + quoted snippet
- Where relevant, reproduce with a test, asan/ubsan pass, or targeted grep
- Each manual check that could be automated goes into `AUDIT_TOOL_AND_WORKBENCH_IMPROVEMENTS.md`
- Formula Workbench exercised for at least one fit round-trip during Phase 2

---

## 8. Stop — confirm scope before Phase 2

Please confirm:

1. **Phase 2 scope** matches §7 above (entire tree minus vendored/build).
2. **Pre-existing audit debt handling:** flag findings already in `IMPROVEMENTS_FROM_9E_AUDIT.md` as "known — cross-reference only", do not re-surface as new.
3. **NodeEditor.json and trend snapshots** are minor git-hygiene items worth including in Phase 2; want them in or split out?
4. **Runtime exercise:** OK to run `./build/bin/vestige` briefly during Phase 2 to verify editor panels + Step 4 WIP, and `./build/bin/formula_workbench` for at least one curve-fit round-trip? (No code changes — observation only.)

On your go-ahead I'll begin Phase 2 (AUDIT.md).
