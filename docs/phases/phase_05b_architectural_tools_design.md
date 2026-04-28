# Phase 5B: Architectural Tools Design

## Overview
Architectural tools for creating walls, rooms, doors/windows, roofs, and stairs procedurally. Also includes align/distribute tools and import dialog improvements.

## Research Summary

### CSG for Cutouts
Full CSG (BSP-tree boolean operations) is overkill for rectangular openings in planar walls. Instead, use 2D polygon subdivision: project wall to 2D, subtract opening rectangles, triangulate the result, extrude for thickness. This produces clean geometry without external dependencies.

**References:** csg.js (Evan Wallace), Godot CSG Tools, MCUT library

### Roof Generation
- **Flat/shed/gable**: Pure vertex math for rectangular footprints — no algorithm needed
- **Hip roofs**: Require straight skeleton algorithm (deferred — not needed for biblical structures)

**References:** CGAL Straight Skeleton, STALGO (lightweight C++ implementation)

### Stair Generation
Standard IBC dimensions: rise max 178mm, run min 279mm, rise+run ~430-460mm. Generate as sequence of box steps (tread + riser faces per step).

### Wall/Room Tools
Data-driven approach: store wall parameters (endpoints, height, thickness, openings), regenerate mesh on change. Room = closed ring of wall segments + floor + optional ceiling.

## Architecture

### ProceduralMeshBuilder (`utils/procedural_mesh.h/.cpp`)
Static utility class for generating architectural meshes:

```cpp
struct WallOpening { float xOffset, yOffset, width, height; };

static Mesh createWall(float width, float height, float thickness);
static Mesh createWallWithOpenings(float width, float height, float thickness,
                                    const std::vector<WallOpening>& openings);
static Mesh createFloor(float width, float depth, float thickness);
static Mesh createGableRoof(float width, float depth, float peakHeight, float overhang);
static Mesh createShedRoof(float width, float depth, float riseHeight, float overhang);
static Mesh createStraightStairs(float totalHeight, float riseHeight,
                                  float treadDepth, float width);
static Mesh createSpiralStairs(float totalHeight, float riseHeight,
                                float innerRadius, float outerRadius, float totalAngle);
```

All meshes centered at origin with proper normals, UVs, and tangents.

### Wall Tool (`tools/wall_tool.h/.cpp`)
Interactive two-click wall placement:
1. First click: place start point (grid-snapped)
2. Move mouse: preview wall with ghost mesh
3. Second click: place end point, create wall entity

Data stored as custom WallData component for re-editing.

### Room Tool (`tools/room_tool.h/.cpp`)
Multi-click corner placement:
1. Click corners to define floor polygon
2. Double-click to close the loop
3. Generates wall entities + floor, grouped under "Room" parent

### Cutout Tool (`tools/cutout_tool.h/.cpp`)
Inspector-based wall opening editor:
1. Select a wall entity
2. Add/remove openings via inspector UI
3. Wall mesh regenerates automatically

### Roof Tool (`tools/roof_tool.h/.cpp`)
1. Select a room group or specify dimensions in inspector
2. Choose type (flat, gable, shed), set parameters
3. Generates roof mesh entity as child of room

### Stair Tool (`tools/stair_tool.h/.cpp`)
Inspector-driven parametric stairs:
1. Click to place, or use Create menu
2. Configure in inspector: height, rise, tread depth, width, type
3. Mesh regenerates on parameter change

### Align/Distribute (`commands/align_distribute_command.h`)
Operates on current selection (2+ entities):
- **Align**: Left/Right/Center on X, Y, or Z using world-space AABB
- **Distribute**: Even spacing along X, Y, or Z
- Fully undoable via CompositeCommand wrapping TransformCommands

### Import Dialog Improvements
- Show triangle count, material count, bounding box dimensions
- Scale validation warning (objects < 0.01m or > 1000m)
- Format-specific info (glTF: animations, skins; OBJ: groups)

## Performance
All mesh generation is CPU-side, one-time cost at creation. Generated meshes use the same Vertex/Mesh pipeline as primitives. No runtime overhead.
