# Asset Folder-Picker Fallback — Design Doc

| Field | Value |
|---|---|
| Feature | Native folder-picker + remembered choice as the last-resort asset-root fallback |
| Status | `cold-eyes converged` (6 loops, 2026-06-25) — signed off by Claude per the delegated-sign-off convention; ready for implementation |
| Relationship | Part B of robust asset resolution. **Part A shipped** (commit `e9e2f9e`): `resolveAssetPath()` resolves `--assets` → `$VESTIGE_ASSETS` → `<exe>/assets` → `<exe>/../share/vestige/assets` → `<cwd>/assets`, returning `""` when none is valid (`engine/utils/asset_locator.{h,cpp}`). |
| Owners | milnet01 |

---

## 1. Goal

When the engine cannot auto-locate its `assets/` directory (Part A returns `""`),
instead of failing, **let the user point at it with a native folder-picker and
remember that choice** so it is not asked again while the remembered path stays
valid. This is a rare safety net —
packaged builds (AppImage / tarball / zip) ship assets beside the binary and are
resolved by Part A — for the case where assets sit somewhere non-standard. (An
explicit but *wrong* `--assets` / `$VESTIGE_ASSETS` is honoured as-is and fails at
load, **not** routed to the picker — see §3 step 1; the picker is for the
auto-locate-found-nothing case.)

## 2. Scope

**In scope**
- A remembered asset path persisted in the platform user-config directory.
- A native OS folder-picker shown only when auto-locate AND the remembered path
  both fail.
- Validation of any chosen/remembered path via the existing `isAssetDir()`
  sentinel (`shaders/scene.vert.glsl`).

**Out of scope**
- Bundling a GUI toolkit; a general settings/preferences system; remembering more
  than one asset root; changing Part A's auto-search order.

## 3. Resolution order (Part A + Part B)

`main()` wires the real inputs; the decision logic is a **pure, testable
function** `chooseAssetRoot()` added to `engine/utils/asset_locator.{h,cpp}`:

```cpp
struct AssetRootChoice { std::string path; bool persist; };

AssetRootChoice chooseAssetRoot(
    const std::string& autoResolved,   // resolveAssetPath(cli); may be ""
    const std::string& remembered,     // from user config; may be ""
    const std::function<bool(const std::string&)>& isValid,  // string→path adapter over isAssetDir
    const std::function<std::string()>& pickFolder);         // "" on cancel/unavailable
```

`chooseAssetRoot` is **string-only** (no `std::filesystem::path` in its signature)
and takes **no default arguments** — the trailing comments are illustrative.
`main()` does the string↔path adaptation at the seam: it passes
`isValid = [](const std::string& p){ return isAssetDir(std::filesystem::path(p)); }`
(Part A's `isAssetDir` takes a `path`) and keeps `chooseAssetRoot` purely on
strings so the test seam needs no filesystem types.

Decision logic:

1. `autoResolved` non-empty → `{autoResolved, false}`. This already covers Part A:
   an explicit `--assets` / `$VESTIGE_ASSETS` (honoured **as-is** — a typo'd
   explicit override is the user's responsibility; it is returned non-empty and
   therefore does **not** fall through to the remembered path or picker, failing
   later at shader load with Part A's clear message) **or** a validated
   binary-relative / cwd hit.
2. else `remembered` non-empty **and** `isValid(remembered)` → `{remembered, false}`.
3. else **picker loop**: call `pickFolder()`; on `""` (cancel or unavailable) →
   `{"", false}`; on a path where `isValid` is true → `{path, true}` (persist);
   on a non-cancelled **invalid** pick → loop and prompt again. Cancel is always
   the exit, so the loop terminates on user action (no arbitrary attempt cap).
   "Valid pick" means `isValid(picked) == true`; the path is persisted **only**
   in that case, so a wrong folder is never written to config.
4. `main()`: if the chosen `path` is empty → **fatal** with Part A's clear
   asset-not-found message, **extended** with "(and no folder could be selected)"
   so a headless / no-dialog user is not told only that assets are missing
   (cancel and dialog-unavailable both arrive here as `""`); if `persist` is true
   → write the path to user config (§6).

`main()` supplies `autoResolved = resolveAssetPath(cli)` (Part A), `remembered =`
the config value (§6), and `pickFolder = pickAssetFolder` (§4 — passed as a
callable, **not** invoked here, so the dialog only opens inside the loop).
Auto-search
precedes the remembered path on purpose: a packaged app's assets move *with* the
binary, so binary-relative stays correct even when relocated; the remembered path
only matters when assets are detached from the binary.

## 4. Dependency: `tinyfiledialogs`

- **Choice:** [`tinyfiledialogs`](https://sourceforge.net/projects/tinyfiledialogs/)
  — a single `tinyfiledialogs.c` + `.h`, no build-time GUI deps. On Windows it
  calls the native Win32 dialog; on Linux it shells out to an installed dialog
  (zenity / kdialog / matedialog / qarma).
- **Why not `nativefiledialog-extended` (NFD):** NFD pulls GTK3 or
  xdg-desktop-portal/DBus as Linux build deps; this project links GLFW, not GTK,
  and the picker is a rare path — a single vendored C file is the lighter,
  lower-risk fit (global `~/.claude/CLAUDE.md` Rule 9 "push back when a simpler
  path exists" + project Rule 3 "modular and minimal").
- **Integration:** vendored under `external/tinyfiledialogs/` (single `.c`
  compiled as C), matching the project's existing vendored-source precedent
  (glad / stb / dr_libs — `THIRD_PARTY_NOTICES.md` "Vendored sources" table).
  Upstream ships SourceForge snapshots with **no git release tags**, so the pin
  is a dated snapshot recorded inline in the row — the "Vendored sources" table
  has **no Version column** (tl::expected carries its `(v1.3.1)` inside the
  Library cell the same way), so note the snapshot's version constant (from the
  `tinyfiledialogs.h` header) + date there. Project Rule 8's "older pin needs a
  written reason" is met by this no-tags note. The **exact SPDX id is lifted from
  the shipped header** at integration (expected `Zlib`). Per
  `THIRD_PARTY_NOTICES.md` "How to update": MIT license-compatibility is confirmed
  (zlib is MIT-compatible); the row goes in the Vendored sources table (not the
  FetchContent table); the NVD/CVE list step is skipped because there is no
  published CVE feed for tinyfiledialogs — matching glad / dr_libs (vendored and
  absent from the NVD list), though note stb and tl::expected *are* listed, so
  this is a per-dep judgement, not a blanket carve-out; and the CHANGELOG note is
  added. The vendoring gets its own cold-eyes per Rule 8.
- **Thin wrapper:** `engine/platform/folder_dialog.{h,cpp}` exposes
  `std::string pickAssetFolder()` — the chosen directory, or `""` on cancel **or**
  when no dialog can be shown.
- **Headless / no-display guard** (normal platform handling — no rule carve-out
  needed): on Linux, `pickAssetFolder()` returns `""` immediately when **both** `$DISPLAY`
  and `$WAYLAND_DISPLAY` are unset — it does **not** call tinyfiledialogs (which
  could block or error without a display), so the "never blocks indefinitely"
  guarantee does not depend on the dialog tool's own behaviour. If a display is
  present but no dialog tool is installed, tinyfiledialogs returns null → `""`.
  Either way the empty result flows to §3 step 4 (fatal, clear message). Desktop
  Linux effectively always has zenity (GNOME) or kdialog (KDE); AppImage users
  hit Part A first and never reach the picker.

## 5. Startup ordering (the subtlety)

The picker is invoked in `main()` **before `engine.initialize()`** — i.e. before
any OpenGL context or asset load. It is a native OS dialog, independent of the
engine's own UI (which itself needs shaders/fonts to draw and so cannot be used
to *find* assets). This avoids the bootstrap paradox: the thing that's missing
(assets) is never required to render the prompt.

## 6. Config persistence — reuse `ConfigPath` + `AtomicWrite`

Part B adds **no** new path-resolution or durable-write code — it reuses the
engine's shipped helpers (global Rule 3 "reuse before rewriting" + project Rule 3
"modular and minimal"). The three small functions it does add (`parseAssetPathConfig`,
`readRememberedAssetPath`, `writeRememberedAssetPath`) live in
`engine/utils/asset_locator.{h,cpp}` alongside `chooseAssetRoot`:

- **Location:** the remembered path is stored in
  `ConfigPath::getConfigFile("asset_root")`
  (`engine/utils/config_path.{h,cpp}`). That resolver already honours a
  `VESTIGE_CONFIG_DIR` override on **all** platforms (returned verbatim — the knob
  the config-redirect tests use; Part A's own tests redirect via `VESTIGE_ASSETS`),
  then Linux
  `$XDG_CONFIG_HOME/vestige` → `$HOME/.config/vestige` → `/tmp/vestige`, and
  Windows `%LOCALAPPDATA%\Vestige` → `%USERPROFILE%\AppData\Local\Vestige` → temp.
  It always yields a directory (a temp fallback, never a "disabled" state), so
  Part B does not reimplement or special-case config-dir resolution.
- **Read:** `std::optional<std::string> readRememberedAssetPath()` reads
  `ConfigPath::getConfigFile("asset_root")` and runs it through the **pure**
  parser `parseAssetPathConfig(contents) -> std::optional<std::string>`. Parser
  rules (pure / testable): split each line on the **first** `=` only (a value may
  contain `=` — Linux paths can); strip a trailing `\r`/`\n`; **return the value
  of the first `assets.path=` line**, skipping lines with no `=` or any other key
  (forward-compatible) and taking the value with no unescaping. Absent / unreadable
  / empty / no `assets.path` line ⇒ `nullopt`.
- **Write:** `bool writeRememberedAssetPath(const std::string& path)` calls the
  existing `AtomicWrite::writeFile(ConfigPath::getConfigFile("asset_root"),
  "assets.path=" + path + "\n")` — reusing the engine's durable tmp→fsync→rename
  helper (cross-platform; `MoveFileEx(REPLACE_EXISTING)` on Windows; it **creates
  parent dirs itself**, so no separate `create_directories` is needed). It
  serializes exactly `assets.path=<path>\n` with **no escaping**, mirroring the
  verbatim parse so the round-trip is exact. Returns `true` on `Status::Ok` **and**
  `Status::DirFsyncFailed` (the file is written and readable — only crash-durability
  is unguaranteed); returns `false` (and the caller logs) on
  `TempWriteFailed`/`FsyncFailed`/`RenameFailed`, leaving any existing config
  untouched. A non-persisted path is self-healing: it just re-triggers the picker
  next launch.

## 7. CPU / GPU placement (project Rule 7)

All **CPU** — startup-time path resolution, file I/O, and a native dialog. No GPU
work, no per-frame cost, nothing to mirror.

## 8. Testing

- **`chooseAssetRoot()` (pure, GL-free):** with an injected `isValid` predicate
  and a scripted `pickFolder`, assert: a non-empty `autoResolved` wins with no
  pick (`persist=false`); a `remembered` path is used only when `isValid`
  accepts it; the picker **loops past an invalid pick to a valid one**
  (`persist=true`); and cancel/unavailable (`pickFolder` returns `""`) yields
  `{"", false}`.
- **`parseAssetPathConfig` (pure, GL-free):** round-trip including a path
  **containing `=`**; unknown-key / `=`-less line tolerance; absent/empty input ⇒
  `nullopt`.
- **Config dir:** resolution is `ConfigPath`'s job (already tested); any
  integration test redirects it via the `VESTIGE_CONFIG_DIR` override — the same
  knob the Part A / Windows tests use — not a new seam.
- **Not unit-tested (thin I/O / OS seams):** `readRememberedAssetPath` /
  `writeRememberedAssetPath` are thin wrappers over the already-tested `ConfigPath`
  + `AtomicWrite` (whose `TestHooks::forceNextWriteFailure` hook, under
  `VESTIGE_TEST_HOOKS`, already covers the write-failure branch) and the pure
  `parseAssetPathConfig` — exercised via those
  tests plus a manual check, not against the real config dir; and the
  `pickAssetFolder()` tinyfiledialogs call (an OS dialog) + its
  `$DISPLAY`/`$WAYLAND_DISPLAY` guard. Note the `chooseAssetRoot` test above
  scripts the **injected** `pickFolder` callback; the real `pickAssetFolder` impl
  is the manual-only seam, never invoked in tests.
- New cases land in the existing `tests/test_asset_locator.cpp` (the `asset_locator`
  gtest target); the suite stays green and runs in the single-process gtest binary.

## 9. Non-goals

A preferences UI, multi-root management, auto-download of assets, or changing the
shipped-package layout. Each is a separate decision, not a silent omission.

## 10. Change log

- 2026-06-25 — initial draft (Part B; Part A already shipped in `e9e2f9e`).
- 2026-06-25 — cold-eyes loop 1 fixes folded in: named the pure orchestration
  seam `chooseAssetRoot()` (§3) + the `folder_dialog` wrapper (§4); specified
  invalid-pick loop + "valid pick"/persist semantics (§3); config parse rules,
  atomic write, XDG empty/HOME-unset edges + testable `configDirFrom` seam (§6);
  headless `$DISPLAY`/`$WAYLAND_DISPLAY` guard (§4); exact SPDX + vendored-table
  destination (§4); citation precision (§4).
- 2026-06-25 — cold-eyes loop 2 fixes: clarified `chooseAssetRoot` is string-only
  with no default args + the `main()` string↔path adapter lambda (§3); named the
  config read/write seams + gave the config-dir helper an `optional` return (§6);
  extended the fatal message for the no-dialog/headless case (§3); `pickFolder`
  passed as a callable, not invoked (§3); §4 update-steps now tick MIT
  license-compat + lean the NVD skip on vendored-dep precedent.
- 2026-06-25 — cold-eyes loop 3 fixes: corrected the NVD-precedent claim (stb +
  tl::expected *are* NVD-tracked — skip justified per-dep on no CVE feed, §4);
  no-git-tags snapshot pin + no Version column in the vendored table (§4);
  first-run dir creation + write-failure semantics for `writeRememberedAssetPath`
  (§6); test strategy for the read/write I/O seams + injected-vs-real picker
  clarity (§8).
- 2026-06-25 — cold-eyes loop 4 fixes (the big one): §6 was reinventing config-dir
  resolution + an atomic write that **already ship** as `ConfigPath::getConfigDir`
  / `getConfigFile` and `AtomicWrite::writeFile` — rewrote §6/§8 to **reuse** them
  (drops the invented `user_config.{h,cpp}` / `configDirFrom` / `userConfigDir`;
  fixes the `%APPDATA%`-vs-`%LOCALAPPDATA%` and "disabled"-vs-temp-fallback
  contradictions; folds in the already-added `VESTIGE_CONFIG_DIR` override).
  Reworded the headless guard's Rule-5 tag (§4) and reordered this log.
- 2026-06-25 — cold-eyes loop 5 fixes (precision): cite global Rule 3 (not 9) for
  "reuse before rewriting" (§6); `writeRememberedAssetPath` returns true on
  `DirFsyncFailed` too (file is written), false only on real write failures, and
  drops the redundant `create_directories` (`AtomicWrite::writeFile` makes parent
  dirs) (§6); parser returns the first `assets.path=` value (§6); test hook is
  `AtomicWrite::TestHooks::forceNextWriteFailure` under `VESTIGE_TEST_HOOKS` (§8);
  dropped the loose Rule-5 tag on the headless guard (§4); §1 notes an explicit
  wrong override isn't routed to the picker.
- 2026-06-25 — cold-eyes loop 6 — **CONVERGED.** Lanes A (accuracy) + C
  (dependency/cross-ref) clean; lane B one trivial LOW (name the test file, §8) —
  fixed. Findings decayed CRITICAL→HIGH→MEDIUM→precision→one naming nit across the
  six loops. Signed off.
