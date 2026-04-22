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
> the **separate `VestigeAssets` repo**, which stays private until
> ~v1.0.0 pending a final redistributability audit of every asset.
> The engine's `external/CMakeLists.txt` exposes `VESTIGE_FETCH_ASSETS`
> (default OFF) to opt in when available. Fresh public clones build
> cleanly without it. See
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
| `CesiumMan.glb` | [Khronos glTF Sample Assets](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/CesiumMan) | **CC-BY 4.0** with Trademark Limitations | © 2017 Cesium |
| `Fox.glb` | [Khronos glTF Sample Assets](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Fox) | Mesh: **CC0 1.0**; rigging/animation/conversion: **CC-BY 4.0** (composite) | tomkranis, @AsoboStudio, @scurest |
| `RiggedFigure.glb` | [Khronos glTF Sample Assets](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/RiggedFigure) | **CC-BY 4.0** | © 2017 Cesium |

Full attribution lines reproduced in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for licence-compliance
scanners.

---

## Fonts — `assets/fonts/`

| File | Source | License | Attribution |
|------|--------|---------|-------------|
| `arimo.ttf`              | Arimo by Steve Matteson                  | **SIL Open Font License 1.1** (OFL) | "Digitized data © 2010 Google Corporation; © 2012 Red Hat, Inc., with Reserved Font Name 'Arimo'" |
| `inter_tight.ttf`        | Inter Tight (variable) by Rasmus Andersson + The Inter Project Authors | **OFL 1.1** | "Copyright 2022 The Inter Project Authors (https://github.com/rsms/inter-tight)" — Reserved Font Name "Inter Tight" |
| `cormorant_garamond.ttf` | Cormorant Garamond (variable) by Catharsis Fonts / The Cormorant Project Authors | **OFL 1.1** | "Copyright 2015 the Cormorant Project Authors (github.com/CatharsisFonts/Cormorant)" — Reserved Font Name "Cormorant Garamond" |
| `jetbrains_mono.ttf`     | JetBrains Mono (variable) by The JetBrains Mono Project Authors | **OFL 1.1** | "Copyright 2020 The JetBrains Mono Project Authors (https://github.com/JetBrains/JetBrainsMono)" — Reserved Font Name "JetBrains Mono" |
| `OFL.txt`                | Accompanying license text                | OFL preamble + per-font copyright headers + license body | n/a |

The OFL permits free use, modification, bundling, and redistribution,
including in commercial software, provided the license text is included
alongside the font files (which the consolidated `OFL.txt` satisfies)
and the Reserved Font Names listed above are not used for derivative
works. Distributing the engine's `assets/fonts/*.ttf` files unmodified
preserves the original Reserved Font Names.

**Why three new fonts?** The Phase 9C UI/HUD design hand-off
(`vestige-ui-hud-inworld`) specifies a typography pairing:
**Cormorant Garamond** for the wordmark + modal titles (display),
**Inter Tight** for body / button / label text (UI — replaces Arimo as
the canonical default; rasterises cleaner at small sizes through
FreeType), and **JetBrains Mono** for captions / micro labels /
key-caps / numeric readouts (mono). Arimo is retained for
backwards-compatibility with code paths that still reference the
historical default.

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

The local maintainer keeps these files on disk so the Tabernacle scene
renders normally during development. They are not present in the public
repo.

**Default-scene policy (as of 2026-04-22):** a fresh public clone opens
`Engine::setupDemoScene()` — the neutral CC0 demo with four textured
blocks (Red Brick, Gold, Wood, Rough Brick) on a grey ground and a solid
sky-blue clear colour. The Tabernacle scene in `Engine::setupTabernacleScene()`
is gated behind the `--biblical-demo` CLI flag (`EngineConfig::biblicalDemo`)
and is reachable only when the maintainer's private texture set is on
disk. Public users who enable the flag will see placeholder fallbacks
for every tabernacle-prefixed texture.

Neither demo scene now references Texturelabs or everytexture files:
the **Gold** block renders as pure PBR (metallic/roughness albedo, no
diffuse texture), the **Wood** block uses the CC0 Poly Haven
`plank_flooring_04` set already shipped in the repo, and the **ground**
renders untextured grey until a CC0 ground material lands via
`VestigeAssets` (which goes public closer to v1.0.0 — see the status
box at the top).

---

## Open redistribution gaps (TODO before first public push)

1. ~~**`default.ttf`**: identify and document, or replace.~~ ✅ Done — Arimo OFL 1.1, `OFL.txt` shipped alongside.
2. ~~**`everytexture-com-stock-rocks-*`**: confirm license.~~ ✅ Done — license forbids redistribution; files untracked.
3. ~~**glTF model attribution**: collect the per-model CC-BY notices into
   `THIRD_PARTY_NOTICES.md`.~~ ✅ Done — verified each model's exact
   license at the Khronos glTF Sample Assets repo and reproduced the
   required attribution lines in both this file and
   `THIRD_PARTY_NOTICES.md`.
4. ~~**Demo-scene texture replacements**: swap the *three* references in
   `engine/core/engine.cpp` that point to non-redistributable files
   (Texturelabs_Metal_124M for the Gold block at :1621,
   Texturelabs_Glass_120M for the Wood block at :1629, and the
   everytexture rocks for the ground at :1595/:1597/:1645) for Poly
   Haven CC0 alternatives so the public-repo clone renders the demo
   correctly.~~ ✅ Done — Gold is now pure PBR (no diffuse texture
   needed, metallic=1.0 / roughness=0.25 render convincingly on their
   own); Wood switched to the CC0 Poly Haven `plank_flooring_04` set
   already shipped in the repo (now with proper normal + roughness
   maps it lacked before); ground renders untextured grey pending a
   CC0 rock material via the `VestigeAssets` repo. The demo scene in
   the public repo now configures and renders correctly from a clean
   clone — the maintainer's local Texturelabs / everytexture files
   remain untracked for offline development only.
5. ~~**Separate public assets repo**: migrate the large Poly Haven
   `.blend.zip` archives and 4K texture variants out of the engine
   repo to keep clone size manageable.~~ ✅ Done — created
   `milnet01/VestigeAssets`, pulled in via CMake `FetchContent` when
   `VESTIGE_FETCH_ASSETS=ON`. The engine's `external/CMakeLists.txt`
   pins `GIT_TAG v0.1.0`.

   **Assets-repo visibility status:** still private as of the
   engine's public launch. The final redistributability audit for
   every 4K texture / `.blend.zip` archive happens closer to v1.0.0;
   the engine's public fallback (the in-engine 2K CC0 set) keeps
   fresh clones building and the demo rendering correctly without
   the asset pack. Flip to public when the audit is complete and the
   pin to update in lockstep with the tag.

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
