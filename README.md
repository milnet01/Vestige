# Vestige

**A modern C++17 / OpenGL 4.5 3D engine, focused on first-person exploration
and (eventually) game development.**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

Vestige is a solo-maintained, open-source 3D engine built around a
subsystem + event-bus architecture. It prioritises a clear codebase, a
sustainable single-maintainer cadence, and a hard **60 FPS minimum** on
mid-range hardware (reference: AMD RX 6600 / Ryzen 5 5600).

> **Project status — early-stage.** API is unstable until 1.0. Expect
> breaking changes between 0.x minor versions. Contributions and issues
> are welcome; response time is variable (one pass per week is the
> intended cadence). See [`ROADMAP.md`](ROADMAP.md) for current direction
> and `docs/` for design notes.

---

## What Vestige is

- A **C++17 engine** targeting Linux and Windows, with OpenGL 4.5 today and
  Vulkan on the long-term roadmap.
- Built for **first-person architectural walkthroughs** first (biblical
  structures such as the Tabernacle and Solomon's Temple were the initial
  target), with a general-purpose feature set that extends to game
  development as the roadmap matures.
- **Opinionated:** Allman-brace C++, `snake_case.cpp` / `PascalCase`
  types / `m_camelCase` members, one class per file, tests mandatory for
  new features, and a strict "no workarounds without root-cause
  investigation" rule. See [`CODING_STANDARDS.md`](CODING_STANDARDS.md)
  and [`CLAUDE.md`](CLAUDE.md).
- **Transparently AI-assisted:** developed in collaboration with
  Anthropic's Claude Code. Contributors must disclose AI assistance in
  PRs but are otherwise unrestricted. See
  [`CONTRIBUTING.md`](CONTRIBUTING.md#ai-assisted-contributions-welcome-must-be-disclosed).

## What Vestige is not (yet)

- Not a game engine in the Unity/Unreal sense — no scripting language,
  no editor store, no asset marketplace.
- Not production-stable — APIs shift regularly during 0.x.
- Not Vulkan-backed yet — the renderer is OpenGL 4.5.
- Not a closed / dual-licensed product. Vestige is MIT and will not be
  relicensed. See [`ROADMAP.md`](ROADMAP.md) *§ Open-Source Release*.

---

## Feature status

Core subsystems are complete through **Phase 8** (see
[`ROADMAP.md`](ROADMAP.md) for the detailed phase plan):

| Area                     | Status        | Notes                                               |
|--------------------------|---------------|-----------------------------------------------------|
| Window / context / input | Complete      | GLFW, OpenGL 4.5, keyboard/mouse/gamepad            |
| Scene graph / ECS        | Complete      | Entity + component system, parent-child transforms  |
| Rendering (forward + PBR)| Complete      | PBR materials, IBL, POM, SSAO, bloom, tone mapping  |
| Shadows                  | Complete      | Cascaded shadow maps + point / spot shadows         |
| Global illumination      | Partial       | SH probe grid + radiosity; see `docs/GI_ROADMAP.md` |
| Editor (Phase 5)         | Complete      | Dockable ImGui editor, gizmos, undo/redo, console   |
| Particles / effects      | Complete      | GPU-instanced particle system                       |
| Animation                | Complete      | Skeletal animation, glTF-driven                     |
| Physics                  | Complete      | Jolt Physics + cloth (XPBD)                         |
| Terrain                  | Core complete | Chunking + streaming pending                        |
| Scripting / gameplay     | Planned       | Phases 9, 11, 16                                    |
| Networking / multiplayer | Planned       | Phase 20                                            |
| Vulkan backend           | Planned       | Long-term                                           |

---

## Quick start

### Requirements

- **Compiler:** GCC 9+, Clang 10+, or MSVC 2019+ (C++17)
- **CMake:** 3.20 or newer
- **GPU:** OpenGL 4.5-capable driver
- **Linux system packages:** `libgl1-mesa-dev`, `xorg-dev`, ALSA dev
  headers (for OpenAL Soft). On Debian/Ubuntu:

  ```bash
  sudo apt install build-essential cmake ninja-build pkg-config \
      libgl1-mesa-dev libglu1-mesa-dev \
      libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
      libxxf86vm-dev libwayland-dev libxkbcommon-dev
  ```

- **Windows:** Visual Studio 2019+ with the C++ workload.

### Build

```bash
git clone https://github.com/milnet01/Vestige.git
cd Vestige
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The first configure pulls third-party dependencies via CMake
`FetchContent` (GLFW, GLM, ImGui, Jolt, OpenAL Soft, FreeType, and
others — see [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)). Plan
for several minutes on a cold build; warm builds are incremental.

The engine ships with a small set of CC0 demo assets in `assets/`
(Poly Haven 2K textures, glTF sample models, Arimo font) — no extra
asset download is required for the demo scene. A larger separate
pack of 4K CC0 textures and blend files will land in a sibling repo
(`milnet01/VestigeAssets`) closer to the v1.0.0 release, once every
asset has been re-audited for full redistributability. Until then
`VESTIGE_FETCH_ASSETS` in `external/CMakeLists.txt` defaults to OFF
and can be ignored.

### Run the tests

```bash
ctest --test-dir build --output-on-failure
```

### Run the demo scene

```bash
./build/Vestige
```

Controls: **WASD** + mouse to move, **Shift** to sprint, **Esc** to
release the cursor. Gamepad input (Xbox / PlayStation) is supported via
GLFW.

---

## Repository layout

```
engine/            Engine source (subsystems, each under its own dir)
tests/             Google Test suites, one per subsystem
tools/             audit/, formula_workbench/ (standalone tooling)
assets/            Shipped shaders, default font, sample models, demo textures
external/          Third-party sources (vendored) + FetchContent glue
docs/              Design notes, research documents, automated audit reports
.github/workflows/ CI definitions
```

---

## Documentation

- [`ROADMAP.md`](ROADMAP.md) — phased development plan and open-source
  release policy
- [`ARCHITECTURE.md`](ARCHITECTURE.md) — subsystem + event-bus design
- [`CODING_STANDARDS.md`](CODING_STANDARDS.md) — C++ naming, formatting,
  structural rules
- [`SECURITY.md`](SECURITY.md) — engine security standards and the
  vulnerability disclosure process
- [`CONTRIBUTING.md`](CONTRIBUTING.md) — how to participate, DCO
  sign-off, AI-assistance disclosure
- [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md) — Contributor Covenant 2.1
- [`ASSET_LICENSES.md`](ASSET_LICENSES.md) — per-asset licensing and
  attribution
- [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) — dependency
  licensing and attribution
- [`CHANGELOG.md`](CHANGELOG.md) — engine changelog
- `docs/` — design and research documents per subsystem

## Tooling

- **Audit tool** (`tools/audit/`) — static analysis, pattern scans, CVE
  lookups, subsystem coverage. Runs in CI at Tier 1. Use locally before
  opening a PR:
  ```bash
  python3 tools/audit/audit.py -t 1 2 3     # quick pass
  python3 tools/audit/audit.py               # full audit
  ```
- **Formula Workbench** (`tools/formula_workbench/`) — interactive
  notebook for authoring, fitting, and exporting numerical formulas
  (physics curves, lighting attenuation, animation easings) as JSON
  the engine loads at asset time. Per project rules, numerical design
  goes through the Workbench rather than hand-coded magic constants.

---

## Contributing

Short version: discuss in an issue before big changes, follow the
coding standards, add tests, run the audit tool, sign your commits
(`git commit -s`). Full details in
[`CONTRIBUTING.md`](CONTRIBUTING.md).

AI-assisted contributions are welcome and must be disclosed in the PR
description. "The AI wrote it" is never an excuse for an unreviewed
change — the committer owns the outcome.

## Reporting security issues

Do not open a public issue. See [`SECURITY.md`](SECURITY.md) *§
Vulnerability disclosure* for the private disclosure process.

---

## License

MIT © 2026 Anthony Schemel. See [`LICENSE`](LICENSE).

Shipped dependencies and assets carry their own licenses; see
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) and
[`ASSET_LICENSES.md`](ASSET_LICENSES.md).
