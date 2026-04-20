# Phase 9F — 2D Game Support (Design)

**Status:** draft, 2026-04-20
**Depends on:** Phase 9C (SystemRegistry / ISystem), Phase 9D (game templates plumbing),
Phase 9E (visual scripting nodes so 2D templates can fire logic).
**Unblocks:** Phase 18 (2D Game and Scene Support — the long-tail polish).

## 1. Goal and non-goals

**Goal.** Make Vestige able to ship a 2D game without leaving the editor. A
designer can drop a sprite entity, wire it to a 2D physics body, paint a
tilemap as a background, pick a 2D camera that follows the player, and run
the scene in play mode. The engine keeps running alongside 3D — 2D is a
first-class rendering mode, not a fork.

**Non-goals (deferred to Phase 18).**
- Advanced auto-tiling beyond simple 4-way / 8-way neighbour rules.
- Spine / Dragon Bones skeletal 2D animation (sprite-sheet frame anim is in).
- 9-slice scaling for UI (the UI system already has its own quad path;
  this phase keeps them separate).
- Lighting with 2D shadow casters.
- Parallax layer system beyond `orderInLayer` + `z` depth.

---

## 2. Research summary (cited)

Full research brief in the conversation log for 2026-04-20. Condensed decisions:

| Question | Decision | Anchor |
|---|---|---|
| Batching strategy | **Instance-rate VBO**: one affine transform + UV offset/scale + tint per sprite; ≤ 10k sprites/frame in one `glDrawArraysInstanced` per (texture, blend, layer) batch. | Bevy 0.12 sprite rewrite (cart, 2023); [learnopengl Instancing](https://learnopengl.com/Advanced-OpenGL/Instancing). The existing `engine/ui/sprite_batch_renderer.{h,cpp}` uses CPU-merged quads — that stays the UI path (mixed primitives) while `SpriteRenderer` is a new game-sprite path. |
| Atlas format | **TexturePacker JSON-Array** (array of frames). Offline packing; runtime just loads. MaxRects is the default packer. | [TexturePacker custom-exporter docs](https://www.codeandweb.com/texturepacker/documentation/custom-exporter). |
| Z-ordering | Stable CPU sort by (sortingLayer, orderInLayer, yFromBottom, creationId). Opaque + alpha-tested use depth buffer; **fully blended sprites** draw in a second back-to-front pass with depth test but no depth write. | Unity 2D Renderer; Godot `z_index`. |
| Animation data | **Aseprite JSON**: per-frame ms duration, named tags with direction (`forward` / `reverse` / `pingpong`). | [Aseprite JSON export](https://community.aseprite.org/t/configure-json-animation-export/1948). |
| 2D physics | **Jolt 5.2.0 (already vendored) with per-body Z-translation + XY-rotation locks**, not a second physics engine. `EAllowedDOFs::Plane2D` does exactly this and is the supported path in Jolt. | [Jolt docs — AllowedDOFs](https://jrouwe.github.io/JoltPhysics/class_body_creation_settings.html). |
| Pass insertion | After `Renderer::endFrame()` (post-tonemap LDR) and before `UISystem::renderUI()` (HUD) and ImGui editor overlay. This matches Unity URP's `AfterRenderingPostProcessing` injection. | LearnOpenGL 2D-game final thoughts; Unity URP 2D Renderer. |

---

## 3. Architecture

### 3.1 Subsystem layout

```
engine/
  renderer/
    sprite_renderer.{h,cpp}       NEW — instance-rate batched sprite pass
    sprite_atlas.{h,cpp}          NEW — TexturePacker JSON loader + frame lookup
    tilemap_renderer.{h,cpp}      NEW — instanced tile draw over an atlas
  animation/
    sprite_animation.{h,cpp}      NEW — frame sequence, direction, state machine
  scene/
    sprite_component.{h,cpp}      NEW — atlas handle + frame + tint + layer
    tilemap_component.{h,cpp}     NEW — layers × tile grid
    rigid_body_2d_component.{h,cpp}       NEW — Jolt body with Plane2D DOF
    collider_2d_component.{h,cpp}         NEW — box / circle / capsule / polygon / edge-chain
    character_controller_2d_component.{h,cpp}  NEW — platformer kinematic controller
    camera_2d_component.{h,cpp}   NEW — ortho smooth follow + deadzone + bounds
    game_templates_2d.{h,cpp}     NEW — Side-Scroller / Shmup presets
  systems/
    sprite_system.{h,cpp}         NEW — ISystem; owns SpriteRenderer, submits per frame
    physics2d_system.{h,cpp}      NEW — ISystem; steps Jolt in Plane2D mode
  editor/
    panels/
      sprite_panel.{h,cpp}        NEW — atlas importer + frame slicing
      tilemap_panel.{h,cpp}       NEW — brush + layer list + eraser
assets/
  shaders/
    sprite.vert.glsl              NEW
    sprite.frag.glsl              NEW
    tilemap.vert.glsl             NEW
    tilemap.frag.glsl             NEW
tests/
  test_sprite_renderer.cpp        NEW
  test_sprite_atlas.cpp           NEW
  test_sprite_animation.cpp       NEW
  test_physics2d_system.cpp       NEW
  test_rigid_body_2d.cpp          NEW
  test_tilemap.cpp                NEW
  test_character_controller_2d.cpp NEW
  test_camera_2d.cpp              NEW
  test_game_templates_2d.cpp      NEW
docs/
  PHASE9F_DESIGN.md               THIS FILE
```

### 3.2 Data flow per frame

```
Entity (Transform + SpriteComponent)
    │
    ├─ SpriteSystem::update()            advances SpriteAnimation state (dt)
    ├─ Physics2DSystem::fixedUpdate()    steps Jolt world, writes Transform
    │                                    back from RigidBody2DComponent
    └─ Engine::run()
        → Renderer::renderScene()        3D pass (unchanged)
        → Renderer::endFrame()           post-process, tonemap
        → SpriteSystem::render()         new — instanced sprite draw
        → UISystem::renderUI()           existing HUD
        → ImGui editor overlay
```

### 3.3 Why Jolt, not Box2D

Jolt is already vendored at `external/CMakeLists.txt:322-336`, already drives 3D
rigid bodies, cloth, and ragdolls. Adding Box2D would mean:

1. Second `World`-like object with its own step schedule.
2. Separate broadphase and collision event pipe.
3. Two physics debug draws.
4. Conflicting `float` precision conventions (Box2D is opinionated).

Jolt supports **per-body allowed degrees of freedom** via
`BodyCreationSettings::mAllowedDOFs = EAllowedDOFs::Plane2D`. That locks Z
translation and X/Y rotation, leaving translation in XY and rotation around Z
— exactly 2D. Broadphase, narrowphase, contacts, and continuous collision all
work unchanged. Performance is equivalent to Box2D for ≤ 10k bodies per the
Jolt author's own benchmarks.

**Trade-off accepted:** Jolt's API is slightly more ceremonial than Box2D's
for pure 2D. We hide that inside `RigidBody2DComponent` + `Physics2DSystem`.

### 3.4 Why a separate SpriteRenderer, not reuse SpriteBatchRenderer

`engine/ui/sprite_batch_renderer.{h,cpp}` is a 4-vertex-per-quad CPU-merged
batcher tailored for mixed UI primitives (text, panels, icons, tinted rects).
Game sprites want a different layout:

1. **Instance-rate attributes** (80 bytes/sprite vs 144 bytes with 4 verts).
2. **World-space transform**, not screen-space pixels.
3. **Layered z-ordering** with per-layer sort keys (UI has none).
4. **Atlas-indexed UV rects**, not a bound `GLuint`.

Keeping them separate means neither has to bend to the other's needs. They
do share `Shader` and `Texture` infrastructure.

---

## 4. API surface

### 4.1 SpriteComponent

```cpp
class SpriteComponent : public Component
{
public:
    SpriteAtlasHandle atlas;       // shared_ptr to loaded atlas
    std::string       frameName;   // resolved each frame via atlas->lookup
    glm::vec4         tint  = glm::vec4(1.0f);
    glm::vec2         pivot = glm::vec2(0.5f, 0.5f);  // 0..1 within frame
    bool              flipX = false;
    bool              flipY = false;
    int               sortingLayer  = 0;   // coarse
    int               orderInLayer  = 0;   // fine
    SpriteAnimationHandle animation;       // optional; drives frameName
};
```

### 4.2 SpriteAtlas

```cpp
class SpriteAtlas
{
public:
    static std::shared_ptr<SpriteAtlas> loadFromJson(
        const std::string& jsonPath, const std::string& texturePath);

    struct Frame
    {
        std::string name;
        glm::vec4   uv;       // (u0, v0, u1, v1) in atlas
        glm::vec2   sourceSize;  // pixels
        glm::vec2   pivot;       // default pivot if artist set one
    };

    const Frame* find(const std::string& name) const;
    std::vector<std::string> frameNames() const;
    GLuint textureId() const;
};
```

Loader reads the TexturePacker "array" JSON shape:
```json
{
  "frames": [
    { "filename":"idle_0", "frame":{"x":0,"y":0,"w":32,"h":32}, ... },
    ...
  ],
  "meta": { "size":{"w":256,"h":256}, "image":"char.png" }
}
```

### 4.3 SpriteAnimation

```cpp
class SpriteAnimation
{
public:
    enum class Direction { Forward, Reverse, PingPong };

    struct Frame { std::string name; float durationMs; };

    void addClip(const std::string& clipName,
                 std::vector<Frame> frames,
                 Direction dir = Direction::Forward,
                 bool loop = true);

    void play(const std::string& clipName);
    void stop();
    void tick(float deltaTime);   // advances m_elapsedMs, picks current frame

    const std::string& currentFrameName() const;
    bool isPlaying() const;
};
```

State machine lives *inside* the component — transitions are a list of
`{fromClip, toClip, condition}` with conditions taken from the gameplay
scripting layer's `Blackboard` (Phase 9E-2 infrastructure). Advanced state
charts are Phase 18.

### 4.4 RigidBody2DComponent

```cpp
enum class BodyType2D { Static, Kinematic, Dynamic };

class RigidBody2DComponent : public Component
{
public:
    BodyType2D  type       = BodyType2D::Dynamic;
    float       mass       = 1.0f;
    float       friction   = 0.5f;
    float       restitution= 0.0f;
    float       linearDamping = 0.0f;
    float       gravityScale = 1.0f;
    bool        fixedRotation = false;  // Z-rot lock on top of Plane2D

    // Runtime (set by Physics2DSystem)
    JPH::BodyID bodyId;
    glm::vec2   linearVelocity;
    float       angularVelocity;   // around Z
};
```

### 4.5 Collider2DComponent

```cpp
enum class ColliderShape2D { Box, Circle, Capsule, Polygon, EdgeChain };

class Collider2DComponent : public Component
{
public:
    ColliderShape2D shape = ColliderShape2D::Box;

    // Shape-specific — union would be neater but members are cheap
    glm::vec2              halfExtents;    // Box
    float                  radius;         // Circle, Capsule
    float                  height;         // Capsule
    std::vector<glm::vec2> vertices;       // Polygon (CCW)
    std::vector<glm::vec2> chain;          // EdgeChain (open line, N-1 edges)

    bool     isSensor = false;
    uint32_t categoryBits = 0x0001;
    uint32_t maskBits     = 0xFFFF;
};
```

### 4.6 CharacterController2DComponent

```cpp
class CharacterController2DComponent : public Component
{
public:
    // Movement tuning
    float maxSpeed = 8.0f;
    float acceleration = 40.0f;
    float airAcceleration = 20.0f;
    float jumpVelocity = 12.0f;
    float coyoteTimeSec = 0.12f;
    float jumpBufferSec = 0.10f;
    float wallSlideSpeed = 2.0f;

    // Runtime state (Physics2DSystem writes)
    bool  onGround = false;
    bool  onWall   = false;
    float timeSinceGrounded = 999.0f;
    float jumpBufferRemaining = 0.0f;
};
```

### 4.7 Camera2DComponent

```cpp
class Camera2DComponent : public Component
{
public:
    float zoom = 1.0f;
    glm::vec2 followOffset = {0, 2};
    glm::vec2 deadzoneHalfSize = {2, 1};    // world units
    bool  clampToBounds = false;
    glm::vec4 worldBounds = {-100,-100,100,100};  // (minX,minY,maxX,maxY)
    float smoothTimeSec = 0.2f;            // critically-damped spring
};
```

### 4.8 Physics2DSystem

Owns a `JPH::PhysicsSystem` dedicated to 2D bodies. Shares the broadphase
interface with the 3D one (they are compatible types in Jolt) but runs its
own step schedule so 2D has a fixed timestep independent of the 3D world.
Emits `CollisionEnter2D`, `CollisionExit2D`, `TriggerEnter2D`, `TriggerExit2D`
events on the shared EventBus — the Phase 9E-2 `OnCollisionEnter` /
`OnTriggerEnter` script nodes subscribe to these.

### 4.9 SpriteSystem

Gathers all `SpriteComponent`s into a render list once per frame, sorts by
`(sortingLayer, orderInLayer, -yFromBottom, creationId)`, packs per-sprite
instance data into a VBO, flushes by atlas texture. Frame budget:
`1.5 ms` on the 60 FPS budget (ample for 10k sprites).

---

## 5. Rendering passes

### 5.1 Shader — sprite.vert.glsl

```glsl
#version 450 core
layout(location = 0) in vec2 aCornerUv;          // {0,0},{1,0},{1,1},{0,1}
layout(location = 1) in vec4 aTransformRow0;     // mat3 row 0 (a, b, tx)
layout(location = 2) in vec4 aTransformRow1;     // mat3 row 1 (c, d, ty)
layout(location = 3) in vec4 aUvRect;            // (u0, v0, u1, v1)
layout(location = 4) in vec4 aTint;
layout(location = 5) in float aDepth;            // derived from sort key

out vec2 vUV;
out vec4 vTint;

uniform mat4 uViewProj;

void main()
{
    mat3 model = mat3(vec3(aTransformRow0.xyz),
                      vec3(aTransformRow1.xyz),
                      vec3(0, 0, 1));
    vec3 world = model * vec3(aCornerUv - 0.5, 1.0);
    gl_Position = uViewProj * vec4(world.xy, aDepth, 1.0);
    vUV = mix(aUvRect.xy, aUvRect.zw, aCornerUv);
    vTint = aTint;
}
```

Instance divisor 1 on locations 1..5.

### 5.2 Pass order

```
Engine::run() per frame:
  1. renderer.renderScene(...)        3D opaque + transparent
  2. renderer.endFrame(dt)            post, tonemap, write to main FB
  3. spriteSystem.render(viewProj)    NEW — sprites in main FB (LDR)
  4. uiSystem.renderUI(w, h)          HUD
  5. editor draw / blit to screen
```

### 5.3 Depth and blending

- `glDepthFunc(GL_LEQUAL)`, depth test on, depth write **on for opaque
  and alpha-tested**.
- Second pass for fully-transparent sprites: depth test on, depth write
  **off**, sorted back-to-front within each (layer, orderInLayer).
- Blend: `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA` for transparent;
  opaque pass is blendless.

---

## 6. Sub-phase plan

| Step | Scope | Ships with |
|---|---|---|
| **9F-1** | Sprite renderer + atlas + animation + component + system + shaders + unit tests | This phase's rendering foundation |
| **9F-2** | Physics2DSystem + RigidBody2DComponent + Collider2DComponent + unit tests | Jolt 2D config via Plane2D DOF |
| **9F-3** | Tilemap component + tilemap renderer + unit tests | Instanced tile draw + multi-layer |
| **9F-4** | Camera2DComponent + CharacterController2DComponent + unit tests | Smooth follow + platformer controller |
| **9F-5** | Side-Scroller + Shmup templates + game templates registry wiring | Designer can instantiate |
| **9F-6** | Editor Sprite panel + Tilemap panel + 2D/3D viewport toggle | Full editor story |

Each sub-phase is its own commit with tests and a CHANGELOG entry. The
audit is run after 9F-6 lands.

---

## 7. Test plan

| Feature | Test |
|---|---|
| Atlas JSON load | Good file → all frames resolvable. Malformed file → clean error, no crash. |
| Atlas UV lookup | `find("idle_0")` returns expected pixel rect normalised correctly. |
| Sprite animation timing | `tick(100ms)` on a 50ms-per-frame clip advances by 2 frames; PingPong reverses at end. |
| Sprite sort order | Given 3 sprites with differing (layer, order, y), returned render list is in the expected sequence. |
| Sprite instance packing | N sprites → `sizeof(InstanceData) * N` bytes uploaded; corner verts constant. |
| Physics2D: gravity | Dynamic body with gravityScale=1 falls 4.9m in 1s (±0.01). |
| Physics2D: Plane2D lock | After 5s of simulation with side-impulse, Z position == initial Z; X/Y rotation == 0. |
| Physics2D: collision | Two dynamic boxes spawned overlapping; after 0.1s they separate (contact resolution works). |
| Physics2D: trigger event | Trigger body + dynamic body → `TriggerEnter2D` event fires once. |
| Character controller: coyote | Walk off ledge, `timeSinceGrounded < coyoteTimeSec` → jump still registers. |
| Character controller: jump buffer | Jump 50ms before landing → jump fires on touchdown. |
| Tilemap: tile lookup | 10×10 map, set(3,4)=7 → get(3,4)==7; get(-1,0)==0. |
| Tilemap: animated tiles | Tile advancement after 500ms increments frame per its rate. |
| Camera2D: deadzone | Target moves within deadzone → camera does not move; moves outside → camera follows. |
| Camera2D: bounds clamp | Target near world edge → camera clamps to keep view inside bounds. |
| Templates: Side-Scroller | Template instantiates into a Scene with expected entity names and components. |
| Templates: Shmup | Same. |

All tests use the existing GoogleTest infrastructure; no test needs a real
window (rendering tests validate CPU-side state, actual draw calls are a
separate smoke test in the workbench executable).

---

## 8. Performance budget

- SpriteSystem::render — ≤ 1.5 ms for 10k sprites (sort + pack + upload + draw).
- Physics2DSystem::fixedUpdate — ≤ 2.0 ms for 1000 bodies.
- TilemapRenderer::render — ≤ 1.0 ms for a 256×256 visible window.

These fit inside the 16.6 ms 60 FPS budget alongside the 3D pipeline.

---

## 9. Integration with existing systems

| System | Touch point |
|---|---|
| `SystemRegistry` | `SpriteSystem` and `Physics2DSystem` register in `Engine::initialize()` alongside `UISystem`. |
| `SceneRenderData` | Unchanged; `SpriteSystem` renders directly (not via render-data submission) because sprite passes are not camera-dependent in the same way as 3D. |
| `EventBus` | `Physics2DSystem` emits `CollisionEnter2D` / `TriggerEnter2D`; Phase 9E-2 nodes subscribe. |
| `ScriptingSystem` | The `OnTriggerEnter` / `OnCollisionEnter` node stubs become live automatically. |
| `Editor` | New panels register through the existing `Editor::drawPanels` mechanism; viewport mode flag added to `EditorMode`. |
| `Camera` | 2D camera writes to the existing `Camera` in ortho mode. 3D camera is disabled in 2D viewport mode. |
| `Renderer::endFrame` | No change — sprite pass runs *after* `endFrame`. |

---

## 10. Open questions (resolved before each sub-phase)

- **Worldspace scale of 1 unit.** Pick once: 1 world unit = 1 metre (same as
  3D) with a `pixelsPerUnit` knob on `SpriteComponent` (default 100). Matches
  Unity's convention and keeps Jolt's metric-based physics consistent.
- **Per-sprite material overrides (normal maps, emission).** Skip in 9F;
  sprites are colour + tint only. Phase 18 can add multi-channel materials.
- **Runtime atlas packing.** Skip. Use offline packer output. An import
  panel in 9F-6 invokes a header-only MaxRects bin-packer on `*.png`
  folders when a designer has no external tool.
- **Z-range for sprites.** Use a linear mapping from sort-key to the depth
  range `[0.99, 0.999]` so all sprites sit just in front of cleared depth
  but behind UI. Leaves 3D undisturbed even when 2D/3D co-render.

---

## 11. References

- Bevy 0.12 sprite rewrite — [bevy.org/news/bevy-0-12](https://bevy.org/news/bevy-0-12/)
- LearnOpenGL Instancing — [learnopengl.com/Advanced-OpenGL/Instancing](https://learnopengl.com/Advanced-OpenGL/Instancing)
- TexturePacker JSON-Array — [codeandweb.com/texturepacker/documentation/custom-exporter](https://www.codeandweb.com/texturepacker/documentation/custom-exporter)
- Aseprite JSON export — [community.aseprite.org/t/configure-json-animation-export/1948](https://community.aseprite.org/t/configure-json-animation-export/1948)
- Unity 2D Sorting — [docs.unity3d.com/6000.3/Documentation/Manual/2d-renderer-sorting.html](https://docs.unity3d.com/6000.3/Documentation/Manual/2d-renderer-sorting.html)
- Godot 2D batching — [docs.godotengine.org/en/3.5/tutorials/performance/batching.html](https://docs.godotengine.org/en/3.5/tutorials/performance/batching.html)
- Jolt Physics EAllowedDOFs — [jrouwe.github.io/JoltPhysics/class_body_creation_settings.html](https://jrouwe.github.io/JoltPhysics/class_body_creation_settings.html)
- Joren — Modern Sprite Batch for Vulkan — [jorenjoestar.github.io/post/modern_sprite_batch/](https://jorenjoestar.github.io/post/modern_sprite_batch/)
- Stephano — OpenGL Bindless Textures (deferred) — [ktstephano.github.io/rendering/opengl/bindless](https://ktstephano.github.io/rendering/opengl/bindless)
