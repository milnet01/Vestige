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

| Dependency       | Version       | License                                           | Source |
|------------------|---------------|---------------------------------------------------|--------|
| GLFW             | 3.4           | zlib                                              | <https://github.com/glfw/glfw> |
| GLM              | 1.0.1         | MIT (modified)                                    | <https://github.com/g-truc/glm> |
| Dear ImGui       | docking branch| MIT                                               | <https://github.com/ocornut/imgui> |
| ImGuizmo         | master        | MIT                                               | <https://github.com/CedricGuillemet/ImGuizmo> |
| imgui-filebrowser| master        | MIT                                               | <https://github.com/AirGuanZ/imgui-filebrowser> |
| imgui-node-editor| v0.9.3        | MIT                                               | <https://github.com/thedmd/imgui-node-editor> |
| ImPlot           | master        | MIT                                               | <https://github.com/epezent/implot> |
| FreeType         | VER-2-13-3    | FreeType Project License (BSD-style) — chosen over GPLv2 | <https://gitlab.freedesktop.org/freetype/freetype> |
| tinyexr          | v1.0.9        | BSD 3-Clause                                      | <https://github.com/syoyo/tinyexr> |
| tinygltf         | v2.9.4        | MIT                                               | <https://github.com/syoyo/tinygltf> |
| nlohmann/json    | v3.12.0       | MIT                                               | <https://github.com/nlohmann/json> |
| Jolt Physics     | v5.2.0        | MIT                                               | <https://github.com/jrouwe/JoltPhysics> |
| OpenAL Soft      | 1.24.1        | LGPL v2.1 (dynamic linking)                       | <https://github.com/kcat/openal-soft> |
| Recast Navigation| v1.6.0        | zlib                                              | <https://github.com/recastnavigation/recastnavigation> |
| GoogleTest       | v1.15.2       | BSD 3-Clause                                      | <https://github.com/google/googletest> |

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
[ASSET_LICENSES.md](ASSET_LICENSES.md). Notable redistributable assets
that ship in this repo include CC0 textures from Poly Haven and CC-BY
glTF sample models from the Khronos Group.

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
