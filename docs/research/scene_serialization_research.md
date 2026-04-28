# Scene Serialization Research

Research findings for implementing scene save/load in the Vestige engine.
Conducted 2026-03-20.

---

## 1. JSON Scene File Format Design

### How Major Engines Structure Scene Files

#### Unity (YAML-based)
Unity serializes each object (GameObject, Component) as a separate YAML document in the scene file. Each document begins with a header `--- !u!{ClassID} &{FileID}` where ClassID identifies the Unity class type and FileID uniquely identifies that object within the file. Objects reference each other using `{fileID: N}` notation.

A typical Unity scene entry:
```yaml
--- !u!1 &6
GameObject:
  m_Name: Cube
  m_Component:
  - 4: {fileID: 8}
  - 33: {fileID: 12}
  - 23: {fileID: 11}

--- !u!4 &8
Transform:
  m_GameObject: {fileID: 6}
  m_LocalPosition: {x: -2.618721, y: 1.028581, z: 1.131627}
  m_LocalRotation: {x: 0.0, y: 0.0, z: 0.0, w: 1.0}
  m_LocalScale: {x: 1.0, y: 1.0, z: 1.0}
  m_Children: []
  m_Father: {fileID: 0}
```

Key Unity design decisions:
- **Flat structure**: All objects at the same level, linked by fileID references
- **Hierarchy via Transform**: Parent-child stored in Transform's `m_Father` and `m_Children`
- **External assets via GUID**: References to assets outside the scene use `{fileID: X, guid: Y}` where the GUID comes from the asset's `.meta` sidecar file
- **Properties prefixed with `m_`**: Follows C++ member naming convention

Sources:
- [Unity Manual: Format of Text Serialized Files](https://docs.unity3d.com/Manual/FormatDescription.html)
- [Unity Blog: Understanding Unity's Serialization Language, YAML](https://unity.com/blog/engine-platform/understanding-unitys-serialization-language-yaml)
- [Unity Manual: YAML Scene Example](https://docs.unity3d.com/Manual/YAMLSceneExample.html)

#### Godot (TSCN text format)
Godot's `.tscn` format uses a custom INI-like text format with sections. The file starts with a descriptor: `[gd_scene load_steps=4 format=3 uid="uid://..."]`.

Sections:
- `[ext_resource]` - References to external files: `[ext_resource type="Material" uid="uid://c4cp0al3ljsjv" path="res://material.tres" id="1_7bt6s"]`
- `[sub_resource]` - Inline resources defined within the scene file: `[sub_resource type="SphereShape3D" id="SphereShape3D_tj6p1"]`
- `[node]` - Scene tree nodes: `[node name="CollisionShape3D" type="CollisionShape3D" parent="."]`
- `[connection]` - Signal connections between nodes

Key Godot design decisions:
- **Hierarchy via parent path**: Nodes specify their parent using a path string (`parent="."` for direct child of root, `parent="Player/Head"` for deeper nesting)
- **First node is root**: The first `[node]` has no `parent` field and becomes the scene root
- **Resource IDs**: External and internal resources get string IDs, referenced as `ExtResource("id")` or `SubResource("id")`
- **UIDs for asset tracking**: Godot 4.x added UIDs alongside paths for rename resilience
- **Default values omitted**: Properties equal to defaults are not stored
- **Binary compilation**: TSCN files are compiled to binary `.scn` in `.import/` for fast loading

Sources:
- [Godot 4.4 Docs: TSCN File Format](https://docs.godotengine.org/en/4.4/contributing/development/file_formats/tscn.html)
- [Godot Engine: TSCN - DEV Community](https://dev.to/winstonyallow/godot-engine-tscn-lfb)
- [Godot Issue #15673: Track resources with unique ID](https://github.com/godotengine/godot/issues/15673)

#### Open 3D Engine (O3DE) (JSON-based)
O3DE uses JSON for serializing objects, designed to be "easy for humans to read and edit, allowing contributors to make small changes to data without requiring specialized tools." The system uses a reflection framework where C++ types are registered with a SerializeContext, and serialization is fully deterministic based on the C++ type.

Source:
- [O3DE Docs: JSON Object Serialization System](https://www.docs.o3de.org/docs/user-guide/programming/serialization/json-intro/)

#### Open-Source Engine Example (Learn Engine Dev)
A simpler JSON scene format using nested hierarchy:
```json
{
  "name": "Root",
  "type": "Node2D",
  "tags": [],
  "components": [
    {
      "transform2D": {
        "position": { "x": 0.0, "y": 0.0 },
        "scale": { "x": 1.0, "y": 1.0 },
        "rotation": 0.0
      }
    },
    {
      "sprite": {
        "texture_path": "assets/textures/image.png",
        "draw_source": { "x": 0, "y": 0, "width": 100, "height": 100 },
        "enabled": true
      }
    }
  ],
  "children": [
    {
      "name": "Child",
      "type": "Node2D",
      "components": [...],
      "children": []
    }
  ]
}
```

Source:
- [Learn Engine Dev: Creating Scene JSON Files](https://chukobyte.github.io/learn-engine-dev/1.foundation/7.serializing_with_json/creating_scene_json_files/)

### Hierarchy Representation: Flat vs. Nested

Two main approaches exist:

**Flat list with parent IDs** (Unity-style):
```json
{
  "entities": [
    { "id": 1, "name": "Root", "parent": null, "components": {...} },
    { "id": 2, "name": "Camera", "parent": 1, "components": {...} },
    { "id": 3, "name": "Light", "parent": 1, "components": {...} }
  ]
}
```
- Pros: Easy to iterate all entities, simple add/remove, natural for ECS flat storage
- Cons: Must reconstruct tree on load, harder to read hierarchy visually

**Nested children array** (Godot/scene-graph-style):
```json
{
  "name": "Root",
  "children": [
    { "name": "Camera", "children": [] },
    { "name": "Light", "children": [] }
  ]
}
```
- Pros: Hierarchy immediately visible in file, natural for tree operations
- Cons: Harder to iterate all entities, deeper nesting harder to parse

**Recommendation for Vestige**: Use a **flat list with parent IDs**. This aligns better with ECS-like architecture where entities are stored contiguously, and the TransformComponent already tracks parent relationships. The flat format is also easier to diff in version control.

Sources:
- [GameDev.net: Parent-Child Relationships in ECS](https://www.gamedev.net/forums/topic/710863-what-is-the-best-design-for-handling-parent-child-relationships-in-entity-component-system/)
- [GameDev.net: Scene Graphs in an Entity-Component Framework](https://gamedev.net/forums/topic/681592-scene-graphs-in-an-entity-component-framework/5307432/)

---

## 2. Asset Reference Strategies

### Path-Based References
Assets referenced by relative filesystem path: `"texture": "assets/textures/granite_diffuse.png"`

**Pros:**
- Dead simple to implement
- Easy to debug -- you can see exactly what file is referenced
- Works well for small projects and solo developers
- No additional tooling needed

**Cons:**
- Renaming or moving an asset breaks all references
- Difficult to detect broken references without scanning all scene files
- Name collisions possible across directories

### GUID/UUID-Based References
Assets referenced by a 128-bit UUID: `"texture": "550e8400-e29b-41d4-a716-446655440000"`

**Pros:**
- Assets can be renamed/moved without breaking references
- Guaranteed unique identification across the entire project
- No collision risk

**Cons:**
- Unreadable -- requires a lookup tool to know what asset is referenced
- Needs a mapping database (UUID -> current path)
- More complex tooling required
- Debugging is harder

### Unity's Hybrid Approach (GUID + .meta files)
Unity creates a `.meta` sidecar file alongside every asset. The `.meta` file contains the asset's GUID and import settings. When a material references a texture, it stores the texture's GUID. Moving/renaming in the Unity Editor automatically updates the `.meta` file mapping.

**Critical issue**: If an asset loses its `.meta` file (e.g., moved outside the editor), Unity generates a new GUID and all references break.

Sources:
- [Unity at Scale: Understanding Meta Files and GUIDs](https://unityatscale.com/unity-meta-file-guide/understanding-meta-files-and-guids/)
- [Game Development by Sean: Asset Identifiers](https://seanmiddleditch.github.io/asset-identifiers/)

### Names vs. GUIDs (Role vs. Identity)
An important conceptual distinction from Noel Llopis at Gamasutra:
- **Names** specify an object's *role* (e.g., `head/left_eye`) -- they enable "late binding" and adapt when objects are replaced
- **GUIDs** specify an object's *identity* (e.g., `90e2294e-...`) -- permanent, unchanging, fast lookup

Use names when objects have meaningful roles and runtime flexibility matters. Use GUIDs when references must be immutable and object identity matters more than role.

Source:
- [Gamasutra: Referencing Objects: Names vs GUIDs](https://www.gamedeveloper.com/programming/referencing-objects-names-vs-guids)

### Recommendation for Vestige
Start with **relative paths** for simplicity. The project is small and solo-developed, so the main downside (rename breakage) is manageable. Add a path validation step on scene load that warns about missing assets. Consider adding UUIDs later if the asset library grows large enough to warrant it -- the migration path is straightforward (add UUID field to each asset reference, fall back to path if UUID not found).

---

## 3. Scene Versioning

### Version Number in File Header
The most common approach: embed a format version integer at the top of the scene file.

```json
{
  "vestige_scene": {
    "format_version": 1,
    "engine_version": "0.5.0",
    "entities": [...]
  }
}
```

When loading, compare `format_version` against the current expected version. If they differ, run migration logic.

### Conditional Serialization (Gabriel Sassone's approach)
Used in binary formats but the concept applies to JSON. Version-conditional logic in the serializer:
```cpp
if (version > 0)
    serialize(&data->v1_padding);
if (version > 2)
    serialize(&data->v3_new_field);
```
Old fields are kept in the struct with valid defaults for backward compatibility.

Source:
- [Gabriel's Virtual Tavern: Serialization for Games](https://jorenjoestar.github.io/post/serialization_for_games/)

### O3DE's Version Converter System (Most Sophisticated)
O3DE provides the most mature open-source versioning system found. Each reflected class has a version number:
```cpp
serializeContext->Class<MyComponent>()
    ->Version(2, &MyVersionConverter)
    ->Field("Data", &MyComponent::m_data);
```

Converters handle three kinds of changes:
1. **Name changes**: `->NameChange(4, 5, "OldName", "NewName")`
2. **Type changes**: `->TypeChange<int, float>("Field", 4, 5, [](int in) -> float { return (float)in; })`
3. **Structural changes**: Full converter functions that manipulate the serialized data tree

Converters can be **chained** (v1 -> v2 -> v3) or **skip versions** (v1 -> v3 directly) to avoid data loss through intermediate conversions. Classes can also be **deprecated** via `ClassDeprecate()`, causing old instances to be silently discarded.

Source:
- [O3DE Docs: Versioning Your Component Serialization](https://docs.o3de.org/docs/user-guide/programming/components/reflection/serialization-context/versioning/)

### Gaffer on Games: CRC32 Checksums
For network serialization (applicable to file formats too), Glenn Fiedler recommends including a CRC32 checksum of the serialization protocol in the header. This detects version mismatches and corruption automatically without explicit version tracking.

Source:
- [Gaffer on Games: Serialization Strategies](https://gafferongames.com/post/serialization_strategies/)

### USD Schema Versioning
Pixar's USD format generates versioned classes for each schema version (e.g., `UsdGeomSphere_0`, `UsdGeomSphere_1`) with a typedef pointing the unversioned name to the latest version.

Source:
- [OpenUSD: Schema Versioning](https://openusd.org/release/wp_schema_versioning.html)

### Recommendation for Vestige
Use a simple **integer format_version** in the scene file header. Implement a `migrateScene(json, fromVersion, toVersion)` function that applies sequential migration steps. Keep it simple:
1. Version 1: Initial format
2. When format changes, increment version and add a migration function
3. Each migration function transforms JSON from version N to version N+1
4. Chain migrations: loading a v1 file in a v3 engine runs migrate(1->2) then migrate(2->3)
5. Reject files with versions newer than the engine supports (no forward compatibility needed yet)

---

## 4. C++ JSON Serialization Libraries

### Performance Benchmarks (1M iterations, M1, Clang 17)

| Library | Write (MB/s) | Read (MB/s) | Roundtrip (s) |
|---------|-------------|------------|---------------|
| Glaze | 1396 | 1200 | 1.01 |
| simdjson | N/A (read-only) | 1163 | N/A |
| yyjson | 1023 | 1106 | 1.22 |
| RapidJSON | 289 | 416 | 3.76 |
| Boost.JSON | 198 | 308 | 5.38 |
| nlohmann/json | 86 | 81 | 15.44 |

Source:
- [GitHub: json_performance benchmarks](https://github.com/stephenberry/json_performance)

### Library Comparison

#### nlohmann/json (already in Vestige)
- **Pros**: Best-in-class API ergonomics, intuitive `json["key"]` syntax, excellent documentation, header-only, MIT license, huge community, exception-based and non-exception error handling (`parse(..., nullptr, false)` returns `discarded` on error), `NLOHMANN_DEFINE_TYPE_INTRUSIVE` macro for automatic struct serialization
- **Cons**: Slowest of all major libraries (81 MB/s read), higher memory usage
- **Error handling**: Typed exceptions (`parse_error`, `type_error`, `out_of_range`), `at()` for safe access with exceptions, `value()` for access with defaults, `contains()` for key checking
- **Best for**: Editor tools, scene files, configuration -- where developer productivity matters more than raw speed

Sources:
- [GitHub: nlohmann/json](https://github.com/nlohmann/json)
- [nlohmann/json: Exceptions](https://json.nlohmann.me/home/exceptions/)
- [nlohmann/json: Parse Exceptions](https://json.nlohmann.me/features/parsing/parse_exceptions/)

#### RapidJSON
- **Pros**: 4-5x faster than nlohmann, SAX and DOM parsing, small memory footprint, fine-grained memory control with custom allocators
- **Cons**: Verbose API (no `operator[]` sugar), manual type checking, harder to write clean code
- **Best for**: Runtime hot paths, large file parsing, memory-constrained environments

Source:
- [RapidJSON: Performance](https://rapidjson.org/md_doc_performance.html)

#### simdjson
- **Pros**: Fastest reader using SIMD instructions (1163 MB/s), on-demand parsing (lazy evaluation)
- **Cons**: Read-only (no serialization/writing), performance degrades badly when keys are out of expected order (drops to 89 MB/s), no custom allocator support
- **Best for**: Read-only ingestion of large JSON datasets

Sources:
- [Daniel Lemire: simdjson vs JSON for Modern C++](https://lemire.me/blog/2019/08/02/json-parsing-simdjson-vs-json-for-modern-c/)

#### Glaze
- **Pros**: Fastest overall (1396 MB/s write, 1200 MB/s read), compile-time reflection (no macros for aggregate types), `std::expected` error handling (no exceptions), binary format (BEVE) support
- **Cons**: Requires C++23 minimum, GCC 13+/Clang 18+, newer library with smaller community
- **Best for**: Performance-critical serialization in modern C++ projects

Source:
- [GitHub: stephenberry/glaze](https://github.com/stephenberry/glaze)

### Recommendation for Vestige
**Keep nlohmann/json**. It is already integrated, the API is excellent for editor tooling, and scene file I/O is not a hot path (files are loaded once at scene open, saved on explicit user action). At 81 MB/s, parsing a 1 MB scene file takes ~12 ms -- imperceptible. If profiling ever shows JSON parsing is a bottleneck (unlikely for scene files), consider Glaze as an upgrade path, but only after moving to C++23.

---

## 5. Component Serialization Patterns

### Pattern 1: Macro-Based Field Declaration (nlohmann)
Each component struct declares its serializable fields with a macro:
```cpp
struct TransformComponent
{
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f};
    glm::vec3 scale{1.0f};

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(TransformComponent, position, rotation, scale)
};
```
This auto-generates `to_json` and `from_json` functions.

- **Pros**: Minimal boilerplate, compile-time checked
- **Cons**: Macro-based, no versioning, all fields must be listed
- **Best for**: Simple components that rarely change structure

Source:
- [EnTT Serialization with nlohmann::json](https://thomas.trocha.com/blog/entt-de-serialization-with-nlohmann-json/)

### Pattern 2: Type Registry + Factory Pattern
A central registry maps type name strings to factory functions:
```cpp
class ComponentRegistry
{
    std::unordered_map<std::string, CreateFunc> m_factories;
    std::unordered_map<std::string, SerializeFunc> m_serializers;
    std::unordered_map<std::string, DeserializeFunc> m_deserializers;

public:
    template<typename T>
    void registerComponent(const std::string& name) { ... }

    std::unique_ptr<Component> create(const std::string& name) { ... }
    json serialize(const std::string& name, const Component& c) { ... }
    std::unique_ptr<Component> deserialize(const std::string& name, const json& j) { ... }
};
```

- **Pros**: Decoupled -- components register themselves, no central switch statement, supports polymorphism
- **Cons**: Runtime lookup cost (negligible for scene load), requires explicit registration
- **Best for**: Engines with many component types, plugin systems

Sources:
- [IndieGameDev: Automatic Serialization in C++ for Game Engines](https://indiegamedev.net/2022/03/28/automatic-serialization-in-cpp-for-game-engines/)
- [GameDev.net: Banshee Engine RTTI & Serialization](https://www.gamedev.net/tutorials/programming/engines-and-middleware/banshee-engine-architecture-rtti-serialization-r3910/)

### Pattern 3: EnTT Snapshot System
EnTT provides a built-in snapshot system. You implement archive classes that EnTT calls back with entity/component data:
```cpp
// Serialize
NJSONOutputArchive archive;
entt::basic_snapshot snapshot(registry);
snapshot.entities(archive)
    .component<Transform, MeshRenderer, Light>(archive);
std::string json = archive.AsString();

// Deserialize
NJSONInputArchive input(json);
entt::basic_snapshot_loader loader(registry);
loader.entities(input)
    .component<Transform, MeshRenderer, Light>(input);
```

Critical: Component types must be serialized and deserialized in the **same order**.

Source:
- [EnTT Serialization with nlohmann::json](https://thomas.trocha.com/blog/entt-de-serialization-with-nlohmann-json/)

### Pattern 4: Visitor / Double Dispatch
A Serializer visits each component, and each component accepts the visitor:
```cpp
class Serializer
{
public:
    virtual void visit(TransformComponent&) = 0;
    virtual void visit(MeshComponent&) = 0;
    virtual void visit(LightComponent&) = 0;
};

// JsonSerializer, BinarySerializer, etc. implement Serializer
```

- **Pros**: Clean separation, supports multiple output formats
- **Cons**: Adding a new component requires updating all serializers (the "expression problem")

Source:
- [Hands-On Design Patterns with C++: Serialization with Visitor](https://www.oreilly.com/library/view/hands-on-design-patterns/9781788832564/290a90f1-1651-47e8-ac53-c820134d3e4f.xhtml)

### Pattern 5: Reflection-Based (O3DE, Flecs)
Components are reflected at registration time, and the serialization system uses that metadata:
```cpp
serializeContext->Class<TransformComponent>()
    ->Version(1)
    ->Field("Position", &TransformComponent::m_position)
    ->Field("Rotation", &TransformComponent::m_rotation)
    ->Field("Scale", &TransformComponent::m_scale);
```

Flecs takes this further with an integrated reflection framework that can serialize an entire ECS world to/from JSON.

Sources:
- [O3DE: Versioning Component Serialization](https://docs.o3de.org/docs/user-guide/programming/components/reflection/serialization-context/versioning/)
- [Flecs ECS](https://github.com/SanderMertens/flecs)
- [CUBOS Engine: Implementing Serialization - Part 1](https://riscadoa.com/gamedev/cubos-serialization-1/)
- [CUBOS Engine: Implementing Serialization - Part 2](https://riscadoa.com/gamedev/cubos-serialization-2/)

### Recommendation for Vestige
Use a **hybrid of Pattern 1 + Pattern 2**:
1. Each component uses `NLOHMANN_DEFINE_TYPE_INTRUSIVE` (or custom `to_json`/`from_json`) for field-level serialization
2. A `ComponentRegistry` maps string type names to serialize/deserialize lambdas
3. Components self-register via a static initializer or explicit `registerComponents()` call
4. The scene serializer iterates entities, looks up each component's type name in the registry, and calls the appropriate serializer

This gives us nlohmann's ergonomic macros for individual components plus a registry for handling the polymorphic "which component type is this?" problem during deserialization.

---

## 6. Auto-Save Strategies

### Timer + Dirty Flag
The simplest approach: a timer fires every N minutes, checks if the scene is dirty (modified since last save), and saves if so.
```cpp
// In editor update loop
m_autoSaveTimer += deltaTime;
if (m_autoSaveTimer >= AUTO_SAVE_INTERVAL && m_sceneDirty)
{
    saveScene("autosave.json");
    m_autoSaveTimer = 0.0f;
    m_sceneDirty = false;
}
```

Source:
- [CodeProject: Autosave and Crash Recovery](https://www.codeproject.com/Articles/324/Autosave-and-Crash-Recovery)

### Background Thread Saving
To avoid frame hitches during auto-save:
1. Take a snapshot of scene data (serialize to string/buffer on main thread -- fast)
2. Hand the buffer to a background thread for file I/O (slow disk write doesn't block rendering)

The key challenge is thread safety: the snapshot must be a consistent copy. Options:
- **Serialize to string on main thread**, then write string to file on background thread (simplest, recommended)
- **Copy-on-write via fork()** (Linux): fork a child process; the OS provides copy-on-write pages so the child has a consistent snapshot without explicit copying. Used by Redis for RDB persistence. Not portable to Windows.
- **Journal/delta approach**: Record individual changes as a log; periodically flush the journal. More complex, better for very large scenes.

Sources:
- [Satisfactory Feature Request: Multithreaded Autosave](https://questions.satisfactorygame.com/post/625a4483ca608e080350c44f)
- [Factorio Forums: Save in Background](https://forums.factorio.com/viewtopic.php?t=25640)

### Atomic File Writes (Crash Safety)
Always write to a temporary file, then atomically rename:
```cpp
void safeWriteFile(const std::string& path, const std::string& content)
{
    std::string tmpPath = path + ".tmp";

    // 1. Write to temporary file
    std::ofstream out(tmpPath);
    out << content;
    out.flush();
    out.close();

    // 2. Sync to disk (fsync)
    // (platform-specific: fsync on Linux, FlushFileBuffers on Windows)

    // 3. Atomic rename
    std::filesystem::rename(tmpPath, path);
}
```

The rename is atomic on POSIX systems when source and destination are on the same filesystem. If the process crashes during write, only the `.tmp` file is corrupted; the original file remains intact.

Sources:
- [GitHub Gist: Safely Writing Files Atomically](https://gist.github.com/datenwolf/a8f5d194b268659e3d37)
- [Tech Champion: Atomic Writes + Temp File Patterns](https://tech-champion.com/data-science/stop-silent-data-loss-checksum-atomic-writes-temp-file-patterns/)

### Crash Recovery
On editor startup, check for:
1. `.tmp` files alongside scene files (interrupted save -- delete the temp file)
2. `autosave.json` files newer than the scene file (offer to restore)
3. A dirty flag or journal file from the previous session

Unity stores backup copies and offers recovery after a crash. Gaea (terrain editor) maintains a parallel autosave that can be recovered from.

Sources:
- [Single Pixel Games: Recovering Unsaved Scene After Unity Crash](https://singlepixelgames.com/post/recovering-an-unsaved-scene-after-a-unity-crash)
- [Gaea Docs: File Recovery and Autosave](https://docs.quadspinner.com/Guide/Using-Gaea/Recovery.html)

### Recommendation for Vestige
1. **Timer + dirty flag** for auto-save triggering (every 2-3 minutes)
2. **Serialize to JSON string on main thread** (fast -- sub-millisecond for typical scenes)
3. **Write to file on background thread** using `std::async` or a dedicated I/O thread
4. **Atomic writes** via write-to-temp + rename pattern
5. **Crash recovery** on startup: detect autosave files and offer restore
6. Save to `<scene_name>.autosave.vestige` alongside the main scene file
7. Delete the autosave file after a successful explicit save

---

## Summary of Recommendations for Vestige

| Topic | Recommendation |
|-------|---------------|
| File format | JSON with flat entity list + parent IDs |
| Hierarchy | Flat array, parent references by entity ID |
| Asset references | Relative paths (add UUIDs later if needed) |
| Versioning | Integer format_version + sequential migration functions |
| JSON library | Keep nlohmann/json (already integrated, adequate performance) |
| Component serialization | nlohmann macros + ComponentRegistry for type dispatch |
| Auto-save | Timer + dirty flag, serialize on main thread, write on background thread |
| Crash safety | Atomic write (temp file + rename), autosave recovery on startup |
