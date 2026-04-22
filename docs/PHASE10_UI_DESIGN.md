# Phase 10 — In-Game UI System (Design Doc)

**Status:** Signed off 2026-04-21 — **complete**. Slices 12.1 – 12.5 all landed 2026-04-21.
**Roadmap item:** Phase 10 Features → "In-game UI system (menus, HUD, information panels/plaques)".
**Scope:** Game-screen state machine, HUD composition layer, menu-prefab signal wiring, default HUD layout, notification toasts. Ships as slices 12.1–12.5.

---

## 1. Why this doc exists

The roadmap bullet "In-game UI system (menus, HUD, information panels/plaques)" is unchecked, but a substantial amount of the underlying machinery has already been built across Phase 9C (Audio domain system) and Phase 10 Accessibility. Before specifying new work, this doc inventories what exists, identifies the genuine gaps, and proposes a slice breakdown that closes them without rebuilding what's already there.

This is **not** a green-field subsystem design. It's a wire-up and composition pass on top of a mature widget library.

---

## 2. What already exists (inventory)

| Area | Location | State |
|---|---|---|
| Domain system wrapper | `engine/systems/ui_system.{h,cpp}` | Complete. Owns SpriteBatchRenderer + UICanvas + base/scaled UITheme. Drives scale / high-contrast / reduced-motion rebuild. Modal-capture + cursor-hit input flags. Called every frame from `Engine::update`. |
| Widget base | `engine/ui/ui_element.{h,cpp}`, `ui_canvas.{h,cpp}` | Complete. Anchor-based layout, child hierarchy, signal-based events (`onClick`, `onHover`), role-tagged accessibility walk. |
| Widgets | `engine/ui/ui_{button,checkbox,slider,dropdown,keybind_row,label,panel,progress_bar,crosshair,fps_counter,image,interaction_prompt,world_label}.{h,cpp}` | Complete. Each widget sets its ARIA-like role in its constructor. |
| Menu prefabs | `engine/ui/menu_prefabs.{h,cpp}` | Partial. `buildMainMenu` / `buildPauseMenu` / `buildSettingsMenu` populate layout but **no signals are wired** — buttons are inert. Settings chrome ships without per-category controls (per-game concern). |
| Text rendering | `engine/renderer/text_renderer.{h,cpp}`, `engine/renderer/font.{h,cpp}` | Complete. FreeType-backed TrueType, 2D/3D text. The next roadmap bullet ("Text rendering (TrueType fonts)") is quietly already done. |
| Theme | `engine/ui/ui_theme.{h,cpp}` | Complete. Palette, typography, sizing; `withScale` / `withHighContrast` / `withReducedMotion` pure transforms. |
| Accessibility | `engine/accessibility/` + `ui_accessible.{h,cpp}` | Complete. ARIA-like role enum + canvas-wide `collectAccessible()` walk. Scale presets / high-contrast / reduced-motion compose via UISystem. |
| Editor ↔ runtime split | `Editor::setMode(EditorMode::{EDIT,PLAY})` | Complete for editor. No parallel state machine for a shipped game (main menu / playing / paused / settings / loading). |

**What is actually missing:**

1. **No game-screen state machine.** Only `EditorMode::{EDIT, PLAY}`. Nothing tracks main-menu → playing → paused. The engine boots straight into either editor or `--play` FP camera, with no main-menu screen.
2. **No HUD composition layer.** The widgets to build a HUD exist (crosshair, FPS counter, interaction prompt, progress bar); no prefab assembles them into a default HUD; no "HUD is visible when playing, hidden when paused" toggle.
3. **No cold-start menu wiring.** `buildMainMenu` returns a canvas of inert buttons. The `Engine` never calls it. `ESC` does not open `buildPauseMenu`.
4. **No notification toast.** Missing primitive for transient messages (objective updated, item collected, save completed, autosave notice).
5. **Editor panel gap.** AudioPanel / FormulaWorkbench / NodeGraph / NavigationPanel exist; no editor surface exercises the in-game UI — you can't preview the main menu or pause menu without running a game build.

Slices 12.1–12.5 close these five gaps.

---

## 3. Slice breakdown

| Slice | Title | Complexity | Ships |
|---|---|---|---|
| **12.1** | Game-screen state machine (pure function) | S | `engine/ui/game_screen.{h,cpp}` + unit tests. No engine wiring yet. |
| **12.2** | UISystem screen stack + Engine integration | M | `UISystem::pushScreen` / `popScreen` / `setRootScreen`. `Engine` opens MainMenu at cold-start (unless `--play` / editor), ESC toggles PauseMenu. `menu_prefabs` buttons wired to state transitions. |
| **12.3** | Notification toast primitives | S | `engine/ui/ui_notification_toast.{h,cpp}` + `NotificationQueue` (pure). No shader work. |
| **12.4** | Default HUD prefab | S | `buildDefaultHud(canvas, theme)` in `menu_prefabs` — crosshair + FPS counter (debug only) + bottom-centre interaction prompt anchor + top-right notification slot. |
| **12.5** | Editor panel — `UIPanel` | M | Four-tab surface mirroring AudioPanel: **State** (current screen + screen-push log) / **Menus** (preview main / pause / settings in the editor viewport) / **HUD** (toggle per-element) / **Accessibility** (compose scale + contrast + reduced-motion live). |

Volumetric / information-plaque scope is **deferred** to their own roadmap slots (not this bullet).

---

## 4. Slice 12.1 API — Game-screen state machine

### File surface

- `engine/ui/game_screen.h` — pure-function primitives (no GL, no engine types).
- `engine/ui/game_screen.cpp` — implementation.
- `tests/test_game_screen.cpp` — unit coverage.

### Types

```cpp
namespace Vestige
{

/// Top-level screens the game can be on. Mutually exclusive — only one
/// is the "root" screen at a time. Modal screens (settings, dialog)
/// stack on top via the separate screen-stack API in slice 12.2.
enum class GameScreen
{
    None,        // Engine is booted but nothing has been shown yet.
    MainMenu,    // Cold-start screen.
    Loading,     // Scene-load modal. Input suppressed.
    Playing,     // Gameplay: HUD visible, world input active.
    Paused,      // Pause menu overlay. World time frozen.
    Settings,    // Modal — composes over MainMenu or Paused.
    Exiting,     // Shutdown confirmation or cleanup.
};

/// Transition intents the UI can emit (buttons, hotkeys).
/// The state machine translates these into actual screen changes.
enum class GameScreenIntent
{
    OpenMainMenu,
    NewWalkthrough,   // MainMenu -> Loading -> Playing
    Continue,         // MainMenu -> Loading -> Playing (from save)
    OpenSettings,     // Current -> Settings (stacks)
    CloseSettings,    // Settings -> previous
    Pause,            // Playing -> Paused
    Resume,           // Paused -> Playing
    QuitToMain,       // Paused -> MainMenu
    QuitToDesktop,    // Any -> Exiting
    LoadingComplete,  // Loading -> Playing (fired by scene loader)
};

/// Pure transition function. Deterministic. No side effects.
/// Returns the new screen, or the unchanged input when the intent is
/// not valid from the current screen (invalid transitions are no-ops,
/// not errors — lets input handlers fire intents without asking first).
GameScreen applyGameScreenIntent(GameScreen current, GameScreenIntent intent);

/// Stable label for logging + editor debug readout.
const char* gameScreenLabel(GameScreen screen);
const char* gameScreenIntentLabel(GameScreenIntent intent);

/// True when the screen suspends world simulation (Paused, Loading, MainMenu).
/// Engine::update uses this to gate physics / animation ticks.
bool isWorldSimulationSuspended(GameScreen screen);

/// True when the screen suppresses world input (look / move / fire).
/// Input handlers gate on this OR UISystem::wantsCaptureInput().
bool suppressesWorldInput(GameScreen screen);

} // namespace Vestige
```

### Guarantees (= what tests enforce)

- **Transition table is total.** Every `(current, intent)` pair has a defined result — invalid intents leave the state unchanged (no exceptions, no asserts).
- **Stable labels.** `gameScreenLabel(GameScreen::Paused)` returns exactly `"Paused"`; editor debug readouts don't shift.
- **Pause freezes simulation.** `isWorldSimulationSuspended(Playing) == false`, `(Paused) == true`. `MainMenu` and `Loading` also return true.
- **Settings composes over both menus.** `OpenSettings` from `MainMenu` produces `Settings`; from `Paused` also produces `Settings`. `CloseSettings` tracks the screen it opened over — handled via the screen stack in slice 12.2, not this pure layer.
- **No self-transitions.** Firing `Pause` while already `Paused` returns `Paused` unchanged (idempotent; lets repeated ESC presses be safe).
- **`Exiting` is terminal.** No intent exits `Exiting`.

### Why pure

Matches the Phase 10 fog / accessibility pattern: pure-function core, CPU-testable without the engine or any GL context. The engine-side glue (UISystem, Editor integration) lives in slice 12.2 on top of this spec.

### What is not in slice 12.1

- Screen-stack for modal composition (`Settings` over `Paused`). Slice 12.2 adds a `std::vector<GameScreen>` stack in UISystem; the pure layer stays single-state.
- Actual canvas swaps. Slice 12.2 wires `UISystem::onScreenChanged(GameScreen)` to `buildMainMenu` / `buildPauseMenu` / `buildSettingsMenu`.
- Save / load wiring. `Continue` transitions into `Loading` — the save subsystem is out of scope.

---

## 5. Slice 12.2 architecture — Engine integration

### UISystem additions

```cpp
class UISystem : public ISystem
{
    // ... existing ...

    /// Sets the root screen. Clears any stacked modals. Rebuilds the
    /// canvas via the registered prefab factory.
    void setRootScreen(GameScreen screen);

    /// Pushes a modal screen on top of the root (Settings, confirm dialog).
    /// The modal canvas is built and rendered on top of the root canvas.
    /// Modal capture is enabled automatically.
    void pushModalScreen(GameScreen screen);

    /// Pops the top modal. If the stack is empty, this is a no-op.
    void popModalScreen();

    /// Current root screen. Defaults to GameScreen::None.
    GameScreen getRootScreen() const;

    /// Apply an intent via the pure state machine, then reconcile the
    /// canvas to match. The one call site game code should use.
    void applyIntent(GameScreenIntent intent);

    // Signal-based callbacks so game code can hook without subclassing.
    Signal<GameScreen> onRootScreenChanged;
    Signal<GameScreen> onModalPushed;
    Signal<GameScreen> onModalPopped;

    /// Builder callback type. Receives the target canvas + theme + text
    /// renderer + back-reference to this UISystem (so the prefab can wire
    /// button onClick signals to applyIntent). Ownership of widgets stays
    /// with the canvas.
    using ScreenBuilder = std::function<void(UICanvas&, const UITheme&,
                                             TextRenderer*, UISystem&)>;

    /// Register a custom builder for a given screen. Overrides the default
    /// prefab (e.g. game code can swap buildPauseMenu for a studio-branded
    /// pause screen without touching engine code). Pass nullptr to clear.
    void setScreenBuilder(GameScreen screen, ScreenBuilder builder);
};
```

**Custom pause-screen hook (and other screens).** `setScreenBuilder` lets a
game project override the default prefab for any `GameScreen` without
subclassing or forking `menu_prefabs.cpp`. Typical use:

```cpp
// In the game project's Game::initialize():
uiSystem.setScreenBuilder(GameScreen::Paused,
    [](UICanvas& canvas, const UITheme& theme, TextRenderer* tr, UISystem& ui)
    {
        buildStudioPauseMenu(canvas, theme, tr, ui);  // Game-specific prefab
    });
```

When `applyIntent(Pause)` fires, `UISystem::pushModalScreen(Paused)` looks up
the builder for `Paused`, falling back to `buildPauseMenu` if none is
registered. Same mechanism covers MainMenu, Settings, Loading — any screen
can be themed per-game.

### Engine wiring

`Engine::initialize` (behind a new `m_enableGameScreens` flag, default false so the editor's `--play` flow stays unchanged):

```cpp
if (m_enableGameScreens && !m_editor)  // Headless game build only
{
    m_uiSystem->setRootScreen(GameScreen::MainMenu);
}
```

`Engine::update`:
- ESC in `Playing` → `applyIntent(Pause)` instead of the current "close editor" path.
- ESC in `Paused` → `applyIntent(Resume)`.
- ESC in `Settings` → `applyIntent(CloseSettings)`.
- `isWorldSimulationSuspended(rootScreen)` gates physics / animation ticks.

`menu_prefabs` signal wiring (slice 12.2 add):

```cpp
// buildMainMenu(canvas, theme, textRenderer, uiSystem) — fourth arg added
// Each button's onClick fires the appropriate intent:
//   New Walkthrough -> applyIntent(NewWalkthrough)
//   Continue        -> applyIntent(Continue)    (if save exists)
//   Settings        -> applyIntent(OpenSettings)
//   Quit            -> applyIntent(QuitToDesktop)
```

Existing signature stays available (for games that wire their own signals); the new overload is the default path.

---

## 6. Slice 12.3 API — Notification toasts

### File surface

- `engine/ui/ui_notification_toast.{h,cpp}` — new widget + queue.
- `tests/test_notification_queue.cpp` — unit coverage.

### Types

```cpp
enum class NotificationSeverity
{
    Info,      // Default. Neutral palette.
    Success,   // Green accent.
    Warning,   // Amber accent.
    Error,     // Red accent.
};

struct Notification
{
    std::string title;           // Bold, one line.
    std::string body;            // Optional, up to two lines.
    NotificationSeverity severity = NotificationSeverity::Info;
    float durationSeconds = 4.0f;
};

struct ActiveNotification
{
    Notification data;
    float elapsedSeconds = 0.0f;  // 0 at spawn, >=duration when expired.
};

/// FIFO queue with per-tick countdown. Caps visible at 3 concurrent
/// (matches the subtitle default, consistent user experience).
/// Push-newest / drop-oldest when over capacity.
class NotificationQueue
{
public:
    static constexpr size_t DEFAULT_CAPACITY = 3;

    void push(const Notification& n);
    void advance(float deltaSeconds);
    const std::vector<ActiveNotification>& active() const;
    void clear();

    size_t capacity() const;
    void   setCapacity(size_t capacity);
};
```

### Rendering

`UINotificationToast` widget renders one `ActiveNotification`. Positioned top-right by default, 320 px wide, vertically stacked; fades in over 200 ms, fades out over 400 ms (respects `UITheme::transitionDuration` → reduced-motion snaps to 0 ms).

Severity colours read from the theme palette so high-contrast and user-recolour survive.

### Accessibility

- Each active toast is tagged `UIAccessibleRole::Label` with `{title + body}` as description so screen-reader bridge can announce.
- `reduceMotionFog`-style global flag already covers motion; no new flag needed.

---

## 7. Slice 12.4 API — Default HUD prefab

```cpp
/// Builds the default HUD composition for FP walkthrough gameplay.
/// Populates the canvas with (and wires to UISystem signals where
/// appropriate):
///   - Crosshair: CENTER, small, theme-coloured. Toggleable.
///   - FPS counter: TOP_LEFT, hidden unless debug flag set.
///   - Interaction prompt anchor: BOTTOM_CENTER (4-lines above bottom).
///   - Notification stack: TOP_RIGHT, 3 slots.
///
/// Game-specific overlays (health bar, inventory, minimap) are added
/// by the game project on top — this prefab provides the walkthrough
/// baseline.
void buildDefaultHud(UICanvas& canvas,
                     const UITheme& theme,
                     TextRenderer* textRenderer,
                     UISystem& uiSystem);
```

All four elements already exist as widgets; this is a pure composition function.

---

## 8. CPU / GPU placement (CLAUDE.md Rule 12)

| Work | Placement | Why |
|---|---|---|
| Game-screen state machine | CPU | Branching state transitions. Maybe 10 transitions per minute of play. Zero GPU benefit. |
| Canvas walk, hit-test, layout | CPU | Tree traversal, per-element branching. Already CPU. |
| Notification queue tick | CPU | O(≤3) items per frame. Branching. Zero GPU benefit. |
| Sprite batch draw | GPU | Already GPU (`SpriteBatchRenderer`). Per-pixel work — scales with screen size. |
| Glyph rasterization | GPU | Already GPU (`TextRenderer` uses texture atlas). Per-pixel. |

No work migrates between CPU and GPU in this phase. Placement is inherited from the existing UI subsystem and is correct.

---

## 9. Accessibility

Every slice composes with the existing `UITheme` transforms and `PostProcessAccessibilitySettings` flags. No new accessibility flags required:

- **UI scale preset** (existing) — applies automatically to menu_prefabs and the default HUD because they read theme sizes.
- **High-contrast** (existing) — palette swap survives prefab build.
- **Reduced-motion** (existing) — notification toast fade-in / fade-out snaps to 0 ms when `transitionDuration == 0`.
- **Screen-reader roles** (existing) — prefabs set accessible roles in every interactive widget; a future TTS bridge walks the canvas and announces.
- **Modal focus trap** — `UISystem::wantsCaptureInput()` already returns true during modal capture; slice 12.2 ensures `setModalCapture` flips when `pushModalScreen` is called so gamepad / keyboard nav can't escape to the world.

---

## 10. Performance targets

This phase is CPU-only (see §8). Targets are generous because the work is trivially small:

| Slice | CPU budget | Justification |
|---|---|---|
| 12.1 state machine | < 0.01 ms/frame | Pure function, called once per input event. |
| 12.2 canvas reconcile | < 0.1 ms on screen change | Rebuild happens on transition, not per-frame. Per-frame cost is the existing canvas walk. |
| 12.3 notification tick | < 0.01 ms/frame | O(≤3) items, single countdown. |
| 12.4 HUD walk | < 0.3 ms/frame | 5–10 widgets. Already captured in existing UISystem budget. |

No measurable impact on the 60 FPS floor. No benchmark harness needed.

---

## 11. Test strategy

- **Slice 12.1** — pure-function tests: label stability, every `(current, intent)` transition, world-simulation-suspended predicate, input-suppressed predicate, idempotent self-transitions, `Exiting` is terminal. Target: 25+ cases.
- **Slice 12.2** — engine integration smoke: headless test builds `UISystem`, applies intent, asserts `getRootScreen()` changes and `onRootScreenChanged` fires. Signal-wiring test: build each menu prefab, simulate button click via `element->onClick.emit()`, assert state changes.
- **Slice 12.3** — queue tests: FIFO, cap-at-3, push-newest evicts oldest, `advance(dt)` expires in order, `clear()` drops everything, negative `dt` no-op, `setCapacity` trims.
- **Slice 12.4** — prefab-build test: `buildDefaultHud` populates 4 elements with expected anchors; hit-test at each anchor hits the right widget.
- **Slice 12.5** — panel-open test + state readout correctness (mirror AudioPanel test pattern).

Target total across slices: ~60 new unit tests.

---

## 12. Explicit non-goals

To keep this phase bounded:

- **Information plaques.** Separate roadmap bullet. A later slice will add `PlaqueComponent` + trigger volumes + dialog-panel activation.
- **Settings persistence.** The "Settings system" roadmap item owns JSON save/load of settings values. Slice 12.2 opens the Settings chrome; game code wires the controls.
- **Scene/level configuration.** Separate roadmap item. The `Loading` screen is a thin progress bar; the actual scene load logic is out of scope.
- **Loading screens.** Engine-level scene transition work. The `Loading` screen state exists here; the transition orchestration is a separate bullet.
- **Gamepad-focus navigation across widgets.** The input-bindings action map exists (`engine/input/input_bindings`); per-screen D-pad focus traversal is a follow-up.
- **Multi-language text.** Localization is its own roadmap bullet. All strings shipped in this phase are English literals marked for future extraction.

---

## 13. Open questions for user sign-off

1. **Cold-start default.** Should the editor build always default to `GameScreen::None` (preserves current behaviour, opt in via a future Play button) and only headless game builds default to `MainMenu`, or should both default to `MainMenu` and the editor have an explicit "skip to viewport" toggle? Current proposal: editor defaults to None (no behaviour change). **→ Signed off 2026-04-21.**
2. **Pause semantics.** Freeze physics + animation when `Paused`, but keep UI widgets (sliders, transitions) animating? Matches Unreal / Unity default. Alternative: freeze everything including UI. Current proposal: freeze world, UI keeps ticking. **→ Signed off 2026-04-21, with amendment:** developers must be able to register a unique pause screen per-game. Captured in §5 as `UISystem::setScreenBuilder(GameScreen, ScreenBuilder)`, which applies to every screen, not just Paused.
3. **Scope of slice 12.2.** Ship the state machine + MainMenu/Pause wiring alone, or include Settings chrome wiring (opening / closing only — no per-setting values) in the same slice? Current proposal: include Settings open/close since the chrome is already built. **→ Signed off 2026-04-21.**
4. **Notification toast default capacity.** 3 concurrent (matches SubtitleQueue) or 5? Current proposal: 3. **→ Signed off 2026-04-21.**
5. **Editor `UIPanel`** (slice 12.5) **— should it preview the HUD against the live scene,** or render into an isolated offscreen buffer so the live scene isn't overdrawn by the preview? Current proposal: offscreen preview so the scene stays clean. **→ Signed off 2026-04-21.**

---

**Status:** Signed off 2026-04-21. Slices 12.1, 12.2, 12.3, 12.4, 12.5 all landed 2026-04-21.

Editor panel name is `UIRuntimePanel` (distinct from the existing
`UILayoutPanel` which covers canvas/theme inspection), exposed on the
editor as `Editor::getUIRuntimePanel()` and wired through
`Editor::setUISystem`. Slice 12.4's queue→widget reconciliation is
live via `UISystem::getNotifications()` + `UISystem::update`; pushing
a `Notification` into the queue will surface it on the live HUD.

Deferred from slice 12.5 (explicit follow-up):
- Offscreen FBO composite for the Menus-tab preview (needs editor
  viewport cooperation — the panel currently shows the preview
  canvas's element count as a structural readout).
