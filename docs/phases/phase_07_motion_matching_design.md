# Phase 7: Motion Matching System Design

## Overview

Motion matching replaces hand-authored animation state machines with continuous
pose searching. Instead of manually defining transitions between clips, the
system searches a preprocessed database of animation frames every ~0.1 seconds
to find the best match for the character's current pose and desired future
trajectory. The result is fluid, responsive animation without explicit
transition logic.

**References:**
- Simon Clavet (Ubisoft), GDC 2016 — "Motion Matching and The Road to Next-Gen Animation"
- Michael Büttner / Kristjan Zadziuk (Ubisoft), GDC 2016 — first AAA motion matching
- David Bollo (The Coalition), GDC 2018 — inertialization blending
- Daniel Holden (Ubisoft La Forge), SIGGRAPH 2020 — Learned Motion Matching
- Daniel Holden — spring-damper systems, dead blending, reference implementation

## Architecture

```
Player Input (gamepad/keyboard)
        │
        ▼
┌─────────────────────┐
│ TrajectoryPredictor  │ ← Spring-damper smoothing of desired velocity
│                      │   Predicts future positions at 3 sample points
└────────┬────────────┘
         │ desired trajectory
         ▼
┌─────────────────────┐     ┌──────────────────────┐
│   MotionMatcher     │────▶│   MotionDatabase     │
│   (runtime driver)  │◀────│   (feature matrix +  │
│                      │     │    KD-tree search)   │
└────────┬────────────┘     └──────────────────────┘
         │ best match (clip index + time)
         ▼
┌─────────────────────┐
│  Inertialization    │ ← Offset decay for smooth transitions
│  (post-process)     │   No double-evaluation of source animation
└────────┬────────────┘
         │ blended pose
         ▼
┌─────────────────────┐
│ SkeletonAnimator    │ ← Existing system — plays selected clip
│ (playback + IK)     │   IK foot placement as post-process
└─────────────────────┘
```

### Integration with Existing Systems

MotionMatcher acts as a **clip selection layer** on top of SkeletonAnimator.
It replaces AnimationStateMachine for locomotion but can coexist with it for
non-locomotion actions (combat, cutscenes). The existing crossfade system in
SkeletonAnimator is bypassed in favor of inertialization, which is more
efficient (single-evaluation vs double-evaluation).

## Feature Vector Design

### Schema (27 dimensions, configurable)

| Feature              | Floats | Space    | Purpose                    |
|----------------------|--------|----------|----------------------------|
| Left foot position   | 3      | Model    | Pose matching (foot plant) |
| Right foot position  | 3      | Model    | Pose matching (foot plant) |
| Left foot velocity   | 3      | Model    | Pose continuity            |
| Right foot velocity  | 3      | Model    | Pose continuity            |
| Hip velocity         | 3      | Model    | Root motion continuity     |
| Future traj pos ×3   | 6      | Root XZ  | Trajectory responsiveness  |
| Future traj dir ×3   | 6      | Root XZ  | Facing direction response  |
| **Total**            | **27** |          |                            |

Trajectory sample points at 0.33s, 0.67s, 1.0s into the future (at 30 Hz
database sampling = frames 10, 20, 30 ahead).

All features are in model-space (relative to root joint), making the system
invariant to world position and orientation.

### Normalization

Z-score normalization per feature dimension across the entire database:
```
normalized[d] = (raw[d] - mean[d]) / stddev[d]
```

This ensures each dimension contributes proportionally to the distance metric
regardless of its natural scale.

### Weights

Per-feature weights control pose-vs-trajectory importance:
- Higher trajectory weight → more responsive to input (snappier turns)
- Higher pose weight → smoother transitions (less foot sliding)
- Default: all weights 1.0, tuned per-project

## Cost Function

Weighted squared Euclidean distance:
```
cost(query, candidate) = Σ w[d] × (query[d] - candidate[d])²
```

Features are pre-scaled by √weight so standard Euclidean distance in scaled
space equals weighted distance in original space. This allows the KD-tree to
use unweighted distance internally.

## KD-Tree Acceleration

### Construction (offline)
- Split dimension = highest variance among remaining features
- Median split for balanced tree
- Leaf nodes contain ≤32 frames (tunable)
- Built from normalized, weight-scaled features

### Query (runtime)
- Standard nearest-neighbor search with backtracking
- Returns best match frame index and cost
- Typical speedup: 10-50× over brute force for >10K frames
- Falls back to brute force for small databases (<5K frames)

## Trajectory Prediction

### Spring-Damper System

Critically damped spring smooths player input into a continuous trajectory:

```cpp
// Exact solution (Holden's formulation)
float y = halflife_to_damping(halflife) / 2.0f;
float j0 = velocity - goalVelocity;
float j1 = acceleration + j0 * y;
float eydt = fast_negexp(y * dt);

position = eydt * ((-j1/(y*y)) + ((-j0 - j1*dt)/y))
         + (j1/(y*y)) + j0/y + goalVelocity*dt + position;
velocity = eydt * (j0 + j1*dt) + goalVelocity;
acceleration = eydt * (acceleration - j1*y*dt);
```

Where `halflife_to_damping(h) = (4 × ln2) / (h + ε)`.

### Prediction
Future positions are computed analytically (no frame-by-frame simulation)
by evaluating the spring at future times t=0.33s, 0.67s, 1.0s.

### Input Processing
- Gamepad stick direction → desired velocity (camera-relative)
- Deadzone filtering (0.2 threshold)
- Speed mapping: stick magnitude × max speed (walk=1.75, run=4.0 m/s)
- Simulation halflife: 0.27s (smooth but responsive)

## Inertialization Blending

### Why Not Crossfade

Traditional crossfade evaluates both source and destination animations during
transitions, doubling the animation cost. Inertialization only evaluates the
destination and applies a decaying offset — halving transition cost.

### Algorithm

At transition time:
1. Record offset = source_pose - destination_pose (per bone)
2. Record velocity = source_velocity - destination_velocity (per bone)

Each frame during transition:
1. Decay offset using critically damped spring (halflife = 0.1s)
2. Add decayed offset to destination pose
3. When offset is negligible, transition is complete

```cpp
// Spring decay for position offset
float y = halflife_to_damping(0.1f) / 2.0f;
float j0 = initialOffset;
float j1 = initialVelocity + j0 * y;
float eydt = fast_negexp(y * elapsed);
float offset = eydt * (j0 + j1 * elapsed);
```

Quaternion rotations use the same principle applied to the rotation difference
(log-space offset with angular velocity decay).

## Motion Database

### Structure
```
MotionDatabase
├── Feature matrix: N×M floats (row-major, cache-friendly)
├── Normalization: mean[M], stddev[M]
├── Frame info: [clipIndex, clipTime, tags] per frame
├── Pose data: full skeleton pose per frame (for inertialization)
├── KD-tree: built from normalized features
└── Schema: feature configuration
```

### Memory Budget
- 5 min animation at 30 Hz = 9,000 frames
- 27 features × 4 bytes = 108 bytes/frame (features)
- ~256 bytes/frame (pose data, assuming 64 joints × 4 bytes compressed)
- Total: ~3.3 MB for 5 minutes
- With mirroring: ~6.6 MB

### Frame Tagging
Bitmask tags per frame for context-sensitive search:
```
TAG_LOCOMOTION  = 0x01
TAG_IDLE        = 0x02
TAG_TURNING     = 0x04
TAG_STOPPING    = 0x08
TAG_NO_ENTRY    = 0x80  // do not transition TO this frame
```

## Database Preprocessing

### Pipeline
1. Load animation clips
2. Sample at 30 Hz (configurable)
3. For each frame: extract feature vector using FeatureSchema
4. Optionally: generate mirrored frames
5. Compute per-feature mean and stddev
6. Normalize all features
7. Build KD-tree from normalized features
8. Serialize to binary format

### Animation Mirroring
Doubles the database without additional mocap:
- Swap left/right bone pairs (configurable mapping)
- Negate X-axis positions and velocities
- Negate X components of trajectory positions/directions
- Mirrored frames are added as additional searchable entries

## Runtime Flow (per frame)

```
1. TrajectoryPredictor.update(input, dt)
   → smooth desired velocity via spring damper
   → predict future trajectory at 3 sample points

2. If searchTimer >= 0.1s:
   a. Build query feature vector:
      - Current pose features from SkeletonAnimator
      - Predicted trajectory features from TrajectoryPredictor
   b. Normalize query using database mean/stddev
   c. Search database (KD-tree or brute force)
   d. If bestMatch differs from current playback by > threshold:
      - Record inertialization offsets
      - Switch SkeletonAnimator to bestMatch clip/time

3. Inertialization.update(dt)
   → decay pose offsets on destination animation
   → apply to SkeletonAnimator output
```

## API Design

### FeatureSchema
```cpp
struct BoneFeature {
    int boneIndex;
    bool position;     // 3 floats
    bool velocity;     // 3 floats
    float weight;
};
struct TrajectorySample {
    float timeOffset;  // seconds into future
    float posWeight;   // XZ position weight
    float dirWeight;   // XZ direction weight
};
class FeatureSchema {
    void addBoneFeature(const BoneFeature& bf);
    void addTrajectorySample(const TrajectorySample& ts);
    int getDimensionCount() const;
    static FeatureSchema createDefault();  // 27-dim standard
};
```

### MotionDatabase
```cpp
class MotionDatabase {
    void build(const FeatureSchema& schema,
               const std::vector<AnimClipEntry>& clips,
               const Skeleton& skeleton,
               const MirrorConfig* mirror = nullptr);
    SearchResult search(const float* query, uint32_t tagMask) const;
    void normalize(float* query) const;  // apply stored mean/stddev
    int getFrameCount() const;
    int getFeatureCount() const;
    const FrameInfo& getFrameInfo(int frameIndex) const;
    const float* getPoseData(int frameIndex) const;
};
```

### TrajectoryPredictor
```cpp
class TrajectoryPredictor {
    void update(const glm::vec2& inputDir, float inputSpeed,
                float cameraYaw, float dt);
    void predictTrajectory(glm::vec2* outPositions,
                           glm::vec2* outDirections,
                           const float* sampleTimes, int count) const;
    glm::vec2 getCurrentVelocity() const;
    glm::vec2 getCurrentPosition() const;
};
```

### Inertialization
```cpp
class Inertialization {
    void start(const std::vector<glm::vec3>& srcPositions,
               const std::vector<glm::quat>& srcRotations,
               const std::vector<glm::vec3>& srcVelocities,
               const std::vector<glm::vec3>& dstPositions,
               const std::vector<glm::quat>& dstRotations,
               const std::vector<glm::vec3>& dstVelocities,
               float halflife);
    void update(float dt);
    void apply(std::vector<glm::vec3>& positions,
               std::vector<glm::quat>& rotations) const;
    bool isActive() const;
};
```

### MotionMatcher
```cpp
class MotionMatcher {
    void setDatabase(std::shared_ptr<MotionDatabase> db);
    void setSchema(const FeatureSchema& schema);
    void setAnimator(SkeletonAnimator* animator);
    void update(const glm::vec2& inputDir, float inputSpeed,
                float cameraYaw, float dt);
    // Tuning
    void setSearchInterval(float seconds);     // default 0.1
    void setTransitionCost(float threshold);   // default 0.02
    void setInertializationHalflife(float hl); // default 0.1
    void setTrajectoryHalflife(float hl);      // default 0.27
    // Debug
    float getLastSearchCost() const;
    int getLastMatchFrame() const;
    float getSearchTimeMs() const;
};
```

## Performance Targets

| Metric              | Target           |
|---------------------|------------------|
| Search (10K frames) | < 50 µs          |
| Feature extraction  | < 10 µs          |
| Inertialization     | < 5 µs           |
| Total per character | < 15 µs/frame    |
| Memory (5 min data) | < 7 MB           |
| Database build      | < 2 seconds      |

All targets maintain 60 FPS with dozens of characters.

## File Plan

| File | Purpose |
|------|---------|
| `animation/feature_vector.h/.cpp` | FeatureSchema, feature extraction, normalization |
| `animation/kd_tree.h/.cpp` | KD-tree nearest-neighbor search |
| `animation/motion_database.h/.cpp` | Database storage, build, search |
| `animation/trajectory_predictor.h/.cpp` | Spring-damper input processing |
| `animation/inertialization.h/.cpp` | Offset-decay transition blending |
| `animation/motion_matcher.h/.cpp` | Runtime driver component |
| `animation/motion_preprocessor.h/.cpp` | Offline feature extraction pipeline |
| `animation/mirror_generator.h/.cpp` | Animation mirroring |
| `tests/test_motion_matching.cpp` | Comprehensive unit tests |

## Testing Strategy

Unit tests cover:
- Feature extraction from known poses
- Z-score normalization correctness
- KD-tree construction and query accuracy vs brute force
- Database search returns expected matches
- Trajectory predictor spring convergence
- Inertialization offset decay to near-zero
- Mirror generator produces valid mirrored poses
- MotionMatcher integration with mock data
