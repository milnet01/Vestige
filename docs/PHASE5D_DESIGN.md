# Phase 5D: Scene Persistence — Design Document

## Goal

Enable saving and loading of complete scene state so that all editor work persists between sessions. Add an undo/redo system for non-destructive editing, and provide file menu operations for project/scene management.

**Milestone:** The user can create a scene, save it, close the editor, reopen it, and find everything exactly as they left it — including the ability to undo/redo all actions within a session.

---

## Current State (End of Phase 5C)

The editor now has:
- **Complete content pipeline:** Asset browser → drag textures → material editor → preview sphere → viewport
- **Scene construction:** Primitives, model import, duplicate/delete, grouping, reparenting
- **Light editing:** Range-based attenuation, color temperature, viewport gizmos (point sphere, spot cone, directional arrows)
- **Prefab system:** Entity subtrees saved/loaded as JSON via `EntitySerializer`
- **Entity serialization:** Full `serializeEntity()`/`deserializeEntity()` for all component types (already handles transforms, MeshRenderer, all light types, materials with texture paths)

**What's missing:**
- No scene save/load — **all work is lost on exit**
- No undo/redo — mistakes are permanent
- No file menu (New/Open/Save/Save As)
- No auto-save safety net
- No unsaved-changes warning on exit
- No project file structure
- No recent files list

---

## Research Summary

Three research documents were produced (see `docs/` folder):
- `SCENE_SERIALIZATION_RESEARCH.md` — JSON format, asset references, versioning, component serialization, auto-save
- `UNDO_REDO_RESEARCH.md` — Command pattern, major engine approaches (Unreal, Godot, Blender, Wicked Engine), gizmo integration, memory management
- `PROJECT_MANAGEMENT_RESEARCH.md` — Project files, file dialogs, recent files, file watching, dirty flag, cross-platform paths

### Key Design Decisions from Research

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Scene format | JSON with flat entity list + parent IDs | Aligns with ECS flat storage; easy VCS diffs |
| Asset references | Relative paths from project root | Simple, debuggable; UUIDs premature for current scale |
| JSON library | nlohmann/json (already integrated v3.12.0) | Excellent API; 12ms for 1MB scene — adequate for editor I/O |
| Scene versioning | Integer `format_version` with chained migration functions | Simple; O3DE-inspired but without reflection overhead |
| Component serialization | Existing `EntitySerializer` pattern (already handles all types) | Proven code; extend, don't replace |
| Undo architecture | Hybrid command pattern with ID-based entity references | Lightweight per-command; safe against entity destruction/recreation |
| Entity ownership on delete | Ownership transfer (`unique_ptr` held by command) | Avoids serialization; Entity ID remains stable |
| Gizmo drag → single undo | Begin/end bracketing (ImGuizmo interaction start/stop) | Simplest; ImGuizmo already reports interaction state |
| Undo history limit | 200 commands (fixed count) | Sufficient depth; simple implementation |
| Dirty detection | Version counter tied to undo stack (Godot-style) | Correctly handles "undo back to saved state = clean" |
| File dialogs | nativefiledialog-extended (NFDe) | Best GLFW integration; native OS look; UTF-8; Zlib license |
| Confirmation dialogs | ImGui modal popups | Consistent with editor UI; no external dependency |
| Auto-save | Timer + dirty flag; serialize on main thread, write on background thread | Sub-ms serialization; no frame hitch from disk I/O |
| Crash safety | Atomic writes (temp file + rename) | POSIX rename is atomic; protects against mid-write crashes |
| Recent files storage | XDG config dir (Linux) / AppData (Windows) | Platform convention; 10 entries |
| Path handling | `std::filesystem::path` (C++17) with forward slashes in project files | Standard library; cross-platform |

---

## Architecture Overview

Phase 5D adds four new subsystems to the editor:

```
+------------------------------------------------------------------+
|                            Editor                                 |
|                                                                   |
|  +------------------+  +-------------------+  +-----------------+ |
|  | SceneSerializer  |  | CommandHistory    |  | ProjectManager  | |
|  | (save/load JSON, |  | (undo/redo stack, |  | (project file,  | |
|  |  auto-save,      |  |  command objects,  |  |  recent files,  | |
|  |  crash recovery)  |  |  dirty tracking)  |  |  file menu)    | |
|  +------------------+  +-------------------+  +-----------------+ |
|                                                                   |
|  +------------------+                                             |
|  | HistoryPanel     |                                             |
|  | (undo list UI,   |                                             |
|  |  click-to-jump)  |                                             |
|  +------------------+                                             |
|                                                                   |
|  Existing: Hierarchy, Inspector, Viewport, AssetBrowser, Prefabs  |
+------------------------------------------------------------------+
```

### Data Flow

```
User Action → Create EditorCommand → CommandHistory::execute()
                                          │
                                    ┌─────┴─────┐
                                    │ cmd->execute() │
                                    │ push to stack  │
                                    │ increment ver  │
                                    └─────┬─────┘
                                          │
                              isDirty() = (version != savedVersion)
                                          │
                              Title bar: *scene_name.scene
```

```
Ctrl+S → SceneSerializer::saveScene()
              │
              ├─ Iterate root entity children
              ├─ EntitySerializer::serializeEntity() per entity
              ├─ Wrap in scene envelope (metadata, format_version)
              ├─ Write JSON string to temp file
              ├─ Atomic rename temp → target
              └─ CommandHistory::markSaved()
```

---

## Scene File Format

### `.scene` File Structure

```json
{
    "vestige_scene": {
        "format_version": 1,
        "name": "Temple Interior",
        "description": "Holy of Holies chamber",
        "author": "",
        "created": "2026-03-20T14:30:00Z",
        "modified": "2026-03-20T16:45:00Z",
        "engine_version": "0.5.0"
    },
    "environment": {
        "ambient_color": [0.03, 0.03, 0.04],
        "skybox_texture": ""
    },
    "entities": [
        {
            "id": 1,
            "name": "Floor",
            "parent_id": 0,
            "transform": {
                "position": [0.0, 0.0, 0.0],
                "rotation": [0.0, 0.0, 0.0],
                "scale": [10.0, 0.1, 10.0]
            },
            "visible": true,
            "locked": false,
            "active": true,
            "components": {
                "MeshRenderer": {
                    "mesh": "builtin:cube",
                    "material": {
                        "type": "PBR",
                        "albedo": [0.8, 0.7, 0.6],
                        "metallic": 0.0,
                        "roughness": 0.8,
                        "textures": {
                            "albedo": "assets/textures/stone_diffuse.png",
                            "normal": "assets/textures/stone_normal.png"
                        }
                    },
                    "casts_shadow": true
                }
            }
        },
        {
            "id": 2,
            "name": "Sun",
            "parent_id": 0,
            "transform": {
                "position": [0.0, 10.0, 0.0],
                "rotation": [-45.0, 30.0, 0.0],
                "scale": [1.0, 1.0, 1.0]
            },
            "visible": true,
            "locked": false,
            "active": true,
            "components": {
                "DirectionalLight": {
                    "direction": [-0.5, -0.7, -0.5],
                    "ambient": [0.05, 0.05, 0.06],
                    "diffuse": [1.0, 0.95, 0.85],
                    "specular": [1.0, 0.95, 0.85],
                    "intensity": 1.0,
                    "cast_shadows": true,
                    "shadow_config": {
                        "cascade_count": 4,
                        "shadow_distance": 100.0,
                        "resolution": 2048
                    }
                }
            }
        }
    ]
}
```

### Design Notes

- **Flat entity list with `parent_id`:** `parent_id: 0` means direct child of root (root is implicit, never serialized). This matches the existing `Entity::m_parent` pattern and is easy to diff in VCS.
- **Component key = type name string:** Maps directly to existing `EntitySerializer` code which already switches on component type names.
- **Asset paths are relative:** `assets/textures/stone_diffuse.png` relative to project root. Resolved at load time via `std::filesystem::path(projectRoot) / relativePath`.
- **Entity IDs are local to the file:** On load, entities get fresh runtime IDs (from `Entity::s_nextId`). A mapping from file-ID → runtime-ID is maintained during deserialization to reconstruct parent-child links.
- **Default values omitted:** Following Godot's convention, properties equal to their default values may be omitted to keep files compact. The deserializer uses `json.value("key", default)` for safe fallback.

### Version Migration

```cpp
// Scene format evolution
// v1: Initial format (Phase 5D)
// v2: (future) Add physics components
// v3: (future) Add animation data

nlohmann::json migrateScene(nlohmann::json scene, int fromVersion, int toVersion)
{
    if (fromVersion < 1 || fromVersion > toVersion)
        throw std::runtime_error("Unsupported scene version");

    // Chain migrations sequentially
    if (fromVersion < 2)
        scene = migrateV1toV2(scene);
    if (fromVersion < 3)
        scene = migrateV2toV3(scene);
    // ... add new migrations here

    return scene;
}
```

---

## Undo/Redo System

### Command Base Class

```cpp
class EditorCommand
{
public:
    virtual ~EditorCommand() = default;

    virtual void execute() = 0;     // Apply the change (first time or redo)
    virtual void undo() = 0;        // Reverse the change

    virtual std::string getDescription() const = 0;

    // For merging continuous operations (e.g., slider drags)
    virtual bool canMergeWith(const EditorCommand& other) const { return false; }
    virtual void mergeWith(EditorCommand& other) {}
};
```

### Command Types (Implementation Priority)

| # | Command | Stored State | Trigger |
|---|---------|-------------|---------|
| 1 | `TransformCommand` | entityId, oldTransform, newTransform | Gizmo drag end, inspector edit |
| 2 | `CompositeCommand` | vector of sub-commands | Multi-select operations |
| 3 | `CreateEntityCommand` | owned Entity subtree, parentId | Create menu, duplicate, paste |
| 4 | `DeleteEntityCommand` | owned Entity subtree, parentId, siblingIndex | Delete key, context menu |
| 5 | `MaterialPropertyCommand` | entityId, oldMaterial (snapshot), newMaterial (snapshot) | Inspector material edits |
| 6 | `ReparentCommand` | entityId, oldParentId, newParentId, oldIndex, newIndex | Hierarchy drag-drop |
| 7 | `EntityPropertyCommand` | entityId, property enum, oldValue, newValue | Name/visible/locked/active changes |
| 8 | `AddComponentCommand` | entityId, componentTypeId, cloned component data | Inspector "Add Component" |
| 9 | `RemoveComponentCommand` | entityId, componentTypeId, cloned component data | Inspector "Remove Component" |

### Command History Manager

```cpp
class CommandHistory
{
public:
    void execute(std::unique_ptr<EditorCommand> cmd);  // Execute + push
    void undo();                                        // Step back
    void redo();                                        // Step forward
    void clear();                                       // Reset (on scene load)

    bool canUndo() const;
    bool canRedo() const;
    bool isDirty() const;     // version != savedVersion
    void markSaved();         // Record current position as saved

    const std::vector<std::unique_ptr<EditorCommand>>& getCommands() const;
    int getCurrentIndex() const;

private:
    std::vector<std::unique_ptr<EditorCommand>> m_commands;
    int m_currentIndex = -1;
    int m_savedIndex = -1;
    size_t m_maxCommands = 200;
};
```

### Critical Design Rules

1. **ID-based references only.** Commands store `uint32_t entityId`, never `Entity*`. Look up by ID at execute/undo time via `scene->findEntityById()`.

2. **Ownership transfer for create/delete.** `DeleteEntityCommand` takes ownership of the removed `unique_ptr<Entity>` subtree. Undo re-inserts it (same object, same ID). No serialization needed.

3. **Gizmo begin/end bracketing.** Capture `oldTransform` when `ImGuizmo::IsUsing()` goes false→true. Record `TransformCommand` when it goes true→false. One drag = one undo step.

4. **Inspector widget bracketing.** Use `ImGui::IsItemActivated()` to capture old value, `ImGui::IsItemDeactivatedAfterEdit()` to record the command.

5. **New command after undo discards redo history.** If the user undoes 3 times then makes a new change, the 3 undone commands are permanently discarded.

6. **Selection is NOT undoable.** But commands restore selection as a side effect (e.g., undo-delete reselects the entity).

7. **Events fire normally.** Undo of delete fires `EntityCreated` event, undo of create fires `EntityDeleted` event — other subsystems react as usual.

---

## Project Management

### File Menu

```
File
├── New Scene          Ctrl+N     Create empty scene (prompt save if dirty)
├── Open Scene...      Ctrl+O     NFDe open dialog, filtered to *.scene
├── ─────────────
├── Save Scene         Ctrl+S     Save to current path (Save As if untitled)
├── Save Scene As...   Ctrl+Shift+S   NFDe save dialog
├── ─────────────
├── Recent Scenes  ►   Submenu with last 10 scenes
├── ─────────────
└── Quit               Ctrl+Q     Prompt save if dirty, then exit
```

### Unsaved Changes Dialog

ImGui modal popup:
```
┌─────────────────────────────────────────────┐
│  Unsaved Changes                            │
│                                             │
│  "temple_interior.scene" has been modified. │
│  Save changes before closing?               │
│                                             │
│  [Don't Save]         [Cancel]    [Save]    │
└─────────────────────────────────────────────┘
```

- **Save:** Save → proceed with original action
- **Don't Save:** Discard → proceed
- **Cancel:** Abort, return to editing
- Title bar shows `*scene_name.scene - Vestige` when dirty

### Recent Files

- Stored in `~/.config/vestige/recent.json` (Linux) / `%APPDATA%\Vestige\recent.json` (Windows)
- 10 entries maximum, most-recent first
- Absolute paths (machine-specific, not in VCS)
- Validate existence before display; gray out missing files
- Each entry: `{ "path": "...", "last_opened": "2026-03-20T14:30:00Z" }`

### Auto-Save

1. Timer checks every **120 seconds** (2 minutes)
2. If scene is dirty, serialize JSON string on main thread (sub-ms for typical scenes)
3. Write to `<scene_name>.autosave.scene` on a background thread via `std::async`
4. Use atomic write: write to `.tmp`, then `std::filesystem::rename()`
5. On startup: if autosave is newer than scene file, offer recovery dialog
6. Delete autosave file after successful explicit save

---

## New External Dependency

### nativefiledialog-extended (NFDe)

- **Purpose:** Native OS file open/save dialogs
- **License:** Zlib (permissive, compatible)
- **Repository:** https://github.com/btzy/nativefiledialog-extended
- **Linux dependency:** `libgtk-3-dev` (GTK3 backend) or `libdbus-1-dev` (Portal backend)
- **Integration:** `add_subdirectory()` in `external/CMakeLists.txt`, link via `target_link_libraries(... nfd)`
- **GLFW integration:** `NFD_GetNativeWindowFromGLFWWindow()` for proper dialog parenting

---

## Implementation Plan — Sub-Phases

### 5D-1: Scene Serializer (Foundation)
**New files:** `engine/editor/scene_serializer.h`, `engine/editor/scene_serializer.cpp`

Implement:
- `saveScene(const Scene& scene, const std::filesystem::path& path)` — Iterate root children, call existing `EntitySerializer::serializeEntity()` for each, wrap in scene envelope JSON, atomic write
- `loadScene(Scene& scene, const std::filesystem::path& path, ResourceManager& resources)` — Parse JSON, validate format_version, deserialize entities with ID mapping, reconstruct parent-child hierarchy
- Scene metadata fields (name, description, timestamps)
- Format version checking with migration stub
- Error handling: parse errors, missing assets (warn and continue), version mismatch

**Testing:** Save a demo scene → close → reload → verify all entities, transforms, components, materials, and hierarchy match. Unit tests for round-trip serialization.

### 5D-2: File Menu & Dialogs
**New files:** `engine/editor/file_menu.h`, `engine/editor/file_menu.cpp`
**New dependency:** nativefiledialog-extended

Implement:
- ImGui main menu bar with File menu (New/Open/Save/Save As/Quit)
- Keyboard shortcuts (Ctrl+N/O/S/Shift+S/Q)
- NFDe integration for Open/Save dialogs with `.scene` filter
- Unsaved changes modal (ImGui popup): Save / Don't Save / Cancel
- Current scene path tracking in Editor
- Title bar update: `scene_name.scene - Vestige` (with `*` when dirty)
- GLFW window close callback → trigger unsaved changes check

**Testing:** Full workflow: New → build scene → Save As → Close → Open → verify. Test unsaved changes dialog with all three buttons.

### 5D-3: Undo/Redo Core
**New files:** `engine/editor/command_history.h`, `engine/editor/command_history.cpp`, `engine/editor/commands/editor_command.h`, `engine/editor/commands/transform_command.h`, `engine/editor/commands/composite_command.h`, `engine/editor/commands/create_entity_command.h`, `engine/editor/commands/delete_entity_command.h`

Implement:
- `EditorCommand` base class
- `CommandHistory` manager (execute, undo, redo, dirty tracking, saved index)
- `TransformCommand` — first command type, with begin/end gizmo bracketing
- `CompositeCommand` — for multi-select transforms
- `CreateEntityCommand` / `DeleteEntityCommand` — with ownership transfer
- Ctrl+Z / Ctrl+Y keyboard bindings
- Wire into existing gizmo code in `Editor::update()` and `ViewportPanel`

**Testing:** Transform an entity → undo → verify position restored. Delete → undo → verify entity reappears with same components. Multi-select transform → single undo.

### 5D-4: Remaining Commands & History Panel
**New files:** `engine/editor/commands/material_command.h`, `engine/editor/commands/reparent_command.h`, `engine/editor/commands/entity_property_command.h`, `engine/editor/commands/component_command.h`, `engine/editor/panels/history_panel.h`, `engine/editor/panels/history_panel.cpp`

Implement:
- `MaterialPropertyCommand` — full material snapshot before/after
- `ReparentCommand` — old/new parent + sibling index
- `EntityPropertyCommand` — name, visible, locked, active changes
- `AddComponentCommand` / `RemoveComponentCommand` — using `Component::clone()`
- Wire all inspector edits through commands
- History panel: scrollable list, current position highlighted, click-to-jump, grayed redo territory

**Testing:** Visual test: modify material → undo → verify render matches. Reparent in hierarchy → undo → verify tree structure. History panel click-to-jump.

### 5D-5: Auto-Save, Recent Files & Polish
**Modify:** `engine/editor/scene_serializer.cpp`, `engine/editor/editor.cpp`
**New files:** `engine/editor/recent_files.h`, `engine/editor/recent_files.cpp`

Implement:
- Auto-save timer (120s) + dirty flag + background thread write
- Atomic file writes (temp + rename pattern)
- Crash recovery dialog on startup (detect stale autosave)
- Recent files manager (load/save to XDG config dir)
- Recent files submenu in File menu
- Delete autosave on successful explicit save
- `Ctrl+Shift+Z` as additional redo binding (alongside Ctrl+Y)

**Testing:** Auto-save fires after edit + wait 2min. Kill process → relaunch → recovery dialog appears. Recent files persist across sessions.

---

## File Inventory

### New Files (14 files)

| File | Purpose |
|------|---------|
| `engine/editor/scene_serializer.h` | Scene save/load API |
| `engine/editor/scene_serializer.cpp` | Scene serialization implementation |
| `engine/editor/file_menu.h` | File menu bar and dialogs |
| `engine/editor/file_menu.cpp` | File menu implementation |
| `engine/editor/command_history.h` | Undo/redo stack manager |
| `engine/editor/command_history.cpp` | Command history implementation |
| `engine/editor/commands/editor_command.h` | Abstract command base class |
| `engine/editor/commands/transform_command.h` | Transform undo/redo |
| `engine/editor/commands/composite_command.h` | Multi-command grouping |
| `engine/editor/commands/create_entity_command.h` | Entity creation undo |
| `engine/editor/commands/delete_entity_command.h` | Entity deletion undo |
| `engine/editor/commands/material_command.h` | Material property undo |
| `engine/editor/commands/reparent_command.h` | Hierarchy reparent undo |
| `engine/editor/commands/entity_property_command.h` | Name/visible/locked undo |
| `engine/editor/commands/component_command.h` | Component add/remove undo |
| `engine/editor/panels/history_panel.h` | Undo history UI |
| `engine/editor/panels/history_panel.cpp` | History panel implementation |
| `engine/editor/recent_files.h` | Recent files manager |
| `engine/editor/recent_files.cpp` | Recent files implementation |

### Modified Files

| File | Changes |
|------|---------|
| `engine/editor/editor.h/cpp` | Add CommandHistory, SceneSerializer, FileMenu, HistoryPanel; wire shortcuts; auto-save timer |
| `engine/editor/panels/viewport_panel.cpp` | Gizmo begin/end bracketing for TransformCommand |
| `engine/editor/panels/inspector_panel.cpp` | Wire property edits through commands |
| `engine/editor/panels/hierarchy_panel.cpp` | Wire reparent, rename, visibility/lock through commands |
| `engine/editor/entity_actions.cpp` | Wire duplicate/delete through commands |
| `engine/editor/entity_factory.cpp` | Wire create through commands |
| `engine/scene/entity.h/cpp` | Add `removeChild()` returning `unique_ptr` if not already available; ensure ID stability |
| `external/CMakeLists.txt` | Add nativefiledialog-extended |
| `CMakeLists.txt` | Link NFDe; add new source files |

---

## Performance Considerations

| Operation | Expected Time | Strategy |
|-----------|--------------|----------|
| Scene save (typical 100 entities) | <5ms serialize + async disk write | No frame hitch |
| Scene load (typical 100 entities) | ~50ms (JSON parse + entity creation + texture reload) | Acceptable one-time cost |
| Undo/redo execution | <1ms (direct value swap or entity re-insert) | Imperceptible |
| Command memory (200 commands) | ~50KB typical (transforms are 72 bytes each) | Negligible |
| Delete command with 100-entity subtree | ~200KB (entity objects held in memory) | Acceptable; freed on eviction |
| Auto-save serialization | <1ms for main-thread JSON generation | No frame impact |
| NFDe dialog open | Native OS dialog; blocks main thread | Expected by users |

---

## Accessibility Considerations

- **Keyboard-first workflow:** All file operations accessible via shortcuts (Ctrl+N/O/S/Q/Z/Y)
- **Title bar dirty indicator:** `*` prefix provides continuous visual feedback
- **Undo descriptions:** Clear, concise text ("Transform 'Wall_01'", "Delete 'Light_03'") for history panel
- **Auto-save:** Protects against accidental data loss without requiring user action
- **Crash recovery:** Automatic detection and recovery dialog on startup

---

## Sources

Full citations in the research documents:
- `docs/SCENE_SERIALIZATION_RESEARCH.md` — 25+ sources on format design, JSON libraries, versioning, auto-save
- `docs/UNDO_REDO_RESEARCH.md` — 25+ sources on command pattern, engine implementations, memory management
- `docs/PROJECT_MANAGEMENT_RESEARCH.md` — 40+ sources on project structure, file dialogs, file watching, dirty flags

Key references:
- Unity YAML scene format — flat object list with fileID references
- Godot UndoRedo — explicit action registration with merge modes
- Blender undo — memfile state snapshots (informed our choice to NOT use this approach)
- Wicked Engine — closest open-source analog; archive-based before/after snapshots
- Game Programming Patterns (Robert Nystrom) — Command pattern, Dirty Flag pattern
- nlohmann/json v3.12.0 — already integrated; `NLOHMANN_DEFINE_TYPE_INTRUSIVE` for component fields
- nativefiledialog-extended — Zlib license, GLFW integration via `nfd_glfw3.h`
