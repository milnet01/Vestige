# Subsystem Specification — `engine/scripting`

## Header

| Field | Value |
|-------|-------|
| Subsystem | `engine/scripting` |
| Status | `shipped` |
| Spec version | `1.0` |
| Last reviewed | `2026-04-28` (initial draft — pending cold-eyes review) |
| Owners | `milnet01` |
| Engine version range | `v0.9.0+` (foundation since Phase 9E-1, 2026-04) |

---

## 1. Purpose

`engine/scripting` is the visual scripting runtime — a node-graph interpreter that lets designers wire gameplay logic ("player enters trigger → open door + play sound") without writing C++. It exists as its own subsystem because the alternative (per-domain script hooks bolted onto renderer / physics / audio) would couple every domain to gameplay-event semantics. By centralising the graph asset format, the node-type registry, the runtime interpreter, and the EventBus bridge here, every domain stays scriptable through the same vocabulary while staying ignorant of scripting itself. For the engine's primary use case — first-person walkthroughs of biblical structures (Tabernacle, Solomon's Temple) — this is what lets a designer attach interactive behaviour to a ramp, a curtain, an oil lamp, or a checkpoint without recompiling. Phase 9E-1/9E-2 shipped the runtime + 60 built-in nodes; Phase 9E-3 the editor; Phase 10.9 closed the auditable hot-path issues (pin-name interning, type index, pure-node memoisation, generation token, output fan-out).

## 2. Scope

| In scope | Out of scope |
|----------|--------------|
| `ScriptingSystem` — the `ISystem` (lifecycle hook) that owns built-in node registration, blackboards, active-instance tracking, latent-action ticking, and EventBus subscription. | Concrete domain side-effects (audio playback, animation, physics impulses, lighting changes) — `engine/audio/`, `engine/animation/`, `engine/physics/`, `engine/renderer/` |
| `ScriptGraph` — serialisable `.vscript` JavaScript Object Notation (JSON) asset (nodes, connections, variables, version, deserialisation caps). | `.vscript` editor canvas drawing, palette UI, undo / redo — `engine/editor/panels/script_editor_panel.{h,cpp}` |
| `ScriptInstance` — runtime state for one graph attached to one `Entity`: per-node instance map, latent action queue, EventBus subscription IDs, generation token, pre-built type / connection caches. | The entity lifecycle itself — `engine/scene/` |
| `ScriptContext` — per-impulse interpreter (data pull, output set, exec trigger, latent schedule, memoisation cache, call-depth guard). | Bytecode generation, JIT, ahead-of-time native compilation (deferred to a future phase per Phase 9E §2.3 research) |
| `ScriptValue` + `ScriptDataType` — type-erased value over `std::variant<bool, int32_t, float, std::string, glm::vec2/3/4, glm::quat, uint32_t-as-EntityId>`. | Reflected C++ types beyond the eleven enum values listed (no `Transform`, no `Mesh*`, no audio handles) |
| `ScriptGraphCompiler` + `CompiledScriptGraph` — load-time validator that turns the on-disk graph into a flat, index-based intermediate representation (IR) with diagnostics. | A second runtime back-end that consumes the IR — current interpreter walks `ScriptInstance` directly; the IR is staging for future codegen. |
| `Blackboard` + `VariableScope` — six-level (Flow / Graph / Entity / Scene / Application / Saved) key → `ScriptValue` store with capped insertion. | Persistence of `Saved`-scope variables to disk — wired through `engine/core/settings.h` (Open Q1). |
| `NodeTypeRegistry` + `NodeTypeDescriptor` — node-type catalogue with execute lambdas, pin definitions, category, memoisation flag. | Node definitions in user / mod packages — registry is engine-only at v0.9.x. |
| `PinId` intern table — process-local string → `uint32_t` for hot-path pin lookup. | Stable cross-process pin identifiers — on-disk schema keeps strings on purpose (see `pin_id.h` head comment). |
| Built-in node packs: `core_nodes` (10 lifecycle / flow / debug), `event_nodes` (input / scene / weather / custom), `flow_nodes` (Switch, ForLoop, WhileLoop, Gate, DoOnce, FlipFlop), `action_nodes` (impure side-effect nodes), `latent_nodes` (Wait, Timeline, MoveTo), `pure_nodes` (math / vector / boolean / queries). | Domain-specific nodes that ship with future packs (e.g. weather scripting pack, terrain authoring pack). |
| `ScriptComponent` — entity component holding `(graphAssetPath, ScriptInstance, entityBlackboard)` triples. | The `Entity` / component framework itself — `engine/scene/`. |
| `script_templates` — five pre-built gameplay graphs (DoorOpens, Collectible, DamageZone, Checkpoint, DialogueTrigger). | The trigger / collision events those templates consume (engine-side stubs at v0.9.x, full wiring tracked in Open Q3). |
| `ScriptCustomEvent` — user-defined event struct routed through the engine `EventBus`. | The `EventBus` itself — `engine/core/event_bus.h`. |

## 3. Architecture

```
                              ┌──────────────────────────────────┐
                              │  ScriptingSystem (ISystem)       │
                              │  scripting_system.h:28           │
                              └─────────────┬────────────────────┘
                                            │ owns
                ┌───────────────────────────┼────────────────────────────┐
                ▼                           ▼                            ▼
       ┌────────────────┐        ┌────────────────────┐         ┌──────────────────┐
       │NodeTypeRegistry│        │ Blackboard (scene) │         │ activeInstances  │
       │(60 built-ins)  │        │ Blackboard (app)   │         │ vector<Instance*>│
       └────────────────┘        └────────────────────┘         └────────┬─────────┘
                                                                         │ register
                                                                         ▼
                                                                ┌───────────────────┐
                                                                │ ScriptInstance    │
                                                                │ script_instance.h │
                                                                │  ┌─────────────┐  │
                                                                │  │type idx     │  │
                                                                │  │outputByNode │  │
                                                                │  │inputByNode  │  │
                                                                │  │subscriptions│  │
                                                                │  │pendingLatent│  │
                                                                │  │generation   │  │
                                                                │  └──────┬──────┘  │
                                                                └─────────┼─────────┘
                                                                          │ refs
                                                                          ▼
                                                                  ┌──────────────┐
                                                                  │ ScriptGraph  │
                                                                  │ (.vscript)   │
                                                                  └──────────────┘

  Per-impulse:  EventBus dispatch  ──► subscribe lambda (captures InstancePtr+gen)
                                       │  isInstanceActive(ptr, gen) ?
                                       ▼
                                ScriptContext(instance, registry, engine)
                                       │  triggerOutput / readInput / setOutput
                                       ▼
                                NodeTypeDescriptor::execute(ctx, nodeInst)
                                       │  side effects via engine subsystems
                                       ▼
                                EventBus::publish → … (next dispatch)
```

Key abstractions:

| Abstraction | Type | Purpose |
|-------------|------|---------|
| `ScriptingSystem` | `ISystem` (class) | Lifecycle owner; registers nodes, ticks updates + latents, manages active instances. `engine/scripting/scripting_system.h:28` |
| `ScriptGraph` | struct (POD-ish) | Serialisable on-disk graph: nodes, connections, variables, version. `engine/scripting/script_graph.h:23` |
| `ScriptInstance` | class | Runtime state for one graph + one entity; owns hot-path caches. `engine/scripting/script_instance.h:78` |
| `ScriptContext` | class | Per-impulse interpreter — data pull, exec trigger, latent schedule, pure-node memo cache. `engine/scripting/script_context.h:30` |
| `ScriptValue` | class (variant-backed) | Type-erased value flowing across data pins. `engine/scripting/script_value.h:39` |
| `ScriptDataType` | enum | 11 canonical types incl. `ANY` wildcard. `engine/scripting/script_value.h:20` |
| `NodeTypeRegistry` | class | Type-name → `NodeTypeDescriptor` map. `engine/scripting/node_type_registry.h:81` |
| `NodeTypeDescriptor` | struct | Execute lambda + pin defs + category + memoisability + event-type tag. `engine/scripting/node_type_registry.h:25` |
| `PinId` / `internPin()` | typedef + free fn | Process-local interned pin name. `engine/scripting/pin_id.h:39` |
| `ScriptGraphCompiler` | class (stateless) | Validate + emit `CompiledScriptGraph` IR. `engine/scripting/script_compiler.h:154` |
| `CompiledScriptGraph` | struct | Flat, index-based IR; entry-point + update indices precomputed. `engine/scripting/script_compiler.h:116` |
| `CompileDiagnostic` / `CompileSeverity` | struct + enum | INFO / WARNING / ERROR with node + connection scope. `engine/scripting/script_compiler.h:38` |
| `Blackboard` | class | String → `ScriptValue` map, capped at `MAX_KEYS = 1024`. `engine/scripting/blackboard.h:48` |
| `VariableScope` | enum | Flow / Graph / Entity / Scene / Application / Saved (Unity Bolt model). `engine/scripting/blackboard.h:19` |
| `ScriptComponent` | class (entity component) | Holds graph-asset paths + instances + entity-scope blackboard. `engine/scripting/script_component.h:22` |
| `PendingLatentAction` | struct | One time- or condition-based suspended branch. `engine/scripting/script_instance.h:54` |
| `ScriptCustomEvent` | `Event`-derived struct | User-defined event payload bridged through `EventBus`. `engine/scripting/script_events.h:25` |
| `buildGameplayTemplate()` | free fn | Five pre-built gameplay graphs (door / collectible / damage / checkpoint / dialogue). `engine/scripting/script_templates.h:41` |

## 4. Public API

The subsystem exposes a deliberately small facade. Per the project convention (CODING_STANDARDS §18) downstream code only `#include`s the headers below; everything else under `engine/scripting/` is implementation detail.

```cpp
/// scripting_system.h — system owner, registered with SystemRegistry.
class ScriptingSystem : public ISystem
{
    bool initialize(Engine&) override;       // NOT idempotent — pair every init with shutdown (audit L1)
    void shutdown() override;
    void update(float dt) override;
    void onSceneLoad(Scene&) override;
    void onSceneUnload(Scene&) override;

    void registerInstance(ScriptInstance&);
    void unregisterInstance(ScriptInstance&);
    bool isInstanceActive(const ScriptInstance*) const;
    bool isInstanceActive(const ScriptInstance*, uint32_t expectedGeneration) const;  // ABA guard, audit H9
    void fireEvent(ScriptInstance&, uint32_t nodeId);

    NodeTypeRegistry& nodeRegistry();
    Blackboard& sceneBlackboard();
    Blackboard& appBlackboard();
};
```

```cpp
/// script_graph.h — on-disk asset.
struct ScriptGraph
{
    static constexpr size_t MAX_NODES        = 10'000;
    static constexpr size_t MAX_CONNECTIONS  = 100'000;
    static constexpr size_t MAX_VARIABLES    = 1'024;
    static constexpr size_t MAX_STRING_BYTES = 256;
    // mutators: addNode / removeNode / addConnection / removeConnection
    bool validate(std::string& errorOut) const;
    nlohmann::json toJson() const;
    static ScriptGraph fromJson(const nlohmann::json&);
    static ScriptGraph loadFromFile(const std::string&);
    bool saveToFile(const std::string&) const;
};
```

```cpp
/// script_compiler.h — load-time validator + flat IR emit.
struct CompilationResult { bool success; std::vector<CompileDiagnostic> diagnostics; CompiledScriptGraph compiled; };
class ScriptGraphCompiler
{
    static CompilationResult compile(const ScriptGraph&, const NodeTypeRegistry&);
    static bool areTypesCompatible(ScriptDataType source, ScriptDataType target);
};
```

```cpp
/// script_instance.h — runtime state per (graph × entity).
class ScriptInstance
{
    void initialize(const ScriptGraph& graph, uint32_t entityId);   // graph lifetime contract: caller-owned
    uint32_t generation() const;                                    // bumped on every initialize()
    const std::vector<uint32_t>& updateNodes() const;
    const std::vector<uint32_t>& nodesByType(const std::string&) const;
    const ScriptConnection* findOutputConnection(uint32_t nodeId, PinId) const;
    template <typename F> void forEachOutputConnection(uint32_t, PinId, F&&) const;  // exec fan-out
    static constexpr int MAX_EVENT_REENTRY_DEPTH = 4;
};
```

```cpp
/// script_context.h — per-impulse interpreter.
class ScriptContext
{
    ScriptContext(ScriptInstance&, const NodeTypeRegistry&, Engine* /*nullable*/);
    ScriptValue readInput(const ScriptNodeInstance&, PinId);
    template <typename T> T readInputAs(const ScriptNodeInstance&, PinId);
    void setOutput(const ScriptNodeInstance&, PinId, const ScriptValue&);
    void triggerOutput(const ScriptNodeInstance&, PinId);
    void executeNode(uint32_t nodeId);
    void scheduleDelay(const ScriptNodeInstance&, const std::string& outPin, float seconds);
    void scheduleWaitForCondition(const ScriptNodeInstance&, const std::string&, std::function<bool()>);
    static constexpr int MAX_CALL_DEPTH = 256;
    static constexpr int MAX_NODES_PER_CHAIN = 1000;
};
```

```cpp
/// script_value.h — type-erased data-pin value. 11 ScriptDataType values.
class ScriptValue { /* explicit ctors only — no implicit conversions, audit M12 */ };
```

```cpp
/// blackboard.h — variable store, six scopes.
class Blackboard { static constexpr size_t MAX_KEYS = 1024; /* set/get/has/remove/clear/toJson/fromJson */ };
enum class VariableScope { FLOW, GRAPH, ENTITY, SCENE, APPLICATION, SAVED };
```

```cpp
/// node_type_registry.h — node-type catalogue.
class NodeTypeRegistry
{
    void registerNode(NodeTypeDescriptor);
    const NodeTypeDescriptor* findNode(const std::string& typeName) const;
    std::vector<const NodeTypeDescriptor*> getByCategory(const std::string&) const;
};
```

```cpp
/// script_templates.h — designer starting points.
ScriptGraph buildGameplayTemplate(GameplayTemplate);
const char* gameplayTemplateDisplayName(GameplayTemplate);
```

**Non-obvious contract details:**

- `ScriptInstance::initialize` takes `const ScriptGraph&` by reference but stores a raw pointer (`m_graph`). The graph **must outlive every instance derived from it**. Audit H2 flagged migrating to `shared_ptr<const ScriptGraph>` if this becomes a recurring footgun — currently deferred.
- `ScriptInstance::generation()` increments on every `initialize()` call. Latent / event callbacks that capture an instance pointer must also capture `generation()` and pair the pointer-liveness check with `ScriptingSystem::isInstanceActive(ptr, gen)` (audit H9). Without the generation token, an editor "stop → restart" cycle re-using the same `ScriptInstance` slot would dispatch stale captures into a rebuilt graph.
- `ScriptingSystem::initialize` is **not idempotent** — calling it twice without an intervening `shutdown()` registers built-in node types twice. Tests that need to re-initialise call `shutdown()` first; engine code is paired by construction (audit L1).
- `ScriptContext`'s `engine` parameter is **nullable** — unit tests that exercise the interpreter without a full engine pass `nullptr`. Nodes that need engine access must check before deref (`ctx.engine() != nullptr`). The previous reference-typed signature forced tests into a `reinterpret_cast<Engine*>(&nullptr)` Undefined Behaviour (UB) hack — audit H5 fixed it.
- Pure-node outputs are memoised inside one `ScriptContext` lifetime (audit M11) — same `(nodeId, pinId)` only runs once per chain. **Opt-out** via `NodeTypeDescriptor::memoizable = false` for nodes that read mutable state mid-chain (`GetVariable`, `FindEntityByName`); leaving the default `true` for those caused `WhileLoop.Condition` to freeze (audit H7/H8).
- `ScriptContext::triggerOutput` follows **all** matching connections (Phase 10.9 Sc1 — `forEachOutputConnection` semantics) — execution-pin fan-out is a deliberate template pattern (`DoOnce.Then → PlayAnimation + PlaySound`). Compiler pass 6 forbids duplicate **input** connections, not output fan-out.
- `EventBus` re-entry into the same instance is capped at `ScriptInstance::MAX_EVENT_REENTRY_DEPTH = 4`; per-chain depth is capped at `ScriptContext::MAX_CALL_DEPTH = 256` and total nodes at `MAX_NODES_PER_CHAIN = 1000`. Latent / new-dispatch chains start a fresh `ScriptContext` so the per-context counters reset.
- `ScriptValue` single-arg constructors are **explicit** (audit M12) — no implicit float-to-pin coercion at call sites; the variant only enters via deliberate `ScriptValue(x)` calls.
- `ScriptGraph` deserialisation enforces hard caps (`MAX_NODES = 10'000`, etc.) so a crafted `.vscript` can't exhaust memory (Phase 9E audit C1/H6/H7).
- `Blackboard` insertions are **capped at 1024 keys per scope** — once full, new keys are rejected; updates to existing keys still succeed.
- `ScriptCustomEvent` carries one `ScriptValue` payload — multi-payload events compose by passing a struct via the variant's `std::string` slot encoded as JSON, or by extending `ScriptCustomEvent` (the Phase 9E design accepted the single-payload limitation).

**Stability:** the facade above is semver-frozen for `v0.9.x`. Two known evolution points: (a) `Saved`-scope blackboard persistence is not yet wired through `engine/core/settings.h` (Open Q1); (b) `OnTriggerEnter` / `OnCollisionEnter` are registered as palette stubs pending `engine/physics/` event-bus emission (Open Q3). Both are additive when they land.

## 5. Data Flow

**Steady-state per-frame (`ScriptingSystem::update` — `engine/scripting/scripting_system.cpp`):**

1. `tickLatentActions(dt)` — walks `m_activeInstances`, decrements `PendingLatentAction::remainingTime` / evaluates `condition`, fires `outputPin` on completion via a fresh `ScriptContext`.
2. `tickUpdateNodes(dt)` — per active instance, walks the cached `updateNodes()` list (audit M9) and calls each node's execute via a fresh `ScriptContext`. The cache is rebuilt only inside `ScriptInstance::initialize`; per-frame cost is O(active instances × OnUpdate nodes), no string scan.

**Event dispatch (asynchronous, EventBus-driven):**

1. Domain subsystem publishes a typed event (e.g. `KeyPressedEvent` from `engine/core/input_manager.cpp`).
2. `EventBus` invokes the lambda that `subscribeOneEventNode` registered at `onSceneLoad`. The lambda captures `(ScriptingSystem*, ScriptInstance*, generation, nodeId)` by value.
3. Lambda calls `sys->isInstanceActive(inst, gen)`; mismatch → no-op (ABA guard, audit H9).
4. Lambda populates the event node's output data pins with event fields (`KeyPressedEvent::glfwKey` → `Key` pin, etc.), increments `eventDispatchDepth`, checks `< MAX_EVENT_REENTRY_DEPTH`.
5. Lambda creates a `ScriptContext(instance, registry, engine)` and calls `ctx.triggerOutput(node, "Then")`. Execution flows through connected nodes, building the per-context pure-node memo cache as it goes.
6. `decrementEventDispatchDepth`. Context destructed; per-chain caches die.

**Cold start / scene load:**

1. `ScriptingSystem::initialize` registers built-in node types (`registerCoreNodeTypes`, `registerEventNodeTypes`, etc.) into `m_nodeRegistry`. Caches `Engine*`. Resets `m_sceneBlackboard` / `m_appBlackboard`.
2. `ScriptingSystem::onSceneLoad(scene)` walks scene entities. For each `ScriptComponent`, loads each `graphAssetPath` via `ScriptGraph::loadFromFile`, runs `ScriptGraphCompiler::compile`, refuses to register on `success == false` (logging diagnostics), and otherwise calls `instance.initialize(graph, entityId)` then `registerInstance(instance)`.
3. `registerInstance` calls `subscribeEventNodes(instance)` — for every node whose descriptor has a non-empty `eventTypeName`, dispatch on the string and call `subscribeOneEventNode<EventT>(...)`. Subscription IDs are appended to `instance.subscriptions()` for later cleanup. Adds the instance to `m_activeInstances`.

**Scene unload:**

1. `ScriptingSystem::onSceneUnload` calls `unregisterInstance` for every entry in `m_activeInstances`, which cancels EventBus subscriptions, drops `pendingActions`, and removes the entry. Scene-scope blackboard cleared.

**Shutdown:**

1. `ScriptingSystem::shutdown` calls `unregisterInstance` for all remaining instances, clears `m_activeInstances`, clears the registry, resets blackboards. Engine pointer nulled. Safe to `initialize()` again.

**Exception path:** none; the compiler returns a `CompilationResult` with diagnostics rather than throwing, the runtime guards every cap with a `Logger::warning` + early return, and EventBus subscribers must not throw (per `engine/core` policy — propagates to publisher otherwise).

## 6. CPU / GPU placement

Not applicable — pure CPU subsystem. The interpreter, compiler, blackboard, and registry are all branching / sparse / decision work (CODING_STANDARDS §17 default for Central Processing Unit (CPU) placement). No per-pixel / per-vertex / per-particle workload exists in scripting; nodes that ultimately drive Graphics Processing Unit (GPU) work do so by calling **into** the renderer / animation / particles subsystems, where the GPU placement is owned by those subsystems' specs. No dual implementation, no parity test.

## 7. Threading model

Per CODING_STANDARDS §13 — main-thread-only by contract.

| Caller thread | Allowed APIs | Locks held |
|---------------|--------------|------------|
| **Main thread** (the only thread that runs `Engine::run`) | All of `ScriptingSystem`, `ScriptGraph`, `ScriptInstance`, `ScriptContext`, `Blackboard`, `NodeTypeRegistry`, `internPin`, `ScriptGraphCompiler`, `ScriptValue`. | None. |
| Worker threads | None. Calling any scripting API from a worker is undefined. | — |

The `pin_id.h` head comment makes the contract explicit: the intern table is single-threaded by design — scripting, editor, node registration all run on the main thread. If a future phase moves any of these off-thread, the intern table needs a mutex (Open Q4).

`EventBus::publish` is synchronous on the publishing thread (per `engine/core` spec §10), so script event dispatch inherits the publisher's thread — which is always the main thread for in-engine events. The `subscribeOneEventNode` lambdas therefore run on the main thread by construction.

## 8. Performance budget

60 frames-per-second hard requirement (CLAUDE.md) → 16.6 millisecond (ms) per-frame budget. Phase 9E §1 set the design target at **< 0.5 ms per frame for 50 active scripts**.

| Path | Budget | Measured (RX 6600, 1080p) |
|------|--------|----------------------------|
| `ScriptingSystem::update` (50 active instances, mixed OnUpdate + 5 latents) | < 0.5 ms | TBD — measure by Phase 11 audit |
| `tickUpdateNodes` per OnUpdate node (cached lookup, audit M9) | < 0.005 ms | TBD — measure by Phase 11 audit |
| `tickLatentActions` per pending action | < 0.002 ms | TBD — measure by Phase 11 audit |
| `subscribeOneEventNode` invocation (event lambda, no node body) | < 0.01 ms | TBD — measure by Phase 11 audit |
| Pure-node read (memoised second hit, audit M11) | < 0.0005 ms | TBD — measure by Phase 11 audit |
| Pure-node read (first hit, ten-input math chain) | < 0.05 ms | TBD — measure by Phase 11 audit |
| `ScriptGraph::loadFromFile` + `ScriptGraphCompiler::compile` (50-node graph) | < 5 ms | TBD — measure by Phase 11 audit |
| `ScriptInstance::initialize` (cache rebuild, 50 nodes) | < 1 ms | TBD — measure by Phase 11 audit |

Profiler markers / capture points: `ScriptingSystem` reports per-frame timing via `ISystem` metrics (over-budget warnings logged with system name). `engine/scripting` does not emit `glPushDebugGroup` markers — it has no GPU passes.

Open Q5 (§15) tracks the one-shot capture to populate the `Measured` column.

## 9. Memory

| Aspect | Value |
|--------|-------|
| Allocation pattern | Heap, mostly through standard-library containers (`std::vector`, `std::unordered_map`, `std::string`). No arena, no per-frame transient allocator. Per-impulse `ScriptContext` lives on the stack of the dispatch lambda — its `m_pureCache` is a small `unordered_map<uint64_t, ScriptValue>` heap-backed but stack-rooted, so it dies with the chain. |
| Peak working set | Order kilobytes (KB) per active instance: hot-path caches (`m_typeIndex`, `m_outputByNode`, `m_inputByNode`, `m_nodeInstances`, runtimeState) typically 4–20 KB for a 50-node graph; `Blackboard` capped at 1024 keys × ScriptValue. Whole-system: low single-digit megabytes (MB) for ~50 active instances + the 60-entry node registry. Negligible vs renderer / scene. |
| Bounded by | `ScriptGraph::MAX_NODES = 10'000`, `MAX_CONNECTIONS = 100'000`, `MAX_VARIABLES = 1024`, `MAX_STRING_BYTES = 256` per string, `Blackboard::MAX_KEYS = 1024` per scope. These are deserialisation defences against crafted input (Phase 9E audit C1/H6/H7). |
| Ownership | `ScriptingSystem` owns the registry + blackboards (scene + app). `ScriptComponent` owns its `ScriptInstance` vector + entity blackboard. `ScriptInstance` stores a **raw pointer** to the `ScriptGraph` — caller (typically the resource manager / scene) owns the graph, see audit H2. EventBus subscription IDs are owned by `ScriptInstance::m_subscriptions` and unsubscribed in `unregisterInstance`. |
| Lifetimes | Instance: scene-load duration. Registry: engine lifetime. Scene blackboard: scene-load duration. Application blackboard: engine lifetime. Saved blackboard: persistent (Open Q1). `ScriptContext`: one event / latent / OnUpdate impulse. Pure-node memo cache: one `ScriptContext`. |

No `new`/`delete` in feature code (CODING_STANDARDS §12). Reusable per-frame buffers are not currently maintained on the system itself — `tickUpdateNodes` walks the per-instance `updateNodes()` cache (a `const std::vector<uint32_t>&`) so no per-frame allocation occurs in the hot path.

## 10. Error handling

Per CODING_STANDARDS §11 — no exceptions in steady-state. Scripting is a runtime that consumes user-authored data, so the error surface is unusually rich; the table below covers the design's principal failure modes.

| Failure mode | Reported as | Caller's recourse |
|--------------|-------------|-------------------|
| Malformed `.vscript` JSON (parse error, missing required field) | `ScriptGraph::loadFromFile` returns a default-constructed graph + `Logger::warning` line. Hard caps (`MAX_NODES` etc.) throw `nlohmann::json` exceptions internally and are caught at the parse boundary. | Scene load skips the broken script; entity continues without its scripts; designer fixes the asset. |
| Unknown node type at compile time | `CompilationResult.diagnostics` gets `CompileSeverity::ERROR` with `nodeId` populated; `success = false`. | `ScriptingSystem::onSceneLoad` refuses to register the instance and logs every diagnostic. Designer sees the error in the editor (or in the log) tied to the offending node id. |
| Missing node reference in connection (target node id not in graph) | `CompileDiagnostic` ERROR with `connectionId` populated. | Same as above — instance not registered. |
| Pin-kind mismatch (data wired to exec or vice versa) | `CompileDiagnostic` ERROR with `connectionId`. | Same. |
| Pin-type mismatch (incompatible `ScriptDataType` per `areTypesCompatible`) | `CompileDiagnostic` ERROR with `connectionId`. | Same. ANY wildcard, INT → FLOAT widening, ENTITY → INT are accepted; narrowing is rejected. |
| Multiple incoming connections to one input pin | `CompileDiagnostic` ERROR (compiler pass 6). | Same. |
| Pure-data cycle | `CompileDiagnostic` ERROR (compiler pass 7). Execution cycles are allowed (loops + re-triggers are legitimate); only **pure-data** edges may not cycle. | Same. |
| Graph with no entry points | `CompileDiagnostic::WARNING` (compiler pass 8). The graph compiles but never fires — interpreted as a library / helper graph. | Designer can ignore or wire an entry. |
| Unreachable impure node | `CompileDiagnostic::WARNING` (compiler pass 9). | Designer notice; runtime ignores it. |
| Runtime call-depth exceeded (`MAX_CALL_DEPTH = 256`) | `Logger::warning`, chain aborts (no throw). | Designer simplifies the graph. |
| Runtime nodes-per-chain exceeded (`MAX_NODES_PER_CHAIN = 1000`) | `Logger::warning`, chain aborts. | Same. |
| EventBus re-entry exceeded (`MAX_EVENT_REENTRY_DEPTH = 4`) | `Logger::warning`, dispatch dropped. | Designer breaks the publish-loop. |
| Blackboard cap exceeded (`MAX_KEYS = 1024`) | Insertion silently rejected, updates still succeed. | Designer audits variable usage. |
| Use-after-free of `ScriptInstance` from latent / event callback | Guarded by `ScriptingSystem::isInstanceActive(ptr, generation)`. Mismatch → callback no-op. | None — guard is invisible. (Audit H9.) |
| Subscriber callback throws inside `EventBus::publish` | Propagates to publisher — policy is "callbacks must not throw" (per `engine/core` spec §10). | Fix the callback. |
| Programmer error (null pointer, invalid pin id) | `assert` (debug) / UB (release). | Fix the caller. |
| Out of memory | `std::bad_alloc` propagates. | App aborts (CODING_STANDARDS §11 — OOM fatal). |

`Result<T, E>` / `std::expected` not yet used — `CompilationResult` predates the codebase-wide policy and uses a `bool success + vector<diagnostic>` shape that is functionally equivalent. Migration is on the broader debt list (engine-wide, not subsystem-specific).

## 11. Testing

| Concern | Test file | Coverage |
|---------|-----------|----------|
| Public API contract — interpreter, blackboard, latents, event dispatch, generation token, fan-out, depth caps | `tests/test_scripting.cpp` | Headless (no `Engine` instance — `ScriptContext(..., nullptr)`) |
| Compiler — every pass (1–9), pin-type compatibility matrix, diagnostic severity, success / failure boundaries | `tests/test_script_compiler.cpp` | Headless |
| Built-in templates — round-trip JSON, validate clean, compile clean | `tests/test_script_templates.cpp` | Headless |

Per the project rule (every feature + bug fix gets a test), no untested public functions in `engine/scripting`.

**Adding a test for `engine/scripting`:** drop a new `tests/test_<thing>.cpp` next to its peers; link against `vestige_engine` + `gtest_main` in `tests/CMakeLists.txt` (auto-discovered via `gtest_discover_tests`). Use `ScriptInstance` / `ScriptContext` / `Blackboard` / `ScriptGraph` directly without an `Engine` instance — every primitive in `engine/scripting` is unit-testable headlessly. The `Engine*` parameter on `ScriptContext` is nullable specifically for tests (audit H5). For end-to-end EventBus dispatch, instantiate a real `EventBus` + `ScriptingSystem` without GLFW; the visual editor canvas is the only path that needs the full runner.

**Coverage gap:** the editor-side panel (`engine/editor/panels/script_editor_panel.{h,cpp}`) is exercised through `engine/editor/` tests, not here — that's a separate spec.

## 12. Accessibility

`engine/scripting` itself produces no user-facing pixels or sound — it only routes designer logic to the subsystems that do. **However**, scripts can author flashing / strobing / motion-amplifying gameplay (e.g. `Timeline` driving a screen flash), which means script outputs must respect downstream accessibility settings:

- Script-driven post-process effects flow through `engine/renderer/` and the `RendererAccessibilityApplySink` (color-vision filter, depth-of-field / motion-blur / fog toggles) — scripts cannot bypass these.
- Photosensitive caps (`PhotosensitiveSafetyWire` in `engine/core/settings.h:210` — `maxFlashAlpha`, `maxStrobeHz`, `bloomIntensityScale`) are enforced **inside** the consumer subsystems (renderer / lighting / camera-shake), not inside scripting. A script that publishes a 10 hertz (Hz) flash event still has the flash clamped by the photosensitive sink.
- The editor's visual debugging affordances (highlight-current-node, value-tooltip on pin hover, breakpoint marker) live in `script_editor_panel.h` — accessibility constraints (contrast ratio, never colour-only encoding, screen-reader-friendly text) belong to `engine/editor/`'s spec.
- `Logger` ring-buffer entries from `Logger::warning` / `Logger::error` lines emitted by the compiler and runtime feed the editor's console panel; per the `engine/core` spec §12, that panel must back the log-level colour with a text label.

Constraint summary for downstream UIs that consume `engine/scripting`:

- The editor must surface every `CompileDiagnostic` with severity in **text** (`ERROR` / `WARNING` / `INFO`), not just colour.
- Script-driven UI text routed through `engine/ui/` must continue to honour the UI scale + high-contrast settings — scripts must not embed pixel-coordinate placement that overrides accessibility scaling.
- Default node-graph palette colours (`NodeTypeDescriptor::category` is the only colour-conveyed signal in the editor) must be surfaced alongside category text labels in the palette (the editor handles this; the registry exposes `getCategories()` for the UI to enumerate).

## 13. Dependencies

| Dependency | Type | Why |
|------------|------|-----|
| `engine/core/i_system.h` | engine subsystem | `ScriptingSystem` extends `ISystem`. |
| `engine/core/event.h`, `event_bus.h`, `system_events.h` | engine subsystem | Event dispatch — `KeyPressedEvent`, `SceneLoadedEvent`, `WeatherChangedEvent`, `ScriptCustomEvent`. |
| `engine/core/engine.h` (forward decl) | engine subsystem | Pointer-only access to engine for nodes that need it; never `#include`d in headers. |
| `engine/core/logger.h` | engine subsystem | All scripting diagnostics route through `Logger`. |
| `engine/scene/scene.h`, `entity.h` | engine subsystem | `onSceneLoad` walks entities; `ScriptContext::resolveEntity` looks up by ID. |
| `engine/editor/panels/script_editor_panel.h` | reverse — consumer of `engine/scripting`, not a dependency | Listed for completeness; scripting does not include any editor header. |
| `<glm/glm.hpp>`, `<glm/gtc/quaternion.hpp>` | external | Vector / quaternion types in `ScriptValue`. |
| `<nlohmann/json.hpp>` | external | `.vscript` serialisation, blackboard persistence, `ScriptValue` round-trip. |
| `<variant>`, `<unordered_map>`, `<map>`, `<vector>`, `<string>`, `<functional>`, `<cstdint>` | std | Variant, hash maps, lambdas, primitives. |

**Direction:** `engine/scripting` depends on `engine/core` (lifecycle, events, logger), `engine/scene` (entity lookup), and external glm + json. It is depended on by `engine/editor/panels/script_editor_panel.{h,cpp}` (palette / canvas) and the test suite. It must **not** depend on any concrete domain (`engine/audio/`, `engine/animation/`, `engine/physics/`, `engine/renderer/`) — action nodes call into those via the `Engine*` pointer dispatched at runtime, never via include.

## 14. References

External research (current within ≤ 1 year):

- The New School. *Unreal Engine 5 Blueprints: The Ultimate Visual Scripting Guide* (2026-03). Modular design, hybrid C++ / Blueprint workflow, breakpoint debugging. <https://thenewschoolexeter.co.uk/2026/03/ue5-blueprints-tutorial.html>
- Wholetomato. *C++ vs Blueprints in Unreal Engine — 2026 Guide.* When-to-use heuristics + performance profile of graph interpreters. <https://www.wholetomato.com/blog/c-versus-blueprints-which-should-i-use-for-unreal-engine-game-development/>
- Algoryte. *Top Unreal Engine Game Development Practices for 2026.* Naming, encapsulation, function / macro extraction patterns. <https://www.algoryte.com/news/top-unreal-engine-game-development-practices-for-2026/>
- Wayline. *The Godot Visual Scripting Trap: When to Embrace Code* (2025). Why low-level wiring fails — drives Vestige's "high-level gameplay templates" decision (`script_templates.h`). <https://www.wayline.io/blog/godot-visual-scripting-trap-when-to-embrace-code>
- Toxigon. *Mastering Godot Visual Scripting: A 2025 Guide.* Industry post-mortem on Godot 4.0 dropping VisualScript — informs the "ship templates, not just primitives" rule we follow. <https://toxigon.com/godot-visual-scripting-2025-guide>
- Wikipedia / Grokipedia. *Node graph architecture* (2025 revision). Atomic-units-with-links taxonomy, virtual-machine vs codegen approaches. <https://en.wikipedia.org/wiki/Node_graph_architecture>
- Aman Shekhar. *Designing Your Own Node-Based Visual Programming Language: A Practical Blueprint for Developers* (2025). Type-safety, modularity, and reusability patterns. <https://shekhar14.medium.com/designing-your-own-node-based-visual-programming-language-a-practical-blueprint-for-developers-08c9b9cdfb5c>
- Epic Games. *Specialized Blueprint Visual Scripting Node Groups in Unreal Engine* (UE 5.7 docs, 2026). Reference for our category / palette grouping. <https://dev.epicgames.com/documentation/en-us/unreal-engine/specialized-blueprint-visual-scripting-node-groups-in-unreal-engine>
- thedmd. *imgui-node-editor* (active Feb 2026). The library Phase 9E-3 picked for the editor canvas. <https://github.com/thedmd/imgui-node-editor>

Internal cross-references:

- `docs/phases/phase_09e_design.md` — the design document this subsystem implements (Phase 9E-1 + 9E-2).
- `docs/phases/phase_09e3_design.md` + `phase_09e3_research.md` — editor-side design (palette, canvas, debugging).
- `CODING_STANDARDS.md` §11 (errors), §12 (memory), §13 (threading), §17 (CPU/GPU), §18 (public API), §22 (DI / globals).
- `ARCHITECTURE.md` — Subsystem + Event Bus pattern (this is the canonical example).
- `CLAUDE.md` rules 1, 5, 7 (research-first, library currency, CPU/GPU placement).

## 15. Open questions

| # | Question | Owner | Target |
|---|----------|-------|--------|
| 1 | `Saved`-scope blackboard variables not yet persisted across application restarts — wire through `engine/core/settings.h` or a dedicated `<asset>/saved_scripts.json` slot? | milnet01 | Phase 10.10 entry |
| 2 | `ScriptInstance::m_graph` is a raw pointer with a "caller must outlive" contract (audit H2). Migrate to `shared_ptr<const ScriptGraph>` if the contract becomes a recurring footgun? | milnet01 | triage |
| 3 | `OnTriggerEnter` / `OnCollisionEnter` are registered as palette stubs pending `engine/physics/` event-bus emission. Two of the five gameplay templates (DamageZone, Checkpoint) depend on it. | milnet01 | Phase 10.10 entry |
| 4 | Pin intern table is single-threaded by contract. If a future phase moves graph compilation off-thread (e.g. background re-compile in the editor), add a mutex or partition the table. | milnet01 | triage |
| 5 | Performance budgets in §8 are placeholders. One-shot Tracy / RenderDoc capture pending. | milnet01 | Phase 11 audit |
| 6 | Bytecode back-end deferred — `CompiledScriptGraph` is the staging IR, but the runtime still walks `ScriptInstance` directly. Worth re-evaluating once 200+ active scripts per scene becomes a real workload. | milnet01 | post-MIT release (Phase 12+) |
| 7 | Multi-payload `ScriptCustomEvent` — current shape carries a single `ScriptValue`. Designers can JSON-encode richer payloads via the string slot, but that's awkward. Consider a `std::vector<ScriptValue>` or a typed payload registry. | milnet01 | triage |

## 16. Spec change log

| Date | Spec version | Author | Change |
|------|--------------|--------|--------|
| 2026-04-28 | 1.0 | milnet01 | Initial spec — `engine/scripting` foundation since Phase 9E-1, formalised post-Phase 10.9 audit (pin interning, type index, pure-node memoisation, generation token, output fan-out all incorporated). |
