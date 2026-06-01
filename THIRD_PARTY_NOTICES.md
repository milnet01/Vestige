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
| FreeType         | VER-2-13-3                                      | FreeType Project License (BSD-style) — chosen over GPLv2 | <https://github.com/freetype/freetype> (official mirror; build fetches this) |
| tinyexr          | v1.0.9                                          | BSD 3-Clause                                      | <https://github.com/syoyo/tinyexr> |
| tinygltf         | v2.9.4                                          | MIT                                               | <https://github.com/syoyo/tinygltf> |
| nlohmann/json    | v3.12.0 (URL tarball; see `external/CMakeLists.txt`)     | MIT                                      | <https://github.com/nlohmann/json> |
| Jolt Physics     | v5.3.0                                          | MIT                                               | <https://github.com/jrouwe/JoltPhysics> |
| OpenAL Soft      | 1.25.1                                          | LGPL v2.1 (dynamic linking)                       | <https://github.com/kcat/openal-soft> |
| Recast Navigation| v1.6.0                                          | zlib                                              | <https://github.com/recastnavigation/recastnavigation> |
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
  `external/CMakeLists.txt` for the full reason). Re-evaluate when upstream
  fixes the `vec_t` constructor.
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

Each vendored source carries its own license header in the file. See
the individual files for the canonical license text.

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

### Fonts — `assets/fonts/`

All four bundled font files are licensed under the
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

### Textures — `assets/textures/` (in-engine subset)

- All `*_2k.jpg` files derived from Poly Haven CC0 sets
  (`red_brick_4k`, `brick_wall_005_4k`, `plank_flooring_04_4k`).
  **CC0** — no attribution required.
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
