# Asset Licenses

This document inventories every asset that ships in the public Vestige
engine repository and records its source, license, and redistribution
status. Keep it up to date when adding or removing assets.

The engine code itself is MIT-licensed (see [LICENSE](LICENSE)). Assets
shipped in this repo carry their own licenses, listed below.

> **Status as of 2026-04-15**: this is a transitional inventory. The
> non-redistributable Texturelabs textures and the biblical-project
> tabernacle content have been removed from the public repo (see
> `.gitignore`). Larger CC0 assets (Poly Haven 4K textures and
> `.blend.zip` files) are slated for migration to a separate
> public assets repo so engine clones stay small. See
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

| File | Source | License |
|------|--------|---------|
| `default.ttf` | **Unknown — verify before public push** | TBD |

> **TODO**: identify the source of `default.ttf`. If unknown, replace
> with a known-permissive font (DejaVu / Liberation / Noto / Inter).

---

## Textures — `assets/textures/`

### Top-level (mixed sources)

| Pattern | Source | License | Notes |
|---------|--------|---------|-------|
| `red_brick_*.jpg` | Poly Haven `red_brick_4k` | **CC0** | 4K variants are large (~13MB each) |
| `brick_wall_005_*.jpg` | Poly Haven `brick_wall_005_4k` | **CC0** | 4K diff is ~13MB |
| `plank_flooring_04_*.jpg` | Poly Haven `plank_flooring_04_4k` | **CC0** | 4K rough is ~12MB |
| `*_4k.blend.zip` | Poly Haven `.blend` archives | **CC0** | 38–77 MB each |
| `*_4k.blend/textures/*` | Extracted Poly Haven blend assets | **CC0** | Up to ~36MB each |
| `label_[1-4].png` | Engine-authored block labels | MIT | Used in demo scene |
| `everytexture-com-stock-rocks-*` | everytexture.com | **TBD — verify** | Used as ground texture in demo scene |

### Excluded (not in public repo, kept locally via `.gitignore`)

| Pattern | Source | Reason for exclusion |
|---------|--------|----------------------|
| `Texturelabs_*.jpg` (46 files) | texturelabs.org | License permits free *use* but **forbids redistribution** in other asset packs / repos |
| `tabernacle/*` (29 files) | Mixed Poly Haven + project-specific | Destined for the separate private biblical-project repo (commercial Steam release) |

The local maintainer keeps these files on disk so the demo scene and
tabernacle scene render normally during development. They are not
present in the public repo. Two demo materials currently reference
Texturelabs files (`Gold` and `Wood` blocks in `engine.cpp`); these
will fall back gracefully on a fresh clone (textures null, materials
render with their albedo colour) until CC0 replacements are wired in.

---

## Open redistribution gaps (TODO before first public push)

1. **`default.ttf`**: identify and document, or replace.
2. **`everytexture-com-stock-rocks-*`**: confirm the everytexture.com
   license permits redistribution. If not, exclude like Texturelabs.
3. **glTF model attribution**: collect the per-model CC-BY notices into
   `THIRD_PARTY_NOTICES.md`.
4. **Demo-scene texture replacements**: swap the two Texturelabs
   references in `engine/core/engine.cpp:1621` (Gold) and `:1629`
   (Wood) for Poly Haven CC0 alternatives so the public-repo clone
   renders the demo correctly.
5. **Separate public assets repo**: migrate the large Poly Haven
   `.blend.zip` archives and 4K texture variants out of the engine
   repo to keep clone size manageable. Engine loads from a configured
   path (env var or CMake-time download).

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
