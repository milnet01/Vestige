# Cloth Wind Behavior and Return-to-Rest Research

**Date:** 2026-03-31
**Purpose:** Deep-dive research into how fabric responds to wind, how cloth simulations handle the "return to rest" problem, common bugs that prevent settling, frame-rate independence via fixed timestep accumulators, realistic damping values, and how major game engines handle cloth settling.

---

## Table of Contents

1. [How Real Fabric Curtains Behave in Wind](#1-how-real-fabric-curtains-behave-in-wind)
2. [Return-to-Rest Forces in Cloth Simulation](#2-return-to-rest-forces-in-cloth-simulation)
3. [Common Bugs: Fabric That Never Settles](#3-common-bugs-fabric-that-never-settles)
4. [Frame-Rate Independence: Fixed Timestep Accumulator](#4-frame-rate-independence-fixed-timestep-accumulator)
5. [Realistic Damping Values for Fabric](#5-realistic-damping-values-for-fabric)
6. [How Game Engines Handle Cloth Settling](#6-how-game-engines-handle-cloth-settling)
7. [Recommendations for Vestige](#7-recommendations-for-vestige)

---

## 1. How Real Fabric Curtains Behave in Wind

### The Physics of Wind on Fabric

Wind displaces fabric through **aerodynamic pressure**. Air moves from high-pressure to low-pressure regions. When wind hits a curtain, the air pressure on the windward face exceeds the pressure on the lee side, creating a net force that pushes the fabric. The force on each surface element depends on:

- **Angle of incidence**: The dot product between the wind direction and the fabric surface normal determines force magnitude. A face parallel to the wind receives no force; a face perpendicular receives maximum force.
- **Wind speed**: Force scales with the square of wind velocity (drag equation: F = 0.5 * rho * v^2 * Cd * A).
- **Fabric porosity**: Loosely woven fabrics (sheer, lace) let air pass through, reducing effective force. Dense fabrics (blackout, velvet) catch more wind.
- **Surface area**: Longer and wider panels present more area to the wind.

### Real-World Fabric Weights (GSM)

| Fabric Type | Weight (GSM) | Wind Response |
|---|---|---|
| Sheer/voile | 20-60 | Billows dramatically in light breeze; lifts easily |
| Lightweight cotton | 80-150 | Sways in moderate breeze; flutters at edges |
| Medium linen | 150-220 | Responds to moderate wind; folds move but fabric stays mostly vertical |
| Heavy cotton/linen | 220-400 | Resists light wind; moves in strong gusts only |
| Velvet/blackout | 300-500+ | Nearly stationary in light wind; slow, heavy movement in strong wind |

Source: [Fabric Weight for Curtains Guide](https://sewingtrip.com/fabric-weight-for-curtains/)

### The Displacement-and-Return Cycle

When wind blows on a hanging curtain:

1. **Displacement phase**: Wind pressure pushes the fabric outward from its hanging position. Lightweight fabric billows; heavy fabric only leans slightly. The bottom and free edges displace most because they are unconstrained.
2. **Peak displacement**: The fabric reaches a maximum displacement angle determined by the balance between wind force (pushing out) and gravity + fabric tension (pulling back down). Heavier fabrics reach smaller angles.
3. **Wind stops -- gravity restores**: When wind ceases, gravity pulls the displaced mass downward. The fabric swings back like a pendulum.
4. **Overshoot and oscillation**: The fabric swings past its rest position (undershoot), then back, oscillating. This is an **underdamped response** -- the same physics as a damped pendulum.
5. **Settling**: Air resistance (both from the surrounding air and the fabric's internal friction) dissipates energy each swing. Amplitude decays exponentially. Light fabrics oscillate more times before settling; heavy fabrics settle faster because gravity dominates the restoring force and internal friction is higher.

### Key Observation for Simulation

The "return to rest" is NOT a special force -- it is simply **gravity** pulling the mass back to equilibrium combined with **damping** (air drag + internal material friction) removing kinetic energy each oscillation. If either gravity or damping is insufficient in a simulation, the fabric will either float at its displaced position or oscillate forever.

Source: [Pendulum Damping Physics (UCSB)](https://web.physics.ucsb.edu/~lecturedemonstrations/Composer/Pages/40.37.html), [Curtain Weight Guide (Kgorge)](https://www.kgorge.com/blogs/user-guides/diy-windproof-outdoor-curtain-weights)

---

## 2. Return-to-Rest Forces in Cloth Simulation

### The Three Forces That Bring Cloth Back

In every cloth simulation (mass-spring or PBD/XPBD), three mechanisms work together to return cloth to its rest pose when external forces (wind) stop:

#### A. Gravity (the primary restoring force)

Gravity constantly pulls every particle downward. For a hanging cloth pinned at the top, gravity is the **dominant restoring force** -- it is what creates the natural drape in the first place. When wind pushes the cloth sideways and then stops, gravity pulls the displaced mass back down toward the lowest-energy hanging configuration.

Without gravity (zero-g simulation), cloth would have no reason to return to any particular pose.

#### B. Constraint Forces / Spring Forces (shape maintenance)

**In mass-spring systems**: Springs connect adjacent particles with a rest length. When particles are displaced, the spring force (Hooke's law: F = -k * (|x| - rest_length) * direction) pulls them back toward the configuration where all springs are at their rest lengths. The combination of structural, shear, and bending springs defines the cloth's "preferred" shape.

**In PBD/XPBD**: Distance constraints replace springs. Each constraint stores the **rest length** between two particles. The constraint solver projects particles back toward positions that satisfy these rest lengths. The constraint itself IS the restoring force -- particles cannot drift apart permanently because the solver corrects them every frame.

The rest lengths are set during initialization from the mesh's initial configuration (the "rest shape"). The cloth always tries to return to this configuration.

#### C. Damping (energy dissipation)

Damping removes kinetic energy from the system so that oscillations decay and the cloth eventually stops moving. Without damping, a cloth displaced by wind would swing back and forth forever (like an ideal frictionless pendulum).

In real fabric, damping comes from:
- **Air drag**: Resistance from surrounding air molecules
- **Internal friction**: Fiber-on-fiber friction within the fabric weave
- **Viscoelastic material behavior**: The fabric itself absorbs energy during deformation

### How Damping Is Implemented

**Verlet integration (position-based) damping:**
```
new_pos = current_pos + (current_pos - old_pos) * (1.0 - DAMPING) + acceleration * dt * dt
```
The `(1.0 - DAMPING)` factor multiplies the velocity term each frame. A DAMPING value of 0.01 means 1% of velocity is lost per step. This causes exponential decay of kinetic energy.

Source: [Pikuma Verlet Integration Tutorial](https://pikuma.com/blog/verlet-integration-2d-cloth-physics-simulation), [Mosegaard Cloth Tutorial](https://viscomp.alexandra.dk/index2fa7.html?p=147)

**Explicit spring damping:**
```
F_damping = -kd * (v_relative dot direction) * direction
```
A dashpot-style damping force that opposes relative motion along each spring. The damping coefficient kd controls how quickly oscillations along that spring die out.

Source: [Stanford Cloth Tutorial (Matt Fisher)](https://graphics.stanford.edu/~mdfisher/cloth.html)

**XPBD Rayleigh damping (velocity-dependent constraint damping):**
```
alphaHat = compliance / (dt * dt)
betaHat  = damping * (dt * dt)
gamma    = (alphaHat * betaHat) / dt
```
The gamma term is added to the denominator of the constraint correction, introducing velocity-dependent resistance. This is Rayleigh damping integrated into the constraint solver itself. Importantly, **this damping only works when compliance > 0** -- constraints with zero compliance (infinite stiffness) cannot be damped this way.

Source: [Bullet Physics Forum - XPBD Damping](https://pybullet.org/Bullet/phpBB3/viewtopic.php?t=13009), [XPBD Paper (Macklin et al., 2016)](https://matthias-research.github.io/pages/publications/XPBD.pdf)

### Shape Matching (an alternative restoring mechanism)

Shape matching computes the best-fit rigid transformation from the rest configuration to the current deformed state, then pulls particles toward the transformed rest positions. This provides a direct "return to original shape" force that can be combined with constraint-based methods. It is unconditionally stable and eliminates the overshooting problem of explicit integration.

Source: [Multi-Resolution Shape Matching for Cloth (RWTH Aachen)](https://animation.rwth-aachen.de/media/papers/2013-CAG-MultiResShapeMatching.pdf)

---

## 3. Common Bugs: Fabric That Never Settles

### Bug 1: Missing or Insufficient Damping

**Symptom**: Cloth swings back and forth forever after wind stops. Oscillations never decay.

**Cause**: Verlet integration with DAMPING = 0.0, or spring systems with kd = 0. Without energy dissipation, the system conserves energy perfectly and oscillates indefinitely.

**Fix**: Add damping. Even a small value (0.01-0.05 in Verlet) is enough to cause settling within a few seconds.

Source: [Blender Bug #73057 - Cloth behaves like jelly](https://developer.blender.org/T73057)

### Bug 2: Velocity Damping Removed in Engine Rewrite

**Symptom**: Cloth worked fine in engine version N, bounces forever in version N+1.

**Cause**: This happened in Blender's cloth rewrite -- velocity damping was removed and not replaced. The closest substitute is **Air Viscosity**.

**Fix**: Increase Air Viscosity (Blender uses a value of ~8) or explicitly add velocity damping back. A quality steps increase from 5 to 15 combined with damping from 0.0 to 0.2 solved the jello-like behavior.

Source: [Blender Bug #73057](https://developer.blender.org/T73057)

### Bug 3: Energy Gain from Numerical Integration

**Symptom**: Cloth slowly gains energy over time, oscillating more and more until it explodes.

**Cause**: Explicit Euler integration (and sometimes symplectic Euler with large timesteps) can add energy to the system rather than conserving it. This is called **numerical energy gain** or **numerical instability**. The larger the timestep, the worse this becomes.

**Fix**: Use Verlet integration (4th-order energy conservation) or implicit integration (naturally energy-dissipating). PBD/XPBD avoids this entirely by working at the position level -- constraint projection cannot add energy.

Source: [Position-Based Dynamics (Muller et al.)](https://matthias-research.github.io/pages/publications/posBasedDyn.pdf)

### Bug 4: Timestep-Dependent Stiffness (PBD-specific)

**Symptom**: Cloth is stiff at 30 FPS but floppy at 120 FPS, or vice versa. The "rest shape" changes depending on frame rate.

**Cause**: In standard PBD, constraint stiffness depends on both the iteration count and the timestep size. Changing either changes the effective material properties.

**Fix**: Use XPBD, which introduces compliance (inverse stiffness) scaled by dt^2: `alpha_tilde = alpha / dt^2`. This makes material stiffness independent of both iteration count and timestep.

Source: [XPBD Paper (Macklin et al., 2016)](https://matthias-research.github.io/pages/publications/XPBD.pdf), [XPBD DeepWiki](https://deepwiki.com/openxrlab/xrtailor/4.2-extended-position-based-dynamics-(xpbd))

### Bug 5: Insufficient Solver Iterations

**Symptom**: Cloth sags unrealistically, looks like jello, constraints appear to not hold.

**Cause**: PBD requires multiple Gauss-Seidel iterations to converge. With too few iterations, distance constraints are only partially enforced -- the cloth stretches beyond its rest lengths and never recovers.

**Fix**: Increase iteration count, or (better) use the **substep approach** from Macklin's "Small Steps in Physics Simulation" -- split each visual timestep into n substeps with 1 iteration each. This converges faster than n iterations with 1 substep and is more parallelizable.

Source: [Small Steps in Physics Simulation (Macklin, 2019)](https://mmacklin.com/smallsteps.pdf)

### Bug 6: Pinned Vertices Create Dead Zones

**Symptom**: Particles adjacent to pinned points don't respond to gravity or wind -- they stay frozen in their initial positions.

**Cause**: Pinned particles have infinite mass (inverse mass = 0). Constraints connecting a free particle to a pinned particle apply all correction to the free particle, but if the free particle is also constrained by other nearby pinned particles, the corrections can cancel out, leaving it stuck.

**Fix**: Use fewer pin points, increase the distance between pins, or add a small compliance to pin constraints to allow slight movement.

Source: [Blender Bug #115325 - Cloth physics stuck](https://projects.blender.org/blender/blender/issues/115325)

### Bug 7: Solver/Render Frequency Mismatch Causes Pulsing

**Symptom**: Cloth "pulses" or "breathes" rhythmically, especially on animated characters.

**Cause**: The cloth solver and the animated mesh update are out of sync or produce numeric instability, so the cloth over-corrects each frame and oscillates.

**Fix**: Increase quality steps (substeps), lower structural/bending stiffness, and increase damping.

Source: [Quora - Cloth Pulsing on Rigged Character](https://www.quora.com/Why-does-cloth-keep-pulsing-when-simulating-on-rigged-character-cloth-simulation-blender-3D)

---

## 4. Frame-Rate Independence: Fixed Timestep Accumulator

### The Problem

If cloth simulation uses the variable frame delta time directly (`dt = time_since_last_frame`), behavior changes with frame rate:
- At high FPS (small dt): springs may become too stiff (PBD) or too loose (explicit integration)
- At low FPS (large dt): springs can explode, particles tunnel through collision geometry, energy gain causes instability
- Frame drops: a single large dt can cause catastrophic failures

Source: [Fix Your Timestep (Gaffer on Games)](https://gafferongames.com/post/fix_your_timestep/)

### The Fixed Timestep Accumulator Pattern

The standard solution used across the game industry:

```cpp
const float PHYSICS_DT = 1.0f / 120.0f;  // Fixed physics step: 120 Hz
const int MAX_SUBSTEPS = 8;               // Safety cap to prevent spiral of death

float accumulator = 0.0f;

void update(float frameDeltaTime)
{
    // Cap frame time to prevent spiral of death
    if (frameDeltaTime > 0.25f)
        frameDeltaTime = 0.25f;

    accumulator += frameDeltaTime;

    int steps = 0;
    while (accumulator >= PHYSICS_DT && steps < MAX_SUBSTEPS)
    {
        simulateCloth(PHYSICS_DT);
        accumulator -= PHYSICS_DT;
        steps++;
    }

    // Leftover accumulator carries over to next frame
    // Optional: interpolate rendering between last two physics states
    float alpha = accumulator / PHYSICS_DT;
    renderState = lerp(previousPhysicsState, currentPhysicsState, alpha);
}
```

### Key Properties

1. **The renderer produces time, the simulation consumes it** in fixed-size chunks.
2. **Leftover time is not discarded** -- it remains in the accumulator and is consumed on the next frame.
3. **Alpha interpolation** between the previous and current physics states eliminates visual stutter at the cost of one frame of latency.
4. **Spiral of death protection**: if the simulation cannot keep up (physics cost exceeds real time), cap either the accumulator or the substep count to prevent the accumulator growing without bound.
5. **Deterministic**: the same sequence of inputs always produces the same simulation result regardless of rendering frame rate.

### Choosing the Physics Rate

| Rate | dt (seconds) | Use Case |
|---|---|---|
| 60 Hz | 0.01667 | Minimum acceptable for cloth; may need higher for stiff constraints |
| 120 Hz | 0.00833 | Good balance of stability and performance for cloth |
| 240 Hz | 0.00417 | Very stable, required for stiff springs with explicit integration |
| 300 Hz | 0.00333 | NvCloth default solver frequency |

XPBD with substeps can often use lower rates (60-120 Hz) because the implicit nature of the solver handles stiff constraints without small timesteps.

Source: [Fix Your Timestep (Gaffer on Games)](https://gafferongames.com/post/fix_your_timestep/), [NvCloth User Guide](https://archive.docs.nvidia.com/gameworks/content/gameworkslibrary/physx/nvCloth/UsersGuide/Index.html)

### XPBD Substep Approach (Alternative to Accumulator)

Macklin's "Small Steps" paper proposes splitting each visual frame into n substeps with 1 solver iteration per substep, rather than using a separate accumulator loop. This produces equivalent results to n iterations but converges faster and enables easier parallelism:

```
for each visual frame:
    sub_dt = frame_dt / num_substeps
    for s = 0 to num_substeps-1:
        predict positions (gravity, external forces)
        solve constraints (1 iteration with compliance scaled by sub_dt)
        update velocities from position delta
```

Typical substep counts: 4 substeps (real-time/fast), 10 substeps (quality), up to 200 (offline/high-fidelity).

Source: [Small Steps in Physics Simulation (Macklin, 2019)](https://mmacklin.com/smallsteps.pdf), [XRTailor XPBD Documentation](https://deepwiki.com/openxrlab/xrtailor/4.2-extended-position-based-dynamics-(xpbd))

---

## 5. Realistic Damping Values for Fabric

### Verlet Integration Damping Factor

The Verlet damping factor multiplies the velocity term: `velocity *= (1.0 - damping)`.

| Damping Value | Effect | Use Case |
|---|---|---|
| 0.0 | No damping; infinite oscillation | Never use this -- cloth will never settle |
| 0.005 | Very light damping; slow settling; many oscillations | Silk, sheer curtains in still air |
| 0.01 | Light damping; cloth settles in ~3-5 seconds | Standard lightweight fabric |
| 0.02 | Moderate damping; cloth settles in ~1-3 seconds | Cotton, linen curtains |
| 0.05 | Heavy damping; barely oscillates | Heavy drapes, velvet, wet fabric |
| 0.1 | Over-damped; cloth falls into place with no oscillation | Thick upholstery, very heavy materials |
| 0.2+ | Very over-damped; sluggish, unrealistic motion | Only for debugging or special effects |

Source: [Pikuma Verlet Tutorial (drag = 0.01)](https://pikuma.com/blog/verlet-integration-2d-cloth-physics-simulation), [CS184 Cloth Simulation (damping 0.0%-0.8%)](https://andrewdcampbell.github.io/clothsim/)

### Mass-Spring Damping Coefficient (kd)

For explicit spring systems using F_damping = -kd * v_relative:

| Parameter | Typical Value | Notes |
|---|---|---|
| Spring constant ks | 5000 N/m | Default for general cloth |
| Damping coefficient kd | 10 | Fixed value used in research implementations |
| Bend spring scaling | 0.2x of structural | Bending springs are deliberately weaker |
| Max spring extension | 10% beyond rest length | Prevents over-stretching |

Source: [CS184 Cloth Simulation Project](https://andrewdcampbell.github.io/clothsim/), [UC Irvine CS114 Cloth Project](https://ics.uci.edu/~shz/courses/cs114/docs/proj3/index.html)

### XPBD Compliance and Damping Values

XPBD uses compliance (inverse stiffness) instead of stiffness directly:

| Parameter | Formula / Value | Notes |
|---|---|---|
| Stretch compliance | 10^-10 to 10^-8 | Very low for stiff distance constraints |
| Bend compliance | 10^-6 to 10^-4 | Higher (softer) for natural bending |
| Damping parameter | ~3 x 10^-8 | Used in Rayleigh damping formulation |
| alphaHat | compliance / dt^2 | Time-scaled compliance |
| betaHat | damping * dt^2 | Time-scaled damping |
| gamma | alphaHat * betaHat / dt | Combined damping-compliance coupling |

Source: [Bullet Physics Forum - XPBD Damping](https://pybullet.org/Bullet/phpBB3/viewtopic.php?t=13009), [XPBD Paper](https://matthias-research.github.io/pages/publications/XPBD.pdf)

### NvCloth / PhysX Reference Values

| Parameter | Default Value | Range |
|---|---|---|
| Damping | 0.0 | 0.0 - 1.0 (0.5 is a reasonable starting point) |
| Solver Frequency | 300 Hz | Minimum 1 per frame; 60 is usable |
| Drag Coefficient | 0.5 | Controls air resistance |
| Lift Coefficient | 0.6 | Controls aerodynamic lift |
| Tether Stiffness | 1.0 | 0.0 (disabled) to 1.0 |
| Constraint Stiffness | 1.0 | Per-phase stiffness multiplier |

Source: [NvCloth User Guide](https://archive.docs.nvidia.com/gameworks/content/gameworkslibrary/physx/nvCloth/UsersGuide/Index.html)

### Blender Cloth Reference Values

| Parameter | Value | Effect |
|---|---|---|
| Air Viscosity | 0 (default), 8 (for settling) | Simulates air resistance; higher values = faster settling |
| Vertex Mass | 0.1 - 0.3 kg | Lower mass = more wind response |
| Quality Steps | 5 (default), 15 (for stability) | More steps = better convergence |
| Damping | 0.0 - 0.2 | 0.2 eliminates jello-like behavior |
| Pin Stiffness | 0.1 - 1.0 | How rigidly pinned vertices hold |

Spring constants by fabric type (Blender presets):
- Very lightweight: ks = 300
- Lightweight: ks = 800
- Normal: ks = 1200
- Solid: ks = 1600
- Very solid: ks = 2000

Source: [Blender Cloth Settings Documentation](https://docs.blender.org/manual/en/latest/physics/cloth/settings/physical_properties.html), [Blender Bug #73057](https://developer.blender.org/T73057)

### Damping in Physical Terms

Real fabric follows the physics of a damped oscillator. The three damping regimes are:

- **Underdamped** (damping ratio zeta < 1): Oscillates with decaying amplitude. Most real curtains in air are underdamped -- they swing back and forth a few times before stopping.
- **Critically damped** (zeta = 1): Returns to rest in the minimum time without oscillating. This is the fastest settling without overshoot.
- **Overdamped** (zeta > 1): Returns to rest slowly without oscillating. Looks sluggish and unrealistic for fabric.

For simulation, aim for **slightly underdamped** behavior (zeta ~ 0.3-0.7) for lightweight curtains and **near-critically damped** (zeta ~ 0.8-1.0) for heavy drapes. Amplitude decays exponentially: A(t) = A0 * e^(-zeta * omega * t).

Source: [Pendulum Damping Research (UCSB)](https://web.physics.ucsb.edu/~lecturedemonstrations/Composer/Pages/40.37.html), [Physics Damping Tutorial](https://realitypathing.com/investigating-damping-effects-in-pendulum-systems/)

---

## 6. How Game Engines Handle Cloth Settling

### Unity (Built-in Cloth Component)

- **PhysX-based** (NVIDIA): Uses PhysX cloth solver internally.
- **Key parameters**: Stretching Stiffness, Bending Stiffness, Damping (motion damping coefficient), World Velocity Scale, World Acceleration Scale.
- **Solver Frequency**: "Number of solver iterations per second" -- controls both accuracy and settling behavior.
- **Sleep Threshold**: When a particle's kinetic energy drops below this threshold, its position stops being updated. This is the rest-detection mechanism.
- **No explicit "return to rest" force**: settling relies entirely on gravity + constraint enforcement + damping.
- **Limitations**: The built-in Cloth component is relatively basic. Many developers use third-party solutions like **Obi Cloth** for more control.

Source: [Unity Cloth Manual](https://docs.unity3d.com/Manual/class-Cloth.html)

### Unity - Obi Cloth (Third-Party, XPBD-based)

- Uses **XPBD solver** with configurable substeps and constraint iterations (default: 3 iterations per substep).
- **Damping**: Velocity damping applied to particle velocities; recommended value ~0.15 for realism.
- **Sleep Threshold**: Freezes particles below a kinetic energy threshold; set to 0 to keep all particles active regardless of velocity.
- **Relaxation Factor**: Default 1.0, range 1.0-2.0; over-relaxation (>1) speeds convergence.
- **Backstop**: Per-particle painted constraint that prevents cloth from moving past a boundary defined by a sphere radius, used especially for character clothing.
- **Key insight**: "Asleep particles are not taken out of the simulation and will not save performance. Their position is simply not updated." The sleep system prevents micro-jitter at rest without removing particles from the solver.

Source: [Obi Solver Documentation](https://obi.virtualmethodstudio.com/manual/6.3/obisolver.html)

### Unreal Engine (Chaos Cloth)

- Replaced NvCloth with the **Chaos cloth solver** in UE5.
- **Backstop**: Sphere-based constraint (Backstop Radius) that prevents any painted cloth point from moving into a restricted region. Combined with Max Distance to define the cloth's motion envelope.
- **Damping**: Per-axis damping controls how quickly particle velocity decays.
- **Wind**: Supports both global wind (via Wind Directional Source actor) and per-cloth wind velocity. Wind has had reported issues with "invisible bounds" where cloth stops moving at certain distances.
- **Solver**: Runs at a configurable frequency with configurable substeps. Lower frequencies are faster but less stable.
- **Rest pose**: Defined by the skinned mesh rest pose. The cloth simulation starts from and tries to return to this rest configuration through constraint enforcement.

Source: [Chaos Cloth Tool Properties Reference (Epic)](https://dev.epicgames.com/community/learning/tutorials/jOrv/unreal-engine-chaos-cloth-tool-properties-reference), [Chaos Cloth Overview](https://dev.epicgames.com/community/learning/tutorials/OPM3/unreal-engine-chaos-cloth-tool-overview)

### Godot

- **No dedicated cloth node** as of Godot 4.x. This is a known feature gap (proposal #2513).
- **SoftBody3D**: Can approximate cloth but is designed for volumetric soft bodies, not thin sheets. No wind support built in.
- **Third-party solutions**: Godot-Silkload provides bone-driven cloth simulation using Verlet integration for Godot 4.4+. It creates an array of RigidBody3D nodes based on bone positions and processes them with Verlet integration and constraints.
- **Custom implementations**: Developers typically implement cloth from scratch using Verlet integration with manual wind forces applied per-particle.

Source: [Godot SoftBody3D Docs](https://docs.godotengine.org/en/stable/tutorials/physics/soft_body.html), [Godot Cloth Proposal #2513](https://github.com/godotengine/godot-proposals/issues/2513), [Godot-Silkload](https://godotengine.org/asset-library/asset/3785)

### NVIDIA NvCloth (Used by Unity, formerly by Unreal)

- **Damping**: Default 0.0, recommended 0.5 for visible damping effect. Varies behavior between local and global space simulation.
- **Solver Frequency**: Default 300 Hz (very high). This is iterations per second, not per frame.
- **Drag/Lift Coefficients**: Drag 0.5, Lift 0.6 -- these control aerodynamic response to wind.
- **Wind**: Set via `setWindVelocity()`. Documentation recommends varying wind continuously for realistic gusts.
- **Rest lengths and angles**: Assigned automatically by the "cooker" from the input mesh geometry. Constraints are organized into phases: vertical stretch, horizontal stretch, bending, shear.
- **Tethers**: Long-range constraints that prevent excessive stretching from pinned points (stiffness default 1.0).

Source: [NvCloth User Guide](https://archive.docs.nvidia.com/gameworks/content/gameworkslibrary/physx/nvCloth/UsersGuide/Index.html)

### Common Patterns Across All Engines

1. **No special "return to rest" force exists.** All engines rely on gravity + constraints + damping.
2. **Sleep/freeze thresholds** are used to detect when cloth has settled (kinetic energy below threshold).
3. **Backstop constraints** are used for character clothing to limit the motion envelope.
4. **Wind is an external acceleration** applied per-particle, optionally weighted by surface normal.
5. **Damping is always present** as a velocity-reduction factor, typically configurable per-axis.
6. **Rest shape comes from the mesh** at initialization time. Constraint rest lengths are computed from the initial vertex positions.

---

## 7. Recommendations for Vestige

### For Tabernacle Curtains Specifically

The Tabernacle curtains are hanging fabric panels pinned at the top, subject to outdoor wind. Based on this research:

#### Simulation Method
- Use **XPBD with substeps** (not mass-spring with explicit integration). XPBD provides time-step independent material properties and is unconditionally stable.
- Use the **substep approach** (4-8 substeps per visual frame, 1 constraint iteration per substep) rather than multiple iterations per substep.

#### Fixed Timestep
- Implement the **fixed timestep accumulator pattern** from Gaffer on Games.
- Use a **physics rate of 120 Hz** (dt = 0.00833s) as the base rate.
- With 4 substeps, each substep dt = 0.002083s -- stable enough for any realistic fabric stiffness.
- Cap the accumulator at 0.25 seconds and limit to 8 physics steps per frame to prevent spiral of death.
- Use **alpha interpolation** for rendering to eliminate visual stutter.

#### Damping Values (Starting Points)
- **Verlet damping factor**: 0.01 for lightweight linen curtains, 0.03 for heavier fabric.
- **XPBD compliance**: Stretch compliance ~10^-9, Bend compliance ~10^-5.
- **XPBD Rayleigh damping**: ~3 x 10^-8.
- These are starting points -- tune visually until the curtain settles naturally within 2-3 seconds after wind stops.

#### Wind Force
- Apply wind force per-triangle using the dot product of wind direction and triangle normal.
- Use the formula from the Mosegaard tutorial: `force = normal * dot(normalized_normal, wind_direction)`.
- Vary wind over time with Perlin noise for natural gusts.

#### Settling / Sleep Detection
- Track total kinetic energy of all cloth particles: `KE = sum(0.5 * m * v^2)`.
- When KE drops below a threshold for 2+ consecutive frames, freeze particle positions (stop updating them).
- Frozen particles should wake up when wind resumes or external forces are applied.

#### Avoid These Bugs
- Never use damping = 0 -- cloth will oscillate forever.
- Always use XPBD (not PBD) to avoid timestep-dependent stiffness.
- Use a fixed timestep accumulator -- never pass variable frame dt directly to the cloth solver.
- Ensure constraints have non-zero compliance if using XPBD Rayleigh damping (damping requires compliance > 0).
- Test at both 30 FPS and 144+ FPS to verify frame-rate independence.

---

## Sources

- [Pikuma - Verlet Integration and Cloth Physics Simulation](https://pikuma.com/blog/verlet-integration-2d-cloth-physics-simulation)
- [Stanford Cloth Tutorial (Matt Fisher)](https://graphics.stanford.edu/~mdfisher/cloth.html)
- [Fix Your Timestep (Gaffer on Games)](https://gafferongames.com/post/fix_your_timestep/)
- [XPBD Paper - Macklin et al., 2016](https://matthias-research.github.io/pages/publications/XPBD.pdf)
- [Small Steps in Physics Simulation - Macklin, 2019](https://mmacklin.com/smallsteps.pdf)
- [XPBD DeepWiki (XRTailor)](https://deepwiki.com/openxrlab/xrtailor/4.2-extended-position-based-dynamics-(xpbd))
- [Bullet Physics Forum - XPBD Damping](https://pybullet.org/Bullet/phpBB3/viewtopic.php?t=13009)
- [Mosegaard Cloth Simulation Tutorial](https://viscomp.alexandra.dk/index2fa7.html?p=147)
- [CS184 Cloth Simulation Project (Andrew Campbell)](https://andrewdcampbell.github.io/clothsim/)
- [UC Irvine CS114 Cloth Project](https://ics.uci.edu/~shz/courses/cs114/docs/proj3/index.html)
- [Unity Cloth Manual](https://docs.unity3d.com/Manual/class-Cloth.html)
- [Obi Solver Documentation](https://obi.virtualmethodstudio.com/manual/6.3/obisolver.html)
- [Chaos Cloth Tool Properties Reference (Epic)](https://dev.epicgames.com/community/learning/tutorials/jOrv/unreal-engine-chaos-cloth-tool-properties-reference)
- [NvCloth User Guide (NVIDIA)](https://archive.docs.nvidia.com/gameworks/content/gameworkslibrary/physx/nvCloth/UsersGuide/Index.html)
- [Godot SoftBody3D Documentation](https://docs.godotengine.org/en/stable/tutorials/physics/soft_body.html)
- [Godot Cloth Proposal #2513](https://github.com/godotengine/godot-proposals/issues/2513)
- [Blender Cloth Settings Documentation](https://docs.blender.org/manual/en/latest/physics/cloth/settings/physical_properties.html)
- [Blender Bug #73057 - Cloth jelly behavior](https://developer.blender.org/T73057)
- [Blender Bug #115325 - Cloth physics stuck](https://projects.blender.org/blender/blender/issues/115325)
- [Multi-Resolution Shape Matching for Cloth (RWTH Aachen)](https://animation.rwth-aachen.de/media/papers/2013-CAG-MultiResShapeMatching.pdf)
- [Position-Based Dynamics (Muller et al.)](https://matthias-research.github.io/pages/publications/posBasedDyn.pdf)
- [PBD Tutorial 2017 Slides (Macklin)](https://matthias-research.github.io/pages/publications/PBDTutorial2017-slides-1.pdf)
- [Fabric Weight for Curtains Guide](https://sewingtrip.com/fabric-weight-for-curtains/)
- [Curtain Weight DIY Guide (Kgorge)](https://www.kgorge.com/blogs/user-guides/diy-windproof-outdoor-curtain-weights)
- [Pendulum Damping (UCSB Physics)](https://web.physics.ucsb.edu/~lecturedemonstrations/Composer/Pages/40.37.html)
- [Damping Effects in Pendulum Systems](https://realitypathing.com/investigating-damping-effects-in-pendulum-systems/)
- [XPBD Blog (Miles Macklin)](https://blog.mmacklin.com/2016/09/15/xpbd/)
- [Carmen Cincotti - XPBD Blog](https://carmencincotti.com/2022-08-08/xpbd-extended-position-based-dynamics/)
- [Implementing XPBD Report (Telecom Paris)](https://perso.telecom-paristech.fr/jlarette-22/XPBD/Report.pdf)
- [Cloth in the Wind (CVPR 2020)](https://openaccess.thecvf.com/content_CVPR_2020/papers/Runia_Cloth_in_the_Wind_A_Case_Study_of_Physical_Measurement_CVPR_2020_paper.pdf)
- [Godot-Silkload Asset](https://godotengine.org/asset-library/asset/3785)
- [Obi Cloth - Unity Asset Store](https://obi.virtualmethodstudio.com/)
