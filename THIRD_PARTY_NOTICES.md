# Third-Party Notices

Vestige is licensed under the MIT License (see [LICENSE](LICENSE)). This file
lists the third-party software and assets shipped with or pulled into the
build, along with their licenses and source URLs. Update this file whenever a
dependency is added, removed, or version-bumped.

For asset attributions (textures, models, fonts), see [ASSET_LICENSES.md](ASSET_LICENSES.md).

---

## Dependencies fetched at configure time (`external/CMakeLists.txt`)

These are pulled in via CMake `FetchContent` and built from source. Each
`GIT_TAG` / version is pinned in `external/CMakeLists.txt`. Update both
this file and the audit tool's NVD dependency list when bumping a pin.
Most rows below pin a release tag; four deps (Dear ImGui, ImGuizmo,
imgui-filebrowser, ImPlot) pin an exact commit on a branch because upstream
publishes no suitable release tag — see "Branch-commit pins" below.
`nlohmann/json` is fetched via URL tarball, not git tag.

| Dependency       | Version                                         | License                                           | Source |
|------------------|-------------------------------------------------|---------------------------------------------------|--------|
| GLFW             | 3.4                                             | zlib                                              | <https://github.com/glfw/glfw> |
| GLM              | 1.0.1                                           | MIT (modified)                                    | <https://github.com/g-truc/glm> |
| Dear ImGui       | commit `4cb21e4a` (docking branch, see below)   | MIT                                               | <https://github.com/ocornut/imgui> |
| ImGuizmo         | commit `a15acd87` (master snapshot)             | MIT                                               | <https://github.com/CedricGuillemet/ImGuizmo> |
| imgui-filebrowser| commit `47a18845` (master branch, see below)    | MIT                                               | <https://github.com/AirGuanZ/imgui-filebrowser> |
| imgui-node-editor| v0.9.3                                          | MIT                                               | <https://github.com/thedmd/imgui-node-editor> |
| ImPlot           | commit `1351ab2c` (master branch, see below)    | MIT                                               | <https://github.com/epezent/implot> |
| FreeType         | VER-2-14-3                                      | FreeType Project License (BSD-style) — chosen over GPLv2 | <https://github.com/freetype/freetype> (official mirror; build fetches this) |
| tinyexr          | v1.0.9                                          | BSD 3-Clause                                      | <https://github.com/syoyo/tinyexr> |
| tinygltf         | v2.9.4                                          | MIT                                               | <https://github.com/syoyo/tinygltf> |
| nlohmann/json    | v3.12.0 (URL tarball; see `external/CMakeLists.txt`)     | MIT                                      | <https://github.com/nlohmann/json> |
| Jolt Physics     | v5.3.0                                          | MIT                                               | <https://github.com/jrouwe/JoltPhysics> |
| OpenAL Soft      | 1.25.1                                          | LGPL v2.1 (dynamic linking)                       | <https://github.com/kcat/openal-soft> |
| libebur128       | v1.2.6                                          | MIT (© Jan Kokemüller; static link)               | <https://github.com/jiixyj/libebur128> |
| Recast Navigation| v1.6.0                                          | zlib                                              | <https://github.com/recastnavigation/recastnavigation> |
| enkiTS           | v1.11                                           | zlib (© Doug Binks; static link)                  | <https://github.com/dougbinks/enkiTS> |
| GoogleTest       | v1.15.2                                         | BSD 3-Clause                                      | <https://github.com/google/googletest> |

### Branch-commit pins

Four deps pin an **exact commit on a branch** rather than a release tag,
because upstream publishes no suitable tag. These are reproducible pins
(byte-stable builds), not moving-branch references — the distinction
SECURITY.md §5 ("Pin versions — no latest") cares about. Per project
rule 8 (`CLAUDE.md`), each non-tag pin carries a written reason:

- **Dear ImGui — `4cb21e4a` (docking branch, 2026-05-28):** the docking
  branch is the only place multi-viewport docking lives; `master` does not
  have it yet, and the docking branch carries no release tags. Re-pin to a
  newer docking commit deliberately; move to a tag once docking lands on
  `master`.
- **ImGuizmo — `a15acd87` (2025-12-27):** pinned below the latest master to
  avoid a GCC 14 compile break introduced upstream (see
  `external/CMakeLists.txt` for the full reason). Upstream ships no release
  tags, so this is a Category-(B) branch-commit pin — tracked here, not the
  `DEPENDENCY_STANDARDS.md` Breaking-Version Registry — even though the trigger
  is a build break. Re-evaluate when upstream fixes the `vec_t` constructor.
- **imgui-filebrowser — `47a18845` (master, 2025-09-24):** upstream publishes
  no tags. Bump deliberately at audit-cycle entry.
- **ImPlot — `1351ab2c` (master, 2026-05-10):** upstream's last tagged release
  (v0.16) predates the ImGui 1.92.x API Vestige builds against. Bump
  deliberately; move to a tag once upstream re-tags above v0.16.

### Notes on the LGPL dependency

OpenAL Soft is LGPL v2.1, which is **compatible with MIT distribution
when linked dynamically**. The Vestige build links OpenAL dynamically
(default CMake target) so the engine's MIT licensing is preserved.
Static linking would propagate LGPL constraints to consumers; do not
change the link mode without a license review.

---

## Vendored sources (`external/`)

These are committed to the repository directly (single-header or
small-payload libraries that don't warrant a `FetchContent` pull).

| Library     | Path             | License            | Source |
|-------------|------------------|--------------------|--------|
| glad (OpenGL loader) | `external/glad/` | MIT (generator) + Public Domain (output) | <https://github.com/Dav1dde/glad> |
| stb (stb_image, stb_image_write, etc.) | `external/stb/` | MIT or Public Domain (dual) | <https://github.com/nothings/stb> |
| dr_libs (dr_wav, dr_flac, dr_mp3) | `external/dr_libs/` | MIT-0 or Public Domain (dual) | <https://github.com/mackron/dr_libs> |
| tl::expected (v1.3.1) | `external/tl_expected/tl/expected.hpp` | CC0 1.0 (public domain) | <https://github.com/TartanLlama/expected> |
| tinyfiledialogs (v3.21.3) | `external/tinyfiledialogs/` | Zlib | <https://sourceforge.net/projects/tinyfiledialogs/> |

Each vendored source carries its own license header in the file. See
the individual files for the canonical license text.

**Why `tl::expected` is vendored:** the engine compiles at the C++17
baseline (`CMAKE_CXX_STANDARD 17`), but `std::expected` is a C++23 *library*
feature — `<expected>` is unreachable under `-std=c++17`. `engine/utils/result.h`
aliases `Result<T, E>` to `std::expected` when a C++23 toolchain is detected,
and to this vendored `tl::expected` otherwise. `tl::expected` is the reference
implementation `std::expected` was standardised from, so the fallback matches
the standard's semantics; the alias switches to the standard type with no
caller changes once the project moves to C++23. Pinned to the latest release
(v1.3.1) per the dependency-currency rule in `CLAUDE.md`.

---

## Asset attributions

Per-asset licenses, sources, and attributions live in
[ASSET_LICENSES.md](ASSET_LICENSES.md). The required attributions for
shipped assets that demand them under their license terms are
reproduced here so this file is self-contained for downstream
licence-compliance scanning.

### glTF sample models — `assets/models/`

Source: <https://github.com/KhronosGroup/glTF-Sample-Assets>

- **`CesiumMan.glb`** — © 2017 Cesium. Licensed under
  **CC-BY 4.0 International with Trademark Limitations**. The Cesium
  trademark/logo is non-copyrightable but separately reserved.
- **`Fox.glb`** — composite work under three licenses:
  - Mesh (PixelMannen): **CC0 1.0 Universal** (no attribution required)
  - Rigging & animation (tomkranis): **CC-BY 4.0 International**
  - glTF conversion (AsoboStudio and scurest): **CC-BY 4.0 International**
  Required attribution: tomkranis, @AsoboStudio, @scurest.
- **`RiggedFigure.glb`** — © 2017 Cesium. Licensed under
  **CC-BY 4.0 International**.

### Nature props — `assets/models/nature/`

Source: <https://kenney.nl/assets/nature-kit> (Nature Kit 2.1)

- **Kenney Nature Kit 2.1** — a curated 22-file subset of low-poly,
  vertex-coloured `.glb` models (trees, rocks, flowers, grass, reeds,
  lily pads, a log, and a mushroom) used by the meadow benchmark scene.
  Licensed under **CC0 1.0 Universal** (public domain dedication) — free
  for personal, educational, and commercial use with no attribution
  required. Created/distributed by Kenney (<https://www.kenney.nl>);
  the credit above is included as a courtesy, not an obligation.

### HDRIs — `assets/hdri/`

Source: <https://polyhaven.com/a/syferfontein_0d_clear>

- **`syferfontein_0d_clear_1k.hdr`** — 1K equirectangular sky HDRI from
  Poly Haven. Licensed under **CC0 1.0 Universal** — no attribution
  required. Provides image-based lighting and pond reflections for the
  meadow benchmark scene.

### Fonts — `assets/fonts/`

All five bundled font files are licensed under the
**SIL Open Font License 1.1**. Consolidated license text + per-font
copyright headers in `assets/fonts/OFL.txt`.

- **`arimo.ttf`** — Arimo by Steve Matteson. Digitized data
  © 2010 Google Corporation; © 2012 Red Hat, Inc. Reserved Font Name
  "Arimo". Kept as backwards-compatibility fallback after the
  Inter Tight swap.
- **`inter_tight.ttf`** — Inter Tight (variable weight).
  © 2022 The Inter Project Authors. Reserved Font Name "Inter Tight".
  Default UI font as of Vestige 0.6.2.
- **`cormorant_garamond.ttf`** — Cormorant Garamond (variable weight).
  © 2015 the Cormorant Project Authors / Catharsis Fonts.
  Reserved Font Name "Cormorant Garamond". Display face for the
  wordmark + modal titles per the Phase 9C design hand-off.
- **`jetbrains_mono.ttf`** — JetBrains Mono (variable weight).
  © 2020 The JetBrains Mono Project Authors. Reserved Font Name
  "JetBrains Mono". Mono face for captions / numerics / key-caps.
- **`frank_ruhl_libre.ttf`** — Frank Ruhl Libre (variable weight) by
  Yanek Iontef (Fontef). © 2016 The Frank Ruhl Libre Project Authors.
  Reserved Font Name "Frank Ruhl Libre". Dedicated biblical-Hebrew serif
  for the multi-script text stack (Phase 10 Localization L2).

### Textures — `assets/textures/` (in-engine subset)

- All `*_2k.jpg` files derived from Poly Haven CC0 sets
  (`red_brick_4k`, `brick_wall_005_4k`, `plank_flooring_04_4k`).
  **CC0** — no attribution required.
- `terrain/{grass,rock,dirt,sand}_*` — terrain ground layers repacked from
  ambientCG CC0 sets (`Grass001`, `Rock035`, `Ground068`, `Ground037`;
  <https://ambientcg.com>). **CC0 1.0** — no attribution required; ambientCG
  (Lennart Demes) credited as courtesy.
- `foliage/grass_blades.png` — grass-blade alpha texture for the foliage renderer
  (Realism B / 3D_E-0038), a portrait tuft cropped from the CC0 OpenGameArt "grass
  blades alpha card texture (side view)" (`vegetation_grass_card_03.png`;
  <https://opengameart.org/content/grass-blades-alpha-card-texture-side-view>).
  **CC0 1.0** — no attribution required.
- `foliage/flower_{yellow,white,purple}.png` — engine-authored procedural
  wildflower billboard cards (Realism C3 / 3D_E-0038). **MIT** (matches engine
  LICENSE).
- `label_[1-4].png` — engine-authored. **MIT** (matches engine LICENSE).

The larger 4K Poly Haven assets that will eventually ship in the
separate `VestigeAssets` repo are also CC0; per-asset attribution will
live in `VestigeAssets/ASSET_LICENSES.md`. That repo is private at
launch time and is expected to go public closer to v1.0.0 after a
final redistributability audit — the engine builds and runs cleanly
without it (see `VESTIGE_FETCH_ASSETS` in `external/CMakeLists.txt`).

---

## How to update this file

1. Bump the pin in `external/CMakeLists.txt` (or update the vendored copy).
2. Update the corresponding row above with the new version.
3. If the upstream license changed, audit compatibility with MIT
   distribution before merging.
4. Update the audit tool's NVD dependency list
   (`tools/audit/audit_config.yaml` → `research.nvd.dependencies`) so
   CVE scans track the new version.
5. Note the bump in `CHANGELOG.md` (if the engine has user-visible
   changes) or `tools/audit/CHANGELOG.md` (if only the audit tool is
   affected).
