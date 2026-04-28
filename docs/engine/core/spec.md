# Subsystem Specification — `engine/core`

## Header

| Field | Value |
|-------|-------|
| Subsystem | `engine/core` |
| Status | `shipped` |
| Spec version | `1.0` |
| Last reviewed | `2026-04-28` (initial draft — pending cold-eyes review) |
| Owners | `milnet01` |
| Engine version range | `v0.1.0+` (foundation since Phase 1) |

---

## 1. Purpose

`engine/core` is the foundation layer every other subsystem stands on. It owns the `Engine` lifecycle (initialise → run → shutdown), the GLFW window + GL context, the variable-rate frame timer, the keyboard/mouse/gamepad input abstraction, the typed publish/subscribe `EventBus`, the `ISystem` / `SystemRegistry` machinery that drives every domain system in lock-step, the engine-wide `Logger`, and the persisted `Settings` chain (load → migrate → validate → apply via sinks → editor). It exists as its own subsystem because everything from the renderer to the audio mixer to the editor needs at least one of those primitives, and pushing them inward (e.g. into renderer or scene) would force unrelated subsystems to depend on each other through an unrelated path. For the engine's primary use case — first-person architectural walkthroughs of biblical structures — `engine/core` is what gets you from `int main()` to "the user is walking around the Tabernacle at 60 FPS".

## 2. Scope

| In scope | Out of scope |
|----------|--------------|
| `Engine` class — owns subsystems, runs main loop, init / shutdown order | Rendering passes, GL state machine — `engine/renderer/` |
| `Window` — GLFW window + GL context, fullscreen / vsync / video-mode toggles | Per-pixel work, framebuffer object orchestration — `engine/renderer/` |
| `Timer` — `steady_clock`-driven dt + FPS counter + optional frame-rate cap | Fixed-step physics integration — `engine/physics/` (Jolt `Update`) |
| `InputManager` — GLFW keyboard / mouse / gamepad polling + event publishing | Action-map data structures (`InputBinding`, `InputActionMap`) — `engine/input/` |
| `FirstPersonController` — WASD + mouse-look + gamepad camera controller, AABB / terrain collision | Physics character controller — `engine/physics/physics_character_controller.h` |
| `EventBus` + `Event` base + common event structs (`Window*Event`, `Key*Event`, `Mouse*Event`, `SceneLoadedEvent`, `WeatherChangedEvent`, …) | Domain-specific events that don't cross subsystems (those live with their owner) |
| `ISystem` interface + `UpdatePhase` enum + `SystemRegistry` (auto-activation, per-frame dispatch, metrics) | Concrete domain systems — `engine/systems/`, `engine/audio/`, `engine/ui/`, … |
| `Logger` — six-level engine logger, ring buffer for editor console, file output | Per-frame profiler markers — `engine/profiler/` |
| `Settings` — JSON load / save / migrate / validate, schema versioning | Renderer accessibility internals (color-vision filter, post-process toggles consume `Settings` via sinks but live in `engine/renderer/` and `engine/accessibility/`) |
| `settings_apply` — sink interfaces + production sinks for video / audio / accessibility / HRTF / subtitles / photosensitive / input | Concrete subsystem behaviour invoked by the sinks (the sinks are thin forwarders) |
| `SettingsEditor` — dirty-tracking state machine for live-apply + commit / revert / per-category restore | The ImGui panel that drives it — `engine/editor/` |
| `engine_paths` — asset-path composition helpers | Asset loading / caching — `engine/resource/` |

## 3. Architecture

```
                           ┌────────────────────────────────────┐
                           │             Engine                 │
                           │  (engine/core/engine.h:99)         │
                           └────────────┬───────────────────────┘
                                        │ owns
   ┌──────────┬──────────┬──────────────┼──────────────┬─────────────┬──────────┐
   ▼          ▼          ▼              ▼              ▼             ▼          ▼
┌──────┐  ┌───────┐  ┌────────┐  ┌──────────────┐  ┌────────┐  ┌─────────┐  ┌────────┐
│Window│  │Timer  │  │InputMgr│  │EventBus      │  │FirstPer│  │Settings │  │SystemRe│
└──┬───┘  └───────┘  └───┬────┘  │(typed pub/sub│  │Controlr│  │+Editor  │  │gistry  │
   │ GLFW                │       │ via std::    │  └────────┘  │+sinks   │  │ ┌─────┐│
   │ +GL ctx             │       │  function)   │              └────┬────┘  │ │ISys │ │
   ▼                     ▼       └──────┬───────┘                   │       │ │tem* │ │
 OS window         GLFW callbacks       │                           ▼       │ └─────┘ │
                                        ▼                       JSON disk   └─┬──────┘
                                  every subsystem                              │
                                  subscribes / publishes                       ▼
                                                                       per-frame
                                                                       updateAll()
```

Key abstractions:

| Abstraction | Type | Purpose |
|-------------|------|---------|
| `Engine` | class | Owns every subsystem; runs the main loop. `engine/core/engine.h:100` |
| `EngineConfig` | struct | Cold-start config (window, asset path, scene, demo flags). `engine/core/engine.h:54` |
| `Window` | class (RAII) | GLFW window + GL 4.5 context; fullscreen / vsync / video-mode toggles. `engine/core/window.h:27` |
| `Timer` | class | `steady_clock`-driven `dt` + FPS counter + optional frame cap. `engine/core/timer.h:18` |
| `InputManager` | class | GLFW callbacks → `EventBus` events; per-frame mouse delta; binding query. `engine/core/input_manager.h:21` |
| `FirstPersonController` | class | WASD / mouse / gamepad camera controller with AABB + terrain collision. `engine/core/first_person_controller.h:37` |
| `EventBus` | class | Type-indexed `std::function`-based pub/sub. `engine/core/event_bus.h:27` |
| `Event` | struct (base) | Polymorphic base for every typed event. `engine/core/event.h:12` |
| `ISystem` | interface | 4 pure virtuals + opt-in hooks for every domain system. `engine/core/i_system.h:78` |
| `UpdatePhase` | enum | `PreUpdate` / `Update` / `PostCamera` / `PostPhysics` / `Render` ordering tag. `engine/core/i_system.h:50` |
| `SystemRegistry` | class | Lifecycle + per-frame dispatch + auto-activation + per-system metrics. `engine/core/system_registry.h:56` |
| `Logger` | class (static) | Six-level engine logger + 1000-entry ring buffer + timestamped log file. `engine/core/logger.h:34` |
| `Settings` | struct | Persisted user settings root (display / audio / controls / gameplay / accessibility / onboarding). `engine/core/settings.h:300` |
| `validate(Settings&)` | free function | Clamp every field to its declared range; called by `fromJson`. `engine/core/settings.h:350` |
| `migrate()` | free function | Walks the v1→v2→… migration chain on a json tree. `engine/core/settings_migration.h:46` |
| `DisplayApplySink` etc. | abstract bases | Eight sink interfaces (display / audio / HRTF / UI a11y / renderer a11y / subtitle / photosensitive / input bindings). `engine/core/settings_apply.h:51..340` |
| `SettingsEditor` | class | Two-copy (`m_applied` / `m_pending`) dirty-tracker with live-apply and per-category restore. `engine/core/settings_editor.h:44` |
| `captionMapPath()` | free function | Compose `<assetPath>/captions.json`. `engine/core/engine_paths.h:29` |

## 4. Public API

The subsystem exposes a deliberately small facade. Headers below are the legitimate `#include` targets for downstream code (per CODING_STANDARDS §18); their semver is respected.

```cpp
/// Cold-start orchestration.
bool          Engine::initialize(const EngineConfig& config);
void          Engine::run();
void          Engine::shutdown();
EventBus&     Engine::getEventBus();                  // shared bus
SystemRegistry& Engine::getSystemRegistry();          // register systems pre-init
const std::string& Engine::getAssetPath() const;
// Plus shared-infrastructure accessors: getWindow / getCamera / getRenderer /
// getResourceManager / getSceneManager / getPhysicsWorld / getProfiler / etc.
```

```cpp
/// Frame-loop primitives.
float         Timer::update();                        // returns dt seconds (clamped to 0.25 s)
int           Timer::getFps() const;
void          Timer::setFrameRateCap(int fps);        // 0 = uncapped
void          Window::pollEvents();                   // static — drives GLFW callbacks
void          Window::swapBuffers();
bool          Window::shouldClose() const;
void          Window::setVideoMode(int w, int h, bool fullscreen, bool vsync);
```

```cpp
/// Input — see input_manager.h:21 for full surface.
bool          InputManager::isKeyDown(int glfwKey) const;
bool          InputManager::isMouseButtonDown(int glfwBtn) const;
glm::vec2     InputManager::getMouseDelta() const;
bool          InputManager::isBindingDown(const InputBinding& b) const;
bool          InputManager::isActionDown(const InputActionMap& m,
                                          const std::string& actionId) const;
```

```cpp
/// Event bus — typed pub/sub.
template<typename T> SubscriptionId EventBus::subscribe(std::function<void(const T&)>);
bool          EventBus::unsubscribe(SubscriptionId id);
template<typename T> void EventBus::publish(const T& event);
void          EventBus::clearAll();
```

```cpp
/// System registration — call before initialize().
template<typename T, typename... Args> T* SystemRegistry::registerSystem(Args&&... args);
template<typename T> T* SystemRegistry::getSystem();
bool          SystemRegistry::initializeAll(Engine&);
void          SystemRegistry::updateAll(float dt);
void          SystemRegistry::shutdownAll();
void          SystemRegistry::clear();
```

```cpp
/// Logger — process-global (header is the one allowed exception
/// to "no engine-wide singletons" per CODING_STANDARDS §22).
Logger::trace / debug / info / warning / error / fatal(const std::string&);
std::deque<LogEntry> Logger::getEntries();    // thread-safe snapshot
void          Logger::openLogFile(const std::string& dir);
void          Logger::closeLogFile();
```

```cpp
/// Settings — persistence + apply.
std::pair<Settings,LoadStatus> Settings::loadFromDisk(const std::filesystem::path&);
SaveStatus    Settings::saveAtomic(const std::filesystem::path&) const;
std::filesystem::path Settings::defaultPath();
bool          validate(Settings&);
void          applyDisplay(const DisplaySettings&, DisplayApplySink&);
void          applyAudio  (const AudioSettings&,   AudioApplySink&);
void          applyAudioHrtf(const AudioSettings&, AudioHrtfApplySink&);
void          applyUIAccessibility(const AccessibilitySettings&, UIAccessibilityApplySink&);
void          applyRendererAccessibility(const AccessibilitySettings&, RendererAccessibilityApplySink&);
void          applySubtitleSettings(const AccessibilitySettings&, SubtitleApplySink&);
void          applyPhotosensitiveSafety(const AccessibilitySettings&, PhotosensitiveApplySink&);
void          applyInputBindings(const std::vector<ActionBindingWire>&, InputActionMap&);
std::vector<ActionBindingWire> extractInputBindings(const InputActionMap&);
SaveStatus    SettingsEditor::apply(const std::filesystem::path&);
void          SettingsEditor::mutate(const std::function<void(Settings&)>&);
void          SettingsEditor::revert();
```

**Non-obvious contract details:**

- `Engine` is non-copyable, non-movable. Construction is cheap; `initialize()` is heavyweight (creates GLFW + GL + every subsystem). `shutdown()` is idempotent and called by `~Engine`.
- `EventBus::publish<T>` is **synchronous** — every subscriber runs on the publishing thread before `publish` returns. The dispatch loop copies the listener list so a callback may freely subscribe / unsubscribe without invalidating iteration.
- `SystemRegistry::registerSystem<T>` must be called **before** `initializeAll()`. After init the registry is sealed (sort + init-prefix-rollback contract — `engine/core/system_registry.cpp:18,42,63`).
- `Timer::update()` clamps `dt` to **0.25 s** to defend against breakpoint pauses and the "spiral of death" (`engine/core/timer.cpp:50`).
- `Window` uses a **static instance pointer** (`s_instance`) for the framebuffer-resize callback because GLFW's per-window user pointer is owned by `InputManager`. Only one `Window` exists per process.
- `Logger` is intentionally `static` — accessed without an instance, mutex-guarded, safe to call from worker threads (`engine/core/logger.cpp:32`).
- `Settings::loadFromDisk` returns defaults on every failure mode and writes a `<path>.corrupt` sidecar for `ParseError` so the user can recover by hand.
- `applyInputBindings` drops unknown action ids with a warning; actions registered on the map but absent from the wire keep their current bindings (forward + backward compat).

**Stability:** the facade above is semver-frozen for `v0.x`. Two known evolution points: (a) `applyDisplay` does not yet propagate the quality preset / render scale (intentional — flagged in `engine/core/settings_apply.h:84`); (b) the wire `scancode` field currently stores GLFW key codes pending a layout-preserving scancode pass (flagged in `engine/core/settings_apply.h:351`). Both are additive when they land.

## 5. Data Flow

**Steady-state per-frame (`Engine::run` — `engine/core/engine.cpp:964`):**

1. `Window::shouldClose()` → if true, route through editor's FileMenu (unsaved-changes guard) before flipping `m_isRunning`.
2. `Timer::update()` → `dt` (clamped to 0.25 s).
3. `Window::pollEvents()` → drains GLFW queue; GLFW callbacks fire on the main thread, calling into `InputManager`'s static handlers, which `EventBus::publish` typed events synchronously to every subscriber.
4. `InputManager::update()` → resets the per-frame mouse / scroll deltas (cursor-position deltas were accumulated by callbacks during step 3).
5. Editor frame prep + viewport resize.
6. `FirstPersonController::update(dt, colliders)` (or `processLookOnly` when the physics character controller owns translation) → reads `InputManager` state + computes desired velocity → applies AABB / terrain / slope collision → writes `Camera`.
7. `SystemRegistry::updateAll(dt)` → walks systems in `UpdatePhase` order, measures each one with `steady_clock`, logs an over-budget warning when `m_lastUpdateTimeMs > m_frameBudgetMs`.
8. `SystemRegistry::submitRenderDataAll(renderData)` → systems push their drawables.
9. Renderer renders.
10. `Window::swapBuffers()`.
11. `Timer::waitForFrameCap()` — hybrid sleep-then-spin, only when a cap is set.

**Cold start (`Engine::initialize` — `engine/core/engine.cpp:72`):**

1. `Logger::openLogFile("logs")` → timestamped file `logs/vestige_YYYYMMDD_HHMMSS.log`.
2. Create `Window` (GLFW init + GL 4.5 context creation).
3. Create `Timer`, `InputManager` (claims GLFW user-pointer + callbacks), `Renderer` (loads shaders, allocates FBOs).
4. Create `ResourceManager`, `SceneManager`, `Camera`, `FirstPersonController`, `Editor`, `DebugDraw`.
5. Initialise `PhysicsWorld` and `PerformanceProfiler`.
6. Register every domain `ISystem` (atmosphere, particles, water, vegetation, terrain, cloth, destruction, character, lighting, audio, UI, navigation, sprite, physics2d).
7. Load `Settings` from `Settings::defaultPath()` → run migration chain → validate → apply via every configured sink (display / audio / HRTF / UI a11y / renderer a11y / subtitles / photosensitive / input).
8. Build apply-sinks (`WindowDisplaySink`, `AudioMixerApplySink`, …) and the `SettingsEditor`.
9. `SystemRegistry::initializeAll(*this)` → stable-sort by `UpdatePhase`, init in order, roll back the prefix in reverse on the first failure (`engine/core/system_registry.cpp:42`).
10. Load startup scene (built-in demo / Tabernacle / `--scene` CLI override).

**Shutdown (`Engine::shutdown` — `engine/core/engine.cpp:1704`):**

1. Save window state (position + size).
2. `SystemRegistry::shutdownAll()` then `clear()` — explicitly destroys system instances **while** GL / Window / Renderer are still alive (AUDIT §H17 — `engine/core/system_registry.cpp:100`).
3. Reset engine-owned `unique_ptr` members in reverse construction order.
4. `EventBus::clearAll()`.
5. `Logger::closeLogFile()`.

**Exception path:** `initialize()` returns `false` if shaders fail to load, the system-init prefix rolls back any partially-init systems before returning, and `~Engine` calls `shutdown()` defensively. No exceptions propagate out of `engine/core` in steady-state.

## 6. CPU / GPU placement

| Workload | Placement | Reason |
|----------|-----------|--------|
| Frame-loop orchestration, timer, event dispatch, system iteration | CPU (main thread) | Branching, sparse, decision-heavy — exactly the CODING_STANDARDS §17 default for CPU work. |
| Settings load / migrate / validate / save | CPU (main thread, init / apply only) | I/O + JSON parse — never per-frame. |
| Input polling (`isKeyDown`, gamepad scan) | CPU (main thread) | Decision-heavy; GLFW APIs are CPU-only. |
| Window / GL context creation | CPU (main thread) | OS + driver call; GL context affinity is single-thread. |
| First-person controller collision (AABB + terrain) | CPU (main thread) | Sparse, branching; per-frame O(colliders) is small (< 100 boxes in demo scenes). |

The only GPU touch in `engine/core` is **`Window` creating + swapping the default framebuffer**. All actual GPU work (passes, draws, compute) lives in `engine/renderer/` and the domain systems. No dual implementation needed; no GPU spec / CPU runtime split applies.

## 7. Threading model

Per CODING_STANDARDS §13 — every subsystem must answer "which threads enter this code, which locks do they hold."

| Caller thread | Allowed APIs | Locks held |
|---------------|--------------|------------|
| **Main thread** (the only thread that runs `Engine::run`) | All of `Engine`, `Window`, `Timer`, `InputManager`, `FirstPersonController`, `EventBus`, `SystemRegistry`, `Settings*`. GL context affinity lives here. | None — main thread is single-threaded by contract. |
| **Worker threads** (job system, audio thread, `AsyncTextureLoader`) | `Logger::trace/debug/info/warning/error/fatal`, `Logger::getEntries`, `Logger::clearEntries`. | `s_logMutex` (internal — `engine/core/logger.cpp:33`). |

**Main-thread-only:** `Engine`, `Window`, `Timer`, `InputManager`, `FirstPersonController`, `EventBus`, `SystemRegistry`, `Settings*`. Calling these from a worker is undefined — `EventBus::publish` is not synchronised; GLFW input/window APIs require the main thread per the GLFW manual.

**Lock-free / atomic:** none required. The sole shared state in `engine/core` between threads is `Logger`'s console + ring buffer + file stream, all guarded by a single `std::mutex` (`engine/core/logger.cpp:33`). `Logger::getEntries()` returns the deque **by value** so callers iterate a stable snapshot while workers may still be writing (`engine/core/logger.h:70`).

**Worker pool ownership:** `engine/core` does **not** own any worker pool. `AsyncTextureLoader` (in `engine/resource/`) and the audio thread (`engine/audio/`) bring their own; they only call into `engine/core` via `Logger`.

## 8. Performance budget

60 FPS hard requirement → 16.6 ms per frame. `engine/core` is overhead-only — every millisecond it consumes is a millisecond the renderer / physics / domain systems don't have. Budgets are tentative pending instrumentation.

| Path | Budget | Measured (RX 6600, 1080p) |
|------|--------|----------------------------|
| `Timer::update` | < 0.01 ms | TBD — to be measured before next audit (low-risk: pure `steady_clock` arithmetic) |
| `Window::pollEvents` | < 0.5 ms | TBD — to be measured before next audit (drives every GLFW callback synchronously) |
| `InputManager::update` (deltas-only) | < 0.01 ms | TBD — to be measured before next audit |
| `EventBus::publish<T>` (1–5 listeners) | < 0.05 ms | TBD — to be measured before next audit |
| `FirstPersonController::update` (≤ 64 AABB colliders) | < 0.2 ms | TBD — to be measured before next audit |
| `SystemRegistry::updateAll` overhead (registry-level loop, excluding system bodies) | < 0.1 ms | TBD — to be measured before next audit |
| One-shot `Engine::initialize` (cold start, demo scene) | < 1500 ms | TBD — to be measured before next audit |
| `Settings::loadFromDisk` + apply chain | < 50 ms | TBD — to be measured before next audit |

Profiler markers / capture points (per `engine/profiler/performance_profiler.h`): the registry per-system timers populate the `SystemMetrics` table; over-budget systems emit a `Logger::warning` line containing the system name (greppable in capture logs). `engine/core` itself does not emit `glPushDebugGroup` markers — it has no GPU passes.

## 9. Memory

| Aspect | Value |
|--------|-------|
| Allocation pattern | Heap (mostly `std::unique_ptr` from `Engine::initialize`); a few stack-local containers in the per-frame loop. No arena, no per-frame transient allocator. |
| Reusable per-frame buffers | `m_renderData` (engine-owned `SceneRenderData`), `m_colliders` (`std::vector<AABB>`), `m_scratchVisibleChunks` — all live across frames so the hot path doesn't heap-alloc (audit-driven; `engine/core/engine.h:230,250`). |
| Peak working set | Negligible vs. renderer / scene / textures: low single-digit MB for `engine/core` itself (Logger 1000-entry ring buffer ≈ ~64 KB; `Settings` JSON parse ≈ ~32 KB; per-system `unique_ptr<ISystem>` table; cached domain pointers). |
| Ownership | `Engine` owns every subsystem via `std::unique_ptr`. Domain `ISystem` instances owned by `SystemRegistry::m_systems`. `EventBus` listener entries owned by the bus. `Logger` static state owned by the process. |
| Lifetimes | Engine-lifetime — every `engine/core` allocation lives from `Engine::initialize` until `Engine::shutdown`. Reusable per-frame buffers retain capacity across frames. |

No `new`/`delete` in feature code (CODING_STANDARDS §12). The legitimate exceptions are GLFW + Jolt internal allocations behind their respective handles (RAII-wrapped).

## 10. Error handling

Per CODING_STANDARDS §11 — no exceptions in steady-state hot paths.

| Failure mode | Reported as | Caller's recourse |
|--------------|-------------|-------------------|
| Shader load failed in `Engine::initialize` | `Logger::fatal` + `return false` | App aborts; no recovery — engine cannot run without shaders. |
| Editor / DebugDraw / PhysicsWorld init failed | `Logger::warning` + soft-disable that subsystem | Engine continues; affected feature unavailable. |
| One `ISystem::initialize` returned false | `SystemRegistry::initializeAll` rolls back the init prefix in reverse, returns false. | Caller (Engine) decides to abort — currently logs and continues without the failing system; planned to abort cold-start (Phase 11 follow-on). |
| `Settings` JSON malformed | `LoadStatus::ParseError`, defaults returned, original moved to `<path>.corrupt` | Engine logs, continues with defaults. User can hand-edit recovery. |
| `Settings` save failed (disk full, permission) | `SaveStatus::WriteError` (atomic-write didn't commit; old file intact) | Editor surfaces "save failed" to user; `m_applied` does not advance, `isDirty()` stays true. |
| Migration unknown future version | `migrate()` returns false → `Settings` defaults | Logger warning; engine continues. |
| Subscriber callback throws | Propagates to publisher | **Bug** — callbacks must not throw inside `EventBus::publish` (the bus has no try/catch wrapper). Treat as programmer error, fix the callback. |
| Programmer error (null pointer, index OOB) | `assert` (debug) / UB (release) | Fix the caller. |
| Out of memory | `std::bad_alloc` propagates | App aborts (matches CODING_STANDARDS §11 — OOM is treated as fatal during init; steady-state allocations are bounded). |

`Result<T,E>` / `std::expected` not yet used in `engine/core` (the codebase pre-dates the introduction). Migration is on the broader engine-wide list, not a `engine/core`-specific debt.

## 11. Testing

| Concern | Test file | Coverage |
|---------|-----------|----------|
| `EventBus` subscribe / publish / unsubscribe / re-entrant unsubscribe | `tests/test_event_bus.cpp` | Public API contract |
| `Timer` dt clamp + FPS counter + `steady_clock` independence from GLFW | `tests/test_timer.cpp` | Public API contract |
| `Logger` thread-safety + ring-buffer eviction + file output | `tests/test_logger.cpp` | Public API contract + concurrency |
| `Settings` round-trip JSON + clamp + `LoadStatus` semantics + `.corrupt` sidecar + onboarding promotion | `tests/test_settings.cpp` | Schema, validation, atomic write |
| `SystemRegistry` registration / phase sort / init-prefix rollback / metrics | `tests/test_system_registry.cpp` | Lifecycle + dispatch ordering |
| `engine_paths::captionMapPath` | `tests/test_engine_paths.cpp` | Path composition + trailing-slash tolerance |
| Input bindings extract / apply / unknown-id drop | `tests/test_input_bindings.cpp` | Wire round-trip + forward-compat |
| `UISystem` input routing (covers Engine → InputManager → EventBus → UI plumbing) | `tests/test_ui_system_input.cpp` | Smoke / integration |

**Adding a test for `engine/core`:** drop a new `tests/test_<thing>.cpp` next to its peers, link against `vestige_engine` + `gtest_main` in `tests/CMakeLists.txt`, use `EventBus`/`Settings`/etc. directly without an `Engine` instance — every `engine/core` primitive except `Window` and `Engine` is unit-testable headlessly. `Window` and the full `Engine::run` loop need a display server; those are exercised via the visual-test harness (`engine/testing/visual_test_runner.h`).

**Coverage gap:** `Engine::initialize` / `run` / `shutdown` are not covered by a fully isolated unit test (the GLFW + GL dependency makes a headless smoke test brittle in CI). Coverage comes through the visual-test runner and through every other `tests/test_*` that links the engine library.

## 12. Accessibility

`engine/core` itself produces no user-facing pixels or sound. **However**, it is the *route* every accessibility surface flows through:

- `Settings::accessibility` carries the persisted state (UI scale, high contrast, reduced motion, subtitles, color-vision filter, post-process toggles, photosensitive caps).
- The eight apply-sinks in `settings_apply.h` are the sole writeable path from "user toggled a checkbox" to "subsystem behaves differently" — UI scale → `UIAccessibilityApplySink`, color-vision filter + DoF/motion-blur/fog → `RendererAccessibilityApplySink`, captions → `SubtitleQueueApplySink`, photosensitive caps → `PhotosensitiveStoreApplySink`.
- `EventBus` carries `KeyPressedEvent::mods` (added Phase 10.9 Slice 3) so keyboard-focus handlers can distinguish `Tab` from `Shift+Tab` without re-querying GLFW (`engine/core/event.h:39`).
- `FirstPersonController` exposes `mouseSensitivity`, `gamepadDeadzone`, `gamepadLookSensitivity`, sprint-multiplier — all rebindable via `Settings::controls`. No motion-blur or screen-shake originates here; `reducedMotion` flows through the renderer / camera-shake consumers via the photosensitive sinks.
- `Logger` ring buffer feeds the editor's console panel — the `LogLevel` enum is the only colour-conveyed signal, and the panel must back colour with text labels (`TRACE` / `INFO` / `WARN` / …) per the partially-sighted-user constraint (project memory).

Constraint summary for downstream UIs that consume `engine/core`:

- Settings UI must surface every accessibility toggle on the Accessibility tab; defaults must match the struct initialisers in `settings.h` (`reducedMotion = false` etc. — sensible "no surprises on first launch").
- Input — every `InputBinding` must be rebindable via `SettingsEditor`; gamepad + keyboard parity is non-negotiable.
- Photosensitive defaults (`PhotosensitiveSafetyWire` in `engine/core/settings.h:210`) are conservative — `maxFlashAlpha = 0.25`, `maxStrobeHz = 2.0`, `bloomIntensityScale = 0.6` — and must remain so absent an explicit design-doc revision.

## 13. Dependencies

| Dependency | Type | Why |
|------------|------|-----|
| `engine/input/input_bindings.h` | engine subsystem | `InputBinding` / `InputActionMap` types consumed by `InputManager` + `Settings`. |
| `engine/input/input_bindings_wire.h` | engine subsystem | Wire format for persisted bindings. |
| `engine/utils/config_path.h` | engine subsystem | Cross-platform user-config directory resolution (XDG / `%LOCALAPPDATA%`). |
| `engine/utils/atomic_write.h` | engine subsystem | Crash-safe settings save. |
| `engine/utils/aabb.h` | engine subsystem | First-person controller collision primitive. |
| `engine/audio/audio_mixer.h`, `audio_engine.h`, `audio_hrtf.h` | engine subsystem | Apply-sink targets — header-only forwarders; `engine/core` does not touch audio output directly. |
| `engine/accessibility/photosensitive_safety.h`, `post_process_accessibility.h` | engine subsystem | Apply-sink target structs. |
| `engine/renderer/color_vision_filter.h`, `camera.h` | engine subsystem | Apply-sink target enum + camera the FPC drives. (FPC ↔ Camera is the one bidirectional dependency: `engine/renderer/camera.h` does **not** include core, `engine/core/first_person_controller.h` does include camera.) |
| `engine/ui/ui_theme.h`, `subtitle.h`, `caption_map.h` | engine subsystem | Apply-sink targets. |
| `engine/scene/scene.h`, `entity.h` | engine subsystem | `SystemRegistry::activateSystemsForScene` walks scene component types. |
| `engine/profiler/performance_profiler.h` | engine subsystem | `ISystem::reportMetrics` + per-frame timing. |
| `<glm/glm.hpp>` | external | Math primitives (`vec2`, `vec3`). |
| `<GLFW/glfw3.h>` | external | Window + input + GL context. |
| `<nlohmann/json.hpp>` (+ `json_fwd.hpp`) | external | Settings persistence. |
| `<chrono>`, `<filesystem>`, `<deque>`, `<typeindex>`, `<functional>`, `<mutex>` | std | Timer, atomic file ops, ring buffer, type-indexed maps, callbacks, log mutex. |

**Direction:** `engine/core` is depended on by virtually every other subsystem (renderer, scene, physics, audio, UI, editor, …). `engine/core` itself depends on `engine/input`, `engine/utils`, and the apply-target headers above; it must **not** depend on `engine/renderer/renderer.h`, `engine/scene/scene_manager.h`, or any concrete `ISystem`. The cached pointers in `Engine` (e.g. `m_terrain`, `m_uiSystem`) are forward-declared in `engine.h` and resolved at registration time precisely to keep the include graph one-way.

## 14. References

Cited research / authoritative external sources:

- André Leite. *Taming Time in Game Engines: Fixed Timestep Game Loop* (2025) — modern accumulator pattern, dt clamp rationale, "spiral of death." <https://andreleite.com/posts/2025/game-loop/fixed-timestep-game-loop/>
- Glenn Fiedler. *Fix Your Timestep!* — canonical decoupled-update reference, source of the dt-clamp + accumulator pattern. <https://gafferongames.com/post/fix_your_timestep/>
- Robert Nystrom. *Game Programming Patterns — Game Loop.* — the variable-render / fixed-update / hybrid taxonomy this engine uses. <https://gameprogrammingpatterns.com/game-loop.html>
- Sander Mertens. *ECS FAQ* (2024–2025, ongoing) — ECS-vs-component-driven discussion that informs the `ISystem` + `SystemRegistry` middle-ground (named-system table over storage-driven ECS). <https://github.com/SanderMertens/ecs-faq>
- skypjack. *EnTT — fast and reliable ECS for modern C++* (2025) — reference implementation for type-indexed per-system lookup. <https://github.com/skypjack/entt>
- Voxagon. *Thoughts on ECS* (2025-03-28) — current-state critique informing why this engine kept a registry-of-systems pattern instead of full archetype storage. <https://blog.voxagon.se/2025/03/28/thoughts-on-ecs.html>
- mmcshane. *EventBus — threadsafe C++ implementation of the EventBus idiom.* — reference for the type-erased `std::function` listener model used by `EventBus`. <https://github.com/mmcshane/eventbus>
- O'Reilly / Praseed Pai. *C++ Reactive Programming — The Event Bus Pattern.* — pub/sub design rationale (type-erasure trade-offs). <https://www.oreilly.com/library/view/c-reactive-programming/9781788629775/4d5f576b-cf55-4106-ab4f-bde3a623b2a1.xhtml>
- GLFW Project. *Input Guide* (3.3 / latest) — gamepad mapping (SDL_GameControllerDB), `glfwUpdateGamepadMappings`, callback vs. polling. <https://www.glfw.org/docs/3.3/input_guide.html>
- ISO C++ Core Guidelines, *Concurrency* (CP.20–CP.43) — threading conventions referenced from CODING_STANDARDS §13. <https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#S-concurrency>
- SPDX 2.3, REUSE 3.0 — license-header conventions (CODING_STANDARDS §28).

Internal cross-references:

- `CODING_STANDARDS.md` §11 (errors), §12 (memory), §13 (threading), §14 (logging), §17 (CPU/GPU), §18 (public API), §22 (DI / globals), §27 (units), §32 (asset paths).
- `ARCHITECTURE.md` §1–6 (subsystem map, engine loop, event bus, scene graph).
- `CLAUDE.md` rules 1, 5, 7 (research-first, library currency, CPU/GPU placement).

## 15. Open questions

| # | Question | Owner | Target |
|---|----------|-------|--------|
| 1 | Should `Engine::initialize` abort cold-start when an `ISystem::initialize` returns false (current behaviour: log + continue)? | unassigned | Phase 11 |
| 2 | `applyDisplay` does not yet propagate `qualityPreset` / `renderScale` (flagged in `settings_apply.h:84`). Pending Renderer-side hook. | unassigned | Phase 11 |
| 3 | Wire-format `scancode` field stores GLFW key codes, not true scancodes — layout-preserving rebind (WASD on AZERTY) requires `glfwGetKeyScancode` + reverse lookup (flagged in `settings_apply.h:351`). | unassigned | Phase 11 |
| 4 | No `Result<T, E>` / `std::expected` adoption yet — `LoadStatus` / `SaveStatus` enums + bool returns predate the codebase-wide policy. Migration on the broader debt list. | unassigned | post-MIT release |
| 5 | `EventBus::publish` has no exception-safety wrapper — a throwing callback escapes the publisher. Defer until a real use case demands it; current policy is "callbacks must not throw." | unassigned | unscheduled |
| 6 | Performance budgets in §8 are placeholders. Need a one-shot Tracy / RenderDoc capture to fill in measured numbers. | unassigned | Phase 11 audit |

## 16. Spec change log

| Date | Spec version | Author | Change |
|------|--------------|--------|--------|
| 2026-04-28 | 1.0 | milnet01 | Initial spec — `engine/core` foundation since Phase 1, formalised post-Phase 10.9 audit. |
