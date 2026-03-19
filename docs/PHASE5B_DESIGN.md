# Phase 5B: Scene Construction — Design Document

## Goal
Place and arrange objects to build rooms, walls, floors, and architectural structures — entirely through the editor UI. By the end of this phase, the user can construct a basic room with furniture-scale objects, import external models, and manage a hierarchy of entities without writing code.

**Milestone:** A working scene construction toolkit with primitive spawning, object management (duplicate/delete/group/lock/hide), glTF import dialog, and a prefab system for reusable entity templates.

---

## Current State (End of Phase 5A)

The editor now has:
- **ImGui docking workspace** with Viewport, Hierarchy, Inspector, and Console panels
- **Editor camera** with orbit/pan/zoom (Alt+LMB, MMB, scroll)
- **Mouse picking** via ID buffer with async PBO readback
- **Selection system** with single/multi-select (Shift/Ctrl+click)
- **Transform gizmos** via ImGuizmo (Translate/Rotate/Scale, W/E/R hotkeys, snap)
- **Hierarchy panel** with tree view, drag-reparent, rename (F2), delete, right-click menu
- **Inspector panel** with transform, material, light, and component editing
- **Edit/Play mode toggle** (Escape key)
- **Create menu** with placeholder entries (Empty Entity, Primitives, Lights — all disabled)

**What's missing for scene construction:**
- Only cube and plane meshes exist as built-in primitives
- Create menu items are wired up but disabled (no implementation)
- No duplicate, group, lock, or hide functionality
- No file browser / model import dialog
- No prefab system
- No undo/redo (planned for Phase 5D — design with it in mind)

---

## Architecture Overview

Phase 5B extends the existing editor with three new subsystems:

```
┌─────────────────────────────────────────────────────┐
│                      Editor                          │
│                                                      │
│  ┌───────────────┐  ┌───────────────┐               │
│  │ PrimitiveFactory│  │ EntityActions │               │
│  │ (mesh generators│  │ (dup, delete, │               │
│  │  + spawn logic) │  │  group, lock, │               │
│  │                 │  │  hide)        │               │
│  └───────────────┘  └───────────────┘               │
│                                                      │
│  ┌───────────────┐  ┌───────────────┐               │
│  │ ImportDialog   │  │ PrefabSystem  │               │
│  │ (file browser, │  │ (save/load    │               │
│  │  model preview) │  │  entity       │               │
│  │                 │  │  templates)   │               │
│  └───────────────┘  └───────────────┘               │
│                                                      │
│  Existing: Selection, Gizmos, Hierarchy, Inspector   │
└─────────────────────────────────────────────────────┘
```

### Design Principles

1. **No undo/redo yet** — Phase 5D adds command pattern. For now, actions are immediate. But we design entity operations as discrete functions (not inline code) so they can later be wrapped in UndoableCommand objects.
2. **Spawn at camera focus** — New entities appear at the editor camera's focus point, not at the origin. This keeps them visible immediately.
3. **Select after create** — Every spawn/duplicate operation selects the new entity automatically.
4. **Metric scale** — All primitives spawn at 1 meter scale. 1 engine unit = 1 meter.

---

## Research Summary

Online research across game engines (Unity, Unreal, Godot, O3DE, FLECS) and graphics programming resources informed the following design decisions. Full sources are cited inline.

### Primitive Generation (Song Ho Ahn, LearnOpenGL, Catlike Coding, Daniel Sieger)

- **UV sphere:** Parameterize by stacks (latitude) and sectors (longitude). Loop sectors from 0 to N **inclusive** to create a duplicate vertex column at the UV seam (u=0 and u=1 share position but differ in UV). Without this, textures smear across one column. Poles produce degenerate triangles naturally — no special fan topology needed. **Recommended defaults: 32 sectors, 16 stacks** (~1,000 triangles, visually smooth on RX 6600).
- **Cylinder:** Three separate vertex groups (side wall, top cap, bottom cap) because normals and UVs differ at shared edges. Side normals are purely radial (no Y component). Caps use planar UV projection.
- **Cone:** Use 4–8 stacks on the side surface (not just 1). This confines the apex normal singularity to a tiny triangle fan at the tip. Side normals tilt outward and upward based on `atan2(baseRadius, height)`.
- **Wedge:** Hardcoded like `createCube()` — 5 faces (2 triangles, 3 quads), 18 vertices, 24 indices. Planar UV per face.
- **Tangents:** Our existing `calculateTangents()` with per-triangle accumulation + Gram-Schmidt orthogonalization works correctly for all shapes including curved surfaces. No changes needed.

### File Browser (GitHub survey, ImGui wiki)

Compared five options: **ImGuiFileDialog** (aiekick, MIT, ~1,500 stars, most features), **imgui-filebrowser** (AirGuanZ, MIT, ~818 stars, header-only), **portable-file-dialogs** (native OS), **tinyfiledialogs** (native OS), **nativefiledialog-extended** (native OS).

**Decision: imgui-filebrowser** — Header-only, C++17 `std::filesystem` (matches our project), renders inside ImGui (stays in WYSIWYG editor flow), handles Linux permission errors with `SkipItemsCausingError` flag, trivial FetchContent integration. The lack of maintenance since June 2024 is acceptable — the library is feature-complete and stable. Upgrade path to ImGuiFileDialog exists if we later need thumbnails or bookmarks.

### Prefab System (Unity, Godot, O3DE, FLECS, Wicked Engine)

- **Unity** uses delta serialization (instances store only overrides via `PropertyModification` lists). Powerful but requires significant infrastructure.
- **Godot** treats scenes and prefabs as the same thing — a prefab is just a scene file. Simplest model.
- **O3DE** uses JSON Patch (RFC 6902) for overrides — architecturally clean but complex.
- **FLECS** distinguishes shared vs owned components (`Inherit` vs `Override` traits).

**Decision: Godot model (scene = prefab).** A prefab file is a JSON file containing one root entity and its children. Same serialization code reused for scene save/load in Phase 5D. Start with full-copy instantiation (clone from JSON); add override tracking later if needed.

### nlohmann/json (benchmarks, CMake docs)

- Parse speed: ~81 MB/s (14x slower than simdjson). For our use case (1–50 KB prefab files), parsing takes <1 microsecond. Performance is irrelevant for editor tooling.
- **FetchContent via URL tarball** (not git clone) to avoid the ~150 MB repo clone.
- Use `json_fwd.hpp` in headers, full `json.hpp` only in serialization `.cpp` files to minimize compile time.
- `j.value("field", default)` for tolerant reading of missing fields (forward/backward compatibility).
- `NLOHMANN_JSON_SERIALIZE_ENUM` macro for MaterialType, AlphaMode, etc.

### Entity Cloning (Unity, Blender, Unreal, Wicked Engine)

- **Unity:** Duplicates at exact same position, no offset. User moves manually.
- **Blender (Shift+D):** Duplicates at same position, immediately enters grab mode.
- **Unreal (Ctrl+W):** Applies automatic ~10-unit offset.
- **Naming:** Unity uses `Object (1)`, Blender uses `Object.001`, Unreal uses `Object2`.

**Decision:** Duplicate at same position with small X offset (+0.5m) to avoid exact overlap while keeping it visible. Use `Object (1)` naming convention (Unity style — most readable for our user).

### Visibility & Locking (Blender, Unity, Unreal)

- **Unity model (recommended):** Eye icon for visibility (inherited by children, Alt+Click for single), pointer icon for pickability (lock), separate from runtime `SetActive`.
- **Blender quirk:** Hiding parent does NOT hide children by default — frequent user complaint. We follow Unity's inherited model instead.
- **Lock behavior:** Locked objects should NOT show transform gizmos but SHOULD remain selectable in hierarchy (otherwise you can't unlock them).

**Decision:** Visibility inherited like Unity (parent hidden = children hidden for rendering). Lock prevents viewport picking and hides gizmos but allows hierarchy selection. `H` for hide toggle, `L` for lock toggle.

### Room/Wall Builder (ProBuilder, Godot CSG, SketchUp, Manifold)

- **CSG is overkill** for our geometry. Walls are boxes; door openings are rectangular holes in boxes. CSG libraries add complexity, numerical robustness issues, and dependencies.
- **Direct mesh generation** is the right approach: generate wall geometry with holes already present by dividing the wall face into rectangular strips around openings. No boolean operations needed.
- **Parametric walls + imported decorative models** is the sweet spot for biblical structures: generate structural geometry (walls, floors, ceilings) parametrically, import ornamental geometry (cherubim, pomegranates, lily-work) from Blender via glTF.

**Decision:** Defer room/wall builder to a dedicated Phase 5B+ sub-phase. The wall system is architecturally distinct from primitive placement and deserves its own design document. Phase 5B focuses on the foundation (primitives, object management, import, prefabs) that the wall builder will build upon.

---

## Implementation Steps

### 5B-1: Primitive Mesh Generators

**Goal:** Add sphere, cylinder, cone, and wedge mesh generators alongside the existing cube and plane.

All primitives are unit-sized (fit in a 1×1×1 bounding box centered at origin) with proper normals, UVs, and tangent data for normal mapping.

#### New Static Methods on Mesh Class

| Primitive | Method | Geometry | Default Size |
|-----------|--------|----------|--------------|
| Sphere | `Mesh::createSphere(rings, sectors)` | UV sphere | Radius 0.5 (diameter 1m) |
| Cylinder | `Mesh::createCylinder(sectors)` | Capped cylinder | Radius 0.5, height 1m |
| Cone | `Mesh::createCone(sectors)` | Capped cone | Base radius 0.5, height 1m |
| Wedge | `Mesh::createWedge()` | Triangular prism (ramp) | 1×1×1 bounding box |

**Parameters:**
- `stacks` (sphere): Number of horizontal rings (latitude). Default 16.
- `sectors` (sphere/cylinder/cone): Number of vertical slices (longitude). Default 32.
- `stacks` (cone): Number of vertical subdivisions on the side surface. Default 4 (fixes apex normal singularity).
- All generators call `calculateTangents()` before returning.

**UV Mapping:**
- **Sphere:** Standard equirectangular (u = longitude 0→1, v = latitude 0→1). Duplicate vertex column at UV seam (j=0 and j=sectors share position, differ in U coordinate) to prevent texture smearing.
- **Cylinder:** Side wraps horizontally with seam duplicate. Caps use planar projection: `u = cos(theta)*0.5+0.5`, `v = sin(theta)*0.5+0.5`.
- **Cone:** Side wraps horizontally with seam duplicate. 4 stacks on the side surface to minimize apex shading artifacts. Base cap uses planar projection.
- **Wedge:** Planar projection per face (like cube). Hardcoded geometry — no parametric loops.

**ResourceManager Integration:**
- Add `getSphere/Cylinder/Cone/WedgeMesh()` methods with cache keys `"__builtin_sphere"`, etc.
- Same pattern as existing `getCubeMesh()` / `getPlaneMesh()`.

**Modified files:**
- `engine/renderer/mesh.h` — Add static factory methods
- `engine/renderer/mesh.cpp` — Implement generators
- `engine/resource/resource_manager.h` / `.cpp` — Add cached getters

---

### 5B-2: Create Menu & Entity Spawning

**Goal:** Wire up all Create menu items to actually spawn entities with geometry and lights.

#### Spawn Behavior

When the user clicks a Create menu item:
1. Get the editor camera's focus point as spawn position
2. Create a new entity via `scene->createEntity(name)`
3. Set `entity->transform.position` to the spawn point
4. Attach appropriate components (MeshRenderer, light, etc.)
5. Select the new entity
6. Log the creation to the console panel

#### Create Menu Structure

```
Create
├── Empty Entity                    → Empty node (for grouping)
├── ─────────────
├── Primitives
│   ├── Cube                        → 1m cube, default PBR material
│   ├── Sphere                      → 1m sphere
│   ├── Plane                       → 2m×2m plane
│   ├── Cylinder                    → 1m cylinder
│   ├── Cone                        → 1m cone
│   └── Wedge                       → 1m ramp
├── Lights
│   ├── Directional Light           → Entity with DirectionalLightComponent
│   ├── Point Light                 → Entity with PointLightComponent
│   └── Spot Light                  → Entity with SpotLightComponent
```

#### Default Materials for Primitives

New primitives get a shared "Editor Default" PBR material:
- Albedo: light gray (0.7, 0.7, 0.7)
- Metallic: 0.0
- Roughness: 0.5
- No textures

This material is created once by the ResourceManager (`"__editor_default"`) and shared across all spawned primitives. The user can change it in the inspector.

#### Editor Integration

The Editor class needs access to Scene and ResourceManager to spawn entities. The `drawPanels()` signature already passes `Scene*`. We'll also pass `ResourceManager*` (or store it during `initialize()`).

**New files:**
- `engine/editor/entity_factory.h` / `.cpp` — Static helper functions for spawning entities

**Modified files:**
- `engine/editor/editor.h` / `.cpp` — Wire Create menu, store ResourceManager pointer
- `engine/core/engine.cpp` — Pass ResourceManager to editor

---

### 5B-3: Object Management — Duplicate & Delete

**Goal:** Implement core object operations: duplicate, delete, and copy/paste transforms.

#### Duplicate (Ctrl+D)

Deep-copies the selected entity and all its children:
1. Clone the entity hierarchy (new IDs for all cloned entities)
2. Clone component data (MeshRenderer shares the same Mesh/Material shared_ptrs — lightweight)
3. Place the clone as a sibling of the original (same parent)
4. Offset position slightly (+0.5m on X) so it doesn't overlap exactly
5. Select the clone
6. Name: Unity-style parenthesized number — `Cube (1)`, `Cube (2)`, etc. If original already has a suffix, increment it.

**Implementation:** Add `Entity* Scene::duplicateEntity(uint32_t entityId)` which recursively clones the entity tree.

#### Delete (Delete key)

Already partially implemented in the hierarchy panel's right-click menu. Extend to:
1. Work via Delete key when viewport is focused (not just hierarchy)
2. Handle multi-selection (delete all selected)
3. Clear selection after delete
4. Prevent deleting the scene root

#### Copy/Paste Transform (Ctrl+Shift+C / Ctrl+Shift+V)

Copies position/rotation/scale from one entity and applies to another:
1. Ctrl+Shift+C: Store the selected entity's local transform
2. Ctrl+Shift+V: Apply stored transform to the currently selected entity
3. Visual feedback via console log

**New files:**
- `engine/editor/entity_actions.h` / `.cpp` — Standalone functions for duplicate, delete, transform clipboard

**Modified files:**
- `engine/scene/scene.h` / `.cpp` — Add `duplicateEntity()` method
- `engine/scene/entity.h` / `.cpp` — Add `clone()` method for deep copy
- `engine/editor/editor.cpp` — Keyboard shortcut handling

---

### 5B-4: Entity Visibility, Locking & Grouping

**Goal:** Add per-entity flags for lock and visibility, plus a grouping shortcut.

#### Entity Flags

Add two new flags to Entity:

| Flag | Default | Effect |
|------|---------|--------|
| `m_isVisible` | `true` | When false, entity and children are skipped during render data collection. Still appears in hierarchy (grayed out with eye icon). |
| `m_isLocked` | `false` | When true, entity cannot be selected via viewport click (ID buffer skips it). Still selectable in hierarchy. Still rendered normally. |

**Hierarchy Panel Integration:**
- Add small icon buttons on each tree row:
  - Eye icon (visibility toggle) — click to show/hide
  - Lock icon (lock toggle) — click to lock/unlock
- Icons use ImGui's built-in font or simple text glyphs ("V" / "-" and "L" / "U")
- Toggling a parent cascades to children visually (children inherit parent visibility for rendering, but their own flag is independent)

#### Grouping (Ctrl+G)

Select multiple entities → Ctrl+G creates a new empty parent entity named "Group" and reparents all selected entities under it. The group entity's position is set to the centroid of the selected entities.

#### Scene Integration

- `collectRenderData()` must check `isVisible()` — skip entity and children if not visible
- `pickEntityAt()` / ID buffer must check `isLocked()` — skip locked entities during ID rendering

**Modified files:**
- `engine/scene/entity.h` / `.cpp` — Add `m_isVisible`, `m_isLocked`, getters/setters
- `engine/scene/scene.cpp` — Respect visibility in `collectRenderDataRecursive()`
- `engine/renderer/renderer.cpp` — Respect locked flag in ID buffer rendering
- `engine/editor/panels/hierarchy_panel.cpp` — Add visibility/lock icons per row
- `engine/editor/editor.cpp` — Ctrl+G grouping shortcut

---

### 5B-5: Model Import Dialog

**Goal:** Browse the filesystem, select a glTF/GLB/OBJ file, and import it into the current scene.

#### Import Flow

1. User clicks File → Import Model (or a toolbar button)
2. A modal dialog opens with:
   - File path text field (editable, with "Browse..." button)
   - Detected format indicator (glTF / GLB / OBJ)
   - Import scale slider (default 1.0 — for models not in metric scale)
   - "Import" and "Cancel" buttons
3. On Import:
   - Load the model via ResourceManager (`loadModel()` for glTF, `loadMesh()` for OBJ)
   - Instantiate in the scene at camera focus point
   - Apply import scale to the root entity's transform.scale
   - Select the imported root entity
   - Log success/failure to console

#### File Browser

Use **imgui-filebrowser** (AirGuanZ, MIT, header-only, C++17 `std::filesystem`). Renders inside ImGui, handles Linux permission errors, trivial FetchContent integration.

```cpp
ImGui::FileBrowser fileDialog(ImGuiFileBrowserFlags_CloseOnEsc);
fileDialog.SetTitle("Import Model");
fileDialog.SetTypeFilters({".gltf", ".glb", ".obj"});
fileDialog.Open();

// Every frame:
fileDialog.Display();
if (fileDialog.HasSelected())
{
    std::filesystem::path selected = fileDialog.GetSelected();
    fileDialog.ClearSelected();
    // Import the model...
}
```

**New dependency:**
- **imgui-filebrowser** (MIT, header-only) — Add via FetchContent. Include path only, no library target to build.

**New files:**
- `engine/editor/panels/import_dialog.h` / `.cpp` — Import dialog wrapping imgui-filebrowser with scale slider and format detection

**Modified files:**
- `external/CMakeLists.txt` — FetchContent for imgui-filebrowser
- `engine/editor/editor.h` / `.cpp` — Add import dialog, File menu item
- `engine/resource/resource_manager.h` / `.cpp` — Add OBJ-to-entity instantiation (currently only glTF has `instantiate()`)

---

### 5B-6: Prefab System

**Goal:** Save entity hierarchies as reusable templates that can be placed multiple times.

#### Concept

A **prefab** is a saved entity tree (structure + component data) stored as a JSON file in `assets/prefabs/`. When placed, it creates a new independent copy — no live link back to the prefab source (live prefab instances are deferred to Phase 5D with serialization).

#### Prefab Data Format (JSON)

```json
{
    "name": "Golden Lampstand",
    "version": 1,
    "root": {
        "name": "Lampstand Root",
        "transform": {
            "position": [0, 0, 0],
            "rotation": [0, 0, 0],
            "scale": [1, 1, 1]
        },
        "components": {
            "MeshRenderer": {
                "mesh": "__builtin_cylinder",
                "material": "gold_material"
            }
        },
        "children": [
            { ... }
        ]
    }
}
```

**Limitations (Phase 5B):**
- Meshes reference built-in primitives or loaded model paths (not embedded geometry)
- Materials reference names from the ResourceManager cache (not embedded definitions)
- No live prefab link — instances are independent copies once placed
- Custom textures must already be loaded (prefab stores paths, not pixel data)

#### Workflow

1. **Save as Prefab:** Right-click entity in hierarchy → "Save as Prefab". Opens a name dialog, saves JSON to `assets/prefabs/`.
2. **Place Prefab:** Create menu → Prefabs submenu lists all `.json` files in `assets/prefabs/`. Click to instantiate at camera focus.
3. **Delete Prefab:** File system operation (not in-editor for now).

#### Serialization Helpers

Since Phase 5D will add full scene serialization, we build reusable serialization utilities now:
- `serializeEntity(entity) → json` — Recursive entity-to-JSON
- `deserializeEntity(json, scene, resourceManager) → Entity*` — JSON-to-entity

These same functions will be reused for scene save/load in Phase 5D.

**New files:**
- `engine/editor/prefab_system.h` / `.cpp` — Save/load prefab JSON files
- `engine/utils/entity_serializer.h` / `.cpp` — Entity ↔ JSON conversion

**New dependency:**
- **nlohmann/json** (MIT, header-only) — Add via FetchContent **URL tarball** (not git clone, avoids ~150 MB repo):
  ```cmake
  FetchContent_Declare(json
      URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  )
  FetchContent_MakeAvailable(json)
  ```
  Link as `nlohmann_json::nlohmann_json`. Use `json_fwd.hpp` in headers, full `json.hpp` only in `.cpp` files.

**Component Clone Pattern:**
- Add `virtual std::unique_ptr<Component> clone() const = 0` to the Component base class
- Each component implements clone(): shared resources (`shared_ptr<Mesh>`, `shared_ptr<Material>`) are copied by pointer; value-type data (transform, flags, light settings) is deep-copied
- Component factory registry for deserialization: `std::unordered_map<std::string, ComponentFactory>` mapping type names to factory lambdas

**Modified files:**
- `external/CMakeLists.txt` — FetchContent for nlohmann/json
- `engine/CMakeLists.txt` — Link json library
- `engine/scene/component.h` — Add virtual `clone()` method
- `engine/scene/mesh_renderer.h` / `.cpp` — Implement `clone()`
- `engine/scene/light_component.h` — Implement `clone()` for all light types
- `engine/editor/editor.cpp` — Prefabs submenu in Create menu
- `engine/editor/panels/hierarchy_panel.cpp` — "Save as Prefab" context menu item

---

## Implementation Order & Dependencies

```
5B-1: Primitive Generators ──────────────┐
                                         ├──→ 5B-2: Create Menu & Spawning
                                         │
5B-3: Duplicate & Delete ───────────────→│
                                         │
5B-4: Visibility, Lock, Group ──────────→│
                                         │
5B-5: Model Import Dialog ──────────────→│
                                         │
5B-6: Prefab System (needs nlohmann/json)┘
```

- **5B-1** must come first (primitives needed for Create menu)
- **5B-2** depends on 5B-1 (spawns primitives)
- **5B-3, 5B-4, 5B-5** are independent of each other, can be done in any order after 5B-2
- **5B-6** is last (uses entity serialization, benefits from all prior work)

---

## Entity Clone Deep Copy Specification

Duplicating an entity requires careful handling of shared vs. owned data:

| Data | Copy Strategy | Reason |
|------|---------------|--------|
| Entity ID | New unique ID | IDs must be globally unique |
| Name | Copy + " Copy" suffix | Distinguish from original |
| Transform | Deep copy (TRS values) | Independent positioning |
| Parent | Same parent as original | Clone is a sibling |
| Children | Recursive deep copy | Full hierarchy clone |
| MeshRenderer mesh | Share (shared_ptr) | Mesh data is immutable GPU buffer |
| MeshRenderer material | Share (shared_ptr) | Material can be changed later via inspector |
| MeshRenderer bounds | Deep copy | Same geometry = same bounds |
| Light components | Deep copy all fields | Independent light settings |
| Active flag | Copy from original | Preserve visibility state |
| Visible flag | Copy from original | Preserve visibility |
| Locked flag | Set to false | Don't lock the clone |

---

## Keyboard Shortcuts Summary

| Action | Shortcut | Context |
|--------|----------|---------|
| Duplicate | Ctrl+D | Entity selected (viewport or hierarchy) |
| Delete | Delete | Entity selected (viewport or hierarchy) |
| Hide/Show | H | Entity selected |
| Lock/Unlock | L | Entity selected (when viewport not focused, to avoid conflict with gizmo L) |
| Group | Ctrl+G | Multiple entities selected |
| Copy Transform | Ctrl+Shift+C | Entity selected |
| Paste Transform | Ctrl+Shift+V | Entity selected |
| Import Model | Ctrl+I | Editor mode |

---

## Accessibility Notes

Continuing the Phase 5A accessibility standards:
- All new menu items have text labels (no icon-only buttons)
- Visibility/lock toggle icons are large enough (minimum 20×20px hit area within the row)
- Import dialog uses large fonts, high-contrast text on file entries
- Confirmation messages appear in the console panel with large text
- No color-only indicators — all states have text/icon alternatives

---

## Performance Considerations

- **Primitive generators** run once at startup (cached in ResourceManager). No per-frame cost.
- **Duplicate** clones entity tree but shares mesh/material data. A scene with 1000 duplicated cubes uses 1 mesh + 1 material + 1000 entity nodes — lightweight.
- **Visibility flag** is checked early in `collectRenderData()`, skipping the entire subtree — faster than rendering and culling invisible objects.
- **Lock flag** is checked during ID buffer rendering only (not every frame's main render pass).
- **Import dialog** file listing is re-scanned only when the directory changes, not every frame.
- **Prefab loading** parses JSON once, then creates entities — negligible cost.

---

## Design Decisions

1. **Room/Wall Builder:** Deferred to a dedicated sub-phase after 5B. Research confirmed parametric wall generation (not CSG) is the right approach — generate wall meshes with door/window holes already present by dividing the face into rectangular strips. This is architecturally distinct from primitive placement and deserves its own design document. Phase 5B builds the foundation it will depend on.

2. **Align Tools:** Deferred to Phase 5F (editor utilities). Basic snapping via gizmo Ctrl-snap covers most alignment needs for now.

3. **Prefab Live Links:** Simple copy approach for 5B. Godot-style "scene = prefab" model — a prefab file is just a JSON entity tree. Full-copy instantiation now; override tracking can be layered on in Phase 5D if needed (O3DE's JSON Patch approach is the cleanest model for that).

4. **Default Material:** Each spawned primitive gets its **own material instance** cloned from the `"__editor_default"` template. This prevents the "edit one cube and all cubes change color" surprise. The clone is cheap (no GPU resources, just a few floats). The template material is shared read-only; each spawn's material is independent.

---

## Future Sub-phases (building on 5B)

- **5C:** Material editor panel, asset browser with thumbnails, light editing widgets
- **5D:** Scene save/load (JSON), undo/redo (command pattern), project management
- **5E:** Particle emitter editor, water surface editor
- **5F:** Performance overlay, console improvements, screenshot tool
- **5G:** Environment painting (grass, trees, paths, biomes)
