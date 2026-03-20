# Undo/Redo System Research for Vestige Editor

Research completed: 2026-03-20
Context: C++17 scene editor with entities, components, transforms, materials, and hierarchy.

---

## Table of Contents

1. [How Major Engines Implement Undo/Redo](#1-how-major-engines-implement-undoredo)
2. [Architectural Approaches Compared](#2-architectural-approaches-compared)
3. [Command Granularity and Batching](#3-command-granularity-and-batching)
4. [Memory Management](#4-memory-management)
5. [Specific Command Types for a Scene Editor](#5-specific-command-types-for-a-scene-editor)
6. [C++ Implementation Patterns](#6-c-implementation-patterns)
7. [Undo History UI](#7-undo-history-ui)
8. [Integration Challenges](#8-integration-challenges)
9. [Recommendations for Vestige](#9-recommendations-for-vestige)
10. [Sources](#10-sources)

---

## 1. How Major Engines Implement Undo/Redo

### 1.1 Unreal Engine — Transaction System

Unreal uses a **transaction-based** approach built on UObject serialization. The core API:

1. `GEditor->BeginTransaction(FText)` — starts a transaction.
2. `Object->Modify()` — snapshots the object's state before mutation.
3. Make your changes to the object.
4. `GEditor->EndTransaction()` — finalizes and pushes to undo stack.

The system uses `FScopedTransaction` for RAII-style scoping — the transaction begins in the constructor and ends in the destructor, ensuring cleanup even on exceptions.

Key details:
- **State-snapshot based**: The system serializes the full UObject state via Unreal's reflection/property system before and after changes.
- **UTransBuffer** manages the undo/redo stack of serialized archives.
- Objects **must be UObjects** with UProperties to participate — plain C++ objects cannot be tracked.
- Nested sub-objects require individual `Modify()` calls.
- Some operations (e.g., spawning actors) automatically participate in transactions.

Trade-offs: Powerful and automatic once objects use UObject, but tightly coupled to Unreal's reflection system. Heavy memory use from full-object serialization.

*Source: [Unreal Forum — How does the transaction system work?](https://forums.unrealengine.com/t/how-does-the-transaction-undo-redo-system-work/355792), [Implement Undo and Redo in Unreal Engine](https://unreal.robertlewicki.games/p/implement-undo-and-redo-in-unreal-engine)*

### 1.2 Godot — UndoRedo Action System

Godot provides a dedicated `UndoRedo` class with an explicit action-registration API:

1. `create_action("Action Name")` — begins recording.
2. `add_do_method()` / `add_undo_method()` — registers forward/backward method calls.
3. `add_do_property()` / `add_undo_property()` — registers property changes.
4. `commit_action()` — finalizes the action.

Key details:
- **Command-based** with explicit do/undo method pairs — the developer must manually specify both directions.
- **Merge modes**: `MERGE_DISABLE` (separate actions), `MERGE_ENDS` (merges consecutive same-named actions, keeping first undo + last do), `MERGE_ALL` (merges everything).
- `MERGE_ENDS` is specifically designed for continuous operations like dragging — it keeps the initial state from the first action and the final state from the last.
- **max_steps** property limits history depth; 0 means unlimited.
- **Version tracking**: Every commit increments a version number, useful for dirty-flag / save detection.

Trade-offs: Very explicit — the developer controls exactly what is recorded, which prevents accidental missed state. But it requires discipline: every editor operation must manually construct its do/undo pair.

*Source: [Godot UndoRedo documentation](https://docs.godotengine.org/en/stable/classes/class_undoredo.html), [Godot EditorUndoRedoManager](https://docs.godotengine.org/en/stable/classes/class_editorundoredomanager.html)*

### 1.3 Blender — Memfile State Snapshots

Blender uses a fundamentally different approach: **serialized state snapshots** via its `.blend` file format.

Architecture (three layers):
1. **Editor level** (`ed_undo.cc`): Provides undo/redo operators and UI exposure.
2. **BKE core** (`undo_system.cc`): Manages the undo stack, handles step processing.
3. **Type-specific handlers**: Subsystems (sculpt, paint, memfile) register `UndoType` handlers.

Two step types:
- **Stateful steps**: Store complete data snapshots. Can restore from any position.
- **Differential steps**: Store only the diff from the previous step. Must be applied sequentially.

The memfile undo system serializes the entire scene into an in-memory `.blend` file for each undo step. This is the same serialization code used for saving to disk.

Key details:
- The system is **fully relative** — to reach a target step, all intermediate steps must be processed.
- Only **data** modifications are tracked — UI state changes (viewport position, etc.) are excluded.
- Different modes (sculpt, texture paint, edit mode) contribute their own step types to a unified stack.
- Steps can be marked "skipped" to hide intermediate states from the user.

Trade-offs: Extremely robust (leverages the battle-tested save/load path) and requires zero per-operation code. But memory-hungry — each step is essentially a full scene copy. Blender mitigates this somewhat with differential steps in specific modes.

*Source: [Blender Developer Documentation — Undo System](https://developer.blender.org/docs/features/core/undo/)*

### 1.4 Wicked Engine — Archive-Based Snapshots

Wicked Engine (open-source C++ engine) uses a **serialization-based** approach with before/after state pairs:

Six operation types tracked via `HistoryOperationType`:
- `HISTORYOP_TRANSLATOR` — transform changes (before/after matrices)
- `HISTORYOP_SELECTION` — selection state changes
- `HISTORYOP_ADD` — entity creation
- `HISTORYOP_DELETE` — entity removal
- `HISTORYOP_COMPONENT_DATA` — component property modifications
- `HISTORYOP_PAINTTOOL` — paint operations

Each operation is recorded as:
1. `AdvanceHistory()` — creates a new `wi::Archive`.
2. Write operation type.
3. `RecordSelection()` + `RecordEntity()` — serialize before-state.
4. Execute the operation.
5. `RecordSelection()` + `RecordEntity()` again — serialize after-state.

Undo reads the archive backwards (restores "before"), redo reads forward (restores "after").

Key details:
- Uses a `std::vector<wi::Archive>` with an integer `historyPos` pointer.
- Stores **complete entity snapshots** per step rather than deltas — trades memory for simplicity.
- Selection is tracked separately from entity data, allowing selection-only undo.

*Source: [DeepWiki — WickedEngine Editor System](https://deepwiki.com/turanszkij/WickedEngine/10-lua-scripting)*

### 1.5 Bevy (Proposed Design) — ECS-Aware Hybrid

The Bevy engine's design document proposes an ECS-aware undo/redo system:

Five data categories: Components, Resources, Assets, Entities, Remote data.

Two competing strategies:
- **Centralized Command System**: All mutations go through `EditorCommand` structures. Full tracking, explicit cancellation. Requires code refactoring.
- **Automatic Change Detection** ("Silent Undo"): System diffs world state automatically. Minimal code changes, but complex and may miss changes.

The design uses a **DAG (Directed Acyclic Graph)** timeline model with:
- Multiple independent timelines per data type.
- Nested hierarchies for different granularity levels.
- Time-continuous collapsible changes for drag operations.

*Source: [Bevy Undo/Redo Design Document](https://hackmd.io/@bevy/r1RXR5DC0)*

---

## 2. Architectural Approaches Compared

### 2.1 Command Pattern (Explicit Commands)

How it works: Each undoable operation is an object with `execute()`, `undo()`, and optionally `redo()` methods. Commands store the minimum state needed to reverse themselves.

```
class Command {
    virtual void execute() = 0;
    virtual void undo() = 0;
};
```

**Pros:**
- Minimal memory per command (stores only changed values, not entire objects)
- Very fast undo/redo (direct value swaps)
- Clear, testable command objects
- Natural fit for the Command design pattern in OOP

**Cons:**
- Every new operation requires a new command class (or at least a new lambda)
- Easy to miss recording a change, leading to subtle bugs
- Commands that reference objects by pointer can break if objects are deleted/recreated

Used by: Godot, Qt (QUndoCommand), most custom editor implementations.

### 2.2 State Snapshots (Memento Pattern)

How it works: Before each operation, serialize the affected object(s) or the entire scene. Store these snapshots. Undo restores the previous snapshot.

**Pros:**
- No per-operation code needed — any mutation is automatically captured
- Impossible to miss a change
- Simple to implement if serialization already exists

**Cons:**
- Memory-intensive (full copies per step)
- Slow for large scenes (serialization/deserialization cost)
- Coarse granularity — hard to undo just one property within a snapshot

Used by: Blender (memfile), some simple editors.

### 2.3 Delta/Diff Approach

How it works: Compute the difference between before and after states. Store only the diff (XOR + RLE compression, or structured patches).

**Pros:**
- Much smaller memory footprint than full snapshots
- Can be data-agnostic (XOR works on any byte stream)
- Combines simplicity of snapshots with efficiency of commands

**Cons:**
- Requires sequential processing (cannot jump to arbitrary history position)
- More complex implementation than either pure commands or pure snapshots
- Compression adds CPU overhead

Advocated by: Max Liani's "Undo, the Art Of" series, Blender's differential steps.

### 2.4 Hybrid (Recommended)

Most production systems use a hybrid:
- **Command objects** for most operations (lightweight, fast)
- **State snapshots** for complex operations where computing the inverse is impractical (e.g., mesh editing, import operations)
- **Merging/coalescing** for continuous operations (drag, slider)

This is what Wicked Engine does: command-like operation types with serialized before/after snapshots.

---

## 3. Command Granularity and Batching

### 3.1 What Constitutes a Single Undoable Action?

The guiding principle from multiple sources: **an action should represent a cost to the user in precision, time, or effort** (Geometer blog). Users expect one Ctrl+Z to undo one "meaningful thing."

Examples of single undoable actions:
- Moving an entity to a new position (including drag-and-release)
- Changing a material color
- Adding a component
- Deleting an entity (including all its children)
- Reparenting an entity

NOT single actions (should be batched):
- Each frame of a gizmo drag (dozens of transform updates)
- Individual property changes when pasting a copied transform
- Multiple deletes from "Delete All Selected"

### 3.2 Continuous Drag / Gizmo Operations

This is the most commonly discussed challenge. When the user drags a transform gizmo, the engine receives many small position updates per frame. The user expects ONE undo to reverse the entire drag.

**Approach A: Begin/End bracketing**
Capture the initial transform when the mouse button goes down. Capture the final transform when the mouse button comes up. Record one command with before/after.

This is the most common approach and what ImGui recommends:
- `ImGui::IsItemActivated()` — capture "before" value
- `ImGui::IsItemDeactivatedAfterEdit()` — capture "after" value and record the command

**Approach B: Merge mode (Godot style)**
Use `MERGE_ENDS` — consecutive actions with the same name are merged, keeping the first action's undo data and the last action's do data.

**Approach C: Timer-based coalescing**
Group changes that happen within a short time window (e.g., 300ms) into a single action. Used by some text editors but less common in 3D editors.

**Recommendation for Vestige**: Approach A (begin/end bracketing) is simplest and most reliable for gizmo operations. The gizmo already knows when interaction starts and ends via ImGuizmo.

*Source: [ImGui Issue #1875 — Finaling events for Undo/Redo](https://github.com/ocornut/imgui/issues/1875), [Geometer — Undo, Redo and Units of Interaction](https://handmade.network/p/64/geometer/blog/p/3108-05._undo,_redo_and_units_of_interaction)*

### 3.3 Multi-Entity Operations

When the user moves 5 selected entities at once, this should be ONE undo step that reverses all 5 moves.

Two approaches:
- **Composite/Group command**: A single command that contains a list of sub-commands. Undo executes all sub-undos in reverse order.
- **Transaction bracketing**: Begin a transaction, record individual changes, end the transaction. The entire transaction is one undo step.

Both work. Composite commands are more explicit; transactions are more convenient for ad-hoc grouping.

---

## 4. Memory Management

### 4.1 Limiting History Size

Three strategies:

**Fixed count**: Keep the last N commands (e.g., 100 or 200). Simple to implement. Godot uses this via `max_steps`.

**Memory budget**: Track total bytes used by undo history. Evict oldest entries when budget is exceeded. More complex but prevents pathological cases (one huge operation consuming all memory).

**Hybrid**: Fixed count with a memory cap. Most practical for real-world use.

### 4.2 Handling Large Operations

Deleting 1000 entities creates a command that must store enough data to recreate all 1000. Strategies:

- **Lazy serialization**: Only serialize the deleted entities to an archive when the command is created. The archive is compact (just data, no live objects).
- **Ownership transfer**: The undo system takes ownership of the deleted entity subtrees. The entities remain in memory but are removed from the scene. Undo re-inserts them. This avoids serialization entirely but means deleted entities consume live memory until the command is evicted.
- **Reference counting with clear ownership**: As Max Liani emphasizes, deleted objects should transfer ownership to the undo system cleanly — no shared ownership or reference counting between active data and undo data.

### 4.3 Object Identity After Undo

A critical challenge: if you undo a delete (recreating an entity), the new entity has a different memory address than the original. Commands later in the stack that reference the original pointer are now dangling.

Solutions:
- **ID-based references**: Commands store entity IDs (uint32_t), not raw pointers. Look up the entity by ID when executing. This is the most robust approach. Vestige already has entity IDs (`Entity::getId()`).
- **Stable handles**: Use a handle/slot-map system where handles survive delete/recreate cycles.
- **Pointer patching**: When recreating an object, update all commands that reference it. Complex and error-prone.

**Recommendation for Vestige**: Use entity IDs exclusively in commands. Never store Entity* in a command object.

*Source: [Game Programming Patterns — Command](https://gameprogrammingpatterns.com/command.html), [Max Liani — Undo, the Art Of](https://maxliani.wordpress.com/2021/09/01/undo-the-art-of-part-1/)*

---

## 5. Specific Command Types for a Scene Editor

Based on Vestige's current architecture (`Entity`, `Component`, `Transform`, `Material`, `Selection`, hierarchy), these are the command types needed:

### 5.1 Transform Changes

```
TransformCommand:
    - entityId: uint32_t
    - oldTransform: Transform (position, rotation, scale)
    - newTransform: Transform
    - undo(): set entity transform to oldTransform
    - redo(): set entity transform to newTransform
```

For multi-entity gizmo transforms, use a composite command containing one TransformCommand per entity.

Continuous drag handling: Capture oldTransform when gizmo interaction begins (ImGuizmo starts manipulating), record newTransform when gizmo interaction ends.

### 5.2 Component Add/Remove

```
AddComponentCommand:
    - entityId: uint32_t
    - componentTypeId: uint32_t
    - componentData: serialized component state (or clone)
    - undo(): remove the component from entity
    - redo(): re-add the component with stored data

RemoveComponentCommand:
    - entityId: uint32_t
    - componentTypeId: uint32_t
    - componentData: serialized/cloned component (captured before removal)
    - undo(): re-add the component with stored data
    - redo(): remove the component
```

Note: Vestige already has `Component::clone()` which can be used to capture component state.

### 5.3 Entity Create/Delete

```
CreateEntityCommand:
    - parentId: uint32_t (0 for root)
    - entityData: serialized entity (captured after creation)
    - createdEntityId: uint32_t
    - undo(): remove entity from scene (transfer ownership to command)
    - redo(): re-insert entity (transfer ownership back to scene)

DeleteEntityCommand:
    - parentId: uint32_t
    - childIndex: int (position among siblings, for correct reinsertion)
    - entityData: the full entity subtree (owned by command after delete)
    - undo(): re-insert entity subtree at original position
    - redo(): remove entity subtree (transfer ownership to command)
```

Critical: Deleting an entity with children must preserve the entire subtree. Vestige's `Entity::clone()` or `removeChild()` (which returns `unique_ptr`) already supports ownership transfer.

### 5.4 Material Property Changes

```
MaterialPropertyCommand:
    - entityId: uint32_t (entity owning the MeshRenderer with this material)
    - propertyId: enum or string identifying which property changed
    - oldValue: variant (float, vec3, texture ptr, etc.)
    - newValue: variant
    - undo(): set property to oldValue
    - redo(): set property to newValue
```

Alternative (simpler): Snapshot the entire Material state:
```
MaterialSnapshotCommand:
    - entityId: uint32_t
    - oldMaterial: Material (full copy)
    - newMaterial: Material (full copy)
```

The snapshot approach is simpler since Material has many properties. A full Material copy is only ~200 bytes (plus shared_ptr refs to textures), so the memory cost is negligible.

### 5.5 Reparenting

```
ReparentCommand:
    - entityId: uint32_t
    - oldParentId: uint32_t
    - newParentId: uint32_t
    - oldChildIndex: int
    - newChildIndex: int (or -1 for "append")
    - undo(): move entity back to oldParent at oldChildIndex
    - redo(): move entity to newParent at newChildIndex
```

This requires detaching from the current parent (`removeChild`) and attaching to the new parent (`addChild`), preserving the entity's world transform during the move.

### 5.6 Multi-Entity Operations

```
CompositeCommand:
    - commands: std::vector<std::unique_ptr<Command>>
    - description: std::string
    - undo(): for each command in reverse order, call undo()
    - redo(): for each command in forward order, call redo()
```

Used for: multi-select delete, multi-select transform, paste multiple entities, etc.

### 5.7 Entity Property Changes

```
EntityPropertyCommand:
    - entityId: uint32_t
    - property: enum (NAME, ACTIVE, VISIBLE, LOCKED)
    - oldValue / newValue
```

### 5.8 Selection Changes (Optional)

Whether selection changes should be undoable is debated:
- **Most editors do NOT** make selection an undoable action — selection is considered navigation, not data modification.
- However, many editors **restore selection state** as part of undoing another action (e.g., undoing a delete reselects the restored entity).

Recommendation: Do NOT make selection changes undoable on their own, but DO store selection state as metadata on commands, so that undo/redo restores the selection to match the operation context.

---

## 6. C++ Implementation Patterns

### 6.1 Virtual Base Class (Classic)

```cpp
class EditorCommand
{
public:
    virtual ~EditorCommand() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual void redo() { execute(); }  // Default: redo == execute
    virtual std::string getDescription() const = 0;

    // For merging continuous operations
    virtual bool canMergeWith(const EditorCommand& other) const { return false; }
    virtual void mergeWith(const EditorCommand& other) {}
};
```

**Pros**: Type-safe, debuggable, each command is a named class with clear semantics.
**Cons**: Requires a new class (or at least a new template instantiation) per operation type.

### 6.2 std::function-Based (Modern C++)

```cpp
using Action = std::function<void()>;
using Transaction = std::pair<Action /*undo*/, Action /*redo*/>;
```

Objects return a Transaction when their state changes, capturing before/after via lambda closures.

**Pros**: No class hierarchy needed, very flexible.
**Cons**: Harder to debug (lambdas have no name), harder to implement merging, no structured way to get descriptions.

### 6.3 Template-Based Value Command (Practical Hybrid)

```cpp
template <typename T>
class PropertyCommand : public EditorCommand
{
public:
    PropertyCommand(std::string desc, T* target, T oldVal, T newVal)
        : m_description(std::move(desc))
        , m_target(target)
        , m_oldValue(std::move(oldVal))
        , m_newValue(std::move(newVal))
    {}

    void execute() override { *m_target = m_newValue; }
    void undo() override { *m_target = m_oldValue; }
    std::string getDescription() const override { return m_description; }

private:
    std::string m_description;
    T* m_target;
    T m_oldValue;
    T m_newValue;
};
```

**WARNING**: Storing raw pointers to values (`T* target`) is dangerous — the pointer can be invalidated if the owning object is destroyed or moved. Prefer ID-based lookup.

### 6.4 ID-Based Safe Pattern (Recommended for Vestige)

```cpp
class TransformCommand : public EditorCommand
{
public:
    TransformCommand(uint32_t entityId, Transform oldT, Transform newT)
        : m_entityId(entityId)
        , m_oldTransform(std::move(oldT))
        , m_newTransform(std::move(newT))
    {}

    void execute() override
    {
        if (Entity* e = findEntityById(m_entityId))
            e->transform = m_newTransform;
    }

    void undo() override
    {
        if (Entity* e = findEntityById(m_entityId))
            e->transform = m_oldTransform;
    }

    std::string getDescription() const override
    {
        return "Transform Entity";
    }

private:
    uint32_t m_entityId;
    Transform m_oldTransform;
    Transform m_newTransform;
};
```

This pattern is safe against entity destruction/recreation because it looks up by ID every time.

### 6.5 The Command History Manager

```cpp
class CommandHistory
{
public:
    void execute(std::unique_ptr<EditorCommand> cmd)
    {
        cmd->execute();
        // Discard any redo history beyond current position
        m_commands.erase(m_commands.begin() + m_currentIndex + 1, m_commands.end());
        m_commands.push_back(std::move(cmd));
        m_currentIndex = static_cast<int>(m_commands.size()) - 1;
        enforceLimit();
    }

    void undo()
    {
        if (m_currentIndex >= 0)
        {
            m_commands[m_currentIndex]->undo();
            m_currentIndex--;
        }
    }

    void redo()
    {
        if (m_currentIndex < static_cast<int>(m_commands.size()) - 1)
        {
            m_currentIndex++;
            m_commands[m_currentIndex]->execute();
        }
    }

    void clear() { m_commands.clear(); m_currentIndex = -1; }
    bool canUndo() const { return m_currentIndex >= 0; }
    bool canRedo() const { return m_currentIndex < (int)m_commands.size() - 1; }

private:
    void enforceLimit()
    {
        while (m_commands.size() > m_maxCommands)
        {
            m_commands.erase(m_commands.begin());
            m_currentIndex--;
        }
    }

    std::vector<std::unique_ptr<EditorCommand>> m_commands;
    int m_currentIndex = -1;
    size_t m_maxCommands = 200;
};
```

### 6.6 RAII Considerations

- Commands that own entity data (from deletions) must properly clean up in their destructors.
- `std::unique_ptr<Entity>` in delete commands handles this automatically.
- When a delete command is evicted from history (oldest command removed), its destructor frees the owned entity data.
- Commands should be move-only (delete copy constructor/assignment).

*Source: [Game Programming Patterns — Command](https://gameprogrammingpatterns.com/command.html), [undoredo-cpp](https://github.com/d-led/undoredo-cpp), [undo-cxx](https://github.com/hedzr/undo-cxx), [Meld Studio — C++ Undo Redo Frameworks](https://meldstudio.co/blog/c-undo-redo-frameworks-part-1/), [RandomMonkeyWorks — Undo/Redo in C++](https://www.randommonkeyworks.com/undoredo-in-c/)*

---

## 7. Undo History UI

### 7.1 How Editors Display History

**Adobe (Photoshop, Illustrator, InDesign)**: A History panel showing a scrollable list of actions. Each entry has a descriptive name and icon. Clicking any entry in the list jumps to that state (undoing everything after it). The current position is highlighted.

**Blender**: Undo History accessible via Edit menu. Shows a numbered list of action names. Selecting an entry jumps to that state.

**Unity**: No visual history panel by default — only Ctrl+Z/Y. Third-party plugins add history panels.

**Wicked Engine**: Tracks selection state alongside operations, allowing the UI to show what was affected.

### 7.2 Recommended UI for Vestige

A simple **History Panel** in the editor:

```
+------------------------+
| History                |
+------------------------+
| > Transform "Wall_01"  |   <-- current position (highlighted)
|   Delete "Light_03"    |   <-- grayed out (in redo territory)
|   Material "Brick"     |
+------------------------+
|   Create "Pillar_04"   |
|   Transform "Floor"    |
|   Reparent "Lamp"      |
|   [Start of History]   |
+------------------------+
```

Features:
- Each entry shows the command description (from `getDescription()`).
- Current position is visually highlighted.
- Entries below current are grayed (available for redo).
- Clicking an entry jumps to that state (executing multiple undos/redos).
- Tooltip shows additional detail (e.g., entity name, old/new values).
- The panel should show the most recent action at the top.

### 7.3 Status Bar Integration

Show brief undo/redo state in the status bar:
- "Undo: Transform Entity (23 more)" / "Redo: Delete Entity (5 more)"
- Or simply show the last action name next to the Ctrl+Z shortcut hint.

*Source: [Adobe — Undo edits and manage history panel](https://helpx.adobe.com/indesign/using/undo-history-panel.html), [Wayline — Undo/Redo for Level Design](https://www.wayline.io/blog/undo-redo-crucial-level-design), [Elementor History](https://elementor.com/blog/undo-redo-history/)*

---

## 8. Integration Challenges

### 8.1 Scene Save/Load

**Dirty flag**: The undo system should track a "saved version" — a marker at the point when the scene was last saved. If the current position differs from the saved position, the scene is "dirty" (has unsaved changes). Godot does this with an auto-incrementing version number on each action commit.

Implementation:
```cpp
// In CommandHistory:
int m_savedIndex = -1;  // Index at last save

void markSaved() { m_savedIndex = m_currentIndex; }
bool isDirty() const { return m_currentIndex != m_savedIndex; }
```

**Clearing history on load**: When loading a new scene, clear the entire undo history. The loaded state becomes the new baseline.

**Edge case**: If the user undoes past the save point, the scene is dirty again (it differs from what was saved). The dirty flag must handle both directions.

### 8.2 Selection Interaction

Key principles:
- Selection changes are NOT undoable operations themselves.
- BUT, commands should restore selection as a side effect of undo/redo.
- Undoing a delete should reselect the restored entity.
- Undoing a create should clear the selection (or select the parent).

Implementation: Store the selection state (vector of entity IDs) as metadata on each command. On undo, restore the pre-command selection. On redo, restore the post-command selection.

### 8.3 Renderer State

Undo/redo of material changes or entity visibility must immediately update the rendered scene. Since Vestige uses a real-time WYSIWYG editor, this should happen naturally — the renderer reads current entity/material state each frame.

Potential issues:
- GPU resources (textures, buffers) might need updating if undo changes a texture assignment.
- Shadow maps may need re-rendering if an entity's transform or visibility changes.
- The material preview sphere should update when a material undo occurs.

These should be handled by the existing render pipeline (it already re-reads state each frame), not by the undo system.

### 8.4 Entity ID Stability

Commands reference entities by ID. Vestige uses a monotonically incrementing `s_nextId`. When an entity is deleted and then recreated via undo, it must get the **same ID** back, or all subsequent commands referencing that ID will break.

Options:
- **Ownership transfer**: Don't destroy the entity on delete — move it out of the scene but keep it alive in the command. Re-insert on undo. The ID never changes because the object is never destroyed. This is the simplest and most robust approach.
- **ID reservation**: When recreating, force-assign the original ID. Requires modifying Entity to accept a specific ID.

Recommendation: Ownership transfer. `Entity::removeChild()` already returns `unique_ptr`, so the command can hold it.

### 8.5 Event Bus Integration

Some undo/redo operations may need to fire events (e.g., `EntityDeleted`, `EntityCreated`) so other subsystems can react. The undo system should fire the same events that would fire during normal operation. This means undo of a delete fires `EntityCreated`, and undo of a create fires `EntityDeleted`.

---

## 9. Recommendations for Vestige

Based on this research, here is the recommended approach for Vestige:

### Architecture
- **Hybrid Command Pattern**: Virtual base class `EditorCommand` with concrete subclasses for each operation type.
- **ID-based references**: All commands reference entities by `uint32_t` ID, never by pointer.
- **Ownership transfer** for entity create/delete: The command holds the `unique_ptr<Entity>` when the entity is "deleted," and transfers it back on undo.
- **Composite commands** for multi-entity operations.

### Command Types (Priority Order)
1. `TransformCommand` — single entity transform change
2. `CompositeCommand` — wraps multiple commands as one action
3. `CreateEntityCommand` / `DeleteEntityCommand` — with full subtree ownership
4. `MaterialPropertyCommand` — full material snapshot (simpler than per-property)
5. `AddComponentCommand` / `RemoveComponentCommand` — using Component::clone()
6. `ReparentCommand` — entity hierarchy changes
7. `EntityPropertyCommand` — name, active, visible, locked changes

### Gizmo Integration
- Capture `oldTransform` when `ImGuizmo::IsUsing()` transitions from false to true.
- Capture `newTransform` when `ImGuizmo::IsUsing()` transitions from true to false.
- Create one `TransformCommand` (or `CompositeCommand` for multi-select) per drag.

### Memory
- Fixed history limit of 200 commands (configurable).
- Oldest commands evicted when limit exceeded.
- Entity subtrees owned by delete commands are freed when the command is evicted.

### UI
- History panel (dockable, like other editor panels).
- Each entry shows command description.
- Click to jump to any point in history.
- Keyboard shortcuts: Ctrl+Z (undo), Ctrl+Y or Ctrl+Shift+Z (redo).

### Save Integration
- Track saved-index for dirty detection.
- Clear history on scene load.
- Show asterisk (*) in title bar when dirty.

---

## 10. Sources

### Engine Documentation and Architecture
- [Unreal Engine Forum — How does the transaction system work?](https://forums.unrealengine.com/t/how-does-the-transaction-undo-redo-system-work/355792)
- [Implement Undo and Redo in Unreal Engine — Robert Lewicki](https://unreal.robertlewicki.games/p/implement-undo-and-redo-in-unreal-engine)
- [Godot UndoRedo Class Documentation](https://docs.godotengine.org/en/stable/classes/class_undoredo.html)
- [Godot EditorUndoRedoManager Documentation](https://docs.godotengine.org/en/stable/classes/class_editorundoredomanager.html)
- [Blender Developer Documentation — Undo System](https://developer.blender.org/docs/features/core/undo/)
- [Bevy Undo/Redo Design Document](https://hackmd.io/@bevy/r1RXR5DC0)
- [DeepWiki — WickedEngine Editor System](https://deepwiki.com/turanszkij/WickedEngine)

### Design Patterns and Implementation
- [Game Programming Patterns — Command Pattern](https://gameprogrammingpatterns.com/command.html)
- [Max Liani — Undo, the Art Of (Part 1)](https://maxliani.wordpress.com/2021/09/01/undo-the-art-of-part-1/)
- [Meld Studio — C++ Undo Redo Frameworks (Part 1)](https://meldstudio.co/blog/c-undo-redo-frameworks-part-1/)
- [Geometer Blog — Undo, Redo and Units of Interaction](https://handmade.network/p/64/geometer/blog/p/3108-05._undo,_redo_and_units_of_interaction)
- [Gernot Klingler — Implementing undo/redo with the Command Pattern](https://gernotklingler.com/blog/implementing-undoredo-with-the-command-pattern/)
- [RandomMonkeyWorks — Undo/Redo in C++](https://www.randommonkeyworks.com/undoredo-in-c/)

### C++ Libraries
- [undoredo-cpp — Header-only C++ undo/redo concepts](https://github.com/d-led/undoredo-cpp)
- [undo-cxx — C++17 undo/redo subsystem](https://github.com/hedzr/undo-cxx)
- [dacap/undo — Non-linear undo/redo](https://github.com/dacap/undo)

### UI and UX
- [ImGui Issue #1875 — Finaling events for Undo/Redo systems](https://github.com/ocornut/imgui/issues/1875)
- [Adobe — Undo edits and manage history panel](https://helpx.adobe.com/indesign/using/undo-history-panel.html)
- [Wayline — Why Undo/Redo is Crucial for Level Design](https://www.wayline.io/blog/undo-redo-crucial-level-design)
- [Wolfire Games — How We Implement Undo](https://www.wolfire.com/blog/2009/02/how-we-implement-undo/)

### Editor Integration
- [Unity Editor Scripting — Dirty Objects and Recording Undo](https://medium.com/@dilaura_exp/unity-editor-scripting-series-chapter-11-dirty-objects-recording-undo-205cd2d077dc)
- [GameDev.net — Custom editor undo/redo system](https://gamedev.net/forums/topic/678496-custom-editor-undoredo-system/)
- [Wikipedia — Undo](https://en.wikipedia.org/wiki/Undo)
