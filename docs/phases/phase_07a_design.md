# Phase 7A Design: Core Skeletal Animation

## Goal

Load and play a single glTF skeletal animation on a skinned mesh. This is the foundation that all later animation features (blending, IK, state machines) build on.

**Visual milestone:** Load `CesiumMan.glb` from the Khronos glTF sample models and see it walk in the scene.

---

## Architecture Overview

Phase 7A adds four new classes and modifies four existing systems:

```
New classes:
  Skeleton          — joint hierarchy + inverse bind matrices
  AnimationClip     — keyframe data (timestamps + values per channel)
  AnimationSampler  — evaluates a clip at a given time (interpolation)
  SkeletonAnimator  — component that drives playback + computes bone matrices

Modified systems:
  Vertex / Mesh     — add bone IDs + weights (attribute locations 10-11)
  GltfLoader        — parse skins, joints, weights, animations
  scene.vert.glsl   — GPU skinning (bone matrix SSBO)
  shadow_depth.vert — GPU skinning for shadow pass
  Renderer          — upload bone matrices, set uniforms
  SceneRenderData   — carry bone matrix data from scene to renderer
```

---

## 1. Vertex Format Extension

### Current Vertex (68 bytes)

| Location | Field     | Type  | Offset |
|----------|-----------|-------|--------|
| 0        | position  | vec3  | 0      |
| 1        | normal    | vec3  | 12     |
| 2        | color     | vec3  | 24     |
| 3        | texCoord  | vec2  | 36     |
| 4        | tangent   | vec3  | 44     |
| 5        | bitangent | vec3  | 56     |

### Extended Vertex (84 bytes)

| Location | Field       | Type  | Offset |
|----------|-------------|-------|--------|
| 0        | position    | vec3  | 0      |
| 1        | normal      | vec3  | 12     |
| 2        | color       | vec3  | 24     |
| 3        | texCoord    | vec2  | 36     |
| 4        | tangent     | vec3  | 44     |
| 5        | bitangent   | vec3  | 56     |
| **10**   | **boneIds** | **ivec4** | **68** |
| **11**   | **boneWeights** | **vec4** | **84** |

Total: **100 bytes** per vertex (was 68).

### Design Decisions

- **4 bones per vertex** — glTF specifies `JOINTS_0`/`WEIGHTS_0` as vec4. 4 influences is sufficient for nearly all models. If a model uses 8 (`JOINTS_1`/`WEIGHTS_1`), we skip the second set and renormalize weights.
- **Locations 10-11** — Locations 6-9 are already used by instanced model matrices. We skip to 10 to avoid conflicts.
- **ivec4 for bone IDs** — Joint indices are integers. GLSL requires `glVertexAttribIPointer` (not `glVertexAttribFormat` with `GL_INT`). We use `glVertexArrayAttribIFormat` (DSA equivalent).
- **Default values** — Static meshes get `boneIds = ivec4(0)` and `boneWeights = vec4(0)`. The shader checks `boneWeights` sum to decide whether to skin.
- **16 extra bytes per vertex** — For a 10K vertex model, that's 160KB extra. Negligible.

### Changes to `mesh.h`

```cpp
struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec3 tangent;
    glm::vec3 bitangent;
    glm::ivec4 boneIds = glm::ivec4(0);      // Joint indices (up to 4 per vertex)
    glm::vec4 boneWeights = glm::vec4(0.0f);  // Corresponding weights (sum to 1.0)
};
```

### Changes to `mesh.cpp` — `upload()`

Add two new attribute setup blocks after the existing 6 attributes:

```cpp
// Location 10: Bone IDs (ivec4) — integer attribute
glEnableVertexArrayAttrib(vao, 10);
glVertexArrayAttribIFormat(vao, 10, 4, GL_INT, offsetof(Vertex, boneIds));
glVertexArrayAttribBinding(vao, 10, 0);

// Location 11: Bone Weights (vec4)
glEnableVertexArrayAttrib(vao, 11);
glVertexArrayAttribFormat(vao, 11, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, boneWeights));
glVertexArrayAttribBinding(vao, 11, 0);
```

**Critical:** `glVertexArrayAttribIFormat` (with the `I`) must be used for integer attributes. Using `glVertexArrayAttribFormat` would silently convert to float, breaking bone index lookup. This is a common pitfall.

---

## 2. Skeleton Class

**File:** `engine/animation/skeleton.h`

```cpp
/// @brief A joint in a skeletal hierarchy.
struct Joint
{
    std::string name;
    int parentIndex = -1;                              // -1 = root joint
    glm::mat4 inverseBindMatrix = glm::mat4(1.0f);    // Transforms from mesh space to bone space
    glm::mat4 localTransform = glm::mat4(1.0f);       // Current local transform (bind pose)
};

/// @brief Skeleton — a hierarchy of joints loaded from a glTF skin.
class Skeleton
{
public:
    /// @brief Gets the joint array (read-only).
    const std::vector<Joint>& getJoints() const;

    /// @brief Gets the joint count.
    int getJointCount() const;

    /// @brief Finds a joint index by name. Returns -1 if not found.
    int findJoint(const std::string& name) const;

    // Data populated by GltfLoader
    std::vector<Joint> m_joints;
    std::vector<int> m_rootJoints;  // Indices of joints with parentIndex == -1
};
```

### How glTF Skins Map to Skeleton

A glTF `skin` object contains:
- `joints[]` — array of node indices that form the skeleton
- `inverseBindMatrices` — accessor pointing to mat4 array (one per joint)
- `skeleton` (optional) — root node

Each joint's `localTransform` is the glTF node's TRS (translation, rotation, scale) converted to a mat4. Parent-child relationships come from the node hierarchy.

### Joint Limit

**Maximum 128 joints per skeleton.** This is the SSBO approach limit we chose (see Section 6). Most humanoid models use 20-70 joints. The Khronos `BrainStem` test model has ~64.

---

## 3. AnimationClip Class

**File:** `engine/animation/animation_clip.h`

```cpp
/// @brief Interpolation method for keyframes.
enum class AnimInterpolation
{
    STEP,          // No interpolation — snap to nearest keyframe
    LINEAR,        // Linear interpolation (SLERP for quaternions)
    CUBICSPLINE    // Hermite spline with in/out tangents (Phase 7B)
};

/// @brief What property a channel animates.
enum class AnimTargetPath
{
    TRANSLATION,   // vec3
    ROTATION,      // quat (vec4 xyzw in glTF)
    SCALE          // vec3
};

/// @brief A single animation channel — targets one property of one joint.
struct AnimationChannel
{
    int jointIndex = -1;                        // Which joint this channel animates
    AnimTargetPath targetPath;                  // Which property (T, R, or S)
    AnimInterpolation interpolation;            // How to interpolate between keyframes

    std::vector<float> timestamps;              // Keyframe times in seconds
    std::vector<float> values;                  // Packed keyframe values:
                                                //   TRANSLATION/SCALE: 3 floats per key
                                                //   ROTATION: 4 floats per key (x,y,z,w)
};

/// @brief A named animation clip containing one or more channels.
class AnimationClip
{
public:
    /// @brief Gets the clip duration (max timestamp across all channels).
    float getDuration() const;

    /// @brief Gets the clip name.
    const std::string& getName() const;

    std::string m_name;
    std::vector<AnimationChannel> m_channels;
    float m_duration = 0.0f;  // Cached during load
};
```

### Storage Format

Keyframe values are stored as flat `float` arrays rather than `glm::vec3`/`glm::quat` to match the raw glTF accessor layout. This avoids a copy during parsing — we read directly from the glTF buffer into the float vector.

For CUBICSPLINE (Phase 7B), each keyframe has 3 values: in-tangent, value, out-tangent. The flat array triples in size. The same `values` vector handles this — the sampler knows the stride from the interpolation mode.

### Duration Calculation

During load, `m_duration` is set to the maximum timestamp across all channels:

```cpp
for (const auto& channel : m_channels)
{
    if (!channel.timestamps.empty())
    {
        m_duration = std::max(m_duration, channel.timestamps.back());
    }
}
```

---

## 4. AnimationSampler (Free Functions)

**File:** `engine/animation/animation_sampler.h`

Rather than a separate class, we provide free functions that evaluate channels at a given time. This keeps the API simple and stateless.

```cpp
/// @brief Samples a translation or scale channel at the given time.
/// @param channel The animation channel to sample.
/// @param time Current playback time in seconds.
/// @return Interpolated vec3 value.
glm::vec3 sampleVec3(const AnimationChannel& channel, float time);

/// @brief Samples a rotation channel at the given time.
/// @param channel The animation channel to sample.
/// @param time Current playback time in seconds.
/// @return Interpolated quaternion (normalized).
glm::quat sampleQuat(const AnimationChannel& channel, float time);
```

### Interpolation Logic

**STEP:** Return the value at the keyframe with the largest timestamp <= `time`.

**LINEAR for vec3:** Standard `glm::mix(a, b, t)`.

**LINEAR for quat:** `glm::slerp(a, b, t)` — spherical linear interpolation to avoid deformation artifacts.

**Keyframe lookup:** Binary search (`std::upper_bound`) on timestamps to find the surrounding keyframes. Cache the last-found index per channel for temporal coherence (sequential playback rarely jumps backwards).

### Boundary Handling

- `time < timestamps[0]` → return first keyframe value (no extrapolation)
- `time >= timestamps[last]` → return last keyframe value
- Looping wraps time via `fmod(time, duration)` before sampling

---

## 5. SkeletonAnimator Component

**File:** `engine/animation/skeleton_animator.h`

```cpp
/// @brief Component that plays skeletal animations on an entity.
/// Attach to the same entity (or parent entity) that has MeshRenderer(s)
/// with skinned meshes.
class SkeletonAnimator : public Component
{
public:
    SkeletonAnimator();
    ~SkeletonAnimator() override;

    /// @brief Per-frame update — advances playback and recomputes bone matrices.
    void update(float deltaTime) override;

    /// @brief Deep copy for entity duplication.
    std::unique_ptr<Component> clone() const override;

    // --- Skeleton ---

    /// @brief Sets the skeleton (shared — multiple instances can share one skeleton).
    void setSkeleton(std::shared_ptr<Skeleton> skeleton);

    /// @brief Gets the skeleton.
    const std::shared_ptr<Skeleton>& getSkeleton() const;

    // --- Clip Management ---

    /// @brief Adds an animation clip (shared between instances).
    void addClip(std::shared_ptr<AnimationClip> clip);

    /// @brief Plays a clip by name. Restarts from the beginning.
    void play(const std::string& clipName);

    /// @brief Stops playback.
    void stop();

    /// @brief Pauses/unpauses.
    void setPaused(bool paused);

    /// @brief Sets looping.
    void setLooping(bool loop);

    /// @brief Sets playback speed multiplier (1.0 = normal, 0.5 = half speed).
    void setSpeed(float speed);

    // --- Output ---

    /// @brief Gets the final bone matrices for upload to GPU.
    /// These are: globalTransform * inverseBindMatrix for each joint.
    /// Returns empty span if no animation is active.
    const std::vector<glm::mat4>& getBoneMatrices() const;

    /// @brief Whether this animator has valid bone data to render.
    bool hasBones() const;

private:
    /// @brief Evaluates all channels of the current clip at the current time,
    /// then walks the joint hierarchy to compute global transforms.
    void computeBoneMatrices();

    std::shared_ptr<Skeleton> m_skeleton;
    std::vector<std::shared_ptr<AnimationClip>> m_clips;
    int m_activeClipIndex = -1;

    float m_currentTime = 0.0f;
    float m_speed = 1.0f;
    bool m_looping = true;
    bool m_paused = false;
    bool m_playing = false;

    // Per-joint local transforms (written by sampler each frame)
    std::vector<glm::vec3> m_localTranslations;
    std::vector<glm::quat> m_localRotations;
    std::vector<glm::vec3> m_localScales;

    // Final output: jointGlobalTransform * inverseBindMatrix
    std::vector<glm::mat4> m_boneMatrices;
};
```

### update() Logic

```
1. If not playing or paused, return early
2. Advance: m_currentTime += deltaTime * m_speed
3. If looping: m_currentTime = fmod(m_currentTime, clip.duration)
   Else if m_currentTime > clip.duration: stop playback
4. For each channel in the active clip:
     Sample the channel at m_currentTime → store in m_localTranslations/Rotations/Scales
5. Call computeBoneMatrices()
```

### computeBoneMatrices() Logic

```
For each joint (in order — parents always have lower indices than children in glTF):
    localMatrix = translate(m_localTranslations[i])
                * mat4_cast(m_localRotations[i])
                * scale(m_localScales[i])

    if joint.parentIndex >= 0:
        globalMatrix = parentGlobalMatrix * localMatrix
    else:
        globalMatrix = localMatrix

    m_boneMatrices[i] = globalMatrix * joint.inverseBindMatrix
```

**Parent-before-child ordering** is guaranteed by glTF spec for skin joints — the loader preserves this order. This means a single forward pass computes all global transforms without recursion.

### Component Lifetime

- `Skeleton` and `AnimationClip` are `shared_ptr` — multiple entity instances of the same model share the same skeleton/clip data (no duplication)
- `m_boneMatrices`, `m_localTranslations`, etc. are per-instance (each entity has its own playback state)
- `clone()` copies playback state and shares the skeleton/clip pointers

---

## 6. Bone Matrix Upload Strategy

### Decision: SSBO (Shader Storage Buffer Object)

| Strategy | Max Bones | Pros | Cons |
|----------|-----------|------|------|
| Uniform array | ~64 (256 mat4 limit) | Simple | Too few bones for complex models |
| UBO | ~256 (16KB / 64B) | Faster than uniforms | 16KB limit per block |
| **SSBO** | **Unlimited** | **No size limit, trivial API** | **Slightly slower than UBO** |
| Texture buffer | Unlimited | Wide support | Awkward API, texelFetch in shader |

SSBO is the right choice for Vestige:
- OpenGL 4.5 guarantees SSBO support
- AMD RX 6600 has no practical SSBO size limit
- 128 joints * 64 bytes (mat4) = 8KB — fits easily
- Same pattern already used for MDI model matrices (`layout(std430, binding = 0)`)
- One `glNamedBufferSubData` call per skinned mesh per frame

### GPU Buffer Management

The `Renderer` will own a single SSBO for bone matrices:

```cpp
// In renderer.h
GLuint m_boneMatrixSSBO = 0;       // Created once in init()
static constexpr int MAX_BONES = 128;

// In renderer.cpp init()
glCreateBuffers(1, &m_boneMatrixSSBO);
glNamedBufferStorage(m_boneMatrixSSBO, MAX_BONES * sizeof(glm::mat4),
                     nullptr, GL_DYNAMIC_STORAGE_BIT);
```

Before drawing a skinned mesh:
```cpp
glNamedBufferSubData(m_boneMatrixSSBO, 0,
                     boneMatrices.size() * sizeof(glm::mat4),
                     boneMatrices.data());
glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_boneMatrixSSBO);
// Binding 0 = MDI model matrices, 1 = reserved, 2 = bone matrices
```

### Binding Point Allocation

| Binding | Current Use | Phase 7A |
|---------|-------------|----------|
| 0 | MDI model matrices | unchanged |
| 1 | (unused) | unchanged |
| **2** | **(new)** | **Bone matrices** |

---

## 7. Vertex Shader Changes

### scene.vert.glsl

Add bone inputs and skinning logic:

```glsl
// Bone vertex attributes
layout(location = 10) in ivec4 boneIds;
layout(location = 11) in vec4 boneWeights;

// Bone matrix SSBO (binding 2)
layout(std430, binding = 2) buffer BoneMatrices
{
    mat4 u_boneMatrices[];
};

uniform bool u_hasBones;  // False for static meshes — skip bone math entirely
```

Skinning in `main()`:

```glsl
vec4 skinnedPos;
vec3 skinnedNormal;
vec3 skinnedTangent;
vec3 skinnedBitangent;

if (u_hasBones)
{
    mat4 boneTransform = boneWeights.x * u_boneMatrices[boneIds.x]
                       + boneWeights.y * u_boneMatrices[boneIds.y]
                       + boneWeights.z * u_boneMatrices[boneIds.z]
                       + boneWeights.w * u_boneMatrices[boneIds.w];

    skinnedPos     = boneTransform * vec4(position, 1.0);
    mat3 boneMat3  = mat3(boneTransform);
    skinnedNormal    = boneMat3 * normal;
    skinnedTangent   = boneMat3 * tangent;
    skinnedBitangent = boneMat3 * bitangent;
}
else
{
    skinnedPos       = vec4(position, 1.0);
    skinnedNormal    = normal;
    skinnedTangent   = tangent;
    skinnedBitangent = bitangent;
}

// Then use skinnedPos, skinnedNormal, etc. in place of position, normal, etc.
vec4 worldPosition = model * skinnedPos;
// ... TBN from skinnedNormal, skinnedTangent, skinnedBitangent
```

**Why `u_hasBones` uniform instead of checking weights?** A uniform branch is free on modern GPUs (entire warp takes the same path). Checking `boneWeights.x + ... > 0.0` per vertex adds a per-vertex cost for every static mesh draw — wasteful since 95% of draw calls have no bones.

### shadow_depth.vert.glsl

Same bone inputs and SSBO. Skinning transforms position only (no need for normals in shadow pass):

```glsl
layout(location = 10) in ivec4 boneIds;
layout(location = 11) in vec4 boneWeights;

layout(std430, binding = 2) buffer BoneMatrices
{
    mat4 u_boneMatrices[];
};

uniform bool u_hasBones;

void main()
{
    vec4 pos = vec4(position, 1.0);
    if (u_hasBones)
    {
        pos = (boneWeights.x * u_boneMatrices[boneIds.x]
             + boneWeights.y * u_boneMatrices[boneIds.y]
             + boneWeights.z * u_boneMatrices[boneIds.z]
             + boneWeights.w * u_boneMatrices[boneIds.w]) * pos;
    }

    // ... existing model/instancing/light space transform on pos
}
```

### id_buffer.vert.glsl

Same treatment — skinned position needed for correct entity picking.

### Other shaders

- `outline.vert.glsl` — If it reads position, needs bone support for correct outline on skinned meshes
- `material_preview.vert.glsl` — No, this renders a sphere preview
- Terrain, water, particle, foliage shaders — No, completely different vertex formats

---

## 8. GltfLoader Changes

### New Data in Model

```cpp
// In model.h
struct Model
{
    // ... existing fields ...

    // Skeletal animation data (empty for static models)
    std::shared_ptr<Skeleton> m_skeleton;
    std::vector<std::shared_ptr<AnimationClip>> m_animationClips;

    // Per-primitive: which joints from the skeleton affect this mesh
    // (for models with multiple skins — rare but possible)
    int m_skinIndex = -1;  // Index into glTF skins array, -1 = no skin
};
```

### Parsing Order

The glTF loader currently processes in this order:
1. Textures → Materials → Meshes (primitives) → Nodes → Hierarchy

Phase 7A adds after meshes, before node instantiation:
1. Textures → Materials → Meshes → **Skins → Animations** → Nodes → Hierarchy

### Parsing JOINTS_0 and WEIGHTS_0

In the primitive parsing loop, after TANGENT and COLOR_0:

```cpp
// JOINTS_0 — bone indices (unsigned byte or unsigned short in glTF)
auto jointsIt = primitive.attributes.find("JOINTS_0");
if (jointsIt != primitive.attributes.end())
{
    const auto& accessor = model.accessors[jointsIt->second];
    // Read as uvec4 (GL_UNSIGNED_BYTE or GL_UNSIGNED_SHORT)
    // Convert to ivec4 and store in vertex.boneIds
}

// WEIGHTS_0 — bone weights (float or normalized unsigned byte/short)
auto weightsIt = primitive.attributes.find("WEIGHTS_0");
if (weightsIt != primitive.attributes.end())
{
    const auto& accessor = model.accessors[weightsIt->second];
    // Read as vec4 (may need normalization from unsigned byte/short)
    // Store in vertex.boneWeights
    // Normalize so weights sum to 1.0 (glTF spec says they should, but verify)
}
```

**Component type handling:** glTF allows JOINTS_0 to be `UNSIGNED_BYTE` (5121) or `UNSIGNED_SHORT` (5123). WEIGHTS_0 can be `FLOAT` (5126), `UNSIGNED_BYTE` (normalized), or `UNSIGNED_SHORT` (normalized). The loader must handle all cases.

### Parsing Skins

```cpp
if (!gltfModel.skins.empty())
{
    const auto& skin = gltfModel.skins[0];  // Use first skin
    auto skeleton = std::make_shared<Skeleton>();

    // Read inverse bind matrices
    const auto& ibmAccessor = gltfModel.accessors[skin.inverseBindMatrices];
    // ... read mat4 array from accessor

    // Build joint hierarchy
    for (int i = 0; i < skin.joints.size(); i++)
    {
        int nodeIndex = skin.joints[i];
        const auto& node = gltfModel.nodes[nodeIndex];

        Joint joint;
        joint.name = node.name;
        joint.inverseBindMatrix = inverseBindMatrices[i];
        joint.localTransform = computeNodeMatrix(node);  // TRS → mat4

        // Find parent: walk skin.joints to see if this node's glTF parent
        // is also in the joints array
        int gltfParent = findParentNode(gltfModel, nodeIndex);
        joint.parentIndex = findInJointsList(skin.joints, gltfParent);

        skeleton->m_joints.push_back(joint);
    }

    outputModel->m_skeleton = skeleton;
}
```

### Parsing Animations

```cpp
for (const auto& anim : gltfModel.animations)
{
    auto clip = std::make_shared<AnimationClip>();
    clip->m_name = anim.name.empty() ? "Animation_" + std::to_string(clipIndex) : anim.name;

    for (const auto& channel : anim.channels)
    {
        const auto& sampler = anim.samplers[channel.sampler];

        AnimationChannel animChannel;

        // Map glTF node index → skeleton joint index
        animChannel.jointIndex = findInJointsList(skin.joints, channel.target_node);
        if (animChannel.jointIndex < 0) continue;  // Skip non-skeleton channels (Phase 7C)

        // Target path
        if (channel.target_path == "translation") animChannel.targetPath = AnimTargetPath::TRANSLATION;
        else if (channel.target_path == "rotation") animChannel.targetPath = AnimTargetPath::ROTATION;
        else if (channel.target_path == "scale")    animChannel.targetPath = AnimTargetPath::SCALE;
        else continue;  // Skip "weights" (morph targets — Phase 7E)

        // Interpolation
        if (sampler.interpolation == "STEP")        animChannel.interpolation = AnimInterpolation::STEP;
        else if (sampler.interpolation == "LINEAR") animChannel.interpolation = AnimInterpolation::LINEAR;
        else                                        animChannel.interpolation = AnimInterpolation::CUBICSPLINE;

        // Read timestamps from input accessor
        // Read values from output accessor
        // Store in animChannel.timestamps and animChannel.values

        clip->m_channels.push_back(std::move(animChannel));
    }

    clip->m_duration = /* max timestamp across all channels */;
    outputModel->m_animationClips.push_back(clip);
}
```

---

## 9. SceneRenderData Integration

### Option A: Store bone data in RenderItem (chosen)

Add an optional bone matrix pointer to `RenderItem`:

```cpp
struct RenderItem
{
    const Mesh* mesh;
    const Material* material;
    glm::mat4 worldMatrix;
    AABB worldBounds;
    uint32_t entityId = 0;
    bool castsShadow = true;
    bool isLocked = false;

    // Skeletal animation (nullptr for static meshes)
    const std::vector<glm::mat4>* boneMatrices = nullptr;
};
```

### Collection in collectRenderDataRecursive()

```cpp
// After checking for MeshRenderer, also check for SkeletonAnimator
auto* animator = entity.getComponent<SkeletonAnimator>();
if (meshRenderer && meshRenderer->isEnabled() && ...)
{
    RenderItem item;
    // ... existing setup ...

    if (animator && animator->isEnabled() && animator->hasBones())
    {
        item.boneMatrices = &animator->getBoneMatrices();
    }

    // ... add to renderItems/transparentItems
}
```

### Renderer Usage

In the draw loop, before each draw call:

```cpp
bool skinned = (item.boneMatrices != nullptr);
shader.setBool("u_hasBones", skinned);

if (skinned)
{
    glNamedBufferSubData(m_boneMatrixSSBO, 0,
                         item.boneMatrices->size() * sizeof(glm::mat4),
                         item.boneMatrices->data());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_boneMatrixSSBO);
}
```

---

## 10. Model Instantiation Changes

When `Model::instantiate()` creates entities, it needs to attach a `SkeletonAnimator` if the model has a skeleton:

```cpp
Entity* Model::instantiate(Scene& scene, Entity* parent, const std::string& name) const
{
    Entity* root = /* existing entity hierarchy creation */;

    // If this model has skeletal animation data, attach animator to root entity
    if (m_skeleton && !m_animationClips.empty())
    {
        auto* animator = root->addComponent<SkeletonAnimator>();
        animator->setSkeleton(m_skeleton);
        for (const auto& clip : m_animationClips)
        {
            animator->addClip(clip);
        }
        // Auto-play the first clip
        animator->play(m_animationClips[0]->getName());
    }

    return root;
}
```

### Entity Hierarchy for Skinned Models

In glTF, a skinned mesh's vertices are in the coordinate space of the skeleton root, not the mesh node. The skinning equation already applies the full joint → mesh transform via inverse bind matrices. This means:

- The mesh entity's world matrix positions the whole skinned model in the scene
- Bone matrices transform vertices relative to the mesh entity
- **We do NOT need per-bone entities** — the skeleton is internal to the SkeletonAnimator component

---

## 11. Bounding Box Update

Skinned meshes deform each frame, which means the static AABB from load time may be wrong. For Phase 7A, we use a simple conservative approach:

**Approach: Scale the bind-pose AABB by 1.5x**

```cpp
// In collectRenderDataRecursive, if skinned:
if (animator && animator->hasBones())
{
    // Expand AABB to account for animation deformation
    AABB expanded = meshRenderer->getLocalBounds();
    glm::vec3 center = expanded.center();
    glm::vec3 halfSize = expanded.extents() * 0.75f;  // 1.5x total size
    expanded = AABB(center - halfSize, center + halfSize);
    item.worldBounds = expanded.transformed(item.worldMatrix);
}
```

This prevents animated meshes from being frustum-culled incorrectly. A proper per-frame AABB recomputation (from bone positions) can be added in Phase 7B if needed.

---

## 12. File Structure

```
engine/animation/
    skeleton.h              — Joint, Skeleton
    skeleton.cpp
    animation_clip.h        — AnimInterpolation, AnimTargetPath, AnimationChannel, AnimationClip
    animation_clip.cpp
    animation_sampler.h     — sampleVec3(), sampleQuat()
    animation_sampler.cpp
    skeleton_animator.h     — SkeletonAnimator component
    skeleton_animator.cpp
```

All new files go in `engine/animation/`. This follows the existing pattern where each subsystem has its own directory (`engine/profiler/`, `engine/environment/`, etc.).

### CMake

Add the new source files to the engine target in `engine/CMakeLists.txt`:

```cmake
set(ENGINE_ANIMATION_SOURCES
    animation/skeleton.cpp
    animation/animation_clip.cpp
    animation/animation_sampler.cpp
    animation/skeleton_animator.cpp
)
```

---

## 13. Testing Strategy

### Unit Tests (no GL context)

**`tests/test_skeleton.cpp`:**
- Construct a 3-joint chain (root → child → grandchild)
- Verify parent indices
- Verify findJoint() by name

**`tests/test_animation_clip.cpp`:**
- Create a clip with 2 keyframes (t=0, t=1)
- Verify duration = 1.0
- Verify channel count

**`tests/test_animation_sampler.cpp`:**
- LINEAR vec3: sample at t=0 → first keyframe, t=0.5 → midpoint, t=1 → last keyframe
- LINEAR quat: sample at t=0.5 → SLERP result
- STEP: sample at t=0.5 → first keyframe value (not interpolated)
- Boundary: sample before first keyframe → first value
- Boundary: sample after last keyframe → last value

**`tests/test_skeleton_animator.cpp`:**
- Create animator with skeleton + single clip
- Call update(0.5) → verify bone matrices are non-identity
- Verify play/stop/pause state transitions
- Verify looping wraps time correctly
- Verify clone() preserves skeleton/clips but resets playback

### Visual Tests

- Load `RiggedSimple.glb` (2 bones) — minimal test case
- Load `CesiumMan.glb` (walk animation) — humanoid
- Load `Fox.glb` (multiple clips) — verify clip switching
- Verify shadows animate correctly (shadow_depth shader skinning)
- Verify entity picking works on skinned meshes (id_buffer shader skinning)

---

## 14. Performance Budget

| Operation | Cost (per skinned mesh, per frame) |
|-----------|-----------------------------------|
| Sample channels (~60 channels for 20-joint humanoid) | ~0.02ms CPU |
| Compute bone matrices (20 joints, TRS → mat4 + hierarchy walk) | ~0.005ms CPU |
| Upload bone matrices (20 * 64 bytes = 1.3KB SSBO update) | ~0.01ms CPU+GPU |
| GPU skinning (vertex shader, 4 mat4 fetches per vertex) | ~0.1ms GPU for 10K vertices |
| **Total per skinned mesh** | **~0.15ms** |

With 1-3 animated characters on screen, total animation cost is well under 1ms. This is well within the 60 FPS budget.

---

## 15. Implementation Order

1. **Vertex + Mesh** — Extend struct, add attribute locations 10-11
2. **Skeleton + AnimationClip** — Data structures only, with unit tests
3. **AnimationSampler** — Interpolation functions with unit tests
4. **GltfLoader** — Parse JOINTS_0, WEIGHTS_0, skins, animations
5. **SkeletonAnimator** — Component with update logic, unit tests
6. **Shader changes** — scene.vert.glsl, shadow_depth.vert.glsl, id_buffer.vert.glsl
7. **Renderer integration** — SSBO creation, bone upload, u_hasBones uniform
8. **SceneRenderData** — Add boneMatrices pointer, collection logic
9. **Model::instantiate()** — Attach SkeletonAnimator when model has skin
10. **Visual test** — Download CesiumMan.glb, load in engine, verify animation plays

---

## 16. Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Mesa AMD driver requires all declared samplers bound | `u_hasBones` uniform avoids accessing SSBO when false. SSBO binding 2 is always bound (empty buffer) to satisfy the driver. |
| glTF joint order not parent-before-child | Topological sort joints during skin parsing (should be rare — glTF spec recommends parent-first) |
| Vertex size increase (68→100 bytes) hurts static mesh perf | Negligible. Vertex fetch is rarely the bottleneck at our scene complexity. Monitor with GPU profiler. |
| Bone index out of bounds in shader | Clamp `MAX_BONES` to 128 in shader. Validate during load that no joint index exceeds this. |
| Models with multiple skins | Phase 7A supports only 1 skin per model (first skin). Multi-skin is rare and can be added later. |

---

## Not in Phase 7A (Deferred)

- CUBICSPLINE interpolation → Phase 7B
- Animation blending / crossfade → Phase 7B
- Animation state machine → Phase 7B
- Node TRS animation (doors, cameras) → Phase 7C
- Tween/easing system → Phase 7C
- IK (two-bone, foot, look-at) → Phase 7D
- Morph targets → Phase 7E
- Dual quaternion skinning → Phase 7E
- Compute shader skinning → Phase 7E
- Editor inspector for animation → After 7A works visually
