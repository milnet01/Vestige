# Phase 9E-3 Design Document: Visual Scripting Editor UI

**Date:** 2026-04-13 (original); 2026-04-19 (status update)
**Status:** In progress — Steps 1–3 shipped (`cffd755` lib integration + M10 pin interning, `e0c56c2` M9 type cache + M11 pure memo + L6 entry pin). Step 4 (canvas + menu) WIP on `8feeffe`. Steps 5–16 remaining. See §13 Acceptance Criteria for the detailed checklist.
**Parent:** `docs/PHASE9E_DESIGN.md`
**Research:** `docs/PHASE9E3_RESEARCH.md`
**Audit baseline:** `docs/PHASE9E_AUDIT_REPORT.md`

---

## 1. Goal

Add the in-editor user interface for visual scripting: a dockable canvas where designers compose `ScriptGraph`s via mouse, with palette/properties/variables side-panels, drag-from-pin context search, save/load, and live hot-reload while the engine runs.

This phase **completes the designer-facing loop** opened by 9E-1/9E-2: previously, scripts could only be authored via C++ (ScriptGraph::addNode / addConnection). After 9E-3, a designer wires a graph, hits Save, and sees the script run on the next play test — without touching code.

### Success Criteria

1. **Designer flow:** create new script → add OnStart / Branch / SetVariable nodes → wire them → save → attach to entity via Inspector → enter play mode → script runs. Zero C++ involvement.
2. **WYSIWYG / real-time:** changes to the graph apply immediately during play, no rebuild step (per `feedback_editor_realtime` memory).
3. **Iteration speed:** drag from any output pin and see only type-compatible nodes in the popup (≤ 200 ms response on a 100-node graph).
4. **Debuggability:** breakpoints pause script execution; pin values are inspectable on hover; the most recently fired connection is animated.
5. **Performance:** the editor panel runs at 60 FPS on the dev hardware (AMD RX 6600 / Ryzen 5 5600) with a 200-node graph open and a play session live.
6. **Audit-debt closeout:** the three Mediums carried into 9E-3 (M9, M10, M11) ship as part of this phase.

---

## 2. Scope

### In scope

- imgui-node-editor library integration with our existing CMake/ImGui setup.
- `ScriptEditorPanel` — dockable canvas inside the existing editor, hosting the node graph view.
- Node palette (categorised, search-filterable).
- Properties side-panel for the selected node (per-property type editors reusing existing inspector primitives where possible).
- Variables panel (Blackboard editor) — add/remove/rename variables, set defaults, browse all scopes.
- Drag-from-pin context popup with type-aware filtering.
- Graph save/load to/from `.vscript` JSON (already supported by `ScriptGraph::loadFromFile / saveToFile`; this phase wires the editor menus).
- Inspector integration: an entity's `ScriptComponent` shows attached graphs and exposes Entity-scope variables for editing.
- Undo/redo via existing `CommandHistory` (new `ScriptGraph*` command subclasses).
- Breakpoint support: per-node breakpoints, pause + step UI, pin value inspection while paused.
- Flow animation for the most-recently-fired connection.
- Read-only badge while a breakpoint is active.
- The three audit-debt items: M9 (cached node-type-IDs), M10 (pin-name interning), M11 (per-execution pure-node memoization).

### Out of scope (deferred to 9E-4 or later)

- Subgraphs / macros (collapsing groups of nodes).
- State-machine graph type (separate graph schema for FSMs).
- Bytecode compilation.
- Custom event definitions (user-defined event types with custom field schemas).
- Array / collection ScriptDataType.
- Templates panel ("Door / Collectible / Checkpoint / DamageZone / Puzzle" — deferred to 9E-4 per parent design §11).
- Formula Workbench NodeGraph editor — the **widget layer** is built in 9E-3, but the workbench's panel that uses it is deferred to a follow-on. The reuse seam is built; the workbench-side wiring is not.

---

## 3. Architecture

### 3.1 Module layout

```
external/CMakeLists.txt                                 ← FetchContent imgui-node-editor
external/patches/imgui-node-editor/  (if needed)        ← cherry-picked compat patches

engine/editor/widgets/
    node_editor_widget.h / .cpp                         ← thin C++ wrapper around ed:: API
    pin_drag_search_popup.h / .cpp                      ← type-filtered palette popup
    flow_animation_overlay.h / .cpp                     ← time-phased dashed Bézier overlay

engine/editor/panels/
    script_editor_panel.h / .cpp                        ← the dockable editor panel
    script_palette_panel.h / .cpp                       ← left-side categorised palette
    script_properties_panel.h / .cpp                    ← right-side selected-node inspector
    script_variables_panel.h / .cpp                     ← blackboard editor
    script_breakpoint_panel.h / .cpp                    ← debug / breakpoint manager

engine/editor/commands/                                 ← new commands for graph edits
    script_add_node_command.h
    script_remove_node_command.h
    script_add_connection_command.h
    script_remove_connection_command.h
    script_move_node_command.h
    script_set_property_command.h

engine/scripting/                                       ← interpreter hooks
    script_context.{h,cpp}                              ← +entryPin, +breakpoint hooks, +pure-node memo
    script_instance.{h,cpp}                             ← +cached type→IDs (M9), +interned pin IDs (M10)
    pin_id.h                                            ← interned-string pin ID type (M10)
    script_breakpoint.h / .cpp                          ← per-graph breakpoint set + pause state

engine/scripting/script_component.{h,cpp}               ← already exists (9E-1); 9E-3 wires it to editor

tests/test_script_editor_widget.cpp                     ← widget-level tests (no ImGui context needed)
tests/test_script_editor_commands.cpp                   ← command undo/redo tests
tests/test_script_breakpoints.cpp                       ← breakpoint state machine tests
```

### 3.2 Layering rules

- **`engine/editor/widgets/node_editor_widget`** is the only place that touches `ed::` types. Everything else uses our wrapper. This isolates the library risk: if we ever swap to ImNodeFlow or pthom's fork, the surface area to update is one file.
- **`engine/scripting/`** does NOT depend on `engine/editor/`. The interpreter exposes hooks (breakpoint state, pure-node cache key); the editor reads/writes them. Tests on the scripting subsystem stay headless.
- **Panels** depend on widgets and on editor primitives (Selection, CommandHistory). They do not directly call `ed::` API.

### 3.3 Reuse seam for Formula Workbench

`node_editor_widget` accepts a callback-based interface so different graph schemas can drive it without forking the widget. The widget's interface (sketch):

```cpp
class NodeEditorWidget
{
public:
    struct Hooks
    {
        std::function<void()> drawNodes;        // caller renders each node's contents
        std::function<void()> drawLinks;        // caller renders each connection
        std::function<void(uint32_t /*srcPin*/, uint32_t /*dstPin*/)> onCreateLink;
        std::function<void(uint32_t /*nodeId*/, ImVec2 /*pos*/)> onMoveNode;
        std::function<void()> onDelete;         // selection delete
        std::function<void(uint32_t /*pinId*/, ImVec2 /*at*/)> onPinDragRelease;
    };

    void initialize();
    void render(const Hooks& hooks);
    void shutdown();
    // ... selection queries, save/load layout settings ...
};
```

`ScriptEditorPanel` and (later) `FormulaEditorPanel` each provide their own `Hooks` instance bound to their schema. The widget owns `ed::EditorContext` lifetime and per-view layout settings.

---

## 4. Library Integration

Per `PHASE9E3_RESEARCH.md`: vendor `thedmd/imgui-node-editor` v0.9.3 via CMake FetchContent, build as a static lib, link from `imgui_lib`. Spike during Step 1 (below) to confirm compatibility with our ImGui v1.92.8 docking branch. Fallbacks ranked: pthom/imgui-node-editor `imgui_bundle` branch → ImGui downgrade to 1.84-docking → ImNodeFlow.

### 4.1 CMake addition

```cmake
# external/CMakeLists.txt — appended
FetchContent_Declare(
    imgui_node_editor
    GIT_REPOSITORY https://github.com/thedmd/imgui-node-editor.git
    GIT_TAG        v0.9.3
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(imgui_node_editor)
if(NOT imgui_node_editor_POPULATED)
    FetchContent_Populate(imgui_node_editor)
endif()
add_library(imgui_node_editor_lib STATIC
    ${imgui_node_editor_SOURCE_DIR}/imgui_node_editor.cpp
    ${imgui_node_editor_SOURCE_DIR}/imgui_node_editor_api.cpp
    ${imgui_node_editor_SOURCE_DIR}/imgui_canvas.cpp
    ${imgui_node_editor_SOURCE_DIR}/crude_json.cpp
)
target_include_directories(imgui_node_editor_lib PUBLIC ${imgui_node_editor_SOURCE_DIR})
target_link_libraries(imgui_node_editor_lib PUBLIC imgui_lib)
target_compile_options(imgui_node_editor_lib PRIVATE -w)
```

If the spike fails: `external/patches/imgui-node-editor/*.patch` + `execute_process(COMMAND git apply ...)` after populate.

### 4.2 Risk and rollback

If post-spike the library proves incompatible:
- **Day 1 fallback:** swap repo URL to `pthom/imgui-node-editor` `imgui_bundle` branch.
- **Day 2 fallback:** downgrade `imgui` GIT_TAG to `v1.84-docking` and accept the lost ImGui features (low-priority modernise tasks).
- **Worst case:** swap to ImNodeFlow — invasive (different API style), would re-shape ScriptEditorPanel.

The spike must happen in Step 1 (before any panel code) so a failure surfaces with a half-day spent rather than a week.

---

## 5. Editor Panels

### 5.1 ScriptEditorPanel (the canvas)

- Dockable ImGui window. Title `Script Editor — <graphName>`.
- File menu: New / Open / Save / Save As / Close. Standard `.vscript` extension.
- Hosts `NodeEditorWidget` over the entire client area minus a thin top toolbar.
- Toolbar: Play/Pause/Step buttons (only enabled in play mode), Zoom Fit, Centre, Save indicator (dirty flag).
- Status bar (bottom): hover-pin type info, breakpoint summary, node/connection counts.
- One open script per panel; multiple panel instances allowed for side-by-side editing.

### 5.2 ScriptPaletteePanel (left dock)

- Tree of NodeTypeDescriptors grouped by `category` field (Events / Flow / Math / Vector / Logic / Comparison / Variables / Latent / Action / Custom).
- Search box at top with substring filter on display name + tooltip.
- Drag-and-drop node descriptor into the canvas to create a new instance at the drop position.
- Tooltip on hover: the descriptor's `tooltip` text plus a small preview icon set (input/output pin types).

### 5.3 ScriptPropertiesPanel (right dock)

- Reflects the **selected node** (from `NodeEditorWidget` selection query).
- Lists each property pin with a typed editor:
  - `BOOL` → checkbox
  - `INT` → drag-int
  - `FLOAT` → drag-float
  - `STRING` → text input (capped at `ScriptGraph::MAX_STRING_BYTES` per audit C1).
  - `VEC2/3/4` → reuse `ImGui::DragFloat2/3/4`.
  - `QUAT` → Euler-angle drag (mirrors entity transform Inspector).
  - `COLOR` → `ImGui::ColorEdit4`.
  - `ENTITY` → entity ID drag-int + "Pick from scene…" button (opens scene tree).
  - `ANY` → type chooser then the appropriate editor.
- Each edit goes through `ScriptSetPropertyCommand` → CommandHistory (undo/redo).
- Multiple-selection: shows common properties; edits apply to all (CompositeCommand).

### 5.4 ScriptVariablesPanel (bottom-left dock, optional)

- Tabs: Graph / Scene / Application / Saved scopes (Entity scope is in the Inspector, see §5.6).
- Per-scope: list of `VariableDef` entries with name + type + default-value editor + delete button.
- "Add variable" button opens a small dialog for name + type.
- Validation: name must be non-empty and unique within scope. Reject with inline error.
- Honors `Blackboard::MAX_KEYS` cap with a visible counter.

### 5.5 ScriptBreakpointPanel (bottom-right dock, optional)

- List of all breakpoints across loaded graphs: `<graphName> :: <nodeId> (<typeName>)`.
- Click a row → pans the editor to that node.
- Pause / Resume / Step buttons mirror the toolbar.
- "Stopped at" indicator showing the active breakpoint when paused.
- Pin value inspector (table of pin name → current ScriptValue) for the paused node.

### 5.6 Inspector integration

`ScriptComponent` was added in 9E-1 with `addScript` / `removeScript`. The Inspector grows a new `Scripts` section that:

- Lists attached scripts (graph asset path + an "Open in Editor" button that opens a ScriptEditorPanel for that graph).
- Renders each Entity-scope variable as a typed editor (same primitives as §5.3) — edits route to `ScriptInstance::graphBlackboard()`.
- "Attach Script…" button opens a file picker.

---

## 6. Interpreter Hooks

### 6.1 Entry-pin field (resolves audit L6 / Gate TODO)

`ScriptContext` gains:

```cpp
const std::string& entryPin() const { return m_entryPin; }
private:
    std::string m_entryPin; // empty string for the initial entry
```

`triggerOutput(node, pinName)` sets `m_entryPin = <target's matching input pin>` before calling the target's execute. Multi-input nodes (Gate's `Open` / `Close` / `Toggle`, future merge nodes) read `ctx.entryPin()` to dispatch.

The Gate node's `// Phase 9E-3 prerequisite` comment becomes the Gate's first 9E-3 commit.

### 6.2 Breakpoints

```cpp
class ScriptBreakpoints
{
public:
    void toggle(const ScriptGraph* graph, uint32_t nodeId);
    bool isSet(const ScriptGraph* graph, uint32_t nodeId) const;
    bool isPaused() const { return m_paused; }
    void resume();
    void step();
    // ... event-bus integration: ScriptBreakpointHitEvent, ScriptResumedEvent
};
```

`ScriptContext::executeNode` checks the breakpoint set before running each node:

```cpp
if (m_breakpoints && m_breakpoints->isSet(&m_instance.graph(), nodeId))
{
    m_breakpoints->setPaused(true, &m_instance, nodeId);
    m_engine.getEventBus().publish(ScriptBreakpointHitEvent{...});
    return; // execution resumes via Step / Resume from the editor
}
```

The breakpoint set is **per-graph** (lives in `ScriptingSystem`, not the asset on disk). When paused, the ScriptingSystem freezes `tickUpdateNodes` and `tickLatentActions` so the world doesn't drift while the designer inspects state. Engine rendering and editor camera continue.

### 6.3 M9 — Generalized type→IDs cache

Today: `ScriptInstance` caches OnUpdate IDs only (Batch 2 H3 fix). 9E-3 generalises this to all node types the system needs at runtime.

```cpp
// script_instance.h
const std::vector<uint32_t>& nodesByType(const std::string& typeName) const;
// script_instance.cpp:rebuildCaches()
for (const auto& [id, inst] : m_nodeInstances)
{
    m_typeIndex[inst.typeName].push_back(id);
}
```

Replaces every remaining `findNodesByType()` allocation on the hot path. Lookup is now a single `unordered_map::find` returning a const reference to a pre-built vector — no per-call allocation.

### 6.4 M10 — Pin-name interning

Today: pin name comparisons happen as `std::string == std::string` on every pin read. With M10 the pin name is interned at node-registration time:

```cpp
// pin_id.h
using PinId = uint32_t; // FNV-1a hash of the pin name string

PinId internPin(const std::string& name); // dedups via a process-global table
const std::string& pinName(PinId id);     // for editor display
```

`NodeTypeDescriptor`'s pin defs hold `PinId` alongside the display string. `ScriptNodeInstance::outputValues` becomes `std::unordered_map<PinId, ScriptValue>`. `ScriptContext::readInput` / `setOutput` accept PinId on the hot path (string overloads remain for backwards compat in tests; they intern then forward).

This is the largest 9E-3 task because it touches every node registration — but every node is touched by 9E-3 anyway (palette descriptors, properties panel) so the marginal cost is small.

### 6.5 M11 — Per-execution pure-node memoization

Today: pure nodes re-evaluate every time their output is read. Inside loops this is multiplicative (a ForLoop reading a pure node 100 times runs that pure node 100 times).

Design: `ScriptContext` gains a memo cache scoped to one `executeNode` call chain:

```cpp
// script_context.cpp
ScriptValue evaluatePureNode(uint32_t nodeId, PinId outputPin)
{
    auto key = (uint64_t(nodeId) << 32) | outputPin;
    auto it = m_pureCache.find(key);
    if (it != m_pureCache.end()) return it->second;

    // ... existing eval logic ...

    auto& result = nodeInst->outputValues[outputPin];
    m_pureCache[key] = result;
    return result;
}
```

Cache lives on the stack (member of ScriptContext) — destroyed when the chain finishes. Pure node side effects (there are none by definition) cannot leak across executions.

**Opt-out:** a per-node `forceReevaluate` flag for the rare case a designer wants stochastic re-evaluation per read (e.g., `RandomFloat` pure node — currently doesn't exist, but reserve the field).

### 6.6 Order of interpreter changes

Implement in order: M10 (pin interning) first, because M9 and M11 both touch the same hot-path code paths. Then M9 + M11 in either order. Each lands as a separate commit with its own benchmark before/after.

---

## 7. UX Flows

### 7.1 Pin-drag context search

1. User press-and-drags from output pin → `ed::QueryNewLink` reports a pending link.
2. User releases over empty canvas → `NodeEditorWidget` calls `Hooks::onPinDragRelease(srcPinId, dropPosition)`.
3. `ScriptEditorPanel` opens an ImGui popup at `dropPosition` filtered by:
   - Pin direction (output → looking for nodes with matching input).
   - Type compatibility (`ScriptDataType` exact match OR `ANY` either side OR convertible per `ScriptValue::convertTo`).
   - Substring match on the search input.
4. Selecting a result:
   - Creates the node via `ScriptAddNodeCommand` (undoable).
   - Connects the pins via `ScriptAddConnectionCommand`.
   - Both commands group into a `CompositeCommand` so one Ctrl+Z undoes both.

Filter computation runs on every keystroke — 70 descriptors × ~5 type checks each = 350 ops — well under the 200 ms budget.

### 7.2 Flow animation

`ScriptFlowAnimator` (lives in widget layer):

- Subscribes to a new `ScriptConnectionFiredEvent` published by `ScriptContext::triggerOutput`.
- Maintains a deque of `(connectionId, firedAt)` with 500 ms TTL.
- Each frame, for each entry: render a dashed Bézier on top of the static link using `ImDrawList::PathBezierCubicCurveTo`; the dash phase advances with elapsed time so the dashes appear to flow from source to target.
- Auto-disabled when the editor is not visible (no per-frame cost when closed).

### 7.3 Breakpoint debug session

1. User clicks a node's "set breakpoint" gutter → `ScriptBreakpoints::toggle`.
2. User enters play mode. Eventually the script reaches the node.
3. `ScriptContext::executeNode` sees the breakpoint, publishes `ScriptBreakpointHitEvent`, returns without executing.
4. Editor pauses world simulation but keeps rendering.
5. User inspects pin values in `ScriptBreakpointPanel`'s pin table (read from `nodeInst->outputValues` and from `evaluatePureNode` for unconnected pure-driven inputs).
6. User clicks Step → executes only that one node, then pauses again at its first downstream node.
7. User clicks Resume → clears pause flag, normal execution continues, breakpoint may re-trigger next time.

### 7.4 Hot-reload

The scene's `ScriptComponent` references a `.vscript` path. When the editor saves a graph, it:

1. Writes the file via `ScriptGraph::saveToFile`.
2. For every active ScriptInstance whose graph asset path matches: call `unregisterInstance`, reload the graph, call `initialize` + `registerInstance` again.
3. Variable values from Graph and Entity scope **persist across reload** (copied from old blackboard to new). This honors `feedback_editor_realtime` — no rebuild, no restart.

Edge case: if a script is mid-latent-action (e.g. waiting in a Delay), the latent action queue is dropped on reload. Document: hot-reload is a "restart this script's runtime state" operation by design.

### 7.5 Save / load

- "Save" writes to the path the graph was loaded from. New graphs prompt for a path on first Save.
- "Save As" opens the file dialog regardless.
- "Open" uses the same file dialog (`imgui_filebrowser`) restricted to `*.vscript`.
- Path safety: every load goes through `ScriptGraph::loadFromFile` which already enforces traversal rejection (Batch 1 fix M7).

---

## 8. Undo / Redo

All graph mutations from the editor route through `CommandHistory` — the same instance the rest of the editor uses, so a single Ctrl+Z works across scene + script edits.

New command classes (each implements `EditorCommand::execute / undo`):

| Command | Mutation |
|---|---|
| `ScriptAddNodeCommand` | adds a node, undo removes by ID |
| `ScriptRemoveNodeCommand` | removes a node + records its connections, undo restores all |
| `ScriptAddConnectionCommand` | adds a connection, undo removes by ID |
| `ScriptRemoveConnectionCommand` | mirror |
| `ScriptMoveNodeCommand` | sets posX/posY, undo restores prior. Coalesce sibling moves within 250 ms (drag yields one undoable step). |
| `ScriptSetPropertyCommand` | sets `node.properties[key]` to a `ScriptValue`, undo restores prior |

`CompositeCommand` (already exists in `engine/editor/commands/`) groups the pin-drag-search "create node + connect pins" pair into a single undoable step (§7.1).

The `MAX_COMMANDS = 200` cap from `CommandHistory` applies — if a designer makes 250 small edits, the oldest 50 are discarded. The `m_savedVersionLost` flag the existing system tracks correctly handles "undo past saved state" semantics.

---

## 9. Performance Budget

| Subsystem | Target per frame at 60 FPS | Owner |
|---|---|---|
| ScriptingSystem `update()` | ≤ 0.5 ms with 50 active scripts (parent design §1) | `engine/scripting` |
| ScriptEditorPanel render | ≤ 2 ms with a 200-node graph open | `engine/editor/panels` |
| Flow animation overlay | ≤ 0.2 ms with 50 active animations | widget layer |
| imgui-node-editor internal | (measured during Step 1 spike) | upstream |

After M9/M10/M11 land, expect (vs. pre-9E-3 baseline measured during Batch 2):
- `findOutputConnection` (fixed in H4): already O(pins-per-node) — unchanged.
- `findNodesByType` calls: O(1) lookup post-M9 (vs O(N) pre).
- Pin-name comparisons in `readInput`: integer compare post-M10 (vs string compare pre).
- Pure-node re-eval inside loops: 1× per chain post-M11 (vs N× pre).

A new benchmark test (`tests/test_scripting_perf.cpp`) measures these on a synthetic 100-node graph and asserts upper bounds — failure breaks CI.

---

## 10. Test Plan

### 10.1 Headless tests (no ImGui context required)

- All command classes: execute / undo / redo round-trips.
- Pin-id interning: `internPin("foo") == internPin("foo")`, `pinName(internPin("bar")) == "bar"`.
- Generalised `nodesByType` cache: rebuilt on `initialize`, returns expected IDs.
- Pure-node memoization: a counter-incrementing pure node fires once per execution chain even when read 10 times.
- Entry-pin propagation: a Gate with `Open` / `Close` / `Toggle` inputs dispatches by `ctx.entryPin()`.
- Breakpoint state machine: toggle, hit, pause, step, resume — verify call counts.
- Hot-reload: variables persist across reload, latent actions drop, instance re-registers.
- Path safety regressions stay green (already in 9E-2 audit suite).

### 10.2 Widget-level tests (ImGui mock context)

- `NodeEditorWidget::Hooks` callbacks fire with expected args on synthetic input events.
- `pin_drag_search_popup` filter returns expected NodeTypeDescriptors for given source pin types.

### 10.3 End-to-end visual tests

Per `feedback_visual_test_each_feature`: every feature gets a manual visual check during development:

1. Library spike: blank canvas with one node, drag, zoom, pan.
2. Palette: all 70 nodes appear, search works.
3. Node placement: drag from palette, reposition.
4. Connection: drag pin to pin, type-incompatible pins refuse.
5. Properties: edit each ScriptDataType.
6. Save / load round-trip: file unchanged byte-for-byte? (canonical JSON formatting).
7. Pin-drag context: drag from `Bool` output, only Bool/Any inputs offered.
8. Hot-reload: edit graph mid-play, see immediate effect.
9. Breakpoint: set, pause, inspect, step, resume.
10. Flow animation: trigger an event, see the connection animate.

Every step: F11 capture → user reviews per `user_accessibility` memory.

### 10.4 Performance regression test

`tests/test_scripting_perf.cpp` builds a 100-node graph (mix of OnUpdate, math, branch, sequence), runs 100 frames, asserts:

- Total ScriptingSystem `update()` time < 1.0 ms / frame avg (4× headroom over the 0.5 ms / 50-scripts target).
- No per-frame heap allocations in the hot path (use a lightweight allocation counter).

---

## 11. Implementation Step Plan

Each step ends with: compile + ctest + visual check + commit.

| Step | Title | Estimated size | Risk gate |
|---|---|---|---|
| 1 | imgui-node-editor integration spike | small | If it fails, switch fallback BEFORE touching panel code |
| 2 | M10 pin-name interning across NodeTypeRegistry / ScriptContext | medium | Must keep all 1730 tests green |
| 3 | M9 generalised type→IDs cache + M11 pure-node memoization + entry-pin field | medium | Performance benchmark passes |
| 4 | `NodeEditorWidget` wrapper + minimal `ScriptEditorPanel` (canvas only) | medium | Visual: blank canvas, dock, save/load via menu |
| 5 | `ScriptPaletteePanel` + drag-to-create | small | Visual: drag any of 70 nodes |
| 6 | `ScriptPropertiesPanel` + per-type editors + `ScriptSetPropertyCommand` | medium | Visual: edit each ScriptDataType |
| 7 | Connection editing + `ScriptAddConnectionCommand` / `ScriptRemoveConnectionCommand` | small | Visual: type-mismatch rejected with red link |
| 8 | Pin-drag context search popup + composite create-and-connect | medium | Visual: type-aware filter works |
| 9 | `ScriptVariablesPanel` (Graph / Scene / App / Saved tabs) | small | Visual: add/edit/delete each scope |
| 10 | Inspector integration (ScriptComponent section + Entity-scope variables) | small | Visual: attach script, edit entity vars |
| 11 | Hot-reload on save | small | Visual: edit during play, see change |
| 12 | Breakpoint state machine + pause / step / resume | medium | Visual: pause, inspect, step, resume |
| 13 | `ScriptBreakpointPanel` UI | small | Visual: complete debug loop |
| 14 | Flow animation overlay | small | Visual: connections animate when fired |
| 15 | Performance benchmark test + tuning if needed | small | Performance budget met |
| 16 | Phase 9E-3 audit (per `AUDIT_STANDARDS.md`) | large | All findings resolved before 9E-4 |

Total: ~16 commits, each independently revertable. No commit leaves the build broken.

---

## 12. Risks

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| imgui-node-editor incompatible with ImGui 1.92.8 docking | Medium | Day-blocker | Step 1 spike + ranked fallbacks (research §3.4) |
| Pin-name interning breaks existing tests | Medium | Step-blocker | Keep string-overloads as wrappers; intern table is process-global / thread-safe |
| Hot-reload corrupts in-flight latent actions | Low | Surprise behavior | Document "reload restarts script runtime"; tests verify variable persist + latent drop |
| Breakpoint pause leaks through to engine simulation | Low | Visual confusion | Pause flag scoped to ScriptingSystem only; renderer/camera/input untouched |
| Performance budget exceeded with 200-node graph | Low | UX | Per-frame benchmark test; if breached, profile before optimising blindly |
| Undo/redo of node delete loses connections | Medium (until tested) | Data loss | `ScriptRemoveNodeCommand` snapshots all involved connections in undo data |

---

## 13. Acceptance Criteria

A binding checklist. **All items required** for 9E-3 to be considered complete and the post-phase audit to start.

**Progress so far (2026-04-19 doc sync):** Steps 1–3 of §11 are shipped (commits `cffd755`, `e0c56c2`); Step 4 is WIP on branch (`8feeffe`). Steps 5–16 remain. The four audit-debt items (M9 / M10 / M11 / L6) and the library-integration spike landed with Step 1–3 ahead of the full panel work.

- [ ] All 16 implementation steps committed; all tests pass. — **Steps 1–3 done, Step 4 WIP, 12 remaining.**
- [x] Library integration is one of: thedmd master v0.9.3, pthom imgui_bundle branch, or documented downgrade. — `thedmd/imgui-node-editor` master via `external/CMakeLists.txt:227`; engine builds `imgui_node_editor_lib` STATIC.
- [x] **M9** generalised type→IDs cache shipped; `findNodesByType` no longer allocates on the hot path. — commit `e0c56c2`.
- [x] **M10** pin-name interning shipped; `ScriptNodeInstance::outputValues` uses `PinId`; string overloads remain as compat wrappers. — commit `cffd755`; `engine/scripting/pin_id.{h,cpp}`.
- [x] **M11** per-execution pure-node memoization shipped; benchmark shows the multiplicative-in-loops behavior eliminated. — commit `e0c56c2`; `ScriptContext::m_executionMemo`, `NodeTypeDescriptor::memoizable`.
- [x] **L6** Gate node uses the new `entryPin` field (TODO comment removed). — commit `e0c56c2`; `flow_nodes.cpp:289` reads `ctx.entryPin()`, TODO comment removed.
- [ ] All Step 16 audit findings resolved. — **Step 16 (phase audit) not yet run.**
- [ ] Performance benchmark test passes (script update ≤ 1 ms / frame at 100-node test graph). — **Step 15 not yet shipped.**
- [ ] Visual test pass on dev hardware: 60 FPS sustained with editor open + 200-node graph + play session live. — **blocked on Step 5–14 (panels + interaction).**
- [ ] CHANGELOG / ROADMAP updated. — Phase 9E-1 / 9E-2 are in `CHANGELOG.md`; 9E-3 entries land with each step.
- [x] ARCHITECTURE.md §19 updated to mention editor integration. — 2026-04-19 doc-sync pass added the "Editor integration (Phase 9E-3)" subsection describing `ScriptEditorPanel`, `NodeEditorWidget`, and the currently-landed Step 4 scope.

---

## 14. Sources

- `docs/PHASE9E_DESIGN.md` — parent design (10 sections covering ScriptValue, Blackboard, NodeTypeRegistry, interpreter, EventBus bridge, node library)
- `docs/PHASE9E3_RESEARCH.md` — library survey, fallback paths, source links
- `docs/PHASE9E_AUDIT_REPORT.md` — Mediums committed to this phase (M9, M10, M11), Lows already resolved
- `CODING_STANDARDS.md` — naming, formatting, structure rules
- `SECURITY.md` — input validation & path safety (existing graph caps continue to apply)
- `AUDIT_STANDARDS.md` — Step 16 audit process
- Memory: `feedback_editor_realtime` — WYSIWYG, no rebuild step
- Memory: `feedback_visual_test_each_feature` — visual validation per step
- Memory: `feedback_always_write_tests` — tests authored alongside features
- Memory: `user_accessibility` — F11 frame capture for visual review
