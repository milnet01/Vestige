# Asset Licenses

This document inventories every asset that ships in the public Vestige
engine repository and records its source, license, and redistribution
status. Keep it up to date when adding or removing assets.

The engine code itself is MIT-licensed (see [LICENSE](LICENSE)). Assets
shipped in this repo carry their own licenses, listed below.

> **Status as of 2026-04-15**: the non-redistributable Texturelabs
> textures, the everytexture.com rocks, and the biblical-project
> tabernacle content have been removed from the public repo (see
> `.gitignore`). The large CC0 assets (Poly Haven 4K textures,
> `.blend.zip` files, extracted blend dirs) have been migrated to
> the **separate `VestigeAssets` repo** and are pulled in via CMake
> `FetchContent` at configure time. See
> [`docs/PRE_OPEN_SOURCE_AUDIT.md`](docs/PRE_OPEN_SOURCE_AUDIT.md) §4.

---

## Shaders — `assets/shaders/`

| Files | Source | License |
|-------|--------|---------|
| `*.glsl` | Engine-authored | MIT (matches engine LICENSE) |

---

## Models — `assets/models/`

| File | Source | License | Attribution required |
|------|--------|---------|----------------------|
| `CesiumMan.glb` | Khronos glTF Sample Models | CC-BY 4.0 | Yes — Cesium Inc. |
| `Fox.glb` | Khronos glTF Sample Models | CC0 | No |
| `RiggedFigure.glb` | Khronos glTF Sample Models | CC-BY 4.0 | Yes |

> **TODO before public push**: verify each model's exact license at
> https://github.com/KhronosGroup/glTF-Sample-Models. Add full
> attribution lines to `THIRD_PARTY_NOTICES.md`.

---

## Fonts — `assets/fonts/`

| File | Source | License | Attribution |
|------|--------|---------|-------------|
| `default.ttf` | Arimo by Steve Matteson | **SIL Open Font License 1.1** (OFL) | "Digitized data © 2010 Google Corporation; © 2012 Red Hat, Inc., with Reserved Font Name 'Arimo'" |
| `OFL.txt`     | Accompanying license text | OFL preamble + license body | n/a |

The OFL permits free use, modification, bundling, and redistribution,
including in commercial software, provided the license text is included
alongside the font file (which `OFL.txt` satisfies) and the Reserved
Font Name "Arimo" is not used for derivative works.

---

## Textures — `assets/textures/`

### Top-level (mixed sources)

| Pattern | Source | License | Notes |
|---------|--------|---------|-------|
| `red_brick_diff_2k.jpg` (+ `_nor_gl_2k`, `_disp_2k`, `_rough_2k`) | Poly Haven `red_brick_4k` | **CC0** | 2K variants used by demo scene |
| `brick_wall_005_diff_2k.jpg` (+ siblings) | Poly Haven `brick_wall_005_4k` | **CC0** | 2K variants used by demo scene |
| `plank_flooring_04_diff_2k.jpg` (+ siblings) | Poly Haven `plank_flooring_04_4k` | **CC0** | 2K variants used by demo scene |
| `label_[1-4].png` | Engine-authored block labels | MIT | Used in demo scene |
| ~~`everytexture-com-stock-rocks-*`~~ | ~~everytexture.com~~ | **Excluded — see below** | Was used as ground texture; license forbids redistribution |
| ~~`*_4k.jpg`, `*_4k.blend.zip`, `*_4k.blend/`~~ | Poly Haven CC0 4K variants | **Moved to VestigeAssets repo** | Pulled in at configure time via CMake `FetchContent`; engine clones stay small |

### Excluded (not in public repo, kept locally via `.gitignore`)

| Pattern | Source | Reason for exclusion |
|---------|--------|----------------------|
| `Texturelabs_*.jpg` (46 files) | texturelabs.org | License permits free *use* but **forbids redistribution** in other asset packs / repos |
| `everytexture-com-stock-rocks-*` (4 files) | everytexture.com | License permits free personal/commercial use but **forbids redistribution** in other repositories. Per the site's Terms and Conditions: "You will not redistribute any of the content from everytexture.com unless this content is specifically made for redistribution." |
| `tabernacle/*` (29 files) | Mixed Poly Haven + project-specific | Destined for the separate private biblical-project repo (commercial Steam release) |

The local maintainer keeps these files on disk so the demo scene and
tabernacle scene render normally during development. They are not
present in the public repo. Two demo materials currently reference
Texturelabs files (`Gold` and `Wood` blocks in `engine.cpp`); these
will fall back gracefully on a fresh clone (textures null, materials
render with their albedo colour) until CC0 replacements are wired in.

---

## Open redistribution gaps (TODO before first public push)

1. ~~**`default.ttf`**: identify and document, or replace.~~ ✅ Done — Arimo OFL 1.1, `OFL.txt` shipped alongside.
2. ~~**`everytexture-com-stock-rocks-*`**: confirm license.~~ ✅ Done — license forbids redistribution; files untracked.
3. **glTF model attribution**: collect the per-model CC-BY notices into
   `THIRD_PARTY_NOTICES.md`.
4. **Demo-scene texture replacements**: swap the *three* references in
   `engine/core/engine.cpp` that point to non-redistributable files
   (Texturelabs_Metal_124M for the Gold block at :1621,
   Texturelabs_Glass_120M for the Wood block at :1629, and the
   everytexture rocks for the ground at :1595/:1597/:1645) for Poly
   Haven CC0 alternatives so the public-repo clone renders the demo
   correctly. Locally the maintainer's files stay in place — these
   replacements are about the public clone experience.
5. ~~**Separate public assets repo**: migrate the large Poly Haven
   `.blend.zip` archives and 4K texture variants out of the engine
   repo to keep clone size manageable.~~ ✅ Done — created
   `milnet01/VestigeAssets` (currently private), pulled in via CMake
   `FetchContent` at configure time. The engine's `external/CMakeLists.txt`
   pins `GIT_TAG v0.1.0`. Bump that pin in lockstep with VestigeAssets
   tags. Both repos will go public together.

---

## How to add a new asset

1. Verify the license **explicitly permits redistribution** under MIT
   or a compatible permissive license. "Free to download and use" is
   *not* sufficient — the license must allow third parties to obtain
   the asset by cloning this repo.
2. Add the file to `assets/<category>/` and reference it from code.
3. Add a row to the appropriate table above with source URL and
   license.
4. If the license requires attribution (CC-BY, OFL, etc.), add the
   attribution line to `THIRD_PARTY_NOTICES.md`.
5. If the asset is large (>1 MB), consider whether it belongs in the
   future public assets repo rather than in the engine repo.
