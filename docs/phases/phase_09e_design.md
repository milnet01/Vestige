# Phase 9E Design Document: Visual Scripting

**Date:** 2026-04-11
**Status:** Implemented for 9E-1 and 9E-2 (core infrastructure + EventBus bridge + 60 node types); 9E-3 (editor UI) in progress — Steps 1–3 shipped (library integration, pin-name interning, type→IDs cache, per-execution pure-node memoization, Gate entryPin), Step 4 (canvas + menu) WIP, Steps 5–16 remaining. See `docs/phases/phase_09e3_design.md` §13 Acceptance Criteria.

---

## 1. Goal

Enable designers to build interactive gameplay logic without writing C++, using a
node-based visual graph editor integrated into the Vestige editor. The system
prioritizes **debuggability and iteration speed** over maximum raw performance.

### Success Criteria

- A designer can wire together "player enters trigger zone -> play sound + open door"
  without touching C++ code
- Scripts are hot-reloadable (change graph while the engine runs in editor mode)
- Execution is debuggable (breakpoints, value inspection, node highlighting)
- Performance overhead is negligible for typical gameplay logic (<0.5ms per frame
  for 50 active scripts)
- The system integrates with all existing domain systems via the EventBus

---

## 2. Research Summary

### 2.1 Engine Survey

| Engine | Execution Model | Key Lesson |
|--------|----------------|------------|
| **Unreal Blueprints** | Stack-based bytecode VM, 5-10x slower than C++ | Hybrid push-pull (exec pins + lazy data). Nativization (compile to C++) was deprecated in UE5 -- complexity not worth the benefit |
| **Godot VisualScript** | Graph interpreter, removed in Godot 4.0 | Failed (0.5% adoption). Too low-level -- must provide **high-level gameplay nodes**, not just bare wiring |
| **Unity Bolt** | Graph interpreter with Flow + State graphs | 6-level variable scoping model is the industry standard |
| **ezEngine** | Graph interpreter calling C++ functions | Most relevant to Vestige. Coroutine support (Yield, Wait, overlap modes). Scripts are "glue code between systems" |
| **Flax Engine** | Impulse-driven interpreter, thread-local stacks | Debug breakpoints and stepping. `alloca()`-based parameter passing for zero-allocation execution |
| **FlowGraph (UE plugin)** | Async/event-driven, nodes subscribe to delegates | Perfect match for Vestige's EventBus. Nodes react to events rather than polling |

**Key takeaway from Godot's failure:** Visual scripting must be bundled with high-level
gameplay nodes (door, checkpoint, collectible, patrol AI). Bare wiring of low-level
operations is worse than text scripting for everyone.

### 2.2 Node Editor Library

| Library | Stars | Zoom | Minimap | Grouping | Flow Anim | Status |
|---------|-------|------|---------|----------|-----------|--------|
| **imgui-node-editor** (thedmd) | 4,384 | Yes | No | Yes | Yes | Active (Feb 2026) |
| **imnodes** (Nelarius) | 2,430 | **No** | Yes | No | No | Semi-active |
| **ImNodeFlow** (Fattorino) | 471 | Yes | No | No | No | Active |
| **ImNodes** (rokups) | 741 | Yes | No | No | No | Abandoned |

**Recommendation: imgui-node-editor** (thedmd). Zoom support is essential for large
graphs. Grouping enables comment boxes for organization. Flow animation provides
visual debugging feedback. MIT license, C++14, ImGui 1.72+ (compatible with Vestige).

The missing minimap can be implemented manually later; zoom + viewport culling is
more important for usability.

**Sources:**
- [imgui-node-editor (GitHub)](https://github.com/thedmd/imgui-node-editor)
- [imnodes (GitHub)](https://github.com/Nelarius/imnodes)
- [imnodes design blog](https://nelari.us/post/imnodes/)
- [ImGui Useful Extensions Wiki](https://github.com/ocornut/imgui/wiki/Useful-Extensions)

### 2.3 Execution Model

| Approach | Performance | Debug | Hot-Reload | Complexity |
|----------|------------|-------|------------|------------|
| **Graph interpreter** | Slowest (but adequate) | Excellent | Excellent | Low |
| **Bytecode VM** | 5-30% of native | Moderate | Moderate | High |
| **Code generation** | Native speed | Poor | Poor | Very high |

**Recommendation: Graph interpreter** (Phase 1). The nodes themselves call heavyweight
C++ functions (physics, audio, rendering). The interpreter overhead is negligible
compared to the work each node does. Bytecode compilation is a potential Phase 2
optimization if needed, but Unreal's experience shows 500 actors with simple logic
consume only ~1ms total even with their VM overhead.

**Sources:**
- [Game Programming Patterns - Bytecode](https://gameprogrammingpatterns.com/bytecode.html)
- [Crafting Interpreters - A Virtual Machine](https://craftinginterpreters.com/a-virtual-machine.html)
- [Blueprint Performance Guidelines](https://intaxwashere.github.io/blueprint-performance/)
- [Flax Engine VisualScript.cpp](https://github.com/FlaxEngine/FlaxEngine/blob/master/Source/Engine/Content/Assets/VisualScript.cpp)
- [ezEngine Visual Script Overview](https://ezengine.net/pages/docs/custom-code/visual-script/visual-script-overview.html)

---

## 3. Architecture

### 3.1 Overview

```
+-------------------+     +------------------+     +------------------+
|   Editor UI       |     |   Script Asset   |     |   Script Runtime |
| (imgui-node-      |<--->| (JSON graph      |---->| (Interpreter +   |
|  editor)          |     |  serialization)  |     |  Event Bridge)   |
+-------------------+     +------------------+     +------------------+
                                                           |
                                                           v
                                              +------------------------+
                                              |   Engine Systems       |
                                              | (EventBus, Audio,      |
                                              |  Physics, Navigation,  |
                                              |  Animation, etc.)      |
                                              +------------------------+
```

Three cleanly separated layers:
1. **Editor UI** -- visual graph editing, node palette, property panel
2. **Script Asset** -- serialized graph data (JSON), loadable as a resource
3. **Script Runtime** -- interprets graphs, bridges to engine systems

### 3.2 Relationship to Existing NodeGraph

The existing `engine/formula/node_graph.h` handles **formula/math node graphs** with
ExpressionTree conversion. The visual scripting system is a **separate system** because:

- Formula graphs are pure DAGs (no side effects, no execution flow)
- Visual scripts have execution pins, side effects, latent/async nodes, and variables
- The execution model is fundamentally different (pull-based data vs push-based events)

However, both systems share the same **editor library** (imgui-node-editor) and some
UI infrastructure (node rendering, connection drawing, serialization patterns).

### 3.3 Core Data Model

```cpp
// -- Pin types --
enum class PinKind { EXECUTION, DATA };

enum class DataType
{
    BOOL, INT, FLOAT, STRING,
    VEC2, VEC3, VEC4, QUAT,
    ENTITY, COLOR, ANY
};

struct Pin
{
    uint32_t id;
    std::string name;
    PinKind kind;           // EXECUTION or DATA
    PinDirection direction; // INPUT or OUTPUT
    DataType dataType;      // Only for DATA pins
    ScriptValue defaultValue;
};

// -- ScriptValue: type-erased value --
// Uses std::variant internally for small types, heap-allocated for strings/arrays.
using ScriptValue = std::variant<
    bool, int32_t, float, std::string,
    glm::vec2, glm::vec3, glm::vec4, glm::quat,
    uint32_t  // Entity ID
>;

// -- Script Node base --
struct ScriptNodeDef
{
    uint32_t id;
    std::string typeName;     // "Branch", "PlaySound", "OnKeyPressed", etc.
    std::vector<Pin> inputs;
    std::vector<Pin> outputs;
    float posX, posY;         // Editor layout
    std::map<std::string, ScriptValue> properties; // Node-specific config
};

// -- Connection --
struct ScriptConnection
{
    uint32_t id;
    uint32_t sourceNode, sourcePin;
    uint32_t targetNode, targetPin;
};

// -- Script Graph (the asset) --
struct ScriptGraph
{
    uint32_t version = 1;
    std::string name;
    std::vector<ScriptNodeDef> nodes;
    std::vector<ScriptConnection> connections;
    std::vector<VariableDef> variables; // Graph-scope variables
};
```

### 3.4 Node Type Registry

Nodes are **data-defined**, not class-per-node. A `NodeTypeRegistry` maps type names
to `NodeTypeDescriptor` structs that define pins, default properties, and an
`execute` function pointer.

```cpp
/// @brief Describes a node type available in the palette.
struct NodeTypeDescriptor
{
    std::string typeName;       // "PlaySound", "Branch", etc.
    std::string displayName;    // "Play Sound"
    std::string category;       // "Audio", "Flow Control", "Events"
    std::string tooltip;        // Hover description

    std::vector<PinDef> inputDefs;
    std::vector<PinDef> outputDefs;

    // For event nodes: which Event type this subscribes to (empty = not an event)
    std::string eventTypeName;

    // Is this a pure (data-only) node?
    bool isPure = false;

    // Is this a latent node (execution can suspend)?
    bool isLatent = false;

    // The execute function -- called when execution reaches this node
    // For pure nodes: called when output data is pulled
    using ExecuteFn = std::function<void(ScriptContext&, const ScriptNodeInstance&)>;
    ExecuteFn execute;
};
```

New node types can be registered from C++:

```cpp
registry.registerNode({
    .typeName = "PlaySound",
    .displayName = "Play Sound",
    .category = "Audio",
    .tooltip = "Play a sound effect at a 3D position",
    .inputDefs = {
        {PinKind::EXECUTION, "Exec", DataType::BOOL},
        {PinKind::DATA, "Clip Path", DataType::STRING},
        {PinKind::DATA, "Position", DataType::VEC3},
        {PinKind::DATA, "Volume", DataType::FLOAT, ScriptValue(1.0f)},
    },
    .outputDefs = {
        {PinKind::EXECUTION, "Then", DataType::BOOL},
    },
    .execute = [](ScriptContext& ctx, const ScriptNodeInstance& node) {
        auto path = ctx.readInput<std::string>(node, "Clip Path");
        auto pos  = ctx.readInput<glm::vec3>(node, "Position");
        auto vol  = ctx.readInput<float>(node, "Volume");
        ctx.engine().eventBus().publish(AudioPlayEvent(path, pos, vol));
        ctx.triggerOutput(node, "Then");
    },
});
```

---

## 4. Execution Model

### 4.1 Graph Interpreter

Execution is **impulse-driven**: events fire, and execution flows forward through
connected execution pins. Data is evaluated **lazily** by pulling from connected
data pins only when needed.

```
Event fires (e.g., KeyPressedEvent)
  |
  v
Event Node receives callback, calls triggerOutput("Pressed")
  |
  v
Interpreter follows execution connection to next node
  |
  v
Next node's execute() is called
  - It reads input data pins (lazy pull from connected outputs)
  - It performs its action (play sound, move entity, etc.)
  - It calls triggerOutput() to continue the chain
  |
  v
... continues until no more execution connections
```

### 4.2 ScriptContext

The `ScriptContext` is the runtime state for one execution of a graph. It provides:

- `readInput<T>(node, pinName)` -- pull data from connected output, or use default
- `triggerOutput(node, pinName)` -- follow execution connection to next node
- `getVariable(name, scope)` / `setVariable(name, scope, value)` -- variable access
- `engine()` -- access to Engine (EventBus, Systems, Scene)
- `suspend(resumeCallback)` -- for latent nodes (Delay, WaitForEvent)

```cpp
class ScriptContext
{
public:
    ScriptContext(ScriptInstance& instance, Engine& engine);

    // Data evaluation (pull)
    template <typename T>
    T readInput(const ScriptNodeInstance& node, const std::string& pinName);

    // Execution flow (push)
    void triggerOutput(const ScriptNodeInstance& node,
                       const std::string& pinName);

    // Variable access
    ScriptValue getVariable(const std::string& name, VariableScope scope) const;
    void setVariable(const std::string& name, VariableScope scope,
                     const ScriptValue& value);

    // Latent support
    void suspend(std::function<void()> onResume);
    void resume();

    // Engine access
    Engine& engine() { return m_engine; }

private:
    ScriptInstance& m_instance;
    Engine& m_engine;
    int m_callDepth = 0;
    static constexpr int MAX_CALL_DEPTH = 256;
};
```

### 4.3 Latent Nodes (Coroutine-style)

Latent nodes suspend execution and resume later. Implementation uses explicit state
snapshots (not C++20 coroutines -- simpler, more portable, serializable).

```
[OnTriggerEnter] -> [PlaySound("door_open")] -> [Delay(1.5s)] -> [SetPosition(...)]
                                                    ^
                                                    |
                                            Execution suspends here.
                                            After 1.5s, resumes at SetPosition.
```

The `Delay` node's execute function:

```cpp
.execute = [](ScriptContext& ctx, const ScriptNodeInstance& node) {
    float seconds = ctx.readInput<float>(node, "Duration");

    // Save the continuation: which output pin to fire when done
    ctx.suspend([&ctx, &node, seconds]() {
        // This lambda is called after 'seconds' have elapsed
        ctx.triggerOutput(node, "Completed");
    });

    // Register a timer with the engine
    ctx.engine().timerSystem().scheduleOnce(seconds, [&ctx]() {
        ctx.resume();
    });
},
```

**Active latent nodes are tracked per-instance.** If a graph is deactivated (entity
destroyed, scene unloaded), all pending latent actions are cancelled.

### 4.4 Safety Guards

- **Call depth limit:** MAX_CALL_DEPTH = 256. Prevents infinite recursion from
  cyclic execution flows. Logs an error and halts the current chain.
- **Execution time budget:** If a single chain executes more than 1000 nodes without
  suspending, halt with a warning. Prevents infinite loops in While/ForLoop.
- **NaN/Inf propagation:** Data pin reads clamp float values to sane ranges.
- **Null entity checks:** Entity access nodes validate that the entity still exists.

---

## 5. Variable System

### 5.1 Scoping (based on Unity's proven 6-level model)

| Scope | Lifetime | Vestige Mapping |
|-------|----------|-----------------|
| **Flow** | One execution chain | Local to the current `ScriptContext` |
| **Graph** | Graph instance | Per-`ScriptInstance` private data |
| **Entity** | Entity lifetime | Per-entity `ScriptComponent` data, visible in Inspector |
| **Scene** | Scene lifetime | Scene-level shared blackboard |
| **Application** | Session | Global engine blackboard |
| **Saved** | Persistent | Serialized to save file |

### 5.2 Blackboard Storage

Each scope uses a `Blackboard` -- a string-keyed map of `ScriptValue`:

```cpp
class Blackboard
{
public:
    void set(const std::string& key, const ScriptValue& value);
    ScriptValue get(const std::string& key) const;
    bool has(const std::string& key) const;
    void remove(const std::string& key);
    void clear();

    // Serialization
    nlohmann::json toJson() const;
    static Blackboard fromJson(const nlohmann::json& j);

private:
    std::unordered_map<std::string, ScriptValue> m_values;
};
```

### 5.3 Variable Nodes

- **Get Variable** (pure): outputs the current value from the specified scope
- **Set Variable** (impure): writes a value, passes execution through
- **Has Variable** (pure): returns bool
- **Variable Changed** (event): fires when a variable in a specific scope changes

Entity-scope variables are editable in the Inspector panel, enabling per-instance
configuration without touching the graph.

---

## 6. Node Type Catalog

### 6.1 Event Nodes (entry points, no exec input pin)

| Node | Fires When | Output Data Pins |
|------|-----------|------------------|
| **OnStart** | Graph first activated | -- |
| **OnUpdate** | Every frame (use sparingly) | deltaTime: float |
| **OnDestroy** | Entity about to be destroyed | -- |
| **OnKeyPressed** | Key pressed | keyCode: int |
| **OnKeyReleased** | Key released | keyCode: int |
| **OnMouseButton** | Mouse button pressed | button: int |
| **OnTriggerEnter** | Entity enters trigger zone | otherEntity: Entity |
| **OnTriggerExit** | Entity exits trigger zone | otherEntity: Entity |
| **OnCollisionEnter** | Physics collision begins | otherEntity: Entity, contactPoint: vec3, normal: vec3 |
| **OnCollisionExit** | Physics collision ends | otherEntity: Entity |
| **OnSceneLoaded** | Scene finishes loading | -- |
| **OnWeatherChanged** | Weather parameters change | temp: float, humidity: float, wind: float |
| **OnCustomEvent** | User-defined event name | configurable data |
| **OnVariableChanged** | Blackboard variable changes | oldValue: Any, newValue: Any |
| **OnAudioFinished** | Audio clip finishes playing | clipPath: string |

### 6.2 Action Nodes (impure, have exec pins)

| Node | Action | Key Inputs |
|------|--------|-----------|
| **PlaySound** | One-shot sound at position | clipPath, position, volume |
| **StopSound** | Stop a playing sound | sourceEntity |
| **SpawnEntity** | Create entity from prefab | prefabPath, position, rotation |
| **DestroyEntity** | Remove entity | entity |
| **SetPosition** | Move entity | entity, position |
| **SetRotation** | Rotate entity | entity, rotation |
| **SetScale** | Scale entity | entity, scale |
| **ApplyForce** | Physics force | entity, force, point |
| **ApplyImpulse** | Physics impulse | entity, impulse |
| **PlayAnimation** | Start animation clip | entity, clipName, blend |
| **SpawnParticles** | Emit particles | templateName, position |
| **SetMaterial** | Change entity material | entity, materialPath |
| **SetVisibility** | Show/hide entity | entity, visible |
| **SetLightColor** | Change light color | entity, color |
| **SetLightIntensity** | Change light brightness | entity, intensity |
| **FindPath** | Navmesh pathfinding (latent) | from, to -> path |
| **PublishEvent** | Fire engine event | eventName, data... |
| **PrintToScreen** | Debug text overlay | message, duration, color |
| **LogMessage** | Write to log | message, severity |
| **SetVariable** | Write to blackboard | name, scope, value |

### 6.3 Flow Control Nodes

| Node | Behavior |
|------|----------|
| **Branch** | If/else: routes execution based on boolean condition |
| **Switch (Int)** | Routes to one of N output pins by integer value |
| **Switch (String)** | Routes by string match |
| **Sequence** | Executes all outputs in order (Then 0, Then 1, ...) |
| **For Loop** | Counted iteration with index output |
| **For Each** | Iterate over array elements |
| **While Loop** | Repeat while condition is true |
| **Gate** | Controllable valve: Enter, Open, Close, Toggle inputs |
| **DoOnce** | Executes once, then blocks until reset |
| **FlipFlop** | Alternates between two output pins each time |

### 6.4 Latent Nodes (suspend and resume)

| Node | Behavior |
|------|----------|
| **Delay** | Wait N seconds, then continue |
| **WaitForEvent** | Wait for a specific EventBus event |
| **WaitForCondition** | Poll condition each frame until true |
| **Timeline** | Animate a float 0->1 over duration (Update + Finished outputs) |
| **MoveTo** | Move entity to position over time (uses nav if available) |

### 6.5 Pure / Data Nodes (no exec pins, lazy evaluation)

| Node | Operation |
|------|-----------|
| **GetPosition** | Read entity world position |
| **GetRotation** | Read entity rotation |
| **GetDistance** | Distance between two vec3 |
| **Raycast** | Physics ray query |
| **FindEntityByTag** | Lookup entity by tag string |
| **Math (Add, Sub, Mul, Div)** | Arithmetic |
| **MathClamp** | Clamp value to range |
| **MathLerp** | Linear interpolation |
| **VectorNormalize** | Normalize vec3 |
| **DotProduct / CrossProduct** | Vector math |
| **BoolAnd / BoolOr / BoolNot** | Boolean logic |
| **ToString** | Convert value to string |
| **GetVariable** | Read from blackboard |
| **HasVariable** | Check if variable exists |
| **CompareEqual / Less / Greater** | Comparison operators |

---

## 7. Engine Integration

### 7.1 ScriptComponent

An entity with visual scripting has a `ScriptComponent` that owns one or more
`ScriptInstance` objects (one per attached graph):

```cpp
class ScriptComponent
{
public:
    void addScript(const std::string& graphAssetPath);
    void removeScript(size_t index);

    // Per-entity blackboard (Entity scope variables)
    Blackboard& entityBlackboard() { return m_entityBlackboard; }

    // Runtime instances
    const std::vector<ScriptInstance>& instances() const { return m_instances; }

private:
    std::vector<ScriptInstance> m_instances;
    Blackboard m_entityBlackboard;
};
```

### 7.2 ScriptingSystem (ISystem)

A new domain system registered with SystemRegistry:

```cpp
class ScriptingSystem : public ISystem
{
public:
    const std::string& getSystemName() const override;  // "Scripting"
    bool initialize(Engine& engine) override;
    void shutdown() override;
    void update(float deltaTime) override;
    void onSceneLoad(Scene& scene) override;
    void onSceneUnload(Scene& scene) override;

    // Node type registration
    NodeTypeRegistry& nodeRegistry() { return m_nodeRegistry; }

    // Scene/Application blackboards
    Blackboard& sceneBlackboard() { return m_sceneBlackboard; }
    Blackboard& appBlackboard() { return m_appBlackboard; }

private:
    NodeTypeRegistry m_nodeRegistry;
    std::vector<ScriptInstance*> m_activeInstances;

    Blackboard m_sceneBlackboard;
    Blackboard m_appBlackboard;

    // Latent action management
    std::vector<PendingLatentAction> m_pendingActions;
};
```

**update()** responsibilities:
1. Tick OnUpdate event nodes for active scripts (only scripts that have OnUpdate)
2. Check and fire pending latent actions (timers, conditions)
3. Respect frame budget -- if over budget, defer remaining latent checks

**onSceneLoad()** responsibilities:
1. Find all entities with ScriptComponent
2. Load graph assets, create ScriptInstance objects
3. Subscribe event nodes to EventBus
4. Fire OnStart for all scripts

**onSceneUnload()** responsibilities:
1. Fire OnDestroy for all scripts
2. Unsubscribe all event nodes from EventBus
3. Cancel all pending latent actions
4. Clear scene blackboard

### 7.3 EventBus Bridge

Event nodes bridge between typed C++ events and the script graph:

```cpp
// During onSceneLoad, for each "OnKeyPressed" event node:
auto subId = eventBus.subscribe<KeyPressedEvent>(
    [instance, nodeId](const KeyPressedEvent& event) {
        ScriptContext ctx(*instance, engine);
        // Set the node's output data pins from event fields
        ctx.setNodeOutput(nodeId, "keyCode", event.keyCode);
        // Fire the execution output
        ctx.triggerOutput(nodeId, "Pressed");
    }
);
// Store subId for cleanup in onSceneUnload
```

This means visual scripts react to the **exact same events** that C++ systems use.
No special scripting-only event layer is needed.

---

## 8. Editor UI

### 8.1 Library Choice: imgui-node-editor

Integration plan:
1. Add `imgui-node-editor` source to `external/imgui-node-editor/`
2. Add to CMakeLists.txt as a library target
3. Suppress warnings from third-party source (same pattern as stb, glad)

### 8.2 Script Editor Panel

A new editor panel (`ScriptEditorPanel`) providing:

- **Node palette** (left sidebar): searchable, categorized list of all node types.
  Drag from palette to canvas to create nodes. Also: right-click canvas for
  context menu search.
- **Graph canvas** (center): imgui-node-editor widget with zoom, pan, selection.
  Nodes rendered with category-based color coding:
  - Red header: Event nodes
  - Blue header: Action nodes
  - Green header: Pure/data nodes
  - Gray header: Flow control
  - Orange header: Latent nodes
- **Properties panel** (right sidebar): shows selected node's configurable
  properties (literal values, dropdown selections, variable scope chooser).
- **Variable panel** (bottom): blackboard editor for graph-scope variables.
  Add/remove/rename variables with type selection.
- **Toolbar**: Play/Stop/Pause buttons for testing, debug toggle, graph selection.

### 8.3 Drag-from-Pin Context Search

When the user drags from an unconnected pin and releases on empty canvas, a
filtered search popup appears showing only compatible node types:
- From execution output -> nodes with execution input
- From data output (float) -> nodes with float input (or Any)
- From data input (vec3) -> nodes with vec3 output

This is the highest-impact UX feature according to cross-engine research.

### 8.4 Debug Visualization

When debug mode is enabled:
- **Active execution**: nodes flash briefly when their `execute()` is called
- **Breakpoints**: click the node header to toggle a red breakpoint indicator.
  When hit, execution pauses and the editor highlights the paused node.
- **Pin values**: hover over any data pin to see its current value in a tooltip
- **Execution trace**: a sidebar log of recently executed nodes with timestamps
- **Flow animation**: imgui-node-editor's built-in `Flow()` animates directional
  indicators on execution connections when they fire

---

## 9. Serialization

### 9.1 Graph Asset Format (JSON)

```json
{
    "version": 1,
    "name": "DoorScript",
    "variables": [
        {"name": "isLocked", "type": "bool", "default": false, "scope": "entity"},
        {"name": "openAngle", "type": "float", "default": 90.0, "scope": "entity"}
    ],
    "nodes": [
        {
            "id": 1,
            "type": "OnTriggerEnter",
            "posX": 100, "posY": 200,
            "properties": {}
        },
        {
            "id": 2,
            "type": "Branch",
            "posX": 350, "posY": 200,
            "properties": {}
        },
        {
            "id": 3,
            "type": "PlaySound",
            "posX": 600, "posY": 100,
            "properties": {"Clip Path": "audio/door_open.wav"}
        }
    ],
    "connections": [
        {"id": 1, "srcNode": 1, "srcPin": "Entered", "tgtNode": 2, "tgtPin": "Exec"},
        {"id": 2, "srcNode": 1, "srcPin": "otherEntity", "tgtNode": 2, "tgtPin": "Condition"},
        {"id": 3, "srcNode": 2, "srcPin": "True", "tgtNode": 3, "tgtPin": "Exec"}
    ]
}
```

Connections reference pins by **name** (not ID) for human readability and resilience
to node type changes. Asset files use the `.vscript` extension and are stored in
`assets/scripts/`.

### 9.2 Version Migration

The `version` field enables forward migration. When loading a graph with an older
version, the loader applies migration functions sequentially (v1->v2, v2->v3, etc.)
before instantiating nodes.

---

## 10. Gameplay Templates

Pre-built graph templates for common gameplay patterns. These are full `.vscript`
files that designers can drop onto entities and customize via entity-scope variables.

### 10.1 Interactive Door
- **Variables:** isLocked (bool), openAngle (float), openDuration (float)
- **Logic:** OnTriggerEnter -> Branch(isLocked) -> True: PlaySound("locked") /
  False: Timeline(openDuration) -> SetRotation(openAngle)

### 10.2 Collectible Item
- **Variables:** scoreValue (int), pickupSound (string)
- **Logic:** OnTriggerEnter -> IncrementVariable("score", Scene) ->
  SpawnParticles("pickup_sparkle") -> PlaySound(pickupSound) -> DestroyEntity(self)

### 10.3 Checkpoint
- **Variables:** activateSound (string), activateParticles (string)
- **Logic:** OnTriggerEnter -> DoOnce -> SetVariable("lastCheckpoint", self.position,
  Saved) -> SpawnParticles(activateParticles) -> PlaySound(activateSound)

### 10.4 Damage Zone
- **Variables:** damagePerSecond (float), damageSound (string)
- **Logic:** OnTriggerEnter -> Gate(Open) -> ForLoop(each frame while inside) ->
  DecrementVariable("health", Entity, damagePerSecond * dt) -> PlaySound

### 10.5 Timed Puzzle Button
- **Variables:** requiredPresses (int), timeLimit (float), successEvent (string)
- **Logic:** OnTriggerEnter -> Increment pressCount -> Branch(>= required) ->
  True: PublishEvent(successEvent) / False: Delay(timeLimit) -> Reset pressCount

---

## 11. Implementation Plan

### Phase 9E-1: Core Infrastructure (estimated: large)

1. **ScriptValue variant type** and Blackboard
2. **NodeTypeDescriptor** and NodeTypeRegistry
3. **ScriptGraph** data model (nodes, connections, serialization)
4. **ScriptContext** interpreter (execute, triggerOutput, readInput)
5. **ScriptInstance** (runtime state for one graph on one entity)
6. **ScriptComponent** (entity attachment)
7. **ScriptingSystem** (ISystem implementation)
8. Register **10 core node types**: OnStart, OnUpdate, OnDestroy, Branch, Sequence,
   Delay, SetVariable, GetVariable, PrintToScreen, LogMessage
9. **Unit tests** for interpreter, serialization, variable scoping, cycle detection,
   call depth limit

### Phase 9E-2: EventBus Bridge + Node Library (estimated: large)

1. **Event node bridge** -- subscribe/unsubscribe to all existing Event types
2. Register **remaining event nodes**: OnKeyPressed, OnKeyReleased, OnMouseButton,
   OnTriggerEnter/Exit, OnCollisionEnter/Exit, OnSceneLoaded, OnWeatherChanged,
   OnCustomEvent, OnVariableChanged, OnAudioFinished
3. Register **action nodes**: PlaySound, SpawnEntity, DestroyEntity, SetPosition,
   SetRotation, SetScale, ApplyForce, PlayAnimation, SpawnParticles, SetMaterial,
   SetVisibility, SetLightColor, SetLightIntensity, PublishEvent
4. Register **pure nodes**: GetPosition, GetRotation, GetDistance, Raycast,
   FindEntityByTag, Math ops, Vector ops, Boolean ops, Comparisons, ToString
5. Register **flow control**: Switch, ForLoop, ForEach, WhileLoop, Gate, DoOnce,
   FlipFlop
6. Register **latent nodes**: WaitForEvent, WaitForCondition, Timeline, MoveTo
7. **Unit tests** for each node category

### Phase 9E-3: Editor UI (estimated: large)

1. Integrate **imgui-node-editor** library into the build
2. **ScriptEditorPanel** -- basic canvas with node creation/deletion/connection
3. **Node palette** with search and categorization
4. **Properties panel** for selected node configuration
5. **Variable panel** (blackboard editor)
6. **Drag-from-pin** context search
7. **Graph asset loading/saving** from editor
8. **Inspector integration** -- entity-scope variables editable in Inspector

### Phase 9E-4: Debug + Templates (estimated: medium)

1. **Breakpoint support** (pause execution, highlight node)
2. **Pin value tooltips** (inspect current values)
3. **Execution trace** sidebar
4. **Flow animation** for active connections
5. **5 gameplay templates** (Door, Collectible, Checkpoint, DamageZone, Puzzle)
6. **Template panel** in editor for one-click attachment

---

## 12. Performance Considerations

- **OnUpdate nodes** are the most expensive because they fire every frame. The
  ScriptingSystem should batch-process these efficiently and warn in the editor
  when too many are active.
- **Pure node re-evaluation**: Pure nodes re-evaluate every time their output is
  consumed. Inside loops, this is multiplicative. Document this behavior and
  consider adding a "Cache" utility node that evaluates once per frame.
- **Latent action overhead**: Pending latent actions are checked each frame. Use a
  priority queue sorted by fire time rather than iterating all pending actions.
- **Frame budget**: ScriptingSystem respects the ISystem frame budget. If scripts
  exceed budget, log a warning. Consider auto-disabling OnUpdate on the most
  expensive scripts.
- **Memory**: Pre-allocate a pool for ScriptContext execution stacks to avoid
  per-execution heap allocation.

---

## 13. Anti-Patterns to Avoid (lessons from industry)

1. **Spaghetti graphs**: Provide comment boxes, reroute nodes, and a 50-node
   guideline per graph. Subgraph/collapsed groups in a future phase.
2. **Event Tick abuse**: Warn when OnUpdate is connected. Encourage event-driven
   patterns. Show performance impact in the profiler.
3. **Pure function traps in loops**: Document the re-evaluation behavior. Consider
   adding an auto-cache for pure nodes inside loops.
4. **Too-low-level nodes**: Godot failed because users had to wire dozens of nodes
   for simple tasks. Provide high-level templates and domain-specific nodes.
5. **Binary graph storage**: Use JSON for diffability and version control.
6. **Missing documentation**: Every node has a tooltip. Templates include
   step-by-step descriptions.

---

## 14. Future Extensions (not in scope for 9E)

- **Subgraphs / Macros**: Collapse a group of nodes into a reusable subgraph node
- **State Machine graphs**: Separate graph type for FSM logic (states + transitions)
- **Bytecode compilation**: Optional compilation for shipping builds
- **Formula node editor integration**: Share imgui-node-editor UI with the existing
  formula NodeGraph system
- **Custom event definitions**: User-defined event types with custom fields
- **Array/Collection support**: Array data type with iteration nodes
- **Undo/Redo for graph edits**: Command pattern integration with existing
  CommandHistory

---

## 15. Research Sources

### Engine Implementations
- [Unreal Blueprints VM Architecture](https://ikrima.dev/ue4guide/engine-programming/blueprints/bp-virtualmachine-overview/)
- [Blueprint Bytecode Internals](https://intaxwashere.github.io/blueprint-part-two/)
- [Blueprint Performance Guidelines](https://intaxwashere.github.io/blueprint-performance/)
- [Blueprint Nativization (deprecated)](https://docs.unrealengine.com/en-US/ProgrammingAndScripting/Blueprints/TechnicalGuide/NativizingBlueprints/)
- [Godot VisualScript Discontinuation](https://godotengine.org/article/godot-4-will-discontinue-visual-scripting/)
- [Godot VisualScript Proposals](https://github.com/godotengine/godot-proposals/issues/8873)
- [ezEngine Visual Scripting](https://ezengine.net/pages/docs/custom-code/visual-script/visual-script-overview.html)
- [Flax Engine Visual Scripting](https://docs.flaxengine.com/manual/scripting/visual/index.html)
- [FlowGraph (UE Plugin)](https://github.com/MothCocoon/FlowGraph)
- [Unity Visual Scripting Variables](https://docs.unity3d.com/Packages/com.unity.visualscripting@1.8/manual/vs-variables.html)

### Execution Models
- [Game Programming Patterns - Bytecode](https://gameprogrammingpatterns.com/bytecode.html)
- [Crafting Interpreters - Virtual Machine](https://craftinginterpreters.com/a-virtual-machine.html)
- [Latent Actions and Continuations in UE4](https://unktomi.github.io/Latent-Actions-Cont/Cont.html)

### Node Editor Libraries
- [imgui-node-editor](https://github.com/thedmd/imgui-node-editor) (recommended)
- [imnodes](https://github.com/Nelarius/imnodes)
- [ImNodeFlow](https://github.com/Fattorino/ImNodeFlow)
- [imnodes Design Blog](https://nelari.us/post/imnodes/)

### Design Patterns
- [Designing Node-Based Visual Languages](https://dev.to/cosmomyzrailgorynych/designing-your-own-node-based-visual-programming-language-2mpg)
- [Visual Node Graph with ImGui](https://gboisse.github.io/posts/node-graph/)
- [Pure & Impure Blueprint Functions](https://medium.com/unreal-engine-technical-blog/pure-impure-functions-516367cff14f)
- [Level Design Scripting Patterns](https://book.leveldesignbook.com/process/scripting)
- [Blueprint Best Practices](https://outscal.com/blog/how-to-create-clean-and-reusable-blueprint-scripts-in-unreal-engine-5)
