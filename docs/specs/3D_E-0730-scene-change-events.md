# 3D_E-0730 — Publish scene-loaded and scene-unloaded events

**Status:** accepted (2026-09-30).
**Kind:** fix.
**Source:** ROADMAP 3D_E-0730 (split from 3D_E-0721, 2026-09-29).

Layman: when a scene is opened, created or replaced, the engine's parts are now told, so 2D physics sets itself up and cleans up after the old scene.

## 1. Goal

Every time the scene the user is working in changes, `SceneUnloadedEvent` is
published while the old contents still exist and `SceneLoadedEvent` is
published once the new contents are in place. `SystemRegistry` turns each into
a call to every active system's `onSceneUnload` or `onSceneLoad`. No replace
path can skip this, because the only way to empty a scene is the object that
publishes.

## 2. Problem

`engine/core/system_events.h` defines `SceneLoadedEvent` and
`SceneUnloadedEvent`. Nothing publishes either, and
`SystemRegistry::onSceneLoadAll` and `onSceneUnloadAll` have no caller outside
the tests. Consequences:

1. `Physics2DSystem::onSceneLoad` is the only caller of
   `Physics2DSystem::ensureBody`, so no 2D physics body is ever created in the
   running app.
2. `Physics2DSystem::onSceneUnload` and `ScriptingSystem::onSceneUnload` never
   run, so bodies, script subscriptions and the scene blackboard would outlive
   the scene they belong to.
3. `ScriptingSystem::subscribeEventNodes` subscribes `OnSceneLoaded` nodes to
   an event that never arrives.

A scene is not replaced by switching a pointer. These paths empty the same
`Scene` and refill it, each by calling `Scene::clearEntities` itself:
`SceneSerializer::loadScene`, `FileMenu::newScene`,
`applyEmptyScene` (first-run wizard) and `TemplateDialog::applyTemplate`.
`Engine::setupDemoScene`, `setupMaterialDemoScene` and `setupTabernacleScene`
fill a scene from `SceneManager::createScene`, which returns the existing scene
when the name is taken; `setupDemoScene` is also handed to the first-run wizard
as a callback, so it can run after start-up. `SceneManager::setActiveScene` and
`removeScene` have no caller outside the manager.

At start-up `Engine::initialize` builds the scene before it calls
`SystemRegistry::initializeAll`, so an event published while the first scene is
built would reach no system.

`SystemRegistry::onSceneLoadAll` begins with `activateSystemsForScene`, which
switches off every system whose owned component types are absent from the
scene. 3D_E-0721 made every initialized system active because that scan never
ran; calling `onSceneLoadAll` as it stands would undo it.

## 3. Scope decisions (agreed with the user)

- A short spec with an independent review, rather than building directly
  (user, 2026-09-30).
- Everything else below follows from §2.

## 4. Design

### 4.1 One way to replace a scene's contents

```cpp
// engine/scene/scene.h
class Scene
{
public:
    /// Empties the scene on construction and announces the new contents on
    /// destruction. The only way to empty a scene.
    class Replacement
    {
    public:
        explicit Replacement(Scene& scene);
        ~Replacement();
        Replacement(const Replacement&) = delete;
        Replacement& operator=(const Replacement&) = delete;
    };

    /// Set by SceneManager for the active scene; null for any other scene.
    void attachEventBus(EventBus* bus);

private:
    void clearEntities();            // was public
    EventBus* m_eventBus = nullptr;
    int m_replacementDepth = 0;
};
```

`Replacement`'s constructor, for the outermost replacement on a scene:
publishes `SceneUnloadedEvent(&scene)` if a bus is attached, then calls
`clearEntities`. Its destructor publishes `SceneLoadedEvent(&scene)` if a bus
is attached. A replacement opened while another is open on the same scene
clears again and publishes nothing.

`Scene::clearEntities` becomes private. The four paths in §2 that call it, and
the three `Engine::setup…Scene` functions, open a `Replacement` where they
clear or begin to fill the scene and hold it until the scene is filled.

`SceneSerializer::loadScene` has two overloads. The three-argument one
validates the file, clears the scene and fills it with entities. The
six-argument one calls it and then restores music, foliage and terrain from the
same file. One replacement spans all of that and still opens only after
validation: both overloads call a private helper that does the validation and
the entity fill, and emplaces the replacement into a
`std::optional<Scene::Replacement>` its caller owns, at the point where the
scene is cleared today. The six-argument overload keeps that optional alive
until the terrain is restored.

### 4.2 Which scene is announced

```cpp
// engine/scene/scene_manager.h
void SceneManager::attachEventBus(EventBus& bus);
```

`attachEventBus` stores the bus, attaches it to the active scene and publishes
`SceneLoadedEvent` for it. `Engine::initialize` calls it once, after the
registry has subscribed (§4.3). Before that call no scene has a bus, so
building the first scene publishes nothing.

Once a bus is stored, the active scene is the one scene that has it:

- `setActiveScene` to a different scene publishes `SceneUnloadedEvent` for the
  old one, moves the bus, and publishes `SceneLoadedEvent` for the new one.
- `createScene`, when it makes its scene active because none was, attaches the
  bus and publishes `SceneLoadedEvent`.
- `removeScene` of the active scene publishes `SceneUnloadedEvent` before the
  scene is destroyed.

### 4.3 Systems hear it

```cpp
// engine/core/system_registry.h
void SystemRegistry::subscribeSceneEvents(EventBus& bus);
```

`subscribeSceneEvents` subscribes to both events on the bus it is given and
keeps the bus and the two subscription ids. The handlers call `onSceneLoadAll`
and `onSceneUnloadAll` with the event's scene. `shutdownAll` unsubscribes if it
subscribed. `initializeAll` does not touch the bus: the registry tests pass it
an `Engine` that was never constructed (`dummyEngine` in
`tests/test_system_registry.cpp`).

`Engine::initialize` calls `subscribeSceneEvents(getEventBus())` after
`initializeAll` succeeds and before `SceneManager::attachEventBus`, so the
first announcement reaches the systems. A test makes its own `EventBus` and
calls `subscribeSceneEvents` with it.

`EventBus::publish` calls subscribers in subscription order. No engine code
registers a script instance before that call (`ScriptingSystem::registerInstance`
has no caller under `engine/`), so a system's `onSceneLoad` runs before a
script's `OnSceneLoaded` node.

### 4.4 Retire scene-driven activation

`SystemRegistry::activateSystemsForScene`, `ISystem::isForceActive` and
`ISystem::getOwnedComponentTypes` are deleted with every override and every
test that asserts them. `onSceneLoadAll` only calls `onSceneLoad`. A system is
active from `initializeAll` until `setActive(false)` or shutdown, as 3D_E-0721
left it. With the scan deleted the registry no longer switches any system off
when a scene loads.

## 5. Invariants

- **INV-1** — Replacing the contents of the scene that has the bus publishes
  exactly one `SceneUnloadedEvent` and then exactly one `SceneLoadedEvent`. An
  entity of the old contents is still found by id inside the unloaded handler;
  an entity added during the replacement is found inside the loaded handler.
  *Test:* `tests/test_scene_change_events.cpp`, which records the order of the
  two events and looks each entity up inside the handlers. The fixture
  isolates publish order relative to `clearEntities`.
  *Breaks when:* the constructor clears before it publishes, or the destructor
  publishes before the caller has filled the scene.

- **INV-2** — Only the active scene has the bus. Replacing the contents of any
  other scene publishes nothing.
  *Test:* `tests/test_scene_change_events.cpp`: a manager that holds a bus
  creates two scenes; the second, which is not active, is replaced while a
  subscriber counts events. The fixture isolates which scene the manager
  attaches the bus to.
  *Breaks when:* the manager attaches the bus to every scene it creates.

- **INV-3** — `SceneManager::attachEventBus` publishes one `SceneLoadedEvent`
  for the active scene, and none when there is no active scene.
  *Test:* `tests/test_scene_change_events.cpp`.
  *Breaks when:* the engine attaches the bus and the first scene is never
  announced, which is the start-up order §2 describes.

- **INV-4** — A replacement opened inside another on the same scene publishes
  nothing; the outer one still publishes one pair.
  *Test:* `tests/test_scene_change_events.cpp`, nesting two replacements.
  *Breaks when:* the depth counter is dropped, so a path that replaces and then
  calls a helper that also replaces announces twice.

- **INV-5** — After `initializeAll` and `subscribeSceneEvents`, a
  `SceneLoadedEvent` published on that bus calls `onSceneLoad` once on every
  active system, and a `SceneUnloadedEvent` calls `onSceneUnload` once. A
  system switched off with `setActive(false)` gets neither. After
  `shutdownAll`, `EventBus::getListenerCount` is what it was before
  `subscribeSceneEvents`.
  *Test:* `tests/test_system_registry.cpp`, with its own `EventBus` and mock
  systems that count the two calls. The fixture isolates the subscription:
  without `subscribeSceneEvents` every count stays zero. The listener count is
  read because `shutdownAll` also switches every system off, so call counts
  stay zero whether or not it unsubscribed.
  *Breaks when:* the registry does not subscribe, or `shutdownAll` leaves its
  handlers on the bus, where they hold a pointer to a registry that may be
  destroyed.

- **INV-6** — A scene holding an entity with `RigidBody2DComponent` and
  `Collider2DComponent`, filled inside a `Replacement` on the active scene,
  has a physics body for that entity afterwards; replacing the scene again
  removes it.
  *Test:* `tests/test_physics2d_system.cpp`. `Physics2DSystem::initialize`
  needs a constructed `Engine`, so the system is not put in a registry: the
  test uses that file's `PhysicsFixture` (its own world, handed over with
  `setPhysicsWorldForTesting`) and subscribes two handlers on its own
  `EventBus` that call the system's `onSceneLoad` and `onSceneUnload`. The
  registry's half of the path is INV-5's. The fixture isolates the order of
  the unload event and the clear.
  *Breaks when:* the unloaded event is published after the entities are gone,
  so `onSceneUnload` walks an empty scene and the body is left behind.

- **INV-7** — `SceneSerializer::loadScene` given a file it rejects publishes
  nothing and leaves the scene as it was.
  *Test:* `tests/test_scene_serializer.cpp`, loading a file with a newer
  `format_version` into an attached scene.
  *Breaks when:* the replacement is opened before validation.

## 6. Failure modes

- **Filling fails part-way** (an entity does not deserialize, or the filling
  code throws). The destructor still publishes `SceneLoadedEvent`, for whatever
  was filled. Systems are never left between an unload and a load.
- **A handler replaces the scene.** It opens a nested `Replacement` only if the
  outer one is still open, which INV-4 covers. A handler of `SceneLoadedEvent`
  that replaces the scene starts a new outermost replacement and the events
  recurse; `EventBus` supports re-entrant publish. Handlers must not do this.
  Nothing detects it.
- **A replacement during `Scene::update`.** `Scene::clearEntities` does not
  defer while the scene is updating, unlike `removeEntity`. That hazard exists
  today and this spec does not change it.
- **The bus outlives or predates the scene.** `Scene` holds a raw pointer.
  `Engine` owns both the bus and the scene manager; the manager must be
  destroyed, or detached, before the bus.

## 7. Tests

| Test file | Locks |
|---|---|
| `tests/test_scene_change_events.cpp` (new) | INV-1, INV-2, INV-3, INV-4 |
| `tests/test_system_registry.cpp` | INV-5 |
| `tests/test_physics2d_system.cpp` | INV-6 |
| `tests/test_scene_serializer.cpp` | INV-7 |

Each is seen failing first, against the finished code with one part broken:

| Invariant | Broken part it fails against |
|---|---|
| INV-1 | `Replacement` clears before it publishes the unload |
| INV-2 | `createScene` attaches the bus to every scene it makes |
| INV-3 | `attachEventBus` stores the bus and publishes nothing |
| INV-4 | the depth counter removed |
| INV-5 | `subscribeSceneEvents` not called; and, for the listener count, `shutdownAll` does not unsubscribe |
| INV-6 | `Replacement` clears before it publishes the unload |
| INV-7 | the replacement opened before validation |

The current tree has no `Replacement`, so it cannot compile these tests; a
build failure is not the red run.

The three tests that call `clearEntities` directly
(`test_camera_component.cpp`, `test_scene.cpp`, `test_scene_serializer.cpp`)
move to `Replacement`. The tests of the activation API are deleted with it.

## 8. Alternatives considered (and rejected)

- **Publish at each replace path.** Every path in §2 would carry its own
  pair of publishes, and the next one added forgets. Rejected for a private `clearEntities`, which the compiler enforces.
- **Publish from `Scene::clearEntities` and a separate `notifyLoaded()`.** The
  unload half cannot be skipped but the load half can, and a path that forgets
  it leaves systems unloaded for good. Rejected for one object that does both.
- **Treat a change of active-scene pointer as the only load.** No path changes
  the pointer today, so nothing would ever be announced.
- **Keep the activation scan and mark most systems force-active.** It restores
  the defect 3D_E-0721 fixed for any system nobody marked.

## 9. Out of scope

- Creating script instances from `ScriptComponent` on load.
  `ScriptingSystem::onSceneLoad` is a stub, and `onSceneUnload` clears every
  instance, so after a replacement no instance exists to hear `OnSceneLoaded`
  until something registers one — deferred; not yet queued.
- A 2D physics body for a component added after the scene loaded — deferred;
  not yet queued.
- Deferring a replacement requested during `Scene::update` — deferred; not yet
  queued.
- `Engine::setupDemoScene` run a second time refills the scene it already
  filled. Under §4.1 it now empties it first. Whether the rest of that function
  is safe to run twice (terrain, foliage, grass) is not examined here. When the
  app started on another scene (`--biblical-demo`, `--material-demo`), the
  wizard's call makes a second scene that is not active, so nothing is
  announced for it — deferred; not yet queued.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1, INV-2, INV-3, INV-4 | `tests/test_scene_change_events.cpp` |
| INV-5 | `tests/test_system_registry.cpp` |
| INV-6 | `tests/test_physics2d_system.cpp` |
| INV-7 | `tests/test_scene_serializer.cpp` |
| No path empties a scene without a `Replacement` (§4.1) | The compiler: `Scene::clearEntities` is private |
| A replace path holds its `Replacement` until the scene is filled (§4.1) | **nothing** — a path that lets it go early announces a half-filled scene |
| `Engine::initialize` calls `subscribeSceneEvents` and then `attachEventBus`, in that order (§4.3) | **nothing** — in the other order the first announcement reaches no system |
| Handlers do not replace the scene (§6) | **nothing** |

## 11. Cross-doc impact

- `CHANGELOG.md`: one entry.
- `docs/engine/systems/spec.md` lists each system's `isForceActive` and
  `getOwnedComponentTypes` overrides and describes activation by scene
  contents. It is rewritten in the same change to match §4.4.
- `docs/phases/phase_09a_design.md` describes scene-driven activation. It is a
  closed design document and is not edited.

## 12. Cold-eyes loop log

Rows live in `../reviews/3D_E-0730-scene-change-events-loop-log.md`.
