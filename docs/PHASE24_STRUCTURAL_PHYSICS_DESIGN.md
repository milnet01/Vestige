# Phase 24: Structural / Architectural Physics Design

## Overview

Adds the physical-attachment machinery that architectural walkthroughs need:
cloth particles fixed to moving rigid bodies (curtain hanging from a pole),
rings sliding on poles, inextensible tethered ropes, and scene-level joints
binding pillars to sockets and walls to tent-pegs. Needed because the
Tabernacle's linen panels, ram-skin covers, and outer-court walls are
currently simulated as free cloth — they drape correctly but have nothing
physical holding them up. Without this phase the rendering-realism pass
(Phase 13 + research update) produces photoreal curtains *floating in mid
air*, which is worse than lower-fidelity-but-attached curtains.

## Scope

**In scope:**
- XPBD cloth particle ↔ Jolt rigid body kinematic attachment
- XPBD cloth particle ↔ static scene anchor (world transform)
- Inextensible tether (distance-max) constraint between arbitrary anchor
  pairs (rigid↔rigid, rigid↔cloth particle, particle↔particle)
- Slider/prismatic joint with limits (built on Jolt's SliderConstraint — the
  constraint type already exists; this phase exposes authoring + physics
  behaviour for the ring-on-rod case)
- Editor UI for attachment authoring (pin-to-body, ring-slider, tether
  endpoint picker, anchor-cord tool)
- Formula Workbench entries for attachment tuning coefficients
- Tabernacle structural pass — actually rig the existing scene with these
  joints

**Out of scope (future phases):**
- Tearable cloth / breakable attachments beyond the existing break-force
  field on `PhysicsConstraint` (Phase 24 uses break forces, doesn't extend
  them)
- Deformable rigid bodies (wood flex, metal bending)
- Fluid-rigid coupling (bowls of water, oil flames)
- Procedural fabric generation / pattern authoring

## Research Summary

### XPBD cloth ↔ rigid body attachment

The standard pattern — used by Unreal's Chaos Cloth, Unity's Obi Cloth, and
the NVIDIA PhysX FleX reference — is a **kinematic pin** rather than a
dynamic joint:

- Each frame, *before* the XPBD solver runs, kinematic-pinned particles
  have their position set from the attached rigid body's current world
  transform (body origin + body rotation × local offset).
- Their inverse mass is clamped to 0 for the substep so XPBD position
  projection treats them as infinitely heavy (same behaviour as the
  existing world-space pin).
- After the solve, kinematic-pinned particle velocity is re-derived from
  the rigid-body motion (finite-difference over the frame) so wind /
  neighbour constraints see a consistent velocity field.

References:
- Macklin, Müller, Chentanez, "XPBD: Position-Based Simulation of Compliant
  Constrained Dynamics" — original XPBD paper, §4.3 covers attachment.
- Jolt Physics soft body code (`SoftBodyVertex::mInvMass = 0` plus
  `mPosition` overwrite) — identical pattern in MIT-licensed form we can
  read directly.
- Unity Obi Cloth `ObiParticleAttachment` — production implementation of
  both "static" and "dynamic" attachment modes; dynamic mode is the
  pattern above.

### Inextensible tether (distance-max)

A tether/rope *constrains maximum distance without applying compressive
force*. Two well-understood formulations:

1. **One-sided XPBD distance constraint** — a standard XPBD distance
   constraint where the stiffness multiplier is applied only when
   `|p_a - p_b| > rest_length` and is zero otherwise. Trivially implemented
   as a branch in the XPBD solver; preserves the existing compliance
   knob.
2. **Long-Range Attachment (LRA), Kim et al. 2012** — already partially
   used by Vestige's cloth solver for pin drift; extends naturally to
   rigid-body endpoints. This phase reuses the existing `LRAConstraint`
   infrastructure with new endpoint types.

The ten linen curtain panels of the Tabernacle are joined by fifty golden
taches (Exodus 26:6) — each tache is a tether between a loop pair. Tether
#1 (one-sided XPBD) is the right tool: when the coupled curtains hang they
don't push on each other, but neither can they separate past the loop
spacing.

References:
- Kim, Chentanez, Müller 2012, "Long Range Attachments — A Method to
  Simulate Inextensible Clothing in Computer Games"
- Macklin et al. 2014, "Unified Particle Physics for Real-Time Applications"
  (NVIDIA FleX), §5.2 on tethers

### Sliding ring on pole

A bronze-ring-on-acacia-pole is a standard prismatic joint with axis
limits: motion along one body axis, free rotation about that axis, zero
motion on the other two axes. Jolt's `SliderConstraint` supports this
directly; this phase adds the authoring story, not the primitive.

One subtlety: when the ring is at a pole limit (end stop), additional force
must *stop* the ring without propagating the full impulse into the curtain
particles attached to it. Solution: clamp the particle attachment's
kinematic target velocity to the ring's post-constraint velocity each
substep (standard one-way coupling).

### Scene-level anchor joints

Pillars sit in silver sockets (Exodus 26:19 — two sockets per pillar,
mortise-and-tenon). Structurally these are **fixed joints between two rigid
bodies** where one body is static. `Jolt::FixedConstraint` via the existing
`PhysicsConstraint` wrapper handles this; no new primitive required.

Tent pegs anchor the outer linen walls of the court (Exodus 27:19). Each
peg is a static rigid body; each anchor-cord is a tether from peg to pole
top. Same tether as above.

### Wind + attachment interaction

Once a curtain is pinned at its top edge, applied wind force produces
pendulum motion with period proportional to the height of the pinned row
above the curtain's centre of mass. For a 4 m curtain hanging from its top
edge the period is ~4 s, which matches the calm-gust timing already tuned
in `ClothSimulator::updateGustState()`. **No solver change required** —
but the wind-field magnitude at the pinned-row height becomes the dominant
driver of visible motion, so the `EnvironmentForces::getWindAt()` sample
position must come from the *particle* world position, not the cloth's
bounding-box centroid. (Quick check: already the case — confirmed by
reading `cloth_simulator.cpp:applyWindForce()`.)

## Architecture

```
PhysicsWorld (existing)
 └── PhysicsConstraint vector (existing HINGE/FIXED/DISTANCE/POINT/SLIDER)
     └── NEW: constraint authoring exposed via editor panel

ClothSimulator (existing)
 ├── PinConstraint (existing — world-space only)
 ├── NEW: KinematicAttachment { particleIdx, rigidBodyHandle, localOffset }
 │        — updated before each substep from the attached body's transform
 ├── LRAConstraint (existing — particle ↔ particle)
 └── NEW: TetherConstraint { anchorA, anchorB, maxLength, compliance }
          where each anchor is (ClothParticle | RigidBody | StaticAnchor)

ClothComponent (existing)
 └── NEW: attachment-authoring API surface

Editor (existing)
 └── NEW: AttachmentAuthoringTool
          ├── Pin-to-body: pick rigid body from the scene, select cloth
          │   vertex / vertex set, creates a KinematicAttachment
          ├── Slider-ring: pick pole rigid body, place ring at point,
          │   creates a SliderConstraint + a small "ring" rigid body
          └── Tether: pick anchor A + anchor B + max length, creates a
              TetherConstraint in whichever subsystem owns both endpoints
```

## New Files

| File | Purpose |
|------|---------|
| `physics/kinematic_attachment.h/cpp` | Cloth particle ↔ Jolt rigid body attachment. Owns the per-attachment local offset; `updateTargets()` called before each XPBD substep sets pinned-particle positions from body transforms. |
| `physics/tether_constraint.h/cpp` | One-sided distance-max constraint. Endpoint is a tagged union of particle / rigid body / static anchor. |
| `physics/attachment_manager.h/cpp` | Owns the vectors of `KinematicAttachment` and `TetherConstraint`, drives the per-substep update, and fans out to `ClothSimulator::setPinPosition` / Jolt body queries. |
| `editor/widgets/attachment_panel.h/cpp` | ImGui panel: three modes (Pin-to-body / Slider-ring / Tether). Picker widgets for rigid bodies + cloth-vertex selection gizmos. |
| `editor/tools/vertex_picker.h/cpp` | Vertex-picking gizmo (ray-cast from cursor → closest particle in screen space). Shared with future skinning / morph tools. |
| `tests/test_kinematic_attachment.cpp` | Unit tests: static-pin equivalence, moving-body tracking, attachment break force, velocity consistency. |
| `tests/test_tether_constraint.cpp` | Unit tests: one-sided behaviour (no compression force below rest length), endpoint tagged union, rigid-body endpoint correctness. |
| `assets/scenes/tabernacle_structural.vscene` | Re-rigged Tabernacle scene using all of the above. |

## Modified Files

| File | Changes |
|------|---------|
| `physics/cloth_simulator.h/cpp` | Expose per-particle kinematic-target-position setter (`setKinematicTarget(uint32_t, glm::vec3, glm::vec3 velocity)`) so the attachment manager can drive positions without going through the public pin API. |
| `physics/cloth_component.h/cpp` | Serialize / deserialize attachment IDs alongside the cloth mesh. |
| `editor/editor.h/cpp` | Register the attachment panel + vertex picker, hotkey bindings. |
| `editor/scene_serializer.cpp` | Persist `KinematicAttachment` + `TetherConstraint` records. |
| `assets/scenes/tabernacle.vscene` | Replace with the structural version (old version kept as `tabernacle_demo.vscene` for regression). |

## Key Algorithms

### Kinematic attachment update (per substep)

```
for each KinematicAttachment a:
    body      = physicsWorld.getBody(a.rigidBodyHandle)
    worldPos  = body.getWorldTransform() * a.localOffset
    prevPos   = cloth.getParticlePosition(a.particleIdx)
    velocity  = (worldPos - prevPos) / substepDt
    cloth.setKinematicTarget(a.particleIdx, worldPos, velocity)
```

The order matters: kinematic targets are applied *before* XPBD position
projection each substep, so neighbour-distance constraints project relative
to the freshly-updated pin. Identical pattern to the existing
world-space pin, just with a moving target.

### Tether constraint (one-sided XPBD)

```
for each TetherConstraint t:
    posA = resolveAnchor(t.anchorA)    // particle, rigid body COM + offset, or static anchor
    posB = resolveAnchor(t.anchorB)
    d    = distance(posA, posB)
    if d <= t.maxLength:
        continue                        // slack — no force
    delta      = d - t.maxLength
    direction  = (posB - posA) / d
    correction = delta / (wA + wB)      // XPBD position correction
    applyCorrection(t.anchorA, +correction * direction * wA)
    applyCorrection(t.anchorB, -correction * direction * wB)
```

Where `resolveAnchor` / `applyCorrection` dispatch on the endpoint tag.
Rigid-body endpoints convert correction to impulse via Jolt's
`AddImpulse()`; static-anchor endpoints swallow the correction.

### Ring-on-pole attachment chain

The full chain for one curtain ring is:
1. **SliderConstraint** between pole rigid body and ring rigid body,
   along the pole's long axis, with axis limits at the pole ends.
2. **KinematicAttachment** from a cloth particle (the top-loop vertex) to
   the ring rigid body.

This decomposition means the ring's dynamics (mass, friction along the
pole, collision with the pole caps) live in Jolt, and the curtain's
dynamics (XPBD) stay in the cloth simulator. Only the position is bridged.

## API Additions

```cpp
// engine/physics/attachment_manager.h
class AttachmentManager
{
public:
    AttachmentHandle addKinematicAttachment(
        ClothHandle cloth, uint32_t particleIdx,
        RigidBodyHandle body, const glm::vec3& localOffset);

    AttachmentHandle addTether(
        const AnchorRef& a, const AnchorRef& b,
        float maxLength, float compliance = 0.0f);

    void removeAttachment(AttachmentHandle h);

    /// Called by PhysicsWorld::update() before each XPBD substep.
    void updateKinematicTargets(float substepDt);

    /// Called by PhysicsWorld::update() during the XPBD solver pass.
    void solveTethers();
};

// engine/physics/cloth_simulator.h — additions
void setKinematicTarget(uint32_t particleIdx,
                       const glm::vec3& worldPos,
                       const glm::vec3& velocity);
```

## Formula Workbench Entries

Per CLAUDE.md Rule 11, every numerical coefficient introduced by this
phase gets a Formula Workbench case so tuning is reproducible and
reviewable.

| Coefficient | Reference data source | Workbench case name |
|-------------|-----------------------|---------------------|
| Ring sliding friction vs pole-normal force | IRL bronze-on-acacia friction measurements (μ ≈ 0.3–0.5 kinetic, 0.4–0.6 static); fit to a single Coulomb-with-viscous-damping curve | `ring_friction` |
| Tether slack ramp (compliance as a function of tension for natural flax linen) | Textile tensile test data (Journal of Natural Fibers, linen stress-strain) | `flax_tether_compliance` |
| Wind-induced pendulum damping vs linen sheet area | Free-swing video references filmed against a still atmosphere | `linen_pendulum_damping` |
| Anchor-cord tensile modulus (twisted goat-hair cord from the outer-court walls) | Historical cord replicas + modern natural-fibre cord specs | `goat_hair_cord` |

## Tabernacle Structural Pass — rigging spec

This is the "acceptance test" — once the infrastructure above lands, the
demo scene should reproduce the following structural hierarchy:

**Inner Tabernacle (the tent proper):**
- 48 acacia-wood boards (upright pillars of the tent frame) — each a
  static rigid body, two sockets per board (`FixedConstraint` to world).
- 5 horizontal bars per long side + 5 on the rear — each a rigid body
  joined to every board it passes through with `FixedConstraint`.
- 10 inner curtains (fine linen, embroidered cherubim) — each a cloth
  mesh, top-edge particles `KinematicAttachment` → 50 loops → 50 taches
  (tethers between curtain pairs), bottom-edge free.
- 11 goat-hair curtains (outer covering over the tent) — same pattern
  but with 50 bronze taches instead of gold.
- 2 coverings: ram-skin (dyed red) + badger / dugong-skin outer —
  `KinematicAttachment` on top edge to tent-frame top corners, corners
  gathered with tethers.
- Veil (between Holy Place and Holy of Holies) —
  `KinematicAttachment` top edge to 4 acacia pillars; bottom free.
- Screen (entrance curtain of the Holy Place) — same pattern, 5 pillars.

**Outer Court:**
- 60 outer pillars (20 south, 20 north, 10 east, 10 west) — static
  rigid bodies in bronze sockets.
- 4 pole-top silver caps per pillar — static sub-bodies `FixedConstraint`
  to pillars; anchor points for the fine-linen walls.
- Fine-linen wall panels (5 cubits × 5 cubits each) — cloth meshes,
  top-edge `KinematicAttachment` to silver cap hooks, bottom-edge free.
- Gate screen (east side, 20 cubits) — same pattern, 4 pillars.
- 4 corner-peg + cord systems per pillar row — static tent-peg rigid
  bodies in the terrain, `TetherConstraint` from peg to pole-top cap.

**Acceptance criteria:**
- On scene load, *nothing* hangs in mid air: every particle above Y = 0 is
  tethered through a chain of joints that terminates at a static anchor.
- Walking into a curtain displaces it locally (cloth collision — already
  works) without the attachment releasing.
- Pulling the entrance screen aside via the script console (applying a
  one-frame force pulse to a cloth particle) makes the rings bunch along
  the pole and stay bunched (ring friction holds position).
- Wind gust (`EnvironmentForces::setWindStrength(5.0f)`) produces
  coordinated pendulum swing across all linen walls, period ≈ 4 s,
  with no rubber-banding or visible attachment pops.

## Performance Considerations

- Kinematic attachment update is O(A) per substep where A is the total
  attachment count (expected ~500 for the full Tabernacle scene after
  the structural pass). One matrix multiply + one
  `ClothSimulator::setKinematicTarget` per attachment. ~10 µs budget.
- Tether solve is O(T) per XPBD iteration. Tether count scales with
  curtain count (~50 taches × 2 layers + ~120 corner cords ≈ 220). One
  vector length + one branch + optional correction application per
  iteration.
- No GPU work added by this phase. Cloth already runs on CPU; rigid
  bodies already run in Jolt. This phase only bridges the two.
- No change to the existing 60 FPS budget. The dominant cost of the
  Tabernacle scene under load is still the cloth XPBD solver itself
  (profiled at 2.1 ms / frame with all 21 curtains active); adding ~700
  attachments + tethers is noise relative to that.

## Test Plan

- **KinematicAttachment:**
  - Static-body attachment == world-space pin (regression check).
  - Moving body: particle tracks body translation + rotation.
  - Body rotation: `localOffset` rotated into world space correctly.
  - Break force: when tension exceeds threshold, attachment releases.
  - Body destruction: attachments to deleted bodies release cleanly.
- **TetherConstraint:**
  - One-sided: no force applied below rest length.
  - Force direction: correction applied along the line of separation.
  - Tagged-union endpoints: particle ↔ particle, particle ↔ rigid body,
    rigid body ↔ rigid body, static ↔ rigid body all work.
  - Zero-length safety: tether with rest length 0 and separated endpoints
    doesn't divide by zero.
- **Slider-ring integration test:** ring at pole-limit, applied cloth
  tension, ring stops at limit, tension propagates into curtain without
  the ring leaving the pole.
- **Tabernacle acceptance test:** load the structural scene, let physics
  settle 5 s of simulated time, assert no particle has drifted more
  than 1 mm from its rest position (still-air baseline). Then apply
  wind gust, sample pendulum period, compare against the Workbench-fit
  reference curve.

## Dependencies + Sequencing

Blocks none of the existing roadmap. Depends on:
- The existing Jolt constraint wrapper (`physics_constraint.h`) —
  already in place.
- The existing XPBD cloth solver (`cloth_simulator.h`) — already in
  place.
- The existing pin API (`pinParticle` / `setPinPosition`) — already in
  place; will be refactored to share state with the new kinematic
  target API.

Enables (future phases):
- The realism-rendering sprint (Phase 13 research update) — meaningful
  only once curtains attach correctly.
- Playable gameplay systems that involve manipulable architectural
  elements (pulling aside a veil, opening a tent flap).
- Multi-user collaborative editing of structural scenes (Phase 22) —
  attachment authoring needs to round-trip through serialization.

## Open Questions

- **Editor ergonomics for vertex-set pinning.** The top edge of a curtain
  is ~40 particles; picking each one manually is tedious. Expected
  answer: a "loop-select"-style screen-space lasso that collects every
  particle within a user-drawn polygon, but decide this during Phase
  24.4 implementation rather than speculating.
- **Where does the attachment list live in the .vscene file?** Options:
  (a) embedded inside the cloth entity's component block; (b) a
  top-level `attachments:` list referencing both endpoints by entity ID.
  Prefer (b) for symmetry with `constraints:`, but confirm with the
  serialization author during 24.5.
- **Should the ring rigid body be auto-created by the editor or
  authored manually?** Auto-create from the slider-ring tool makes
  authoring one-click; manual preserves explicit user control over
  mass/collision. Probably offer both, default to auto.
