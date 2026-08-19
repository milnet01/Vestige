# Vestige

**A modern C++17 / OpenGL 4.5 3D engine, focused on first-person exploration
and (eventually) game development.**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

### In plain English

Vestige is a program for **walking around 3D spaces on your computer** —
you move with the keyboard and mouse, in first person, the way you would
in a video game. It was built to explore reconstructions of historical
buildings (the Tabernacle and Solomon's Temple were the first targets),
and it has grown into a general-purpose toolkit for building 3D scenes.

It comes in one piece: an **editor** for arranging a scene, and a
**walkthrough mode** for stepping inside it. You do not need to write any
code to look around — see [Just want to look around?](#just-want-to-look-around)
below.

If you are a developer: it is a solo-maintained, open-source 3D engine
built around a subsystem + event-bus architecture, prioritising a clear
codebase, a sustainable single-maintainer cadence, and a hard **60 FPS
minimum** on mid-range hardware (reference: AMD RX 6600 / Ryzen 5 5600).

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

**Short version:** the engine works and you can walk around in it today.
The parts that make a scene look and sound right — lighting, shadows,
audio, weather, vegetation — are built. What is still missing is most of
the *game*-making layer: networking, multiplayer, and a shipping-game
settings menu.

In roadmap terms, core subsystems are complete through **Phase 9**
(domain-driven systems + visual scripting, including the Phase 9E-3
node-graph editor). **Phase 10** is in active, multi-track development:
localization, accessibility, settings, UI/HUD and audio have shipped, and
recent work has concentrated on rendering (volumetric fog, screen-space GI,
GPU grass), multi-threading, and performance on weaker hardware. See
[`ROADMAP.md`](ROADMAP.md) for the detailed phase plan and current slice
numbering.

| Area                     | Status        | Notes                                                                 |
|--------------------------|---------------|-----------------------------------------------------------------------|
| Window / context / input | Complete      | GLFW, OpenGL 4.5, keyboard/mouse/gamepad                              |
| Scene graph / ECS        | Complete      | Entity + component system, parent-child transforms                    |
| Rendering (forward + PBR)| Complete      | PBR materials, IBL, POM, SSAO, bloom, tone mapping                    |
| Shadows                  | Complete      | Cascaded shadow maps + point / spot shadows                           |
| Global illumination      | Core complete | Baked SH probe grid + radiosity, plus real-time screen-space GI (SSGI) with froxel colour injection. World-space dynamic GI is next (`docs/research/gi_roadmap.md`) |
| Editor (Phase 5)         | Complete      | Dockable ImGui editor, gizmos, undo/redo, console                     |
| Particles / effects      | Complete      | GPU-instanced particle system                                         |
| Animation                | Core complete | Skeletal animation, IK, state machines, morph targets live; motion matching / facial / lip-sync implemented, production wiring tracked in Phase 10.9 |
| Physics                  | Core complete | Jolt rigid bodies, character controller, joints, XPBD cloth (CPU + GPU) live; ragdoll / fracture / grab implemented, wiring tracked in Phase 10.9 |
| Audio                    | Complete      | OpenAL Soft 3D audio + streaming music, geometry-driven occlusion (sound is muffled by real walls), convolution reverb with baked per-room acoustics, procedural footsteps/impacts, loudness normalisation |
| UI / HUD                 | Complete      | Menus, HUD, notifications, theme tokens, in-game screen state machine, interaction prompts |
| Navigation / pathfinding | Foundation    | Recast navmesh + Detour A*; AI state / editor tooling in follow-ons   |
| Localization             | Complete      | UTF-8 text, multi-language string table, Hebrew / Greek / Latin with RTL, in-settings language picker |
| Accessibility            | Complete      | Colour-blind modes, subtitles, remappable input bindings, photosensitive safety, reduce-motion |
| Visual scripting         | Complete      | Phase 9E-1/9E-2/9E-3 shipped; node-graph editor (imgui-node-editor) with 70 registered node types |
| Formula Pipeline         | Complete      | Expression trees, workbench, C++/GLSL codegen, quality tiers          |
| Terrain                  | Core complete | Heightmap terrain with PBR ground texturing; chunking + streaming pending |
| Vegetation               | Core complete | GPU-generated grass (Bézier blades, ~1M blades, casts shadows), tree LOD/impostors; procedural plant growth planned |
| Atmosphere / fog          | Complete      | Distance + height fog, sun inscatter, screen-space god rays, and volumetric (froxel) fog on the High preset |
| Performance scaling       | Core complete | Low/Medium/High/Ultra quality presets, render-scale, FXAA + CAS, and a perf-regression gate. Weak-hardware tiers still being widened |
| AI assistance in editor  | Planned       | Phase 23 — design doc pending                                         |
| Structural / attachments | Planned       | Phase 24 — design doc drafted                                         |
| Networking / multiplayer | Planned       | Phase 20                                                              |
| Vulkan backend           | Planned       | Long-term                                                             |

---

## Just want to look around?

**You do not need to build anything.** Every
[GitHub Release](https://github.com/milnet01/Vestige/releases) has ready-to-run
downloads attached. Grab the one for your system, then:

| Your system | What to download | How to run it |
|---|---|---|
| **Windows 10/11 (64-bit)** | `vestige-<version>-windows-x86_64.zip` | unzip it anywhere, then run `vestige.exe` (or `vestige-editor.cmd`) |
| **Linux (easiest)** | `Vestige-<version>-x86_64.AppImage` | right-click → Properties → tick "allow executing as a program", then double-click it |
| **Linux (plain archive)** | `vestige-<version>-linux-x86_64.tar.gz` | extract it, then run `./vestige-editor` from inside the extracted folder |

Both are 64-bit Intel/AMD builds. ARM machines (including Raspberry Pi and
Windows-on-ARM) are not built yet.

**Windows will warn you the first time.** The build is not code-signed — a
certificate costs money this pre-1.0 project has not spent — so SmartScreen
shows "Windows protected your PC". Click *More info* → *Run anyway*.
[`TESTING.md`](TESTING.md) documents this.

Then press **Esc** to drop into the walkthrough and move with **W A S D** and
the mouse. Press **Esc** again to come back out to the editor. The
[Controls](#controls) table lists the rest.

**If it will not start**, the usual cause is a graphics driver older than
OpenGL 4.5. Updating your graphics driver fixes almost every case. If it
still will not run, please [open an issue](https://github.com/milnet01/Vestige/issues)
and say what machine and graphics card you have — those reports are genuinely
useful, see [Testing wanted](#testing-wanted).

macOS is **not** supported: Apple caps OpenGL at 4.1 and Vestige needs 4.5.

---

## Quick start

*(This section is for building from source. If you only want to run it, use
[Just want to look around?](#just-want-to-look-around) above.)*

### Requirements

- **Compiler:** GCC 9+, Clang 10+, or MSVC 2019+ (C++17)
- **CMake:** 3.21 or newer
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

### Launching the editor

Vestige is a single binary that hosts both the editor and the
first-person runtime. After a build, run it directly from the build
tree:

```bash
./build/bin/vestige-editor          # editor, demo scene (default)
./build/bin/vestige-editor --play   # first-person walkthrough
./build/bin/vestige-editor --scene path/to/my.scene
./build/bin/vestige-editor --help   # full CLI reference
```

`vestige-editor` is a thin wrapper around the `vestige` binary; you
can invoke either. The wrapper exists so desktop launchers and
menu shortcuts have a stable, obviously-editor-named entry point.

A Linux `.desktop` entry ships in `packaging/vestige-editor.desktop`
and is installed to `${prefix}/share/applications/` when you run
`cmake --install build` — menu integration then picks it up
automatically. Icons are pulled from the system theme by name
(`vestige`), so providing one is optional until we ship a branded
asset.

### Controls

| Key              | Action                                             |
|------------------|----------------------------------------------------|
| **Esc**          | Toggle between editor and first-person play mode   |
| **WASD** + mouse | Move (play mode); Shift to sprint                  |
| **F1**           | Toggle wireframe                                   |
| **F2**           | Cycle tonemapper (Reinhard → ACES → linear clamp)  |
| **F3**           | HDR false-color luminance overlay                  |
| **F4**           | Toggle Parallax Occlusion Mapping                  |
| **[** / **]**    | Decrease / increase manual exposure                |
| **F10**          | Toggle performance profiler overlay                |
| **F11**          | Capture screenshot + frame diagnostics             |
| **F12**          | Toggle fullscreen                                  |

Gamepad input (Xbox / PlayStation) is supported via GLFW.

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
- **Performance-regression gate** (`tools/perf_gate.py`) — compares a
  profiler CSV (captured with the engine's `--profile-log[=PATH]`) to a
  committed baseline and flags per-pass slowdowns. Run it on real GPU
  hardware (timings are meaningless on CI's software renderer):
  **No baseline is committed** — GPU timings are meaningless across
  different hardware, so the first step on any machine is to record your
  own, then compare later runs against it:
  ```bash
  # 1. record a reference run on YOUR hardware (do this once)
  ./build/bin/vestige --visual-test --profile-log=baseline-run.csv
  python3 tools/perf_gate.py --update-baseline --current baseline-run.csv \
      --out my-baseline.json --hardware "RX 6600"

  # 2. after a change, capture again and check it against that baseline
  ./build/bin/vestige --visual-test --profile-log=run.csv
  python3 tools/perf_gate.py --baseline my-baseline.json --current run.csv
  ```
  Discard the first capture of a session — it runs cold and reads slow.
  A self-test over committed fixtures (`tests/fixtures/perf_gate/`) runs
  as part of `ctest`, so the gate itself is covered even on CI's software
  renderer.

---

## Contributing

Short version: discuss in an issue before big changes, follow the
coding standards, add tests, run the audit tool, sign your commits
(`git commit -s`). Full details in
[`CONTRIBUTING.md`](CONTRIBUTING.md).

AI-assisted contributions are welcome and must be disclosed in the PR
description. "The AI wrote it" is never an excuse for an unreviewed
change — the committer owns the outcome.

## Testing wanted

**You don't need to write C++ to help.** The maintainer develops on
AMD RDNA2 / Linux, so reports from NVIDIA, Intel, older AMD, Windows,
and non-openSUSE Linux are especially valuable. Pre-built binaries are
attached to every [GitHub Release](https://github.com/milnet01/Vestige/releases);
what to try and how to report lives in
[`TESTING.md`](TESTING.md).

## Reporting security issues

Do not open a public issue. See [`SECURITY.md`](SECURITY.md) *§
Vulnerability disclosure* for the private disclosure process.

---

## License

MIT © 2026 Anthony Schemel. See [`LICENSE`](LICENSE).

Shipped dependencies and assets carry their own licenses; see
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) and
[`ASSET_LICENSES.md`](ASSET_LICENSES.md).
