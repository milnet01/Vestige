# Vestige Engine Changelog

All notable engine-level changes are documented here. Per-tool changelogs
live alongside each tool (`tools/audit/CHANGELOG.md`,
`tools/formula_workbench/CHANGELOG.md`).

The engine version tracks phase milestones, not SEMVER. Pre-1.0 commits
may change any interface without notice.

## [Unreleased]

### 2026-04-22 Phase 10 — Settings editor panel (slice 13.5b)

ImGui editor panel wrapping the `SettingsEditor` orchestrator.
User-facing Settings UI now lives in the editor — reachable via
`Help → Settings...`. Slice 13.5c adds click-to-rebind capture;
slice 13.5d wires the live-apply sinks to the real subsystems.

- `engine/editor/panels/settings_editor_panel.{h,cpp}` — five-tab
  panel (Display / Audio / Controls / Gameplay / Accessibility)
  + footer with per-category Restore + Restore All + Revert +
  Apply buttons and a live dirty indicator.
- Widgets per tab:
  - **Display**: resolution inputs, fullscreen / vsync checkboxes,
    quality preset combo, render-scale slider, Restore button.
  - **Audio**: six bus-gain sliders (Master / Music / Voice / SFX
    / Ambient / UI), HRTF toggle, Restore button.
  - **Controls**: mouse sensitivity, invert Y, gamepad left +
    right deadzone sliders. Three-column keybinding **table**
    (Action / Primary / Secondary / Gamepad) with `(Rebind capture
    lands in slice 13.5c)` placeholder note. Restore button.
  - **Gameplay**: doc-stub; game projects mutate
    `SettingsEditor::pending().gameplay` directly for their own UI.
    Restore button.
  - **Accessibility**: UI scale combo, high-contrast + reduced-motion
    + subtitles-enabled checkboxes, subtitle-size combo, color-vision
    filter combo, post-process accessibility toggles (DoF / motion
    blur / fog + intensity slider), photosensitive safe-mode
    section (shown when enabled: max flash alpha, shake scale,
    max strobe Hz). Restore button.
- Footer: dirty indicator (`All changes saved.` vs `Unsaved changes.`),
  `Restore All Defaults`, `Revert` (disabled when clean), `Apply`
  (disabled when clean, saves via `SettingsEditor::apply`).
- Every widget mutation routes through `SettingsEditor::mutate`
  so the live-apply contract holds once sinks are wired (13.5d).
- `Editor::wireSettingsEditorPanel(editor*, inputMap*, path)` +
  member `m_settingsEditorPanel`. `Help → Settings...` entry opens
  it. `Help → First-Run Wizard` also added so users can rerun
  onboarding without digging through settings.
- `Engine` owns the `SettingsEditor` (std::unique_ptr). Constructs
  it after loading `Settings` from disk; passes to the editor
  panel via `wireSettingsEditorPanel`. Apply targets are null in
  this slice (panel still works end-to-end for persistence, restore,
  revert, apply; live-apply plumbing is the next slice).

No new tests — the orchestrator tests from 13.5a cover the mutation
surface, and ImGui panels are hard to unit-test cleanly without
an ImGui context (the panel is a thin wrapper that forwards widget
events into `SettingsEditor::mutate` calls, which are already
exhaustively tested). Visual verification on the editor's
`Help → Settings...` menu entry is the acceptance gate.

Full suite 2661 passing (1 pre-existing skip).

Next: slice 13.5c — click-to-rebind modal in the Controls tab, +
slice 13.5d — live-apply sink wiring for audio / ui / renderer
subsystems.

### 2026-04-22 Phase 10 — SettingsEditor orchestrator (slice 13.5a)

Final slice of the Phase 10 settings chain begins. Ships the
`SettingsEditor` state machine that sits behind every UI — the
load-bearing piece — and is fully headless-testable.

- `engine/core/settings_editor.{h,cpp}` — `SettingsEditor`:
  - Owns `m_applied` (last-committed, matches disk + subsystems)
    and `m_pending` (user's in-progress edits).
  - **Live-apply semantics**: every `mutate()` pushes the modified
    `m_pending` through every configured sink so users see the
    change immediately. `apply()` performs only the persistence
    step (save to disk + advance `m_applied`); a failed write
    leaves the editor still dirty so the user can retry / revert
    without silent data loss.
  - **Per-category restore granularity**: five dedicated reset
    methods (`restoreDisplayDefaults`, `restoreAudioDefaults`,
    `restoreControlsDefaults`, `restoreGameplayDefaults`,
    `restoreAccessibilityDefaults`) plus `restoreAllDefaults`.
    Granular so a user's `2.0×` scale + `high-contrast` survives
    a single-category reset.
  - **Onboarding + schemaVersion preservation across Restore All**:
    a full reset does NOT clear `onboarding.hasCompletedFirstRun`
    or revert `schemaVersion`, so clicking "Restore All Defaults"
    cannot accidentally re-trigger the first-run wizard or break
    the next load's migration path.
  - `ApplyTargets` struct holds raw pointers to every apply sink
    (display / audio / hrtf / ui-accessibility / renderer / subtitle
    / photosensitive / input map). Any target may be null — caller
    wires only what they want driven by this editor. Non-owning.
  - `revert()` re-pushes `m_applied` through every sink so live
    preview rolls back. `forceLiveApply()` is an escape hatch for
    callers that want the editor to drive subsystems from scratch.
  - `restoreControlsDefaults` / `restoreAllDefaults` also call
    `InputActionMap::resetToDefaults()` when one is attached, so
    the live rebind state follows the struct reset.

Tests: 13 new in `tests/test_settings.cpp`:
- Initial state matches applied, not dirty.
- Mutate diverges pending, marks dirty.
- Mutate pushes through every configured sink once per call.
- Apply commits + persists + clears dirty; reloading from disk
  round-trips the values.
- Apply with failed write keeps editor dirty (retry path).
- Revert restores from applied + re-pushes through sinks.
- Per-category restores are isolated (only their section resets).
- Restore All preserves onboarding + schemaVersion.
- Per-category restore is live-applied through sinks.
- Dirty tracking is correct across mutate/apply/revert cycles.
- RestoreControls resets the attached InputActionMap's bindings.

Full suite 2661 passing (1 pre-existing skip).

Next (slice 13.5b): ImGui `SettingsEditorPanel` wiring the orchestrator
to per-category widgets. Slice 13.5c adds the 3-column keybinding
rebind dialog.

### 2026-04-22 Phase 10 — Input bindings extract + apply (slice 13.4)

Bridges `Settings::controls.bindings` (the on-disk
`std::vector<ActionBindingWire>`) to the in-memory `InputActionMap`.
Second-last slice of the Phase 10 settings chain.

- `extractInputBindings(map) → std::vector<ActionBindingWire>` —
  serialises every registered action in `map.actions()` order.
  Covers all three slots (primary / secondary / gamepad). Device
  → wire-string mapping: Keyboard → `"keyboard"`, Mouse → `"mouse"`,
  Gamepad → `"gamepad"`, None → `"none"`. Empty `map` extracts
  to an empty vector.
- `applyInputBindings(wires, map)` — reverse direction. Enforces
  the init-order contract documented in
  `PHASE10_SETTINGS_DESIGN.md`: game code registers actions before
  `Settings::load`; an id in `wires` that doesn't resolve to a
  registered action is **dropped with a logged warning** (prevents
  typos in a hand-edited `settings.json` from creating ghost
  actions, and protects against stale saves referencing actions
  removed from a newer engine build). Actions registered on the
  map but absent from `wires` keep their current bindings (no
  clobbering to defaults).
- Unbound-binding normalisation: a wire with `device == "none"` or
  `scancode < 0` (either condition) collapses to the fully-unbound
  `InputBinding::none()` on apply, so `isBound()` stays consistent.

Wire-format limitation documented inline: `scancode` currently
carries the in-memory GLFW *key code* rather than a true scancode.
Layout-preserving scancode translation (WASD stable across AZERTY /
Dvorak) needs `glfwGetKeyScancode` + a reverse table and lands in
a follow-on slice. Keep-values-identical means 13.4's round-trip
is testable without a GLFW context and matches what users see when
hand-editing `settings.json`.

Tests: 10 new in `tests/test_settings.cpp` (design-doc target: 10):
- Extract emits every action in insertion order.
- Extract round-trips device strings across all four device enum
  values.
- Extract preserves all three binding slots.
- Apply updates bindings of registered actions (remap flow).
- Apply drops phantom ids without auto-registering — map size
  stays at its registered count.
- Apply preserves actions that are registered but absent from the
  wire list (no clobber).
- Unknown device string falls back to None.
- Negative scancode on an otherwise-valid wire collapses to fully
  unbound.
- Full extract → apply round-trip is lossless across all three
  device kinds.
- End-to-end via `Settings::controls.bindings` — a wire populated
  through the settings struct reaches a registered map's action
  intact.

Full suite 2648 passing (1 pre-existing skip). Slice 13.4 complete.

Next: slice 13.5 — Settings UI wiring (per-category control widgets
into `buildSettingsMenu`) + Apply / Revert / Restore Defaults buttons.

### 2026-04-22 Phase 10 — Renderer + subtitle + HRTF + photosensitive apply (slice 13.3b)

Completes slice 13.3 — adds the four remaining accessibility / audio
apply paths that 13.3a deferred. Every `Settings::accessibility`
field and the `audio.hrtfEnabled` bool now have a typed apply sink
+ production forwarder + pure-function orchestrator.

- `RendererAccessibilityApplySink` — color vision mode +
  post-process (DoF / motion blur / fog + fog intensity + reduce-motion
  fog) pushed through `Renderer` in one call per field group.
  `applyRendererAccessibility` translates the wire-format colour-vision
  string (`"none"` / `"protanopia"` / `"deuteranopia"` / `"tritanopia"`)
  to the typed `ColorVisionMode` enum, maps `PostProcessAccessibilityWire`
  → `PostProcessAccessibilitySettings`.
- `SubtitleApplySink` + `SubtitleQueueApplySink` — `subtitlesEnabled`
  bool + `subtitleSize` string (`"small"` / `"medium"` / `"large"` /
  `"xl"`). The `enabled` flag is held on the sink until slice 14's UI
  wiring queries it at render time (SubtitleQueue doesn't currently
  expose an enable toggle; its owner drains it regardless).
- `AudioHrtfApplySink` — `audio.hrtfEnabled` bool → `HrtfMode`.
  `true` → `HrtfMode::Auto` (driver decides based on headphones
  detection), `false` → `HrtfMode::Disabled` (force off).
- `PhotosensitiveApplySink` — `photosensitiveSafety.enabled` bool +
  `PhotosensitiveLimits` struct (maxFlashAlpha / shakeAmplitudeScale
  / maxStrobeHz / bloomIntensityScale). No production-concrete impl
  yet — the caps are consumed by individual effect-site call sites
  (`clampFlashAlpha`, `clampShakeAmplitude`, …), so "applying" means
  writing to a central engine-side store. The abstract sink + pure
  apply function land now; the engine store lands when the first
  effect that reads from it needs to.

Tests: 9 new in `tests/test_settings.cpp` — every colour-vision
string → enum (4 values), unknown fallback, post-process wire-field
forward, every subtitle size → enum (4 values), subtitle enabled
forward, HRTF bool → HrtfMode (both polarities), photosensitive
enabled + limits forward, end-to-end JSON → fromJson → applyRenderer
round-trip, SubtitleQueue production sink mutates state. Full suite
2638 passing (1 pre-existing skip).

Slice 13.3 is now complete end-to-end. Next: slice 13.4 — input
bindings toJson/fromJson with scancode wire format.

### 2026-04-22 Phase 10 — Audio + UI accessibility apply (slice 13.3a)

Continues the Phase 10 settings chain. Adds runtime apply paths for
the audio block and the UI-side accessibility triad (scale /
contrast / motion). Renderer-side accessibility (color-vision
filter, post-process, photosensitive safety) + HRTF + subtitles are
deferred to a follow-on slice — keeps this change focused on the
most-used knobs.

- `AudioMixer::setBusGain(AudioBus, float)` + `getBusGain(AudioBus)`
  centralise the [0, 1] clamp policy. `audio_panel.cpp` migrated
  off direct `busGain[i]` array access so the clamp is consistent.
- `UISystem::applyAccessibilityBatch(scale, contrast, motion)`
  coalesces the three individual setters (each of which triggers a
  `rebuildTheme`) into one rebuild. Equivalent outcome; one-third
  the work per apply.
- `AudioApplySink` + `AudioMixerApplySink` in `core/settings_apply.{h,cpp}`.
  `applyAudio(AudioSettings, AudioApplySink&)` pushes all six bus
  gains in enum order. HRTF stays on `AudioEngine` and is a
  follow-on; this sink covers only `AudioMixer`.
- `UIAccessibilityApplySink` + `UISystemAccessibilityApplySink`
  sibling. `applyUIAccessibility(AccessibilitySettings, sink)`
  translates the wire-format scale-preset string (`"1.0x"` /
  `"1.25x"` / `"1.5x"` / `"2.0x"`) to the typed `UIScalePreset`
  enum — unknown strings fall back to 1.0× consistent with the
  Settings validation policy.

Tests: 12 new in `tests/test_settings.cpp` (design-doc target: 12):
- AudioMixer API: clamp policy + getBusGain is raw not master-product.
- Audio apply: forwards all 6 buses in order, sink actually mutates
  mixer state, sink clamps out-of-range input, JSON → fromJson →
  apply round-trip preserves values.
- UI accessibility apply: string→enum mapping for each preset,
  unknown-string fallback, contrast/motion pass through verbatim,
  batch call is one rebuild not three, full preset-string table
  is pinned, JSON → fromJson → apply round-trip.

Full suite 2629 passing (1 pre-existing skip).

Next: slice 13.3b — renderer accessibility (color vision, post-process,
photosensitive safety) + HRTF + subtitle size. Or slice 13.4
(input bindings toJson/fromJson) if the renderer coupling turns
out to want more design work first.

### 2026-04-22 Phase 10.5 — First-run wizard engine wiring (slice 14.4)

Fourth and final slice of the first-run wizard work. Wires the
wizard into the live engine and editor loops; Phase 10.5 onboarding
flow is now end-to-end.

- `Settings` now loads from `Settings::defaultPath()` during
  `Engine::initialize`. ParseError / MigrationError falls back to
  in-memory defaults + logs a warning; Ok and FileMissing both
  honour the legacy-flag promotion shipped in 14.1.
- `Engine` owns a `Settings m_settings` member, wires the
  `onboarding` sub-struct into the editor's wizard via
  `Editor::wireFirstRunWizard(onboarding*, assetRoot, applyDemoFn)`.
  `applyDemoFn` is a `std::function` that captures `this` and calls
  the private `setupDemoScene()`, so the wizard's "Show me the Demo"
  option works without promoting the method to public.
- `Editor::drawPanels` dispatches the wizard's returned `SceneOp`:
  `ApplyEmpty` → new `applyEmptyScene(scene, resources)` helper
  (one camera, one directional light, one ground plane — Q2
  resolution); `ApplyDemo` → `m_applyDemoCallback()`; `ApplyTemplate`
  → `TemplateDialog::applyTemplate` with the wizard's selected index
  against `allWizardTemplates()`. Each op clears selection + marks
  the file dirty.
- Edge-triggered persistence: `Editor::consumeWizardJustClosed()`
  returns true on the single frame the wizard transitioned from
  open → closed. The engine's frame loop polls it and calls
  `Settings::saveAtomic(Settings::defaultPath())` exactly once,
  so the frame cost in steady state is zero.
- `WelcomePanel::initialize` no longer auto-opens on first launch
  (Q3 resolution). The keyboard-shortcuts reference is retained
  as `Help → Welcome Screen`.

Tests: 4 new in `tests/test_first_run_wizard.cpp` (design-doc target: 3):
wizard auto-opens when onboarding is incomplete, stays closed when
complete, re-opens via `openFromHelpMenu()` after completion with
Step reset to Welcome, and `WelcomePanel::initialize` no longer
auto-opens on a fresh config dir. Full suite 2617 passing (1
pre-existing skip).

Phase 10.5 onboarding is now feature-complete. Remaining Phase 10.5
items (command palette, contextual help, guided tour, preview
thumbnails, etc.) live in the larger Editor Usability Pass; this
closes out the "first-run welcome dialog" roadmap bullet.

### 2026-04-22 Phase 10.5 — Template visibility filter (slice 14.3)

Third slice of the first-run wizard. Adds the template availability
filter so private-repo-only templates stay hidden in public clones.

- `GameTemplateConfig::requiredAssets` — new `std::vector<std::string>`
  field (default empty = always visible). Paths resolved relative
  to the engine's `assetPath`.
- `filterByAvailability(templates, assetRoot)` free function in
  `first_run_wizard.{h,cpp}`. Kept as a free function so the biblical
  walkthrough template landing in the private sibling repo (Q4
  resolution) surfaces on the maintainer's machine without a code
  change — just file presence on disk. Empty `assetRoot` disables
  filtering (useful for tests + early init before asset path is known).
- `FirstRunWizard::initialize(OnboardingSettings*, assetRoot)` — new
  optional asset-root parameter threaded into the panel state. The
  picker's draw path filters both featured and more buckets by
  availability at render time.
- Design contract pinned by `FirstRunWizardFilter.NonWizardMenuListsAllUnconditionally`:
  the `File → New from Template…` menu path (served by
  `TemplateDialog::getTemplates`) does NOT run the filter. Power
  users always see what exists; wizard users see only what works.

Tests: 4 new in `tests/test_first_run_wizard.cpp` (design-doc target):
empty-required-assets always visible, missing-asset hides the template,
present-asset shows it, non-wizard menu lists all 8 unconditionally.
Full suite 2613 passing (1 pre-existing skip).

Next: slice 14.4 — Engine-level wiring (wizard opens at cold-start
when onboarding.hasCompletedFirstRun is false, scene ops dispatched
to applyEmptyScene / setupDemoScene / TemplateDialog::applyTemplate,
WelcomePanel auto-open stripped, Help menu wiring added).

### 2026-04-22 Phase 10.5 — First-run wizard state machine + panel (slice 14.2)

Second slice of the first-run wizard. Ships the panel class and
its pure-function state machine; engine wiring lands in slice 14.4.

- `engine/editor/panels/first_run_wizard.{h,cpp}` — new panel.
  - Pure-function `applyFirstRunIntent(step, onboarding, intent, nowIso)`
    → `FirstRunTransition{step, onboarding, sceneOp, closed}`.
    Zero ImGui or Scene dependencies; fully headless-testable.
    Injecting `nowIso` keeps tests deterministic and production
    uses a `<chrono>` + `gmtime`/`gmtime_s` wall-clock wrapper.
  - Intents: `PickTemplate`, `StartEmpty`, `ShowDemo`, `SkipForNow`,
    `Back`, `FinishWithTemplate`, `CloseAtWelcome`, `CloseAtPicker`.
  - Scene ops: `None`, `ApplyEmpty`, `ApplyDemo`, `ApplyTemplate`.
    Scene construction is delegated to the UI layer — the pure
    function only tags the op so slice 14.4 can dispatch to
    `TemplateDialog::applyTemplate` / `Engine::setupDemoScene` /
    a minimal empty scene.
  - Q7 skip semantics: `SkipForNow` + `CloseAtWelcome` increment
    `skipCount`; on the second skip the wizard auto-completes
    (hasCompletedFirstRun = true, completedAt stamped). Close-at-picker
    routes to Back without bumping skipCount.
  - Q1 template filter: `featuredTemplates()` surfaces the four
    archetype-coverage picks (First-Person 3D, Third-Person 3D,
    2.5D Side-Scroller, Isometric); `moreTemplates()` holds the
    remaining four (Top-Down, Point-and-Click, 2D Side-Scroller,
    2D Shmup) behind a "More templates" expander.
  - Q8 reduced-motion: no transition animation yet (the two steps
    swap instantly), so the question is satisfied vacuously —
    revisit if 14.4 or a follow-on adds any crossfade.
- `TemplateDialog::applyTemplate` promoted from `private` instance
  method to `public static` — it uses no `this` state, and the
  wizard shares the same scene-construction implementation rather
  than duplicating it. No behaviour change.

Tests: 11 new in `tests/test_first_run_wizard.cpp` (8 state-machine
per design-doc target + 3 template-filter invariants the doc
didn't list but are worth pinning). Full suite 2609 passing
(1 pre-existing skip).

Next: slice 14.3 — `GameTemplateConfig::requiredAssets` filter so
the picker can hide templates whose assets are absent (enables
the private-repo biblical template to surface only when the
maintainer's assets are on disk).

### 2026-04-22 Phase 10.5 — First-run wizard foundation (slice 14.1)

First slice of the Phase 10.5 first-run wizard work
(`docs/PHASE10_5_FIRST_RUN_WIZARD_DESIGN.md`, approved 2026-04-22).
Ships the persistence layer underneath the wizard — no UI yet;
slices 14.2 – 14.4 add the panel, the template visibility filter,
and the engine wiring.

- `OnboardingSettings` section on `Settings`: `hasCompletedFirstRun`,
  `completedAt` (ISO-8601 UTC, empty until completion), `skipCount`
  (bumped by "Skip for now"; after two, `hasCompletedFirstRun`
  auto-flips — Q7 resolution in the design doc).
- Schema version bumped **v1 → v2**. First exercise of the chained
  migration scaffolding shipped in slice 13.1.
  `migrate_v1_to_v2(j)` inserts the `onboarding` block with
  defaults; idempotent on a tree that already carries one.
- Legacy flag promotion in `Settings::loadFromDisk`. Pre-v2 builds
  wrote `<configDir>/welcome_shown` from `WelcomePanel::markAsShown`.
  Upgraders whose first post-upgrade launch predates any Apply
  click have only that signal; loading now promotes it to
  `onboarding.hasCompletedFirstRun = true` and deletes the legacy
  file (best-effort). Lossless: struct mutation happens before
  file deletion, so a crash between them just re-runs on the
  next launch. Runs on both the Ok and FileMissing load paths.
- Tests: 6 new in `tests/test_settings.cpp` — defaults, JSON
  round-trip, v1→v2 migration inserts block, legacy flag
  promotion (file-missing path), promotion deletes the flag
  file, promotion skipped when struct is already complete.
  Full suite 2598 passing (1 pre-existing skip).

Next: slice 14.2 — `FirstRunWizard` panel class wrapping the
existing Phase 9D `TemplateDialog`.

### 2026-04-22 Public default scene + tester onboarding

Two open-source-release drive-bys landed together.

**Default scene is now the neutral demo, not the Tabernacle.** A fresh
public clone opens `Engine::setupDemoScene()` (four CC0 textured blocks
on a grey ground, sky-blue clear colour) instead of
`setupTabernacleScene()`, which referenced assets under
`assets/textures/tabernacle/` that live in a separate private repo and
are gitignored in the public repo. The Tabernacle scene is still
reachable for the maintainer via a new `--biblical-demo` CLI flag
(`EngineConfig::biblicalDemo`, default `false`).

- `EngineConfig::biblicalDemo` — maintainer opt-in; default `false` so
  public clones no longer silently fall back on missing textures.
- `engine.cpp` — `setupDemoScene()` now owns its renderer baseline
  (skybox disabled, sky-blue clear, bloom + SSAO with sane defaults,
  manual exposure 1.0) rather than inheriting Tabernacle-tuned values
  by accident. The Tabernacle path keeps its own overrides.
- `app/main.cpp` — `--biblical-demo` flag + `--help` entry.
- `ASSET_LICENSES.md` — clarifies the default-scene policy.

**Tester onboarding documentation.** The engine is ready for broader
testing but hadn't published a "how to test without writing C++" path.

- `TESTING.md` — 10-minute smoke-test script, 30-minute deeper pass,
  hardware gaps the maintainer cannot cover (NVIDIA, Intel, Windows,
  non-RDNA AMD), and pointers to the release binaries + issue templates.
- `.github/ISSUE_TEMPLATE/tester_feedback.md` — companion to the
  existing `bug_report.md`; captures hardware, version, frame-rate
  observations, and suggestions for "works / rough / would-change" reports.
- `.github/workflows/release.yml` — first-cut tag-triggered release
  workflow. `v*` tag push (or manual dispatch) builds Linux x86_64
  tarball + Windows x86_64 zip, attaches both to a draft GitHub
  Release. AppImage + code-signing are follow-ons.
- `README.md` + `CONTRIBUTING.md` — cross-link `TESTING.md` so
  non-developers arriving at either doc see the tester path.

### 2026-04-22 Phase 10 — Video-mode runtime apply (slice 13.2)

Unblocks the "Window is immutable after construction" blocker
from the settings design (§5 #1). Adds runtime resolution /
fullscreen / vsync changes and wires `DisplaySettings` into a
testable apply layer.

- `Window::setVideoMode(width, height, fullscreen, vsync)` — single
  entry point for runtime video-mode changes. Uses GLFW's
  `glfwSetWindowMonitor` for the windowed ↔ fullscreen toggle
  (preserves the GL context across the transition — no window
  reconstruction needed, contra the design-doc's conservative
  "one-frame GL context validity gap" worry), remembers the prior
  windowed rectangle on entering fullscreen so the reverse toggle
  restores it, and falls back to windowed mode if no primary monitor
  is connected. The framebuffer-size callback fires automatically,
  so renderer framebuffers re-allocate via the existing
  `WindowResizeEvent` subscription — zero extra wiring.
- `Window::isFullscreen()` — queries `glfwGetWindowMonitor`.
- `engine/core/settings_apply.{h,cpp}` — apply layer between
  `Settings` and subsystems. Introduces `DisplayApplySink`
  (abstract base) so tests can supply a recording mock, with
  `WindowDisplaySink` as the production forwarder. `applyDisplay(
  DisplaySettings, DisplayApplySink&)` pushes width/height/fullscreen
  /vsync at the sink in a single call. Render scale and quality
  preset are intentionally deferred — they belong to the Renderer
  and per-subsystem shader-variant wiring, coming in later slices.
- Tests: 4 new `SettingsApply` tests in `tests/test_settings.cpp`
  — forwards all four fields verbatim, handles the default
  windowed case, is idempotent across repeated calls, and survives
  a full JSON → fromJson → apply round-trip. Full suite 2591
  passing (1 pre-existing skip).

Next: slice 13.3 — `AudioMixer::setBusGain` + wiring the `audio`
and `accessibility` blocks into the apply layer.

### 2026-04-22 Phase 10 — Settings primitive + atomic-write + config-path (slice 13.1)

First slice of the Settings system. Ships the persistence primitive
itself — JSON schema v1, load / save / migrate / validate lifecycle,
durable atomic writes, and the shared per-user config-path resolver.
No engine wiring yet; slices 13.2 – 13.5 add per-subsystem apply
paths.

- `engine/utils/config_path.{h,cpp}` — `ConfigPath::getConfigDir()`
  returns the Vestige per-user directory
  (`$XDG_CONFIG_HOME/vestige/` → `$HOME/.config/vestige/` → `/tmp/vestige/`
  on POSIX; `%LOCALAPPDATA%\Vestige\` via `SHGetKnownFolderPath` on
  Windows). Factored out of `editor/recent_files.cpp` so Settings,
  save-games, and any future persistence use one resolver.
  `RecentFiles::getConfigDir` now forwards to the helper.
- `engine/utils/atomic_write.{h,cpp}` — crash-safe file replacement
  via tmp → fsync → rename → fsync-dir on POSIX, `MoveFileExW(MOVEFILE_
  REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` on Windows. A crash
  mid-write leaves either the old file or the new one, never a
  truncated hybrid. Status-code return (`Ok` / `TempWriteFailed` /
  `FsyncFailed` / `RenameFailed` / `DirFsyncFailed`) so callers own
  logging policy.
- `engine/core/settings.{h,cpp}` — root `Settings` struct with five
  sections (`display`, `audio`, `controls`, `gameplay`,
  `accessibility`). `loadFromDisk` parses via
  `JsonSizeCap::loadJsonWithSizeCap` (1 MB cap), runs the migration
  chain, applies validation clamps, and returns a `LoadStatus`
  (`Ok` / `FileMissing` / `ParseError` / `MigrationError`). A
  corrupt file is moved to `<path>.corrupt` so the user can recover
  it manually. `saveAtomic` serialises to JSON, hands off to
  `AtomicWrite::writeFile`, and returns a `SaveStatus`.
  `Settings::defaultPath()` resolves to
  `$XDG_CONFIG_HOME/vestige/settings.json` on Linux.
- `engine/core/settings_migration.{h,cpp}` — chained migration
  scaffolding driven by the root `schemaVersion` integer. v1 is
  current so the chain is a no-op today; the scaffolding is in
  place so v1 → v2 slots in cleanly when the schema evolves.
  Future-version files are refused (we do not downgrade).
  Migrations must be idempotent.
- Schema coverage (see `docs/PHASE10_SETTINGS_DESIGN.md` §4.3 for
  the JSON shape):
  - `display`: windowWidth/Height, fullscreen, vsync,
    qualityPreset (low/medium/high/ultra/custom), renderScale.
  - `audio`: six-bus gain table (master/music/voice/sfx/ambient/ui)
    + HRTF toggle.
  - `controls`: mouse sensitivity, invertY, gamepad deadzones,
    keybinding array (scan-code wire format).
  - `gameplay`: untyped `string→JsonValue` map — per-game values.
  - `accessibility`: UI scale preset, high-contrast, reduced-motion,
    subtitles, color-vision filter, photosensitive-safety caps,
    post-process motion toggles (DoF / motion-blur / fog).
- Validation: render scale clamped to [0.25, 2.0]; bus gains to
  [0, 1]; mouse sensitivity to [0.1, 10.0]; gamepad deadzones to
  [0, 0.9]; non-positive resolutions snap to 1280×720; unknown
  uiScalePreset / subtitleSize / colorVisionFilter strings fall
  back to defaults with a logged warning; malformed keybinding
  entries (missing or wrongly-typed `id`) dropped silently.
- Forward/backward compat: unknown JSON fields ignored on load;
  missing fields get struct-initialiser defaults.
- Tests: `tests/test_settings.cpp` — 36 tests covering
  `ConfigPath` env-var policy, `AtomicWrite` success/parent-dir/
  tmp-cleanup/empty-payload/`describe()`, `Settings` round-trip
  (including partial JSON + unknown fields + gameplay map),
  validation clamps for every bounded field, migration chain
  (no-op / missing-version / future-version), and disk
  load/save (corrupt-file sidecar, parent-dir creation).

Next: slice 13.2 — `Window::setVideoMode` for runtime resolution /
vsync / fullscreen changes + wiring the `display` block into Apply.

### 2026-04-22 Phase 10 — Settings system design approved (slices 13.1–13.5)

Design-review checkpoint. `docs/PHASE10_SETTINGS_DESIGN.md` is
approved; all eight §12 open questions signed off as proposed.
No code ships in this commit — the design doc is the deliverable
and it unblocks slice 13.1 implementation.

Key decisions recorded in the sign-off log (doc tail):

- **Format/location.** Single JSON at
  `$XDG_CONFIG_HOME/vestige/settings.json` (Linux) /
  `%LOCALAPPDATA%\Vestige\settings.json` (Windows), root-level
  `schemaVersion: 1`, nlohmann_json (engine's existing serialiser).
- **Schema.** Five top-level sections: `display` (resolution /
  vsync / fullscreen / quality preset / render scale), `audio`
  (six-bus gains + HRTF), `controls` (mouse sensitivity, invert-Y,
  gamepad deadzones, keybindings as GLFW scancodes), `gameplay`
  (untyped `string→JsonValue` map — per-game), `accessibility`
  (UI scale preset, high-contrast, reduced-motion, subtitles,
  color-vision filter, photosensitive safety, post-process toggles).
- **Lifecycle.** Chained migration functions (not discard-on-error),
  ignore-unknown + default-missing, `.corrupt` sidecar on parse
  failure. Atomic writes via tmp→fsync→rename→fsync-dir (POSIX)
  / `MoveFileExA(MOVEFILE_REPLACE_EXISTING)` (Windows). Apply /
  Revert / Restore Defaults mapped to `m_applied` / `m_pending`
  state copies. No Apply-on-close — explicit Apply required.
- **Accessibility policy.** "Restore All Defaults" spares the
  Accessibility tab; Accessibility gets its own explicit
  "Restore accessibility defaults" button so a partially-sighted
  user doesn't lose their 2.0× UI scale to a reset click.
- **Blockers inventoried.** `Window` is immutable after
  construction (fixed in slice 13.2 via `setVideoMode`);
  `AudioMixer::busGain` has no setter (added in slice 13.3);
  `UITheme` rebuilds clobber overrides (batched in slice 13.5).
- **Slice plan.** 13.1 Settings primitive + atomic-write + config-
  path helper factoring; 13.2 video runtime apply; 13.3 audio +
  accessibility apply; 13.4 input bindings JSON; 13.5 Settings UI
  wiring + Restore Defaults. Five slices, each independently
  testable and commitable. ~68 new tests planned across all five.

### 2026-04-22 Phase 10 — Text rendering bullet (TrueType fonts)

Documentation-only tick. The roadmap's Phase 10 Features →
"Text rendering (TrueType fonts)" bullet is now checked. The
underlying implementation (`engine/renderer/font.{h,cpp}` +
`engine/renderer/text_renderer.{h,cpp}` — FreeType-backed TTF
loader + 2D/3D text rendering + glyph atlas) shipped earlier as
part of Phase 9C / 9F / 10 UI work and is exercised by
`tests/test_text_rendering.cpp` and every menu / HUD / FPS counter
/ interaction prompt built on top of it. The design doc for the
slice-12 UI system called this out (§2 inventory: "The next
roadmap bullet 'Text rendering (TrueType fonts)' is quietly
already done"); this entry formally retires the bullet so the
remaining Phase 10 Features list reflects actual outstanding work
(scene config, settings, loading screens, info plaques).

### 2026-04-21 Phase 10 UI — Toasts + HUD + editor panel (slices 12.3–12.5)

Closes the "In-game UI system" roadmap bullet. Slices 12.1 (pure
`GameScreen` state machine) and 12.2 (`UISystem` screen stack + Engine
integration + menu-prefab signal wiring) landed earlier in the same
day; these three close the remaining gaps identified in
`docs/PHASE10_UI_DESIGN.md` §2 (inventory).

- **Slice 12.3 — Notification toast primitives.** New
  `engine/ui/ui_notification_toast.{h,cpp}`:
  - `NotificationSeverity::{Info, Success, Warning, Error}` plus
    `Notification { title, body, severity, durationSeconds }`.
  - `NotificationQueue` — FIFO with `DEFAULT_CAPACITY = 3`
    (mental-model parity with `SubtitleQueue::DEFAULT_MAX_CONCURRENT`),
    push-newest / drop-oldest eviction, `advance(dt, fadeSeconds)` tick.
  - Pure `notificationAlphaAt(elapsed, duration, fade)` envelope:
    fade-in → plateau → fade-out. `fade ≤ 0` collapses to a
    rectangle (reduced-motion snap); short durations degenerate to
    a triangle so an in-and-out ramp still happens.
  - `UINotificationToast` — `UIElement` subclass rendering one
    `ActiveNotification` as a panel + left-edge severity accent
    strip + title label + optional body. Alpha multiplies every
    drawn colour so the envelope fade is one knob. Accessibility
    role `Label`; `label` carries the title, `description` carries
    the body so a future TTS bridge announces the whole entry in
    one utterance.
  - `UISystem::update` now advances the queue against
    `UITheme::transitionDuration` each frame.

- **Slice 12.4 — Default HUD prefab.** New
  `buildDefaultHud(canvas, theme, textRenderer, uiSystem)` in
  `engine/ui/menu_prefabs.{h,cpp}`:
  - Crosshair at `CENTER` (theme-coloured, small).
  - FPS counter at `TOP_LEFT`, **hidden by default** (debug-only).
  - Interaction-prompt anchor — transparent `UIPanel` at
    `BOTTOM_CENTER`, 4 body-lines above the bottom edge — reserved
    slot for game code's `UIInteractionPrompt` widgets.
  - Notification stack — `UIPanel` container at `TOP_RIGHT` with
    three pre-created `UINotificationToast` children (matching the
    queue cap), all at alpha 0 until populated.
  - `GameScreen::Playing` now has a built-in `ScreenBuilder` default
    that points at `buildDefaultHud`, so `setRootScreen(Playing)`
    yields a working HUD without any game-project plumbing.

- **Slice 12.5 — Editor `UIRuntimePanel`.** New
  `engine/editor/panels/ui_runtime_panel.{h,cpp}`, four tabs
  mirroring the `AudioPanel` discipline:
  - **State** — current root / top-modal readout, a button grid
    firing every `GameScreenIntent`, and a 20-entry scrollback
    recording every `(from, to, intent)` transition with a Clear
    button.
  - **Menus** — combo selector for MainMenu / Paused / Settings
    preview, "Rebuild" button, live element-count readout. The
    offscreen composite FBO path is left as an explicit TODO
    (pending editor-viewport cooperation); the structural readout
    lands now so prefab changes are visible without a game build.
  - **HUD** — four checkboxes (crosshair / FPS counter /
    interaction anchor / notification stack) that write through to
    the live `UISystem` canvas when the root screen is `Playing`.
  - **Accessibility** — scale-preset combo + high-contrast and
    reduced-motion checkboxes that call straight into
    `UISystem::setScalePreset` / `setHighContrastMode` /
    `setReducedMotion`, so a user can compose all three transforms
    and see every menu prefab + the HUD react immediately.
  - `Editor` gains `setUISystem(UISystem*)` and
    `m_uiRuntimePanel` is drawn from the main editor loop; the
    engine wires `setUISystem(m_systemRegistry.getSystem<UISystem>())`
    next to `setAudioSystem`.

- **Tests (new, ~45 cases).** `tests/test_notification_queue.cpp`
  covers severity labels, FIFO eviction, capacity changes,
  negative-duration clamp, the full alpha envelope (ramps /
  plateau / reduced-motion snap / short-duration triangle /
  past-expiry zero), severity → theme colour mapping under default
  and high-contrast palettes, and every `UINotificationToast`
  update path (title-only, title+body, alpha-only no-rebuild fast
  path, alpha clamp). `tests/test_default_hud.cpp` pins the four
  root elements, their expected anchors, the FPS-hidden invariant,
  the transparent interaction anchor, the three-slot notification
  stack, and the `Playing`-defaults-to-HUD screen-builder wiring.
  `tests/test_ui_runtime_panel.cpp` covers panel open/close, the
  screen-log capacity cap, menu-preview rebuild across all three
  menus, HUD toggle round-trips, the `applyHudTogglesTo` clamp to
  `min(canvasCount, HUD_ELEMENT_COUNT)`, and the no-op guard when
  the root screen is not `Playing`.

- **Roadmap.** Phase 10 Features → "In-game UI system (menus, HUD,
  information panels/plaques)" is now checked. Remaining
  unchecked bullets in Phase 10 Features: text rendering,
  scene/level configuration, settings system, loading screens,
  information plaques.

### 2026-04-21 Phase 10 fog — Accessibility transform (slice 11.9)

Third Phase 10 fog slice. The accessibility-settings struct shipped
ahead of the fog feature (`PostProcessAccessibilitySettings`) already
carried `fogEnabled`, `fogIntensityScale`, and `reduceMotionFog`
fields with unit-tested defaults, but nothing *applied* those flags —
the GPU simply uploaded whatever was authored. This slice closes the
gap so a user who opens Settings → Accessibility and toggles the fog
sliders actually sees the render change.

- `engine/renderer/fog.{h,cpp}` — new `FogState` struct (a snapshot of
  every authorable fog parameter: mode + distance params + height-fog
  enable/params + sun-inscatter enable/params) plus the pure-function
  `applyFogAccessibilitySettings(authored, settings) → effective`
  transform. Rules:
  - `fogEnabled = false` is the **master switch** — every layer goes
    off, ignoring intensity + reduce-motion. The one-click
    accessibility toggle is unambiguous regardless of flag order.
  - `fogIntensityScale` scales per-layer:
    - **Linear distance fog** — `end` is pushed outward by
      `start + (end - start) / scale` (scale 0.5 → roughly half the
      perceived density, scale 1 → identity). `scale ≤ 1e-3`
      collapses to `FogMode::None` (avoids a divide-by-zero and
      gives the expected "no fog" experience at the floor).
    - **Exponential / Exponential² distance fog** — density is
      multiplied by scale. scale = 0 produces density 0 which is
      pass-through in `computeFogFactor`.
    - **Height fog** — both `groundDensity` and `maxOpacity` scale
      so the transmittance floor can't pin a ghost layer at scale 0.
    - **Sun inscatter lobe** — colour (not exponent) scales so the
      lobe *shape* stays authored but peak brightness dims.
  - `reduceMotionFog = true` further halves the sun-inscatter colour
    (on top of intensity scale), matching WCAG 2.2 SC 2.3.3 / Xbox
    AG 117 photosensitivity guidance that restricts rapid-onset
    flashing. Distance + height fog are frame-static so the flag is
    a no-op for them today; volumetric fog (slice 11.6+) will
    consult the same flag for temporal reprojection disable.

- `engine/renderer/renderer.{h,cpp}` — adds
  `setPostProcessAccessibility(settings)` / `getPostProcessAccessibility()`
  plus the `m_postProcessAccessibility` member. The composite-pass
  upload now builds a `FogState` from the authored members, runs the
  transform, and uploads the effective values. Authored state is
  **never mutated** — users can toggle accessibility without losing
  their scene-authored look.

- `tests/test_fog.cpp` — 12 new `FogAccessibility` cases:
  `DefaultsArePassThrough`, `MasterDisableCollapsesEveryLayer`,
  `IntensityHalfScalesExponentialDensity`,
  `IntensityZeroTurnsExponentialDensityOff`,
  `IntensityHalfPushesLinearEndOutward`,
  `IntensityZeroCollapsesLinearToNone`,
  `IntensityHalfScalesHeightFogGroundDensityAndMaxOpacity`,
  `IntensityScalesSunInscatterColourNotExponent`,
  `ReduceMotionFogHalvesSunInscatterColour`,
  `ReduceMotionFogDoesNotAffectDistanceOrHeightFog`,
  `SafePresetProducesHalfIntensityReducedMotionFog` (end-to-end via
  `safeDefaults()`), `MasterDisableBeatsIntensityOneAndReduceMotion`
  (flag-precedence guard). All 48 fog-related tests pass.

Corresponds to slice 11.9 in `docs/PHASE10_FOG_DESIGN.md`. Remaining
non-volumetric fog slices: 11.5 (screen-space god rays) and 11.10
(editor FogPanel). Volumetric slices 11.6 – 11.8 are the heavy-lift
remainder of Phase 10 fog.

VESTIGE_VERSION: 0.1.7 → 0.1.8

### 2026-04-21 Phase 10 fog — Shader integration (distance + height + sun inscatter)

Second Phase 10 fog slice. The CPU-side primitives shipped in the
previous slice (`Vestige::computeFogFactor`,
`computeHeightFogTransmittance`, `computeSunInscatterLobe`) are now
evaluated by the final composite shader and produce a visible
contribution on screen. Only the non-volumetric fog stack — volumetric
froxels and god-rays remain deferred per the
`docs/PHASE10_FOG_DESIGN.md` 10-slice rollout plan.

- `assets/shaders/screen_quad.frag.glsl` — adds `u_fogMode`,
  `u_fogColour`, `u_fogStart`, `u_fogEnd`, `u_fogDensity`,
  `u_heightFogEnabled` (+ colour / Y / density / falloff / maxOpacity),
  `u_sunInscatterEnabled` (+ colour / exponent / startDistance),
  `u_sunDirection`, `u_fogDepthTexture` (unit 12, shared with SSAO /
  contact shadows and re-bound in the composite for Mesa declared-
  sampler safety), `u_fogInvViewProj`, `u_fogCameraWorldPos`. GPU
  formula ports `fog.cpp` byte-for-byte. Composition order matches
  `docs/PHASE10_FOG_DESIGN.md` §4: SSAO → contact shadows → **fog
  mix** → bloom add → exposure → tonemap → LUT → colour vision →
  gamma. Bloom therefore samples fogged radiance in linear HDR, which
  is the UE / HDRP convention.

- Reverse-Z world-space reconstruction — `u_fogInvViewProj` takes the
  per-pixel NDC + depth back to world space, so the height-fog ray
  integral uses real world-Y rather than a view-space proxy. Sky
  pixels (reverse-Z depth == 0) skip fog so the skybox colour passes
  through untouched.

- `engine/renderer/fog.{h,cpp}` — new `FogCompositeInputs` struct and
  `composeFog(surfaceColour, inputs, worldPos)` helper that mirror the
  GLSL composite byte-for-byte. Acts as the shared CPU / GPU spec —
  `test_fog.cpp` pins the CPU form, the shader pins the GPU form, and
  the unit tests catch drift between the two.

- `engine/renderer/renderer.{h,cpp}` — new `FogMode` / `FogParams` /
  `HeightFogParams` / `SunInscatterParams` state plus `setFogMode` /
  `setFogParams` / `setHeightFogEnabled` / `setHeightFogParams` /
  `setSunInscatterEnabled` / `setSunInscatterParams` setters (and
  matching getters). Composite uniforms are pushed each frame between
  the contact-shadow and LUT uniform blocks. `m_cameraWorldPosition`
  is cached inside `renderScene()` when not on an override view
  (cubemap-face captures skip fog — the state only matters for the
  main composite).

- 7 new `FogComposite` unit tests — all-disabled identity; distance
  fog at far-end gives pure fog colour; distance fog near camera
  gives pure surface; sun-inscatter warps distance-fog colour to the
  sun tint at `cosθ=1`; height-fog fully obscures surface when
  ground-density is saturated; exact 50/50 two-layer composition
  algebra (0.25, 0.25, 0.5 expected from 0.5·(red,green) mixed with
  0.5·blue); zero-view-distance is a pass-through even when every
  layer is enabled.

All 2436 unit tests pass (2437 total, 1 pre-existing GPU-only skip).

VESTIGE_VERSION: 0.1.6 → 0.1.7.

### 2026-04-21 Roadmap — Add Phase 10.5 Editor Usability Pass

New phase inserted between Phase 10 (Polish and Features) and Phase 11
(Gameplay Systems). Scope principle: no new major features — every
item is about making existing editor functionality findable, obvious,
and fast. Sections:

- Discoverability (command palette, contextual help, searchable
  settings, panel launcher, in-editor glossary).
- Onboarding (first-run dialog, guided tour, sample scenes,
  opt-in local-file telemetry).
- Workflow ergonomics (keyboard parity, chord shortcuts, universal
  undo/redo, unified copy/paste, drag-and-drop, multi-select,
  auto-save, project-relative paths).
- Tooltips & contextual help (every widget, status-bar hints,
  inline warnings, "why is this greyed out?").
- AI assistance integration hooks (editor-exposed command API,
  scene-state snapshot format, optional chat panel, prompt
  templates, keyboard agent invocation). Rule: editor must be
  fully functional without AI.
- Performance & responsiveness (per-panel ms budgets, async
  imports, incremental saves, aggressive panel collapse).
- Editor-side accessibility (scaling presets, high-contrast mode,
  screen-reader labels on ImGui widgets, colourblind-safe gizmos,
  keyboard-only workflow).
- Docs surface (in-editor markdown browser, inline video tooltips,
  troubleshooting decision tree).

Milestone: a person who has never opened Vestige can follow the
first-run tour and produce a buildable scene without reading source
code or external tutorials. Keyboard-only users can drive every
action without a mouse.

### 2026-04-21 Formula Workbench 1.17.0 — Weighted LM, max-abs metric, step sweeps

Three backwards-compatible extensions surfaced by the Phase 10 fog
research (`docs/PHASE10_FOG_RESEARCH.md` §8 /
`docs/PHASE10_FOG_DESIGN.md` §9). All three unlock rendering-formula
fits (phase functions, tonemap curves, BRDF lobes) that the existing
fitter could almost — but not quite — handle. Prepares the Workbench
for the Schlick-to-Henyey-Greenstein phase-function fit in Phase 10's
volumetric-fog slice.

- **Weighted LM** — `CurveFitter::fitWeighted` new overload in
  `engine/formula/curve_fitter.{h,cpp}`. Per-sample weight vector
  parallel to data; minimises `sum(w_i · r_i²)`. Empty / mismatched
  weights degrade to uniform LM. Negative / non-finite weights clamp
  to 0. Reported rmse / maxError / rSquared stay unweighted so the
  numbers remain comparable across fits.
- **`max_abs_error_max`** — new optional expected-block field in
  reference-case JSON. Checks `FitResult::maxError` (already
  computed) and fails when worst-case residual exceeds bound.
  Rendering fits fail on worst-case error, not mean. Default
  `+infinity` keeps existing cases unchanged.
- **Step-based input sweeps** — optional `step` field on
  `InputSweep` alongside `min` / `max` / `count` / `values`. Three
  sweep forms in priority order: explicit values → step → count.
  Endpoint always included (tail sample appended if range isn't an
  integer multiple of step).
- **Documented N-dimensional Cartesian product** — harness already
  supported multi-axis sweeps via multi-key `input_sweep`; now has
  unit-test coverage.
- **Tests** — 5 new curve-fitter tests (weighted path, empty-weights
  parity, mismatch-falls-back-to-uniform, zero-weight-drops-row,
  negative-weights-clamp-to-zero, skewed-data weighted optimum); 7
  new harness tests (step endpoint, non-divisible tail, step=0
  fallback, 2-axis cardinality, max-abs JSON parse, max-abs default
  infinity, weights mismatch failure, uniform weights pass-through).

WORKBENCH_VERSION: 1.16.0 → 1.17.0.

### 2026-04-21 Phase 10 fog — Non-volumetric foundation (distance, height, sun inscatter)

First Phase 10 fog slice. Ships the pure-function primitives for the
three canonical distance-fog modes, the Quílez analytic exponential
height-fog integral, and the UE-style directional sun-inscatter lobe —
plus accessibility toggles so users with motion / contrast sensitivity
can tune the look without losing it entirely.

Follows the Phase 10 audio cadence: ship pure-function primitives
first (exhaustively tested, editor + tests can exercise the math),
wire into the GPU composite in a follow-up slice.

- `docs/PHASE10_FOG_RESEARCH.md` — 2,400-word research report with 20
  citations across UE5 Exponential Height Fog, Unity HDRP, Godot 4,
  Wronski 2014 and Hillaire 2015 (volumetric), Mitchell 2008 (god
  rays), Quílez 2010 (height-fog integral), D3D9 fog-formula spec,
  and accessibility standards (WCAG 2.2 SC 2.3.1 / 2.3.3, Xbox AG
  117/118, Game Accessibility Guidelines).
- `docs/PHASE10_FOG_DESIGN.md` — 10-slice rollout plan, HDR
  composition order, performance budgets per slice, Workbench
  applicability analysis, and three proposed Workbench improvements
  (2D input grids, max-abs-error metric, weighted-LM support).

- `engine/renderer/fog.{h,cpp}` — pure-function primitives:
  * `FogMode` enum (`None` / `Linear` / `Exponential` /
    `ExponentialSquared`) + `FogParams` (colour, start, end,
    density).
  * `computeFogFactor(mode, params, distance)` — canonical
    Linear / GL_EXP / GL_EXP2 formulas (OpenGL Red Book §9; D3D9
    fog-formulas). Returns surface *visibility* in `[0, 1]`. Guards
    degenerate params (start==end, negative density, negative
    distance) with sensible pass-through behaviour.
  * `HeightFogParams` + `computeHeightFogTransmittance` — closed-form
    Quílez integral of `d(y) = a·exp(-b·(y - fogHeight))` along a
    view ray. Uses `expm1` for numerical stability near
    `density·rayDirY·rayLength ≈ 0`. Separate horizontal-ray branch
    collapses to Beer-Lambert when `|rd.y| < 1e-5` so the shader
    doesn't spike at the horizon line. `maxOpacity` clamp matches UE
    `FogMaxOpacity` so the sky doesn't fully vanish on long
    sightlines.
  * `SunInscatterParams` + `computeSunInscatterLobe` — cosine-lobe
    directional scattering (UE "DirectionalInscatteringColor"
    pattern). Zero below `startDistance`, zero on backlit rays.
  * `applyFog(surface, fog, factor)` — CPU mirror of GLSL
    `mix(fog, surface, factor)` with `[0, 1]` clamp.
  * `fogModeLabel(mode)` — stable strings for the editor + tests.

- `tests/test_fog.cpp` — 29 headless unit tests covering:
  * Label stability across every enum value.
  * `None` mode pass-through at any distance.
  * Zero/negative distance pass-through for every mode.
  * Linear knees: unity below `start`, zero at `end`, 0.5 at
    midpoint, zero-span returns unity, clamped past `end`.
  * Exponential: factor = 0.5 when `density·d = ln 2`,
    zero-density returns unity, negative-density clamps to zero,
    monotonic decay across 200 samples.
  * Exponential-squared: factor = `exp(-1)` at `density·d = 1`,
    softer onset than GL_EXP at matching density (the defining
    property of the squared form), monotonic decay.
  * `applyFog` byte-for-byte parity with GLSL `mix()` at
    factor ∈ {0, 0.5, 1} and out-of-range clamp.
  * Height fog: zero-length / zero-density pass-through, monotonic
    decay across distance, horizontal-ray ↔ Beer-Lambert equivalence,
    thinner at altitude, maxOpacity floor, small-angle ↔
    horizontal-branch agreement.
  * Sun inscatter: zero inside start distance, unity when looking
    into sun, zero on backlit, tighter lobe at larger exponent,
    negative exponent defensive clamp.

- `engine/accessibility/post_process_accessibility.{h,cpp}` —
  three new fields: `fogEnabled` (default `true`),
  `fogIntensityScale` (default `1.0`), `reduceMotionFog` (default
  `false`). Rationale documented in the header — disabling fog
  entirely creates a harsh fog-horizon cutoff that's *worse* for
  low-contrast-sensitivity users than keeping fog on at half
  density. `safeDefaults()` therefore keeps fog on, halves density
  (`fogIntensityScale = 0.5`), and enables reduced-motion mode so
  the (future) volumetric temporal reprojection and sun-inscatter
  lobe can't flash on rapid camera pans.

- `tests/test_post_process_accessibility.cpp` — 4 new tests:
  defaults for the three new fields, safe-preset keeps fog on at
  half intensity, per-field equality coverage, per-field
  independence.

**Follow-up slices, explicitly deferred:**
- Shader integration (`screen_quad.frag.glsl` uniforms, depth
  reconstruction, mix-before-bloom HDR composition) — renderer
  surgery, own commit.
- God rays (Mitchell screen-space radial blur).
- Volumetric fog — froxel grid, compute-shader injection,
  Beer-Lambert accumulation, Halton-jitter temporal reprojection.
- Volumetric phase function — Schlick approximation to
  Henyey-Greenstein, **fit via Formula Workbench** per CLAUDE.md
  Rule 11. The Workbench improvements (below) land before this.
- Editor FogPanel (mirror of AudioPanel four-tab pattern).

All formulas in this slice are closed-form / textbook — no
coefficients to fit, Formula Workbench not applicable (the
Workbench-fit slice is the Schlick-to-HG approximation in
volumetric-fog).

### 2026-04-21 Phase 10 audio — Editor AudioPanel closes Phase 10 audio

Tenth Phase 10 audio slice. Ships the last remaining bullet
("Editor integration") and with it, all of Phase 10 audio.

- `engine/editor/panels/audio_panel.{h,cpp}` — new `AudioPanel`
  class following the `NavigationPanel` pattern: non-GL state
  (mixer, ducking, zone lists, mute/solo sets, overlay toggle)
  is exposed through getters so unit tests exercise every
  mutator without an ImGui context.
- Four tabs:
  * **Mixer** — per-bus sliders (Master / Music / Voice / Sfx /
    Ambient / UI), dialogue-duck trigger checkbox, attack /
    release / floor sliders, live current-gain readout.
  * **Sources** — iterates the active scene via
    `Scene::forEachEntity`, displays each `AudioSourceComponent`
    with per-entity Mute + Solo checkboxes, volume / pitch /
    min-max-distance sliders, and the attenuation-model label.
  * **Zones** — editor-draft reverb zones (name + center +
    core-radius + falloff-band + preset combo) and ambient zones
    (name + center + clip path + radii + max volume + priority);
    add / remove / select with selection-shift-on-remove so the
    selected index stays on the intended zone when an earlier
    entry is removed.
  * **Debug** — audio-availability indicator, distance model,
    Doppler factor, speed of sound, HRTF mode + status +
    dataset + available-dataset enumeration, viewport overlay
    toggle for the zone falloff spheres.
- `computeEffectiveSourceGain(entityId, bus)` — panel exposes
  the routing math so the AudioSystem can consult it for
  live playback gain decisions. Rules (matching every DAW's
  convention): mute beats solo (hard kill), solo-exclusive
  routing when any source is soloed, otherwise
  `master · bus · duckGain` clamped to [0, 1].
- Engine wiring: `Editor::setAudioSystem(AudioSystem*)` mirrors
  `setNavigationSystem`. `Engine::initialize` calls
  `setAudioSystem(m_systemRegistry.getSystem<AudioSystem>())`.
  Editor's main draw loop calls `m_audioPanel.draw(m_audioSystem,
  scene)` right after the NavigationPanel.
- `tests/test_audio_panel.cpp` — 18 headless tests: defaults
  (closed / empty zone lists / −1 selections / mixer unity /
  duck untriggered at 1.0), open/close + toggle, reverb zone
  add-returns-index + remove-shifts-selection-down +
  remove-selected-clears-selection + out-of-range no-op, ambient
  zone mirror of same invariants, mute/solo set state
  operations + `hasAnySoloedSource` flag, effective-gain routing
  (muted = 0, solo-exclusive, mute-beats-solo convention, bus ×
  ducking product, [0, 1] clamp), and overlay toggle.

This closes Phase 10 audio. The final audio suite covers:
distance attenuation curves (Phase 10.1), Doppler shift
(10.2), HRTF selection (10.3), material-based occlusion /
obstruction (10.4), reverb zones (10.5), environmental ambient
(10.6), dynamic music (10.7), mixer buses + ducking + voice
eviction (10.8), streaming-music decode state machine (10.9),
and now the editor surface (10.10). Ten slices, 173 new unit
tests, 2384 tests passing overall.

### 2026-04-21 Phase 10 audio — Streaming-music decode state machine

Ninth Phase 10 audio slice. Closes the "Audio engine integration"
parent bullet in ROADMAP.md by landing the last missing sub-item
(streaming playback for music — the one-shot path and the
`AudioSourceComponent` were already in place).

- `engine/audio/audio_music_stream.{h,cpp}` — `MusicStreamState`
  models the decoder-side pipeline independent of OpenAL. Tracks
  `totalFramesDecoded`, `totalFramesConsumed`, `sampleRate`,
  `loopCount`, `maxLoops` (−1 = infinite, 0 = one-shot),
  `minSecondsBuffered` / `maxSecondsBuffered` targets,
  `framesPerChunk`, and an explicit `finished` flag set when the
  consumer has drained everything after EOF.
- `notifyStreamFramesConsumed(state, n)` advances the consumer
  cursor and flips `finished` once `trackFullyDecodedOnce &&
  consumed >= decoded`. `notifyStreamFramesDecoded(state, n,
  eofReached)` advances the decoder cursor and increments
  `loopCount` each time the decoder reports EOF.
- `computeStreamBufferedSeconds(state)` returns
  `(decoded − consumed) / sampleRate`, guarded against zero
  sample rate and consumer overrun (defensive floor at 0 rather
  than negative).
- `planStreamTick(state, decoderAtEof)` is the pure-function
  policy brain that the engine-side MusicPlayer calls once per
  update. Decision tree:
    1. Stream already `finished` → return `trackFinished`.
    2. Buffered seconds ≥ `maxSecondsBuffered` → back-pressure,
       no decode work.
    3. `decoderAtEof` + loops exhausted → mark `finished`.
    4. `decoderAtEof` + loops remaining → `rewindForLoop` +
       request a chunk after the seek.
    5. Otherwise → request one chunk (rounded up to
       `framesPerChunk`) to move toward `minSecondsBuffered`.
  One chunk per tick so a slow frame doesn't avalanche decoder
  work.
- `tests/test_audio_music_stream.cpp` — 16 new tests: buffered
  seconds at start / mid-stream / zero-sample-rate /
  consumer-overrun, notify counters (consumed advance, decoded
  advance + loopCount on EOF, finished after full drain,
  not-finished until full drain, not-finished if EOF not seen),
  plan back-pressure at cap, plan chunk-refill below min, plan
  refill after consumer drain, plan rewinds on EOF under
  infinite policy, plan finishes on EOF when loops exhausted,
  finite-loop policy allows exact N playthroughs, finished
  stream keeps reporting finished.

Also closes the ROADMAP bullet "Audio engine integration":
OpenAL Soft (zlib license) chosen over FMOD for MIT-open-source
compatibility; no runtime licensing concerns for the Steam
launch path. One-shot playback already shipped in Phase 9C via
`AudioEngine::playSound` + `AudioClip::loadFromFile`; streaming
playback lands now via the primitives above; the
`AudioSourceComponent` has been accreting fields across the
Phase 10 audio slices (attenuation / velocity / occlusion).

Per CLAUDE.md Rule 11: ratio / threshold / integer counters —
no coefficients to fit.

### 2026-04-21 Phase 10 audio — Mixer buses, ducking, voice eviction

Eighth Phase 10 audio slice. Ships the three primitives the
engine-side AudioSystem needs to route sources into mixer buses,
duck ambient while dialogue plays, and pick which voice to drop
when the OpenAL source pool is full.

- `engine/audio/audio_mixer.{h,cpp}`:
  * `SoundPriority` enum (Low / Normal / High / Critical) +
    `soundPriorityRank(p)` returning 0..3. Used as the dominant
    axis of the eviction score.
  * `AudioBus` enum (Master / Music / Voice / Sfx / Ambient /
    Ui) + `AudioMixer` struct (per-bus gain table defaulting to
    1.0) + `effectiveBusGain(mixer, bus)` returning `master * bus`
    clamped to [0, 1]. Querying the Master bus returns just the
    Master gain (no Master*Master double-apply). Settings UI
    writes bus gains; the AudioSystem reads `effectiveBusGain`
    when setting `AL_GAIN` on each source.
  * `DuckingState` (currentGain + triggered) + `DuckingParams`
    (attackSeconds + releaseSeconds + duckFactor) +
    `updateDucking(state, params, dt)` — slew `currentGain`
    toward `duckFactor` (triggered) or 1.0 (released) at a rate
    that travels the full swing `(1 − floor)` in the configured
    time. Clamped to [duckFactor, 1]. Zero-duration attack /
    release uses an epsilon so the slew is fast rather than
    inf/nan. Negative dt is a no-op.
  * `VoiceCandidate` (priority + effectiveGain + ageSeconds) +
    `voiceKeepScore(v) = rank·1000 + gain·10 − age` +
    `chooseVoiceToEvict(voices)` returning the index of the
    lowest-score entry (or sentinel `-1` when empty). The 1000×
    priority weight guarantees priority dominates realistic
    gain+age combinations; gain breaks ties within a tier;
    age breaks ties within gain.
- `tests/test_audio_mixer.cpp` — 19 new tests:
  * Labels: priority labels, bus labels, priority ranks
    monotonically increasing.
  * Bus gains: default unity per bus, Master multiplies with
    each bus, Master bus ignores self-double, clamps to
    [0, 1] on both sides.
  * Ducking: attacks toward floor over the configured duration,
    releases toward unity over its own duration, clamps at
    floor (no below-floor overshoot under huge dt), clamps at
    unity, negative dt is a no-op, zero-duration falls back to
    an epsilon-guard that still lands in the valid range.
  * Eviction: empty list returns the sentinel, lower priority
    evicts before higher, within a tier the quieter voice goes
    first, within tier+gain the oldest voice goes first,
    Critical survives against a loud-fresh Low, keep-score
    ordering matches priority rank.

Per CLAUDE.md Rule 11: linear slew + product-of-bus-gains +
integer-weighted score are canonical forms with no coefficients
to fit — Formula Workbench doesn't apply.

### 2026-04-21 Phase 10 audio — Dynamic music system primitives

Seventh Phase 10 audio slice. Three pure-function / pure-data
primitives that the engine-side MusicSystem composes into an
adaptive soundtrack.

- `engine/audio/audio_music.{h,cpp}`:
  * `MusicLayer` enum (Ambient / Tension / Exploration / Combat
    / Discovery / Danger) + `MusicLayerState` (currentGain,
    targetGain, fadeSpeedPerSecond) + `advanceMusicLayer(state,
    dt)` — per-layer slew toward `targetGain` at the fade-speed
    limit, clamped to [0, 1], no overshoot. Callers can write
    `targetGain` every frame; the slew absorbs the twitchiness.
  * `intensityToLayerWeights(intensity, silence)` — maps a
    single [0, 1] gameplay signal to the per-layer mix via
    triangle envelopes with peaks at 0.00 (Ambient) / 0.25
    (Exploration) / 0.50 (Tension + Discovery as subtler bed) /
    0.75 (Combat) / 1.00 (Danger). Adjacent layers meet at
    0.5/0.5 at every midpoint so intermediate intensities are a
    genuine blend rather than one layer winning hard. The
    `silence` parameter multiplicatively scales every layer so
    scripted quiet beats drop the full mix without disturbing
    the intensity routing.
  * `MusicStingerQueue` — FIFO queue with fixed capacity
    (DEFAULT_CAPACITY=8); push-newest / drop-oldest eviction so
    the latest event always wins. `advance(dt)` decrements
    every entry's delay and returns the fired stingers in FIFO
    order. `setCapacity(n)` trims in place; `clear()` discards
    pending; negative delta is a no-op so framerate stalls can't
    mass-fire.
- `tests/test_audio_music.cpp` — 21 new tests:
  * Layer labels stable.
  * Slew: reaches target after sufficient time, no overshoot
    under large fadeSpeed, fades down as well, clamps
    currentGain to [0, 1] defensively, zero delta keeps current.
  * Intensity routing: 0.00 → Ambient only, 1.00 → Danger only,
    0.25 → Exploration peak with zero Ambient (zero crossover),
    0.125 → Ambient+Exploration blend at 0.5/0.5 (mid-envelope),
    Tension peaks at 0.5, Combat peaks at 0.75, clamps at both
    ends.
  * Silence: scales every layer down uniformly, silence=1
    collapses to zero, ratios between layers preserved under
    partial silence.
  * Stinger queue: enqueue + fire after delay, FIFO multi-fire,
    capacity-based eviction of oldest, setCapacity trims in
    place, zero capacity rejects enqueue, clear drops pending,
    negative delta does not cause fire.

Per CLAUDE.md Rule 11: triangle envelope + linear slew +
multiplicative scaling are canonical forms with no coefficients
to fit — Formula Workbench doesn't apply.

### 2026-04-21 Phase 10 audio — Environmental ambient audio primitives

Sixth Phase 10 audio slice. Ships three independent pure-function
primitives that the engine-side AmbientSystem composes into the
full ambient pipeline (no coupling between them, so each is
unit-testable in isolation).

- `engine/audio/audio_ambient.{h,cpp}`:
  * `AmbientZone` struct (clipPath + coreRadius + falloffBand +
    maxVolume + priority) with `computeAmbientZoneVolume(zone,
    distance)` reusing the reverb-zone falloff profile so both
    subsystems share a single sphere-with-linear-falloff curve.
    Priority orders overlapping zones (cave ambience overrides
    outdoor wind in the falloff band).
  * `TimeOfDayWindow` enum (Dawn / Day / Dusk / Night) +
    `TimeOfDayWeights` struct (dawn, day, dusk, night) +
    `computeTimeOfDayWeights(hourOfDay)` — triangle-envelope
    mapping of a 24-hour clock to the four windows with peaks at
    06 / 13 / 20 / 01. Weights are normalised so they sum to 1.0
    at every hour — ensures `clip.volume * weight` budgets stay
    predictable under future peak retuning. Wraps around the 24h
    clock (hour=25 ≡ hour=1, hour=-2 ≡ hour=22).
  * `RandomOneShotScheduler` (minIntervalSeconds,
    maxIntervalSeconds, timeUntilNextFire) +
    `tickRandomOneShot(scheduler, dt, sampleFn)` — cooldown
    scheduler that draws a fresh interval from [min, max] each
    time it fires, using a caller-injected uniform-sample
    function in [0, 1] so tests stay deterministic and the
    engine plugs in `std::uniform_real_distribution`. Fires at
    most once per tick so a framerate stall can't avalanche
    one-shots. Null sampler falls back to the midpoint; negative
    delta treated as zero.
- Weather-driven modulation (rain intensity, thunder, wind howl)
  deliberately *not* wired here — once the Phase 15 weather
  controller publishes its rain/wind intensity outputs, the
  engine-side AmbientSystem applies them as a thin multiplier on
  top of these primitives. Keeping the pure-function layer free
  of Phase-15 dependencies preserves headless testability.
- `tests/test_audio_ambient.cpp` — 17 new tests:
  * AmbientZone volume: inside core returns maxVolume, outside
    falloff returns 0, mid-falloff is linear, maxVolume>1 clamps.
  * TimeOfDay: window labels stable, weights always sum to 1
    (sampled every 15 min across the full day), each peak makes
    its window dominant over all others, wrap-around symmetry on
    both sides of [0, 24], midnight makes night dominate.
  * RandomOneShot: fires when cooldown expires, does not fire
    when cooldown remains, only fires once per tick even under a
    huge delta, uses sampler value to pick interval, clamps
    sampler to [0, 1], null sampler falls back to midpoint,
    negative delta is zero, draws fresh interval per fire from a
    deterministic sampler sequence.

Per CLAUDE.md Rule 11: triangle envelope + linear interpolation
are canonical forms with no coefficients to fit — Formula
Workbench doesn't apply.

### 2026-04-21 Phase 10 audio — Reverb zones with smooth crossfade

Fifth Phase 10 audio slice. Ships the preset / zone-weight / blend
primitives needed to drive EFX reverb across a scene with
continuous transitions as the listener moves between rooms.

- `engine/audio/audio_reverb.{h,cpp}` — new `ReverbPreset` enum
  (`Generic` / `SmallRoom` / `LargeHall` / `Cave` / `Outdoor` /
  `Underwater`) paired with a `ReverbParams` struct that mirrors
  the non-EAX subset of the OpenAL EFX reverb model (`decayTime`,
  `density`, `diffusion`, `gain`, `gainHf`, `reflectionsDelay`,
  `lateReverbDelay`). `reverbPresetParams(preset)` returns values
  adapted from Creative Labs `efx-presets.h` (`EFX_REVERB_PRESET_*`
  entries) — kept to the subset that round-trips through
  `AL_REVERB_*` properties so the engine stays compatible with
  drivers that don't ship EAX reverb.
- `computeReverbZoneWeight(coreRadius, falloffBand, distance)` —
  sphere-with-linear-falloff weight function. Inside `coreRadius`
  returns 1.0; between `coreRadius` and `coreRadius + falloffBand`
  decays linearly to 0.0; outside returns 0.0. `falloffBand == 0`
  gives a hard step at the radius. Negative inputs clamp.
- `blendReverbParams(a, b, t)` — component-wise linear blend across
  every field with `t` clamped to [0, 1]. The engine-side
  ReverbSystem picks the highest-weighted zone and the next-highest
  neighbour, then passes their relative weights to this function so
  the crossfade through doorways / cave mouths is continuous rather
  than stepped.
- Auto-detection of room geometry → decay time is *not* in this
  slice — that step needs physics AABBs / mesh volumes and belongs
  one layer up in the engine-side ReverbSystem. The pure-function
  layer intentionally carries no geometry awareness so tests run
  headless.
- `tests/test_audio_reverb.cpp` — 13 new tests: label stability,
  every preset stays inside sensible EFX ranges (decay [0.1, 20],
  all ratios [0, 1]), ordering invariants (SmallRoom shortest
  decay, Cave longest, Underwater strongest HF damping), weight
  falloff cases (inside core, band=0 hard step, linear mid-band,
  negative clamps), and blend math (t=0/0.5/1 + out-of-range
  clamp, plus exact equality at boundaries).

Per CLAUDE.md Rule 11: the blend is a canonical linear lerp; the
preset values come from an established industry table — no
coefficients to fit, so the Formula Workbench flow doesn't apply.

### 2026-04-21 Phase 10 audio — Material-based occlusion + obstruction

Fourth Phase 10 audio slice. Gives the engine a canonical gain /
low-pass model for sound passing through solid geometry — walls,
doors, windows, water — so the AudioSystem can set final per-source
gain and EFX filter values once the physics raycaster has measured
the obstruction.

- `engine/audio/audio_occlusion.{h,cpp}` — new
  `AudioOcclusionMaterialPreset` enum (Air / Cloth / Wood / Glass /
  Stone / Concrete / Metal / Water) paired with an
  `AudioOcclusionMaterial` struct (`transmissionCoefficient`,
  `lowPassAmount`). Preset values are calibrated for first-person
  walkthroughs (Concrete transmits 0.05 with 0.90 low-pass, Cloth
  transmits 0.70 with 0.30 low-pass, etc.) — relative ordering not
  dB-measured accuracy. `computeObstructionGain(openGain,
  transmission, fraction)` blends open-path and transmitted-path
  gain via the canonical `openGain · (1 − f · (1 − t))` form;
  `computeObstructionLowPass(amount, fraction)` produces the
  matching EFX low-pass target. Both clamp out-of-range inputs.
- `AudioSourceComponent` — new `occlusionMaterial` +
  `occlusionFraction` fields (default `Air` / 0.0 so existing
  sources stay unaffected); `clone` carries them. The
  engine-side raycaster writes these each frame; the AudioSystem
  reads them to compute the final gain + filter values.
- Diffraction explicitly *not* modelled in this layer. The
  engine-side raycaster is responsible for picking a secondary
  source position that hugs the diffraction edge and feeding that
  into the normal attenuation + obstruction path, keeping the
  pure-function layer blind to geometry for testability.
- `tests/test_audio_occlusion.cpp` — 15 new tests: label stability
  for all presets, Air is fully transparent, Concrete is the
  least-transmissive solid, Cloth is the least-muffling non-Air,
  all presets inside [0, 1] on both axes, gain blend math
  (zero-fraction / full-fraction / half-fraction), out-of-range
  clamps on both fraction and transmission, and matching coverage
  for the low-pass path.

Per CLAUDE.md Rule 11: the blend is a canonical linear form with
no coefficients to fit; the numeric preset values are judgement
calls calibrated to listening rather than laboratory measurements,
so the Formula Workbench flow doesn't apply. Values are
deliberately exaggerated over real transmission-loss tables so
material differences stay audible without pushing source gains
into headroom.

### 2026-04-21 Phase 10 audio — HRTF selection closes Spatial audio parent

Third Phase 10 spatial-audio slice. Completes the "Spatial audio"
parent in ROADMAP.md (distance attenuation + Doppler + HRTF all
shipped) and lets players opt into head-tracked stereo-headphone
rendering via the OpenAL Soft `ALC_SOFT_HRTF` extension.

- `engine/audio/audio_hrtf.{h,cpp}` — new `HrtfMode` enum
  (`Disabled` / `Auto` / `Forced`), `HrtfStatus` enum mirroring
  `ALC_HRTF_STATUS_SOFT` values (`Disabled` / `Enabled` / `Denied` /
  `Required` / `HeadphonesDetected` / `UnsupportedFormat` /
  `Unknown`), `HrtfSettings` struct (mode + preferredDataset name),
  and the pure-function `resolveHrtfDatasetIndex(available,
  preferred)` that maps a user-chosen dataset name onto the
  driver-reported list (case-sensitive; empty preferred picks index
  0; unknown name returns -1). Headless — no OpenAL linkage so the
  tests run without an audio device.
- `AudioEngine::setHrtfMode(mode)` / `setHrtfDataset(name)` store
  the desired configuration and call `applyHrtfSettings()` which
  runs `alcResetDeviceSOFT` with the appropriate attribute list
  (`ALC_HRTF_SOFT=false` for `Disabled`, unset for `Auto`,
  `ALC_HRTF_SOFT=true` for `Forced`, plus `ALC_HRTF_ID_SOFT` when a
  valid dataset is named). Extension function pointers are loaded
  via `alcGetProcAddress` after `alcIsExtensionPresent` confirms
  availability — drivers without `ALC_SOFT_HRTF` leave the pointers
  null and every HRTF method becomes a no-op.
- `AudioEngine::getHrtfStatus()` queries `ALC_HRTF_STATUS_SOFT` and
  maps it to the portable `HrtfStatus` enum so settings UI / debug
  overlays can report what the driver actually decided (e.g.
  `Forced` → `Denied` on surround output).
- `AudioEngine::getAvailableHrtfDatasets()` enumerates the driver's
  `ALC_NUM_HRTF_SPECIFIERS_SOFT` + `ALC_HRTF_SPECIFIER_SOFT` pair
  and returns a `std::vector<std::string>` — empty if the extension
  is absent or the driver ships no datasets. Index order is
  driver-defined; index 0 is the default target when the user
  hasn't picked a dataset.
- `tests/test_audio_hrtf.cpp` — 10 new headless tests: mode labels
  stable, status labels stable, default settings (`Auto`, empty
  dataset), equality considers both fields, resolver handles empty
  available list / empty preferred / exact match / unknown name /
  case-sensitivity + trailing whitespace.

Rationale for the policy layer: HRTF is markedly worse than plain
panning on speakers (the listener's own ears double-convolve the
signal), so the engine ships with `Auto` as the default — the
driver's own headphone-detection heuristic flips HRTF on when a
stereo headset is present and leaves it off otherwise. `Forced`
exists for users whose driver doesn't auto-detect headphones
reliably; `Disabled` is the escape hatch for output configurations
where HRTF would degrade rather than improve positioning.

Reference: OpenAL Soft `ALC_SOFT_HRTF` extension specification and
the accompanying `alhrtf.c` example.

### 2026-04-21 Phase 10 audio — Doppler shift for fast-moving sources

Second Phase 10 spatial-audio slice, landing the Doppler sub-bullet
under "Spatial audio" in ROADMAP.md. Gives the engine a canonical
pitch-shift formula that matches what OpenAL evaluates natively, so
CPU-side priority / preview code and GPU-side playback agree.

- `engine/audio/audio_doppler.{h,cpp}` — new `DopplerParams`
  (`speedOfSound` defaults 343.3 m/s for dry air at 20 °C,
  `dopplerFactor` defaults 1.0 matching OpenAL 1.1 defaults) and
  pure-function `computeDopplerPitchRatio(params, srcPos, srcVel,
  listenerPos, listenerVel)`. Implements the OpenAL 1.1 §3.5.2
  formula `f' = f · (SS − DF·vLs) / (SS − DF·vSs)` with velocity
  projections clamped to [−SS/DF, SS/DF]; co-located source and
  listener return unity (no well-defined axis) and `dopplerFactor
  <= 0` disables the effect entirely.
- `AudioEngine::setDopplerFactor(factor)` / `setSpeedOfSound(speed)`
  push the values to `alDopplerFactor` / `alSpeedOfSound` and keep
  the engine's `DopplerParams` in sync with OpenAL's native state.
  `getDopplerParams()` exposes the current settings for CPU-side
  uses (virtual-voice priority, editor preview).
- `AudioEngine::setListenerVelocity(vec3)` — stores listener
  velocity; the next `updateListener` call uploads it as
  `AL_VELOCITY` (previously always hard-zero, which suppressed
  Doppler entirely).
- `AudioEngine::playSoundSpatial(path, position, velocity, params,
  volume, loop)` — new overload that sets per-source `AL_VELOCITY`
  in addition to the existing attenuation parameters. The
  velocity-less overload still zeroes `AL_VELOCITY` so stationary
  one-shots stay unaffected.
- `AudioSourceComponent` — new `glm::vec3 velocity` field (zero
  default so stationary emitters cost nothing to ship). `clone`
  carries it.
- `tests/test_audio_doppler.cpp` — 14 new tests: defaults match
  OpenAL spec, zero-velocity / co-located / disabled-factor /
  non-positive-speed pass-throughs, source-approach and
  source-recede sign conventions, listener-approach and
  listener-recede sign conventions, perpendicular motion producing
  no shift, both-approaching amplifies more than either alone,
  `dopplerFactor` scaling, and the [−SS/DF, SS/DF] velocity clamp
  for supersonic inputs staying finite and sign-correct.

Per CLAUDE.md Rule 11: the Doppler formula is canonical textbook
with no coefficients to fit, so the Formula Workbench flow (author
via fit + export) doesn't apply — the module ships as hand-written
math, matching the same treatment given to the distance-attenuation
curves in the previous slice.

### 2026-04-20 Phase 10 audio — Distance attenuation curves

First Phase 10 audio slice. Adds selectable distance-attenuation
curves for spatial sources, replacing the previous single-curve
(inverse-distance-clamped, hard-coded refDist=1 / maxDist=50)
behaviour with three canonical curves + a pass-through.

- `engine/audio/audio_attenuation.{h,cpp}` — new `AttenuationModel`
  enum (`None` / `Linear` / `InverseDistance` / `Exponential`) +
  `AttenuationParams` (referenceDistance / maxDistance /
  rolloffFactor). Pure-function `computeAttenuation(model, params,
  distance)` reproduces OpenAL's math for CPU-side uses (priority
  sorting, virtual-voice culling, editor preview).
- `alDistanceModelFor(model)` maps each model to the matching
  `AL_*_DISTANCE_CLAMPED` constant, returned as `int` so the header
  doesn't pull in `<AL/al.h>`.
- `AudioEngine::setDistanceModel(model)` swaps the engine-wide
  curve (defaults to `InverseDistance`, matching the Phase 9C
  behaviour — adoption is non-breaking).
- `AudioEngine::playSoundSpatial(path, position, params, volume,
  loop)` — new overload accepting `AttenuationParams`. Sets
  `AL_REFERENCE_DISTANCE`, `AL_MAX_DISTANCE`, `AL_ROLLOFF_FACTOR`
  per-source. The legacy `playSound` overload still ships its
  previous hard-coded values.
- `AudioSourceComponent` — new `attenuationModel` + `rolloffFactor`
  fields; `clone` carries them. Defaults match engine-wide defaults.
- `tests/test_audio_attenuation.cpp` — 15 new tests: model labels;
  model → AL-constant mapping; unity-gain-below-reference invariant
  across every curve; `None` pass-through at any distance; linear
  hits zero at max-distance, halfway point is half gain, clamps
  past max; inverse-distance matches classic formula at d=2 and
  d=5, monotonic falloff, clamps at max; exponential matches power
  formula including inverse-square at rolloff=2; flat at rolloff=0;
  clamps at max; negative-distance safety; zero-span linear safety;
  rolloff=0 flattens inverse-distance.

*Rule 11 note*: These are textbook canonical forms (OpenAL 1.1 spec
§3.4). They have no coefficients to fit against reference data, so
the Formula Workbench rule — use workbench for numerical design —
doesn't apply here. Each formula is documented inline with its
spec-section reference.

Follow-ups within the *"Spatial audio"* parent bullet: HRTF support
(OpenAL Soft ALC_HRTF_SOFT extension) and Doppler effect
(`alDopplerFactor` + per-source velocity).

### 2026-04-20 Phase 10 — DoF + motion-blur accessibility toggles

Final Phase 10 accessibility slice. Closes the last two accessibility
items on the roadmap: *"Depth-of-field toggle (off by default in
accessibility preset)"* and *"Motion-blur toggle (off by default in
accessibility preset)"*.

- `engine/accessibility/post_process_accessibility.{h,cpp}` — new
  `PostProcessAccessibilitySettings` struct with
  `depthOfFieldEnabled` + `motionBlurEnabled` bool fields, both
  defaulting to `true` (normal visual quality). `safeDefaults()`
  factory returns the struct with both flags flipped to `false` —
  the one-click "Accessibility preset" the settings screen applies
  when the user opts for the safest motion configuration.
- `tests/test_post_process_accessibility.cpp` — 5 new tests:
  both-effects-default-on (guards against a silent regression in
  shipped defaults), safeDefaults-disables-both, safeDefaults-
  distinct-from-zero-init (proves the one-click preset is not a
  no-op), equality matches all fields, per-field toggles are
  independent (migraine-from-DoF-only users can disable just one).

*Why ship the toggles before the effects?*  The DoF and motion-blur
effects themselves land in the Phase 10 Post-Processing Effects
Suite. Shipping the canonical toggle home now means (a) the settings
UI + persistence layer can wire the full accessibility preset today,
(b) user preferences survive the moment the effects appear — on
merge day each effect reads a single boolean from a settled location,
and (c) the "Accessibility preset" concept has a real type to hang
off rather than being a loose collection of individual toggles
invented on the fly.

References: WCAG 2.2 SC 2.3.3 ("Animation from Interactions"); Game
Accessibility Guidelines ("Avoid motion blur; allow it to be turned
off"); Xbox / Ubisoft accessibility guidelines (camera-blur
effects should be opt-out).

**Phase 10 accessibility complete** — all eight roadmap items shipped:
UI scale presets, high-contrast mode, colour-vision-deficiency
simulation, photosensitivity safe mode, subtitles, screen-reader
labels, remappable controls, DoF + motion-blur toggles. Suite:
2226 passing + 1 pre-existing GL-context skip.

### 2026-04-20 Phase 10 — Remappable controls (action map)

Sixth Phase 10 accessibility slice. Addresses the roadmap bullet
*"Fully remappable controls (keyboard, mouse, gamepad)"*. Ships the
data model + query path + GLFW integration; persistence and per-
game default loading are follow-ups.

- `engine/input/input_bindings.{h,cpp}` — new action-map
  architecture (Unity Input System / Unreal Enhanced Input / Godot
  InputMap pattern). `InputDevice` enum (None / Keyboard / Mouse /
  Gamepad). `InputBinding` with factory helpers (`key(glfwKey)`,
  `mouse(btn)`, `gamepad(btn)`, `none()`), equality, and
  `isBound()`. `InputAction` with id + label + category + three
  binding slots (primary / secondary / gamepad) + `matches(binding)`
  any-slot predicate.
- `InputActionMap` — insertion-order registry with a parallel
  defaults snapshot. APIs: `addAction` (re-registering an id
  replaces both the live entry and the defaults snapshot, matching
  editor hot-reload expectations), `findAction`,
  `findActionBoundTo` (reverse lookup), `findConflicts(binding,
  excludeSelfId)` (for rebind-UI "already assigned to X" warnings —
  excludes the currently-rebinding action so it doesn't flag
  itself), per-slot setters, `clearSlot(id, slot)`,
  `resetToDefaults()` (map-wide), and
  `resetActionToDefaults(id)` (single action — other user rebinds
  kept).
- `bindingDisplayLabel(binding)` — readable name for every GLFW
  key / mouse button / gamepad button. Gamepad names follow GLFW's
  Xbox layout convention (A / B / X / Y / LB / RB / D-Pad Up …);
  PlayStation users see that vocabulary per GLFW's documented
  translation. Unbound renders as em-dash "—".
- Pure-function `isActionDown(map, id, bindingChecker)` is the
  query path. `bindingChecker` is caller-supplied so tests run
  without a GLFW context. Handles null-checker + unknown-id
  gracefully.
- `engine/core/input_manager.{h,cpp}` — thin GLFW shim:
  `InputManager::isBindingDown` dispatches to
  `glfwGetKey` / `glfwGetMouseButton` / `glfwGetGamepadState` (the
  last polling every connected joystick slot so single-player
  users don't have to pick "player 1" before remaps work).
  `InputManager::isActionDown(map, id)` is a one-liner wrapping
  the free function with its own `isBindingDown` closure.
- `tests/test_input_bindings.cpp` — 30 new tests covering:
  `InputBinding` default / factory / equality; `InputAction`
  `matches()`; map insertion order; lookup; re-registration
  replacement; reverse lookup; conflict detection including
  self-exclusion; per-slot setters returning false for unknown
  ids; `clearSlot` valid + invalid indices; map-wide and single-
  action reset-to-defaults; keyboard / mouse / gamepad display
  labels; unbound em-dash; `isActionDown` true/false paths; any-
  of-three-slots sufficiency; unknown-id; no-slots-bound; null
  binding checker.

Follow-ups (intentionally not in this slice): JSON save/load of
user rebinds (trivial additional I/O layer once Phase 10's
settings-persistence story is chosen), per-game default action-map
bundles, and routing the existing `FirstPersonController` /
engine input paths through the action map (currently they still
call `isKeyDown(GLFW_KEY_W)` directly — a mechanical swap best
done as a dedicated slice so input regressions are easy to
bisect).

### 2026-04-20 Phase 10 — Screen-reader / ARIA-like UI semantics

Fifth Phase 10 accessibility slice. Addresses the roadmap bullet
*"Screen-reader friendly UI labels (ARIA-like semantic tags on
widgets where feasible)"*. Ships the metadata layer and tree-walk
enumeration that a future TTS / screen-reader bridge consumes — the
bridge itself is deferred pending a platform-dependent design
decision (AT-SPI vs. UIAutomation vs. a cross-platform VoiceOver-
style in-engine reader).

- `engine/ui/ui_accessible.{h,cpp}` — new `UIAccessibleRole` enum
  (`Button` / `Checkbox` / `Slider` / `Dropdown` / `KeybindRow` /
  `Label` / `Panel` / `Image` / `ProgressBar` / `Crosshair` /
  `Unknown`) + `UIAccessibleInfo` struct (role + label +
  description + hint + value, mirroring WAI-ARIA 1.2's
  `role / aria-label / aria-describedby / aria-keyshortcuts /
  aria-valuetext`). `uiAccessibleRoleLabel(role)` returns a stable
  human-readable string used by tests and debug panels.
- `engine/ui/ui_element.{h,cpp}` — every `UIElement` now carries
  an `m_accessible` member exposed via `accessible()` getter pair.
  New virtual `collectAccessible(vector<Snapshot>&)` walks the
  subtree and emits an entry per element that has either a non-
  `Unknown` role or a non-empty label. Hidden subtrees are skipped
  entirely (a screen reader must not announce UI the sighted user
  cannot see).
- `engine/ui/ui_canvas.{h,cpp}` — new `UICanvas::collectAccessible()`
  returns the canvas-wide flat snapshot list.
- Every shipping widget sets its role in its constructor:
  `UIButton` → Button, `UICheckbox` → Checkbox, `UISlider` → Slider,
  `UIDropdown` → Dropdown, `UIKeybindRow` → KeybindRow,
  `UILabel` → Label, `UIPanel` → Panel, `UIImage` → Image,
  `UIProgressBar` → ProgressBar, `UICrosshair` → Crosshair.
  Context-specific strings (label / value / hint) stay caller-side:
  menus set `btn->accessible().label = "Play Game"` where the
  widget is wired up.
- `tests/test_ui_accessible.cpp` — 13 new tests covering: role-label
  lookup, per-widget default role, default-empty strings, mutable
  label / description / hint, Unknown-with-empty-label omission,
  role-alone-is-enough, label-alone-is-enough, hidden-subtree
  exclusion, interactive-flag carry-over, child-order walk,
  unlabelled container passthrough, canvas enumeration, empty
  canvas returns empty vector.

Scope note: ImGui editor widgets are a separate surface. They need
per-call-site label attachment rather than per-type constructor-
set roles, so they are deliberately out of scope for this slice.

### 2026-04-20 Phase 10 — Subtitle / closed-caption queue + size presets

Fourth Phase 10 accessibility slice. Addresses the roadmap bullet
*"Subtitle / closed caption system for spatial audio cues, with size
presets (Small / Medium / Large / XL)"*.

- `engine/ui/subtitle.{h,cpp}` — headless `SubtitleQueue` FIFO with
  per-tick countdown and push-newest / drop-oldest overflow. Default
  concurrent cap is 3, matching BBC caption guidelines and the
  2–3-lines-at-once recommendation from Romero-Fresco 2019 reading-
  speed research. `clear()` for scene transitions;
  `setMaxConcurrent(n)` trims in-place if n < current size.
- `Subtitle` authored struct: `text`, `speaker`, `durationSeconds`,
  `category` (`Dialogue` / `Narrator` / `SoundCue`), and
  `directionDegrees` (0 = front, 90 = right, … — ready for spatial
  audio integration to surface a direction hint).
- `SubtitleSizePreset` ladder (`Small` 1.00× / `Medium` 1.25× /
  `Large` 1.50× / `XL` 2.00×) with `subtitleScaleFactorOf(preset)`
  helper. Ratios intentionally mirror `UIScalePreset` so a user who
  knows the UI ladder understands the caption ladder, and the two
  compose: a consumer multiplies `UITheme::typeCaption` by the
  subtitle factor, then by the UI-wide scale factor.
- `tests/test_subtitle.cpp` — 17 new tests: size-preset ladder;
  empty-queue baseline; enqueue / tick countdown; zero-second
  expiry; over-budget long-frame expiry; selective expiry; FIFO
  order; overflow eviction; `setMaxConcurrent` trim-in-place; raising
  the cap no-op; `clear`; category + spatial-direction round-trip;
  dialogue-speaker preservation; negative-duration clamp.

Rendering is deliberately out of scope for this slice — the queue is
headless so it can be unit tested without GL context and reused by any
future UI register (HUD caption strip, log overlay, etc.). Rendering
lands when audio-event wiring is designed, at which point the renderer
reads `queue.activeSubtitles()` and draws each entry at
`typeCaption × subtitleScaleFactor × uiScaleFactor` pixels.

### 2026-04-20 Phase 10 — Photosensitivity safe mode (reduced flashing)

Third Phase 10 accessibility slice. Addresses the roadmap bullet
*"Reduced-flashing / photosensitivity safe mode (caps camera shake,
strobes, muzzle-flash alpha)"*.

- `engine/accessibility/photosensitive_safety.{h,cpp}` — new
  `PhotosensitiveLimits` struct with published, research-grounded caps
  (WCAG 2.2 SC 2.3.1 "Three Flashes or Below Threshold", Epilepsy
  Society photosensitive-games guidance, IGDA GA-SIG / Xbox / Ubisoft
  accessibility best-practice bullets): max flash α 0.25, shake scale
  0.25, max strobe 2 Hz, bloom intensity scale 0.6. Four pure-function
  helpers — `clampFlashAlpha`, `clampShakeAmplitude`, `clampStrobeHz`,
  `limitBloomIntensity` — that subsystems call before handing values
  downstream. Identity pass-through when disabled — zero runtime cost.
  Per-caller override of the defaults (e.g. a horror sequence tightening
  the flash ceiling) via an optional `limits` parameter.
- `engine/ui/ui_theme.{h,cpp}` — new `UITheme::withReducedMotion()`
  pure transform that zeroes `transitionDuration` (the reduce-motion
  hook that was already flagged in the field's doc comment). Palette
  and sizing are left untouched so the transform composes cleanly.
- `engine/systems/ui_system.{h,cpp}` — `setReducedMotion(bool)` /
  `isReducedMotion()` accessibility accessors. `rebuildTheme` now
  composes **scale → high-contrast → reduced-motion**, so users can
  run any combination of the three accessibility toggles
  simultaneously.
- `tests/test_photosensitive_safety.cpp` — 13 new tests covering:
  disabled-is-identity for every helper, published default limits,
  flash-alpha ceiling clamping, shake amplitude scaling, strobe-Hz
  ceiling, bloom intensity scaling, and per-caller override of the
  defaults.
- `tests/test_ui_theme_accessibility.cpp` — 5 new tests covering:
  `withReducedMotion` zeros `transitionDuration`, leaves palette +
  sizes untouched, `UISystem::setReducedMotion` rebuilds the active
  theme, reduced-motion composes with scale + high-contrast, and
  toggling off restores the base transition timing.

Today's accessibility composition surface: UI scale 1.0×/1.25×/1.5×/
2.0× + high-contrast mode + reduced-motion mode + colour-vision-
deficiency simulation. Each stage is an independent pure transform;
all four can run simultaneously. The clamp helpers are ready for
future shake/flash/strobe systems to consult — they currently wire
only into the UI transition duration because that's the only
reduced-motion-sensitive system the engine ships today.

### 2026-04-20 Phase 10 — Colorblind simulation filter (CVD matrices)

Second Phase 10 accessibility slice. Addresses the roadmap bullet
*"Colorblind modes (Deuteranopia, Protanopia, Tritanopia LUT modes
applied post-tonemap)"*.

- `engine/renderer/color_vision_filter.h/.cpp` — new `ColorVisionMode`
  enum (`Normal`, `Protanopia`, `Deuteranopia`, `Tritanopia`) and a
  `colorVisionMatrix(mode)` lookup returning the 3×3 RGB simulation
  matrix. Coefficients are the canonical Viénot/Brettel/Mollon 1999
  dichromat projections — the dataset cited by Unity, Unreal, and the
  IGDA GA-SIG accessibility guidance. `colorVisionModeLabel(mode)`
  provides a stable string for future settings UIs.
- `assets/shaders/screen_quad.frag.glsl` — two new uniforms
  (`u_colorVisionEnabled`, `u_colorVisionMatrix`) applied between the
  artistic color-grading LUT and the sRGB gamma conversion, so the
  simulation reflects the final displayed colour. Clamped to `[0,1]`
  to contain any minor over/undershoot from the matrix multiply.
- `engine/renderer/renderer.{h,cpp}` — added `setColorVisionMode` /
  `getColorVisionMode` and `m_colorVisionMode` (default `Normal`).
  The composite pass sets `u_colorVisionEnabled=false` in the Normal
  case so the multiply is skipped — zero-cost when off.
- `tests/test_color_vision_filter.cpp` — 12 new tests covering:
  identity transform, labelling, Brettel coefficient values per mode,
  row-sum-1 invariant (equivalent to preserving achromatic input),
  characteristic dichromat projections (red→yellow for protanopes,
  green shifted toward red for deuteranopes, blue→cyan-band for
  tritanopes), and black/white fixed-point preservation across all
  three modes.

Composes with the existing UI accessibility state: a partially-sighted
user with colour-vision deficiency can run UI scale 1.5× + high-
contrast + a CVD simulation mode simultaneously; each stage is an
independent transform. The simulation is off by default; enable via
`Renderer::setColorVisionMode(ColorVisionMode::...)` or a future
settings panel.

### 2026-04-20 Phase 10 — UI scaling presets + high-contrast mode

First Phase 10 accessibility slice. Addresses two roadmap bullets
directly: *"UI scaling presets (1.0× / 1.25× / 1.5× / 2.0× — minimum
1.4× recommended for partially-sighted users)"* and *"High-contrast
mode for UI elements"*.

- `engine/ui/ui_theme.h/.cpp` — added `UIScalePreset` enum + free
  function `scaleFactorOf(preset)` returning the numeric multiplier.
  Added two pure-function transforms on `UITheme`:
  - `UITheme::withScale(factor)` — returns a copy with every pixel
    size field (buttons, sliders, checkboxes, dropdowns, keybinds,
    type sizes, crosshair, focus ring, panel borders, progress bars)
    multiplied. Palette, motion timing (`transitionDuration`), and
    font family names are intentionally left untouched.
  - `UITheme::withHighContrast()` — returns a copy with a pure-
    black / pure-white palette, full-alpha panel strokes, and a
    saturated amber accent. Sizes stay untouched so high-contrast
    composes cleanly on top of any scale preset.
- `engine/systems/ui_system.{h,cpp}` — `UISystem` now tracks a base
  theme (`m_baseTheme`), a scale preset, and a high-contrast flag.
  New API: `setBaseTheme`, `getBaseTheme`, `setScalePreset`,
  `getScalePreset`, `setHighContrastMode`, `isHighContrastMode`.
  Each setter triggers an idempotent `rebuildTheme` that composes
  `withScale` then (if enabled) `withHighContrast` onto the base.
- `tests/test_ui_theme_accessibility.cpp` — 14 new tests: preset
  factors, full-field scale coverage at 1.5×, palette-and-motion
  invariants under scale, identity-at-1.0 fixed point, high-contrast
  palette invariants (pure-black bg, pure-white text, full-alpha
  strokes, discriminable disabled text), UISystem defaults, scale
  rebuild, high-contrast rebuild, composition of the two, toggle-off
  round-trip, and `setBaseTheme` preserving active scale. Suite
  2117 → 2131.

### 2026-04-20 ROADMAP housekeeping — Phase 9F-4 checkbox flipped

Line 873 was a stale unchecked *"2D character controller"* item that
had actually been shipped in commit ec62677 (Phase 9F-4) alongside
the 2D camera. Flipped to `[x]` with a backpointer to the
implementation location and the feature set that landed (coyote
time, jump buffering, variable-jump cut, wall slide, ground/air
acceleration, ground friction).

### 2026-04-20 Cursor Bridge — MCP-driven editor tab management

Shipped a two-part local bridge that lets Claude Code (or any MCP
client) drive Cursor / VS Code tab state. A companion to the official
Claude Code extension which already handles inline diffs and
selection-as-context — this adds tab *management* (open, focus, close-
others, list, reveal) that the official extension does not expose.

Lives under `tools/cursor_bridge/`:

- `extension/` — VS Code extension, Cursor-compatible via the standard
  extension API. TypeScript, listens on `127.0.0.1:39801` (loopback
  only — no remote exposure). NDJSON protocol, one request per line:
  `{ id, command, args }` → `{ id, ok, result | error }`. Six commands:
  `ide_open_file`, `ide_focus`, `ide_close_others`,
  `ide_close_all_except`, `ide_get_open_tabs`, `ide_reveal_in_explorer`.
  Non-file tabs (settings, walkthroughs, diff editors) are left alone
  by the close helpers.
- `mcp_server/` — Node MCP server (stdio transport). Registers the six
  tools, forwards each call over TCP to the extension. Short-lived
  per-call connections + 5 s timeout so a reloaded extension doesn't
  leave the server in a bad state.
- `README.md` with install steps (sideload the .vsix, register the MCP
  server in `~/.claude/mcp_config.json`, restart Claude Code).

Both TypeScript projects compile cleanly with `npm run compile` and
ship their own `.gitignore` so `node_modules/` and `dist/` stay out of
the tree.

### 2026-04-20 Phase 9F-6 — Editor 2D panels + template dialog wiring

Shipped the editor-side hooks that let designers work with 2D scenes
without leaving the IDE.

- `engine/editor/panels/sprite_panel.{h,cpp}` — loads a TexturePacker
  JSON atlas, lists its frames, assigns the atlas (and optionally a
  specific frame) to the selected entity's SpriteComponent. Adds the
  component when the selected entity doesn't already have one.
- `engine/editor/panels/tilemap_panel.{h,cpp}` — layer list
  (active-layer picker + add), resize knobs, tile palette picker (keyed
  off `TilemapComponent::tileDefs`), and headless `paintCell` /
  `eraseCell` for the viewport brush (wired from the viewport click
  pipeline — the paint helpers are already public so scripted/tested
  flows can drive them).
- Template dialog: added `GameTemplateType::SIDE_SCROLLER_2D` and
  `SHMUP_2D` (total 6 → 8). Dispatch in `applyTemplate` routes 2D types
  to `createSideScrollerTemplate` / `createShmupTemplate` from Phase
  9F-5 instead of the 3D flow.
- 6 new tests covering atlas load, panel visibility toggles, and
  paint/erase operations. Updated `test_editor_viewers.cpp`'s
  `TemplateCount` test from 6 → 8. Full suite **2120 → 2126 passing**.

Auto-tiling, slicing from a raw PNG, and the viewport-click paint
pipeline are Phase 18 polish per the design doc.

### 2026-04-20 Phase 9F-5 — 2D game-type templates (Side-Scroller + Shmup)

Ship two starter-scene generators designers can instantiate from the
editor (9F-6 wires them into the TemplateDialog). Each template
composes the existing 2D components (SpriteComponent, RigidBody2D,
Collider2D, CharacterController2D, Camera2D, Tilemap) into a wired
scene that Just Works out of the box.

- `engine/scene/game_templates_2d.{h,cpp}`:
  - `createSideScrollerTemplate(scene, config)` — player (capsule,
    fixedRotation, CharacterController2D), ground, two platforms, and a
    smoothed-follow camera clamped to world bounds.
  - `createShmupTemplate(scene, config)` — kinematic gravity-free player,
    scrolling-backdrop tilemap on sorting layer -100, locked
    orthographic camera.
  - Optional atlas binding via `GameTemplate2DConfig`: when provided,
    the template attaches SpriteComponents with the config-specified
    frame names; when omitted the structure ships without sprites so
    designers can drop assets later.
- 9 new unit tests covering entity layout, component presence / types,
  camera configuration, and graceful no-atlas fallback.

### 2026-04-20 Phase 9F-4 — 2D camera + platformer character controller

Shipped the 2D camera (ortho smooth-follow with deadzone + world bounds)
and the platformer character controller (coyote time, jump buffering,
variable jump cut, wall slide, ground friction). Both ship as
component + free-function pairs rather than new ISystem classes —
callers decide when to step them (editor, game loop, scripted sequence)
without paying for an auto-drive in scenes that don't use them.

- `engine/scene/camera_2d_component.{h,cpp}` — orthoHalfHeight, follow
  offset, deadzone, smoothTimeSec, maxSpeed, worldBounds clamp. The
  critical-damped spring integrator is the same formula Unity's
  SmoothDamp uses; first-frame snap avoids a visible sweep-in.
  `updateCamera2DFollow(camera, target, dt)` is the step helper.
- `engine/scene/character_controller_2d_component.{h,cpp}` — tuning
  (maxSpeed, acceleration, airAcceleration, groundFriction,
  jumpVelocity, variableJumpCut, coyoteTimeSec, jumpBufferSec,
  wallSlideMaxSpeed) + runtime state (onGround, onWall,
  timeSinceGrounded, jumpBufferRemaining, jumpingFromBuffer).
  `stepCharacterController2D(ctrl, entity, physics, input, dt)` reads
  the body's current velocity, applies input + timers + friction +
  wall-slide, writes a new velocity, returns true on jump so callers
  can trigger SFX / particles.
- 16 new unit tests: camera deadzone suppression, follow after leaving
  deadzone, bounds clamp, zero-smooth instant snap, clone reset;
  controller acceleration, jump on ground, coyote-time late jump,
  buffered jump on landing, buffer expiry, variable jump cut, wall-slide
  cap, ground friction decel. Full suite **2086 → 2111 passing**.

### 2026-04-20 Phase 9F-3 — Tilemap component + renderer helper

Shipped multi-layer tilemaps with animated tiles. The tilemap is just
another consumer of the sprite atlas — tilemap cells convert into
SpriteInstance records that feed the existing SpriteRenderer, so there
is no dedicated tilemap shader or draw path. This keeps sprites and
tilemaps in a single z-ordered pass.

- `engine/scene/tilemap_component.{h,cpp}` — TilemapLayer (dense grid,
  row-major bottom-first), TileId (uint16, 0 = empty), TilemapTileDef
  (maps an ID to an atlas frame or an animated sequence),
  TilemapAnimatedTile (frame list + framePeriodSec + ping-pong flag).
  Animation time wraps at 1 hour to keep float precision tight in long
  gameplay sessions.
- `engine/renderer/tilemap_renderer.{h,cpp}` — pure helper
  `buildTilemapInstances(tilemap, worldMatrix, depth, outInstances)` —
  no GL, no state. Called by the sprite pass to emit one instance per
  visible cell. Tilemap origin = entity position; column 0 / row 0 at
  the origin.
- 12 new unit tests covering layer resize overlap, out-of-bounds set/get,
  animated-tile time-based resolution, forEachVisibleTile short-circuit,
  clone semantics, and instance-vector construction.

### 2026-04-20 Phase 9F-2 — 2D physics via Jolt Plane2D DOF lock

Shipped the 2D physics subsystem on top of the existing Jolt 5.2.0 build.
No new third-party dependency — per-body `EAllowedDOFs::Plane2D` locks Z
translation and X/Y rotation, so 2D bodies share the same broadphase,
narrowphase, and contact solver as the 3D world. A mixed 2D+3D scene now
works out of the box.

- `engine/scene/rigid_body_2d_component.{h,cpp}` — BodyType2D (Static /
  Kinematic / Dynamic), mass, friction, restitution, damping, gravity
  scale, fixedRotation, collision bits; runtime fields (bodyId,
  linearVelocity, angularVelocity) cached from Jolt each step.
- `engine/scene/collider_2d_component.{h,cpp}` — shape descriptor
  (Box / Circle / Capsule / Polygon / EdgeChain), trigger-mode sensor
  flag, zThickness + zOffset for the extruded-slab representation.
- `engine/systems/physics2d_system.{h,cpp}` — ISystem registered after
  SpriteSystem. Shares the Engine's PhysicsWorld via
  `getPhysicsWorld()`; `ensureBody` / `removeBody` / `applyImpulse` /
  `setLinearVelocity` / `setTransform` expose a 2D-native API that
  hides JPH::Vec3 plumbing. Dedicated `setPhysicsWorldForTesting` test
  seam lets the test suite spin up a standalone PhysicsWorld without
  Engine bootstrap.
- Jolt `cDefaultConvexRadius = 0.05f` collision: authored zThickness
  smaller than 0.12 is silently widened in `makeShape` so designers
  don't have to think about Jolt's internal margin.
- **15 new unit tests** — DOF lock, gravity fall, static-floor rest,
  impulse/velocity, shape coverage (box, circle, capsule, polygon,
  edge chain), degenerate-shape rejection, sensor pass-through,
  fixed-rotation lock. Full suite now **2074 tests**.

### 2026-04-20 Phase 9F-1 — sprite foundation (atlas, animation, instance-rate renderer)

Shipped the 2D-sprite rendering foundation. Sprites now have atlas-backed
frame lookup, Aseprite-compatible per-frame animation, and an instance-rate
batched renderer separate from the UI's `SpriteBatchRenderer`. Game sprites
pack one affine transform + UV rect + tint + depth per instance (80 bytes)
and draw in a single `glDrawArraysInstanced` per (atlas, pass). The
`SpriteSystem` collects, sorts, and batches — all three steps are headless
so tests validate the CPU pipeline without a GL context.

- `engine/renderer/sprite_atlas.{h,cpp}` — TexturePacker JSON loader
  (array + hash forms), pre-normalised UVs, optional per-frame pivots.
- `engine/animation/sprite_animation.{h,cpp}` — per-frame-duration clips,
  forward / reverse / ping-pong direction, loop control.
- `engine/scene/sprite_component.{h,cpp}` — attachable component with
  atlas + frameName + tint + pivot + flips + pixelsPerUnit + sorting
  layer/order + sortByY + isTransparent.
- `engine/renderer/sprite_renderer.{h,cpp}` — instance-rate VBO with a
  static 4-vertex corner quad; depth / blend state restored on `end()` so
  the sprite pass doesn't disturb the 3D pipeline.
- `engine/systems/sprite_system.{h,cpp}` — `ISystem`, registered in
  `Engine::initialize` after `NavigationSystem`. Render path not yet
  wired into the frame loop (waits for Phase 9F-4's 2D camera for a
  proper view-projection).
- `assets/shaders/sprite.vert.glsl` / `sprite.frag.glsl` — shared shader
  pair; vertex shader reconstructs the 2D affine from two packed rows to
  avoid wasted floats per instance.
- **27 new unit tests** across atlas, animation, sort/batch, instance
  packing, depth monotonicity, and component cloning. Full suite now at
  **2059 tests** (was 2032).
- Fixed a move-before-read bug in `SpriteAnimation::addClip` surfaced by
  the replace-clip test: cache the key before `std::move(clip)`.
- Design doc: `docs/PHASE9F_DESIGN.md`.

### 2026-04-20 Post-Phase-9E audit — formula-workbench dangling-temp fix + audit 2.14.1

Ran the full audit stack (cppcheck, semgrep p/security-audit, gitleaks,
custom `tools/audit/audit.py` tiers 2-3) against the post-Phase-9E
working tree. Baseline clean: build 0 warnings / 0 errors, 2032 tests
passing, 0 HIGH/CRITICAL from any tool. Findings breakdown:

- **Fixed — dangling const-ref to ternary temporary**
  (`tools/formula_workbench/formula_node_editor_panel.cpp:196`).
  `const std::string& sweepLabel = cond ? std::string("<auto>") :
  m_preview.sweepVariable;` technically works (the common-type
  materialised temporary gets lifetime-extended through the const ref)
  but is brittle and cppcheck flags it `danglingTemporaryLifetime`.
  Dropped the `&` — now stores by value, same cost under NRVO/elision,
  no lifetime question.
- **Audit-tool 2.14.1 — `c_style_cast` FP filter.** All 19 tier-2
  Memory-Safety matches were FPs (parameter decls with `/*comment*/`
  names that `skip_comments` preprocessed into `(float )`, plus
  function-pointer type syntax like `float (*)(float)` where the
  trailing `(float)` matched the cast regex). Tightened the regex to
  require an operand after the close paren — tier-2 finding total
  dropped 231 → 212 with zero lost signal. See
  `tools/audit/CHANGELOG.md` [2.14.1].
- **Ignored (false positives).** 83 gitleaks hits in
  `build/_deps/imgui-src/` (third-party). 2 cppcheck
  `returnDanglingLifetime` in `engine/scene/entity.cpp` — already have
  inline suppressions, manual cppcheck invocation was missing
  `--inline-suppr` (audit.py passes it correctly). 2
  `duplicateAssignExpression` on Ark-of-the-Covenant dimensions
  (1.5 cubits = 1.5 cubits per Exodus 25:10) and cube-face mip dims.
  3 semgrep hits in `tools/audit/lib/` (dedicated `run_shell_cmd`
  wrapper with explicit contract; NVD API URL uses fixed domain + url-
  encoded query + 16 MB body cap per AUDIT.md §L7).

### 2026-04-20 Phase 9E-3 runtime verification closed — node layout survives restart

Runtime-verified the Script Editor's imgui-node-editor integration end-to-end
(clean shutdown + layout restore), closing the last unchecked box under
Phase 9E-3. The shutdown SEGV fix from the earlier commit already worked,
but dragged node positions reset to the template defaults on every relaunch
because the template-load code force-called `ed::SetNodePosition` for every
node — stomping the positions the library had just restored from
`~/.config/vestige/NodeEditor.json`.

- `NodeEditorWidget` now parses `NodeEditor.json` at init and exposes
  `hasPersistedPosition(nodeId)`. The parse handles the library's
  `"node:<id>"` key format (see `Serialization::GenerateObjectName` in
  `imgui_node_editor.cpp`) and the older bare-integer form. nlohmann/json
  is already a dep; no new externals.
- `ScriptEditorPanel::renderGraph` skips the template-default seed for
  nodes that already have a saved position, so the library's
  `CreateNode` → `UpdateNodeState` path wins and the user's drags
  survive. Nodes that aren't in the settings file (fresh template on a
  clean profile) still get seeded from `ScriptNodeDef::posX/posY`, so
  multi-node templates no longer stack at the origin on first launch.
- The save-callback path merges new ids into the persisted set instead
  of replacing it, because the library serializes only nodes that have
  already been referenced via `BeginNode` / `SetNodePosition`. A save
  fired at end-of-frame before the panel has rendered any nodes would
  otherwise write an empty `"nodes"` object and clear the set we just
  populated from disk.
- Manual test (Door Opens template, node:2 dragged from (220,0) to
  (223,-153), editor closed, relaunched, template re-picked): node
  reappears at the dragged position, all links reroute correctly, no
  crash on shutdown.

### 2026-04-20 Phase 9E-5 — ScriptGraphCompiler (graph → validated IR)

Closes the "Graph compilation to executable logic (beyond expression trees)"
item under Phase 9E. Visual scripting graphs now go through a dedicated
validation + lowering pass at load time, so broken graphs (unknown node
types, dangling connections, pin-type mismatches, pure-data cycles)
surface as a single clear error before any chain runs — instead of a
partial trace mid-dispatch.

- `engine/scripting/script_compiler.h/.cpp` add `ScriptGraphCompiler`,
  `CompiledScriptGraph`, `CompiledNode`, `CompiledInputPin`,
  `CompiledOutputPin`, `CompilationResult`, and `CompileDiagnostic`. The
  compiler is stateless and never throws — even a null-registry / empty
  graph input returns a usable result with an "empty graph" warning.
- Validation passes: node type resolution against the registry, unique
  node ids, connection endpoint resolution (source + target node and pin
  names by string lookup), pin kind match (exec↔exec, data↔data), pin
  data-type compatibility (ANY wildcard, same-type, and whitelisted
  widenings — INT→FLOAT, BOOL→INT/FLOAT, ENTITY→INT, COLOR↔VEC4, and
  all types → STRING, mirroring `ScriptValue` runtime coercions so the
  compile-time check never rejects a connection the interpreter would
  accept), input fan-in ≤ 1, pure-data cycle detection via iterative
  DFS (execution cycles and execution-output fan-out are intentionally
  permitted — loops, re-triggers, and `DoOnce.Then → Anim + Sound`
  templates depend on both), entry-point discovery (event nodes, OnStart,
  OnUpdate, and anything in the `Events` category so stub events like
  OnTriggerEnter / OnCollisionEnter still register as roots), and
  reachability classification that warns on orphaned impure nodes while
  ignoring pure library helpers.
- `ScriptingSystem::registerInstance` runs the compiler and refuses to
  activate instances that produce fatal errors, logging each diagnostic
  against the graph name. Warnings still activate so library-style
  graphs with no entry points don't get silently dropped.
- `CompiledScriptGraph` stores flat index-based wiring
  (`sourceNodeIndex` / `sourceOutputPinIndex` / `targetNodeIndices`) and
  `indexForNodeId()` so future codegen or bytecode back-ends can consume
  the IR without hashing node ids or pin names.
- Tests: 16 new cases in `tests/test_script_compiler.cpp` —
  every shipped gameplay template compiles clean, every error class
  exercised (unknown type, dangling connection, duplicate node id,
  duplicate input connection forged past `addConnection`'s editor-side
  dedupe, pin kind mismatch, pin data-type mismatch, pure-data cycle),
  entry-point discovery (OnUpdate), unreachable impure node warning,
  exec fan-out accepted, exec cycle accepted, full type-compatibility
  matrix, and resolved-wiring round-trip. Full suite: 2032 / 2032 pass.

### 2026-04-20 Phase 9E — Formula Node Editor panel (visual composition, drag-drop, live preview)

Closes the three remaining `Formula Node Editor` roadmap items under
Phase 9E in one panel inside the FormulaWorkbench:

- Visual formula composition UI (ImGui node editor canvas over
  `NodeGraph`, rendering every node's pins / links from the graph's
  own port layout — same `NodeEditorWidget` used by
  `ScriptEditorPanel`, separate `ed::EditorContext` so state cannot
  leak between the two canvases).
- Drag-and-drop from the `PhysicsTemplates` catalog into the graph
  (ImGui `BeginDragDropSource` → `FORMULA_TEMPLATE` payload →
  `AcceptDragDropPayload` on the canvas child). Click-to-load is the
  keyboard-friendly fallback.
- Output-node curve preview rendered via ImPlot under the canvas;
  samples the graph across a user-configurable sweep variable + range
  + sample count and plots the result every frame.

**Files.** `tools/formula_workbench/formula_node_editor_panel.{h,cpp}`
(panel + ImGui / ImPlot rendering) and `formula_node_editor_core.cpp`
(headless state: constructor, `initialize` / `shutdown`, `loadTemplate`,
`recomputePreview`, and the `sampleFormulaCurve()` / `findOutputNodeId()`
free helpers). Split is deliberate — tests link only `core.cpp` +
`vestige_engine` and exercise the full state machine without pulling
in ImGui / ImPlot / imgui-node-editor. `Workbench` owns one instance;
`View → Node Editor` toggles visibility; lifecycle hooked through new
`Workbench::initializeGui()` / `shutdownGui()` so `ed::DestroyEditor`
runs before `ImGui::DestroyContext` (same pattern as `ScriptEditorPanel`).

**Sampler guarantees.** `sampleFormulaCurve()` never throws — all
`ExpressionEvaluator` exceptions funnel into
`FormulaCurveSample::error`. Behaviours: empty graph / missing output
node / broken tree each set a descriptive error string; constant-only
graphs fan out to a flat line instead of a single point; auto-pick
selects the first variable referenced by the tree; unknown explicit
sweep variable silently falls back to auto-pick (so stale UI selection
from a previous template doesn't flash errors); unbound variables
default to 0.0f; `sampleCount` is clamped to `[2, 4096]`.

**Tests.** 13 new tests in `tests/test_formula_node_editor_panel.cpp`
covering the sampler behaviour matrix above plus the panel's state
transitions (load / unload / sweep-range update). Monotonicity check
on `ease_in_sine`, analytical-linearity check on `aerodynamic_drag`
swept on `vDotN`. Test-suite total: 2016 / 2016 (1 pre-existing skip).

`WORKBENCH_VERSION` bumped to `1.16.0`.

### 2026-04-20 Phase 9E-4 — gameplay templates menu in ScriptEditorPanel

Wires the 5 shipped gameplay templates into the Script Editor menu bar
so they're one click away from the canvas rather than buried behind a
C++ call. New `Templates` menu (between `File` and `View`) lists
Door Opens / Collectible Item / Damage Zone / Checkpoint /
Dialogue Trigger; hovering each item shows its one-line description,
clicking replaces the current graph with the template (sets `m_dirty`
and clears `m_currentPath` so the next save prompts for a path).

### 2026-04-20 Phase 9E-4 — pre-built gameplay script templates

Designer-side starter graphs for the five gameplay patterns called out
in the Phase 9E roadmap (door that opens, collectible item, damage
zone, checkpoint, dialogue trigger). New module
`engine/scripting/script_templates.{h,cpp}` exposes:

- `GameplayTemplate` enum (five values above).
- `buildGameplayTemplate(GameplayTemplate)` → fully-wired `ScriptGraph`
  whose `.name` matches the template so loaded instances survive
  round-trip through JSON.
- `gameplayTemplateDisplayName` / `gameplayTemplateDescription` for
  editor palette presentation.

All templates start from `OnTriggerEnter` — the stub event on the
EventBus side is already registered in the node registry with the
correct pin set, so the graphs are valid *now* and fire automatically
the moment trigger / collision events are wired through.

**Template wiring summary** (per-instance property defaults set via
`ScriptNodeDef::properties` so the graph is self-explanatory):

- `DOOR_OPENS` — `OnTriggerEnter` → `DoOnce` fan-outs to `PlayAnimation`
  ("DoorOpen", blend 0.2s) and `PlaySound`
  ("assets/sounds/door_open.ogg", vol 0.8).
- `COLLECTIBLE_ITEM` — `OnTriggerEnter` → `PlaySound`
  ("assets/sounds/pickup.ogg") → `SetVariable` (score ← 1) →
  `DestroyEntity` (self, via entity-input 0 fallback).
- `DAMAGE_ZONE` — `OnTriggerEnter` → `PublishEvent` ("damage",
  payload 10).
- `CHECKPOINT` — `OnTriggerEnter` → `DoOnce` → `SetVariable`
  ("lastCheckpoint" ← piped `otherEntity`) → `PrintToScreen`.
- `DIALOGUE_TRIGGER` — `OnTriggerEnter` → `DoOnce` → `PublishEvent`
  ("dialogue_started", "greeting") → `PrintToScreen`.

Tests: 8 in `tests/test_script_templates.cpp` — each of the 5 templates
validates and every connection's pin names resolve against the
populated `NodeTypeRegistry`; JSON round-trip preserves graph shape;
metadata coverage for all enum values; a sanity invariant that every
shipped template starts from `OnTriggerEnter`. Test-suite total:
2003 / 2003 (1 pre-existing skip).

### 2026-04-20 Phase 9E — CONDITIONAL node type (formula ternary round-trip)

Closes the last lossy conversion path in the Formula Workbench → node
graph round-trip. `ExprNodeType::CONDITIONAL` (ternary `if/then/else`)
now has a dedicated node-graph representation rather than silently
falling back to `literal(0)` with a warning.

**New API.** `NodeGraph::createConditionalNode()` builds an `If` node
with 3 typed float inputs (`Condition`, `Then`, `Else`) and a single
`Result` output, categorised as `INTERPOLATION` alongside lerp /
smoothstep.

**Bidirectional conversion.** `fromExpressionTree` now emits a real
conditional node with its three branch sub-trees wired up, and
`nodeToExpr` dispatches by `operation == "conditional"` before the
generic 1-input / 2-input branches so a 3-input node doesn't fall
through to the `literal(0)` fallback. The old `Logger::warning(...)`
path (AUDIT §M10 note) is deleted; `<core/logger.h>` include in
`node_graph.cpp` removed along with it.

Tests: 4 new in `tests/test_node_graph.cpp`
(`NodeGraph_Factory.CreateConditionalNode`,
`NodeGraph_ExprTree.FromExpressionTreeConditional`,
`RoundTripConditionalExpr`, `RoundTripNestedConditional`). Suite:
1995/1995 passing (1 pre-existing skipped). Unblocks PhysicsTemplates
with ternary saturation curves.

### 2026-04-20 Phase 9C closeout — editor UI layout + theme panel

The 6th (and last) Phase 9C UI/HUD sub-item. New editor panel
`engine/editor/panels/ui_layout_panel.{h,cpp}` registered under
`Window → UI Layout`.

**Two tabs:**

- **Element tree inspector.** Given a `UICanvas*`, shows each root
  element with visibility / interactivity flags + anchor tag, click
  to select; inspector below exposes the selected element's
  `position` / `size` (drag-float), `anchor` (combo), `visible` /
  `interactive` (checkbox). Edits propagate live to the rendered
  canvas.

- **Theme editor.** Full color-picker + size-drag surface over the
  active `UITheme`: backgrounds (bgBase / bgRaised / panelBg /
  panelBgHover / panelBgPressed), strokes/rules, text hierarchy,
  accent+ink, HUD, and component sizes (button / slider / checkbox /
  dropdown / keybind / crosshair / transition duration). "Reset to
  Vellum" / "Reset to Plumbline" buttons for quick-switch between
  the two shipped registers.

Supporting change: `UICanvas::getElementAt(size_t)` added (const +
non-const) so the panel can walk the element list without owning
the canvas.

**Out of scope for this panel (follow-ups):** drag-place widget
palette and JSON canvas serialisation. Both gated on factoring the
editor's ImGui viewport out of `editor.cpp` so the panel can capture
viewport mouse events without fighting the main viewport.

Tests: 5 new (`UILayoutPanel.*` defaults + toggle;
`UICanvasAccessor.*` null-when-empty, out-of-range returns null,
returns added elements in order, const overload). Suite: 1991/1991
passing.

**Phase 9C UI/HUD is now feature-complete across all 6 sub-items.**

### 2026-04-20 Phase 9B Step 12 — ClothComponent cutover to `unique_ptr<IClothSolverBackend>`

The last deferred item from the Phase 9B GPU compute cloth pipeline.
`ClothComponent` now owns its solver polymorphically — either
`ClothSimulator` (CPU XPBD) or `GpuClothSimulator` (GPU compute) —
selected at `initialize()` by `createClothSolverBackend()` per
`GPU_AUTO_SELECT_THRESHOLD` + `GpuClothSimulator::isSupported()`.

**Interface widening.** `IClothSolverBackend` now covers the full
mutator + accessor surface needed by `ClothComponent`,
`inspector_panel.cpp`, and `engine.cpp`:

- Live tuning: `setSubsteps`, `setParticleMass`, `setDamping`,
  `setStretchCompliance`, `setShearCompliance`, `setBendCompliance`.
- Wind: `setWind`, `setDragCoefficient`, `setWindQuality`,
  `getWindVelocity` / `getWindDirection` / `getWindStrength` /
  `getDragCoefficient` / `getWindQuality`.
- Pins / LRA: `pinParticle`, `unpinParticle`, `setPinPosition`,
  `isParticlePinned`, `getPinnedCount`, `captureRestPositions`,
  `rebuildLRA`.
- Diagnostics: `getConstraintCount`, `getConfig`.
- Colliders: `addSphereCollider` / `clearSphereColliders`,
  `addPlaneCollider` / `clearPlaneColliders`, `setGroundPlane` /
  `getGroundPlane`, `addCylinderCollider` / `clearCylinderColliders`,
  `addBoxCollider` / `clearBoxColliders`.

The nested `ClothSimulator::WindQuality` enum was promoted to a
top-level `ClothWindQuality` (declared in `cloth_solver_backend.h`)
so the interface can reference it without dragging in the full CPU
implementation. `ClothSimulator::WindQuality` stays as a
backwards-compat `using` alias.

**Backend coverage:**
- CPU (`ClothSimulator`): implements the full surface — no behaviour
  change; every method now carries `override`.
- GPU (`GpuClothSimulator`): supports sphere/plane/ground colliders
  and the full live-tuning surface. Cylinder/box/mesh colliders are
  CPU-only per the design doc; GPU backend logs a one-time warning
  and drops them so call sites can drive a single code path.
  `captureRestPositions` is a no-op on GPU (rest pose is implicit
  in the CPU position mirror).

**`ClothComponent` changes:**
- `m_simulator` is now `std::unique_ptr<IClothSolverBackend>`.
- `getSimulator()` returns `IClothSolverBackend&` — call sites using
  the old `ClothSimulator&` return type get the polymorphic view
  transparently (every method they called is now on the interface).
- New `setBackendPolicy(AUTO|FORCE_CPU|FORCE_GPU)` and
  `setShaderPath(const std::string&)` setters invoked before
  `initialize()` to override the auto-select or pin GPU for tests.

**Inspector panel** updated to use the new top-level
`ClothWindQuality` enum (was `ClothSimulator::WindQuality`).

Suite: 1986/1986 still passing — the interface widening preserves
every caller's semantics. Phase 9B GPU compute cloth pipeline is
now fully end-to-end.

### 2026-04-20 Phase 9C font swap — Inter Tight / Cormorant Garamond / JetBrains Mono

Asset-side change to back the typography pairing specified in the
`vestige-ui-hud-inworld` Claude Design hand-off.

Three new OFL fonts added under `assets/fonts/`:
- **`inter_tight.ttf`** (variable weight, 568 KB) — UI default,
  rasterises cleaner at small sizes through FreeType than Arimo did.
- **`cormorant_garamond.ttf`** (variable weight, 1.14 MB) — display
  face for the wordmark + modal titles (Vellum register).
- **`jetbrains_mono.ttf`** (variable weight, 183 KB) — mono face
  for captions / micro labels / key-caps / numerics.

`default.ttf` was removed from the engine — its two call sites
(`engine/core/engine.cpp` text renderer init, `engine/editor/editor.cpp`
ImGui font load) now load `inter_tight.ttf` directly. Arimo is
preserved as `assets/fonts/arimo.ttf` for backwards-compatibility
with any external consumer that referenced the old default by path.

`assets/fonts/OFL.txt` rewritten as a consolidated manifest carrying
per-font copyright headers (Arimo, Inter Tight, Cormorant Garamond,
JetBrains Mono) above the single shared OFL 1.1 body. Each font's
Reserved Font Name is called out separately so the OFL clause-3
restriction is unambiguous.

`ASSET_LICENSES.md` and `THIRD_PARTY_NOTICES.md` updated to list all
four fonts with attributions.

**Caveat:** `TextRenderer` is still single-font today — it loads
whichever TTF was passed to `initialize()` and renders everything
through that face. The `UITheme::fontDisplay` / `fontUI` / `fontMono`
logical names are forward-looking metadata. Multi-font support
(routing labels through one face, wordmark through another) is a
separate `TextRenderer` refactor not covered by this commit.

Suite: 1986/1986 still passing (no test depended on the old
`default.ttf` path).

### 2026-04-20 Phase 9C UI batch 4 — menu prefabs (Main / Pause / Settings)

Composes the Phase 9C widget set into the three menu canvases per
the `vestige-ui-hud-inworld` Claude Design layouts.

New module `engine/ui/menu_prefabs.{h,cpp}` with three factory
functions, each taking a fresh `UICanvas` + theme + text renderer
and populating the canvas with positioned widgets:

- `buildMainMenu` — top chrome rule, "VESTIGE" wordmark + 5-item
  button list (New Walkthrough / Continue / Templates / Settings /
  Quit) on the left, continue card on the right, footer with
  keyboard shortcut hints. Quit uses `UIButtonStyle::DANGER`.
- `buildPauseMenu` — tinted scrim, centred 720×760 modal panel with
  4 corner brackets in accent (drawn as 8 thin strips), "PAUSED"
  caption + "The walk is held." headline, 7 buttons (Resume primary,
  Quit-to-Desktop danger, others default), footer line with autosave
  + slot info.
- `buildSettingsMenu` — full-bleed modal (inset 120/80 px), header
  with title + ESC close ghost button, header rule, 300-px-wide
  sidebar with 5 categories (first one accent-highlighted), vertical
  rule separating sidebar from content area, footer with dirty
  indicator + Restore Defaults / Revert / Apply buttons.

**Settings is chrome-only by design.** Per-category controls are
per-game integration — the engine can't know which settings any
given game project exposes. The chrome guarantees every game's
settings menu shares the same framing + footer language.

Builders are safe to call without a `TextRenderer*` (the nullptr
passes through to text elements which skip the draw call). This
lets game projects construct prefabs at startup before the renderer
is wired.

Tests: 6 new (`MenuPrefabs.*` covering element-count bounds for each
prefab, Plumbline-register parity, double-build duplication,
nullptr-safety). Suite: 1986/1986 passing.

**Phase 9C UI/HUD: 5 of 6 done.** Only the editor visual UI layout
editor remains as a separate larger initiative.

### 2026-04-20 Phase 9C UI batch 3 — Claude Design Vellum theme + interactive widget set

Translates the `vestige-ui-hud-inworld` Claude Design hand-off into
native engine widgets. Two visual registers (Vellum primary,
Plumbline alternative) and the full interactive widget family
needed for the menu prefabs.

**`UITheme` widening** — palette now matches the design's Vellum
register: warm bone text on deep walnut-ink, burnished-brass accent.
New fields: `bgBase`, `bgRaised`, `panelStroke`, `panelStrokeStrong`,
`rule`, `ruleStrong`, `accentInk` (text drawn on accent fills).
Component sizing tokens added (`buttonHeight`, `sliderTrackHeight`,
`checkboxSize`, `dropdownHeight`, `keybindKeyMinWidth`, etc.) +
type sizes (display 88, H1 42, body 18, caption 14, etc.) + font
family logical names (`fontDisplay = "Cormorant Garamond"`,
`fontUI = "Inter Tight"`, `fontMono = "JetBrains Mono"` —
asset-side font swap is a follow-up; the engine still renders Arimo
until the OFL fonts ship). `UITheme::plumbline()` static returns the
alternative register with cooler near-black backgrounds and the same
component sizing.

**Five new widgets:**
- `UIButton` — `.btn` family. Variants: `DEFAULT`, `PRIMARY`, `GHOST`,
  `DANGER`. State enum (`NORMAL`/`HOVERED`/`PRESSED`/`DISABLED`)
  drives colour selection. Hover renders a 4 px brass tick on the
  left edge for `DEFAULT`/`DANGER` (matches the design's `.btn::before`).
  Optional `UIButtonShortcut` renders a key-cap on the right edge.
  `small` flag toggles `.btn--sm` height.
- `UISlider` — track + accent fill + 16×16 thumb with 2 px accent
  ring + right-aligned mono value readout. Optional formatter
  callback (defaults to `"N %"`). Optional tick marks across the
  track. `ratio()` accessor exposes the clamped fill fraction.
- `UICheckbox` — 20×20 box; accent-filled with a checkmark drawn in
  `accentInk` when checked, 1.5 px stroked when unchecked. Hover
  brightens the stroke. Inline label drawn 12 px to the right.
- `UIDropdown` — 40 px tall, mono caret indicator, hover/open states
  brighten the border. Open state draws a popup menu with the option
  list (selected option in accent). `currentLabel()` returns the
  display string for the active option.
- `UIKeybindRow` — label / key-cap / CLEAR layout. Listening state
  renders "PRESS KEY..." in accent on accent-bordered key-cap.

Stylistic decisions echo the design verbatim: the accent tick on the
left edge of menu buttons, dropdown caret as an ASCII arrow until
the engine font ships arrow glyphs, key-cap as a bordered mono
fragment, hover-brightened panel-stroke language across all widgets.

**Tests:** 13 new (`UIThemeRegisters.*` covering Vellum warm-bone
text invariant, Plumbline darker-background invariant, accent /
accentInk luma contrast, and shared component sizing across both
registers; `UIButton.*` covering defaults, small-flag height,
without-theme safety; `UISlider.*` for ratio clamping + degenerate
range; `UICheckbox.*` defaults; `UIDropdown.*` `currentLabel()`
out-of-range handling + closed defaults; `UIKeybindRow.*` defaults).
Plus one `UITheme.AccentDimIsDarkerShadeOfAccent` rewrite — old test
asserted dim was translucent (my earlier batch-1 interpretation);
new design uses dim as a darker opaque shade for pressed states, so
the assertion now compares luma instead of alpha. Suite: 1980/1980
passing.

**Still pending in Phase 9C UI/HUD (in-flight):** menu prefab
factories (Main / Pause / Settings) — widgets are in place; next
commit composes them into the three menu canvases per the design's
React layouts. Editor visual UI layout editor remains a separate
larger initiative.

### 2026-04-20 Phase 9C UI batch 2 — in-world UI

Ticks the 4th of 6 remaining Phase 9C UI/HUD sub-items. Two new
elements + one extracted helper.

`ui/ui_world_projection.{h,cpp}` — pure-CPU `projectWorldToScreen()`
helper. Takes a world point + combined view-projection matrix +
viewport size, returns a `WorldToScreenResult` with the top-left-origin
screen pixel coords + NDC depth + a `visible` flag (false when the
point is behind the camera or outside the [-1, 1] NDC clip box).
Extracted as a free function so the projection + frustum-cull logic
is testable without a GL context.

`ui/ui_world_label.{h,cpp}` — `UIWorldLabel`. Anchors to a
`worldPosition`, projects each frame, and draws via
`TextRenderer::renderText2D` at the resulting screen pixel.
`screenOffset` lifts the label above the anchor (e.g. above an
entity's head). The base UIElement's `position` / `anchor` fields
are intentionally ignored — world-space anchoring takes precedence.
Off-screen / behind-camera labels are silently skipped.

`ui/ui_interaction_prompt.{h,cpp}` — `UIInteractionPrompt` extends
`UIWorldLabel`. Two text fields (`keyLabel`, `actionVerb`) compose
into "Press [keyLabel] to actionVerb". Linear distance-based alpha
fade: full opacity at `fadeNear` (default 2.5 m), zero at `fadeFar`
(default 4.0 m). Camera distance is consulted before any projection
work so off-range prompts cost nothing.

Nameplate use case is handled by `UIWorldLabel` directly — game code
calls `nameplate.worldPosition = entity.getWorldPosition() + headOffset`
each frame.

Tests: 11 new (`UIWorldProjection.*` covering behind-camera cull,
centred-when-directly-ahead, off-screen cull, NDC depth bounds,
zero-viewport defensive case; `UIInteractionPrompt.*` covering text
composition, fade-at-bounds, linear midpoint, default interactivity).
Suite: 1967/1967 passing.

**Still pending in Phase 9C UI/HUD (2 of 6):** menu system (best
driven by Claude Design mockups for the visual look first) + editor
visual UI layout editor.

### 2026-04-19 Phase 9C UI batch 1 — theme + input routing + HUD widgets

Ticks 3 of the 6 remaining Phase 9C UI/HUD sub-items.

**UITheme** (`engine/ui/ui_theme.h`) — central style struct consulted by
in-game UI elements. Bg / text / accent palettes, HUD-specific crosshair
+ progress-bar colours, default text scale + crosshair / progress-bar
sizes. `UITheme::defaultTheme()` returns sane neutrals; game projects
override per-field via `UISystem::getTheme()` (mutable ref). Marketing-
facing visual lock-in is best driven by Claude Design mockups before
freezing the final palette.

**Input routing** — `UISystem::setModalCapture(bool)` for sticky modal
capture (pause menu, dialog), `updateMouseHit(cursor, w, h)` for
cursor-over-interactive-element capture. `wantsCaptureInput()` returns
the union — game input handlers consult it each frame and skip
movement / look / fire bindings when true. The pre-existing
`m_wantsCaptureInput` field stays for ABI continuity but the canonical
sources are now the modal flag + the cursor-hit cache.

**HUD widgets** — three `UIElement` subclasses:
- `UICrosshair` — centred plus pattern with configurable arm length,
  thickness, and centre gap. Always renders at viewport centre
  regardless of the base UIElement's anchor / position (matches FPS
  reticle conventions).
- `UIProgressBar` — horizontal bar with `value / maxValue` fill ratio
  (clamped to [0,1]); separate fill / empty colours; skips the fill
  draw call when ratio == 0.
- `UIFpsCounter` — smoothed FPS via exponential moving average (caller
  feeds `tick(dt)` each frame); drawn through `TextRenderer::renderText2D`
  with `"%.0f FPS"` formatting.

**Tests:** 12 new (`UITheme.*`, `UIProgressBar.*`, `UIFpsCounter.*`,
`UICrosshair.*`, `UISystemInput.*`). Suite: 1957/1957 passing.

**Still pending in Phase 9C UI/HUD:** in-world UI (floating text,
interaction prompts), menu system (main menu / pause / settings —
Claude Design candidate for visual mockups), editor visual UI layout
editor (multi-week initiative).

### 2026-04-19 Phase 9B GPU compute cloth pipeline — feature complete

Bundles Steps 7–11 of the Phase 9B GPU cloth migration (Steps 1–6
shipped earlier today). The XPBD cloth solver is now fully
implemented on the GPU as a parallel alternative to the existing
CPU `ClothSimulator`.

**Step 7 — collision (sphere + plane + ground).** New compute shader
`assets/shaders/cloth_collision.comp.glsl`. Per-particle thread loops
over sphere + plane collider arrays (passed as a UBO at
binding 3, std140 layout, capped at 32 spheres + 16 planes). Pushes
particles to `surface + collisionMargin` and zeros inward velocity.
New mutators: `addSphereCollider`, `clearSphereColliders`,
`addPlaneCollider`, `clearPlaneColliders`, `setGroundPlane`,
`setCollisionMargin`. UBO uploaded lazily when collider state changes.
Cylinder + box + mesh colliders deferred per the design doc.

**Step 8 — normals.** New compute shader
`assets/shaders/cloth_normals.comp.glsl`. Per-particle thread walks
the (up to) 6 grid-adjacent triangles, accumulates area-weighted
face normals, normalises. Atomic-free — each particle is the sole
writer of its own normal slot. Runs once per frame (not per substep)
since normals are for rendering, not physics. Render path still goes
through `ClothComponent`'s vertex buffer for now; the SSBO-direct
render path is bundled with the deferred `ClothComponent` cutover.

**Step 9 — pins + LRA tethers.** New compute shader
`assets/shaders/cloth_lra.comp.glsl` — unilateral tethers that
activate only when a free particle has drifted past its rest-pose
distance from its nearest pin. No graph colouring needed (each
thread writes only its own particle). New `GpuLraConstraint` type
+ `generateLraConstraints()` helper. Pin support on
`GpuClothSimulator`: `pinParticle` / `unpinParticle` /
`setPinPosition` / `isParticlePinned` / `getPinnedCount`,
`rebuildLRA()`. CPU position mirror's `w` channel is the source of
truth for pin state; positions SSBO is re-uploaded when pins change.

**Step 10 — auto CPU↔GPU select factory.** New module
`engine/physics/cloth_backend_factory.{h,cpp}` with
`chooseClothBackend()` (pure CPU, testable) and
`createClothSolverBackend()` (constructs the chosen backend). Three
policies: `AUTO`, `FORCE_CPU`, `FORCE_GPU`. Threshold:
`GPU_AUTO_SELECT_THRESHOLD = 1024` particles (≈ 32×32 grid). The
`ClothComponent` swap to `unique_ptr<IClothSolverBackend>` is
intentionally a follow-up commit — the factory is in place and
unit-tested so the cutover is a one-line change at the call site
plus broadening the `IClothSolverBackend` interface to cover the
mutator surface used by `inspector_panel`.

**Step 11 — sweep.**
- `tools/audit/audit_config.yaml` gains a new `shader.ssbo_vec3_array`
  rule that flags `vec3 \w+\[\]` in `*.comp.glsl` files. std430's
  array stride for `vec3` is 16 B on Mesa AMD (and is implementation-
  defined elsewhere); the GPU cloth pipeline uses `vec4` everywhere
  with `w` as padding / inverse mass. The audit rule guards against a
  follow-up commit silently reintroducing the pitfall.
- `ROADMAP.md` Phase 9B "GPU Compute Cloth Pipeline" item ticked,
  with deferred follow-ups documented inline (GPU self-collision,
  GPU mesh-collider, GPU tearing, `ClothComponent` cutover, Vulkan
  port, perf-acceptance gate).

**Test coverage delta across Steps 7–11**: 13 new tests
(`GpuClothSimulator.*` collider defaults / accept / reject / clear /
binding pin; `GpuClothSimulator.*` pin defaults / LRA binding;
`ClothConstraintGraph.*` LRA empty / tether-every-free-particle;
`ClothBackendFactory.*` AUTO / FORCE_CPU / FORCE_GPU / no-context
fallback / threshold pin / CPU-create-and-init). Suite: **1945/1945**
passing across the full engine (up from 1899 at Step 1 entry).

**What's still gated:** the GPU backend is implemented and
unit-tested but is not yet wired into `ClothComponent::m_simulator`.
The factory exists; the call-site swap and the broader
`IClothSolverBackend` interface widening are a follow-up because
they touch every `getSimulator()` caller (especially
`inspector_panel.cpp`). When that lands, the
`docs/PHASE9B_GPU_CLOTH_DESIGN.md` § 7 perf-acceptance gates
(100×100 ≥ 120 FPS on RX 6600, etc.) become the merge criteria.

### 2026-04-19 Phase 9B Step 6: cloth_dihedral.comp.glsl + dihedral constraints

Per-quad-pair angle-based bending lands on the GPU. Different math
from the skip-one *distance* bend (Step 5): a dihedral constraint
binds two adjacent triangles via their shared edge and constrains
the angle between their face normals to a rest angle (Müller et al.
2007 — same formulation the CPU `ClothSimulator::solveDihedralConstraint`
uses, so behaviour matches).

New compute shader `assets/shaders/cloth_dihedral.comp.glsl` —
`local_size_x = 32` (smaller workgroup than the distance shader to
match the larger per-thread register footprint of 4-particle
gradient computation). Reads the dihedral SSBO, computes face
normals for both triangles, the current/rest angle delta, the four
gradient vectors, and writes corrections to all four particles.

New types in `cloth_constraint_graph`:
- `GpuDihedralConstraint` — `uvec4 p` (wing0, wing1, edge0, edge1)
  + `vec4 params` (restAngle, compliance, padding) → 32 B std430.
- `generateDihedralConstraints()` — walks the triangle index buffer,
  hashes each edge `(min(v0,v1), max(v0,v1))` into an
  `unordered_map`, and emits one constraint per edge shared by
  exactly two triangles. Boundary and non-manifold edges are skipped.
- `colourDihedralConstraints()` — same greedy 64-bit-bitset algorithm
  as the distance variant, generalised to 4 endpoints. Within a
  colour no two dihedrals touch any of the same four particles.

`GpuClothSimulator` upgrades:
- New SSBO `BIND_DIHEDRALS = 5` (32 B per constraint).
- New `m_dihedralShader`, loaded alongside the others when shader
  path is set.
- `simulate()` substep loop gains a step 4 (dihedral solve) right
  after the distance solve; same per-colour structure but smaller
  workgroup count (32 vs 64).
- New accessors `getDihedralCount()`, `getDihedralColourCount()`,
  `getDihedralsSSBO()`.
- `destroyBuffers()` and `reset()` clean up the dihedral state.

For a 4×4 grid: 21 dihedral constraints (formula `3MN − M − N` for
M=N=3). For a flat grid every rest angle is 0 — the cloth's neutral
pose is "lay flat", and the constraint pushes back proportional to
how far folded the cloth deviates from that pose.

Tests: 6 new dihedral tests in `test_cloth_constraint_graph.cpp`
(analytical count formula, flat-grid rest angle = 0, single-triangle
yields no dihedrals, the load-bearing "no shared particle within
colour" invariant on a 6×6, partition contract, struct-size pin).
2 new `GpuClothSimulator.*` tests (binding-enum pin, default-state
zero accessors). Suite: 1927/1927 passing.

### 2026-04-19 Phase 9B Step 5: bend constraints (skip-one distance edges)

`generateGridConstraints()` extended with a `bendCompliance`
parameter and now also emits skip-one stretch edges along X and Z
(`(x,z)–(x+2,z)` and `(x,z)–(x,z+2)`). Bend edges share the same
XPBD distance-constraint shader as stretch/shear — only the rest
length and compliance differ — so they slot transparently into the
existing colour partitioning + multi-pass dispatch loop.

`GpuClothSimulator::buildAndUploadConstraints()` now passes
`config.bendCompliance` through. Cloth resists folding rather than
just pulling apart along the grid lines.

Per-interior-particle degree of the constraint graph rises from 8
(stretch+shear) to 12 (+ skip-one in 4 cardinal directions), so
greedy colouring's worst case rises from Δ+1=9 to 13. The
`ColouringIsConservativeForRegularGrid` cap was loosened from 12
to 16 colours to match (still flags any real algorithmic
regression).

Tests: 2 new bend-focused tests (`BendConstraintsHaveSkipOneRestLength`
verifying rest = 2·spacing, `NoBendConstraintsForGridSmallerThanThree`
guarding the 2×N edge case). Existing test counts updated.
Suite: 1919/1919 passing.

### 2026-04-19 Phase 9B Step 4: cloth_constraints + greedy graph colouring

XPBD distance-constraint solver lands on the GPU.

New compute shader `assets/shaders/cloth_constraints.comp.glsl` —
one thread per constraint within a colour group, computes the XPBD
position correction `Δp = -C / (w0 + w1 + α̃) · n` (with
`α̃ = compliance / dt²`), and writes both endpoints back to the
positions SSBO. Within a colour no two constraints share a particle,
so writes are race-free without atomics. Pinned-on-both-ends and
zero-length constraints are short-circuited.

New module `engine/physics/cloth_constraint_graph.{h,cpp}` —
pure-CPU helpers used at `initialize()` time:
- `generateGridConstraints()` builds stretch (W·H structural edges)
  and shear (down-right + down-left diagonals) constraints, mirroring
  the topology of the CPU `ClothSimulator`.
- `colourConstraints()` runs greedy graph colouring over those
  constraints, reorders them in place by colour, and returns
  per-colour `[offset, count]` slices. A 64-bit per-particle bitset
  tracks "colours seen so far"; the lowest unused bit becomes the
  constraint's colour. For a regular grid this lands at ~5 colours
  (well under the Δ+1 = 7 worst case).

`GpuClothSimulator` upgrades:
- New SSBO `BIND_CONSTRAINTS = 4` holds `GpuConstraint[]`
  (i0, i1, restLength, compliance — 16 B each, std430 friendly).
- `simulate()` now runs an XPBD substep loop (default 10 substeps,
  matches the CPU path). Each substep: wind dispatch → barrier →
  integrate → barrier → for each colour { constraint dispatch →
  barrier }. Damping is split across substeps so visual behaviour
  is comparable as substep count varies.
- `setSubsteps()` accessor (clamps to ≥ 1). `getConstraintCount()` /
  `getColourCount()` accessors for telemetry + tests.

The cutover from `ClothSimulator` to `GpuClothSimulator` inside
`ClothComponent` is still gated behind Step 10; until then this
backend is exercised by tests + manual instantiation only.

Tests: 8 new `ClothConstraintGraph.*` tests (counts, rest lengths,
edge cases, the load-bearing "no shared particle within colour"
invariant on an 8×8 grid, conservative colour-count sanity check on
16×16, and the offset/count partition contract); 3 new
`GpuClothSimulator.*` tests (constraint count is zero pre-init,
substep clamping, binding-enum pinning). Suite: 1917/1917 passing.

### 2026-04-19 Phase 9B Step 3: cloth_wind + cloth_integrate compute shaders

First real GPU work. Two compute shaders land:
- `assets/shaders/cloth_wind.comp.glsl` — applies gravity + uniform
  wind-drag force to per-particle velocities. `local_size_x = 64` to
  fit AMD wavefronts. Per-particle noise / per-triangle drag (the CPU
  path's FULL wind tier) is intentionally deferred.
- `assets/shaders/cloth_integrate.comp.glsl` — symplectic Euler with
  velocity damping. Snapshots `prev` then advances `pos += vel · dt`.
  Pinned particles (positions[i].w == 0) are skipped — the inverse-mass
  channel is reserved for Step 9 LRA / pin work; Step 3 leaves every
  particle's w at 1 (free).

`GpuClothSimulator::simulate()` now dispatches: bind velocities → wind
shader → `glMemoryBarrier` → bind positions/prev/velocities → integrate
shader → `glMemoryBarrier` → mark CPU mirror dirty. Free-fall cloth
visibly drops under gravity in the editor.

Loading: `setShaderPath()` must be called pre-`initialize()`. Without
a shader path the SSBOs still allocate but `simulate()` is a no-op
(CPU mirror returns the rest pose), so any caller that forgets to wire
up the shader directory degrades gracefully rather than crashing.

CPU readback: `getPositions()` / `getNormals()` are now lazy — each
calls `glGetNamedBufferSubData` and stages vec4→vec3 only when the
mirror is dirty. The dirty flag is set by `simulate()` and cleared by
the next reader. Per-frame readback while the renderer still uploads
through `ClothComponent`'s vertex buffer; Step 8 will switch the
renderer to read SSBOs directly and skip readback entirely on the hot
path.

`reset()` re-uploads the rest-pose grid into positions/prev and zeros
velocities. Mirror is left clean (it was never moved by simulate; only
mutated by readback).

`Shader::setUInt()` added (just `glUniform1ui`) — used by the cloth
shaders' `uniform uint u_particleCount`. Reusable for future GLSL
unsigned uniforms.

Tests: 2 new unit tests (`HasShadersDefaultsFalse`,
`ParameterSettersCompileAndAccept`); 1906/1906 passing. GPU dispatch
correctness is a visual-launch verification item per the
`tests/test_gpu_particle_system.cpp` precedent.

### 2026-04-19 Phase 9B Step 2: GpuClothSimulator skeleton

New backend `engine/physics/gpu_cloth_simulator.{h,cpp}` — the GPU
half of the IClothSolverBackend dual. Step 2 scope is buffer
plumbing only: SSBO allocation in `initialize()`, teardown in the
destructor, no-op `simulate()`, CPU mirror returned by `getPositions()`
/ `getNormals()`. The compute-shader dispatches land incrementally
in Steps 3–9 per the design doc.

Five SSBOs are allocated up-front using DSA (`glCreateBuffers` /
`glNamedBufferStorage`): positions, prev positions, velocities,
normals, indices. All particle buffers use `vec4` layout (xyz + w
padding / future inverse-mass channel) to dodge std430's vec3-array
padding pitfall — same workaround the GPU particle pipeline already
uses on Mesa AMD. Binding indices are pinned via a
`BufferBinding` enum (0/1/2/6/7) that pairs with the cloth_*.comp.glsl
contract from the design doc.

`isSupported()` is a no-context-safe probe: returns false if no GL
context is current, otherwise checks for GL ≥ 4.5 (DSA + compute +
SSBO). Callers can call it before `initialize()` to decide whether
to construct the GPU backend at all.

Tests: 5 new unit tests in `tests/test_gpu_cloth_simulator.cpp`
covering default state, polymorphic construction via
`unique_ptr<IClothSolverBackend>`, the no-context probe path,
SSBO-handle-zero-pre-init invariants, and a guard against
accidental SSBO-binding-index reordering. Suite: 1904/1904 passing
(no regressions; 6 cloth-backend tests now alongside the 80
existing cloth tests).

### 2026-04-19 Phase 9B Step 1: IClothSolverBackend interface

New header `engine/physics/cloth_solver_backend.h` declaring
`IClothSolverBackend` — the per-frame simulation contract that
both `ClothSimulator` (CPU XPBD) and the upcoming
`GpuClothSimulator` (Phase 9B GPU compute) will satisfy.

Scope is intentionally lean: only the lifecycle + readback methods
are virtual (`initialize`, `simulate`, `reset`, `isInitialized`,
`getParticleCount`, `getPositions`, `getNormals`, `getIndices`,
`getTexCoords`, `getGridWidth/Height`). Configuration mutators
(`setWind`, `addSphereCollider`, `pinParticle`, etc.) remain on
the concrete `ClothSimulator` type during the transitional phase
and will widen as the GPU backend matures — see
`docs/PHASE9B_GPU_CLOTH_DESIGN.md` § 4.

`ClothSimulator` now inherits from `IClothSolverBackend` and
marks the 11 methods `override`. No behavioural change.
`ClothComponent` keeps its concrete embedding (`ClothSimulator
m_simulator`) for now; the cutover to `unique_ptr<IClothSolverBackend>`
lands in a later step once `GpuClothSimulator` exists.

Tests: 4 new unit tests in `tests/test_cloth_solver_backend.cpp`
covering polymorphic construction, initialize-through-interface,
simulate-and-reset round-trip, and virtual-destructor safety.
Suite: 1899/1899 passing (no cloth regressions across 80 existing
cloth tests).

### 2026-04-19 Phase 9C: Navigation editor — visualisation + bake controls

New `NavigationPanel` editor panel
(`engine/editor/panels/navigation_panel.{h,cpp}`) drives the
Navigation domain system from the editor: exposes Recast build
parameters as ImGui `DragFloat`/`DragInt` widgets, fires
`NavigationSystem::bakeNavMesh()` on a button press, reports
last-bake polygon count and wall-clock time, and provides a
"Show polygon overlay" toggle that draws every navmesh polygon's
edges via the engine's `DebugDraw` line renderer (configurable
colour + Y-lift to avoid z-fighting).

Wiring:
- `Editor::setNavigationSystem()` accepts the live system pointer
  during engine init (mirrors `setFoliageManager` / `setTerrain` /
  `setProfiler`).
- `NavMeshBuilder::extractPolygonEdges()` walks Detour tiles via
  the public const `getTile()` overload, skipping
  `DT_POLYTYPE_OFFMESH_CONNECTION` polys, appending segment
  endpoints to a caller-supplied buffer.
- `engine.cpp` calls the extractor + `DebugDraw::line` in the
  existing per-frame debug-draw pass when the panel toggle is on.
- `Window` menu gets a new `Navigation` toggle next to `Terrain`.

Tests: 6 new unit tests in `tests/test_navigation_panel.cpp`
covering panel defaults, toggle behaviour, overlay parameter
sanity, and the `extractPolygonEdges()` empty-mesh + append
contracts. Suite: 1895/1895 passing.

Closes the **Editor: navmesh visualization and bake controls**
item under Phase 9C → AI & Navigation in `ROADMAP.md`. Patrol
path placement remains deferred to Phase 16 (AI behaviour trees).

### 2026-04-19 Phase 9B GPU compute cloth — design doc

New design document `docs/PHASE9B_GPU_CLOTH_DESIGN.md` for the
last-remaining Phase 9B sub-item: migrating the XPBD cloth solver
to a GPU compute pipeline (SSBO storage + 4 compute shaders +
red-black graph colouring + auto CPU↔GPU select). Implementation
gated on maintainer review per CLAUDE.md research-first rule;
covers algorithm, file layout, buffer layout, workgroup sizing,
testing strategy, perf acceptance criteria, risks, and explicit
out-of-scope items.

### 2026-04-19 tooling: CMake compatibility CI matrix

`.github/workflows/ci.yml` gains a separate `cmake-compat` job
exercising the engine's declared minimum (`3.20.6`) and the
latest upstream CMake on every push/PR via
`jwlawson/actions-setup-cmake@v2`. Release-only, build-and-test
(no audit), kept separate from `linux-build-test` so main-CI cost
is unchanged. Catches FetchContent / SOURCE_SUBDIR regressions
before downstream users report them. Closes the
`PRE_OPEN_SOURCE_AUDIT.md` §8 follow-up.

### 2026-04-19 tooling: pretool frugal-output Bash hook

Adds a `PreToolUse` hook (`tools/hook-pretool-bash-frugal.sh`) that
bounces known-noisy commands (`pytest` without `-q`, `cmake --build`
without a tail/redirect, `ctest -V`, `tools/audit/audit.py`) with a
one-line reminder pointing at `| tail -200` / `--quiet` / `> /tmp/
<name>.log`. Bypassed via a trailing `# frugal:ok` marker. Saves
~5–20 k context tokens per accidental verbose run.

`.claude/settings.json` — three read-only allowlist additions
(`gitleaks detect *`, `semgrep --config *`,
`clang-include-cleaner --disable-insert *`). Most observed traffic
was already covered by Claude Code's built-in allowlist or existing
cmake/ctest/cppcheck/clang-tidy entries.

### 2026-04-19 audit tool 2.14.0 — three detectors close out the 30-idea list

Ships the final three queued detectors from the 2026-04-19
"30 consolidated detector ideas" list. The list is now fully shipped.

- **`per_frame_heap_alloc`** (tier 4, MEDIUM in-loop / LOW otherwise)
  — idea **#18**. Flags heap allocations inside per-frame functions
  (`render` / `draw` / `update` / `tick`). Brace-balanced loop
  tracking; honours `// ALLOC-OK` reviewer markers and skips
  `static const` one-shot initialisers.
- **`dead_public_api`** (tier 4, LOW) — idea **#25**. Flags public
  class / free-function declarations with zero external callers via
  word-bounded full-corpus grep.
- **`token_shingle_similarity`** (tier 4, LOW) — idea **#28**. Jaccard
  similarity over hashed K-token windows; complements line-aligned
  `tier4_duplication` by catching reflowed near-duplicates.

Also in this commit:
- `lib/config.py` `DEFAULTS` dict split into per-section module-level
  blocks (`_DEFAULTS_PROJECT` / `_BUILD` / `_TIER4` / …) assembled
  at the bottom — adding a future detector default is now a
  localised edit.
- `lib/config.py` `Config.enabled_tiers` fallback fixed: was
  `[1..5]`, now matches `DEFAULTS["tiers"] = [1..6]`.

45 new unit tests; full audit suite now at 850 passing. Smoke run
against the engine: 63 per-frame allocs / 238 functions, 4 / 2398
dead public APIs, 5 similar pairs / 597 files — all real signal, no
FP flood.

### 2026-04-19 docs: sync ROADMAP / PHASE9E3_DESIGN / ARCHITECTURE §19

Pure documentation-sync pass (no code, no tests). Phases 9A / 9C /
9D had been shipping code without corresponding checkbox /
annotation updates in `ROADMAP.md`; Phase 9E-3's acceptance-criteria
checklist hadn't reflected what actually landed in commits `cffd755`
/ `e0c56c2`.

- **ROADMAP.md** — Phase 9A marked COMPLETE (10 sub-bullets ticked
  with file refs and noted renames); Phase 9C marked FOUNDATIONS
  SHIPPED (3 items ticked, 15 annotated "deferred to Phase 10" or
  "not yet"); Phase 9D marked COMPLETE (all 4 sub-sections ticked,
  game-template enum confirmed covering all 6 variants).
- **docs/PHASE9E3_DESIGN.md** §13 — 5 acceptance-criteria items
  ticked with commit refs (library integration, M9 / M10 / M11,
  L6); progress header added noting Steps 1–3 shipped, Step 4 WIP,
  12 remaining.
- **ARCHITECTURE.md §19** — new "Editor integration (Phase 9E-3)"
  subsection describing the `NodeEditorWidget` / `ScriptEditorPanel`
  split, current Step 4 scope, `CommandHistory` integration plan,
  and hot-reload contract.

### 2026-04-19 L41 follow-up: `-Werror` lock-in

Enables `-Werror` on the `vestige_engine` target now that the 2026-04-19
L41 sweep drove it to zero warnings under the full
`-Wformat=2 / -Wconversion / -Wsign-conversion / -Wshadow /
-Wnull-dereference / -Wdouble-promotion / -Wimplicit-fallthrough` set.
Future regressions now fail the build instead of silently accumulating.

Build clean, 1889/1889 tests pass. If a warning ever needs a justified
suppression in the future, the policy is a narrowly-scoped `#pragma`
at the call site — never a global flag removal.

### 2026-04-19 GI roadmap sync + SH-probe-grid unit tests

Reconciles `docs/GI_ROADMAP.md` with the actual engine state — SH
probe grid (2026-03-29) and radiosity baker (2026-03-30) landed
months ago but were still marked "Planned" in the roadmap. Next GI
step is now **SSGI** (Screen-Space Global Illumination), promoted
from MEDIUM to HIGH priority.

- `docs/GI_ROADMAP.md` — items 2 (SH grid) and 3 (radiosity) marked
  IMPLEMENTED with file pointers and dates; implementation-order
  list struck out the shipped items; item 4 (SSGI) flagged as the
  next priority.
- `tests/test_sh_probe_grid.cpp` — new unit-test file (6 tests, 1889
  total). Covers the pure-math statics that had no coverage:
  `projectCubemapToSH` (uniform-colour/zero/clamped-HDR cases) and
  `convolveRadianceToIrradiance` (Ramamoorthi-Hanrahan 2001 cosine
  coefficients, and the combined pipeline). GPU upload/bind paths
  remain covered by the live scene-renderer capture path (need a GL
  context).

### 2026-04-19 post-launch: gitleaks CI + pre-commit, Dependabot

Closes two of the four post-launch maintenance items in
`docs/PRE_OPEN_SOURCE_AUDIT.md`:

- `.github/workflows/ci.yml` — new `secret-scan` job runs
  `gitleaks/gitleaks-action@v2` against the full git history on every
  push and PR. Honours the committed `.gitleaks.toml` allowlist
  (rotated-and-scrubbed NVD key, documented in SECURITY.md).
- `.pre-commit-config.yaml` — added `gitleaks@v8.30.1` hook so
  contributors' `pre-commit install` catches staged secrets before
  they ever reach a remote.
- `.github/dependabot.yml` — new, weekly cadence on
  `github-actions` and `pip` (audit tool) ecosystems, max 5 open PRs
  per ecosystem, Monday 06:00 UTC. Tracks CI action CVEs without
  depending on a human to remember to bump.

Local sweep verified clean: 255 commits, ~11.75 MB, 0 leaks.

### 2026-04-19 audit tool 2.13.0 — three detectors + copyright-header backfill

Ships three new tier-4 detectors that were deferred from audit 2.12.0
(they need multi-line windows or cross-file grep logic). All three
produce **zero findings** against the current Vestige tree after a
small copyright-header backfill in this same commit. 801+ audit-tool
unit tests pass (+35 new, +4 extra during FP tightening).

- **`file_read_no_gcount`** (tier 4, medium) — flags `stream.read(buf,
  N)` calls with no `.gcount() / .good() / .fail() / .eof()` check in
  the next N-line window. Also excludes `.read(` tokens inside
  double-quoted string literals so embedded Python snippets (e.g.
  `sys.stdin.read()` in `tests/test_async_driver.cpp`) don't false
  fire.
- **`dead_shader`** (tier 4, low) — flags `.glsl` files whose basename
  (or stem) does not appear as a substring anywhere in the source
  corpus. Substring-not-regex is deliberate to avoid the 2026-04-19
  `ssr.frag.glsl` FP caused by runtime-constructed shader paths.
- **`missing_copyright_header`** (tier 4, low) — per-file check that
  the first 3 lines (shebang-adjusted) contain a `Copyright (c) YEAR
  NAME` line and an `SPDX-License-Identifier` line. Covers `//`, `#`,
  `--` comment tokens.

Copyright backfill for the five files the new detector caught:

- `app/CMakeLists.txt`, `engine/CMakeLists.txt`, `tests/CMakeLists.txt`
- `engine/utils/json_size_cap.h`, `engine/utils/json_size_cap.cpp`

### 2026-04-19 manual audit — L41 clean-warning-flag sweep + include-cleaner pass

Closes the last deferred item from the 2026-04-19 audit backlog (L41,
``-Wformat=2 -Wshadow -Wnull-dereference -Wconversion -Wsign-conversion``)
and does an engine-wide unused-include pass. All 1883 tests pass; engine
and full build compile with **zero warnings, zero errors** under the
hardened warning set.

#### L41 — warning-flag sweep

The flags were already in ``engine/CMakeLists.txt`` (added by a prior
audit commit) but had been producing 633 warnings — mostly third-party
cascade from vendored GLM headers and ``-Wmissing-field-initializers``
noise in the visual-scripting node tables. Root cause fixed in the
three highest-leverage places, then the remaining 148 in-project
``-Wsign-conversion`` warnings cleaned file-by-file.

- ``engine/CMakeLists.txt`` — promoted ``glm-header-only`` to SYSTEM
  via ``set_target_properties(glm-header-only PROPERTIES SYSTEM TRUE)``
  (CMake 3.25+). Earlier ``target_include_directories(vestige_engine
  SYSTEM ...)`` was no-op because the original ``-I`` from
  ``glm-header-only``'s ``INTERFACE_INCLUDE_DIRECTORIES`` still preceded
  any re-export. Removed 346 cascaded GLM warnings.
- ``engine/scripting/node_type_registry.h`` — added ``= {}``
  default-member-initializers to ``NodeTypeDescriptor::inputIndexByName``
  and ``outputIndexByName``. Removed 136 ``-Wmissing-field-initializers``
  warnings from the six ``*_nodes.cpp`` aggregate registration sites
  without touching every call site.
- ``engine/physics/ragdoll.cpp:491`` — default-initialized
  ``glm::vec3/vec4/quat`` out-params before ``glm::decompose`` so the
  compiler can prove definite-assignment even though the function always
  overwrites; silences ``-Wmaybe-uninitialized``.
- ``engine/editor/panels/texture_viewer_panel.cpp:608`` — ``PbrRole
  role`` initialised to ``ALBEDO`` before a non-pass-through
  out-parameter helper that may leave it untouched when no suffix
  matches.
- ``engine/audio/audio_engine.h`` — ``std::vector<bool>
  m_sourceInUse`` → ``std::vector<uint8_t>`` to sidestep the
  specialised-bitvector proxy-reference weirdness and the GCC 15
  libstdc++ ``-Warray-bounds`` false positive in ``stl_bvector.h``
  ``resize()``. Corresponding ``assign(MAX_SOURCES, 0u)`` in the cpp.
- 26 source files touched for 148 ``-Wsign-conversion`` fixes —
  ``size_t`` loop aliases in hot paths (``particle_emitter.cpp``, etc.)
  and ``static_cast<size_t>/GLuint/GLsizeiptr/GLenum`` at call sites
  elsewhere. Hot loops (particle kill/update/spawn) use a local
  ``const size_t u = static_cast<size_t>(i);`` alias to keep indexing
  readable.

#### Include-cleaner pass

Ran ``clang-include-cleaner --disable-insert`` across all 224
``engine/*.cpp`` files. 82 unused includes removed from 78 files; 14
flagged removals were reverted as false positives (``<glm/gtc/matrix_transform.hpp>``
providing unqualified ``glm::lookAt/perspective/translate`` calls not
visible to the checker, ``<ft2build.h>`` needed for the
``FT_FREETYPE_H`` macro in ``font.cpp``). Report preserved at
``.unused_includes_report.txt`` for reproducibility.

#### clang-side fix

- ``editor/panels/model_viewer_panel.cpp:670-671`` — explicit
  ``static_cast<float>`` on the ``(int) / 10.0f`` truncate-to-one-decimal
  expression. Silences clang ``-Wimplicit-int-float-conversion`` while
  keeping the intentional truncation semantics.

### 2026-04-19 manual audit — low-item close-out + research update

Finishes the remaining Low-severity items from the 2026-04-19 audit
backlog and folds in a GDC 2026 / SIGGRAPH 2025 shader-research survey.
All 1883 tests pass.

#### Dead code / small correctness (L11, L13, L23, L24, L25, L30, L31, L33, L35, L39, L40)

- `FileWatcher::m_onChanged` (and its dispatch branch in ``rescan()``)
  deleted — the setter had already gone in L5 so every `if (m_onChanged)`
  site was unreachable.
- ``engine.cpp:1471,1478`` — Jolt ``BoxShape* new`` results marked
  ``const`` (cppcheck ``constVariablePointer``).
- ``editor.cpp`` — ``tonemapNames`` / ``aaNames`` moved into their
  branch scope (dead outside ``BeginMenu``). Three ``ImGuiIO&`` locals
  that never mutate the returned reference converted to
  ``const ImGuiIO&``.
- ``command_history.cpp`` — dirty-tracking arithmetic was not
  closed-form correct: undo-then-execute-new could land
  ``m_version == m_savedVersion`` even though the saved state lived on
  the discarded redo branch. Added explicit ``m_savedVersionLost``
  update on redo-branch discard and tightened the trim off-by-one. Two
  new regression tests (``DirtyAfterUndoThenNewExecute``,
  ``DirtyAfterDeepUndoThenNewExecute``) cover both paths.
- ``memory_tracker.cpp::recordFree`` — added compare-exchange loop that
  clamps at zero instead of letting a double-free or
  free-without-alloc wrap both atomics to ``SIZE_MAX``. Two regression
  tests added.
- ``pure_nodes.cpp::MathDiv`` — div-by-zero warning was firing every
  frame when a node graph fed a persistent zero. Rate-limited to the
  first occurrence per ``nodeId`` via a mutex-guarded
  ``std::unordered_set``.
- ``markdown_render.cpp`` — dead ``if (cells.empty())`` branch removed;
  ``splitTableRow`` always returns at least one cell.
- ``workbench.cpp:2189`` — inner ``VariableMap vars`` that shadowed the
  outer loop variable was rebuilt every iteration of a 100-sample
  loop. Reuse the main curve's map.
- ``workbench.cpp:249`` — ``static char csvPath[256]`` bumped to
  ``[4096]`` (PATH_MAX). 256 silently truncated deeply-nested paths.

#### DRY refactors (L12, L13, L14)

- New ``engine/renderer/ibl_prefilter.h`` — extracted the mip×face
  prefilter loop shared by ``EnvironmentMap`` and ``LightProbe`` into
  ``runIblPrefilterLoop()``. ~35 lines of identical code collapsed to
  a single call site in each class.
- New ``engine/utils/deterministic_lcg_rng.h`` — ``DeterministicLcgRng``
  class replaces the byte-for-byte duplicated LCG
  (``state * 1664525 + 1013904223``) in ``ClothSimulator`` and
  ``EnvironmentForces``. Preserves the exact output sequence for both
  callers.
- New ``engine/renderer/scoped_forward_z.h`` — RAII helper that saves
  the current clip/depth state, switches to forward-Z
  (``GL_NEGATIVE_ONE_TO_ONE`` + ``GL_LESS`` + ``clearDepth(1.0)``),
  and restores on destruction. Replaces four manual save/switch/restore
  triples in ``renderer.cpp`` (light-probe capture, SH-grid capture,
  directional CSM shadow pass, point-shadow pass) so a thrown
  exception or early-return never leaves the reverse-Z pipeline in a
  corrupt state.

#### Shader hardening (L15-L20)

- Added local ``safeNormalize(v, fallback)`` to ``scene.vert.glsl`` and
  ``scene.frag.glsl`` (``vec3 = dot-and-inversesqrt`` guarded by a
  ``1e-12`` length-squared floor). Applied to the TBN basis
  (``scene.vert.glsl:161-163``), shadow-bias ``lightDir`` in point
  shadow sampling (``scene.frag.glsl:448``), and camera view direction
  (``scene.frag.glsl:989``). Per Rule 11 the epsilon carries a ``TODO:
  revisit via Formula Workbench once reference data is available``
  comment.
- Added ``safeClipDivide(clip)`` to ``motion_vectors_object.frag.glsl``
  so a vertex on the camera plane (``w ≈ 0``) can't produce NaN motion
  vectors that later leak through TAA bilinear sampling.
- ``scene.vert.glsl`` morph-target loop now iterates
  ``min(u_morphTargetCount, MAX_MORPH_TARGETS)`` so a stale/corrupt
  uniform can never index past the 8-element ``u_morphWeights`` array.
- ``particle_simulate.comp.glsl`` gradient / curve loops likewise
  capped at compile-time ``MAX_COLOR_STOPS - 1`` / ``MAX_CURVE_KEYS -
  1``.

#### Roadmap update — GDC 2026 / SIGGRAPH 2025 research survey

Added ``ROADMAP.md`` § "2026-04 Research Update" under Phase 13 listing
newly identified techniques (spatiotemporal blue noise, SSILVB,
two-level BVH compute RT, hybrid SSR → RT fallback, physical camera
post stack) and priority hints for existing roadmap items (volumetric
froxel fog, FSR 2.x, sparse virtual shadow maps, GPU-driven MDI,
radiance cascades). Cites primary sources for each.

#### Phase 24 — Structural / Architectural Physics (design doc)

Draft design document for the attachment-physics phase:
``docs/PHASE24_STRUCTURAL_PHYSICS_DESIGN.md``. Cross-referenced from
``ROADMAP.md``. Covers:

- XPBD cloth particle ↔ Jolt rigid body kinematic attachment (pattern
  used by Chaos Cloth, Obi Cloth, PhysX FleX).
- Tagged-union tether constraint (particle / rigid body / static
  anchor endpoints) with one-sided distance-max XPBD projection.
- Slider-ring authoring on top of the existing
  ``Jolt::SliderConstraint`` wrapper.
- Editor attachment panel + vertex-picker gizmo.
- Full Tabernacle structural rigging spec: 48 boards + 5 bars/side +
  21 curtains + 2 coverings + veil + screen + 60 outer pillars +
  linen walls + tent-pegs-and-cords.
- Formula Workbench entries for every new tuning coefficient.

Rationale added to ``ROADMAP.md``: Phase 24 must land alongside the
rendering-realism pass, because photoreal curtains floating in mid air
are *worse* than the current lower-fidelity floating curtains.

### 2026-04-19 manual audit — Batch 4 delegated sweep (L2-L10, L21, L22)

Mechanical cleanup sourced from the 2026-04-19 audit report. Delegated
to a subagent so the main thread stayed focused on structural work.
All 1878 tests pass.

#### Dead public API (L2-L7) — 6 methods deleted after cross-repo grep

- `ResourceManager::loadTextureAsync` / `getAsyncPendingCount` /
  `getModelCount` — zero callers anywhere (engine, tests, tools, app).
- `FileWatcher::setOnFileChanged` / `getTrackedFileCount` — zero
  callers. ``m_onChanged`` is now permanently default-constructed;
  the dispatch branch in ``rescan()`` is unreachable and flagged for
  a future follow-up removal.
- `Benchmark::runDriverCaptured` — superseded by the W1 async-worker
  path (workbench 1.10.0). Doc references in `async_driver.*` and
  `SELF_LEARNING_ROADMAP.md` kept as historical context.

#### Dead shaders (L8-L10) — 4 of 5 deleted

- Deleted: ``bloom_blur.frag.glsl``, ``bloom_bright.frag.glsl``,
  ``basic.vert.glsl``, ``basic.frag.glsl``.
- **Kept**: ``ssr.frag.glsl`` — the audit entry was wrong; it's
  loaded at ``engine/renderer/renderer.cpp:397``. Flagged in the
  audit-tool improvements doc as a FP risk for detector #26
  (dead-shader grep).

#### L21 — `const Entity*` sweep (14 sites)

Converted non-mutating ``Entity*`` locals to ``const Entity*`` across
``editor.cpp`` (10 sites) and ``engine.cpp`` (4 sites). ~16 other
sites (``EntityFactory::createXxx``/``scene->createEntity`` results)
were skipped — those pointers are mutated immediately after creation.

#### L22 — `static` (10 functions)

Marked the listed member functions that never touch ``this`` as
``static``: ``AudioAnalyzer::computeFFT``, ``Window::pollEvents``,
``Editor::setupTheme``, ``FoliageManager::worldToGrid``,
``BVH::findBestSplit`` (was ``const``, now ``static``),
``Shader::compileShader``, three ``unbind()`` variants (``Mesh``,
``MeshPool``, ``DynamicMesh``), ``GPUParticleSystem::nextPowerOf2``,
``GPUParticleSystem::drawIndirect``.

### Editor launcher — CLI, wrapper, .desktop

Makes the editor discoverable to downstream users of the engine: a
stable ``vestige-editor`` entry point, a proper ``--help``, and a
Linux desktop-menu integration.

- **`app/main.cpp`** — reworked CLI parser. New flags: ``--editor``
  (explicit), ``--play`` (start in first-person mode with editor UI
  hidden), ``--scene PATH`` (load a saved scene instead of the demo),
  ``--assets PATH`` (override the asset directory), and ``-h``/
  ``--help`` (prints a full usage summary with examples). Unknown
  arguments produce a helpful error and exit code 2. The existing
  ``--visual-test`` and ``--isolate-feature`` flags are preserved.
- **`engine/core/engine.h` / `engine.cpp`** — ``EngineConfig`` gained
  ``startupScene`` and ``startInPlayMode``. ``Engine::initialize``
  calls ``SceneSerializer::loadScene`` after the built-in scene is
  set up (so a failed load falls back to the demo without crashing);
  paths are resolved against CWD first, then ``<assets>/scenes/``.
  When ``startInPlayMode`` is set the editor is flipped to
  ``EditorMode::PLAY`` and the cursor is captured at startup.
- **`packaging/vestige-editor.sh`** — thin bash wrapper that ``exec``s
  the sibling ``vestige`` binary. Lets desktop launchers reference a
  stable, obviously-named entry point without us having to ship two
  distinct binaries.
- **`packaging/vestige-editor.desktop`** — standard XDG desktop entry.
  Categories ``Graphics;3DGraphics;Development;Game``, MIME type
  ``application/x-vestige-scene`` (reserved for future scene-file
  registration), icon name ``vestige``.
- **`app/CMakeLists.txt`** — new ``editor_launcher`` custom target
  copies the wrapper + ``.desktop`` into ``build/bin/`` every build,
  and a Linux-only ``install()`` block places them at
  ``${prefix}/bin/vestige-editor`` and
  ``${prefix}/share/applications/vestige-editor.desktop``.
- **`README.md`** — new "Launching the editor" section with CLI
  examples and a controls table. Fixes the stale ``./build/Vestige``
  path (actual: ``./build/bin/vestige``).

### 2026-04-19 manual audit — batch 4/5 deferred fixes

Close-out pass over the Medium-severity items deferred from batch 1/2/3
(commit `676ab34`). All 1878 tests pass; one GTest case added for
``safePow`` emission plus new EXPECT_* assertions folded into three
existing cases (``HelpersMatchEvaluatorPrecisely``,
``CodegenGlslEmitsSafeDivAndHelpers``, ``GlslPreludeDefinesAllFourHelpers``).

#### Medium severity (7)

- **`engine/utils/json_size_cap.h` + `.cpp` (new)** — shared
  ``JsonSizeCap::loadJsonWithSizeCap`` + ``loadTextFileWithSizeCap``
  helpers. Replaces the hand-rolled ``ifstream + json::parse`` pattern
  at every JSON/text loader site listed below. Default 256 MB cap
  matches obj_loader / gltf_loader / scene_serializer. **(AUDIT M17–M26.)**
- **`engine/formula/formula_library.cpp`,
  `engine/formula/formula_preset.cpp`,
  `engine/utils/material_library.cpp`,
  `engine/editor/recent_files.cpp`,
  `engine/editor/prefab_system.cpp`,
  `engine/animation/lip_sync.cpp`** — routed all six JSON/text loaders
  through the new helpers. RecentFiles uses a 1 MB cap (tiny file);
  LipSync keeps an inline 16 MB cap (Rhubarb tracks). **(AUDIT M17–M23.)**
- **`engine/formula/lut_loader.cpp`** — hard 64 M-sample
  (``MAX_LUT_SAMPLES = 256 MB``) ceiling above the pre-existing
  SIZE_MAX / streamsize overflow guards. A 2000³-axis header would
  otherwise authorise an 8 GB float allocation. **(AUDIT M24.)**
- **`engine/renderer/shader.cpp`** — ``loadFromFiles`` / ``loadCompute``
  now go through ``loadTextFileWithSizeCap`` with an 8 MB shader-source
  ceiling. **(AUDIT M26.)**
- **`engine/renderer/skybox.cpp::loadEquirectangular`** — 512 MB
  equirect on-disk cap before handing off to stb_image; a hostile HDR
  header would otherwise drive stbi into multi-GB allocations.
  **(AUDIT M26.)**
- **`engine/editor/widgets/animation_curve.cpp::fromJson`** — 65 536
  keyframe ceiling on the ``push_back`` loop. A malicious ``.scene``
  carrying a 10M-element curve array used to allocate gigabytes here.
  **(AUDIT M26.)**
- **`engine/renderer/text_renderer.{h,cpp}`** — batched glyph upload.
  Both ``renderText2D`` and ``renderText3D`` now build one vertex array
  for the whole string, issue one ``glNamedBufferSubData`` + one
  ``glDrawArrays``, and truncate strings above
  ``MAX_GLYPHS_PER_CALL = 1024`` (≈ 96 KB vertex data). Previously the
  loop issued one upload + one draw per glyph. **(AUDIT M29.)**

#### Medium — Formula Pipeline (1)

- **`engine/formula/safe_math.h`,
  `engine/formula/expression_eval.cpp`,
  `engine/formula/codegen_cpp.cpp`,
  `engine/formula/codegen_glsl.cpp`** — new
  ``Vestige::SafeMath::safePow(base, exp)`` + matching GLSL prelude
  definition. Integer exponents pass through unchanged (``pow(-2, 3)
  = -8``); fractional exponents on negative bases project to 0 instead
  of returning NaN. All three evaluation paths (tree-walking
  evaluator, C++ codegen, GLSL codegen) now route ``pow`` through the
  shared helper so LM-fitter R² / AIC / BIC scores no longer diverge
  from the runtime. 7 new GTest cases; ``CodegenCpp.EmitBinaryOps``
  and ``CodegenGlsl.GenerateFunction`` updated for the new emission.
  **(AUDIT M11; CLAUDE.md Rule 11.)**

#### High severity (1)

- **`engine/renderer/renderer.cpp` (bloom FBO + 2× capture FBOs),
  `engine/renderer/light_probe.cpp::generateIrradiance`** — added
  ``glCheckNamedFramebufferStatus`` with a placeholder colour
  attachment at creation time for each of the 4 FBOs that previously
  had no completeness verification. Matches the pattern already used
  in ``cascaded_shadow_map.cpp``, ``environment_map.cpp``,
  ``framebuffer.cpp``, ``water_fbo.cpp``, ``text_renderer.cpp``.
  **(AUDIT M15.)**

#### Low severity (4, safe subset)

- **`engine/editor/panels/welcome_panel.cpp`** — dropped unused
  ``#include "core/logger.h"``. **(AUDIT L36.)**
- **`engine/formula/formula_preset.cpp::loadFromJson`** — renamed local
  ``count`` → ``loaded`` so it no longer shadows the
  ``FormulaPresetLibrary::count()`` member. **(AUDIT L37.)**
- **`engine/editor/panels/inspector_panel.cpp`** — removed the dead
  ``before = cfg;`` assignment; the variable goes out of scope at the
  following ``ImGui::TreePop()``. **(AUDIT L38.)**
- **`engine/core/engine.cpp`** — explicit ``default: break;`` on the
  keyboard-event switch (L28), and ``const Exclusion exclusions[]``
  for the foliage exclusion table (L26).

#### Housekeeping

- **`.gitignore`** — ignore ``/audit_rule_quality.json`` (raw
  per-rule-hit dump emitted by ``tools/audit/`` into the repo root).

### 2026-04-19 manual audit — batch 1/2/3 fixes

29 files touched, +490 / −170. All 1878 tests pass. Findings report in
`docs/AUDIT_2026-04-19.md` (gitignored per `docs/AUDIT_[0-9]*.md`).

#### High severity (12)

- **`engine/renderer/renderer.cpp` (per-object motion-vector overlay):**
  switched `glDepthFunc(GL_LESS)` → `GL_GREATER` so the pass writes
  under the engine's reverse-Z convention. The motion FBO is cleared
  with `glClearDepth(0.0)` (= far in reverse-Z); under `GL_LESS` no
  fragment ever passed, leaving TAA on camera-only motion for all
  dynamic objects. Fixes the 2026-04-13 visual regression flagged in
  the source comment at line 938.
- **`engine/utils/gltf_loader.cpp::readFloatAccessor`:** added
  `accessor.count > SIZE_MAX / componentsPerElement` overflow check
  before `result.resize(...)` — a malicious glTF could otherwise size
  the output to a tiny vector and have the subsequent `memcpy` walk
  off the end.
- **`engine/utils/entity_serializer.cpp` (6 texture-slot sites):** new
  file-scope `sanitizeAssetPath` rejects absolute paths and `..`
  components in scene-JSON-sourced texture paths (`diffuseTexture`,
  `normalMap`, `heightMap`, `metallicRoughnessTexture`,
  `emissiveTexture`, `aoTexture`). Scene loading no longer trusts
  untrusted paths directly to `ResourceManager::loadTexture`.
- **`engine/editor/scene_serializer.cpp`:** new static helper
  `openAndParseSceneJson` with a 256 MB file-size cap (matches
  obj_loader / gltf_loader). Replaces 4 separate `json::parse(file)`
  call sites that previously had no ceiling — a 10 GB `.scene` would
  OOM-kill the process.
- **`tools/audit/web/app.py::/api/detect`:** added `_is_safe_path(root)`
  403 guard that every sibling endpoint was already enforcing. The
  endpoint could previously be used to probe arbitrary filesystem
  directories via the web UI.
- **`engine/renderer/shader.h` + `.cpp`:** all `set*` setters now take
  `std::string_view` (was `const std::string&`). The uniform cache is
  now `std::map<std::string, GLint, std::less<>>` (transparent) so
  `const char*` / string-literal callers cost zero heap allocations on
  cache hits. Was ~250 temporary `std::string` allocations per frame
  per `renderScene` call, most ≥16 chars and therefore past libstdc++
  SSO.
- **`engine/renderer/renderer.cpp::drawMesh` (morph path):**
  pre-cached the `u_morphWeights[0..7]` uniform names as
  `static const std::array<std::string, 8>` so we don't rebuild the
  indexed name via `std::to_string` + concat on every morph-targeted
  draw.
- **`engine/renderer/renderer.cpp::renderScene` (MDI material
  grouping):** `m_materialGroups` now clears each inner vector
  (preserving capacity) instead of clearing the outer map — was
  destroying every per-material vector's buffer and re-allocating on
  every frame's `push_back` chain.
- **`engine/environment/foliage_manager.{h,cpp}`:** added out-param
  overloads of `getAllChunks` and `getVisibleChunks` so the shadow
  pass (up to 4 cascades per frame) and main foliage render reuse
  scratch vectors (`Renderer::m_scratchFoliageChunks`,
  `Engine::m_scratchVisibleChunks`) instead of allocating a fresh
  `std::vector<const FoliageChunk*>` per call.
- **`engine/renderer/renderer.cpp::captureIrradiance`:** deleted the
  unused `std::vector<float> facePixels(faceSize²·3)` — allocation was
  never read or written; only `cubemapData` at line 2042 was the
  actual read target. Also promoted `faceSize * faceSize * 3` to
  `size_t` arithmetic for overflow safety.
- **`engine/utils/gltf_loader.cpp::loadGltf` (POSITION read):** removed
  the unreachable `if (!hasPositions) continue;` defensive check; every
  path that fails to populate positions already `continue`s earlier.
- **`engine/editor/entity_factory.cpp::createParticlePreset`:** removed
  the dead `std::string entityName = "Particle Emitter"` initializer —
  every `if`/`else if`/`else` branch overwrote it, making the literal
  suggest a fallback that never activated.

#### Medium severity (15)

- **`tools/formula_workbench/async_driver.cpp`:** narrowed the
  PID-reuse TOCTOU race by clearing `m_childPid` to -1 *before*
  `waitpid()`. Linux only frees the PID on `waitpid`, so the stale-pid
  window is now zero inside normal cancel/poll flows. Full pidfd-based
  fix deferred to a future glibc 2.36+ upgrade.
- **`tools/formula_workbench/async_driver.cpp::start`:** try/catch
  around `std::thread` construction — on OOM-throw the orphaned child
  is now reaped via `SIGKILL` + `waitpid`, and pipe fds are closed.
  Previously leaked both on a vanishingly rare but possible failure.
- **`tools/formula_workbench/pysr_parser.cpp`:** added
  `MAX_PARSE_DEPTH = 256` via RAII `DepthGuard` in
  `parseAdd` / `parseUnary` / `parsePrimary`. Closes a stack-overflow
  DoS on deeply nested expressions (same pattern as CVE-2026-33902
  ImageMagick FX parser, CVE-2026-40324 Hot Chocolate GraphQL parser).
- **`tools/formula_workbench/pysr_parser.cpp`:** swapped `std::strtof`
  for `std::from_chars` — the former is locale-aware and would misparse
  `"1.5"` as `1` under a German locale. `from_chars` is locale-free
  (C++17, libstdc++ 11+).
- **`tools/formula_workbench/fit_history.cpp::toHex64`:** format string
  `%016lx` + `unsigned long` cast silently truncated the high 32 bits
  of a `uint64_t` on Windows (LLP64 — `unsigned long` is 32-bit).
  Swapped to `%016llx` + `unsigned long long`.
- **`engine/editor/entity_actions.cpp` (align + distribute):** two
  use-after-move bugs — `entries.size()` was read *after*
  `std::move(entries)` on the preceding line, so "N entities" always
  logged as 0. Captured `size_t count = entries.size()` before the
  move.
- **`engine/animation/motion_database.cpp::getFrameInfo / getPose`:**
  added empty-database guards — `std::clamp(x, 0, size()-1)` is UB
  when the database is empty (hi < lo). Now returns a static empty
  `FrameInfo` / `SkeletonPose` in that case.
- **`engine/renderer/renderer.{h,cpp}`:** stored the
  `WindowResizeEvent` subscription token and unsubscribed it in
  `~Renderer`. Engine owns both `Renderer` and `EventBus`, and
  `~Renderer` runs first — a resize event published during teardown
  would previously call into a half-destroyed `Renderer`.
- **`engine/editor/tools/ruler_tool.h::isActive`:** now includes the
  `MEASURED` state. `processClick` still consumes clicks in `MEASURED`
  (restarts the measurement); callers that gated viewport-click
  routing on `isActive()` were double-routing those clicks.
- **`engine/utils/gltf_loader.cpp::resolveUri`:** path-prefix check now
  appends a separator before comparing, so `base=/assets/foo` no
  longer accepts `/assets/foo_evil/x.png`.
- **`engine/environment/terrain.cpp::loadHeightmap` / `loadSplatmap`:**
  added `file.gcount()` checks after `file.read(...)`. A truncated
  terrain file was previously leaving `m_heightData` / `m_splatData`
  with partial fresh + partial stale contents.
- **`tools/audit/web/app.py::/api/config` (GET):** extension gate now
  restricts to `.yaml` / `.yml` (mirroring the PUT sibling). Was
  previously able to read any file inside allowed roots.
- **`engine/core/first_person_controller.cpp::applyDeadzone`:** added
  `std::isfinite(v)` + `std::clamp(v, -1.0f, 1.0f)` on gamepad axis
  input. A faulty HID report / driver bug could produce NaN or
  out-of-range values that propagated into camera rotation.
- **`engine/renderer/foliage_renderer.cpp`:** hoisted the
  `m_visibleByType[typeId]` map lookup out of the per-instance loop.
  Thousands of `unordered_map::operator[]` hash probes per frame
  become tens.
- **`engine/renderer/renderer.h::m_frameArena`:** added `{}`
  value-initialization to silence the recurring cppcheck
  `uninitMemberVar` — the pmr arena overwrites this storage anyway,
  but cppcheck needed an explicit initializer to stop re-flagging it
  every audit run.

#### Low (4)

- **`VERSION`**: synced `0.1.4 → 0.1.5` to match CMakeLists.txt (drift
  introduced in commit `200d75f`; `scripts/check_changelog_pair.sh`
  expects these to match).
- **`engine/renderer/renderer.cpp`**: removed dead
  `setVec2("u_texelSize", ...)` call — the matching shader uniform
  was never declared in `motion_vectors.frag.glsl`, so the set was a
  silent no-op.
- **`app/main.cpp`**, **`tools/formula_workbench/main.cpp`**: added the
  standard `// Copyright (c) 2026 Anthony Schemel` + SPDX-License
  headers that every other `.cpp`/`.h` in the repo carries.
- **`engine/formula/node_graph.cpp`**: collapsed the redundant
  `else if (abs|sqrt|negate)` branch into the final `else` — both
  already assigned `MATH_ADVANCED` (clang-tidy
  `bugprone-branch-clone`).

### Documentation

- **`ROADMAP.md`**: 6 sections updated from the GDC 2026 /
  SIGGRAPH 2025 research pass. WishGI SH-fit lightmaps, Brixelizer SDF
  GI (primary RDNA2-feasible software-RT path), HypeHype + MegaLights
  stochastic lighting split, GPU-driven MDI + Hi-Z flagged as the
  highest-ROI OpenGL 4.5 item, Slang language unification added,
  partitioned TLAS annotated with the VK_KHR watch note, tonemapping
  policy (ACES 1.3 default, 2.0 opt-in), accessibility section
  expanded with 4 new items. References linked to slide PDFs.
- **`SECURITY.md`**: added CVE-2026-23213 (AMD amdgpu kernel SMU-reset
  flaw — RDNA2/RDNA3) and Mesa 26.0.x regression notes. New Linux
  support matrix: minimum kernel 6.9+, minimum Mesa 26.0.4.
- **`.claude/settings.json`**: added a read-only permission allowlist
  (15 entries — cmake/ctest/make/cppcheck/clang-tidy plus
  MCP filesystem read tools) to reduce per-turn prompts during audit
  sessions.

## [0.1.5] - 2026-04-18

### Fixed — completes 2026-04-16 strict-aliasing sweep

One actionable finding from the 2026-04-18 full audit (5,149 raw
findings, 1 actionable = 0.02% post-triage). Closes out the morph-
target sites that were missed in engine 0.1.4 (commit `1f6fd24`).

- **`engine/utils/gltf_loader.cpp` (lines 770, 790, 810): strict-
  aliasing UB in morph-target delta loading.** The 0.1.4 sweep fixed
  the matching pattern in `nav_mesh_builder.cpp` but left three
  identical sites in the glTF morph-target POSITION/NORMAL/TANGENT
  loops unpatched. glTF `byteStride` is not required to preserve
  4-byte alignment, and `reinterpret_cast<const float*>` on an
  `unsigned char*` is a strict-aliasing violation regardless of
  alignment — `-O2` is free to reorder or elide the loads. Replaced
  each cast with `float fp[3]; std::memcpy(fp, data + stride*i,
  sizeof(fp));` — same AMD64 codegen, portable under strict-aliasing.
  cppcheck: `invalidPointerCast` (portability) × 3.

### Tooling

- **`.gitleaksignore`**: added `docs/AUTOMATED_AUDIT_REPORT_*` so
  gitleaks stops re-emitting 3,500+ false-positive `generic-api-key`
  hits on every audit. The hits are our own audit tool's JSON
  `results` sidecars — rule IDs and short hashes tripping the
  generic-API-key regex. No real secrets in repo.

## [0.1.4] - 2026-04-17

### Fixed — cppcheck audit cycle

Eight actionable cppcheck findings from the 2026-04-16 audit run (1
portability bug, 7 performance hits) against a noise baseline of ~300
raw findings. Triage kept local per `AUDIT_STANDARDS.md`.

- **`engine/navigation/nav_mesh_builder.cpp`: strict-aliasing UB in
  scene-geometry collection.** `reinterpret_cast<const float*>` on a
  `uint8_t*` VBO buffer violated the strict-aliasing rule; `-O2` is
  free to reorder or elide such loads, so the UB was latent rather
  than visibly buggy. Switched to `std::memcpy` into a local
  `float[3]` — the standard-blessed way to reinterpret bytes as a
  different trivially-copyable type. Compiles to the same load on
  AMD64; portable under strict-aliasing. cppcheck:
  `invalidPointerCast` (portability).

- **`engine/formula/lut_generator.cpp`: redundant map lookup in
  default-variable insertion.** `vars.find(name) == end()` followed
  by `vars[name] = default` is now `vars.try_emplace(name, default)`
  — one traversal instead of two. cppcheck: `stlFindInsert`.

- **`engine/formula/node_graph.cpp`: redundant set probe in
  cycle-detection frontier.** `visited.count(target) == 0` followed
  by `visited.insert(target)` is now
  `if (visited.insert(target).second)` — `insert` returns
  `{iter, inserted}`, so a single call replaces the count-then-insert
  pair. cppcheck: `stlFindInsert`.

- **`engine/utils/cube_loader.cpp` + `tests/test_color_grading.cpp`:
  `line.find(x) == 0` → `line.rfind(x, 0) == 0`.** The
  `rfind(x, 0)` overload only searches at position 0 so it
  short-circuits as soon as the prefix matches or fails; the
  `find(x) == 0` form scans the whole string before reporting the
  position. C++17-compatible equivalent of `starts_with()` (which is
  C++20). Seven call sites updated. cppcheck: `stlIfStrFind`.

## [0.1.3] - 2026-04-15

### Changed — launch-prep: `VESTIGE_FETCH_ASSETS` default → OFF

- **Default changed** in `external/CMakeLists.txt`: fresh clones no
  longer attempt to pull the `milnet01/VestigeAssets` CC0 asset pack.
  The sibling repo stays private until ~v1.0.0 pending a final
  redistributability audit of every 4K texture and `.blend.zip`
  archive. The engine's demo scene renders correctly against the
  in-engine 2K CC0 set shipped in `assets/` (Poly Haven plank /
  brick / red_brick, glTF sample models, Arimo font) — no asset
  download is required. Maintainers with access to the private
  sibling repo can opt in with `-DVESTIGE_FETCH_ASSETS=ON`.

- Public docs updated to reflect the new default and the
  private-assets-repo status: `README.md`, `ASSET_LICENSES.md`,
  `SECURITY.md`, `THIRD_PARTY_NOTICES.md`, `ROADMAP.md`. CI comments
  in `.github/workflows/ci.yml` rewritten: the `-DVESTIGE_FETCH_ASSETS=OFF`
  flag is now an explicit default-match (testing the fresh-public-clone
  path), not a temporary stopgap.

- Launch-sweep script (`scripts/final_launch_sweep.sh`) end-of-run
  message updated: no "remove the flag" step, single-repo flip only.

When VestigeAssets later goes public the flip is a single commit
that sets the default back to `ON`, drops the explicit flag from
CI, and re-links the sibling repo in `README.md`.

### Changed — launch-prep: Timer → `std::chrono::steady_clock`

- **`engine/core/timer.cpp` no longer depends on GLFW.** Switched the
  time source from `glfwGetTime()` to `std::chrono::steady_clock` via a
  private `elapsedSecondsSince(origin)` helper. Public API is unchanged
  — callers still see the same `update()` / `getDeltaTime()` /
  `getFps()` / `getElapsedTime()` semantics. The only observable
  behavioural difference is `getElapsedTime()`'s epoch: previously
  "seconds since GLFW init", now "seconds since Timer construction".
  The sole non-test caller (wind animation in `engine::render`) only
  uses rate-of-change, so the epoch shift is invisible.

- **`tests/test_timer.cpp` is now a pure unit test.** Removed
  `glfwInit()` / `glfwTerminate()` from `SetUp`/`TearDown`. Added two
  new tests: `ElapsedTimeAdvancesWithWallClock` (verifies monotonic
  advance across a 10 ms `sleep_for`) and `FrameRateCapRoundTrip`
  (verifies the uncapped/capped/uncapped setter round-trip).

- **Root cause for the test-suite flakiness surfaced by
  `scripts/final_launch_sweep.sh`.** `glfwInit()` pulled in
  libfontconfig / libglib global caches, which LeakSanitizer flagged
  as an 88-byte leak at process exit under parallel `ctest -j nproc`.
  The test logic itself always passed, but the LSan leak tripped the
  harness's exit code — giving flaky 1-3 test failures across
  back-to-back sweep runs, which in turn cascaded into launch-sweep
  "regressions" (`tests_failed` contributes to the audit HIGH count).
  Removing the GLFW init removes the whole libglib/libfontconfig
  lifecycle from `vestige_tests` (it was the only test that called
  `glfwInit`). Five consecutive parallel runs now pass 100%.

## [0.1.2] - 2026-04-13

### Fixed — §H18 + §H19 divergent SH grid radiosity bake

Post-audit follow-up. User reported every textured surface looked
like an emissive light after the §H14 SH basis correction landed;
bisection via the new `--isolate-feature` CLI flag localised it to
the IBL diffuse path, specifically the SH probe-grid contribution.
Two real bugs, neither addressed by §H14 / §M14:

- **§H19 SH grid irradiance was missing the /π conversion** — the
  *real* cause. `evaluateSHGridIrradiance` returns Ramamoorthi-Hanrahan
  irradiance E (`∫ L(ω) cos(θ) dω`); the diffuse-IBL formula at the
  call site is `kD * irradiance * albedo`, which assumes the
  *pre-divided* value E/π that LearnOpenGL's pre-filtered irradiance
  cubemap stores (PI is multiplied in during the convolution, then
  implicitly divided back out via `(1/nrSamples) * PI`). Without the
  /π division, the SH grid path produced a diffuse contribution
  π × the correct value, so the radiosity transfer factor became
  `π × albedo`. For any albedo ≥ 1/π ≈ 0.318 — i.e. all common
  materials — that's > 1, and the multi-bounce bake series diverged
  instead of converging. Observed energy growth ~1.7× per bounce
  matched `π × scene-average-albedo ≈ π × 0.54` exactly. Fix: divide
  the SH evaluation result by π so it matches the cubemap convention.
  Bake now converges geometrically (Tabernacle scene: 5.47 → 6.16 →
  6.49, deltas 0.69 → 0.33).

- **§H18 skybox vertex shader was Z-convention-blind** — masked the
  §H19 bug below the surface. The shader hard-coded
  `gl_Position.z = 0`, which is the far plane in reverse-Z (main
  render path) but the *middle* of the depth buffer in forward-Z
  (capture passes used by `captureLightProbe` and `captureSHGrid`).
  Without this fix, the §M14 workaround had to gate the skybox out
  of capture passes entirely, leaving the SH probe-grid bake without
  any sky direct contribution and forcing it to feed off pure
  inter-geometry bounce — the exact configuration where §H19's
  missing /π factor blew up. The shader now reads `u_skyboxFarDepth`
  and emits `z = u_skyboxFarDepth * w`, so z/w = u_skyboxFarDepth
  after the perspective divide. The renderer sets the uniform per
  pass: 0 for reverse-Z main render, 0.99999 for forward-Z capture
  (close-but-not-equal-to-1.0 so GL_LESS still passes against the
  cleared far buffer). The §M14 `&& !geometryOnly` gate is removed
  since the skybox now draws correctly in both Z conventions. Sky
  direct light is back in the SH grid bake.

- **Diagnostic CLI flag `--isolate-feature=NAME`** retained for
  future regression bisection. Recognised values: `motion-overlay`,
  `bloom`, `ssao`, `ibl`, `ibl-diffuse`, `ibl-specular`, `sh-grid`.
  Each disables one specific renderer feature so a `--visual-test`
  run's frame reports can be diff-mechanically compared against a
  baseline to identify the offending subsystem. Used to find
  §H18+§H19 in 5 short visual-test passes — without it the bisection
  would have required either reverting commits or interactive shader
  editing.

## [0.1.1] - 2026-04-13

### Fixed — §H17 SystemRegistry destruction lifetime

Post-audit follow-up to the 0.1.0 audit cycle. The §H16 fix
(gated `SaveSettings` during `ed::DestroyEditor`) closed one
shutdown SEGV but a second, independent SEGV remained, surfacing as
ASan "SEGV on unknown address (PC == address)" + nested-bug abort
immediately after the "Engine shutdown complete" log line.

- **§H17 SystemRegistry destruction lifetime**: root cause was
  structural, not the §H16 ImGui-node-editor race:
  `SystemRegistry::shutdownAll()` called each system's `shutdown()`
  but left the `unique_ptr<ISystem>` entries in the vector. The
  systems' destructors therefore ran during `~Engine` member
  cleanup — *after* `m_renderer.reset()` and `m_window.reset()` had
  already destroyed the renderer and torn down the GL context — so
  any system dtor that touched a cached Renderer*/Window* or freed a
  GL handle dereferenced freed memory or called a dead driver
  function pointer. New `SystemRegistry::clear()` destroys the
  systems in reverse registration order; `Engine::shutdown()` calls
  it immediately after `shutdownAll()` so destruction happens while
  shared infrastructure is still alive. Closes the §H16
  runtime-verification deferral — §H16 (ed::DestroyEditor
  SaveSettings race) was correct as far as it went; §H17 was the
  second, independent shutdown path that masked the §H16 fix's
  success. Six new unit tests in `tests/test_system_registry.cpp`
  pin the contract: destructors run in reverse order inside
  `clear()`, the registry empties, `clear()` is idempotent, and the
  canonical `shutdownAll()` → `clear()` sequence produces the
  expected eight-event log.

## [0.1.0] - 2026-04-13

Initial changelog entry. Prior history captured in `ROADMAP.md` Phase
notes and `docs/PHASE*.md` design documents.

Subsystems in place as of this release:
- Core (engine/window/input/event-bus/system-registry)
- Renderer (OpenGL 4.5 PBR forward, IBL, TAA, SSAO, bloom, shadows, SH probe grid)
- Animation (skeleton, IK, morph, motion matching, lip sync)
- Physics (rigid body, constraints, character controller, cloth)
- Scripting (Phase 9E visual scripting, 60+ node types)
- Formula (template library, Levenberg-Marquardt curve fitter, codegen)
- Editor (ImGui dock-based; Phase 9E-3 node editor panel in progress)
- Scene / Resource / Navigation / Profiler / UI / Audio

### Security — audit cycle
- Flask web UI of the audit tool hardened against path-traversal and shell-injection (affects local-dev setups that ran the web UI only; no public deployment). Details in `tools/audit/CHANGELOG.md` v2.0.1–2.0.6.
- **Formula codegen injection hardened** (AUDIT.md §H11). `ExprNode::variable/binaryOp/unaryOp` factories + `fromJson` now validate identifiers against `[A-Za-z_][A-Za-z0-9_]*` and operators against an allowlist. Codegen (C++ + GLSL) throws on unknown op instead of raw-splicing. A crafted preset JSON like `{"var": "x); system(\"rm -rf /\"); float y("}` is now rejected at load time, well before any generated header is compiled.

### Fixed — audit cycle

**Scripting (§H5–§H9, §M5–§M8)**
- **§H5 UB `&ref == 0` guards**: `Engine& m_engine` → `Engine*`. `reinterpret_cast<uintptr_t>(&m_engine) == 0` was undefined behavior that `-O2` could fold to false, crashing release builds on paths that rely on the guard.
- **§H6 Blackboard `fromJson` cap bypass**: `fromJson` now routes through `set()`, enforcing `MAX_KEYS = 1024` and `MAX_STRING_BYTES = 256`.
- **§H7/§H8 pure-node memoization opt-out**: `NodeTypeDescriptor::memoizable` flag; `GetVariable`, `FindEntityByName`, `HasVariable` are marked non-memoizable so loop bodies see fresh reads after `SetVariable`. `WhileLoop.Condition` now works — previously froze at its first value.
- **§H9 latent-action generation token**: `ScriptInstance::generation()` bumps on every `initialize()`; Timeline onTick lambdas capture and validate, dropping stale callbacks across the editor test-play cycle instead of dereferencing nodeIds from a rebuilt graph.
- **§M5** `ScriptGraph::addConnection` now `clampString`s pin names (matches the on-disk load path).
- **§M6** `isPathTraversalSafe` rejects absolute paths, tilde paths, and empty strings, not just `..` components.
- **§M7** `subscribeEventNodes` warns on unknown `eventTypeName` (known-not-yet-wired types exempted), surfacing typos that used to produce silent non-firing nodes.
- **§M8** Quat JSON order documented as `[w, x, y, z]` on both serializer and deserializer.

**Formula pipeline (§H12, §H13, §M9, §M10, §M12)**
- **§H12 Evaluator↔codegen safe-math parity**: new `engine/formula/safe_math.h` centralises `safeDiv`, `safeSqrt`, `safeLog`. Evaluator, C++ codegen, and GLSL codegen (via a prelude) all share the same semantics, so the LM fitter's coefficients no longer validate against one set of math and ship a different one.
- **§H13 curve-fitter non-finite residuals** (already landed in `d007349`): LM bails on NaN/Inf initial residuals with an explanatory message; rejects non-finite trial steps; accumulators are `double`.
- **§H14 SH basis constant** (already landed in `553277d`): `assets/shaders/scene.frag.glsl:553` changed from `c3 = 0.743125` to `c1 = 0.429043` on the L_22·(x²−y²) band-2 term (Ramamoorthi-Hanrahan Eq. 13). Removes a ~1.73× over-weight that tilted chromatic response on indoor ambient bakes.
- **§M9** `NodeGraph::fromJson` throws on duplicate node IDs; `m_nextNodeId` is recomputed as `max(id)+1` regardless of the serialised counter.
- **§M10** `fromExpressionTree` CONDITIONAL now logs a warning on the import-time logic loss (collapse to `literal(0)` with orphaned branch sub-trees). Root-cause fix tracked in ROADMAP.md §Phase 9E "Deferred".
- **§M12** `ExprNode::toJson` guards against null children so malformed in-memory trees emit a null placeholder instead of crashing the save path.

**Renderer + shaders (§H15, §M13–§M18, §L4, §L5)**
- **§H15 per-object motion vectors**: new overlay pass after the full-screen camera-motion pass. New shaders `motion_vectors_object.{vert,frag}.glsl` take per-draw `u_model` / `u_prevModel` matrices; Renderer tracks `m_prevWorldMatrices` keyed by entity id. TAA reprojection on dynamic / animated objects now reproduces their real motion instead of ghosting. TAA motion vector FBO gained a depth attachment so the overlay depth-tests against its own geometry.
- **§M13** BRDF LUT left column (NdotV=0) clamped to 1e-4 at entry so the LUT is Fresnel-peaked, not black. Fixes dark rim on rough dielectrics under IBL.
- **§M14** Skybox pass explicitly gated on `!geometryOnly` + hardening comment. Light-probe / SH-grid capture paths use forward-Z; skybox's `gl_FragDepth = 0` would have passed GL_LESS if the gate ever went away.
- **§M15** Bloom bright-extraction epsilon raised to 1e-2 and output clamped to `vec3(256.0)`. Saturated-hue pixels no longer amplify up to 10000× into the blur chain.
- **§M16/§M18** SSR and contact-shadow normals use four-tap central differences with gradient-disagreement rejection. No more silhouette halos at depth discontinuities.
- **§M17** Point-shadow slope-scaled bias scaled by `farPlane` so the same shader behaves correctly at Tabernacle-scale (~5m) and outdoor-scale (~100m) farPlanes — no more Peter-Panning indoors.
- **§L4** SH probe grid 3D-sampler fallbacks (units 17–23) rebound after both `captureLightProbe` and `captureSHGrid`. Prevents stale 3D texture reads on subsequent captures.
- **§L5** TAA final resolve `max(result, 0.0)` — cheap NaN clamp before history accumulation.

**Editor (§H16, §M1–§M4)**
- **§H16** imgui-node-editor shutdown SEGV root-caused. `NodeEditorWidget` routes `Config::SaveSettings`/`LoadSettings` through free-function callbacks gated on an `m_isShuttingDown` flag so `ed::DestroyEditor` no longer runs a save that dereferences freed ImGui state. Canvas layout persistence re-enabled at `~/.config/vestige/NodeEditor.json`.
- **§M1** `ScriptEditorPanel::makePinId` widened from 16-bit to 32-bit nodeId and 31-bit pinIndex. Eliminates collisions on generated/procedural graphs. Static-asserts 64-bit uintptr_t.
- **§M2** `ScriptEditorPanel::open(path)` preserves the existing graph on load failure (matches the header contract).
- **§M3** `NodeTypeDescriptor::inputIndexByName`/`outputIndexByName` populated at `registerNode` time; editor renders connections in O(1) instead of linear scans per frame.
- **§M4** Unknown pin names → skip connection + `Logger::warning` instead of silent "draw to pin 0".

**Misc (§L1–§L9 tail)**
- **§L1** `ScriptingSystem::initialize/shutdown` idempotency contract documented on the header.
- **§L2** Formula Workbench file-dialog `popen` now reads full output (was truncated at 512 bytes).
- **§L3** Curve fitter RMSE accumulators already moved to `double` in §H13 — noted in the CHANGELOG for audit completeness.
- **§L8** `NodeEditor.json` added to `.gitignore` (runtime layout artifact).
- **§L9** Audit tool `--keep-snapshots N` flag; `docs/trend_snapshot_*.json` gitignored.

### Added — infrastructure
- **§I1** GitHub Actions CI (`.github/workflows/ci.yml`): Linux Debug+Release matrix, build + ctest under xvfb, separate audit-tool Tier 1 job.
- **§I2** `.clang-format` at repo root mirroring CODING_STANDARDS.md §3 (Allman braces, 4-space indent, 120-column limit, pointer-left alignment). Not a hard gate — opportunistic enforcement.
- **§I3** Repo-level `VERSION` (0.1.0) + this `CHANGELOG.md` (already in place).
- **§I4** `.pre-commit-config.yaml` + `scripts/check_changelog_pair.sh`. Local git hook that fails commits touching `tools/audit/`, `tools/formula_workbench/`, or `engine/` without also touching the respective `CHANGELOG.md` (and `VERSION` if present). Bypassable with `--no-verify` for trivial fixes.

### Deferred to ROADMAP
Three known gaps documented in ROADMAP.md:
- Per-object motion vectors via MRT (eliminates the overlay pass; enables skinned/morphed motion) — Phase 10 rendering enhancements.
- `NodeGraph` CONDITIONAL node type (preserves `ExprNode` conditional round-trip) — Phase 9E visual scripting.
- imgui-node-editor shutdown SEGV visual confirmation — Phase 9E-3 step-4 acceptance (code-level race is closed; needs one editor launch to verify).
