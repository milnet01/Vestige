<!-- ants-spec-format: 1 -->
# 3D_E-0874 — Resolve relative asset paths against the asset root

**Status:** spec draft (2026-11-06).
**Kind:** fix.
**Source:** ROADMAP 3D_E-0874 (readability review lead lane-02 `Engine::initialize`, 2026-11-05 note).

Layman: the demo scenes and saved scenes find their textures and sounds wherever the engine is started from, and a texture picked in the editor is still there after the scene is saved and reopened.

## 1. Goal

One rule decides what a relative asset path means: **it names a file under
the asset root** (`Engine::getAssetPath()`), never under the working
directory. Every load and every saved scene follows it. A scene saved in the
editor stores root-relative paths, and reopening it restores every texture
it saved, whatever the working directory.

## 2. Problem

The asset root is found by `resolveAssetPath` (`engine/utils/asset_locator.cpp`).
It is usually an absolute path such as `<exeDir>/assets`. Paths in the code and
in scene files are interpreted three different ways:

1. **Relative to the working directory.** The demo scenes in
   `engine/core/engine.cpp` pass literals such as
   `"assets/textures/red_brick_diff_2k.jpg"`. `LocalizationService::m_dir`
   defaults to `"assets/localization"` and nothing in `engine/` or `app/` calls
   `setLocalizationDir`. `SpriteSystem::initialize` passes the literal
   `"assets"`. `script_templates.cpp` stores `"assets/sounds/door_open.ogg"`.
   `PathSandbox::validateInsideRoots` resolves a relative input with
   `weakly_canonical`, which uses the working directory. These loads succeed
   only when the engine is started from the repository root. Started from the
   taskbar or another folder, textures, sounds and translations go missing.
2. **Absolute.** The asset browser (`AssetBrowserPanel`) builds
   `entry.fullPath` from the absolute root. The import dialog passes the file
   browser's absolute selection. The glTF loader caches external textures under
   the absolute canonical path from `PathSandbox::resolveUriIntoBase`.
3. **Saved as given.** `ResourceManager::findTexturePath` returns the string
   the texture was loaded with. `MaterialJson::toJson` writes it into the
   scene. On load, `MaterialJson::apply` with `TexturePaths::SandboxedRelative`
   rejects absolute paths through `PathSandbox::isSafeRelativeAsset`. So a
   texture picked in the editor, or imported with a glTF model, is **lost when
   the scene is saved and reopened**. The test
   `SceneDoesNotLoadAnAbsoluteTexturePath` pins the rejection, which is right;
   what is wrong is that the editor writes absolute paths at all.

The audio fields already store root-relative strings in their tests
(`"audio/music/theme.ogg"` in `tests/test_entity_serializer_registry.cpp`),
but nothing resolves them against the root either.

No committed scene file stores a texture or model path: the only `.scene` in
the repository, `tests/fixtures/scenes/pre_v2_no_music.scene`, has no
entities. Scenes saved by users may hold `"assets/..."` paths (from the demo
scenes) or absolute ones (from the editor).

## 3. Scope decisions (agreed with the user)

None from the user: they were away, and this was decided as code owner on
2026-11-06. The choices are: stored paths are root-relative (§ 8 records the
alternatives); old `"assets/"`-prefixed paths keep loading (§ 4.3); the
terrain heightmap and splatmap stay relative to the scene file (§ 9).

## 4. Design

### 4.1 One helper owns the rule

New `engine/utils/asset_path.{h,cpp}`:

```cpp
namespace Vestige::AssetPath
{
/// Root-relative, '/'-separated form of `path`, for caching and saving.
/// - absolute and lexically inside `root`  -> the part below root
/// - relative                              -> legacy-stripped (4.3), else unchanged
/// - absolute outside `root`, or root empty -> `path` unchanged
std::string toKey(const std::filesystem::path& root, const std::string& path);

/// The file a key or a caller's path names.
/// - absolute                -> unchanged
/// - relative, root empty    -> unchanged (working directory, as today)
/// - relative                -> root / path, lexically normalised
std::filesystem::path resolve(const std::filesystem::path& root, const std::string& path);
}
```

Both are lexical: neither follows a symbolic link. A file under a linked
folder (`assets/models/nature_local/*`) keeps its path below the root, so it
saves as root-relative. The sandbox check stays in `validateInsideRoots`,
which does follow links, and still runs on every load.

### 4.2 Where the rule is applied

| Site | Change |
|------|--------|
| `ResourceManager` | New `setAssetRoot(path)`. `loadTexture`, `loadMesh` and `loadModel` key their caches by `toKey` and open `resolve(key)`. `findTexturePath` therefore returns a root-relative key, and so does `findMeshKey`. |
| `AudioEngine` | New `setAssetRoot(path)`. `validatePath` and `resolveSandboxedPath` resolve with `resolve` before the sandbox check. |
| `GltfLoader` | The cache key for an external image is the lexical join of the model's directory and the URI. The canonical path from `resolveUriIntoBase` is still what the sandbox checks. |
| `Engine::initialize` | Calls both `setAssetRoot`s with `config.assetPath`, and `LocalizationService::setLocalizationDir(config.assetPath + "/localization")` right after registering the service, before settings are live-applied. |
| `engine.cpp` demo scenes | Literals lose the `"assets/"` prefix. Loads that bypass `ResourceManager` (`Texture::loadFromFile`, `Texture::generateNormalFromHeight`, `Renderer::loadSkyboxHDRI`, the terrain ground-texture and foliage path builders) join through `AssetPath::resolve`. |
| `SpriteSystem::initialize` | `engine.getAssetPath()` instead of `"assets"`. |
| `ParticleRenderer` | A scene's `texturePath` is opened through `AssetPath::resolve`. |
| `script_templates.cpp` | Stored clip paths lose the `"assets/"` prefix. |

### 4.3 Old `"assets/"` paths

Scenes saved before this change may store `"assets/textures/x.png"`. `toKey`
strips a leading `assets/` component from a relative path when
`root / "assets"` does not exist. So an old scene loads, and saving it again
writes `"textures/x.png"`. A root that really holds an `assets/` subfolder
keeps the path as written.

### 4.4 Paths outside the root

An absolute path outside the root keeps its absolute key. Under a sandbox it
can load only when it lies inside another sandbox root (the working directory
or a linked folder's target). Saved into a scene, it is rejected on load as it
is today, with the existing warning. That case is out of scope (§ 9).

## 5. Invariants

- **INV-1** — A relative path given to `ResourceManager::loadTexture` opens the
  file under the asset root, whatever the working directory.
  *Test:* `tests/test_asset_path.cpp` `RelativeTextureLoadsFromRootNotCwd`: a
  temporary root holding `textures/a.png`, the working directory changed to an
  empty temporary folder, sandbox roots set to the root only; the load returns
  a texture that is not the default texture.
  *Breaks when:* `loadTexture` opens the path as given, so it is resolved
  against the working directory, where no `textures/a.png` exists.

- **INV-2** — A texture loaded by an absolute path under the root saves as its
  root-relative path, and the saved scene restores it.
  *Test:* `tests/test_material_json.cpp` `EditorPickedTextureSurvivesSceneRoundTrip`:
  load `<root>/textures/a.png` by absolute path, `MaterialJson::toJson` gives
  `"diffuseTexture": "textures/a.png"`, and `MaterialJson::apply` with
  `SandboxedRelative` restores a diffuse texture.
  *Breaks when:* the cache key is the absolute string, so `toJson` writes it
  and `apply` rejects it.

- **INV-3** — A legacy `"assets/"`-prefixed path loads the same file as its
  root-relative form, and both share one cache entry.
  *Test:* `tests/test_asset_path.cpp` `LegacyAssetsPrefixSharesCacheEntry`:
  `loadTexture("assets/textures/a.png")` and `loadTexture("textures/a.png")`
  return the same pointer, under a root with no `assets/` subfolder.
  *Breaks when:* `toKey` does not strip the prefix, so the legacy path opens
  `<root>/assets/textures/a.png`, which does not exist, and returns the
  default texture.

- **INV-4** — The prefix is kept when the root really contains an `assets/`
  folder.
  *Test:* `tests/test_asset_path.cpp` `AssetsSubfolderIsNotStripped`: with
  `<root>/assets/` present, `toKey(root, "assets/x.png")` is `"assets/x.png"`.
  *Breaks when:* the prefix is stripped unconditionally.

- **INV-5** — The trust boundary is unchanged. A scene path that is absolute,
  rooted or contains `..` is still rejected on scene load, and a resolved path
  outside every sandbox root is still refused.
  *Test:* the existing `tests/test_path_sandbox.cpp`,
  `tests/test_material_json.cpp` `SceneDoesNotLoadAnAbsoluteTexturePath` and
  `tests/test_resource_manager_sandbox.cpp` stay green, plus
  `tests/test_asset_path.cpp` `RootRelativeTraversalIsRefused`:
  `loadTexture("../outside.png")` with a file one level above the root returns
  the default texture.
  *Breaks when:* `resolve` joins `root / "../outside.png"` and the result
  reaches the loader without the sandbox check.

- **INV-6** — The demo scenes, sprites and translations load when the engine
  is started from a folder other than the repository root.
  *Test:* manual: from an empty folder, run
  `<repo>/build-release/bin/vestige --demo-flythrough` headless under cage,
  then search the log for `Failed to load`, `using default` and
  `StringTable`. None appear for a demo asset.
  *Breaks when:* any listed literal keeps its `"assets/"` prefix, or
  `setLocalizationDir` is not called before settings are live-applied.

## 6. Failure modes

- **Root empty** (tests, or a caller that never sets it): `resolve` and
  `toKey` leave paths unchanged, so behaviour is as today.
- **Root relative** (`--assets assets`): `resolve` gives a relative result,
  which the loader resolves against the working directory. That is what the
  user asked for by passing a relative root.
- **A file missing under the root**: the loader fails and logs as today. The
  log names the path as given.
- **Two spellings of one file** (absolute, legacy, relative): one key, so one
  GPU texture (INV-3).

## 7. Tests

New `tests/test_asset_path.cpp` locks INV-1, INV-3, INV-4 and the new half of
INV-5. A new case in `tests/test_material_json.cpp` locks INV-2. Each new test
must be seen to fail against the pre-change code before the fix lands; INV-4
fails only against a mutation that strips unconditionally. INV-6 is the
manual run, because the demo scenes need a GL context and the full engine.

## 8. Alternatives considered (and rejected)

- **Keep `"assets/"` in stored paths and resolve against the root's parent.**
  No migration, but it works only when the root folder is named `assets`.
  A user-picked root (`chooseAssetRoot`) named anything else breaks every path.
- **Store absolute paths and allow them on scene load.** Saved scenes would
  stop working on another machine or after moving the install, and the
  `isSafeRelativeAsset` defence against a hostile scene file would go.
- **Change the working directory to the root's parent at startup.** One line,
  but it hides the rule instead of stating it, still needs the root to be
  named `assets`, and changes where screenshots, logs and relative command-line
  paths land.

## 9. Out of scope

- Dropping the working directory from the sandbox roots (`Engine::initialize`
  adds `current_path`) — deferred; not yet queued.
- The scene `mesh` key reaching `ResourceManager::loadMesh` without
  `isSafeRelativeAsset`, while the comment in `entity_serializer.cpp` says it
  never opens a file — deferred; not yet queued.
- Terrain `heightmap_file` and `splatmap_file` stay relative to the scene
  file's folder, by design: they are written beside the scene.
- Audio clip fields typed into the inspector are stored as typed.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/test_asset_path.cpp` `RelativeTextureLoadsFromRootNotCwd` |
| INV-2 | `tests/test_material_json.cpp` `EditorPickedTextureSurvivesSceneRoundTrip` |
| INV-3 | `tests/test_asset_path.cpp` `LegacyAssetsPrefixSharesCacheEntry` |
| INV-4 | `tests/test_asset_path.cpp` `AssetsSubfolderIsNotStripped` |
| INV-5 | the existing sandbox tests named in INV-5, and `RootRelativeTraversalIsRefused` |
| INV-6 | **nothing** automatic — a manual headless run; the demo scenes need a GL context |
| A new direct-open site using a cwd-relative literal | **nothing** — no check greps for `"assets/` literals |

## 11. Cross-doc impact

`CHANGELOG.md`: the editor fix (picked textures survive save and reopen) and
starting the engine from any folder. `docs/specs/3D_E-0024-interface-languages.md`
names `assets/localization/`; that folder does not move, so it needs no edit.

## 12. Cold-eyes loop log

Rows live in `../reviews/3D_E-0874-asset-relative-paths-loop-log.md`.
