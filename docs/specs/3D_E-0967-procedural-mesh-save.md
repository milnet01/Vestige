<!-- ants-spec-format: 1 -->
# 3D_E-0967 — Save editor-built meshes as a build recipe

**Status:** spec draft (2026-11-09).
**Kind:** fix.
**Source:** ROADMAP 3D_E-0967 (3D_E-0874 cutout lead re-check, 2026-11-08).
**Blocker for:** the 3D_E-0874 lane-16 lead `cutout_tool` (a second cut drops the first hole).

Layman: walls, rooms, roofs, stairs and paths made with the building tools are still there after the scene is saved and reopened, and a wall keeps every door and window cut into it.

## 1. Goal

Every mesh the editor builds carries the recipe it was built from: which
generator, and the numbers it was given. A saved scene stores that recipe,
and loading rebuilds the same mesh from it. Cutting a second opening into a
wall keeps the first, because the wall's recipe lists every opening.

## 2. Problem

`serializeMeshRenderer` (`engine/utils/entity_serializer.cpp`) saves a mesh
only by the key `ResourceManager::findMeshKey` returns. Built-in primitives
have keys (`"__builtin_cube"`, `"__builtin_sphere_<s>x<t>"`), and
`ResourceManager::getMeshByKey` rebuilds them. A mesh made with
`std::make_shared<Mesh>` outside the resource manager has no key, so the saved
JSON has no `"mesh"`, and `deserializeMeshRenderer` adds no `MeshRenderer`.

Those meshes are (`grep -rn 'make_shared<Mesh>' engine`, 2026-11-09):

- `EntityFactory::createWall`, `createWallWithDoor`, `createWallWithWindow`,
  `createRoom`, `createRoof`, `createStairs`, `createSpiralStairs`,
  `createFloorSlab` (`engine/editor/entity_factory.cpp`);
- `WallTool`, `RoomTool`, `RoofTool`, `StairTool`, `CutoutTool`
  (`engine/editor/tools/`);
- `PathTool::generatePathMesh`, which builds its vertices inline from a
  `CatmullRomSpline`;
- the demo fence cloth in `engine/core/engine.cpp`, which the demo scene
  rebuilds in code on every run (§ 9).

Every one except `PathTool` and the demo fence calls a
`ProceduralMeshBuilder::create*` function (`engine/utils/procedural_mesh.h`).

Consequences:

1. **Built geometry vanishes.** Saving and reopening a scene, or saving and
   placing a prefab, leaves these entities with no mesh. The red test
   `FactoryWallKeepsItsMeshThroughSaveAndLoad`, parked outside the tree at
   `Vestige_private/drafts/test_procedural_mesh_save.cpp`, proves it for a
   factory wall with a door.
2. **A second cut drops the first.** `CutoutTool` reads the wall's size from
   the mesh bounds and rebuilds it with a one-element `openings` list. Nothing
   remembers the earlier openings.

## 3. Scope decisions (agreed with the user)

None from the user. Decided as code owner on 2026-11-09, following the
handoff's lean (2026-11-08): store a recipe, not raw vertices (§ 8 says why);
the recipe lives on the `Mesh` (§ 4.2); the scene format version goes from 2
to 3 (§ 13).

## 4. Design

### 4.1 The recipe

New header `engine/utils/mesh_recipe.h`, with no GL dependency:

```cpp
struct MeshRecipe
{
    enum class Generator { WALL, FLOOR, ROOF, STAIRS_STRAIGHT, STAIRS_SPIRAL, PATH };

    Generator generator = Generator::WALL;
    // Named after the ProceduralMeshBuilder parameters they feed.
    float width = 0, height = 0, thickness = 0, depth = 0;       // wall, floor, roof
    std::vector<WallOpening> openings;                            // wall
    RoofType roofType = RoofType::FLAT;                           // roof
    float peakHeight = 0, overhang = 0;                           // roof
    float totalHeight = 0, riseHeight = 0, treadDepth = 0;        // stairs
    float innerRadius = 0, outerRadius = 0, totalAngle = 0;       // spiral
    std::vector<glm::vec3> points;                                // path waypoints
    int samplesPerSegment = 0;                                    // path

    nlohmann::json toJson() const;
    static std::optional<MeshRecipe> fromJson(const nlohmann::json& j);  // nullopt = invalid
};
```

`WallOpening` and `RoofType` move from `procedural_mesh.h` into
`mesh_recipe.h`; `procedural_mesh.h` includes it, so their users are
unchanged.

### 4.2 Where the recipe lives

`Mesh` (`engine/renderer/mesh.h`) gains:

```cpp
void setRecipe(std::shared_ptr<const MeshRecipe> recipe);
const MeshRecipe* getRecipe() const;   // nullptr when the mesh has none
```

The move constructor and move assignment move it. The recipe is on the mesh,
not on `MeshRenderer`, so anything that swaps the mesh swaps the recipe with
it: `MeshRenderer::setMesh`, `CutoutOpeningCommand` undo and redo, and
component clone, which shares the mesh pointer.

### 4.3 Building

`ProceduralMeshBuilder` gains:

```cpp
static ProceduralMeshData generatePath(const std::vector<glm::vec3>& points,
                                       float width, int samplesPerSegment);
static Mesh createPath(const std::vector<glm::vec3>& points,
                       float width, int samplesPerSegment);
static Mesh build(const MeshRecipe& recipe);   // dispatches on recipe.generator
```

`generatePath` is the body of `PathTool::generatePathMesh`, moved; the tool
calls `createPath`. Every `create*` function stamps the recipe of its own
arguments onto the mesh it returns. So every call site in § 2 gets a recipe
with no change of its own, and a recipe cannot disagree with its geometry.
`createWall` stamps a `WALL` recipe with no openings, and
`createWallWithOpenings` one with its openings; `build` calls
`createWallWithOpenings` when `openings` is not empty, else `createWall`.

### 4.4 The saved form

`serializeMeshRenderer` writes `"mesh"` when the mesh has a key, as now.
Otherwise, when it has a recipe, it writes `"procedural": recipe.toJson()`.
A mesh with neither is still not saved, as now.

```json
"procedural": {
  "generator": "wall",
  "width": 3.0, "height": 3.0, "thickness": 0.3,
  "openings": [ { "x": 1.0, "y": 0.0, "width": 1.0, "height": 2.1 } ]
}
```

Generator names: `"wall"`, `"floor"`, `"roof"`, `"stairs_straight"`,
`"stairs_spiral"`, `"path"`. Fields per generator:

| Generator | Fields |
|---|---|
| `wall` | `width`, `height`, `thickness`, `openings` (array, may be absent or empty) |
| `floor` | `width`, `depth`, `thickness` |
| `roof` | `type` (`"flat"`, `"gable"`, `"shed"`), `width`, `depth`, `peakHeight`, `overhang` |
| `stairs_straight` | `totalHeight`, `riseHeight`, `treadDepth`, `width` |
| `stairs_spiral` | `totalHeight`, `riseHeight`, `innerRadius`, `outerRadius`, `totalAngle` |
| `path` | `points` (array of `[x, y, z]`), `width`, `samplesPerSegment` |

`deserializeMeshRenderer` reads `"mesh"` first, as now. When it is absent and
`"procedural"` is present, it calls `MeshRecipe::fromJson`. A valid recipe is
built with `ProceduralMeshBuilder::build`. An invalid one logs a warning naming
the entity, and the entity loads with no `MeshRenderer`.

### 4.5 Limits on a loaded recipe

A scene file is untrusted input. `fromJson` returns `nullopt` unless every
check holds, so a hostile file cannot make the builder allocate without bound:

- `generator` is one of the six names, and `type` one of the three;
- every float is finite;
- every length, radius and height is greater than 0 and at most 1000 m,
  except `overhang`, `peakHeight` and the opening offsets, which are at least 0
  and at most 1000 m;
- `innerRadius` is less than `outerRadius`;
- `totalAngle` is greater than 0 and at most 3600 degrees;
- `totalHeight / riseHeight` is at most 1024 (the step count);
- `openings` holds at most 64 entries;
- `points` holds 2 to 4096 entries; `samplesPerSegment` is 1 to 64.

Each limit is far above anything the tools make; each bounds a loop or an
allocation in the builder.

### 4.6 Cutting an opening

`CutoutTool` reads the wall's `width`, `height` and `thickness` from the
mesh's `WALL` recipe, and builds the new mesh from the recipe's `openings` plus
the new one. Where the mesh has no `WALL` recipe it keeps today's behaviour:
size from the bounds, one opening.

That build now sits inside `drawConfigDialog`'s ImGui Apply button. It moves
to a public `CutoutTool::applyOpening(Scene&, CommandHistory&)`, which the
button calls. It acts on the wall `selectWall` chose, with the tool's public
opening fields.

## 5. Invariants

- **INV-1** — An entity made by each `EntityFactory` architectural preset,
  and by each of `WallTool`, `RoomTool`, `RoofTool`, `StairTool` (straight and
  spiral) and `PathTool`, still has a `MeshRenderer` after
  `serializeEntity` then `deserializeEntity`, and its mesh has the same local
  bounds and index count as the original. `createRoom` and `RoomTool` return
  a group with no mesh; for them this holds for each child that has one.
  *Test:* `tests/test_procedural_mesh_save.cpp`, one case per builder; the
  factory-wall case is the parked red test.
  *Breaks when:* a `create*` function does not stamp its recipe, or `build`
  dispatches a generator to the wrong function or argument order.

- **INV-2** — A wall cut twice keeps both openings: after two `CutoutTool`
  applies, the wall's recipe lists two openings, and the saved and reloaded
  wall has the same index count as one built directly with both.
  *Test:* `tests/test_procedural_mesh_save.cpp`
  `SecondCutKeepsTheFirstOpening`.
  *Breaks when:* `CutoutTool` builds from the new opening alone.

- **INV-3** — Undoing a cut restores the earlier recipe: cut, undo, save
  gives a wall with no openings.
  *Test:* `tests/test_cutout_opening_command.cpp` `UndoRestoresTheRecipe`.
  *Breaks when:* the recipe is kept anywhere the mesh swap does not reach,
  such as on `MeshRenderer`.

- **INV-4** — A keyed mesh is saved by its key alone: a `MeshRenderer` on
  `getCubeMesh()`, after a `WALL` recipe is set on that mesh, saves
  `"mesh": "__builtin_cube"` and no `"procedural"`.
  *Test:* `tests/test_procedural_mesh_save.cpp`
  `KeyedMeshSavesKeyNotRecipe`.
  *Breaks when:* the writer checks the recipe before the key.

- **INV-5** — A recipe that breaks any limit in § 4.5 is refused, and the
  entity loads with no `MeshRenderer`. This is the trust boundary.
  *Test:* `tests/test_mesh_recipe.cpp`, one case per limit: each fixture is a
  valid recipe with one check broken, and `fromJson` returns `nullopt`. For a
  numeric bound, a twin with the field at the bound returns a value; for a
  strict bound (greater than, less than) the twin takes the nearest value that
  passes. The finiteness and name checks have no twin.
  `tests/test_procedural_mesh_save.cpp` `RefusedRecipeLoadsWithoutMeshRenderer`
  loads an entity whose recipe breaks a limit.
  *Breaks when:* any one check is missing. The twin shows the refusal comes
  from that check and not from another.

- **INV-6** — `toJson` then `fromJson` returns an equal recipe for every
  generator.
  *Test:* `tests/test_mesh_recipe.cpp` `RoundTripsEveryGenerator`.
  *Breaks when:* a field is written under one name and read under another.

- **INV-7** — A saved scene has `format_version` 3, and a version-2 scene
  still loads.
  *Test:* `tests/test_scene_serializer.cpp` `SavedSceneIsVersion3`, and
  `Version2SceneStillLoads`, which loads a scene whose `format_version` is 2.
  *Breaks when:* `CURRENT_FORMAT_VERSION` is not raised, or `migrateScene`
  refuses version 2.

## 6. Failure modes

- **A recipe field is missing.** `fromJson` returns `nullopt`, as for a
  broken limit. A missing `openings` is the one exception: it means none.
- **Both `"mesh"` and `"procedural"` are present** (a hand-edited file).
  `"mesh"` wins, as today.
- **The builder changes later.** A reloaded mesh is rebuilt by the current
  builder, so a builder fix (such as the 2026-11-06 stair and opening fixes)
  reaches saved scenes. A change that alters a generator's meaning for the
  same numbers changes saved scenes too; that is accepted (§ 8).
- **A scene saved before this fix.** Its built meshes were never written.
  Nothing can restore them.
- **An older engine opens a version-3 scene.** `migrateScene` refuses it as
  newer than supported. Without the version bump it would load the walls as
  empty entities, and saving would delete their recipes for good.

## 7. Tests

| Test | File | Locks |
|---|---|---|
| one case per builder | `tests/test_procedural_mesh_save.cpp` (new; GL fixture) | INV-1 |
| `SecondCutKeepsTheFirstOpening` | same | INV-2 |
| `KeyedMeshSavesKeyNotRecipe` | same | INV-4 |
| `RefusedRecipeLoadsWithoutMeshRenderer` | same | INV-5 |
| `UndoRestoresTheRecipe` | `tests/test_cutout_opening_command.cpp` | INV-3 |
| one case per limit, `RoundTripsEveryGenerator` | `tests/test_mesh_recipe.cpp` (new; no GL) | INV-5, INV-6 |
| `SavedSceneIsVersion3`, `Version2SceneStillLoads` | `tests/test_scene_serializer.cpp` | INV-7 |

Each new test is run red against the current code before the fix. No case
goes through ImGui: the `PathTool` case drives `processClick` and
`finishPath`, and the `CutoutTool` cases drive `selectWall` and
`applyOpening` (§ 4.6).

## 8. Alternatives considered (and rejected)

- **Save raw vertices and indices.** Works for any mesh, but a wall becomes
  kilobytes of numbers in a hand-readable scene file, a load must validate an
  arbitrary vertex buffer, and the openings are lost, so the second-cut defect
  stays. Rejected.
- **Encode the recipe in a resource-manager key**, as built-ins do
  (`"__builtin_sphere_32x16"`). Reuses `getMeshByKey`, but a wall's openings
  and a path's waypoints do not fit a key string, and keyed meshes are
  shared through the cache, so two walls with equal numbers would alias and a
  cut on one would need a copy. Rejected.
- **Keep the recipe on `MeshRenderer` or a new component.** Every mesh swap
  (`setMesh`, cutout undo and redo) would have to swap it too, and one that
  forgets leaves a recipe that disagrees with the geometry. Rejected for the
  mesh-held form (§ 4.2).
- **A side table in `ResourceManager`**, mesh pointer to recipe. Same swap
  problem as above, plus entries that outlive their mesh. Rejected.
- **Keep the scene format at version 2.** The field is optional, so an older
  engine could read the file; it would drop the walls and lose them on its
  next save (§ 6). Rejected.

## 9. Out of scope

- The demo fence cloth in `engine/core/engine.cpp`: built in code each run,
  never saved by a user.
- Meshes loaded from glTF files: saved through their model path, unchanged.
- `CutoutTool` accepting any mesh as a wall — tracked by 3D_E-0874 (lane-16
  lead `cutout selectWall`).
- The prefab `"version"` field: written by `PrefabSystem` and read by
  nothing, so it is left at 1. An older engine placing a new prefab gets the
  built meshes as empty entities, as today — deferred; not yet queued.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/test_procedural_mesh_save.cpp`, one case per builder |
| INV-2 | `tests/test_procedural_mesh_save.cpp` `SecondCutKeepsTheFirstOpening` |
| INV-3 | `tests/test_cutout_opening_command.cpp` `UndoRestoresTheRecipe` |
| INV-4 | `tests/test_procedural_mesh_save.cpp` `KeyedMeshSavesKeyNotRecipe` |
| INV-5 | `tests/test_mesh_recipe.cpp` limit cases; `tests/test_procedural_mesh_save.cpp` `RefusedRecipeLoadsWithoutMeshRenderer` |
| INV-6 | `tests/test_mesh_recipe.cpp` `RoundTripsEveryGenerator` |
| INV-7 | `tests/test_scene_serializer.cpp` |
| A new builder call site gets a recipe | Partial: a site through `ProceduralMeshBuilder::create*` gets one by § 4.3; a site building vertices inline, as `PathTool` did, gets none and nothing catches it |

## 11. Cross-doc impact

- `CHANGELOG.md`: a Fixed entry, and a note that scenes now save as format 3.
- `ROADMAP.md`: 3D_E-0967 shipped; the lane-16 cutout row unblocked.
- No other spec binds `serializeMeshRenderer`, the scene format version or
  `ProceduralMeshBuilder` (`invariant_check` over the touched files,
  2026-11-09; `3D_E-0874-asset-relative-paths.md` cites
  `entity_serializer.cpp` for texture paths only).

## 12. Cold-eyes loop log

Rows live in `../reviews/3D_E-0967-procedural-mesh-save-loop-log.md`.

## 13. Migration / compatibility

- **Reading old scenes:** version 2 has no `"procedural"`; `migrateV2toV3` is
  a documented no-op, as `migrateV1toV2` is.
- **Writing:** every save writes version 3.
- **Older engines:** refuse version-3 scenes (§ 6).
- **Prefabs:** see § 9.
