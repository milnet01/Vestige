# Phase 10 — Settings System (Design Doc)

**Status:** Approved 2026-04-22 — all eight §12 questions signed off; implementation proceeds through slices 13.1 – 13.5.
**Roadmap item:** Phase 10 Features → "Settings system (resolution, quality presets, keybindings)".
**Scope:** A persistent, user-editable settings store covering Display / Audio / Controls / Gameplay / Accessibility, wired into the Settings-menu chrome shipped in Phase 10 slice 12.2, with robust load / validate / migrate / atomic-save lifecycle. Ships as slices 13.1 – 13.5.

---

## 1. Why this doc exists

The Settings-menu **chrome** (header / sidebar / content frame / Apply-Revert-Restore footer) is already live (`engine/ui/menu_prefabs.cpp::buildSettingsMenu`, slice 12.2). What's missing is the **persistence layer** underneath it: a JSON file that reads on startup, applies to every subsystem that carries user-tunable state, and writes atomically on Apply. Nothing in the engine today persists UI scale, high-contrast, keybindings, audio mixer gains, or any accessibility setting — a user who toggles reduced-motion loses their choice on every launch.

This doc's job is to specify that lifecycle, not to design per-category controls (those are per-game concerns the engine provides primitives for).

---

## 2. What already exists (inventory)

Surveyed by an exploration pass on 2026-04-22.

| Area | Location | State |
|---|---|---|
| **XDG config path helper** | `editor/recent_files.cpp::getConfigDir` | Complete on Linux. Uses `$XDG_CONFIG_HOME` → `$HOME/.config` → `/tmp` fallback, appends `/vestige`. Windows path not yet implemented — extend in slice 13.1. |
| **nlohmann_json patterns** | 46 callsites. Key references: `utils/entity_serializer.cpp`, `formula/formula_preset.cpp`, `editor/recent_files.cpp` | Complete. `JsonSizeCap::loadJsonWithSizeCap()` is the canonical load helper; `j.value(key, default)` for optional fields and try/catch for partial-load recovery are the established patterns. |
| **UI accessibility state** | `UISystem::{setScalePreset, setHighContrastMode, setReducedMotion}` + getters | Complete. Public API ready to serialize. Warning: these three call `rebuildTheme()` which discards per-field theme overrides — order matters (apply accessibility before custom theme tweaks). |
| **Post-process accessibility** | `engine/accessibility/post_process_accessibility.h` (`PostProcessAccessibilitySettings`) | Plain struct with `depthOfFieldEnabled`, `motionBlurEnabled`, `fogEnabled`, `fogIntensityScale`, `reduceMotionFog`, etc. Applied via `Renderer::setPostProcessAccessibility`. |
| **Color-vision filter** | `engine/renderer/color_vision_filter.h` | Enum preset + calibration. |
| **Photosensitive safety** | `engine/accessibility/photosensitive_safety.h` | Settings struct with strobe-suppression knobs. |
| **Fog accessibility** | `engine/renderer/fog.h` (`FogAccessibility` — slice 11.9) | Part of the post-process-accessibility block. |
| **Audio mixer** | `engine/audio/audio_mixer.h` | `std::array<float, 6> busGain` (Master / Music / Voice / Sfx / Ambient / UI). No setter — settings must mutate the array directly. |
| **Subtitle preferences** | `engine/ui/subtitle.h` (`SubtitleSizePreset`) + `SubtitleQueue::{sizePreset, setSizePreset}` | Complete. Size preset + enable/disable belong in settings. |
| **Input bindings** | `engine/input/input_bindings.h` (`InputActionMap`) | Complete. Dual-snapshot design: `m_actions` + `m_defaults` parallel arrays; `resetToDefaults()` snaps back. Missing: JSON serialisation. |
| **Window config** | `engine/core/window.h` (`WindowConfig{title, width, height, isVsyncEnabled}`) | **Immutable after construction.** Resolution / vsync / fullscreen can't be changed at runtime today. This is a blocker for runtime Apply — see §5 blockers. |

**What is actually missing:**

1. **No settings file format, no load / save lifecycle, no schema version.** `settings.json` does not exist.
2. **No single "apply settings to engine" entry point.** Each subsystem knows how to accept its own settings, but no orchestrator walks the engine in the right order.
3. **No runtime video-settings path.** `Window` is immutable; changing resolution / vsync / fullscreen today requires restarting the process.
4. **No atomic-write utility.** `RecentFiles::save()` writes directly with `ofstream` — acceptable for a recent-files list that regenerates on use, not for settings where a crash mid-save must not brick the user's preferences.
5. **No migration / version-drift handling.** Every existing loader expects exact schema match.

Slices 13.1 – 13.5 close these five gaps.

---

## 3. CPU / GPU placement (CLAUDE.md Rule 12)

Every piece of this subsystem runs on the **CPU**. JSON parsing is pure text + tree traversal; atomic-write is file I/O; applying settings is single-threaded state mutation. No per-pixel / per-vertex work, no per-frame hot path, no scale signal that would justify GPU offload. Placement is inherited from the existing serialisers (scene_serializer, formula_preset, recent_files) and is correct.

No piece of the design migrates to GPU. If future work adds a settings-driven shader variant selector, that selector's *evaluation* remains CPU (branch on bool, bind shader); the shader work itself is already on GPU as part of the render pipeline it belongs to.

---

## 4. File format + schema

### 4.1 Format decision — JSON

Settings ship as a single **JSON** file with a root-level `schemaVersion: integer`. Decision made against INI / TOML / binary:

- **JSON** — matches every other persistence callsite in the engine (46 files). Diffable by the user, editable by modding tooling, supported natively by `nlohmann_json` (already a dependency). O3DE settings registry uses JSON for the same reasons.
- **INI** — Unreal's `GameUserSettings.ini`. Rejected because the engine has no INI infrastructure and the schema has nested sub-objects (per-bus gains, per-action key bindings) that INI handles awkwardly.
- **TOML** — no engine uses it widely; would mean adding another parser.
- **Binary** — rejected. Undebuggable, un-griefable, un-moddable; cuts off the ticket-triage pipeline ("please send me your settings.json").

### 4.2 Location

- **Linux:** `$XDG_CONFIG_HOME/vestige/settings.json` (default `~/.config/vestige/settings.json`). Reuses `RecentFiles::getConfigDir()` directly.
- **Windows:** `%LOCALAPPDATA%\Vestige\settings.json`. Slice 13.1 extends `getConfigDir()` with a `SHGetKnownFolderPath(FOLDERID_LocalAppData, …)` branch under `#ifdef _WIN32`. Matches Unreal's convention.
- **macOS** (if we ever port): `~/Library/Application Support/Vestige/settings.json`. Not implemented; design leaves room.

XDG spec reference: [freedesktop.org basedir latest](https://specifications.freedesktop.org/basedir/latest/).

### 4.3 Schema (slice 13.1)

```json
{
  "schemaVersion": 1,
  "display": {
    "windowWidth": 1920,
    "windowHeight": 1080,
    "fullscreen": false,
    "vsync": true,
    "qualityPreset": "high",
    "renderScale": 1.0
  },
  "audio": {
    "busGains": {
      "master": 1.0,
      "music":  0.8,
      "voice":  1.0,
      "sfx":    0.9,
      "ambient":0.7,
      "ui":     0.7
    },
    "hrtfEnabled": true
  },
  "controls": {
    "mouseSensitivity": 1.0,
    "invertY": false,
    "gamepadDeadzoneLeft":  0.15,
    "gamepadDeadzoneRight": 0.10,
    "bindings": [
      {
        "id": "move_forward",
        "primary":   {"device": "keyboard", "scancode": 17},
        "secondary": {"device": "none"},
        "gamepad":   {"device": "gamepad",  "scancode": 11}
      }
    ]
  },
  "gameplay": {
    "values": {
      "difficulty": "normal",
      "fovDegrees": 90
    }
  },
  "accessibility": {
    "uiScalePreset":   "1.5x",
    "highContrast":    false,
    "reducedMotion":   false,
    "subtitlesEnabled":true,
    "subtitleSize":    "medium",
    "colorVisionFilter": "none",
    "photosensitiveSafety": {
      "strobeGuardEnabled": true,
      "flashThresholdHz":   3.0
    },
    "postProcess": {
      "depthOfFieldEnabled": true,
      "motionBlurEnabled":   true,
      "fogEnabled":          true,
      "fogIntensityScale":   1.0,
      "reduceMotionFog":     false
    }
  }
}
```

`gameplay.values` is an **untyped string→JsonValue map** — the engine doesn't dictate gameplay settings; each game project reads what it wrote.

### 4.4 Keybinding wire format

Per the research pass, keybindings store **scan codes**, not keycodes. GLFW exposes `glfwGetKeyScancode(key)` and passes a scancode through `GLFWkeyfun`, so the wire format is `{device: "keyboard"|"mouse"|"gamepad"|"none", scancode: int}`. UI labels localise via the existing `bindingDisplayLabel()` helper. This is the Godot `physical_keycode` convention and it keeps WASD in the same physical spot across AZERTY / Dvorak layouts.

Reference: [Godot InputEventKey — physical_keycode](https://docs.godotengine.org/en/stable/classes/class_inputeventkey.html).

### 4.5 Migration

Chained migration functions, not discard-on-error. Root-level `schemaVersion` drives it:

```cpp
// settings_migration.cpp
void migrate_v1_to_v2(nlohmann::json& j);
void migrate_v2_to_v3(nlohmann::json& j);

bool migrate(nlohmann::json& j)
{
    int v = j.value("schemaVersion", 1);
    while (v < kCurrentSchemaVersion)
    {
        switch (v)
        {
            case 1: migrate_v1_to_v2(j); break;
            case 2: migrate_v2_to_v3(j); break;
            default: return false;
        }
        v = j.value("schemaVersion", v + 1);
    }
    return true;
}
```

Unknown fields are **ignored on load** (forward-compat: a newer build's settings.json loaded by an older build must not crash). Missing fields get **defaults from the struct initialiser** (backward-compat: old settings.json loaded by a newer build gets the new field at its default).

Discard-on-parse-error is the last-ditch fallback only, and it logs a warning + preserves the failing file as `settings.json.corrupt` so the user can recover it manually.

Pattern reference: [MongoDB schema-versioning](https://www.mongodb.com/docs/manual/data-modeling/design-patterns/data-versioning/schema-versioning/).

### 4.6 Validation

Validate at load time against allowlists, never at consumer site:

- **Resolution** — enumerate `glfwGetVideoModes()` before accepting stored `windowWidth × windowHeight`. Unknown mode snaps to the default video mode.
- **Quality preset** — allowlist `{"low", "medium", "high", "ultra", "custom"}`. Unknown snaps to `"medium"`.
- **Bus gains** — clamp to `[0.0, 1.0]`.
- **Render scale** — clamp to `[0.25, 2.0]`.
- **Mouse sensitivity** — clamp to `[0.1, 10.0]`.
- **Deadzones** — clamp to `[0.0, 0.9]`.
- **Keybindings** — drop entries whose `id` doesn't match any registered `InputAction`. Log a warning.
- **Enum strings** — scale preset / subtitle size / color-vision filter map to their enum values; unknown strings fall back to the default.

Pattern reference: Unreal's `UGameUserSettings::ValidateSettings()` calls `IsScreenResolutionValid()` and resets invalid modes.

---

## 5. Blockers (inventoried before slicing)

Every blocker below needs either a design answer or a "punt to slice N" note.

1. **`Window` is immutable after construction.** Resolution / vsync / fullscreen can't change at runtime today.
   **Answer:** Slice 13.2 adds `Window::setVideoMode(width, height, fullscreen, vsync)` — for resolution + fullscreen, reconstruct the GLFW window preserving the GL context via `glfwCreateWindow` share; for vsync, call `glfwSwapInterval(vsync ? 1 : 0)` (no reconstruct needed). **Accept one-frame GL context validity gap for resolution changes** — screenshot support already handles this pattern.

2. **`AudioMixer::busGain` has no setter.** The array is public but unprotected.
   **Answer:** Slice 13.3 adds `AudioMixer::setBusGain(AudioBus, float)` + `getBusGain(AudioBus)`. Settings calls through the setter; existing direct-array callsites migrate in the same slice.

3. **`InputActionMap` requires actions be registered before bindings load.** A settings JSON can't define new actions, only rebind existing ones.
   **Answer:** Correct by design. Game projects register actions in `initialize()` before `Settings::load()` is called. Slice 13.4 documents the init order contract; an action present in settings but not registered logs a warning and is dropped.

4. **`UITheme` is rebuilt on every accessibility toggle, clobbering custom overrides.** Applying scale + contrast + reduced-motion three times runs three rebuilds.
   **Answer:** Slice 13.5 adds `UISystem::applyAccessibilityBatch(scale, contrast, motion)` that takes all three at once and runs a single `rebuildTheme()` call. Not a correctness bug — a perf + custom-theme-preservation improvement.

5. **Subsystem init order during `Settings::applyToEngine`.** Video (`Window`) changes can invalidate the GL context; audio / UI changes must happen after GL is up. If we re-apply on every `Apply` click, we re-traverse in the right order.
   **Answer:** `Settings::applyToEngine(Engine&)` walks subsystems in a fixed order: video → audio → UI accessibility → input bindings → render post-process → gameplay. Documented as a contract in `settings.h`.

6. **Apply vs. Revert vs. Restore Defaults lifecycle.** What does the Apply button commit? What does Revert undo to?
   **Answer:** The Settings UI holds two state copies: `m_applied` (last-Apply state, equal to disk state) and `m_pending` (the edits in flight). Apply copies `m_pending → m_applied`, writes to disk, and calls `applyToEngine`. Revert copies `m_applied → m_pending`. Restore Defaults replaces `m_pending` with the engine's baseline (via `resetToDefaults()` on each subsystem). This matches the design's UI chrome mental model (dirty indicator = `m_pending != m_applied`).

7. **Restore Defaults + accessibility survival.** If a partially-sighted user clicks Restore Defaults, should they lose their 2.0× UI scale?
   **Answer:** No. Split into two buttons: **Restore Defaults (this tab)** (resets only the tab's subsystem) and **Restore All Defaults** (resets every tab *except* Accessibility). Accessibility tab has its own "Restore accessibility defaults" button that operates only when clicked explicitly. This is a design choice, not an industry standard — [Apple Community — restore default accessibility](https://discussions.apple.com/thread/252445088) documents that no OS has a one-click accessibility reset either, for the same reason. Rationale recorded in the code comment on `Settings::restoreAllDefaults()`.

---

## 6. Lifecycle

### 6.1 Startup

```cpp
// Engine::initialize, after all ISystems are registered + initialized
Settings settings;
const auto path = Settings::defaultPath();  // XDG / %LOCALAPPDATA%
settings.loadFromDisk(path);                // Parse + migrate + validate.
settings.applyToEngine(*this);              // Walk subsystems in fixed order.
m_settings = std::move(settings);
```

On first launch (file missing) or parse failure (file corrupt), `loadFromDisk` returns a default-constructed `Settings` and logs. The failing file, if present, is moved to `settings.json.corrupt` for manual recovery.

### 6.2 Runtime Apply

```cpp
// User clicks Apply in the Settings UI
m_pending.applyToEngine(engine);
m_applied = m_pending;
m_applied.saveAtomic(Settings::defaultPath());
```

Save is **atomic** per §7.

### 6.3 Shutdown

No auto-save on shutdown. The design requires explicit Apply — a user who edits without Apply and quits loses the edits. This matches Unreal / Godot. Apply-on-close would surprise a user who was exploring settings without committing.

---

## 7. Atomic write

POSIX write-temp-rename-fsync-dir dance, matching the `write-file-atomic` / `atomic-write-file` industry implementations:

1. Write serialised JSON into `settings.json.tmp` in the same directory.
2. `fsync(tmp_fd)` to flush file contents.
3. `rename("settings.json.tmp", "settings.json")` — atomic on POSIX same-filesystem; `MoveFileExA(MOVEFILE_REPLACE_EXISTING)` on Windows.
4. `fsync(dir_fd)` on POSIX to persist the directory entry itself. Without step 4, a crash can leave the old file after reboot — `rename(2)` does not guarantee durability of the rename alone.

Slice 13.1 ships this as a standalone utility (`utils/atomic_write.{h,cpp}`) so save-games and any future persistence layer can reuse it.

Reference: [Calvin Loncaric — How to Durably Write a File on POSIX](https://calvin.loncaric.us/articles/CreateFile.html), [rename(2) man page](https://man7.org/linux/man-pages/man2/rename.2.html).

---

## 8. Slice breakdown

| Slice | Title | Complexity | Ships |
|---|---|---|---|
| **13.1** | Settings primitive + atomic-write utility | M | `engine/core/settings.{h,cpp}` (Settings struct, schema enum, `loadFromDisk` / `saveAtomic`, migration scaffolding, validation). `engine/utils/atomic_write.{h,cpp}` (tmp-fsync-rename-fsync-dir). Windows branch in `RecentFiles::getConfigDir` (or move the helper to `utils/config_path`). Unit tests over every validation + migration path. **No engine wiring yet.** |
| **13.2** | Video runtime apply | M | `Window::setVideoMode`. Settings wires `display` block → Window on apply. Screenshot test to confirm GL context survives a video-mode change. |
| **13.3** | Audio + accessibility apply | S | `AudioMixer::setBusGain` + `getBusGain`. Settings wires `audio` block → AudioMixer, and `accessibility` block → UISystem / Renderer / PhotosensitiveSafety / ColorVisionFilter. `UISystem::applyAccessibilityBatch` lands here to avoid triple-rebuild. |
| **13.4** | Input bindings JSON | M | `InputActionMap::toJson` / `fromJson` (scan-code wire format). Settings wires `controls.bindings` on load; game code's `addAction` registration contract documented. Rebind UI wiring is per-game (slice 13.5 provides the per-widget hook). |
| **13.5** | Settings UI wiring + Restore Defaults | M | Per-category control wiring into the Settings chrome (buildSettingsMenu add-ons). Apply / Revert / Restore Defaults buttons wire to `Settings::{apply, revert, restoreAllDefaults}`. Accessibility tab gets its own separate "Restore accessibility defaults" button. |

Total: ~5 days of session time. Each slice is individually testable and commitable.

---

## 9. Test strategy

- **13.1** — Parse valid JSON → assert struct equality. Parse missing file → default struct. Parse bad JSON → default struct + `.corrupt` sidecar written. Migration: write a `v0`-style file, load, assert `schemaVersion == current`. Validation: out-of-range gains clamp, unknown quality preset snaps to Medium, phantom keybinding id drops with warning. Atomic write: write file, kill fsync mid-call via fault injection (if feasible), assert old file still present; otherwise assert that `saveAtomic` leaves no `.tmp` on success. Target ~30 tests.

- **13.2** — Build a headless window, call `setVideoMode`, assert `glfwGetFramebufferSize` reports the new size. Toggle vsync, assert `glfwSwapInterval` was called. Skip fullscreen toggling in CI (no display in headless) — gate via `--visual` flag. Target ~8 tests.

- **13.3** — Load settings with `audio.busGains.master = 0.5`, call `applyToEngine`, assert `AudioMixer::getBusGain(Master) == 0.5`. Same for each accessibility flag → UISystem / Renderer getters. Target ~12 tests.

- **13.4** — Round-trip InputActionMap through JSON, assert `m_actions == original`. Load JSON with phantom action id, assert it's dropped + warning logged. Load with scancode-17 for `move_forward`, assert `InputActionMap::findAction("move_forward")->primary.scancode == 17`. Target ~10 tests.

- **13.5** — Panel-open test. Restore Defaults on Display tab → only Display reset, others untouched. Restore All Defaults → every tab except Accessibility reset. Accessibility's own "Restore accessibility defaults" → only accessibility reset. Target ~8 tests.

Total: ~68 new tests.

---

## 10. Performance targets

Load happens once at startup; save happens on Apply (user-triggered, not per-frame). Both are well under the 60 FPS frame budget even in the worst case:

| Operation | Target | Justification |
|---|---|---|
| `loadFromDisk` | < 5 ms | nlohmann_json over a ~10 KB file is microseconds; most of 5 ms is `open` + `stat` + dir traversal. |
| `saveAtomic` | < 20 ms | Two `fsync` syscalls dominate; each is ~5–10 ms on a warm SSD, more on a cold spinning disk. This runs off the render thread — synchronous is fine for a settings commit. |
| `applyToEngine` | < 50 ms | Video mode change is the slow path (GL context reconstruct). Non-video changes are single setter calls. Runs on Apply only. |
| Per-frame | 0 ms | Settings does nothing per-frame. |

No benchmark harness needed.

---

## 11. Explicit non-goals

To keep this phase bounded:

- **Per-game gameplay settings schema.** `gameplay.values` is an opaque string→JsonValue map. Each game project reads what it wrote; the engine doesn't dictate gameplay knobs.
- **Save-game persistence.** Settings is session-wide; save-games are per-scene. Different system, different file.
- **Cloud sync / Steam Cloud.** Engine-local only; Steam Cloud integration is a later phase.
- **Per-user accounts / profiles.** Single `settings.json` per install. Multi-profile is a follow-up if needed.
- **In-UI rebind capture.** Slice 13.4 ships the JSON schema + apply path; per-widget "press a key to rebind" capture is slice 13.5's UI-surface job and is per-game.
- **Localised display names for bindings.** Existing `bindingDisplayLabel()` returns English. Localisation is its own roadmap bullet.

---

## 12. Open questions for user sign-off

1. **Schema version starting point.** Start at `schemaVersion: 1`, or `0`? Convention across both research subjects is `1` (0-indexed version is rare in settings files). **Proposal: 1.**
2. **Where does `Settings` live?** `engine/core/settings.{h,cpp}` (alongside `engine` / `window` / `input_manager`) reads cleanly. Alternative: `engine/systems/settings_system.{h,cpp}` as an ISystem. **Proposal: `engine/core/settings.{h,cpp}`** — Settings isn't a per-frame system, and wrapping it in ISystem adds noise; plain struct + free functions match `FormulaPreset` / `RecentFiles`.
3. **Gameplay section — untyped or schema-ized?** Current proposal is untyped `string→JsonValue`. Alternative: let each game project supply a schema-validated section. **Proposal: untyped.** Simpler; games that want validation can validate post-load.
4. **Apply-on-close?** Current proposal is "no — require explicit Apply". **Proposal: no.**
5. **Split "Restore Defaults" + accessibility policy.** Accessibility tab gets its own reset button; "Restore All Defaults" spares accessibility. **Proposal: confirmed** — document the rationale in code.
6. **Scan codes on disk?** Yes — GLFW scancodes, as Godot does for `physical_keycode`. **Proposal: confirmed.** Alternative (layout-aware keycodes) loses WASD across AZERTY.
7. **Slice granularity.** Five slices (13.1–13.5). Alternative: collapse 13.2–13.4 into one "apply block" slice. **Proposal: five** — each slice is independently testable and commitable; video especially warrants its own slice because the Window immutability fix is invasive.
8. **Config-path helper factoring.** Move `RecentFiles::getConfigDir` out of `editor/recent_files.cpp` into `engine/utils/config_path.{h,cpp}` so non-editor consumers (Settings) can use it? **Proposal: yes, in slice 13.1** — lightweight refactor, unblocks reuse.

---

**Status:** Approved 2026-04-22. All eight §12 questions accepted as proposed. Slice 13.1 implementation begins.

### Sign-off log

- **2026-04-22** — User accepted all eight proposals in §12 without modification:
  1. Schema version starts at `1`.
  2. `Settings` lives at `engine/core/settings.{h,cpp}` as a plain struct + free functions (not an ISystem).
  3. `gameplay.values` stays untyped (`string → JsonValue`); per-game validation is the game's responsibility.
  4. No Apply-on-close; explicit Apply is required.
  5. Split Restore Defaults; "Restore All Defaults" spares Accessibility; Accessibility tab has its own reset button.
  6. Keybindings stored as GLFW scancodes on disk (Godot `physical_keycode` convention).
  7. Five slices (13.1 – 13.5) kept separate; 13.2 – 13.4 not collapsed.
  8. `RecentFiles::getConfigDir` factored to `engine/utils/config_path.{h,cpp}` in slice 13.1.
