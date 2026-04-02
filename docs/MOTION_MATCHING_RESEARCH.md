# Motion Matching Research

## Overview

Motion matching is a data-driven animation technique that replaces hand-authored animation state machines with continuous pose searching. Instead of manually defining transitions between animation clips, the system continuously searches a database of animation frames to find the best match for the character's current pose and desired future trajectory. The result is fluid, responsive animation that can handle arbitrary locomotion patterns without explicit transition logic.

Games using motion matching include For Honor (Ubisoft, 2017), The Last of Us Part II (Naughty Dog, 2020), FIFA (EA), and Half-Life: Alyx (Valve, 2020). UE5 includes motion matching as a built-in feature via the PoseSearch plugin.

---

## 1. Core Algorithm

### Fundamental Idea

Every frame (or every few frames), the system:

1. Extracts a **feature vector** from the character's current pose and the desired future trajectory
2. Searches a preprocessed **motion database** for the frame whose feature vector is closest to the query
3. If the best match is sufficiently different from the currently playing frame, **transitions** to the new frame using inertialization blending
4. Otherwise, continues playing the current animation

The key insight (Simon Clavet, Ubisoft, GDC 2016): "Every frame, look at all mocap and jump to the best place." Rather than placing small animations in a big state machine structure, you place small structured markup on top of long animations. You capture 5-10 minutes of a performer running around and import it directly into the engine. At runtime, you continuously find the frame in the mocap database that simultaneously matches the current pose and the desired future plan.

### The Cost Function

The cost function evaluates each candidate frame by combining two components:

1. **Pose cost**: How well the candidate frame's pose matches the character's current pose (bone positions/velocities)
2. **Trajectory cost**: How well the animation following the candidate frame will move the character toward the desired future path

The cost returns zero when "the candidate matches the current situation and the piece of motion that follows brings us where we want." The frame with minimum cost is selected.

### Transition Decision

A transition occurs when the winning candidate is sufficiently different from the current playback position. Typical thresholds:
- If the best match is more than ~0.2 seconds away from the current frame, trigger a transition
- Blend time for transitions: approximately 0.2-0.3 seconds
- Search interval: typically every 0.1 seconds (10 times per second), not every single frame, to reduce CPU cost

---

## 2. Feature Vectors

### Composition

A feature vector is a compact numerical representation of a frame's ability to perform a task. It contains two categories of information:

**Pose features (quality/smoothness):**
- Left foot position (3 floats, relative to root)
- Right foot position (3 floats, relative to root)
- Left foot velocity (3 floats, relative to root)
- Right foot velocity (3 floats, relative to root)
- Hip/root velocity (3 floats)
- Optionally: weapon position (used in For Honor), body yaw rotation speed

**Trajectory features (input response):**
- Future trajectory positions at 3+ sample points (XZ plane only, relative to character = 2 floats each)
- Future trajectory facing directions at 3+ sample points (XZ plane only = 2 floats each)
- Optionally: past trajectory positions for history matching

### Typical Dimensionality

From Daniel Holden's reference implementation (orangeduck/Motion-Matching):
- **27 dimensions total**:
  - Foot positions: 2 feet x 3 = 6
  - Foot velocities: 2 feet x 3 = 6
  - Hip velocity: 3
  - Trajectory positions: 3 future points x 2 (XZ) = 6
  - Trajectory directions: 3 future points x 2 (XZ) = 6

O3DE's default schema uses **59 features**, which includes more joints and more trajectory samples. The dimensionality is configurable -- more features = better quality but slower search and more memory.

All features are computed in **model-space** (relative to the root joint), making the algorithm invariant to world position and orientation.

### Trajectory Sample Points

From O3DE's implementation:
- **4 past samples** across 0.7 seconds (for history matching)
- **6 future samples** across 1.2 seconds (for trajectory prediction)

From Daniel Holden's implementation:
- **3 future points** at 20-frame intervals (at 60 Hz, that's 0.33s, 0.67s, 1.0s into the future)

### Normalization

Features are normalized using standard z-score normalization to ensure each feature dimension contributes proportionally to the distance metric:

```
normalized = (feature - mean) / standard_deviation
```

Where mean and standard deviation are computed per-feature-dimension across the entire database during preprocessing. Without normalization, features with larger absolute values (e.g., positions measured in meters) would dominate over features with smaller values (e.g., velocities).

Denormalization (for extracting actual values):
```
feature = normalized * scale + offset
```

### Weighting

Beyond normalization, per-feature or per-channel weights control the relative importance of different feature types. For example, in UE5's PoseSearch, if the pose channel weight is 1 and the trajectory channel weight is 3, the system cares three times more about trajectory differences than pose differences.

Clavet's guidance: "Add more weight to matching the character's pose and velocity, the algorithm will then prefer choosing frames of animation which will result in smoother blending." In practice, weighting is tuned per-game to balance responsiveness (trajectory weight) versus smoothness (pose weight).

---

## 3. Cost Function and Distance Metric

### Weighted Squared Euclidean Distance

The cost function computes a weighted squared Euclidean distance between the query feature vector Q and each candidate feature vector F_i in the database:

```
cost(Q, F_i) = SUM over all dimensions d: w_d * (Q_d - F_i_d)^2
```

Where:
- `Q_d` is the d-th component of the query vector (normalized)
- `F_i_d` is the d-th component of the candidate vector (normalized)
- `w_d` is the weight for dimension d

Since features are already normalized, the weights serve as an additional tuning mechanism. Typical weight values start at 1.0 for all features, then are adjusted based on quality needs.

### Feature Cost Breakdown

The total cost decomposes as:

```
total_cost = pose_weight * pose_cost + trajectory_weight * trajectory_cost
```

Where:
```
pose_cost = SUM(w_feet_pos * ||feet_pos_query - feet_pos_candidate||^2 +
               w_feet_vel * ||feet_vel_query - feet_vel_candidate||^2 +
               w_hip_vel * ||hip_vel_query - hip_vel_candidate||^2)

trajectory_cost = SUM(w_traj_pos * ||traj_pos_query - traj_pos_candidate||^2 +
                      w_traj_dir * ||traj_dir_query - traj_dir_candidate||^2)
```

---

## 4. KD-Tree Search

### Two-Phase Search

Motion matching uses a two-phase search architecture:

**Broad phase (KD-tree):** A KD-tree partitions the feature space to rapidly eliminate most candidate frames. The tree is queried with the feature vector and returns a set of nearest-neighbor candidates. By adjusting:
- Maximum tree depth
- Minimum number of frames per leaf node

you control the tradeoff between candidate set size (visual quality) and search speed.

**Narrow phase:** The candidate set from the KD-tree is iterated through, computing the full cost function for each candidate. The frame with minimum cost is selected as the best match.

### KD-Tree Construction

The KD-tree is built offline during database preprocessing:
- Each leaf node contains a small cluster of frames
- The tree splits along the dimension with highest variance at each level
- Typical dimensionality: 27-60 dimensions

### Dimensionality Concerns

Standard KD-trees lose effectiveness in very high dimensions (the "curse of dimensionality"). For motion matching's typical 27-60 dimensions, KD-trees still provide significant speedup over brute force, though not as dramatic as in low-dimensional spaces. Alternatives for high dimensions include:
- Approximate nearest neighbor (ANN) techniques
- VP-trees (vantage point trees)
- Locality-sensitive hashing (LSH)
- PCA-based dimensionality reduction before search

### Weighted KD-Tree Distance

The distance function in the KD-tree must match the cost function's weights. When using weighted features, the KD-tree splits and searches use the weighted distance metric. Alternatively, features can be pre-scaled by the square root of their weights so that standard Euclidean distance in the scaled space equals the weighted distance in the original space.

### Brute Force Alternative

For smaller databases (a few minutes of animation), brute force search across all frames can be fast enough. Clavet's original presentation used brute force. With 10,000 frames and 27 dimensions, the search involves ~270,000 floating-point operations, which modern CPUs handle in microseconds with SIMD. The KD-tree becomes necessary as databases grow to tens of minutes or hours of animation.

---

## 5. Key GDC Talks and Papers

### Foundational

1. **Michael Buttner & Kristjan Zadziuk (Ubisoft Toronto, nucl.ai 2015 / GDC 2016)**
   - "Motion Matching, The Future of Games Animation... Today"
   - GDC Vault: https://www.gdcvault.com/play/1023478/
   - First public presentation of motion matching being implemented in a AAA game
   - Buttner presented the initial tech at nucl.ai 2015; Zadziuk presented the artist/gameplay integration at GDC 2016

2. **Simon Clavet (Ubisoft Montreal, GDC 2016)**
   - "Motion Matching and The Road to Next-Gen Animation"
   - GDC Vault: https://www.gdcvault.com/play/1023280/
   - Slides (PDF): https://ia800301.us.archive.org/29/items/GDC2016Clavet/GDC2016-Clavet.pdf
   - Full text: https://archive.org/details/GDC2016Clavet
   - Definitive technical talk on the algorithm for For Honor
   - Key details: brute force search, selective bone matching (feet + weapon), spring-damper trajectory, 0.25s blend windows

3. **David Bollo (The Coalition / Microsoft, GDC 2018)**
   - "Inertialization: High-Performance Animation Transitions in Gears of War"
   - GDC Vault: https://www.gdcvault.com/play/1025331/
   - Slides (PDF): https://media.gdcvault.com/gdc2018/presentations/bollo_david_inertialization_high_performance.pdf
   - Eliminated crossfade blending entirely in Gears of War 4 with post-process offset decay

### Advanced / Machine Learning

4. **Daniel Holden, Oussama Kanoun, Maksym Perepichka, Tiberiu Popa (Ubisoft La Forge, SIGGRAPH 2020)**
   - "Learned Motion Matching"
   - Paper: https://theorangeduck.com/media/uploads/other_stuff/Learned_Motion_Matching.pdf
   - ACM: https://dl.acm.org/doi/10.1145/3386569.3392440
   - Blog: https://www.ubisoft.com/en-us/studio/laforge/news/6xXL85Q3bF2vEj76xmnmIu/introducing-learned-motion-matching
   - Replaces the motion database + search with three neural networks (stepper, projector, decompressor) for 10-70x memory reduction

5. **Daniel Holden (The Orange Duck)**
   - "Spring-It-On: The Game Developer's Spring-Roll-Call": https://theorangeduck.com/page/spring-roll-call
   - "Dead Blending": https://theorangeduck.com/page/dead-blending
   - "Inertialization Transition Cost": https://theorangeduck.com/page/inertialization-transition-cost
   - Reference implementation: https://github.com/orangeduck/Motion-Matching
   - Essential practical resources with code, formulas, and detailed explanations

### Additional References

6. **O3DE Motion Matching Documentation**
   - https://docs.o3de.org/blog/posts/blog-motionmatching/
   - Detailed open-source implementation with KD-tree broad phase, feature schema, and debugging tools

7. **JLPM22 MotionMatching (Unity)**
   - https://github.com/JLPM22/MotionMatching
   - Open-source Unity implementation with documentation

---

## 6. Inertialization Blending

### The Problem with Crossfade Blending

Traditional animation transitions use crossfade blending: simultaneously evaluate both the old (source) animation and the new (destination) animation, then lerp between them over a blend duration. This has two problems:
1. **Double evaluation cost**: Both source and destination must be evaluated during the transition, effectively doubling the animation CPU cost
2. **Source animation must remain available**: The source animation state (with all its layers, blend trees, etc.) must be kept alive during the blend period

### How Inertialization Works

Inertialization (Bollo, GDC 2018) eliminates blended transitions entirely by handling motion transitions as a **post-process**:

1. **At transition time**: Record the difference (offset) between the source pose and the destination pose, including both position and velocity differences for each bone
2. **After transition**: Only evaluate the destination animation. Apply a decaying offset to the destination pose that smoothly reduces to zero over the blend duration
3. **Result**: The character smoothly transitions from the source pose to the destination without ever needing to evaluate the source animation again

### Offset Decay Methods

**Spring-based decay (critically damped spring):**

```cpp
// At transition: record initial offset and velocity
float x0 = source_pos - dest_pos;  // Initial position offset
float v0 = source_vel - dest_vel;  // Initial velocity offset

// Each frame: decay using critically damped spring
float y = halflife_to_damping(halflife) / 2.0f;
float j0 = x0;
float j1 = v0 + x0 * y;
float eydt = fast_negexp(y * t);

float offset = eydt * (j0 + j1 * t);  // Apply this offset to destination
```

Where:
```
halflife_to_damping(halflife) = (4.0 * ln(2)) / (halflife + epsilon)
```

Typical halflife for inertialization: **0.1 seconds** (from Holden's reference implementation).

**Polynomial decay (cubic):**

A cubic polynomial `a*t^3 + b*t^2 + c*t + d` with coefficients derived from initial conditions (initial offset, initial velocity) that reaches zero at the blend duration with zero velocity.

### Dead Blending (Holden's Alternative)

Daniel Holden proposed "dead blending" as a simpler alternative to offset-based inertialization:

1. At transition time, record the source animation's current pose position and velocity
2. Each frame during transition:
   - **Extrapolate** the recorded source pose forward using its velocity: `ext_pos += ext_vel * dt`
   - Decay the velocity using exponential damping: `vel *= exp(-ln(2) * dt / halflife)`
   - Compute a blend factor using smoothstep interpolation based on elapsed time
   - **Lerp** between the extrapolated source and the destination pose

Advantages of dead blending over offset inertialization:
- Simpler implementation
- The result is always between the extrapolated source and destination (guaranteeing reasonable poses)
- Avoids certain artifacts like knee pops and swimming motions
- Requires only the current pose and its velocity at transition time

### Exponential Decay Function

```cpp
float damper_decay_exact(float x, float halflife, float dt)
{
    return x * expf(-(0.69314718056f * dt) / (halflife + 1e-8f));
}
```

### Fast Exponential Approximation

```cpp
float fast_negexp(float x)
{
    return 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
}
```

This avoids calling `expf()` in hot loops while maintaining good accuracy.

---

## 7. Database Preprocessing

### Feature Extraction Pipeline

During offline preprocessing:

1. **Sample all animation clips** at a fixed rate (typically 30 Hz) to extract per-frame data: bone positions, rotations, velocities (via finite differences or analytical derivatives)
2. **Compute feature vectors** for every frame: extract the selected bone positions/velocities relative to root, and compute trajectory features by looking ahead in the animation data
3. **Store in feature matrix**: An N x M matrix where N = number of frames across all clips, M = feature dimensionality
   - Row-major storage for cache-friendly sequential access
   - Example scale: 1 hour at 30 Hz = 108,000 frames. With 59 features = ~6.4 million float values = ~24.3 MB
4. **Normalize features**: Compute per-column mean and standard deviation, then z-score normalize all features
5. **Build acceleration structure**: Construct a KD-tree over the normalized feature vectors

### Animation Mirroring

Mirroring doubles the effective database size without additional mocap:

1. **Bone pair mapping**: Define left/right bone pairs (e.g., LeftFoot <-> RightFoot, LeftHand <-> RightHand). Center-line bones (pelvis, spine, head) map to themselves
2. **Position mirroring**: For each frame, swap the positions/velocities of paired bones and negate the lateral axis (typically X) to mirror the motion across the character's sagittal plane
3. **Trajectory mirroring**: Negate the lateral component of trajectory positions and directions
4. **Rotation mirroring**: Apply axis-specific negation and rotation offsets per bone, accounting for skeleton-specific bone orientations. The mirror axis and flip axis may differ per bone
5. **Feature vector generation**: Compute feature vectors for mirrored frames identically to originals. The mirrored frames are added to the database as additional searchable entries

### Clip Annotation and Tagging

Annotations provide logical information that cannot be inferred automatically:
- **Locomotion type**: walk, run, strafe, turn, stop, jump
- **Combat actions**: attack type, defense stance (as in For Honor)
- **Contact events**: foot plants, hand contacts
- **Quality tags**: mark frames as "do not transition to" (e.g., the middle of a specific combo)

Tags can be used to **mask** regions of the database during search, restricting results to contextually appropriate animations.

### Velocity and Acceleration Caching

Pre-compute and cache per-frame:
- Joint velocities (finite difference of positions)
- Joint accelerations (finite difference of velocities)
- Root velocity and angular velocity
- These cached values feed both the feature vectors and the runtime pose comparison

---

## 8. Trajectory Prediction

### From Player Input to Desired Trajectory

The trajectory prediction system converts raw player input (gamepad stick, keyboard) into a smooth desired future path for matching:

1. **Read input**: Get desired direction from gamepad stick (with deadzone filtering, typically 0.2) and speed from stick magnitude
2. **Compute desired velocity**: Combine stick direction with camera-relative facing and multiply by movement speed
   - Walk speed: ~1.75 m/s
   - Run speed: ~4.0 m/s
3. **Smooth via spring damper**: Use a critically damped spring to smoothly interpolate between current velocity and desired velocity
4. **Predict future positions**: Integrate the spring forward in time to predict positions at multiple future sample points

### Spring-Damper Trajectory Prediction (Holden's Method)

The critically damped spring provides smooth, predictable trajectory prediction:

```cpp
void spring_character_update(
    float& x, float& v, float& a,
    float v_goal, float halflife, float dt)
{
    float y = halflife_to_damping(halflife) / 2.0f;
    float j0 = v - v_goal;
    float j1 = a + j0 * y;
    float eydt = fast_negexp(y * dt);

    // Position integrates the smoothed velocity
    x = eydt * ((-j1 / (y * y)) + ((-j0 - j1 * dt) / y))
        + (j1 / (y * y)) + j0 / y + v_goal * dt + x;
    v = eydt * (j0 + j1 * dt) + v_goal;
    a = eydt * (a - j1 * y * dt);
}
```

This is the critically damped spring applied to character velocity (not position), with the integral used to compute the character position. The simulation halflife for velocity/rotation is typically **0.27 seconds**.

### Future Trajectory Prediction

To predict the trajectory at arbitrary future times without simulating frame-by-frame:

```cpp
void spring_character_predict(
    float px[], float pv[], float pa[], int count,
    float x, float v, float a, float v_goal,
    float halflife, float dt)
{
    for (int i = 0; i < count; i++)
    {
        px[i] = x; pv[i] = v; pa[i] = a;
    }
    for (int i = 0; i < count; i++)
    {
        spring_character_update(px[i], pv[i], pa[i],
            v_goal, halflife, i * dt);
    }
}
```

This accurately predicts the spring state at any future point without simulating intermediate steps, because the critically damped spring has an exact analytical solution.

### Key Spring Properties

- **Stability**: Guaranteed stable regardless of timestep size or parameter magnitude
- **Velocity continuity**: Maintains smooth motion through goal changes (unlike lerp-based damping)
- **Framerate independence**: Produces identical behavior at any timestep
- **Computational efficiency**: Exact solution in constant time without iteration

### O3DE's Approach

O3DE uses an exponential curve starting at the current character position that bends toward the joystick target direction to generate the desired future trajectory, predicting 2D character positions and directions at 20, 40, and 60 frames into the future (assuming 60 Hz).

---

## 9. Performance Considerations

### Database Sizes

| Database | Frames | Features | Memory |
|----------|--------|----------|--------|
| Small (few minutes) | ~5,000-10,000 | 27 | ~1-2 MB |
| Medium (10-30 min) | ~18,000-54,000 | 27 | ~5-15 MB |
| Large (1 hour) | ~108,000 | 59 | ~24 MB |
| AAA production | ~500,000+ | 27-60 | ~50-150 MB |

Learned Motion Matching can reduce memory to ~15 MB for 150 MB databases (10x reduction) and ~17 MB for 590 MB databases (70x reduction, 8.5 MB with 16-bit quantization).

### Search Cost

**Brute force**: O(N * D) where N = frames, D = dimensions
- 10,000 frames x 27 dimensions = 270,000 float operations
- With SIMD (AVX2, 8 floats at a time): ~34,000 SIMD ops
- Wall time: ~10-50 microseconds on modern CPU

**KD-tree**: O(D * log N) typical case
- Provides orders-of-magnitude speedup for large databases
- Construction is O(N * D * log N), done offline
- Diminishing returns above ~60 dimensions due to curse of dimensionality

### Search Frequency

The matching search does **not** need to run every frame:
- Typical: **every 0.1 seconds** (10 times per second) -- from Holden's reference implementation
- Some implementations: 3-5 times per second for less responsive but cheaper characters
- More frequent = more responsive but more CPU cost
- Between searches, the currently selected animation simply plays forward

### Optimization Strategies

1. **Amortized search**: Search only every 0.1s, not every frame
2. **Early termination**: If a candidate's partial cost already exceeds the current best, skip remaining dimensions
3. **SIMD vectorization**: Process multiple feature dimensions simultaneously using SSE/AVX
4. **Frame exclusion**: Skip frames near the current playback position (they are likely already being considered)
5. **Tag masking**: Exclude tagged frame ranges from search based on gameplay context
6. **LOD**: Reduce search frequency or database size for distant/less-important characters
7. **Multithreading**: Distribute search across multiple cores for multiple characters

### Per-Character Budget

A single motion matching character typically costs:
- **Search**: 10-100 microseconds per search (depending on database size and acceleration structure)
- **Feature extraction**: ~5-10 microseconds (computing query vector from current pose)
- **Inertialization**: ~1-5 microseconds (applying decayed offset)
- **Total per frame**: ~20-120 microseconds at 10 searches/second = 2-12 microseconds amortized per frame

This is well within budget for dozens of characters at 60 FPS (16.67 ms frame budget).

---

## 10. UE5 PoseSearch Implementation

### Architecture

UE5's motion matching is implemented in the **PoseSearch** plugin (experimental, then production in 5.4+). Key components:

1. **PoseSearchSchema**: Defines the feature vector structure
   - Configures which bones to sample (position and velocity)
   - Configures trajectory sampling (time domain: negative for history, positive for future prediction)
   - Each part of the schema is called a **channel**
   - Quaternions use 6 floats (cardinality 6), vectors use 3 floats (cardinality 3)

2. **PoseSearchDatabase**: Contains preprocessed animation data
   - Holds animation sequences with extracted feature vectors
   - Configurable sample rate (lower = fewer frames, less memory, coarser matching)
   - Good starting sample interval: 0.1 seconds
   - Supports KD-tree optimization for search

3. **PoseSearchNormalizationSet**: Normalizes feature values across the database

4. **CharacterMovementTrajectory Component**: Predicts the character's desired future trajectory from movement input

5. **FAnimationNode_MotionMatching**: Animation blueprint node
   - Calls `UpdateAssetPlayer()` every frame
   - Contains a `UPoseSearchSearchableAsset` (usually a `UPoseSearchDatabase`)
   - Calls `PoseSearchLibrary::UpdateMotionMatchingState()` in the Update method

### Channel Weights

Channel weights tell the system how much each feature type influences scoring:
- Example: Pose channel weight = 1, Trajectory channel weight = 3 means trajectory differences matter 3x more than pose differences
- Weights are configured in the Motion Database Config asset

### Search Process

1. The animation node builds a query feature vector from the current pose and predicted trajectory
2. The database selects the search method based on its configured optimization (e.g., KD-tree)
3. The search finds the frame with minimum weighted distance to the query
4. If the result differs significantly from the current playback position, a transition is triggered

### Trajectory Prediction

UE5 uses the **Character Movement Trajectory** component to predict the character's future path from movement input. This feeds into the trajectory channels of the feature vector for matching.

### Debugging

UE5 provides motion matching debugging tools including:
- Pose search cost visualization
- Feature vector comparison
- Database coverage analysis

---

## 11. Implementation Roadmap for Vestige

Based on this research, a recommended implementation order for the Vestige engine:

### Phase 1: Foundation
1. Define a feature vector schema (start with 27 dimensions: feet pos/vel, hip vel, 3 future trajectory points/directions)
2. Implement offline feature extraction from animation clips
3. Implement z-score normalization (compute mean/stddev per feature dimension)
4. Store features in a flat N x M array (row-major, cache-friendly)

### Phase 2: Runtime Search
1. Implement brute-force search with weighted squared Euclidean distance
2. Implement spring-damper trajectory prediction from player input
3. Implement query vector construction each frame
4. Run search every 0.1 seconds (amortized)

### Phase 3: Transitions
1. Implement inertialization blending (spring-based offset decay, halflife = 0.1s)
2. Implement foot contact detection and foot locking (IK-based, unlock radius = 0.2m)
3. Tune transition thresholds

### Phase 4: Optimization
1. Implement KD-tree acceleration structure
2. Add SIMD vectorization to the cost function
3. Add early termination to the narrow-phase search
4. Profile and tune search frequency

### Phase 5: Database Tools
1. Implement animation mirroring (bone pair swapping + axis negation)
2. Add clip annotation/tagging system
3. Build editor tools: database browser, pose search debugger, cost visualization
4. Implement PCA-based feature visualization for database coverage analysis

### Phase 6: Advanced (Future)
1. Learned Motion Matching (neural network replacement for search)
2. Dead blending as inertialization alternative
3. LOD system for distant characters
4. Multithreaded search for many characters

---

## 12. Key Data Structures

### MotionDatabase
```
struct MotionDatabase
{
    // Feature matrix: N frames x M features, row-major
    std::vector<float> features;     // size = num_frames * num_features
    int num_frames;
    int num_features;

    // Normalization parameters (per-feature)
    std::vector<float> feature_mean;   // size = num_features
    std::vector<float> feature_stddev; // size = num_features

    // Source animation references
    struct FrameInfo
    {
        int clip_index;       // Which animation clip
        float clip_time;      // Time within that clip
        uint32_t tags;        // Bitmask of annotation tags
    };
    std::vector<FrameInfo> frame_info; // size = num_frames

    // Bone data for pose reconstruction
    std::vector<BonePose> bone_poses;  // Full skeleton pose per frame

    // KD-tree for acceleration (built from normalized features)
    KDTree kd_tree;
};
```

### FeatureSchema
```
struct FeatureSchema
{
    // Pose features: which bones to track
    struct BoneFeature
    {
        int bone_index;
        bool use_position;    // 3 floats
        bool use_velocity;    // 3 floats
        float weight;
    };
    std::vector<BoneFeature> bone_features;

    // Trajectory features: sample times
    struct TrajectorySample
    {
        float time_offset;    // Negative = past, positive = future
        bool use_position;    // 2 floats (XZ)
        bool use_direction;   // 2 floats (XZ)
        float weight;
    };
    std::vector<TrajectorySample> trajectory_samples;

    int totalDimensions() const;  // Sum of all feature floats
};
```

### Key Constants (from Holden's reference)

| Constant | Value | Purpose |
|----------|-------|---------|
| Search interval | 0.1 s | How often to run matching |
| Inertialization halflife | 0.1 s | Transition smoothing speed |
| Simulation halflife | 0.27 s | Velocity/rotation smoothing |
| Walk speed | 1.75 m/s | Default walk speed |
| Run speed | 4.0 m/s | Default run speed |
| Gamepad deadzone | 0.2 | Stick input filter |
| Foot unlock radius | 0.2 m | IK foot lock release distance |
| Foot height | 0.02 m | Ground plane offset for feet |
| Position clamp | 0.15 m | Max character position deviation |
| Rotation clamp | 0.5 * PI rad | Max character rotation deviation |
| Velocity change threshold | 50.0 | Input urgency detection |

---

## Sources

- Simon Clavet, "Motion Matching and The Road to Next-Gen Animation", GDC 2016
  - https://www.gdcvault.com/play/1023280/
  - https://archive.org/details/GDC2016Clavet
- Kristjan Zadziuk & Michael Buttner, "Motion Matching, The Future of Games Animation... Today", GDC 2016
  - https://www.gdcvault.com/play/1023478/
- David Bollo, "Inertialization: High-Performance Animation Transitions in Gears of War", GDC 2018
  - https://www.gdcvault.com/play/1025331/
- Daniel Holden et al., "Learned Motion Matching", SIGGRAPH 2020
  - https://dl.acm.org/doi/10.1145/3386569.3392440
  - https://www.ubisoft.com/en-us/studio/laforge/news/6xXL85Q3bF2vEj76xmnmIu/introducing-learned-motion-matching
- Daniel Holden, "Spring-It-On: The Game Developer's Spring-Roll-Call"
  - https://theorangeduck.com/page/spring-roll-call
- Daniel Holden, "Dead Blending"
  - https://theorangeduck.com/page/dead-blending
- Daniel Holden, "Inertialization Transition Cost"
  - https://theorangeduck.com/page/inertialization-transition-cost
- Daniel Holden, Motion-Matching reference implementation
  - https://github.com/orangeduck/Motion-Matching
- O3DE Motion Matching documentation
  - https://docs.o3de.org/blog/posts/blog-motionmatching/
- UE5 Motion Matching documentation
  - https://dev.epicgames.com/documentation/en-us/unreal-engine/motion-matching-in-unreal-engine
- Motion Symphony (Unity plugin) documentation
  - https://www.wikiful.com/@AnimationUprising/motion-symphony/motion-matching/understanding-motion-matching
- JLPM22 MotionMatching for Unity
  - https://github.com/JLPM22/MotionMatching
