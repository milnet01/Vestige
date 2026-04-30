# Subsystem Specification — `engine/input`

## Header

| Field | Value |
|-------|-------|
| Subsystem | `engine/input` |
| Status | `shipped` |
| Spec version | `1.0` |
| Last reviewed | `2026-04-28` (initial draft — pending cold-eyes review) |
| Owners | `milnet01` |
| Engine version range | `v0.1.0+` (action-map data model since Phase 10 accessibility; wire helpers relocated here Phase 10.9 Slice 9 I2) |

---

## 1. Purpose

`engine/input` owns the engine's *rebindable-controls data model* — the action map, the per-action three-slot binding table (primary keyboard / mouse, secondary keyboard / mouse, gamepad), the JSON wire format that persists user rebinds across launches, and the human-readable display labels the rebind UI shows in its "bound to" column. It is deliberately a **pure data + free-function** subsystem — no GLFW polling, no per-frame state, no GL context — so every primitive is unit-testable headlessly. The polling shim (`InputManager`) lives in `engine/core` and consumes these types; `engine/input` is the half a Game Engine for biblical architectural walkthroughs needs to load, edit, persist, and route user rebinds without dragging a GLFW (Graphics Library Framework) handle through every test. Splitting it out lets `engine/core` depend on `engine/input` without `engine/input` depending on the GLFW + window stack — a one-way include direction and a strict accessibility contract (every binding rebindable, gamepad + keyboard parity) made the boundary worth its own subsystem.

## 2. Scope

| In scope | Out of scope |
|----------|--------------|
| `InputBinding` — `(InputDevice, code)` value type | GLFW (Graphics Library Framework) callback wiring — `engine/core/input_manager.h` |
| `InputAction` — id + label + category + three binding slots | Per-frame mouse delta accumulation — `engine/core/input_manager.h` |
| `InputActionMap` — registry + reverse lookup + conflict detection + reset-to-defaults | First-Person Controller (FPC) movement code that *consumes* actions — `engine/core/first_person_controller.h` |
| `bindingDisplayLabel()` — GLFW-code → human string for the rebind UI | Localisation of those strings (Phase 10 Localization owns the string table) |
| `isActionDown()` — pure-function query with dependency-injected binding checker | Polling GLFW / GLFW gamepad state — `InputManager::isBindingDown` |
| `InputBindingWire` / `ActionBindingWire` — JSON Document Object Notation (JSON) wire structs | Settings orchestration around them — `engine/core/settings.h` (`ControlsSettings::bindings`) |
| Pure binding ↔ JSON helpers (`bindingToJson`, `bindingFromJson`, `actionBindingToJson`, `actionBindingFromJson`) | Wire ↔ in-memory translation (`extractInputBindings` / `applyInputBindings`) — `engine/core/settings_apply.cpp` |
| Phantom-id validation policy (drop with logged warning) | Schema migration / `Settings` version chain — `engine/core/settings_migration.h` |
| Same-device-only conflict detection (Phase 10.9 Slice 9 I4) | Editor rebind panel ImGui — `engine/editor/panels/settings_editor_panel.cpp` |

## 3. Architecture

```
                  game code                                rebind UI
              isActionDown("Jump")                  setPrimary("Jump", key(F))
                       │                                       │
                       ▼                                       ▼
        ┌─────────────────────────────────────────────────────────────┐
        │                       InputActionMap                        │
        │  m_actions   (live bindings — what game code queries)       │
        │  m_defaults  (parallel snapshot — resetToDefaults source)   │
        │      ▲                                                      │
        │      │ addAction                                            │
        └──────┼──────────────────────────────────────────────────────┘
               │                                  ▲             ▲
        InputAction { id, label,                  │             │
            primary,secondary,gamepad }           │             │
               │                                  │             │
               ▼                                  │             │
        InputBinding { InputDevice, int code }    │             │
                                                  │             │
   wire round-trip                                │             │
   ─────────────────                              │             │
   ActionBindingWire { id, primary,               │             │
       secondary, gamepad: InputBindingWire }     │             │
                ▲                                 │             │
   actionBindingFromJson / actionBindingToJson    │             │
                                                  │             │
   bindingFromJson / bindingToJson                │             │
                                                  │             │
   (the ↔ in-memory translation lives             │             │
    in settings_apply.cpp, not here:              │             │
    extractInputBindings  ───────────────────────┘             │
    applyInputBindings    ─────────────────────────────────────┘
```

Key abstractions:

| Abstraction | Type | Purpose |
|-------------|------|---------|
| `InputDevice` | enum class | `None` / `Keyboard` / `Mouse` / `Gamepad`. `engine/input/input_bindings.h:38` |
| `InputBinding` | struct | `(device, code)` pair + `isBound` + factory helpers (`key`, `mouse`, `gamepad`, `none`). `engine/input/input_bindings.h:52` |
| `InputAction` | struct | id + label + category + three binding slots + `matches(binding)`. `engine/input/input_bindings.h:80` |
| `InputActionMap` | class | Live + defaults registry; rebind, reverse lookup, conflict detection, reset. `engine/input/input_bindings.h:99` |
| `bindingDisplayLabel()` | free function | GLFW code → "W" / "Space" / "Left Mouse" / "LB" / "—". `engine/input/input_bindings.h:166` |
| `isActionDown()` | free function | Pure query — caller injects the polling predicate. `engine/input/input_bindings.h:174` |
| `InputBindingWire` | struct | JSON-shape `(device:string, scancode:int)`. `engine/input/input_bindings_wire.h:35` |
| `ActionBindingWire` | struct | JSON-shape `(id, primary, secondary, gamepad)`. `engine/input/input_bindings_wire.h:50` |
| `bindingToJson` / `bindingFromJson` | free functions | Symmetric round-trip; missing fields take wire defaults. `engine/input/input_bindings_wire.h:69` |
| `actionBindingToJson` / `actionBindingFromJson` | free functions | Same shape at action level. `engine/input/input_bindings_wire.h:74` |

## 4. Public API

Small surface — two public headers, both legitimately included by downstream code (per CODING_STANDARDS §18):

```cpp
// engine/input/input_bindings.h — runtime data model
enum class InputDevice { None, Keyboard, Mouse, Gamepad };

struct InputBinding {
    InputDevice device;
    int         code;
    bool isBound() const;
    bool operator==(const InputBinding&) const;
    static InputBinding key(int glfwKey);
    static InputBinding mouse(int glfwButton);
    static InputBinding gamepad(int glfwButton);
    static InputBinding none();
};

struct InputAction {
    std::string id, label, category;
    InputBinding primary, secondary, gamepad;
    bool matches(const InputBinding&) const;
};

class InputActionMap {
    InputAction&            addAction(const InputAction&);
    const std::vector<InputAction>& actions() const;
    InputAction*            findAction(const std::string& id);
    const InputAction*      findActionBoundTo(const InputBinding&) const;
    std::vector<std::string> findConflicts(const InputBinding&,
                                           const std::string& excludeActionId = {}) const;
    bool setPrimary  (const std::string& id, const InputBinding&);
    bool setSecondary(const std::string& id, const InputBinding&);
    bool setGamepad  (const std::string& id, const InputBinding&);
    bool clearSlot   (const std::string& id, int slotIndex);
    void resetToDefaults();
    bool resetActionToDefaults(const std::string& id);
};

std::string bindingDisplayLabel(const InputBinding&);
bool        isActionDown(const InputActionMap&,
                         const std::string& actionId,
                         const std::function<bool(const InputBinding&)>& isBindingDown);
```

```cpp
// engine/input/input_bindings_wire.h — JSON wire format
struct InputBindingWire { std::string device = "none"; int scancode = -1; };
struct ActionBindingWire {
    std::string      id;
    InputBindingWire primary, secondary, gamepad;
};

nlohmann::json    bindingToJson      (const InputBindingWire&);
InputBindingWire  bindingFromJson    (const nlohmann::json&);
nlohmann::json    actionBindingToJson(const ActionBindingWire&);
ActionBindingWire actionBindingFromJson(const nlohmann::json&);
```

**Non-obvious contract details:**

- `InputBinding::operator==` compares **both** device and code — keyboard scancode 32 and mouse button 32 are not equal. `findConflicts` additionally gates on `device` first (Phase 10.9 Slice 9 I4) so a future change to `operator==` that drops the device field can't silently re-introduce cross-device false positives.
- `InputActionMap::addAction` is **idempotent for first registration but warns on re-registration with divergent live bindings** (Audit I5 — `engine/input/input_bindings.cpp:40`). The silent-nuke flow it guards against: game code calls `addAction(default)` *after* `Settings::load` has already applied user rebinds; without the warning the user's keyboard layout would silently revert to defaults. Game code should register every action **before** `Settings::load`. Re-registering with identical bindings (editor hot-reload) is silent.
- `addAction` also overwrites the parallel `m_defaults` snapshot. This is the documented behaviour for hot-reload — if a future engine build ships new defaults, a re-registration *is* the upgrade path.
- `isActionDown` accepts a null `std::function` and returns `false` cleanly, so a caller can pass `InputManager::isBindingDown` even when the bus is not yet wired up (defensive — covered by `IsActionDown.HandlesNullBindingCheckerGracefully`).
- `bindingDisplayLabel` returns the U+2014 em-dash ("—") for unbound bindings, and a `"Key <n>"` debug fallback for codes outside the curated table — the fallback is intentional for engineering visibility, not localised.
- `bindingFromJson` clamps `scancode` to `-1` whenever `device == "none"` even if the JSON carried a stale scancode, so a deserialised "none" binding compares equal to `InputBinding::none()` regardless of file contents.
- `actionBindingFromJson` tolerates missing slot fields — older `settings.json` files lacking `secondary` / `gamepad` round-trip cleanly with default-constructed `InputBindingWire{}`.
- `findActionBoundTo` is a forward linear scan and returns the **first** match; with three slots × ≤ 64 actions the cost is negligible. Conflict detection via `findConflicts` walks the same vector and is also O(actions).

**Stability:** the surface above is semver-frozen for `v0.x`. One known evolution point is flagged in §15: the wire `scancode` field currently stores GLFW *key codes*, not true scancodes — layout-preserving WASD-on-AZERTY (Azerty French layout) rebind requires `glfwGetKeyScancode` + a reverse lookup and is the same Open Question as `engine/core` Open Q3. The wire shape itself does **not** need to change for that fix.

## 5. Data Flow

This subsystem has no per-frame state — flow is "data shapes plus the routes through them."

**Game-code query path (per-frame, called by FPC and gameplay systems):**

1. Caller → `InputManager::isActionDown(map, "Jump")` (lives in `engine/core`).
2. `InputManager` → `isActionDown(map, "Jump", [this](const InputBinding& b){ return isBindingDown(b); })` — pure free function in `engine/input`.
3. Free function reads `map.findAction("Jump")`, walks the three slots, calls the injected predicate per bound slot, returns true on first hit.
4. No allocation; no GLFW touch from `engine/input` itself — the predicate does the GLFW poll.

**Cold-load path (once per launch, inside `Settings::load`):**

1. `engine/core` registers every game action on `InputActionMap` via `addAction` *before* loading settings (init-order contract — see §10).
2. JSON parsed into `Settings::controls.bindings: std::vector<ActionBindingWire>` via `actionBindingFromJson` (this subsystem) called from `Settings`'s `fromJson`.
3. `applyInputBindings(wires, map)` (`engine/core/settings_apply.cpp:335`) walks `wires`, looks up each id in `map`, drops unknown ids with a logged warning, writes the three slots in place.

**Save path (on `SettingsEditor::apply`):**

1. `extractInputBindings(map)` (`engine/core/settings_apply.cpp:318`) walks `map.actions()`, produces `std::vector<ActionBindingWire>` in registration order.
2. `Settings::toJson` calls `actionBindingToJson` (this subsystem) for each entry.
3. JSON is written atomically by `Settings::saveAtomic`.

**Rebind UI path (interactive, while the editor's Settings panel is open):**

1. User clicks the "[bound to]" cell — panel reads `bindingDisplayLabel(action.primary)` etc. for display.
2. User presses a key — panel constructs `InputBinding::key(glfwCode)` (or `mouse` / `gamepad`).
3. Panel calls `map.findConflicts(captured, currentActionId)` — surfaces colliding action ids in the warning pill (same-device only).
4. Panel calls `map.setPrimary(id, captured)` (or `setSecondary` / `setGamepad`).
5. `SettingsEditor` schedules an apply pass; on commit, `extractInputBindings` runs and the pending state is persisted.

**Reset-to-defaults paths:**

- Per-action: `map.resetActionToDefaults(id)` — restores from the `m_defaults` snapshot.
- Whole map: `map.resetToDefaults()` — `m_actions = m_defaults`.

No exception path beyond the file's standard "JSON shape is wrong" handling — `bindingFromJson` falls back to defaults, `actionBindingFromJson` skips missing fields, `applyInputBindings` drops unknown ids.

## 6. CPU / GPU placement

Not applicable — pure CPU subsystem (Pattern A per the spec template). `engine/input` has no GLFW handle, no GL (Graphics Library) call, no shader, no buffer. Decision-heavy, sparse, branching; default CPU placement per CODING_STANDARDS §17.

## 7. Threading model

| Caller thread | Allowed APIs | Locks held |
|---------------|--------------|------------|
| Main thread | All of `InputBinding`, `InputAction`, `InputActionMap`, `bindingDisplayLabel`, `isActionDown`, `bindingToJson`/`fromJson`, `actionBindingToJson`/`fromJson`. | None. |

**Main-thread-only.** `InputActionMap` is a plain `std::vector` of structs with no internal synchronisation; concurrent mutate from a worker would race the editor's rebind UI and the per-frame query path. GLFW (the polling layer in `engine/core`) is itself main-thread-only per the GLFW manual, so the consumer side already serialises every read. There is no use case in this engine for cross-thread input mutation.

The pure-function helpers (`isActionDown`, `bindingDisplayLabel`, JSON helpers) are **re-entrant** — they own no global state and can be called from a worker if a future use case ever needed to e.g. format a binding label off the main thread. The `Logger::warning` call inside `addAction` is mutex-guarded by `engine/core/logger.cpp:33` so it is safe to invoke from any thread, but the surrounding `InputActionMap` mutation is not.

No locks held; no shared state across threads.

## 8. Performance budget

60 Frames-Per-Second (FPS) hard requirement → 16.6 ms per frame. `engine/input` is overhead-only — every microsecond it consumes is a microsecond the renderer / physics / domain systems don't get.

| Path | Budget | Measured (RX 6600, 1080p) |
|------|--------|----------------------------|
| `isActionDown` (3-slot scan + predicate) | < 0.005 ms | TBD — measure by Phase 11 audit (low-risk: linear scan over 3 slots) |
| `findAction` (≤ 64 actions, linear scan) | < 0.005 ms | TBD — measure by Phase 11 audit |
| `findConflicts` (full-map scan) | < 0.05 ms | TBD — measure by Phase 11 audit (only runs in rebind UI, not per-frame) |
| `bindingDisplayLabel` (switch over 100 codes) | < 0.001 ms | TBD — measure by Phase 11 audit (only runs in rebind UI) |
| `extractInputBindings` (whole-map serialisation) | < 0.5 ms | TBD — measure by Phase 11 audit (only runs on settings save) |
| `applyInputBindings` (whole-map deserialisation) | < 1 ms | TBD — measure by Phase 11 audit (only runs on settings load) |

Per-frame cost dominated by `isActionDown`, which the FPC and a handful of gameplay verbs call ≤ ~10× per frame — well inside the 0.05 ms slice. JSON paths run only at load / save, not per-frame.

Profiler markers / capture points: none — `engine/input` is below the threshold that warrants `glPushDebugGroup` markers, and the polling that surrounds it is timed under the `InputManager::update` slice in `engine/core`.

## 9. Memory

| Aspect | Value |
|--------|-------|
| Allocation pattern | Heap via `std::vector<InputAction>` + small `std::string` members (small-string-optimisation hits for typical action ids like `"Jump"`, `"Fire"`). |
| Peak working set | Negligible. With ~30 action verbs the live + defaults vectors total ~6 KB on a 64-bit build (each `InputAction` = 3 strings + 3 `InputBinding`s ≈ 100 B). |
| Ownership | `InputActionMap` owns its `m_actions` and `m_defaults` vectors. `Engine` owns the canonical `m_inputActionMap` (`engine/core/engine.h:221`). |
| Lifetimes | Engine-lifetime — registration happens in `Engine::initialize`, the map persists until `Engine::shutdown`. JSON wire structs are transient (built on save / discarded after load). |

No `new`/`delete` (CODING_STANDARDS §12). No arena, no per-frame transient allocator needed — `isActionDown` does no allocation, so even at 60 FPS × 10 calls/frame = 600 calls/s the heap stays cold.

## 10. Error handling

Per CODING_STANDARDS §11 — no exceptions in steady-state hot paths. `engine/input` predates the codebase-wide `Result<T, E>` policy and uses bool returns + logged warnings.

| Failure mode | Reported as | Caller's recourse |
|--------------|-------------|-------------------|
| Unknown action id passed to `findAction` / `setPrimary` / `setSecondary` / `setGamepad` / `resetActionToDefaults` | `nullptr` (lookup) / `false` (mutator) — silent | Caller checks the return; the editor's rebind panel does. |
| Unknown action id in `applyInputBindings` (i.e. `settings.json` references an action the engine no longer registers) | `Logger::warning` "input binding for unknown action id '<id>' dropped" + skipped (`engine/core/settings_apply.cpp:346`) | Engine continues; user can hand-edit the typo. Forward-compat: stale saves from a newer build silently lose bindings for actions removed in the current build. |
| Re-registering an action whose live bindings differ from the new defaults | `Logger::warning` "InputActionMap::addAction: overwriting live bindings…" + overwrite (`engine/input/input_bindings.cpp:51`) | Caller re-orders init: register actions **before** `Settings::load`. |
| `clearSlot` with `slotIndex` out of `[0, 2]` | `false` — silent | Caller validates the slot index. |
| Malformed JSON: `bindingFromJson` missing fields | `value("device", "none")` / `value("scancode", -1)` defaults | Round-trip yields `InputBinding::none()`; no log, no throw. |
| Malformed JSON: `actionBindingFromJson` missing slot objects | Slot defaults to `InputBindingWire{}` | Round-trip yields `InputBinding::none()` for that slot. |
| Stale "none" with carried-over scancode (hand-edited file) | `bindingFromJson` zeroes `scancode` to `-1` when `device == "none"` | Caller sees a clean `InputBinding::none()` after wire→runtime translation. |
| Gamepad disconnect mid-session | **Not handled here** — `engine/input` has no notion of device presence. `InputManager::isBindingDown` (in `engine/core`) returns `false` for a disconnected gamepad via GLFW's `glfwJoystickPresent` / `glfwGetGamepadState`. | Action queries return `false` for the gamepad slot; keyboard / mouse slots remain live (this is the parity-preserving behaviour). |
| Programmer error (e.g. caller mutates `m_actions` via a stale pointer after `addAction` reallocates) | UB | Don't hold pointers across `addAction`. The pointer returned by `addAction(...)` remains valid only until the next mutation. |
| Out of memory | `std::bad_alloc` propagates | App aborts (matches CODING_STANDARDS §11). |

`Result<T, E>` / `std::expected` is on the broader engine-wide migration list (see `engine/core` Open Q4); `engine/input`-specific debt is none beyond that.

## 11. Testing

| Concern | Test file | Coverage |
|---------|-----------|----------|
| `InputBinding` primitive (factory helpers, equality, isBound) | `tests/test_input_bindings.cpp` | Public API contract |
| `InputAction::matches` over three slots | `tests/test_input_bindings.cpp` | Public API contract |
| `InputActionMap` registration / lookup / re-registration / hot-reload | `tests/test_input_bindings.cpp` | Public API + Audit I5 silent-nuke regression |
| Reverse lookup + conflict detection (same-device only) | `tests/test_input_bindings.cpp` | Audit I4 cross-device false-positive regression |
| Rebind setters + `clearSlot` invalid-index rejection | `tests/test_input_bindings.cpp` | Public API contract |
| `resetToDefaults` / `resetActionToDefaults` | `tests/test_input_bindings.cpp` | Public API contract |
| `bindingDisplayLabel` table coverage incl. numpad / Pause / extended-F / WORLD_1/2 | `tests/test_input_bindings.cpp` | Audit I6 fallback regression |
| `isActionDown` pure-function query (incl. null-checker, unknown action) | `tests/test_input_bindings.cpp` | Public API contract |
| `bindingToJson` / `bindingFromJson` round-trip + "none" scancode collapse | `tests/test_input_bindings.cpp` | Audit I2 wire round-trip in new home |
| `actionBindingToJson` / `actionBindingFromJson` round-trip + missing-slot defaults | `tests/test_input_bindings.cpp` | Audit I2 forward-compat |
| Editor → InputManager → EventBus integration | `tests/test_ui_system_input.cpp` | Smoke (covers the consumer side, not this subsystem directly) |

**Adding a test for `engine/input`:** drop a new case into `tests/test_input_bindings.cpp` next to its peers (the file is the canonical home; CMake (Cross-Platform Make) auto-discovers via `gtest_discover_tests`). Use `InputActionMap` directly without an `Engine` instance — every primitive in this subsystem is unit-testable headlessly because there is no GLFW context required. Inject a lambda into `isActionDown` to simulate "key X is currently down" without polling. The wire helpers similarly need only an `nlohmann::json` value, not a `Settings` instance.

**Coverage gap:** none in the headless surface. Visual confirmation (the rebind dialog visibly reflects a change) is deferred to the manual Phase 10.9 visual-test pass — it exercises the consumer (the Settings panel) rather than this subsystem.

## 12. Accessibility

`engine/input` itself produces no user-facing pixels or sound. **However**, it is the *route* every accessibility surface for keyboard / mouse / gamepad input flows through — and per CLAUDE.md and the engine accessibility contract, every binding must be rebindable and gamepad + keyboard parity is non-negotiable. This subsystem is one of the routing-pattern infrastructure subsystems alongside `engine/core` (settings) and `engine/core/event_bus`.

Routing surfaces:

- `InputAction` carries the persisted user-facing binding state (primary, secondary, gamepad). Every game verb registered on the action map is rebindable by construction — there is no "hardcoded" path for a `glfwGetKey` check elsewhere in the engine that would bypass this layer (any such path would be a regression).
- `Settings::controls.bindings` (a `std::vector<ActionBindingWire>` defined in `engine/core/settings.h:145`) is the persisted projection of `InputActionMap`. The sole writeable path from "user rebound a key in the Settings panel" to "subsystem behaves differently" is `applyInputBindings` (free function — `engine/core/settings_apply.cpp:335`). Note this is a *free function*, not an apply-sink — it differs in shape from the seven sinks in `settings_apply.h` because the data lives inside the `InputActionMap` object the engine owns, not behind an abstract interface.
- `InputActionMap::resetToDefaults` and `resetActionToDefaults` provide the per-row reset and per-tab reset the rebind UI surfaces. The defaults must remain accessible-friendly (e.g. no two-handed shortcut for a one-handed user — Phase 11 will introduce a one-handed preset).
- `bindingDisplayLabel` is the **sole** source for human-readable binding strings shown in the rebind UI; the editor panel never reads `glfwGetKeyName` directly. Localisation (Phase 10 Localization) wraps the returned token rather than overriding the table.
- `findConflicts` powers the "this key is already assigned to X" warning pill — Audit I4 nailed it to **same-device** so a keyboard rebind never spuriously flags a gamepad slot at the same code value.
- The Audit I5 warning on `addAction` re-registration protects user rebinds from being silently clobbered — without it, a code path running after `Settings::load` would erase the user's accessibility configuration with no log trail.

Constraint summary for downstream UI (User Interface) consumers:

- **Every binding rebindable.** Every game verb consumed via `InputManager::isActionDown` must be registered on `InputActionMap` — no `glfwGetKey(...)` short-cuts for "this one shortcut is special." Reviewers should grep for `glfwGetKey` outside `engine/core/input_manager.cpp` and treat hits as regressions.
- **Gamepad + keyboard parity, non-negotiable.** Every action with a keyboard default must also ship with a gamepad default; Settings panel should surface both columns. Audit I4 (same-device conflicts only) is what makes "bind C to gamepad and keyboard independently" usable.
- **No time-pressure puzzles.** This subsystem has no time-pressure semantics (no "double-tap within 200 ms"), and the engine's gameplay layer is forbidden from baking one in via `isActionDown` polling alone. Phase 11 hold-action / chord support, when it lands, must surface a configurable timing setting.
- **Defaults must remain accessible-friendly.** Engine-shipped defaults (`addAction(...)` calls at engine startup) are the floor; reset-to-defaults must never produce an inaccessible binding (e.g. requiring two simultaneous modifier keys with no single-key alternative).
- **Display strings respect partial sight.** `bindingDisplayLabel` returns full words ("Left Shift", "Page Down") rather than ambiguous glyphs; the rebind UI should pair the label text with the device icon, never colour-only.

## 13. Dependencies

| Dependency | Type | Why |
|------------|------|-----|
| `engine/core/logger.h` | engine subsystem | `Logger::warning` for the Audit I5 silent-nuke surfaced regression. |
| `<GLFW/glfw3.h>` | external | GLFW key / mouse-button / gamepad-button code constants used in the display-label switch and as the value space for `InputBinding::code`. Header is read-only — no GLFW handle / context call from this subsystem. |
| `<nlohmann/json.hpp>` (+ `json_fwd.hpp`) | external | JSON wire helpers. `_fwd.hpp` in the public header keeps build cost down for consumers. |
| `<functional>`, `<string>`, `<vector>`, `<algorithm>` | std | Core data model + injected-predicate signature. |

**Direction:** `engine/input` is depended on by `engine/core` (`InputManager` consumes it; `Engine` owns an `InputActionMap`; `Settings` embeds `ActionBindingWire`), `engine/editor` (rebind panel), `engine/scene/camera_mode.h` (forward-declared). `engine/input` itself does **not** depend on `engine/core/input_manager.h`, `engine/scene/`, or any `ISystem` — that one-way include direction is the whole point of the subsystem split.

## 14. References

External — current to within ≤ 1 year (per CLAUDE.md Rule 1):

- *UE Enhanced Input System: In-Game Remapping Before and After UE 5.3* (Medium / xersendo, 2024–2025) — current state of the EIS (Enhanced Input System) action-asset + IMC (Input Mapping Context) idiom; informs the three-slot "primary / secondary / gamepad" split this engine adopted. <https://medium.com/@xersendo/ue-enhanced-input-system-in-game-remapping-before-and-after-ue-5-3-03986abff066>
- Unity *Input System — Actions and Action Maps* (Unity Discussions, 2025) — Unity's switchable Action Map model; this engine simplified to a single map because the use case (architectural walkthroughs) does not need gameplay-mode-vs-menu-mode action context switching. <https://discussions.unity.com/t/best-practice-architecting-structure-use-of-inputs/756157>
- Godot 4 *InputMap with `physical_keycode`* (UhiyamaLab, 2025; godot PR #18020 / commit `1af06d3` rename) — direct precedent for the layout-preserving scancode story flagged in §15. <https://uhiyama-lab.com/en/notes/godot/input-map-key-binding-management/> · <https://github.com/godotengine/godot/pull/18020>
- *SDL_GameControllerDB* (mdqinc, ongoing — community-maintained 2026) — the gamepad mapping database GLFW embeds at build time; `glfwUpdateGamepadMappings` is the runtime hook for shipping a refreshed copy. <https://github.com/mdqinc/SDL_GameControllerDB>
- GLFW *Gamepad Mappings and SDL_GameControllerDB* (GLFW Discourse, 2025) — confirms GLFW's mapping vocabulary follows the Xbox layout (this engine's `gamepadName` table mirrors that decision). <https://discourse.glfw.org/t/gamepad-mappings-and-sdl-gamecontrollerdb/1621>
- GLFW *Input Guide — keyboard input* (3.3 / latest) — the canonical key-vs-scancode reference and the source of the "use scancodes for layout-stable WASD" pattern this subsystem still owes (§15). <https://www.glfw.org/docs/3.3/input_guide.html>
- *SDL Scancode vs Keycode — 2026 Updates and Best Practices* (copyprogramming, 2026) — recent re-statement of the same scancode-for-game-input rule. <https://copyprogramming.com/howto/difference-between-sdl-scancode-and-sdl-keycode>
- Microsoft / Xbox *Accessibility Guidelines for In-Game Controls* — three-column rebind UI pattern, per-device conflict pill, one-shot reset-to-default; cited inline in the file header for the binding factory helpers.

Internal cross-references:

- `CODING_STANDARDS.md` §11 (errors), §12 (memory), §17 (CPU/GPU), §18 (public API).
- `CLAUDE.md` rules 1, 5, 7 (research-first, library currency, CPU/GPU placement).
- `docs/engine/core/spec.md` — `engine/core` is the polling-side consumer; Open Q3 there is the same scancode-pending item flagged below.
- `docs/phases/phase_10_settings_design.md` slice 13.4 — original move plan that put the wire helpers under `engine/input/`.

## 15. Open questions

| # | Question | Owner | Target |
|---|----------|-------|--------|
| 1 | Wire-format `scancode` field stores GLFW *key codes* (not true scancodes); layout-preserving rebind (WASD on AZERTY / Dvorak) requires `glfwGetKeyScancode` + reverse lookup at the wire ↔ runtime boundary. Same Open Question as `engine/core` Open Q3 — fix lives in `settings_apply.cpp`'s `bindingToWire` / `bindingFromWire`, not in this subsystem's wire shape. | milnet01 | Phase 11 entry |
| 2 | No notion of *axis* bindings (analog stick deflection, trigger pressure, mouse wheel delta) — only digital "is this binding down?" Phase 11 hold-action / chord / axis support will require either extending `InputBinding` with a value type field or adding a parallel `InputAxisBinding` shape. Spec'd separately when scoped. | milnet01 | Phase 11 entry |
| 3 | `findActionBoundTo` returns the **first** match in registration order; if two actions share a slot (legitimate use case: same key bound to two contextual verbs in different gameplay modes) the second is invisible to reverse lookup. Currently fine because the engine has no mode contexts. Re-evaluate when Phase 11 introduces context switching. | milnet01 | Phase 11 entry |
| 4 | `bindingDisplayLabel` for keyboard codes uses a hand-curated switch (not `glfwGetKeyName`), so non-Latin layouts on Linux/X11 always render the US-QWERTY label. Acceptable today (layout-preserving rebind is Open Q1's prerequisite); revisit alongside that fix. | milnet01 | Phase 11 entry |
| 5 | Gamepad mappings ship via GLFW's embedded `SDL_GameControllerDB` snapshot; no runtime refresh path is wired up yet. `glfwUpdateGamepadMappings` could be called from `Engine::initialize` against a bundled `gamecontrollerdb.txt` to pick up new controllers without an engine rebuild. | milnet01 | triage |

## 16. Spec change log

| Date | Spec version | Author | Change |
|------|--------------|--------|--------|
| 2026-04-28 | 1.0 | milnet01 | Initial spec — `engine/input` action-map data model + JSON wire format; relocated wire helpers since Phase 10.9 Slice 9 I2; formalised post-Phase 10.9 audit. |
