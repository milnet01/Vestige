# Phase 5A: Editor Foundation — Design Document

## Goal
Get the basic editor shell running — panels, viewport, and the ability to interact with the scene visually. The editor is the primary tool for building scenes without writing code.

**Milestone:** A working editor with dockable panels, 3D viewport, scene hierarchy, entity inspector, orbit camera, mouse picking, and transform gizmos.

---

## Current State (End of Phase 4)

The engine currently has:
- **Forward rendering** with PBR/Blinn-Phong shaders, HDR pipeline, MSAA/TAA
- **Post-processing:** Bloom, SSAO, tone mapping, color grading, auto-exposure
- **Shadows:** Cascaded directional + omnidirectional point light shadows
- **Scene system:** Entity-component hierarchy, frustum culling, instanced rendering
- **Input:** Keyboard, mouse, gamepad via GLFW, first-person controller
- **Resources:** Texture loading (sync + async), glTF/OBJ model loading, material system
- **No editor UI — everything controlled via hotkeys and hardcoded demo scene**

---

## Architecture Overview

The editor is a new subsystem that renders ImGui panels on top of the existing 3D scene. The key insight: the 3D viewport becomes an ImGui image widget displaying the existing FBO texture.

```
┌─────────────────────────────────────────────────────┐
│                      Engine                          │
│                                                      │
│  ┌──────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │ Renderer │  │    Editor    │  │ FirstPerson   │  │
│  │ (scene → │  │ (ImGui, cam, │  │ Controller    │  │
│  │  FBO)    │  │  gizmos,     │  │ (play mode)   │  │
│  │          │  │  selection)  │  │               │  │
│  └──────────┘  └──────────────┘  └───────────────┘  │
│                                                      │
│  Mode Switch: EDITOR ←→ PLAY                         │
│  - Editor: ImGui panels, orbit camera, gizmos        │
│  - Play:   Cursor captured, FPS controller, no GUI   │
└─────────────────────────────────────────────────────┘
```

### Engine Mode System

| | Editor Mode | Play Mode |
|---|---|---|
| **Cursor** | Visible, free | Captured, hidden |
| **Camera** | Orbit/pan/zoom (editor camera) | FPS controller (game camera) |
| **Input** | Routed through ImGui first | Direct to game systems |
| **GUI** | Full ImGui panels | HUD only (optional) |
| **Scene** | Editable (select, move, etc.) | Running (physics, scripts later) |

---

## New Dependencies

| Library | Purpose | License | Version |
|---------|---------|---------|---------|
| Dear ImGui (docking branch) | Immediate-mode GUI with dockable panels | MIT | docking branch |
| ImGuizmo | Transform gizmos (translate/rotate/scale) | MIT | Latest |

Both are fetched via CMake FetchContent and compiled as static libraries.

---

## Implementation Steps

### 5A-1: ImGui Integration
**Goal:** ImGui renders on top of the 3D scene with a dockable workspace.

- Add Dear ImGui (docking branch) and ImGuizmo via FetchContent
- Create `Editor` class that manages ImGui lifecycle
- Modify engine loop: insert ImGui frame between `endFrame()` and `swapBuffers()`
- Add Edit/Play mode toggle (Escape key)
- Route input through ImGui (skip engine hotkeys when ImGui wants input)
- Dark theme with readable fonts (18px minimum for accessibility)
- **Do NOT enable `ImGuiConfigFlags_ViewportsEnable`** — causes issues on Linux/Mesa

**Rendering order:**
```
beginFrame → renderScene → endFrame (resolve, post-process, composite to screen)
→ ImGui::NewFrame → draw panels → ImGui::Render → swapBuffers
```

**New files:**
- `engine/editor/editor.h` / `editor.cpp`

**Modified files:**
- `external/CMakeLists.txt` — FetchContent for ImGui + ImGuizmo
- `engine/CMakeLists.txt` — Link libraries, add editor source
- `engine/core/engine.h` / `engine.cpp` — Editor subsystem, mode toggle, input gating

### 5A-2: Editor Shell & Docking Layout
**Goal:** Dockable workspace with viewport displaying the scene FBO texture.

The 3D viewport is the resolved FBO texture displayed via `ImGui::Image()` inside a dockable window. Mouse coordinates are remapped from ImGui window space to viewport UV space.

**Default layout:**
```
┌──────────────────────────────────────────────────┐
│  Menu Bar: File | Edit | View | Create | Help    │
├─────────┬──────────────────────┬─────────────────┤
│         │                      │                 │
│ Scene   │    3D Viewport       │   Inspector     │
│ Hierarc │    (FBO texture)     │   (properties   │
│ hy      │                      │    of selected  │
│ (tree)  │                      │    entity)      │
│         │                      │                 │
├─────────┴──────────────────────┴─────────────────┤
│  Console / Log           │  Asset Browser        │
└──────────────────────────┴───────────────────────┘
```

**New files:**
- `engine/editor/panels/viewport_panel.h` / `.cpp`

### 5A-3: Editor Camera
**Goal:** Orbit/pan/zoom camera for scene editing.

Turntable camera (simpler, better for architectural work):
- **Orbit:** Alt + left-drag rotates around a focus point
- **Pan:** Middle-mouse drag translates the view plane
- **Zoom:** Scroll wheel moves toward/away from focus point
- **Focus:** F key snaps to selected entity's center
- **Orthographic views:** Numpad 1/3/7 for Front/Right/Top
- **Smooth transitions:** Lerp between view changes (~0.3 seconds)

**New files:**
- `engine/editor/editor_camera.h` / `.cpp`

### 5A-4: Selection System
**Goal:** Click to select entities in the viewport.

**Approach: ID buffer (color picking)**
- Render entities to a separate FBO with unique color-encoded entity IDs
- On click, read the pixel under the cursor → decode entity ID → select
- Use double-buffered PBOs for async readback (same pattern as auto-exposure)
- Pixel-perfect accuracy, works with any geometry shape

**Selection features:**
- Single click → select one entity
- Shift+click → add to selection
- Ctrl+click → toggle in selection
- Click on empty → deselect all
- Selection highlight: stencil-based orange outline

**New files:**
- `engine/editor/selection.h` / `.cpp`
- `assets/shaders/id_buffer.vert.glsl` / `id_buffer.frag.glsl`
- `assets/shaders/outline.vert.glsl` / `outline.frag.glsl`

### 5A-5: Scene Hierarchy Panel
**Goal:** Tree view of all entities.

- Recursive tree using `ImGui::TreeNodeEx()`
- Click to select (syncs with viewport selection)
- Drag to reparent entities
- Right-click context menu: Create, Rename, Duplicate, Delete
- Search/filter bar at top
- Icons for entity types

**New files:**
- `engine/editor/panels/hierarchy_panel.h` / `.cpp`

### 5A-6: Inspector Panel
**Goal:** View and edit selected entity properties.

- Transform section: Position (X/Y/Z), Rotation, Scale with drag fields
- Components list: all attached components with collapsible sections
- Material section: color pickers, sliders, texture slots
- Light section: color picker, intensity, range sliders
- "Add Component" button with searchable dropdown

**Widget mapping:**
| Property Type | Widget |
|---|---|
| Float (0-1) | Slider with numeric input |
| Float (unbounded) | Drag field |
| Vec3 | Three labeled drag fields (X=red, Y=green, Z=blue) |
| Color | Color swatch + picker popup |
| Boolean | Checkbox |
| Enum | Dropdown |
| Texture | Thumbnail + drag-and-drop zone |

**New files:**
- `engine/editor/panels/inspector_panel.h` / `.cpp`

### 5A-7: Transform Gizmos
**Goal:** Visual handles for moving/rotating/scaling entities.

Uses ImGuizmo library (MIT, battle-tested):
- Translate, Rotate, Scale modes
- W/E/R hotkeys to switch
- Local vs World space toggle
- Snap to grid (Ctrl held)
- `ImGuizmo::IsOver()` prevents clicking through gizmo to pick behind it

**New files:**
- None (uses ImGuizmo directly in viewport panel)

---

## Selection Outline Technique

Stencil-based outline (nearly free, works with forward rendering):

1. Render selected entity normally, writing 1 to stencil buffer
2. Render same entity scaled slightly larger (1.05x) with flat orange shader, only where stencil ≠ 1
3. Result: crisp outline around selected objects

---

## Entity Identity

Entities need unique IDs for picking, serialization, and undo/redo:
- Add `uint32_t m_id` to Entity, assigned via incrementing counter at creation
- The ID buffer renders this ID as a flat color
- Scene serialization (Phase 5D) will use these IDs for stable references

---

## Accessibility Considerations

The user is partially sighted. Built-in from the start:

- **Large fonts:** 18px minimum, scalable via UI Scale setting
- **High contrast theme:** Dark background (#1A1A1A), bright text (#FFFFFF), 15:1+ contrast ratio
- **Thick selection outlines:** 3-4px bright orange
- **Large click targets:** 44x44px minimum interactive areas
- **Generous padding:** 8px+ between list items
- **Tooltips:** High contrast, short delay (300ms)
- **Keyboard navigation:** Tab cycling, arrow keys
- **Status bar messages:** Large text confirming actions

---

## Performance Notes

- ImGui overhead: negligible (~0.5ms for full editor UI)
- Viewport renders at panel size, not window size (fewer pixels when panels are open)
- ID buffer only renders on click (not every frame)
- Use `ImGuiDockNodeFlags_PassthruCentralNode` to skip dockspace background rendering
- Skip TAA jitter and motion vectors in editor mode (not needed)

---

## Future Sub-phases (built on this foundation)

- **5B:** Primitive placement, wall/room builder, model import, prefabs
- **5C:** Material editor panel, asset browser with thumbnails, light editing
- **5D:** Scene save/load (JSON), undo/redo (command pattern), project management
- **5E:** Particle emitter editor, water surface editor
- **5F:** Performance overlay, console, screenshot, shortcuts panel
- **5G:** Environment painting (grass, trees, paths, water, biomes)
