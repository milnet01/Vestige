# Subsystem Specification — `engine/scene`

## Header

| Field | Value |
|-------|-------|
| Subsystem | `engine/scene` |
| Status | `shipped` |
| Spec version | `1.0` |
| Last reviewed | `2026-04-28` (initial draft — pending cold-eyes review) |
| Owners | `milnet01` |
| Engine version range | `v0.1.0+` (Scene + SceneManager since Phase 3; per-component additions through Phase 10.9) |

---

## 1. Purpose

`engine/scene` is the in-memory representation of "the world the player is walking around in." It owns the `Scene` (a tree of `Entity` nodes), the `SceneManager` (named-scene registry + active-scene pointer), the `Entity` + `Transform` + `Component` core types, and the concrete `Component` subclasses that downstream systems and the renderer consume — `MeshRenderer`, the three light components, `CameraComponent`, the camera-mode strategy, particle emitters (Central Processing Unit (CPU) and Graphics Processing Unit (GPU) variants), water surfaces, sprite + tilemap (the 2D path), interactable + pressure-plate gameplay markers, and the 2D rigid-body / collider / character-controller / camera triplet. It exists as its own subsystem because every other domain system reads scene state — the renderer collects render data, physics walks rigid bodies, audio queries listener position, scripts mutate entities — and pushing those types into any one consumer would force unrelated subsystems to depend on each other through that consumer. For the engine's primary use case — first-person architectural walkthroughs of biblical structures (Tabernacle, Solomon's Temple) — `engine/scene` is the data model that holds "the Tabernacle" while every other subsystem renders it, lights it, plays audio in it, and lets the user interact with it.

## 2. Scope

| In scope | Out of scope |
|----------|--------------|
| `Scene` — entity tree, id index, active-camera pointer, deferred-mutation queue, render-data + collider collection | Renderer pass orchestration, framebuffer / shader binding — `engine/renderer/` |
| `SceneManager` — named-scene registry + active-scene pointer + per-frame `update(dt)` dispatch | System lifecycle / phase ordering — `engine/core/SystemRegistry` |
| `Entity` — transform, parent / children, component map, world matrix cache, active / visible / locked flags, deep-clone | Scene serialisation to / from JavaScript Object Notation (JSON) on disk — `engine/editor/scene_serializer.{h,cpp}`, `engine/utils/entity_serializer.{h,cpp}`, `engine/utils/component_serializer_registry.h` |
| `Transform` — position / Euler-rotation / scale + matrix-override escape hatch | Skeletal pose / morph-target animation state — `engine/animation/` (the bone matrices + morph weights flow *through* `SceneRenderData::RenderItem` but are owned by animation components in `engine/animation/`) |
| `Component` base class + `ComponentTypeId` (compile-time-stable per-type id) + per-subclass `clone()` contract | Scripting node graph that mutates components — `engine/scripting/` |
| `MeshRenderer` — mesh + material + collision Axis-Aligned Bounding Box (AABB) + culling AABB + cast-shadows flag | `Mesh`, `Material`, `Texture` resource ownership — `engine/renderer/` + `engine/resource/` |
| `DirectionalLightComponent`, `PointLightComponent`, `SpotLightComponent`, `EmissiveLightComponent` (auto-derived from emissive material) | Shadow-map allocation, cluster building — `engine/renderer/` |
| `CameraComponent` — Field Of View (FOV), near / far, projection-type, view + projection matrices derived from entity transform | The renderer's standalone `Camera` (kept for backwards compat; `CameraComponent::syncToCamera` is the bridge) — `engine/renderer/camera.h` |
| `CameraMode` strategy interface + `CameraViewOutput` + `CameraInputs` + `blendCameraView()` | Concrete first / third / iso / top-down / cinematic mode bodies — `engine/camera/` |
| `ParticleEmitterComponent` (CPU Structure Of Arrays (SoA) sim) + `GPUParticleEmitter` (compute-shader sim) + `ParticlePresets` factories | Particle vertex / fragment passes — `engine/renderer/` |
| `WaterSurfaceComponent` — wave config + GPU mesh cache + reflection-mode enum | Water shader, planar-reflection Frame Buffer Object (FBO), caustics texture — `engine/renderer/` |
| `SpriteComponent`, `TilemapComponent` — 2D draw atoms (atlas + frame name + sort key) | Sprite atlas asset, batched draw — `engine/animation/sprite_animation.h`, `engine/renderer/sprite_system.h` |
| `InteractableComponent`, `PressurePlateComponent` + `computePressurePlateCenter()` helper | Grab-system + puzzle-system behaviour — `engine/systems/` |
| `RigidBody2DComponent`, `Collider2DComponent`, `CharacterController2DComponent` + `stepCharacterController2D` | Jolt body lifecycle, contact listener — `engine/systems/physics2d_system.h`, `engine/physics/` |
| `Camera2DComponent` + `updateCamera2DFollow` deadzone / spring helper | The 3D-camera follow code — `engine/core/first_person_controller.h` |
| `game_templates_2d.h` — `createSideScrollerTemplate` / `createShmupTemplate` (build-a-starter-scene helpers) | Real art binding — `engine/editor/` |
| `SceneRenderData` — Plain Old Data (POD) bundle the renderer reads (render items, lights, particle / water emitters, cloth items) | Render-item submission to Open Graphics Library (OpenGL) — `engine/renderer/` |
| Mid-update mutation contract — `Scene::beginUpdate / endUpdate / ScopedUpdate`, deferred add / remove queues | Script node implementations that depend on this contract — `engine/scripting/` |
| Photosensitive-safe forwarding — `Scene::collectRenderData` propagates `photosensitiveEnabled` + `PhotosensitiveLimits` to `getCoupledLight` | Photosensitive-cap definitions / sliders — `engine/accessibility/` |

## 3. Architecture

```
                          ┌──────────────────────────────┐
                          │        SceneManager          │
                          │ (engine/scene/scene_manager  │
                          │  .h:18) name → Scene*        │
                          └──────────────┬───────────────┘
                                         │ active scene
                                         ▼
                          ┌──────────────────────────────┐
                          │            Scene             │
                          │ root Entity + id index +     │
                          │ active CameraComponent +     │
                          │ deferred-mutation queue      │
                          └──────────────┬───────────────┘
                                         │ owns tree
                                         ▼
                          ┌──────────────────────────────┐
                          │           Entity             │
                          │ Transform + parent/children  │
                          │ + components map             │
                          └──────────────┬───────────────┘
                                         │ owns components
   ┌───────────┬───────────┬─────────────┼─────────────┬───────────┬──────────┐
   ▼           ▼           ▼             ▼             ▼           ▼          ▼
┌───────┐ ┌────────┐ ┌──────────┐ ┌─────────────┐ ┌────────┐ ┌─────────┐ ┌────────┐
│Mesh-  │ │*Light- │ │Camera-   │ │Particle-    │ │Water-  │ │Sprite-  │ │RB2D /  │
│Render │ │Compo-  │ │Component │ │Emitter (CPU │ │Surface │ │Tilemap  │ │Coll2D /│
│er     │ │nent    │ │+ Camera- │ │ + GPU)      │ │        │ │         │ │CC2D /  │
│       │ │        │ │Mode      │ │             │ │        │ │         │ │Cam2D   │
└───────┘ └────────┘ └──────────┘ └─────────────┘ └────────┘ └─────────┘ └────────┘
   │ producer of                                               + Interactable /
   ▼                                                            PressurePlate
SceneRenderData (POD)  →  consumed by engine/renderer/
collectColliders() →       consumed by FirstPersonController
```

Key abstractions:

| Abstraction | Type | Purpose |
|-------------|------|---------|
| `SceneManager` | class | Named-scene registry + active-scene pointer; per-frame `update(dt)` dispatch. `engine/scene/scene_manager.h:18` |
| `Scene` | class | Entity tree + id index + active camera + mid-update mutation queue. `engine/scene/scene.h:89` |
| `SceneRenderData` | struct (POD) | What the renderer pulls per frame: opaque + transparent items, lights, particle emitters, water surfaces, cloth. `engine/scene/scene.h:36` |
| `Scene::ScopedUpdate` | nested Resource Acquisition Is Initialization (RAII) class | Marks a scene as "currently iterating" so `createEntity` / `removeEntity` queue rather than mutate the tree. `engine/scene/scene.h:242` |
| `Entity` | class | Tree node — transform, parent, children, component map, world matrix cache. `engine/scene/entity.h:72` |
| `Transform` | struct | Position / Euler / scale + matrix-override escape hatch (skeletal authors set the matrix directly). `engine/scene/entity.h:23` |
| `Component` | abstract base | Per-type attached behaviour; pure-virtual `clone()` so `Entity::clone` deep-copies every subclass. `engine/scene/component.h:33` |
| `ComponentTypeId` | utility class | Process-stable atomic id per `T`; the entity map keys on it. `engine/scene/component.h:18` |
| `MeshRenderer` | component | Mesh + material + collision AABB + culling AABB + cast-shadows. `engine/scene/mesh_renderer.h:19` |
| `DirectionalLightComponent` / `PointLightComponent` / `SpotLightComponent` / `EmissiveLightComponent` | components | Light sources attached to entities; world position derives from entity transform. `engine/scene/light_component.h:17,32,47,63` |
| `CameraComponent` | component | View + projection from entity transform; reverse-Z infinite-far for rendering, finite-far for culling. `engine/scene/camera_component.h:30` |
| `CameraMode` | abstract base | Per-entity strategy producing `CameraViewOutput` from `CameraInputs`; pure function for testability. `engine/scene/camera_mode.h:126` |
| `CameraViewOutput` | struct (POD) | Per-frame mode result; equality operator so `CameraBlender` can lerp. `engine/scene/camera_mode.h:59` |
| `ParticleEmitterComponent` + `ParticleEmitterConfig` + `ParticleData` | components | CPU SoA particle sim with swap-on-death; light-coupling clamps to `PhotosensitiveLimits`. `engine/scene/particle_emitter.h:25,89,116` |
| `GPUParticleEmitter` | component | Compute-shader sim (10 to 50 times the CPU path) with composable behaviours; falls back to CPU if compute unavailable. `engine/scene/gpu_particle_emitter.h:42` |
| `WaterSurfaceComponent` + `WaterSurfaceConfig` | component | Up-to-4 summed sine waves + colour gradient + reflection-mode enum + caustics. `engine/scene/water_surface.h:78,27` |
| `SpriteComponent` | component | 2D atlas-frame reference + tint + pivot + sort key + optional `SpriteAnimation`. `engine/scene/sprite_component.h:34` |
| `TilemapComponent` + `TilemapLayer` + `TilemapAnimatedTile` | components | Multi-layer tile grid + animated-tile sequences + `forEachVisibleTile` walker. `engine/scene/tilemap_component.h:80,34,59` |
| `InteractableComponent` | component | Marks grab / push / toggle target; max mass, throw force, hold-spring tuning. `engine/scene/interactable_component.h:25` |
| `PressurePlateComponent` + `computePressurePlateCenter` | component + helper | Sphere-overlap trigger volume; helper returns world-space query centre (parented-plate fix). `engine/scene/pressure_plate_component.h:45,37` |
| `RigidBody2DComponent`, `Collider2DComponent`, `CharacterController2DComponent`, `Camera2DComponent` | components | The 2D-game stack: Jolt-backed body + shape description + platformer controller + smooth-follow camera. `engine/scene/rigid_body_2d_component.h:33`, `collider_2d_component.h:32`, `character_controller_2d_component.h:35`, `camera_2d_component.h:29` |
| `createSideScrollerTemplate` / `createShmupTemplate` | free functions | Build-a-starter-scene helpers for the 2D path. `engine/scene/game_templates_2d.h:65,81` |
| `ParticlePresets` | utility struct | Authored-config factories — `torchFire`, `candleFlame`, `campfire`, `smoke`, `dustMotes`, `incense`, `sparks`. `engine/scene/particle_presets.h:14` |

## 4. Public API

The subsystem has 21 public headers (every file under `engine/scene/`); per CODING_STANDARDS section 18 every header is `#include`-able from downstream code. Apply the **facade pattern** — group by header, show the headline shape, and point the reader at the source for the full surface.

```cpp
/// Scene tree + active-scene registry.
/// engine/scene/scene_manager.h
class SceneManager
{
public:
    Scene*  createScene(const std::string& name);
    bool    setActiveScene(const std::string& name);
    Scene*  getActiveScene();
    void    update(float dt);          // forwards to active scene
    void    removeScene(const std::string& name);
    size_t  getSceneCount() const;
};
```

```cpp
/// engine/scene/scene.h — see header for the full surface.
struct SceneRenderData {
    struct RenderItem { /* mesh, material, world matrix, AABB, entity id,
                          shadow flag, lock flag, bone matrices, morph
                          weights + Shader Storage Buffer Object (SSBO),
                          previous-frame world matrix */ };
    std::vector<RenderItem>                          renderItems;
    std::vector<RenderItem>                          transparentItems;
    DirectionalLight                                 directionalLight;
    bool                                             hasDirectionalLight;
    std::vector<PointLight>                          pointLights;
    std::vector<SpotLight>                           spotLights;
    std::vector<std::pair<const ParticleEmitterComponent*, glm::mat4>> particleEmitters;
    std::vector<std::pair<const WaterSurfaceComponent*,    glm::mat4>> waterSurfaces;
    std::vector<ClothRenderItem>                     clothItems;
};

class Scene
{
public:
    explicit Scene(const std::string& name = "Untitled Scene");

    Entity*           createEntity(const std::string& name = "Entity");
    bool              removeEntity(uint32_t id);
    Entity*           duplicateEntity(uint32_t id);
    bool              reparentEntity(uint32_t id, uint32_t newParentId);
    Entity*           findEntity(const std::string& name);
    Entity*           findEntityById(uint32_t id);
    Entity*           getRoot();
    void              clearEntities();

    void              update(float dt);
    SceneRenderData   collectRenderData(bool photosensitiveEnabled = false,
                                         const PhotosensitiveLimits& = {}) const;
    void              collectRenderData(SceneRenderData& out, /* ... */) const;  // capacity-preserving
    std::vector<AABB> collectColliders() const;
    void              collectColliders(std::vector<AABB>& out) const;

    void              setActiveCamera(CameraComponent*);
    CameraComponent*  getActiveCamera() const;

    void              forEachEntity(const std::function<void(Entity&)>&);
    void              forEachEntity(const std::function<void(const Entity&)>&) const;
    void              rebuildEntityIndex();        // call after bulk deserialise

    // Mid-update mutation contract.
    void              beginUpdate();
    void              endUpdate();
    bool              isUpdating() const;
    class             ScopedUpdate;  // RAII wrapper
};
```

```cpp
/// engine/scene/entity.h
struct Transform
{
    glm::vec3 position, rotation /* Euler degrees */, scale;
    glm::mat4 getLocalMatrix() const;
    void      setLocalMatrix(const glm::mat4&);  // override TRS path
    bool      hasMatrixOverride() const;
    void      clearMatrixOverride();
};

class Entity
{
public:
    explicit Entity(const std::string& name = "Entity");
    uint32_t  getId() const;                          // process-unique, never 0

    template <class T, class... A> T* addComponent(A&&...);
    template <class T> T*       getComponent();
    template <class T> bool     hasComponent() const;
    template <class T> void     removeComponent();
    std::vector<uint32_t>       getComponentTypeIds() const;

    Entity*   addChild(std::unique_ptr<Entity>);
    Entity*   insertChild(std::unique_ptr<Entity>, size_t index);
    std::unique_ptr<Entity> removeChild(Entity*);
    Entity*   findChild   (const std::string&);     // non-recursive
    Entity*   findDescendant(const std::string&);   // recursive

    Transform   transform;
    glm::mat4   getWorldMatrix()  const;            // cached
    glm::vec3   getWorldPosition() const;

    std::unique_ptr<Entity> clone() const;          // deep — components via Component::clone
    void        setActive(bool); bool isActive() const;
    void        setVisible(bool); bool isVisible() const;     // render-only gate
    void        setLocked(bool);  bool isLocked()  const;     // viewport-pick gate
};
```

```cpp
/// engine/scene/component.h
class Component
{
public:
    virtual ~Component() = default;
    virtual void update(float dt);
    Entity* getOwner() const;   void setOwner(Entity*);
    void    setEnabled(bool);   bool isEnabled() const;
    /// Pure-virtual — every subclass must deep-copy itself. Audit
    /// trail: ROADMAP Phase 10.9 Slice 1 F2 (a non-overriding subclass
    /// silently dropped from `Entity::clone`).
    virtual std::unique_ptr<Component> clone() const = 0;
};

class ComponentTypeId  // template<T> static get<T>() returns a stable atomic id.
{
public:
    template <class T> static uint32_t get();
};
```

```cpp
/// engine/scene/mesh_renderer.h
class MeshRenderer : public Component
{
public:
    MeshRenderer();
    MeshRenderer(std::shared_ptr<Mesh>, std::shared_ptr<Material>);
    void        setMesh(std::shared_ptr<Mesh>);
    void        setMaterial(std::shared_ptr<Material>);
    const AABB& getBounds()        const;   void setBounds(const AABB&);
    const AABB& getCullingBounds() const;   void setCullingBounds(const AABB&);
    void        setCastsShadow(bool); bool castsShadow() const;
    std::unique_ptr<Component> clone() const override;
};
```

```cpp
/// engine/scene/light_component.h — four light components, each is a
/// thin POD wrapper around the renderer's Directional/Point/SpotLight.
class DirectionalLightComponent : public Component { DirectionalLight light; /* + clone */ };
class PointLightComponent       : public Component { PointLight       light; /* + clone */ };
class SpotLightComponent        : public Component { SpotLight        light; /* + clone */ };
class EmissiveLightComponent    : public Component {
    float lightRadius = 5.0f, lightIntensity = 1.0f;
    glm::vec3 overrideColor = glm::vec3(0.0f);   /* + clone */
};
```

```cpp
/// engine/scene/camera_component.h
enum class ProjectionType : uint8_t { PERSPECTIVE, ORTHOGRAPHIC };

class CameraComponent : public Component
{
public:
    float fov = DEFAULT_FOV, nearPlane = 0.1f, farPlane = 1000.0f, orthoSize = 10.0f;
    ProjectionType projectionType = ProjectionType::PERSPECTIVE;

    glm::mat4   getViewMatrix() const;
    glm::mat4   getProjectionMatrix       (float aspect) const;  // reverse-Z, infinite-far
    glm::mat4   getCullingProjectionMatrix(float aspect) const;  // finite-far for culling
    glm::vec3   getWorldPosition() const, getForward() const, getRight() const, getUp() const;
    const Camera& getCamera() const;        // legacy bridge
    void        syncFromCamera(const Camera&);
    void        syncToCamera(Camera&) const;
};
```

```cpp
/// engine/scene/camera_mode.h
enum class CameraModeType : uint8_t { FirstPerson, ThirdPerson, Isometric, TopDown, Cinematic };

struct CameraViewOutput { /* POD: position, orientation, fov, ortho, near, far, projection */ };
struct CameraInputs     { const Entity* target; glm::vec2 mouseLookDelta; const InputActionMap*; const PhysicsWorld*; float aspectRatio; };

class CameraMode : public Component
{
public:
    virtual CameraViewOutput computeOutput(const CameraInputs&, float dt) = 0;
    virtual CameraModeType   type() const = 0;
};

CameraViewOutput blendCameraView(const CameraViewOutput& from,
                                  const CameraViewOutput& to, float t);
```

```cpp
/// engine/scene/particle_emitter.h — see header for full ParticleEmitterConfig.
struct ParticleEmitterConfig {
    float emissionRate, /* min/max lifetime, speed, size; */ ;
    int   maxParticles;
    enum class Shape { POINT, SPHERE, CONE, BOX } shape;
    enum class BlendMode { ADDITIVE, ALPHA_BLEND } blendMode;
    bool  emitsLight; /* + lightColor / range / intensity / flickerSpeed */
    /* over-lifetime curves: ColorGradient, AnimationCurve x2 */
};
struct ParticleData { /* SoA arrays, swap-on-death compaction */ };

class ParticleEmitterComponent : public Component
{
public:
    void update(float dt) override;
    ParticleEmitterConfig&       getConfig();
    const ParticleData&          getData() const;
    void                         restart(); void setPaused(bool); bool isPaused() const;
    bool                         isPlaying() const;
    /// Coupled point light for fire emitters; flickerSpeed is clamped
    /// through `clampStrobeHz(limits)` when `photosensitiveEnabled`.
    std::optional<PointLight>    getCoupledLight(const glm::vec3& worldPos,
                                                  bool photosensitiveEnabled = false,
                                                  const PhotosensitiveLimits& = {}) const;
};
```

```cpp
/// engine/scene/gpu_particle_emitter.h — same config schema; compute-shader sim.
class GPUParticleEmitter : public Component
{
public:
    bool init(const std::string& shaderPath);                   // allocate compute buffers
    void update(float dt) override;
    void setConfig(const ParticleEmitterConfig&);
    void addBehavior(ParticleBehaviorType, const BehaviorParams&);  // composable
    void removeBehavior(ParticleBehaviorType); void clearBehaviors();
    int  getBehaviorCount() const;
    void setPaused(bool); bool isPaused() const;
    void restart(); bool isPlaying() const;
    uint32_t getAliveCount() const;     // 1-frame delayed (avoids GPU stall)
    bool     isGPUPath()    const;      // false → CPU fallback active
    void     enableDepthCollision(GLuint depthTex, const glm::mat4& vp,
                                   const glm::vec2& screenSize, float near);
    void     buildBehaviorsFromConfig();  // converts gravity / lifetime curves
};
```

```cpp
/// engine/scene/water_surface.h — see header for full config.
enum class WaterReflectionMode { NONE, PLANAR, CUBEMAP };
struct WaterSurfaceConfig { /* width/depth/grid + 4 sine waves + colours + caustics */ };

class WaterSurfaceComponent : public Component
{
public:
    WaterSurfaceConfig& getConfig();
    void rebuildMeshIfNeeded() const;                   // lazy GPU-mesh cache
    GLuint getVao() const;  int getIndexCount() const;
    float  getLocalWaterY() const;                       // always 0 (local-space plane)
};
```

```cpp
/// engine/scene/sprite_component.h
class SpriteComponent : public Component
{
public:
    std::shared_ptr<SpriteAtlas> atlas;
    std::string                  frameName;
    glm::vec4                    tint = glm::vec4(1.0f);
    glm::vec2                    pivot = glm::vec2(0.5f);
    bool                         flipX = false, flipY = false;
    float                        pixelsPerUnit = 100.0f;
    int                          sortingLayer = 0, orderInLayer = 0;
    bool                         sortByY = false, isTransparent = true;
    std::shared_ptr<SpriteAnimation> animation;          // optional
};
```

```cpp
/// engine/scene/tilemap_component.h
using TileId = std::uint16_t;
constexpr TileId kEmptyTile = 0;
struct TilemapLayer { /* width, height, zOrder, tile-id grid */ };
struct TilemapAnimatedTile { /* base id, frame names, framePeriodSec, pingPong */ };
struct TilemapTileDefinition { /* atlas frame + animated flag */ };

class TilemapComponent : public Component
{
public:
    std::shared_ptr<SpriteAtlas>          atlas;
    std::vector<TilemapTileDefinition>    tileDefs;
    std::vector<TilemapAnimatedTile>      animations;
    std::vector<TilemapLayer>             layers;
    float tileWorldSize = 1.0f, pixelsPerUnit = 100.0f;
    int   sortingLayer = 0, orderInLayer = 0;
    std::size_t addLayer(const std::string&, int w, int h);
    std::string resolveFrameName(TileId id) const;
    void        forEachVisibleTile(const std::function<bool(std::size_t,int,int,
                                                             const std::string&)>&) const;
};
```

```cpp
/// engine/scene/interactable_component.h
enum class InteractionType : uint8_t { GRAB, PUSH, TOGGLE };
class InteractableComponent : public Component
{
public:
    InteractionType type = InteractionType::GRAB;
    float           maxGrabMass = 50.0f, throwForce = 10.0f, grabDistance = 3.0f,
                    holdDistance = 1.5f, holdSpringFrequency = 8.0f, holdSpringDamping = 1.0f;
    bool            highlighted = false;
    std::string     promptText  = "Grab";
};
```

```cpp
/// engine/scene/pressure_plate_component.h
glm::vec3 computePressurePlateCenter(const Entity& owner, float detectionHeight);

class PressurePlateComponent : public Component
{
public:
    void  setPhysicsWorld(PhysicsWorld*);
    bool  isActivated() const; size_t getOverlapCount() const;
    std::function<void()> onActivate, onDeactivate;
    float detectionRadius = 1.0f, detectionHeight = 0.5f, queryInterval = 0.1f;
    bool  inverted = false;
};
```

```cpp
/// engine/scene/rigid_body_2d_component.h, collider_2d_component.h,
///        character_controller_2d_component.h, camera_2d_component.h.
enum class BodyType2D     { Static, Kinematic, Dynamic };
enum class ColliderShape2D { Box, Circle, Capsule, Polygon, EdgeChain };

class RigidBody2DComponent  : public Component { /* type, mass, friction, ..., JPH::BodyID */ };
class Collider2DComponent   : public Component { /* shape, halfExtents, radius, height, vertices */ };
class CharacterController2DComponent : public Component { /* coyote / buffer / wall-slide tuning + runtime state */ };
class Camera2DComponent     : public Component { /* deadzone + critical-damped spring + world-bounds clamp */ };

bool stepCharacterController2D(CharacterController2DComponent&, Entity&,
                               Physics2DSystem&, const CharacterControl2DInput&, float dt);
void updateCamera2DFollow(Camera2DComponent&, const glm::vec2& targetWorldPos, float dt);
```

```cpp
/// engine/scene/game_templates_2d.h — starter-scene factories.
struct GameTemplate2DConfig { std::shared_ptr<SpriteAtlas> atlas; /* + frame names */ };
Entity* createSideScrollerTemplate(Scene&, const GameTemplate2DConfig& = {});
Entity* createShmupTemplate       (Scene&, const GameTemplate2DConfig& = {});
```

```cpp
/// engine/scene/particle_presets.h — authored-config factories.
struct ParticlePresets {
    static ParticleEmitterConfig torchFire(), candleFlame(), campfire(),
                                  smoke(), dustMotes(), incense(), sparks();
};
```

**Non-obvious contract details:**

- **Process-stable component type ids.** `ComponentTypeId::get<T>()` returns a `static` atomic id assigned in registration order on first call. Ids are stable within one process run but **not** across runs — never persist them. Persistence keys on the JSON `typeName` string registered in `ComponentSerializerRegistry`.
- **Mid-update mutation queueing (Phase 10.9 Slice 3 S2).** While `Scene::isUpdating()` is true (i.e. inside `Scene::update`, `forEachEntity`, or any caller-supplied `ScopedUpdate`), `createEntity` / `removeEntity` / `duplicateEntity` enqueue rather than mutate. The id index is updated **eagerly** on `createEntity` so `findEntityById` resolves the spawn within the same frame, even though the tree-attach is deferred. Drain order on depth → 0: pending adds first, then pending removals (so a "spawn-then-destroy" pair in the same frame goes through the normal `unregisterEntityRecursive` path rather than touching the live tree).
- **Active camera clears on remove.** `Scene::removeEntity` recursively walks the subtree and nulls `m_activeCamera` if the active `CameraComponent` lived inside (Phase 10.9 Slice 1 S1). Otherwise the renderer would dereference a freed pointer the next frame.
- **`Entity::clone` deep-copies components.** Every concrete `Component` subclass must override `clone()` (pure-virtual since Phase 10.9 Slice 1 F2). GPU-owning components (`WaterSurfaceComponent`, `GPUParticleEmitter`, `ClothComponent`) clone configuration + CPU state only — the clone re-creates GPU resources lazily on first use or via an explicit `init`.
- **`SceneRenderData::RenderItem::prevWorldMatrix`** is identity at construction; the renderer's motion-vector pass owns a per-entity-id cache and writes the previous frame's matrix back into the item before the Temporal Anti-Aliasing (TAA) reproject. `engine/scene` does not maintain this history — collection is stateless.
- **`Entity::isVisible()` gates rendering only.** Invisible entities still tick every frame and still appear in `findEntity` / `forEachEntity`. The renderer skips them in `collectRenderData`. For a tick-and-render skip, use `setActive(false)`.
- **`Entity::isLocked()` gates the editor's id-buffer pick** and is otherwise inert at runtime.
- **`CameraComponent::farPlane`** is **only** used for culling. Rendering uses a reverse-Z infinite-far projection so far-plane Z-fighting is impossible regardless of the value.
- **`Scene::collectRenderData` is `const`** but the `WaterSurfaceComponent::rebuildMeshIfNeeded` it calls is a `mutable` GPU-mesh cache. The const-correctness escape hatch is deliberate — the cache is opaque to the caller's mental model.
- **Renderer path for fire emitters reads `getCoupledLight(...)`.** When `Settings::accessibility.photosensitiveSafetyEnabled` is on, the engine forwards `Settings::accessibility.photosensitive` to `Scene::collectRenderData`, which forwards to the emitter, which clamps `flickerSpeed` through `clampStrobeHz(limits)`. The authored `ParticleEmitterConfig::flickerSpeed` is preserved in memory — only the effective per-frame frequency is clamped.

**Stability:** the public API listed above is semver-frozen for `v0.x`. Two known evolution points: (a) `Entity::transform.rotation` stores Euler degrees — a quaternion variant is on the roadmap and would require a versioning bump on the JSON wire format; (b) `Scene::collectRenderData` will likely grow a frustum-cull early-out parameter when terrain chunking lands (project memory `project_terrain_chunking.md`). Both are additive when they land.

## 5. Data Flow

**Per-frame (`SceneManager::update` driven by `Engine::run` step 7 — see `engine/core/spec.md` §5):**

1. Caller → `SceneManager::update(dt)` → forwards to `Scene::update(dt)`.
2. `Scene::update` opens a `ScopedUpdate` guard → walks the tree depth-first via `m_root->update(dt, identity)`, which:
   - Updates each entity's world matrix from `parentWorldMatrix * Transform::getLocalMatrix()`.
   - Calls `Component::update(dt)` on every enabled component (`MeshRenderer` is a no-op; particle emitters tick the SoA sim; tilemap advances the global animation timer; etc.).
   - Recurses into children with the freshly-computed world matrix.
3. Any `createEntity` / `removeEntity` / `duplicateEntity` calls during step 2 enqueue (see §4 contract details).
4. `ScopedUpdate` destructs → `endUpdate` → `drainPendingMutations` applies queued adds, then queued removals.
5. Renderer (separately) → `Scene::collectRenderData(out, photosensitiveEnabled, limits)` → recurses, appending to the supplied `SceneRenderData` (capacity preserved across frames per the audit-driven hot-path contract).
6. `FirstPersonController` (or character-physics path) → `Scene::collectColliders(out)` → builds AABB list.
7. Renderer reads `out` and submits draws.

**Cold scene-load (`SceneSerializer::loadScene` — `engine/editor/scene_serializer.cpp:245`, lives outside this subsystem but is the only legitimate scene-load path):**

1. `openAndParseSceneJson` enforces a 256 MiB size cap, parses with `nlohmann::json`.
2. Validates the `vestige_scene` envelope + `format_version` integer; refuses future versions.
3. `migrateScene(...)` walks v1→vN migrations (currently a stub; first migration lands when format_version increments).
4. `Scene::clearEntities` → drops the existing tree, keeps the root.
5. For each JSON entry in `entities[]`, `EntitySerializer::deserializeEntity` creates an `Entity` and dispatches each component dict by name through `ComponentSerializerRegistry::findByName`. Unknown component names emit a `Logger::warning` and are skipped — the rest of the entity loads (forward-compatibility, Phase 10.9 Slice 1 F3).
6. `Scene::rebuildEntityIndex` rebuilds the id map from the live tree (bulk deserialise bypassed `createEntity`).
7. Result struct returns success + entity count + warning count.

**Cold scene-save (`SceneSerializer::saveScene`):**

1. `buildSceneJson` walks the live tree; `EntitySerializer::serializeEntity` iterates every entry in `ComponentSerializerRegistry` and asks each "do you own one of me?" via `trySerialize`. Order is registration order (so files diff cleanly).
2. `nlohmann::json::dump(4)` pretty-prints with 4-space indent.
3. `AtomicWrite::writeFile` writes via temp-file + `fsync` + `rename` + `fsync(dir)` — crash-safe (the old file remains intact if power is lost mid-write).
4. On save failure (disk full, permission), the JSON dump is discarded; the in-memory scene is unchanged.

**Exception path:** scene operations never throw in steady state. JSON parse errors become string error messages on the `SceneSerializerResult`. A `Component::clone()` that recursively allocates can propagate `std::bad_alloc` — treated as fatal.

## 6. CPU / GPU placement

| Workload | Placement | Reason |
|----------|-----------|--------|
| Scene tree walk, component updates, world-matrix cache | CPU (main thread) | Sparse / branching / per-entity decisions — exactly the CODING_STANDARDS §17 default for CPU work. |
| `SceneRenderData` collection (recursive walk + push_back) | CPU (main thread) | Sparse; must complete before the renderer dispatches; AABB tests are O(entities) which stays small in demo scenes. |
| `collectColliders` | CPU (main thread) | Same reasoning; consumed by the FirstPersonController collision path which is itself CPU. |
| `ParticleEmitterComponent::update` (CPU SoA sim, swap-on-death) | CPU (main thread) | Default fallback path; ≤ 1000 particles per emitter in typical scenes. |
| `GPUParticleEmitter::update` — emission timing + dispatch | CPU (main thread) | Branching / dispatch-decision is CPU; the physics-of-particles compute lives in `engine/renderer/gpu_particle_system.h`. |
| `GPUParticleEmitter` particle physics + collision (compute) | GPU (compute shader) | Per-particle data-parallel; up to 100 000 particles where the CPU path stalls. |
| `WaterSurfaceComponent::buildMesh` (grid VAO upload) | CPU (one-shot per resize) → GPU (lifetime) | Grid topology generation is CPU; the VAO/VBO/EBO live on the GPU until the entity dies. |
| Camera matrices (`CameraComponent::getViewMatrix`, `getProjectionMatrix`) | CPU (main thread) | A handful of `glm` calls per frame — branching the projection-type is CPU work. |
| Camera-mode `computeOutput` | CPU (main thread) | Pure function of inputs; explicitly designed for CPU testability per `camera_mode.h:14`. |
| `TilemapComponent::forEachVisibleTile` (renderer-driven walk) | CPU (main thread) | Dispatch-decision; the per-tile draw is GPU but originates from the sprite system. |
| Per-2D-body / collider authoring | CPU (main thread, scene-load + edit) | Pure data; the actual broadphase + narrowphase runs on the CPU inside Jolt (see `engine/physics/spec.md`). |

No dual implementations. The CPU vs GPU particle path is a runtime-selectable optimization (different *components*, not the same component running in two places), so no parity test is needed.

## 7. Threading model

Per CODING_STANDARDS section 13.

| Caller thread | Allowed APIs | Locks held |
|---------------|--------------|------------|
| Main thread | All of `Scene`, `SceneManager`, `Entity`, every `Component` subclass. | None — main thread is single-threaded by contract. |
| Worker threads | None — call into `engine/scene` is **undefined**. | n/a |

`engine/scene` is **main-thread-only**. The mutation queue is unsynchronized; the entity-id map is `std::unordered_map` (no atomic guarantees); the active-camera pointer is a plain `CameraComponent*`. Worker threads that need scene data take a snapshot on the main thread and read the snapshot.

The deferred-mutation contract solves *re-entrancy* (a script's `OnUpdate` mutating the tree the walk is iterating), not *concurrency*. Both effects mimic each other; the mechanism only addresses the first.

## 8. Performance budget

60 frames per second (FPS) hard requirement → 16.6 ms per frame. `engine/scene` produces the data the renderer + physics + audio consume; every millisecond it spends is one those subsystems lose.

Not yet measured — will be filled by Phase 11 audit; tracked as Open Q1 in §15.

Profiler markers / capture points: `engine/scene` does not emit `glPushDebugGroup` markers (no GPU passes). The `SystemRegistry` per-system timer covers `SceneManager::update`. Renderer-side, `engine/renderer` brackets the `collectRenderData` call site if needed for capture.

## 9. Memory

| Aspect | Value |
|--------|-------|
| Allocation pattern | Heap (`std::unique_ptr<Entity>` + `std::unique_ptr<Component>` everywhere). No arena, no per-frame transient allocator. `SceneRenderData` and the collider list are reused — capacity is preserved across frames per the capacity-preserving overload contract documented in the doc-comments immediately above `Scene::collectRenderData` and `Scene::collectColliders` declarations in `engine/scene/scene.h`. |
| Reusable per-frame buffers | The renderer / engine owns a `SceneRenderData` and `std::vector<AABB>` it passes to `collect*` so per-frame heap traffic is zero in steady state. |
| Peak working set | Demo scenes: low single-digit MiB total (a Tabernacle scene with ~200 entities, each ≤ 4 components, plus typical tilemap layers, fits comfortably). Particle SoA is `maxParticles × ~80 bytes` per emitter (CPU path); the GPU path lives in shader-storage buffers in `engine/renderer/`. Tilemap layers are `width × height × 2 bytes` (Tile Identifier (TileID) is 16-bit). Architectural-walkthrough scenes scale to tens of thousands of entities; this is the dominant memory consumer in practice. |
| Ownership | `SceneManager` owns scenes via `std::unique_ptr<Scene>`. `Scene` owns the root `Entity`. Each `Entity` owns its children + components. `MeshRenderer` holds `std::shared_ptr<Mesh>` / `Material` (mesh / material are renderer-owned, scene-shared). |
| Lifetimes | Scene-load duration. Components live until their entity is removed. The mutation queue is per-frame transient — non-empty only between `beginUpdate` and `drainPendingMutations`. |

No `new` / `delete` in feature code (CODING_STANDARDS §12); the GPU-owning components allocate OpenGL handles in their constructor / `init` and release in their destructor (RAII).

## 10. Error handling

Per CODING_STANDARDS §11 — no exceptions in steady-state hot paths.

| Failure mode | Reported as | Caller's recourse |
|--------------|-------------|-------------------|
| Scene-load JSON parse error | `SceneSerializerResult{success=false, errorMessage=...}` (the `nlohmann::json::parse` throw is caught by `openAndParseSceneJson` and converted to a string) | Editor surfaces the message; old scene stays loaded. |
| Scene-load size cap exceeded (> 256 MiB) | `SceneSerializerResult{success=false, errorMessage="Scene file exceeds 256 MiB cap..."}` | Reject the file. Cap defends against an OOM-kill from a malicious / corrupt scene (audit H4). |
| Scene-load JSON depth bomb (≥ 128 nested children) | `entity_serializer.cpp` rejects the document; `countJsonEntities` under-counts (Phase 10.9 Slice 5 D7) | Editor surfaces the deserialise failure. |
| Missing component type on load (forward-compat) | `Logger::warning` + skip; rest of the entity loads | Designer-time decision — install a build with the missing system, or drop the component on save. |
| Missing `format_version` in envelope | `SceneSerializerResult{success=false}` (`format_version == 0` is the sentinel) | Treat as malformed; prompt user. |
| `format_version` newer than `CURRENT_FORMAT_VERSION` | `migrateScene` returns false → `SceneSerializerResult{success=false}` | Reject; user must upgrade engine. |
| Save failure (disk full / permission) | `SceneSerializerResult{success=false, errorMessage="atomic-write: ..."}`. Atomic-write didn't commit; the previous file is intact. | Surface to user; in-memory scene stays dirty. |
| Entity-id collision after bulk deserialise | `Scene::rebuildEntityIndex` overwrites — the second insert wins; the first entity becomes orphan-by-id (still in the tree). Defended in practice because `Entity::s_nextId` is process-global and monotonic. | Programmer error; treat the file as malformed. (Open Q2.) |
| `Component::update` throws | Propagates — `engine/scene` does not wrap | **Policy: components must not throw from `update`.** Treat as programmer error; fix the component. |
| `Entity::clone()` `std::bad_alloc` | Propagates | App aborts (matches CODING_STANDARDS §11 — Out Of Memory (OOM) is treated as fatal). |
| Programmer error (null component pointer, wrong template type) | `assert` (debug) / Undefined Behavior (UB) (release) | Fix the caller. |

`Result<T,E>` / `std::expected` not yet used — `SceneSerializerResult` pre-dates the policy. Migration on the engine-wide debt list (Open Q2 in §15).

## 11. Testing

| Concern | Test file | Coverage |
|---------|-----------|----------|
| `Scene` create / find / clear / iterate / remove + active-camera null-out | `tests/test_scene.cpp` | Public API contract |
| `Scene` deferred mutation (spawn / destroy mid-`update`) | `tests/test_scene_deferred_mutation.cpp` | Phase 10.9 Slice 3 S2 contract |
| `Entity` hierarchy + transform + world-matrix + clone-by-component | `tests/test_entity.cpp` | Hierarchy + transform |
| `Entity` deep-clone fidelity per component | `tests/test_component_clone.cpp` | Phase 10.9 Slice 1 F2 contract |
| `EntitySerializer` round-trip | `tests/test_entity_serializer_registry.cpp` | Component-name dispatch (forward-compat) |
| `EntitySerializer` JSON depth cap | `tests/test_entity_serializer_depth_cap.cpp` | Stack-bomb defence (Phase 10.9 Slice 5 D7) |
| `SceneSerializer` save / load round-trip + atomic-write | `tests/test_scene_serializer.cpp` | Schema, version migration, missing-component warning |
| `CameraComponent` view / projection / sync | `tests/test_camera_component.cpp` | Public API |
| `Camera2DComponent` follow / spring / deadzone / bounds clamp | `tests/test_camera_2d.cpp` | Authored tuning |
| `CharacterController2DComponent` coyote / buffer / wall-slide | `tests/test_character_controller_2d.cpp` | Phase 9F-4 contract |
| `RigidBody2D` + `Collider2D` + `Physics2D` integration | `tests/test_physics2d_system.cpp` | 2D physics surface |
| `PressurePlateComponent` parented-plate centre helper | `tests/test_pressure_plate.cpp` | Phase 10.9 Slice 3 S6 fix |
| `ParticleEmitterComponent` SoA + photosensitive clamp | `tests/test_particle_data.cpp` | CPU sim correctness |
| `GPUParticleEmitter` compute-shader path | `tests/test_gpu_particle_system.cpp` | GPU sim correctness (skipped if no GL context) |
| `WaterSurfaceComponent` mesh rebuild + caustics defaults | `tests/test_water_surface.cpp` | Authored config |
| `SpriteComponent` animation-driven frame name | `tests/test_sprite_animation.cpp`, `tests/test_sprite_renderer.cpp` | Public API + render path |
| `SpriteAtlas` + sprite panel | `tests/test_sprite_atlas.cpp`, `tests/test_sprite_panel.cpp` | Atlas authoring |
| `TilemapComponent` layer + animated-tile walk | `tests/test_tilemap.cpp` | Public API |
| `game_templates_2d` factories | `tests/test_game_templates_2d.cpp` | Starter-scene shape |
| `AudioSourceComponent` (lives in `engine/audio` but participates in scene) | `tests/test_audio_source_component.cpp`, `tests/test_audio_source_state.cpp` | Component / scene wiring |

**Adding a test for `engine/scene`:** drop a new `tests/test_<thing>.cpp` next to its peers; link against `vestige_engine` + `gtest_main` in `tests/CMakeLists.txt` (auto-discovered via `gtest_discover_tests`). Use `Scene` / `Entity` directly without an `Engine` instance — every primitive in this subsystem **except `WaterSurfaceComponent` and `GPUParticleEmitter`** is unit-testable headlessly. Those two require a live OpenGL context (VAO + compute-shader allocation in their constructors) and exercise via `engine/testing/visual_test_runner.h`. Deterministic seeding for randomness in particle tests inlines a fixed seed (`engine/testing/random_helpers.h` if present).

**Coverage gap:** `EmissiveLightComponent`'s "auto-derive from material emissive" path is exercised end-to-end only by visual tests (the renderer reads the component to synthesize a `PointLight`). `GPUParticleEmitter` headless-mode (the `init() == false` fallback) is exercised by `test_gpu_particle_system.cpp` but the depth-collision path is GL-bound.

## 12. Accessibility

`engine/scene` produces no user-facing pixels or sound directly. **However**, it is on the route every scene-saved accessibility setting flows through, and several scene-stored fields are accessibility-relevant.

- **Photosensitive forwarding.** `Scene::collectRenderData` accepts `(bool photosensitiveEnabled, const PhotosensitiveLimits& limits)` and forwards to `ParticleEmitterComponent::getCoupledLight`, which clamps `flickerSpeed` through `clampStrobeHz(limits)`. The authored value in `ParticleEmitterConfig::flickerSpeed` is preserved verbatim — only the per-frame effective frequency is clamped, so disabling safe mode returns the emitter to its authored character. **Round-trip constraint:** the saved scene file persists the authored value; the photosensitive caps are owned by `Settings::accessibility.photosensitive` and are **not** baked into the scene. Loading a scene on a different machine respects that machine's caps.
- **Reduced-motion-relevant fields persisted in scenes:**
  - `Camera2DComponent::smoothTimeSec`, `maxSpeed`, `deadzoneHalfSize` — designer authors per-camera; the user's "reduce motion" toggle in `Settings::accessibility.reducedMotion` is read by camera-shake / motion-blur consumers in `engine/renderer/`, not by scene authoring.
  - `ParticleEmitterConfig::flickerSpeed` (see above) — clamp at draw time, not save time.
  - `WaterSurfaceConfig` waves (amplitude / wavelength / speed) — **not** clamped by reduced-motion; flagged as Open Q3.
- **Scene-saved-settings round-trip — accessibility-flag preservation.** Scene files (`*.scene`) **must not store any field whose authoritative source is `Settings::accessibility`**. Specifically:
  1. Photosensitive caps — owned by `Settings`, never serialised into a scene.
  2. Reduced-motion toggle — owned by `Settings`, never serialised into a scene.
  3. Subtitle / caption settings — owned by `Settings`, scene-side audio-source components reference caption *keys* (string ids) but not user-side font/size/colour preferences.
  4. Color-vision-filter mode — owned by `Settings`, scene materials store authored colours; the filter is applied at render time.
  5. User Interface (UI) scale — owned by `Settings`, never appears in `*.scene`.
  Round-trip rule: opening a scene file in the editor, saving it untouched, and reopening it on a machine with different `Settings::accessibility` MUST produce a byte-identical (`SceneSerializer::serializeToString`) file. A future audit checks this constraint by saving + diffing under different accessibility profiles. (Open Q4.)
- **Locked entities (`Entity::isLocked`)** are deliberately invisible to viewport-pick raycasts in the editor. This is keyboard-pick parity insurance — a partially-sighted user who navigates the hierarchy panel by keyboard rather than the viewport gets the same selection set.
- **`InteractableComponent::promptText`** is a string the UI subtitle / caption layer renders — not a translation key today (Open Q5). When the engine adds localisation, the prompt becomes a key + `caption_map.h` lookup.

For the Tabernacle / Solomon's Temple use case specifically, the scene-load path must be deterministic with respect to the user's accessibility profile. A guided tour built around captioned audio sources, with photosensitive-safe braziers and lamps, must produce the same authored content on every supported machine — only the runtime *effects* of accessibility flags vary.

## 13. Dependencies

| Dependency | Type | Why |
|------------|------|-----|
| `engine/animation/skeleton_animator.h`, `sprite_animation.h` | engine subsystem | `Scene` references skeletal animator state (bone matrices, morph weights) for `SceneRenderData::RenderItem`; `SpriteComponent` owns a `std::shared_ptr<SpriteAnimation>`. |
| `engine/renderer/camera.h`, `mesh.h`, `material.h`, `light.h` | engine subsystem | Camera bridge, mesh + material `shared_ptr` storage, light POD types embedded in components. |
| `engine/renderer/gpu_particle_system.h` | engine subsystem | `GPUParticleEmitter` owns a `std::unique_ptr<GPUParticleSystem>`. |
| `engine/utils/aabb.h` | engine subsystem | `MeshRenderer` collision + culling bounds. |
| `engine/accessibility/photosensitive_safety.h` | engine subsystem | `PhotosensitiveLimits` propagated through `Scene::collectRenderData`. |
| `engine/editor/widgets/animation_curve.h`, `color_gradient.h` | engine subsystem | `ParticleEmitterConfig` over-lifetime curves (uses editor types — they live in `engine/editor/widgets/` because the editor authors them, but they are pure data). |
| `engine/physics/cloth_component.h` (forward-decl) | engine subsystem | `SceneRenderData::ClothRenderItem` references `DynamicMesh`; the cloth component is owned by `engine/physics/`. |
| `engine/scripting/` (forward-decl) | engine subsystem | Script nodes drive the deferred-mutation queue. |
| `Jolt/Physics/Body/BodyID.h` | external (Jolt) | `RigidBody2DComponent::bodyId` storage. |
| `<glad/gl.h>` | external (OpenGL) | `WaterSurfaceComponent` + `GPUParticleEmitter` GPU handles. |
| `<glm/glm.hpp>`, `<glm/gtc/quaternion.hpp>` | external | Vector / matrix / quaternion math. |
| `<nlohmann/json_fwd.hpp>` | external (header-only) | Forward-declared in serializer interfaces; full `json.hpp` only included in `.cpp`. |
| `std::unordered_map`, `std::vector`, `std::unique_ptr`, `std::shared_ptr`, `std::function`, `std::optional`, `std::string`, `std::filesystem` | std | Core containers + smart pointers + file paths (the latter only in serializer signatures). |

**Direction:** `engine/scene` is depended on by `engine/renderer`, `engine/editor`, `engine/systems` (every domain system), `engine/physics` (cloth + character), `engine/audio` (`AudioSourceComponent` cooperates with scene transforms), `engine/scripting`, `engine/profiler`. `engine/scene` does **not** depend on `engine/renderer/renderer.h` (consumes `Camera`, `Mesh`, `Material`, `Light` types only) or `engine/editor/scene_serializer.h` (the serializer depends on `engine/scene`, not the other way around). Asset-path resolution for textures referenced by `MaterialDescriptor` is handled by the entity serializer (`sanitizeAssetPath` + the future `EnginePaths::assetRoot()` per CODING_STANDARDS §32) — `engine/scene` itself never opens a file.

## 14. References

Cited research / authoritative external sources (≥ 1 within the last 12 months):

- Sander Mertens. *ECS FAQ — Frequently Asked Questions about Entity Component Systems* (2024–2026, ongoing). <https://github.com/SanderMertens/ecs-faq> — informs the registry-of-components-on-entity middle ground we kept rather than archetype storage.
- Reactive ECS critique (DEV Community, 2025-Q4). *Reactive ECS: A Radical Alternative to Traditional ECS Architectures*. <https://dev.to/geseey/event-driven-ecs-a-system-that-outsmart-unity-bevy-and-entt-1mec> — current-2026 critique of pure-archetype ECS that informs the deferred-mutation contract here.
- Jordan Grilly. *Building an ECS — What is an ECS and Why Rust? A Deep Dive into Data-Oriented Game Engine Design* (2025). <https://medium.com/@jordangrilly/what-is-an-ecs-and-why-rust-a-deep-dive-into-data-oriented-game-engine-design-887680a5583a> — recent comparison of archetype vs sparse-set vs registry-of-components storage.
- ezEngine. *World / Scenegraph System overview*. <https://ezengine.net/pages/docs/runtime/world/world-overview.html> — hybrid scene-graph + per-component-system design we partially mirror (one `ISystem` per component family; scene tree under the hood; ECS-shaped user API).
- *Effectively Handle Versioning in C# Serialization: A Practical Guide for Developers* (2025-02). <https://coldfusion-example.blogspot.com/2025/02/effectively-handle-versioning-in-c.html> — current best-practice on JSON `format_version` + per-version migrators, which is exactly the shape `migrateScene` implements.
- *The Evolving Landscape of Data Formats: JSON, YAML, and the Rise of Specialized Standards in 2025* (DEV Community, 2025). <https://dev.to/dataformathub/the-evolving-landscape-of-data-formats-json-yaml-and-the-rise-of-specialized-standards-in-2025-3mp8> — chosen-format rationale (JSON over Protobuf / FlatBuffers for hand-edited authoring + diff-friendly source control).
- Confluent. *Schema Evolution and Compatibility for Schema Registry on Confluent Platform*. <https://docs.confluent.io/platform/current/schema-registry/fundamentals/schema-evolution.html> — backward / forward / full compatibility taxonomy applied to scene loads (we target FORWARD: a v2 engine reads a v1 scene; a v1 engine reading a v2 scene is rejected with a clear error per `migrateScene`).
- Unity Technologies. *Entity prefab instantiation workflow* (Entities 1.4). <https://docs.unity3d.com/Packages/com.unity.entities@1.4/manual/ecs-workflow-example-prefab-instantiation.html> — reference for prefab vs scene-instance separation; we ship the parallel via `PrefabSystem` (`engine/editor/prefab_system.h`) reusing the same `EntitySerializer`.
- skypjack. *EnTT — fast and reliable ECS for modern C++* (2025). <https://github.com/skypjack/entt> — reference implementation for type-indexed component lookup; informs `ComponentTypeId::get<T>()`.

Internal cross-references:

- `CODING_STANDARDS.md` §11 (errors), §12 (memory), §13 (threading), §17 (CPU/GPU), §18 (public API), §27 (units — metres, Y-up, right-handed; matches every `Transform::position` and every collider half-extent in this subsystem), §32 (asset paths — scene-referenced textures resolve through `EnginePaths` / asset-root canonicalization in the entity serializer, never raw filesystem strings in scene files), §33 (editor-runtime boundary — scene save is a serialisation event, not a recompute trigger).
- `ARCHITECTURE.md` §1–6 (subsystem map, scene graph, event bus).
- `CLAUDE.md` rules 1, 5, 7 (research-first, library currency, CPU/GPU placement).
- `docs/engine/core/spec.md` — `engine/core` is the upstream `ISystem` host that drives `SceneManager::update`.
- `docs/engine/renderer/spec.md` — downstream consumer of `SceneRenderData`.
- `docs/engine/physics/spec.md` — downstream consumer of `Collider2DComponent` / `RigidBody2DComponent`.

## 15. Open questions

| # | Question | Owner | Target |
|---|----------|-------|--------|
| 1 | Performance budgets in §8 are placeholders — need a Tracy capture across (a) demo scene, (b) Tabernacle scene, (c) 10 000-entity stress scene, with `collect*` and tree-walk costs split out. | milnet01 | Phase 11 audit (concrete: end of Phase 10.9 + Phase 11 entry) |
| 2 | Entity-id collision after bulk deserialise has no explicit guard — `Scene::rebuildEntityIndex` lets the second insert win. Should we (a) reassign colliding ids on load with a warning, (b) refuse to load with an error, or (c) treat as programmer error and assert? Currently (c) by default; (a) is friendliest. | milnet01 | Phase 11 entry |
| 3 | `WaterSurfaceConfig::waves` are not clamped under `reducedMotion`. Should the wave amplitude / speed be scaled at draw time the same way particle `flickerSpeed` is? Need a partially-sighted user-test pass before deciding. | milnet01 | Phase 11 accessibility audit |
| 4 | "No accessibility-owned field appears in `*.scene` files" is asserted in §12 but there is no automated test for it. Add a test that saves the same scene under three `Settings::accessibility` profiles and diffs the bytes. | milnet01 | Phase 11 entry |
| 5 | `InteractableComponent::promptText` and `PressurePlateComponent` callbacks are raw strings / `std::function` — no localisation key path yet. When `engine/audio/caption_map` becomes the canonical key store, cut these over. | milnet01 | post-MIT release (Phase 12) |
| 6 | `Result<T,E>` / `std::expected` adoption — `SceneSerializerResult` predates the policy. Migration on the broader engine-wide debt list, not scene-specific. | milnet01 | post-MIT release (Phase 12) |
| 7 | `Entity::transform.rotation` stores Euler degrees; quaternion variant on the roadmap requires a `format_version` bump and a migrator. Decide whether to ship Euler-and-quaternion side-by-side or hot-swap. | milnet01 | triage (no scheduled phase) |
| 8 | Terrain chunking (`project_terrain_chunking.md`) will likely add a frustum-cull early-out parameter to `Scene::collectRenderData`. Decide whether to ship that as an additive parameter or a new `collectVisibleRenderData` overload. | milnet01 | triage (depends on terrain-chunking phase) |

## 16. Spec change log

| Date | Spec version | Author | Change |
|------|--------------|--------|--------|
| 2026-04-28 | 1.0 | milnet01 | Initial spec — `engine/scene` covering Scene + SceneManager + Entity + Component + 21 component subclasses since Phase 3, formalised post-Phase 10.9 audit. |
