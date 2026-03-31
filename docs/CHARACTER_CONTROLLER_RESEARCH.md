# Physics-Based Character Controller Research

**Date:** 2026-03-31
**Purpose:** Research physics-based character controller approaches to upgrade Vestige's current AABB-based first-person controller. Jolt Physics has already been selected as the physics engine (see `PHYSICS_ENGINE_RESEARCH.md`).

---

## Table of Contents

1. [Current Vestige Controller Analysis](#1-current-vestige-controller-analysis)
2. [Kinematic vs Dynamic Character Controllers](#2-kinematic-vs-dynamic-character-controllers)
3. [Jolt Physics Character Controller](#3-jolt-physics-character-controller)
4. [Bullet Physics Character Controller (Reference)](#4-bullet-physics-character-controller-reference)
5. [Slope Handling](#5-slope-handling)
6. [Step Climbing](#6-step-climbing)
7. [Ground Detection](#7-ground-detection)
8. [Interaction with Dynamic Objects](#8-interaction-with-dynamic-objects)
9. [Ghost Objects / Shape Casting](#9-ghost-objects--shape-casting)
10. [Best Practices from Modern Engines](#10-best-practices-from-modern-engines)
11. [Integration Plan: AABB to Physics-Based](#11-integration-plan-aabb-to-physics-based)
12. [Sources](#sources)

---

## 1. Current Vestige Controller Analysis

The current controller (`engine/core/first_person_controller.cpp`) uses a hand-rolled approach:

**What it does:**
- AABB collision: Builds a player bounding box from `playerHeight` (1.7m) and `playerRadius` (0.3m), tests against world AABB colliders, and applies minimum push-out resolution.
- Terrain following: In walk mode, queries terrain heightmap and applies smoothed vertical damping (fast up, gentle down).
- Slope limiting: Compares terrain normal against `maxSlopeAngle` (50 degrees); reverts XZ position if slope is too steep and player would ascend.
- Movement: Direct position manipulation -- compute desired position from input, then correct for collisions.

**Current limitations:**
- **No capsule shape** -- uses an axis-aligned box, which catches on corners and edges that a rounded shape would slide past.
- **No sweep tests** -- collision is detected only at the final position (discrete), not along the movement path. Fast movement can tunnel through thin geometry.
- **No step climbing** -- small obstacles (stairs, curbs, door thresholds) block the player entirely.
- **No sliding along walls** -- the minimum-push-out approach resolves overlap but does not decompose remaining velocity along the contact surface.
- **No interaction with dynamic objects** -- the AABB system is purely static geometry.
- **No gravity/velocity model** -- movement is position-based with no concept of velocity, acceleration, or falling.
- **Terrain-only ground detection** -- grounded state is determined solely by heightmap queries, not by general physics contact.

---

## 2. Kinematic vs Dynamic Character Controllers

### Kinematic Controller

A kinematic controller directly works with input displacement vectors (1st order control). The application sets the position or velocity each frame, and the controller performs collision detection (via sweep tests) to resolve movement. The physics engine does not apply forces or gravity -- the game code handles everything.

**Advantages:**
- Predictable, game-friendly movement. The character goes exactly where the code says, minus collision corrections.
- No jittering, tunneling, or instability issues that plague dynamic controllers.
- Easy to implement game-specific behaviors (wall running, ledge grabbing, teleportation) since the game code has full control.
- More expensive to implement correctly (sweep tests, depenetration), but the result is more polished.
- Can be updated at a specific point in the frame, decoupled from the physics timestep.

**Disadvantages:**
- Other physics objects do not automatically see or react to the character. Interaction must be explicitly coded (applying forces in callbacks).
- More complex implementation -- the engine must provide sweep tests, depenetration, slope logic, and step climbing that a dynamic controller gets "for free."

### Dynamic Controller

A dynamic controller treats the character as a physics rigid body. Movement is achieved by applying forces or velocities, and the physics engine handles collision response naturally.

**Advantages:**
- Automatic interaction with other physics objects -- pushing crates, being pushed by moving platforms, etc.
- Simpler initial implementation -- the physics engine handles collision response.

**Disadvantages:**
- Movement feels "floaty" or "sluggish" unless heavily tuned. Characters slide on slopes, bounce off walls, and exhibit momentum that fights player intent.
- Physics instabilities: jittering when standing on moving surfaces, unexpected launches from constraint violations, stacking issues.
- Difficult to prevent unwanted behaviors (spinning when pushed, sliding on slopes that should be walkable).
- Harder to synchronize with game logic -- the physics step may not align with when you need the character to move.

### Recommendation for Vestige

**Use a kinematic controller.** For a first-person architectural walkthrough engine:
- Movement must feel precise and responsive (the player is exploring, not fighting physics).
- There are no gameplay mechanics that require the character to be a dynamic body.
- PhysX, Unreal, Unity, and Godot all use kinematic approaches for their primary character controllers.
- Jolt's `CharacterVirtual` is specifically designed for this pattern and is the recommended approach.

---

## 3. Jolt Physics Character Controller

Jolt provides two character controller classes, designed to be used together.

### CharacterVirtual (Primary -- Kinematic)

`CharacterVirtual` does not use a rigid body. It moves by performing collision checks only (hence "virtual"). It is not tracked by the `PhysicsSystem` and must be updated manually by the application.

**Key advantage:** You control exactly when the character moves in the frame. This is critical because character movement often needs to happen at a specific point in the update loop (after input processing, before rendering).

**Core API:**

| Method | Purpose |
|---|---|
| `SetLinearVelocity()` / `GetLinearVelocity()` | Set desired movement speed |
| `Update(deltaTime, gravity, ...)` | Main simulation step -- applies velocity, performs collision |
| `ExtendedUpdate(deltaTime, gravity, settings, ...)` | Combined Update + StickToFloor + WalkStairs |
| `GetGroundState()` | Returns `OnGround`, `OnSteepGround`, `NotSupported`, or `InAir` |
| `IsSupported()` | True if on normal or steep ground |
| `GetGroundNormal()` / `GetGroundPosition()` | Contact geometry |
| `WalkStairs(deltaTime, stepUp, ...)` | Attempts step-up/forward/down sequence for stair climbing |
| `StickToFloor(deltaTime, ...)` | Projects character downward to maintain ground contact on slopes |
| `CanWalkStairs(velocity)` | Returns true if the character hit a vertical wall it could potentially step over |
| `CancelVelocityTowardsSteepSlopes()` | Prevents climbing slopes beyond max angle |
| `SetShape()` / `GetShape()` | Switch collision shape (standing/crouching) |
| `CheckCollision(position, rotation, ...)` | Query contacts at a specific location |
| `RefreshContacts()` | Recalculate contacts after teleportation |
| `SetMass()` / `SetMaxStrength()` | Weight and push force on dynamic bodies |
| `SetPenetrationRecoverySpeed()` | How fast overlaps are resolved |
| `GetActiveContacts()` | Current collision contact list |
| `HasCollidedWith(bodyID)` | Check collision with specific body |

**Configuration (CharacterVirtualSettings):**

```cpp
CharacterVirtualSettings settings;
settings.mShape = capsuleShape;              // Collision shape (usually capsule)
settings.mMaxSlopeAngle = DegreesToRadians(50.0f);  // Max walkable slope
settings.mCharacterPadding = 0.02f;          // Skin thickness for collision
settings.mPenetrationRecoverySpeed = 1.0f;   // Overlap resolution speed
settings.mPredictiveContactDistance = 0.1f;   // Look-ahead for contacts
settings.mSupportingVolume = Plane(Vec3::sAxisY(), -radius);  // Ground detection plane
settings.mMass = 70.0f;                      // Character mass (for pushing objects)
settings.mMaxStrength = 100.0f;              // Max force on dynamic bodies
```

**Per-frame update pattern (from Jolt's CharacterVirtualTest sample):**

```
1. Read input -> compute desired horizontal velocity
2. If grounded: set vertical velocity to 0 (or jump impulse)
   If airborne: preserve current vertical velocity
3. Apply gravity: velocity += gravity * deltaTime
4. Smooth input: 0.25 * newVelocity + 0.75 * previousVelocity
5. Call ExtendedUpdate(deltaTime, gravity, updateSettings, ...)
   - This internally calls Update() + StickToFloor() + WalkStairs()
6. Query GetGroundState() for animation/sound triggers
```

### Character (Secondary -- Rigid Body Proxy)

The `Character` class creates an actual rigid body in the physics simulation. It is used as a **companion** to `CharacterVirtual` so that other physics objects can collide with and react to the player.

**Usage pattern:**
1. Create a `CharacterVirtual` for movement logic (primary control).
2. Create a `Character` as a keyframed body (no gravity, no dynamic forces).
3. Each frame, after `CharacterVirtual` updates, set the `Character`'s position/velocity to match.
4. Dynamic objects in the world see the `Character` rigid body and react to it naturally.

Alternatively, `CharacterVirtual` supports `SetInnerBodyShape()` which creates an internal proxy body automatically, simplifying this pairing.

### Why Two Classes?

This split solves a fundamental tension: the game code needs full control over movement timing and behavior (kinematic), but physics objects need something to collide with (rigid body). By pairing them, you get the best of both worlds without the downsides of a fully dynamic character.

---

## 4. Bullet Physics Character Controller (Reference)

Included for reference since Bullet is widely documented and many existing tutorials use it.

### btKinematicCharacterController

Bullet's built-in kinematic character controller uses a `btPairCachingGhostObject` for collision detection. The ghost object is a special collision object that tracks all overlapping objects without generating contact responses.

**Known issues:**
- The controller does not properly respect the `CF_NO_CONTACT_RESPONSE` flag on ghost objects used as triggers. It treats them as solid obstacles.
- Interaction with dynamic rigid bodies is not implemented -- you must manually apply forces.
- Stair climbing is rudimentary and unreliable on complex geometry.
- The class is **no longer actively maintained** by the Bullet project.
- Requires manual setup of `btGhostPairCallback` on the broadphase for ghost objects to work.

**Workarounds documented by the community:**
- Custom subclass that filters trigger objects in `recoverFromPenetration()` and in the convex result callback.
- Manual force application in the collision callback for dynamic object interaction.

**Conclusion:** Bullet's character controller is a cautionary example. It shipped with fundamental issues that were never fixed, and the community had to work around them for years. This reinforces the decision to use Jolt, where the character controller is actively maintained and significantly more capable.

---

## 5. Slope Handling

### The Problem

When a character walks on a sloped surface, several behaviors must be managed:
- **Walkable slopes** (below max angle): Character should walk normally, optionally at constant speed regardless of incline.
- **Steep slopes** (above max angle): Character should not be able to ascend. May slide downward.
- **Transitioning between slopes**: Movement should feel smooth, not jerky.

### How PhysX Handles It

PhysX's CCT uses a cosine-based slope limit:
```cpp
slopeLimit = cosf(degToRad(45.0f));  // 45-degree limit
```

Two modes:
- `ePREVENT_CLIMBING`: Blocks upward movement on steep slopes but allows lateral/downward traversal.
- `ePREVENT_CLIMBING_AND_FORCE_SLIDING`: Forces the character to slide down non-walkable slopes.

**Important limitation:** Slope limiting only works against static objects, not dynamic or kinematic objects.

### How Jolt Handles It

Jolt provides `CancelVelocityTowardsSteepSlopes()`, which modifies the desired velocity to prevent movement further onto slopes exceeding `mMaxSlopeAngle`. This is called before `Update()`.

The `GetGroundState()` method distinguishes:
- `OnGround` -- normal walkable surface
- `OnSteepGround` -- ground contact exists but angle exceeds max slope
- `NotSupported` -- touching something but not ground
- `InAir` -- no ground contact

### How Godot Handles It

Godot's `CharacterBody3D` has:
- `floor_max_angle` (default 45 degrees): Maximum angle considered a floor.
- `floor_constant_speed`: When true, speed is constant regardless of slope angle. When false, character moves faster downhill and slower uphill.
- `floor_stop_on_slope`: Prevents sliding when standing still on a slope.
- `floor_snap_length`: Snapping distance to keep the character attached to slopes when moving downhill.

### Recommended Approach for Vestige

- Use Jolt's `mMaxSlopeAngle` set to ~50 degrees (matching current config).
- Call `CancelVelocityTowardsSteepSlopes()` before each update.
- Use `floor_constant_speed` equivalent behavior: normalize horizontal velocity on slopes so walking speed feels consistent.
- Implement floor snapping via `StickToFloor()` (built into `ExtendedUpdate()`).

---

## 6. Step Climbing

### The Problem

Small obstacles like stairs, curbs, door thresholds, and slight floor height changes should not block the player. The character should smoothly step over obstacles below a configurable height.

### The Algorithm (Jolt's WalkStairs)

Jolt's `WalkStairs()` implements the standard three-phase step climbing algorithm:

1. **Step Up**: Cast the character shape upward by `stepUp` distance (e.g., 0.35m). If blocked, the step is too high.
2. **Step Forward**: From the elevated position, cast forward by the desired movement distance. This checks if there is actually walkable ground at the higher level.
3. **Step Down**: From the forward position, cast downward to find the landing surface. If a valid floor is found within `stepDown` distance, the character lands there.

If any phase fails (blocked upward, no forward progress, no valid landing), the step attempt is rejected and normal sliding behavior applies.

**Key parameters:**
- `mWalkStairsStepUp`: Maximum step height (typically 0.3-0.5m; real-world stair risers are 17-20cm).
- `mWalkStairsMinStepForward`: Minimum forward distance required after stepping up.
- `mWalkStairsStepForwardTest`: Forward test distance.
- `mWalkStairsStepDownExtra`: Extra distance to search downward for a landing.

### How PhysX Handles It

PhysX uses a `stepOffset` parameter. Key details:
- Keep step offset as small as possible for predictable behavior.
- Capsule shapes have two modes: `eEASY` (capsule curves may exceed stated step offset) and `eCONSTRAINED` (enforces step offset as hard limit).
- Auto-stepping only activates when the character is close to the ground.

### How Godot Handles It (Community Solutions)

Godot does **not** have built-in step climbing. The community has developed workarounds:
- Custom `move_and_stair_step()` functions that replace `move_and_slide()`.
- Uses `PhysicsServer3D.body_test_motion()` to test collisions with a copy of the player's shape at elevated positions.
- Collision shape margin must be very low (0.01 or less) to prevent snagging.

An official proposal (godotengine/godot-proposals#2751) has been open since May 2021 requesting built-in step climbing support.

### Recommended Approach for Vestige

- Use Jolt's `ExtendedUpdate()` which includes `WalkStairs()` automatically.
- Set `mWalkStairsStepUp` to ~0.35m (covers standard stair risers and small architectural features).
- `CanWalkStairs()` returns true when the character hits a vertical wall it could potentially step over -- use this to conditionally invoke the stair logic.

---

## 7. Ground Detection

### The Problem

The character controller must reliably know if the player is on the ground, to determine:
- Whether to apply gravity or ground friction.
- Whether jumping is allowed.
- Which movement mode to use (walking vs. falling).
- What sound effects and animations to play.

### Approaches

**1. Contact-based (Jolt's approach):**
Jolt's `CharacterVirtual` maintains an `activeContacts` list from sweep tests during `Update()`. The ground state is derived from the contact with the most vertical normal below the character's supporting volume plane.

`GetGroundState()` returns one of four states:
- `OnGround` -- supported, normal walkable surface
- `OnSteepGround` -- contact below but angle exceeds max slope
- `NotSupported` -- contact exists but not below (e.g., touching a wall)
- `InAir` -- no contacts below

Additional queries: `GetGroundNormal()`, `GetGroundPosition()`, `GetGroundVelocity()`, `GetGroundMaterial()`, `GetGroundBodyID()`.

**2. Raycast-based:**
Cast a single ray (or multiple rays) downward from the character's feet. If the ray hits within a threshold distance, the character is grounded.

- Simple to implement but unreliable on edges, slopes, and uneven terrain.
- Multiple rays (center + perimeter) improve edge detection but increase cost.
- Shape casts (sphere cast downward) are more reliable than rays.

**3. Collision flag-based (PhysX):**
PhysX returns `PxControllerCollisionFlags` from the `move()` call, including `eCOLLISION_DOWN` indicating ground contact.

### Recommended Approach for Vestige

Use Jolt's built-in `GetGroundState()`. It is contact-based, accounts for slope angles, and provides rich ground information (material, body ID, velocity) needed for:
- Footstep sounds (material-dependent).
- Moving platform support (ground velocity).
- Slope-dependent movement speed.
- Jump availability.

---

## 8. Interaction with Dynamic Objects

### The Problem

A kinematic character controller is invisible to the physics simulation. Without explicit handling:
- The player walks through physics objects.
- Physics objects cannot push or block the player.
- There is no sense of physical presence in the world.

### Jolt's Approach

Jolt's `CharacterVirtual` provides several mechanisms:

**1. Contact Listener Callbacks:**
```
OnContactAdded(characterID, bodyID, ...)
OnContactPersisted(characterID, bodyID, ...)
OnContactRemoved(characterID, previousBodyID, ...)
OnContactSolve(characterID, bodyID, contactPosition, contactNormal, ...)
```

In `OnContactSolve()`, you can:
- Apply impulses to dynamic bodies being pushed by the character.
- Cancel character velocity when hitting immovable objects.
- Implement custom slide behavior on specific surfaces.

**2. Mass and Strength:**
- `SetMass(70.0f)` -- character's effective mass for push calculations.
- `SetMaxStrength(100.0f)` -- maximum force the character can exert on dynamic bodies.

**3. Inner Body Proxy:**
`SetInnerBodyShape()` creates an invisible rigid body that follows the `CharacterVirtual`. This body exists in the physics world so dynamic objects can collide with and be pushed by the character naturally, without manual callback code for every interaction.

**4. From Jolt's Sample (CharacterVirtualTest):**
Dynamic boxes on ramps use bitmask permutations:
- Bit 0: Controls whether the character can push the object.
- Bit 1: Controls whether the object can push the character (via impulse).

This provides granular control over which objects the player interacts with and how.

### PhysX's Approach

PhysX deliberately avoids automatic force application in its CCT. Reasons cited in the documentation:
- Artificial bounding volumes (capsule/box) do not realistically model character-object interactions.
- Capsule-box collisions cause undesired rotation on the pushed object.
- Physics-based pushing contradicts kinematic design philosophy.

The recommended approach is to use the `onShapeHit()` callback to apply custom, game-specific forces.

### Recommended Approach for Vestige

1. Use `CharacterVirtual` with `SetInnerBodyShape()` for automatic physics presence.
2. Set mass to ~70kg, max strength to ~100N for natural-feeling interactions.
3. For special objects (interactable props, puzzle elements), use the contact listener callbacks with custom logic.
4. For the initial implementation, focus on: character pushes small objects (crates, furniture), character is blocked by large objects (walls, pillars), character ignores trigger volumes.

---

## 9. Ghost Objects / Shape Casting

### Sweep Tests (Shape Casts)

A sweep test (or shape cast) moves a collision shape along a path and reports the first intersection. It is the foundation of kinematic character controllers.

**How it works:**
1. Start with the character's collision shape at the current position.
2. "Sweep" the shape along the desired displacement vector.
3. If a collision is found, report the hit position, normal, and penetration depth.
4. The character moves to just before the hit point, and the remaining velocity is projected onto the surface (the "slide" vector).

**The collide-and-slide algorithm (from PhysX, used since Quake/Doom era):**
1. Sweep forward to first impact.
2. Move to impact point minus a small skin offset (to prevent floating-point overlap).
3. Decompose remaining velocity: subtract the component along the hit normal, keeping the tangent component.
4. Repeat with the tangent velocity (up to 3 iterations typically).
5. If two surfaces form an acute angle, slide along their intersection (cross product of normals) to prevent oscillation.

**Depenetration:**
When the character starts overlapping geometry (teleportation, spawning, pushed by moving platforms), a recovery step is needed:
1. Use an inflated shape (slightly larger than the character) to detect all overlapping objects.
2. For each overlap, compute the penetration direction and depth.
3. Iteratively push the character out (Gauss-Seidel relaxation, typically 4-16 iterations).
4. Use `penetrationRecoverySpeed` to control how fast this happens (instant recovery looks jarring).

### Jolt's Collision Queries

Jolt provides through its `NarrowPhaseQuery`:
- `CastShape()` -- sweep a shape along a ray, get first/all hits.
- `CollideShape()` -- test overlap at a position, get all contacts.
- `CastRay()` -- traditional raycast.

`CharacterVirtual` uses these internally but they are also available for game code to perform custom queries (line-of-sight checks, ground probes, interaction range tests).

### Ghost Objects (Bullet Terminology)

Bullet uses `btGhostObject` / `btPairCachingGhostObject` as the collision volume for its character controller. A ghost object detects all overlapping bodies without generating physics responses. This is analogous to trigger/sensor volumes in other engines.

In Jolt, the equivalent concept is using collision queries (`CollideShape`) with appropriate layer filters. Jolt's layer system (object layers + broadphase layers) provides efficient filtering without special object types.

### Contact Offset / Skin Width

All physics engines use a thin shell ("skin") around the character shape:
- **PhysX:** `contactOffset` parameter.
- **Jolt:** `mCharacterPadding` parameter.
- **Unity:** `skinWidth` on CharacterController.

Purpose: Prevents the character from being exactly flush with surfaces, which causes sweep tests to miss contacts on the next frame. Typical value: 0.02-0.05m.

---

## 10. Best Practices from Modern Engines

### Unreal Engine (CharacterMovementComponent)

Unreal uses a **kinematic capsule** with sweep-based collision. Key design decisions:

- **Sweep-first movement:** Every position change uses `SweepSingleByChannel` to detect collisions before moving.
- **Step-up algorithm:** When a sweep hits an obstacle, Unreal sweeps upward, then forward, then downward to attempt stepping over it. Same three-phase algorithm as Jolt.
- **Floor detection:** Vertical sweep from the capsule bottom. Returns hit distance, impact normal, and floor walkability.
- **Movement modes:** Walking, Falling, Swimming, Flying, Custom. Each mode has its own movement logic and collision behavior.
- **Sub-stepping:** Large time steps are broken into smaller increments to prevent tunneling and improve collision accuracy.
- **Slide decomposition:** After a collision, remaining movement is projected onto the surface tangent plane using `VectorPlaneProject(Remainder, Hit.Normal)`.
- **All movement functions are virtual** -- extensibility is a core design principle.

### Unity (CharacterController)

Unity's built-in CharacterController:

- **Capsule shape only** -- no box option.
- **Slope limit** (default 45 degrees) and **step offset** (default 0.3m) as primary parameters.
- **Skin width** (default 0.08m): Collision skin to prevent overlaps.
- **Grounded check:** `isGrounded` property based on collision flags from the last `Move()` call.
- **No physics interaction by default** -- the CharacterController component does not participate in the rigidbody physics simulation. Custom code in `OnControllerColliderHit` callback is needed.
- **Discrete collision** -- Unity's CharacterController does not use continuous collision detection internally, relying on the skin width and step offset to prevent tunneling at normal speeds.

### Godot (CharacterBody3D)

Godot uses `move_and_slide()`:

- **Automatic slide decomposition** -- after collision, velocity is projected along the surface.
- **Floor snapping** (`floor_snap_length`): Keeps the character glued to the ground when walking downhill or over small bumps.
- **Floor constant speed** (`floor_constant_speed`): Normalizes speed regardless of slope angle.
- **No built-in step climbing** -- this is the biggest gap in Godot's character controller. Community workarounds exist but it remains an open proposal.
- **Max floor angle** (default 45 degrees).
- **Multiple collision iterations** -- `move_and_slide` internally iterates to resolve complex multi-surface contacts.
- **Platform detection** -- identifies moving platforms via floor velocity queries.

### Common Patterns Across All Engines

| Feature | Unreal | Unity | Godot | PhysX | Jolt |
|---|---|---|---|---|---|
| Approach | Kinematic sweep | Kinematic discrete | Kinematic sweep | Kinematic sweep | Kinematic sweep |
| Shape | Capsule | Capsule | Any convex | Capsule or Box | Any (capsule recommended) |
| Max slope | Configurable | 45 deg default | 45 deg default | Cosine-based | Configurable |
| Step climbing | Built-in (sweep up/fwd/down) | Built-in (step offset) | Not built-in | Built-in (step offset) | Built-in (WalkStairs) |
| Floor snapping | Yes | Via skin width | Yes (snap length) | Via contact offset | Yes (StickToFloor) |
| Dynamic interaction | Custom in callbacks | Custom in callbacks | Limited | Custom in callbacks | Callbacks + inner body |

**Universal takeaway:** Every major engine uses a kinematic sweep-based approach with a capsule shape. Step climbing and floor snapping are considered essential. Dynamic object interaction is always application-specific, handled through callbacks rather than automatic physics.

---

## 11. Integration Plan: AABB to Physics-Based

### Phase 1: Foundation (Non-breaking)

1. **Add Jolt to CMake** via FetchContent (already planned in `PHYSICS_ENGINE_RESEARCH.md`).
2. **Create `PhysicsWorld` subsystem** wrapping `JPH::PhysicsSystem`, `JobSystemThreadPool`, `BroadPhaseLayerInterface`, `ObjectLayerPairFilter`.
3. **Create `PhysicsCharacterController` class** wrapping `JPH::CharacterVirtual`.
4. **Keep `FirstPersonController` as the input/camera controller** -- it still handles WASD, mouse look, gamepad. It now outputs a desired velocity vector instead of directly modifying position.

### Phase 2: Shape Transition (AABB to Capsule)

**Current:** Player AABB is 0.6m wide x 1.7m tall.
**New:** Capsule with radius 0.3m and height 1.1m (total height with hemispheres = 1.1 + 2*0.3 = 1.7m). This matches the current dimensions.

The capsule's rounded bottom hemisphere naturally handles:
- Smooth traversal over small bumps and edges (no more catching on corners).
- Better stair climbing behavior.
- More realistic collision with angled surfaces.

### Phase 3: Movement Migration

Replace direct position manipulation with velocity-based control:

**Before (current):**
```
newPosition = currentPosition + direction * speed * deltaTime
applyCollision(newPosition, colliders)
camera.setPosition(newPosition)
```

**After (Jolt-based):**
```
desiredVelocity = direction * speed
characterVirtual.SetLinearVelocity(desiredVelocity + gravity)
characterVirtual.ExtendedUpdate(deltaTime, gravity, settings, ...)
camera.setPosition(characterVirtual.GetPosition() + eyeOffset)
```

### Phase 4: Feature Parity

Ensure the new controller matches or exceeds current behavior:

| Current Feature | New Implementation |
|---|---|
| Terrain following (walk mode) | `ExtendedUpdate()` with `StickToFloor()` |
| Slope limiting (50 deg) | `mMaxSlopeAngle = DegreesToRadians(50.0f)` |
| Height damping (smooth up/down) | Jolt handles this internally; adjust `mPenetrationRecoverySpeed` |
| Sprint speed | Multiply velocity before passing to Jolt |
| Fly mode | Set gravity to zero, allow vertical velocity from input |
| AABB colliders | Convert scene AABBs to Jolt `BoxShape` static bodies |

### Phase 5: New Features

Features that become possible with the physics-based controller:

- **Step climbing:** `WalkStairs()` via `ExtendedUpdate()`.
- **Proper wall sliding:** Jolt's collide-and-slide handles this automatically.
- **Crouching:** `SetShape()` to swap between standing and crouching capsules.
- **Jumping:** Apply upward velocity when grounded, let gravity pull back down.
- **Dynamic object pushing:** `SetInnerBodyShape()` + mass/strength configuration.
- **Moving platforms:** Query `GetGroundVelocity()` and add to character velocity.
- **Material-dependent footsteps:** `GetGroundMaterial()` for surface type queries.

### Preserving Movement Feel

Critical consideration: the current movement feel must not regress. Key factors:

1. **Input smoothing:** Jolt's sample uses `0.25 * new + 0.75 * old` velocity blending. Tune this to match current responsiveness.
2. **No momentum in walking:** The current controller has no inertia -- you stop immediately when releasing keys. Replicate this by setting velocity to zero (plus ground velocity) when no input is active, rather than decelerating.
3. **Mouse look unchanged:** The camera rotation system is independent of physics and should not change.
4. **Sprint feel:** Same multiplier (2x), just applied to velocity instead of position delta.
5. **Terrain following smoothness:** The current asymmetric damping (fast up, gentle down) should be replicated. Jolt's `StickToFloor()` provides the "gentle down" and `PenetrationRecoverySpeed` controls the "fast up."

### Migration Strategy

- Implement the new controller alongside the old one.
- Add a toggle (config flag or debug key) to switch between AABB and physics-based controllers at runtime.
- Compare behavior side-by-side until feature parity is achieved.
- Remove the old controller only after the new one is fully validated.

---

## Sources

- [PhysX 5.4.1 Character Controllers Documentation](https://nvidia-omniverse.github.io/PhysX/physx/5.4.1/docs/CharacterControllers.html)
- [PhysX 3.3.4 Character Controllers Documentation](https://docs.nvidia.com/gameworks/content/gameworkslibrary/physx/guide/3.3.4/Manual/CharacterControllers.html)
- [Jolt Physics CharacterVirtual API Reference](https://jrouwe.github.io/JoltPhysics/class_character_virtual.html)
- [Jolt Physics CharacterVirtualSettings API Reference](https://jrouwe.github.io/JoltPhysicsDocs/5.0.0/class_character_virtual_settings.html)
- [Jolt Physics CharacterVirtualTest Sample](https://github.com/jrouwe/JoltPhysics/blob/master/Samples/Tests/Character/CharacterVirtualTest.cpp)
- [Jolt Physics Architecture Documentation](https://github.com/jrouwe/JoltPhysics/blob/master/Docs/Architecture.md)
- [Jolt Physics GitHub Repository](https://github.com/jrouwe/JoltPhysics)
- [Jolt Physics Discussion: Kinematic Character Controller](https://github.com/jrouwe/JoltPhysics/discussions/853)
- [Jolt Physics Discussion: How to Fix my Character Controller](https://github.com/jrouwe/JoltPhysics/discussions/984)
- [Jolt Physics CMake HelloWorld Example](https://github.com/jrouwe/JoltPhysicsHelloWorld)
- [Bullet Physics btKinematicCharacterController Source](https://github.com/bulletphysics/bullet3/blob/master/src/BulletDynamics/Character/btKinematicCharacterController.cpp)
- [Bullet Physics Character Controller Manual](https://cuppajoeman.github.io/bullet-physics-manual/character-controller.html)
- [Bullet Forum: Should you use the kinematic character controller?](https://pybullet.org/Bullet/phpBB3/viewtopic.php?t=9358)
- [Bullet Forum: Character controller from scratch](https://pybullet.org/Bullet/phpBB3/viewtopic.php?t=8220)
- [GameDev.net: Character Controller - Rigidbody vs Kinematics](https://gamedev.net/forums/topic/696521-character-controller-rigidbody-vs-kinematics/)
- [GameDev.net: Character control from a Physics Engine perspective](https://gamedev.net/forums/topic/667953-character-control-from-a-physics-engine-perspective/)
- [DigitalRune: Character Controller Implementation](https://digitalrune.github.io/DigitalRune-Documentation/html/7cc27ced-9a65-4ddd-8b8e-fa817b7fe6b7.htm)
- [OpenKCC: Physics for Character Controllers](https://nickmaltbie.com/OpenKCC/docs/manual/kcc-design/physics-notes.html)
- [Collision Sliding Tech Breakdown (Little Polygon)](https://blog.littlepolygon.com/posts/sliding/)
- [Unreal Engine CharacterMovementComponent Documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/character-movement-component?application_version=4.27)
- [Unreal Engine: CharacterMovementComponent Architecture Tutorial](https://dev.epicgames.com/community/learning/tutorials/76ad/unreal-engine-character-movement-component-architecture)
- [Unity CharacterController Manual](https://docs.unity3d.com/560/Documentation/Manual/class-CharacterController.html)
- [Unity: Dynamic Rigidbody Interactions (Character Controller Package)](https://docs.unity3d.com/Packages/com.unity.charactercontroller@1.0/manual/dynamic-body-interaction.html)
- [Unity: Slope Management (Character Controller Package)](https://docs.unity3d.com/Packages/com.unity.charactercontroller@1.0/manual/slope-management.html)
- [Godot CharacterBody3D Documentation](https://docs.godotengine.org/en/stable/classes/class_characterbody3d.html)
- [Godot CharacterBody3D Source Code](https://github.com/godotengine/godot/blob/master/scene/3d/physics/character_body_3d.cpp)
- [Godot Proposal: Automatic Stair Step-Up/Step-Down](https://github.com/godotengine/godot-proposals/issues/2751)
- [Godot Community: Stair-Stepping Implementations](https://github.com/Andicraft/stairs-character)
- [Getting Started with Jolt Physics (Jan Wedekind)](https://www.wedesoft.de/simulation/2024/09/26/jolt-physics-engine/)
- [Jolt Physics on Conan Package Manager](https://conan.io/center/recipes/joltphysics)
- [Custom Character Controller in Unity (Roystan Ross)](https://roystanross.wordpress.com/2014/05/07/custom-character-controller-in-unity-part-1-collision-resolution/)
