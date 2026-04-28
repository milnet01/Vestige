# Phase 9E-3 Research: Node Editor Library Selection

**Date:** 2026-04-13
**Goal:** Pick a node-editor library for the visual scripting editor UI. Selection must serve both Phase 9E-3 (ScriptGraph editor) and the future Formula Workbench NodeGraph editor.
**Scope:** Library evaluation, integration risk assessment, reuse opportunities. Implementation details belong in `phase_09e3_design.md`.

---

## 1. Local Context

| Fact | Value |
|------|-------|
| ImGui version in tree | **v1.92.8 WIP** (`docking` branch, fetched via CMake FetchContent) |
| ImGui-adjacent libs already integrated | ImGuizmo, imgui-filebrowser |
| Existing node graph code | `engine/formula/node_graph.{h,cpp}` (1,275 LOC) — data only, no UI |
| Existing scripting graph code | `engine/scripting/script_graph.{h,cpp}` — data only, no UI |
| Formula workbench NodeGraph UI | **not yet integrated** — Phase 9E-3 is the right time to pick the lib once for both |

**Two graph schemas, one editor:**

| Aspect | ScriptGraph (9E) | NodeGraph (formula) |
|---|---|---|
| Pin types | `ScriptDataType` (10 types: bool/int/float/string/vec2-4/quat/entity/color/any) | `PortDataType` (4: float/vec2-4) |
| Pin kinds | EXECUTION + DATA | DATA only |
| Connection model | many-output-pins-fan-out (planned), one-input-pin | one-output-pin → one-input-pin |
| Node sources | descriptor-driven runtime registry | typed structs (math/function/literal/variable/output) |
| Serialization | `ScriptGraph::toJson` | `NodeGraph::toJson` |

The two backing data models are different enough that they can't share a single graph type — but a node-editor **widget layer** can render both as long as it accepts custom node descriptors and renders user-supplied content. All three libraries surveyed support this.

---

## 2. Library Survey

Three credible options. All MIT-licensed. None dependency-heavy.

### 2.1 `thedmd/imgui-node-editor`

| Fact | Value |
|------|-------|
| Latest release | **v0.9.3** (2026-10-14 in commit history; aligned to engine/ImGui versioning) |
| GitHub stars / forks | 4.4k / 666 |
| Required ImGui | "Vanilla ImGui 1.72+" |
| C++ standard | C++14 |
| Production reference | Spark CE engine (commercial blueprint editor) |
| Visual style | UE4 Blueprints clone (matches our phase_09e_design.md anti-pattern study) |
| Build | CMake, examples for Win/macOS/Linux |
| Open issues | ~97 (active triage; recent activity 2025-2026) |

**API model:** "draw your content, we do the rest". The library handles selection, dragging, zoom, pan, link drawing, context menu invocation. The application provides node and pin rendering inside `ed::BeginNode` / `ed::BeginPin` blocks. Bézier links, customizable styling, group dragging, selection rectangles, basic shortcuts (cut/copy/paste/delete) all built in.

**Docking-branch caveat (the load-bearing risk):** `thedmd/imgui-node-editor`'s own `docking` branch (which integrates ImGui's stack-layout extension) was **last touched July 2021** and is effectively abandoned. **This is not the same as needing ImGui's docking feature.** Our use case wants ImGui docking (panel rearrange) + node editor in one of the panels — this works with `master` v0.9.3 against ImGui 1.92.8 docking, with minor patches.

**The actively maintained fork:** `pthom/imgui-node-editor` (`imgui_bundle` branch) carries two upstream-targeted patches:
1. Fix in `ed::EditorContext::Begin` (PR proposed upstream).
2. `SettingsFile` stored as `std::string` rather than `const char*`.

Both are minimal and cherry-pickable.

**Feature coverage vs. our 9E-3 needs:**

| 9E-3 requirement | Built-in? | Notes |
|---|---|---|
| Pin-drag context search | No | Surfaced as a `ed::QueryNewLink()` event; the search palette is application-level UI we draw on top |
| Flow animation | Partial | Custom link rendering via `ed::Flow()` API exists; need custom shader-free path |
| Breakpoints | No | Application-level overlay (icon over node) using `ed::GetNodePosition` + `ImDrawList` |
| Comment boxes / groups | Yes | `NodeEditor::Group` |
| Reroute nodes | No | Implementable as a 2-pin pass-through node |
| Copy/paste/delete | Yes | Shortcuts handled by the lib |
| Save/load layout | Yes | `SettingsFile` mechanism |
| Multi-input on one pin | Configurable | Per-pin policy |
| Variable type pin colors | Yes | Pin styling per descriptor |

### 2.2 `Nelarius/imnodes`

| Fact | Value |
|------|-------|
| Latest tagged release | **v0.5** (2022-03) |
| Commits | 458 (stale-ish but still maintained sporadically) |
| Required ImGui | Not specified; works with current versions in practice |
| API model | "you do most of the state, we do the drawing" — immediate-mode |

**Known issue:** `ImGui::Separator()` inside a node spills outside (visual glitch — fixable with custom drawing).

**Why not pick this:** lower-level than thedmd's lib. We'd reimplement node movement state, link Bézier rendering, group selection, context menu plumbing — work that thedmd's lib already does. Not worth the duplicated effort given we're targeting a UE-style blueprint UI.

### 2.3 `Fattorino/ImNodeFlow`

| Fact | Value |
|------|-------|
| Latest release | **v1.2.2** (2024-06) — newest of the three |
| Stars / forks | 471 / 64 |
| Required ImGui | Not specified |
| API model | Higher-level than thedmd's: inherit from `BaseNode`, library handles the rest |

**Why not pick this:**
- Documentation does **not** mention flow animation, breakpoints, or pin-drag context — we'd be the early adopters of those features.
- Inheritance-based API conflicts with our descriptor-driven node registry (`NodeTypeDescriptor`). We'd need a `BaseNode` subclass per node type → 70+ subclasses, fighting our own architecture.
- Less production maturity than thedmd's lib (no large-engine reference).

---

## 3. Recommendation

**Use `thedmd/imgui-node-editor` master @ v0.9.3, with a thin patch layer.**

### 3.1 Specific integration plan

1. **Vendor source via FetchContent**, pinned to the v0.9.3 tag (or a specific commit hash for reproducibility):
   ```cmake
   FetchContent_Declare(
       imgui_node_editor
       GIT_REPOSITORY https://github.com/thedmd/imgui-node-editor.git
       GIT_TAG        v0.9.3
       GIT_SHALLOW    TRUE
   )
   ```
2. **Build as static lib** alongside `imgui_lib` / `imguizmo_lib`. Same `target_link_libraries(... PUBLIC imgui_lib)` pattern.
3. **Apply pthom patches as `.patch` files** in `external/patches/imgui-node-editor/` and apply with `execute_process` after `FetchContent_Populate` if the upstream fixes are not yet merged. If the v0.9.3 release works against ImGui 1.92.8 docking out of the box, no patches needed.
4. **Spike during 9E-3 Step 1**: build a single example node in an editor panel and confirm rendering, drag, zoom, save/load all work with our ImGui version. This burns half a day; if it fails, we have a fallback (see §3.4).

### 3.2 Reuse architecture

```
engine/editor/widgets/node_editor_widget.{h,cpp}     ← thin wrapper around ed:: API
        ▲
        │ used by
        │
engine/editor/panels/script_editor_panel.{h,cpp}     ← Phase 9E-3 (binds ScriptGraph)
tools/formula_workbench/formula_node_panel.{h,cpp}   ← future (binds NodeGraph)
```

The widget layer hides `ed::Begin/EndNode`, `ed::Link`, etc. and accepts a callback-based interface so the two panels can have different schemas without forking the widget.

### 3.3 Risk register

| Risk | Likelihood | Mitigation |
|---|---|---|
| ImGui 1.92.8 ABI break in node-editor internals | Medium | Spike (3.1.4); fallback to ngscopeclient or pthom fork |
| Stack-layout features needed (now or future) | Low | Don't enable; if needed later, switch to `pthom/imgui-node-editor` `imgui_bundle` branch |
| Flow animation needs deeper than `ed::Flow()` API | Low | Implement via `ImDrawList` overlay using node positions |
| Library goes truly unmaintained | Low | All-MIT, vendored source, we can fork at any time |

### 3.4 Fallback path

If the v0.9.3 spike fails against ImGui 1.92.8 docking:

1. Switch `GIT_REPOSITORY` to `https://github.com/pthom/imgui-node-editor.git`, `GIT_TAG imgui_bundle`. Maintained, has the patches we need, downside is one extra dependency layer.
2. If THAT fails, downgrade ImGui to 1.84-docking (the last version with widely-tested node-editor compat) — but this would lose recent ImGui features and is the worst-case path.
3. ImNodeFlow is the architecture-level fallback (different API style). Last resort.

---

## 4. Pin-drag context search — design note

This is the headline UX in 9E-3 (designer drags from a pin and gets a filtered palette). Not a library feature in any candidate; we implement it as an application-level popup on top of `ed::QueryNewLink()`:

1. User starts drag from output pin → `ed::QueryNewLink()` reports a pending link with no target.
2. User releases over empty canvas → we open an `ImGui::BeginPopup` with a search filter.
3. Filter is pre-applied: only NodeTypeDescriptors whose first input pin's `ScriptDataType` matches the source pin's type appear (or `ANY` types).
4. Selecting a result instantiates the node at the drop position and connects it.

This design works identically across all three library candidates and against any ImGui version — it's mostly NodeTypeRegistry filtering.

---

## 5. Flow animation, breakpoints — design notes

Both are application-level overlays on top of node-editor primitives. Sketches for the design doc:

- **Flow animation:** when a connection fires, push it onto a "recently-fired" deque with a 500ms TTL. Each frame, render an animated dashed Bézier on top of the static link using `ImDrawList::PathBezierCubicCurveTo` with phase offset `= time * speed`. Cost: O(active animations), trivial.
- **Breakpoints:** per-graph set of node IDs. When ScriptContext is about to execute a node ID in this set, set a system-level "paused" flag, push the node ID to a "stopped at" stack, and raise a `ScriptBreakpointHitEvent`. Editor renders a red dot icon over the node via `ed::GetNodePosition + ImDrawList::AddCircleFilled`. Resume = pop the stack and continue execution.

Both depend on Phase 9E-3 adding the `entryPin` / breakpoint hooks to the interpreter, which is already on our 9E-3 scope list (audit M9-M11 + L6 commitments).

---

## 6. Sources

- [thedmd/imgui-node-editor](https://github.com/thedmd/imgui-node-editor) — master v0.9.3
- [thedmd/imgui-node-editor at docking](https://github.com/thedmd/imgui-node-editor/tree/docking) — stale
- [Nelarius/imnodes](https://github.com/Nelarius/imnodes) — alternative
- [Fattorino/ImNodeFlow](https://github.com/Fattorino/ImNodeFlow) — alternative
- [pthom/imgui-node-editor `imgui_bundle` branch](https://github.com/pthom/imgui-node-editor) — actively maintained patched fork
- [ngscopeclient/imgui-node-editor](https://github.com/ngscopeclient/imgui-node-editor) — secondary fork (less active)
- [Mojang/imgui-node-editor](https://github.com/Mojang/imgui-node-editor) — internal Minecraft fork
- [imgui_bundle integration discussion](https://github.com/pthom/imgui_bundle/discussions/66) — patch list rationale
- [Useful ImGui Extensions Wiki](https://github.com/ocornut/imgui/wiki/Useful-Extensions) — ocornut's curated list
- [DeepWiki: thedmd/imgui-node-editor](https://deepwiki.com/thedmd/imgui-node-editor) — generated API docs
- [imnodes design blog](https://nelari.us/post/imnodes/) — architecture rationale
