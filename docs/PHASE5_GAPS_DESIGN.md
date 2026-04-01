# Phase 5 Gaps Design Document

**Date:** 2026-04-01
**Scope:** Batch 1 (Editor QoL), Batch 2 (Architectural Tools), Batch 3 (Environment Painting)

---

## Batch 1: Editor Quality-of-Life

### 1a. Ruler / Measurement Tool + Dimension Display

**Problem:** No way to measure distances or see object dimensions in meters.

**Design:**
- New `RulerTool` class in `engine/editor/tools/ruler_tool.h/.cpp`
- Two-click workflow: click point A (ray-scene intersect), click point B, display distance
- Renders measurement line + text overlay via DebugDraw + ImGui viewport drawlist
- Dimension display: in InspectorPanel, below Transform section, show AABB width/height/depth of selected entity's MeshRenderer bounds (read-only, in meters)
- Room dimension input: dialog in Create menu — user types width x depth x height, spawns box entities for floor + walls + ceiling as a group

**Integration:**
- Add `RulerTool m_rulerTool` member to Editor class
- Toggle via toolbar button or View menu
- processInput() called from Editor::drawPanels() when active

### 1b. Object Lock UI — ALREADY COMPLETE

Lock toggle buttons ("V" for visibility, "L" for lock) already exist in hierarchy_panel.cpp lines 485-550, with undo support via EntityPropertyCommand. Context menu Lock/Unlock also present at line 649. No work needed.

### 1c. Texture Filtering UI

**Problem:** No way to change texture filtering (nearest/linear/anisotropic) per texture.

**Design:**
- Add `FilterMode` enum to Texture class: `NEAREST`, `LINEAR`, `TRILINEAR`, `ANISOTROPIC_4X`, `ANISOTROPIC_8X`, `ANISOTROPIC_16X`
- Add `setFilterMode(FilterMode)` and `getFilterMode()` methods
- Implementation: `glTextureParameteri(m_textureId, GL_TEXTURE_MIN_FILTER, ...)` + `GL_TEXTURE_MAX_ANISOTROPY_EXT`
- Inspector UI: dropdown in `drawMaterialTextures()` next to each texture slot

### 1d. Light Visualization — All Lights Mode

**Problem:** `Engine::drawLightGizmos()` only draws gizmos for selected lights. Users can't see light coverage of unselected lights.

**Design:**
- Add `bool m_showAllLightGizmos = false` to Editor class
- Add View menu toggle "Show All Light Gizmos"
- In `Engine::drawLightGizmos()`: when enabled, iterate ALL entities with light components (not just selected), draw with dimmer colors (30% alpha). Selected lights still draw at full brightness on top.

### 1e. File Watcher with Asset Reload

**Problem:** AssetBrowserPanel polls for directory changes but doesn't reload already-loaded textures/models when they change on disk.

**Design:**
- New `FileWatcher` class in `engine/resource/file_watcher.h/.cpp`
- Uses `std::filesystem::last_write_time()` polling (2-second interval)
- Tracks path → timestamp map for all loaded assets
- On change detected: calls `ResourceManager::reloadTexture(path)` or `reloadModel(path)`
- ResourceManager gets new `reloadTexture()` method: re-reads file, re-uploads to GPU via existing Texture::loadFromFile(), preserves GL handle
- Ticked from Engine main loop

### 1f. Welcome Screen Panel

**Problem:** No first-launch guidance for new users.

**Design:**
- New `WelcomePanel` class in `engine/editor/panels/welcome_panel.h/.cpp`
- Shows on first launch (check `~/.config/vestige/welcome_shown` flag)
- Content: keyboard shortcut overview, mode explanation (Edit/Play), brush tools summary
- "Don't show again" checkbox + close button
- Accessible via Help > Welcome Screen

### 1g. Validation Warnings Panel

**Problem:** No feedback about scene issues (missing textures, lights without shadows, out-of-bounds objects).

**Design:**
- New `ValidationPanel` class in `engine/editor/panels/validation_panel.h/.cpp`
- "Validate Scene" button runs checks and populates warning list
- Checks: (a) MeshRenderer with null mesh, (b) material with missing texture files, (c) entity position > 1000m from origin, (d) light without shadow map enabled
- Each warning: severity icon + message + click-to-select entity
- Auto-validate on scene load (optional toggle)

---

## Batch 2: Architectural Tools (deferred to after Batch 1)

### 2a. Wall / Room / Cutout / Roof / Stair Tools
- Procedural mesh generation utilities for walls (box with specified height/thickness), rooms (floor + 4 walls + ceiling group), stairs (stepped geometry)
- CSG-lite for door/window cutouts (split wall mesh at opening, no full boolean ops)
- Roof tool: gabled/hipped/flat from selected wall perimeter

### 2b. Align / Distribute Tools
- Already have `entity_actions.h` — extend with `alignEntities()` and `distributeEntities()`
- Edit menu integration

### 2c. Import Dialog Improvements
- Add model preview using offscreen FBO render
- Display material/texture list from imported model
- Scale validation with warning

---

## Batch 3: Environment Painting (deferred to after Batch 2)

### 3a. Density Map
- Paintable grayscale R8 texture mapped over world space
- FoliageManager::paintFoliage() multiplies density by density map sample

### 3b. Auto-Clear Foliage Along Paths
- After path placement, erase foliage within path width

### 3c. Bank Blending
- Auto-modify terrain splatmap near water entity edges

---

## Sources
- ImGui documentation: https://github.com/ocornut/imgui
- OpenGL texture filtering: https://www.khronos.org/opengl/wiki/Sampler_Object
- GL_TEXTURE_MAX_ANISOTROPY_EXT: https://registry.khronos.org/OpenGL/extensions/EXT/EXT_texture_filter_anisotropic.txt
