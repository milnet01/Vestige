# Physics Engine Research: Bullet Physics vs Jolt Physics

**Date:** 2026-03-31
**Purpose:** Evaluate physics engine options for integration into the Vestige 3D Engine (C++17/OpenGL 4.5)

---

## 1. Architecture and API Design

### Jolt Physics

- **Language standard:** C++17 (exact match for Vestige). Depends only on the C++ standard library.
- **No RTTI, no exceptions** -- lightweight and compatible with game engine compilation flags.
- **Settings-based API:** Bodies are created via settings objects (`BodyCreationSettings`), then added to the physics system through a `BodyInterface`. Clean, predictable pattern.
- **Thread-safe body interface:** Comes in two flavors -- a locking variant (mutex-protected) and a non-locking variant for when you manage synchronization yourself.
- **Custom memory allocator support:** All internal allocations can be routed through user-provided `Allocate`, `Free`, `AlignedAllocate`, and `AlignedFree` functions.
- **Consistent, modern codebase:** Erwin Coumans (Bullet's creator) himself stated that Jolt uses "similar algorithms but better optimized, more consistent code, better documentation and solves some edge cases."

### Bullet Physics

- **Language standard:** Written in C++ but uses pre-C++11 patterns extensively. Manual `new`/`delete`, raw pointers throughout, no smart pointers or modern RAII patterns.
- **Verbose API:** Requires creating a collision configuration, dispatcher, broadphase, solver, and dynamics world separately -- many more boilerplate objects than Jolt.
- **API criticism:** Documented complaints about unnecessary indirections, fragile API between versions, and inconsistent feature exposure. Forum threads dating back years describe the API as "ugly" with calls for refactoring.
- **Documentation:** Notoriously poor. A 2025 forum thread titled "After 15 years of people asking -- Why is Bullet documentation still so very bad?" captures the community sentiment.

**Verdict:** Jolt has a significantly more modern, cleaner API that aligns naturally with C++17 practices. Bullet's API feels dated and requires more boilerplate.

---

## 2. Feature Set

| Feature | Jolt Physics | Bullet Physics |
|---|---|---|
| **Rigid bodies** | Static, dynamic, kinematic | Static, dynamic, kinematic |
| **Collision shapes** | Box, sphere, capsule, cylinder, convex hull, triangle mesh, heightfield, compound, scaled, offset, rotated translated | Box, sphere, capsule, cylinder, cone, convex hull, triangle mesh, heightfield, compound, multi-sphere |
| **Constraints** | Fixed, point, hinge, slider, cone twist, 6-DOF, distance, path, rack and pinion, gear, pulley, spring (via motor/soft limits) | Point-to-point, hinge, slider, cone twist, 6-DOF, 6-DOF spring, gear, fixed |
| **Character controller** | Two variants: `Character` (rigid body-based) and `CharacterVirtual` (kinematic, no physics body) | `btKinematicCharacterController` (basic, known to have edge-case issues) |
| **Soft bodies** | Yes -- XPBD-based. Edge constraints, bend constraints, dihedral bend constraints, skinning constraints. Suitable for cloth, ropes, inflatable objects, vegetation. | Yes -- position-based dynamics. Nodes + links (springs). Cloth, rope, deformable volumes. Anchoring, tearing, self-collision. |
| **Vehicles** | Wheeled and tracked vehicle controllers | `btRaycastVehicle` (basic) |
| **Ragdolls** | Built-in ragdoll support with constraint-based skeletons | Possible via manual constraint setup |
| **Double precision** | Supported (5-10% performance penalty) | Limited support |
| **Convex radius** | Shapes use convex radius for performance (rounded collision) | No equivalent concept |

**Verdict:** Both cover the core feature set well. Jolt has more refined character controllers and built-in vehicle/ragdoll support. Bullet has a longer track record with soft bodies but Jolt's XPBD-based soft bodies are more modern. For the Vestige engine's architectural walkthrough focus, both are more than sufficient.

---

## 3. Performance

### Jolt Physics

- **Multithreading:** Designed from the ground up for multi-core. Uses a job system (`JobSystem`) that distributes work across threads. Collision and constraint solving are parallelized via a greedy grouping algorithm that identifies independent body groups.
- **Scaling:** Scales well up to ~16 CPU cores, after which memory bus and atomic operations become bottlenecks (especially across AMD CCX boundaries). For a Ryzen 5 5600 (6 cores/12 threads), this is an ideal fit.
- **Guerrilla Games benchmark (GDC 2022):** Switching from a commercial physics engine to Jolt for Horizon Forbidden West resulted in:
  - Reduced memory usage
  - Smaller executable size
  - **Doubled simulation frequency** while using **less CPU time**
- **Godot 4.4 benchmarks:** Jolt maintained ~179 FPS up to 2,500 bodies. At 7,500 bodies, Jolt still held 179 FPS while a competing engine (Avian/Bevy) dropped to 34 FPS -- a 5.2x performance gap.
- **Background streaming:** Supports loading/unloading physics sections in the background, preparing batches of bodies on a background thread without locking the simulation.

### Bullet Physics

- **Multithreading:** Has some multithreading support but it was retrofitted, not designed-in. Less effective thread utilization than Jolt.
- **Single-threaded performance:** Historically decent for its era, but has not seen optimization work in years.
- **No recent benchmarks:** The last meaningful performance work was done years ago. No recent (2024+) benchmark data available.
- **OpenCL GPU acceleration:** Bullet had experimental GPU rigid body pipeline support via OpenCL, but this was never production-ready and development stalled.

**Verdict:** Jolt is decisively faster and more efficient, especially on multi-core hardware. The Guerrilla Games case study is compelling real-world proof. For targeting 60+ FPS, Jolt is the clear winner.

---

## 4. Cloth / Soft Body Simulation

### Jolt Physics

- **Soft body system:** Uses XPBD (Extended Position Based Dynamics) for constraint solving between vertices.
- **Constraint types for cloth:**
  - Edge constraints (structural -- maintain shape)
  - Bend constraints (prevent folding)
  - Dihedral bend constraints (prevent unrealistic cloth folding)
  - Skinning constraints (drive soft body with character animation)
- **Real-world usage:** Wicked Engine developer reported ~3ms physics simulation for 10 curtains + a monkey head + 4 kinematic ragdolls in Sponza -- acceptable for real-time.
- **Godot integration:** SoftBody3D support via Godot Jolt confirmed working.

### Bullet Physics

- **Soft body system:** Longer-established, uses position-based dynamics.
- **Features:** Anchoring/pinning, tearing, self-collision, clusters for collision handling, cloth + rope + deformable volumes.
- **Material control:** Angular-stiffness, linear-stiffness coefficients for fine-tuning.
- **Integration with DCC tools:** Used in Blender, Maya (Autodesk), Cinema 4D for cloth simulation.

### Recommended Approach for Vestige

For an architectural walkthrough engine, cloth simulation would primarily be used for:
- Curtains and drapes in the Tabernacle/Temple
- Fabric coverings and tent materials
- Priestly garments (if character models are added)

Either engine handles this. Jolt's XPBD approach is more modern and its skinning constraints would integrate well with the existing animation system being developed (Phase 7). The Wicked Engine developer's curtain performance numbers are directly relevant to Vestige's use case.

**Verdict:** Both support cloth. Jolt's approach is more modern (XPBD) and has proven real-time performance for exactly the kind of scenes Vestige needs. Bullet has a longer track record but less active development.

---

## 5. Community and Maintenance

### Jolt Physics

- **Primary author:** Jorrit Rouwe, Lead Game Tech Programmer at Guerrilla Games.
- **Active development:** Continuous commits through 2025-2026. Recent work includes Visual Studio 2026 support, 6-DOF constraint fixes, macOS compilation fixes, memory leak fixes.
- **GitHub stars:** ~7k+ (reached 4k in October 2023, growing rapidly).
- **Release cadence:** Regular versioned releases (5.0.0, 5.1.0, etc.).
- **Documentation:** Architecture documentation, Doxygen API docs, HelloWorld example, CMake FetchContent example, extensive sample applications covering every feature. Every feature has its own sample.
- **Community:** Active GitHub Discussions, growing ecosystem of bindings (C, JavaScript/WASM, Java, Rust).
- **Godot integration:** Officially integrated into Godot 4.4 (March 2025), which dramatically increased community exposure and testing.

### Bullet Physics

- **Primary author:** Erwin Coumans (now at NVIDIA, previously Google). Focus has shifted to robotics/PyBullet.
- **Development status:** Effectively stalled for game use. Last tagged release (3.25) was April 2022. Sporadic commits but no meaningful feature development. Community perception: "abandoned" for game development purposes.
- **GitHub stars:** ~12.2k (higher due to age and historical significance, but growth has plateaued).
- **Documentation:** Notoriously poor. Community has complained for 15+ years. No comprehensive tutorials or guides.
- **Community:** Forum still active but largely for PyBullet/robotics. Game development discussions have migrated elsewhere.
- **Godot:** Godot removed Bullet as default physics in Godot 4.0, replaced with GodotPhysics, then added Jolt in 4.4.

**Verdict:** Jolt is actively maintained by a AAA game developer with a growing community. Bullet is effectively in maintenance mode for game development. This is perhaps the single most important differentiator for a long-term engine project.

---

## 6. License

| | Jolt Physics | Bullet Physics |
|---|---|---|
| **License** | MIT | zlib |
| **Commercial use** | Yes, unrestricted | Yes, unrestricted |
| **Attribution required** | Technically yes (retain copyright notice in source) | No (not even documentation attribution needed) |
| **Redistribution** | Permitted | Permitted |
| **Modification** | Permitted | Permitted |

**Verdict:** Both are fully permissive and suitable for commercial indie development and future Steam distribution. MIT is marginally more restrictive (requires keeping the copyright notice) but this is trivially satisfied. No practical difference for Vestige.

---

## 7. Build System

### Jolt Physics

- **Build system:** CMake 3.20+
- **C++ standard:** C++17 (set in CMakeLists.txt)
- **Integration method:** CMake FetchContent (recommended), or add as subdirectory. Official `JoltPhysicsHelloWorld` repo demonstrates CMake integration.
- **Header-only:** No -- compiles as a static/shared library, then linked.
- **Dependencies:** None beyond the C++ standard library.
- **Platforms:** Windows, Linux, macOS, Android, iOS, WebAssembly.
- **Compiler support:** MSVC, GCC, Clang. Recent VS2026 support added.

### Bullet Physics

- **Build system:** CMake
- **C++ standard:** No specific C++17 requirement. Compiles with older standards.
- **Integration method:** CMake subdirectory or install + find_package.
- **Header-only:** No -- compiles as multiple libraries (BulletDynamics, BulletCollision, LinearMath, BulletSoftBody).
- **Dependencies:** None for core (optional: OpenGL for demos, OpenCL for GPU).
- **Platforms:** Windows, Linux, macOS, Android, iOS.

**Verdict:** Jolt's CMake integration is cleaner and more modern. The official FetchContent example makes integration straightforward. Bullet works but produces multiple library targets that need to be linked individually.

---

## 8. Notable Users

### Jolt Physics -- Shipped Games and Engines

**AAA Games:**
- Horizon Forbidden West (Guerrilla Games / PlayStation)
- Death Stranding 2: On the Beach (Kojima Productions)
- Blood Line: A Rebel Moon Game
- X4 Foundations

**Game Engines:**
- Godot Engine (integrated in 4.4, March 2025)
- Unreal Engine 5 (via Unreal Jolt plugin)
- Wicked Engine
- ezEngine
- NeoAxis Engine
- Nazara Engine
- Supernova Engine
- Traktor Engine
- luxe engine

**Other:**
- VPhysics-Jolt (Source Engine replacement)
- OpenMW (replacing Bullet with Jolt)
- Light Tracer Render

### Bullet Physics -- Shipped Games and Engines

**AAA Games:**
- Grand Theft Auto IV and V (via Rockstar RAGE engine, co-developed with Rockstar)
- Red Dead Redemption
- Rocket League
- DiRT series

**Engines and Applications:**
- Blender (built-in physics)
- Godot 3.x (removed in 4.0)
- Houdini
- Cinema 4D
- LightWave 3D
- Maya (Autodesk)
- Poser
- ROS (robotics)

**Movies (visual effects):**
- Bullet has been used for VFX in multiple Hollywood films (specific titles under NDA).

**Verdict:** Bullet has a longer legacy with iconic titles (GTA, Rocket League). Jolt has rapidly captured the modern game engine market and is now the physics engine behind current AAA PlayStation exclusives. The trajectory strongly favors Jolt.

---

## Summary Comparison

| Criteria | Jolt Physics | Bullet Physics | Winner |
|---|---|---|---|
| C++17 compatibility | Native C++17 | Pre-C++11 patterns | **Jolt** |
| API cleanliness | Modern, settings-based, consistent | Verbose, boilerplate-heavy | **Jolt** |
| Rigid body features | Complete | Complete | Tie |
| Constraints | Full set + motors + springs | Full set | **Jolt** |
| Collision shapes | Full set + convex radius optimization | Full set | **Jolt** |
| Character controller | Two variants (rigid + virtual) | Basic, known issues | **Jolt** |
| Soft body / cloth | XPBD, modern | PBD, established | **Jolt** (slight) |
| Multithreading | Designed-in, job system | Retrofitted, limited | **Jolt** |
| Performance | Proven AAA, doubled sim frequency | Adequate but dated | **Jolt** |
| Memory efficiency | Custom allocators, proven savings | Standard allocation | **Jolt** |
| Documentation | Good -- Doxygen, samples, HelloWorld | Poor -- 15+ year complaint | **Jolt** |
| Active development | Yes -- continuous 2025-2026 | Stalled since ~2022 | **Jolt** |
| Community momentum | Growing rapidly (Godot, Unreal) | Declining for games | **Jolt** |
| License | MIT | zlib | Tie |
| CMake integration | FetchContent, single target | Multiple library targets | **Jolt** |
| Shipped AAA titles | Horizon FW, Death Stranding 2 | GTA, Rocket League | Tie (different eras) |

---

## Recommendation for Vestige

**Use Jolt Physics.** The case is overwhelming:

1. **C++17 native** -- matches Vestige's language standard exactly. No wrapping legacy code.
2. **CMake FetchContent** -- clean integration into the existing CMake build system.
3. **Performance proven on similar hardware** -- Jolt scales well up to 16 cores; the Ryzen 5 5600 with 6 cores/12 threads is in the sweet spot. The 60 FPS target is easily achievable.
4. **Actively maintained** -- by a AAA lead programmer at Guerrilla Games. Bugs get fixed. Features get added.
5. **Cloth/soft body via XPBD** -- directly applicable to Tabernacle curtains, tent coverings, and temple fabrics.
6. **Character controller** -- `CharacterVirtual` is ideal for a first-person walkthrough engine (no physics body needed for the player, just collision queries).
7. **Background streaming** -- supports loading/unloading physics sections without locking the simulation, which is relevant for large architectural scenes.
8. **Growing ecosystem** -- Godot 4.4 integration means the library gets tested by thousands of developers, catching bugs faster.
9. **MIT license** -- compatible with future Steam distribution and commercial use.
10. **The Bullet creator himself endorses Jolt** -- Erwin Coumans publicly stated Jolt is better optimized with more consistent code.

### Integration Plan (High Level)

1. Add Jolt via CMake FetchContent in `engine/CMakeLists.txt`
2. Create a `PhysicsWorld` subsystem wrapping `JPH::PhysicsSystem`
3. Implement a `JobSystemThreadPool` with thread count matching available cores
4. Add collision shapes as components on scene entities
5. Use `CharacterVirtual` for the first-person camera/player
6. Later: soft bodies for fabric/cloth in Tabernacle scenes

---

## Sources

- [Jolt Physics GitHub Repository](https://github.com/jrouwe/JoltPhysics)
- [Bullet Physics GitHub Repository](https://github.com/bulletphysics/bullet3)
- [Architecture of Jolt Physics](https://jrouwe.github.io/JoltPhysics/)
- [Jolt Physics API Documentation v5.0.0](https://jrouwe.github.io/JoltPhysicsDocs/5.0.0/index.html)
- [Architecting Jolt Physics for Horizon Forbidden West (GDC 2022)](https://www.guerrilla-games.com/read/architecting-jolt-physics-for-horizon-forbidden-west)
- [Architecting Jolt Physics for Horizon Forbidden West (Slides)](https://jrouwe.nl/architectingjolt/)
- [Jolt Physics Multicore Scaling (PDF)](https://jrouwe.nl/jolt/JoltPhysicsMulticoreScaling.pdf)
- [Jolt vs PhysX Discussion](https://github.com/jrouwe/JoltPhysics/discussions/327)
- [How Similar Is This to Bullet? -- Jolt Discussion](https://github.com/jrouwe/JoltPhysics/discussions/1234)
- [Erwin Coumans on Jolt vs Bullet](https://x.com/erwincoumans/status/1496513233146068992)
- [JoltPhysics HelloWorld CMake Example](https://github.com/jrouwe/JoltPhysicsHelloWorld)
- [Jolt Physics Performance Test](https://github.com/jrouwe/JoltPhysics/blob/master/Docs/PerformanceTest.md)
- [Projects Using Jolt](https://jrouwe.github.io/JoltPhysics/md__docs_2_projects_using_jolt.html)
- [Godot 4.4 Jolt Integration](https://docs.godotengine.org/en/latest/tutorials/physics/using_jolt_physics.html)
- [Godot 4.4 Release Announcement](https://www.devclass.com/development/2025/03/05/godot-44-released-open-source-game-engine-adds-jolt-physics-net-8-and-more/1627701)
- [Jolt Soft Body Physics (DeepWiki)](https://deepwiki.com/jrouwe/JoltPhysics/3.2-soft-body-physics)
- [Godot Jolt Soft Body Support](https://80.lv/articles/godot-jolt-now-supports-soft-bodies-in-godot-4-3)
- [Wicked Engine Curtain Physics (Jolt)](https://x.com/turanszkij/status/1805979390557528217)
- [Bullet Physics Wikipedia](https://en.wikipedia.org/wiki/Bullet_(software))
- [Bullet Documentation Complaints (Forum)](https://pybullet.org/Bullet/phpBB3/viewtopic.php?t=13081)
- [Bullet API Design Issues (GitHub)](https://github.com/bulletphysics/bullet3/issues/2198)
- [Physics Engine Benchmarks: Jolt vs Avian](https://github.com/Kaylebor/physics-engine-benchmarks)
- [Picking a Physics Engine for Thrive](https://forum.revolutionarygamesstudio.com/t/picking-a-physics-engine-for-thrive/1031)
- [Jolt Physics License (MIT)](https://github.com/jrouwe/JoltPhysics/blob/master/LICENSE)
- [Bullet Physics License (zlib)](https://github.com/bulletphysics/bullet3/blob/master/LICENSE.txt)
- [OpenMW Jolt Integration MR](https://gitlab.com/OpenMW/openmw/-/merge_requests/3919)
- [Jolt Build Instructions](https://jrouwe.github.io/JoltPhysics/md__build_2_r_e_a_d_m_e.html)
- [Getting Started with Jolt (Jan Wedekind)](https://www.wedesoft.de/simulation/2024/09/26/jolt-physics-engine/)
- [Godot Proposals: Investigate Jolt](https://github.com/godotengine/godot-proposals/discussions/5161)
- [Horizon Forbidden West GDC Coverage (Game Developer)](https://www.gamedeveloper.com/marketing/-horizon-forbidden-west-gdc-session-covers-studio-s-switch-to-open-source-physics-engine)
