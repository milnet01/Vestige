# Phase 8 (Physics) — Design-Doc Index

Phase 8 (Physics) was implemented across multiple sub-phases over a long period; each sub-phase landed its own focused design doc. This index points readers at the right doc for the topic they care about, without folding the historical docs into a single synthetic re-write that would lose the per-slice rationale.

For the *current shipped state* of the physics subsystem (the canonical reference for how the code looks today), see `docs/engine/physics/spec.md`. The phase docs below capture the historical *why* of design decisions; the spec captures the *what* of today's code.

| Doc | Topic | Phase 8 sub-area |
|-----|-------|-------------------|
| `phase_08_design.md` | Phase 8 foundation — Jolt integration goals, character controller, rigid bodies, constraint family scope | 8 (foundation) |
| `phase_08g_physics_foundation_design.md` | Physics-foundation deep-dive: layer system, broadphase, fixed-step accumulator, error-handling contract | 8G |
| `phase_08c_design.md` | Constraint system: hinge / fixed / distance / point / slider, breakable thresholds, deterministic storage | 8C |
| `phase_08_advanced_physics_design.md` | Advanced physics — buoyancy, force fields, joint motors (forward-looking work, partially deferred) | 8 (advanced) |
| `phase_08d_design.md` | Cloth solver foundation: XPBD architecture, particle / constraint / colour-graph data layout | 8D |
| `phase_08e_design.md` | Cloth polish + GPU research: solver-improvement plan that fed into the GPU cloth pipeline | 8E |
| `phase_08f_cloth_collision_design.md` | Cloth collision: triangle-mesh BVH (binned-SAH), spatial hash for self-collision, mesh ↔ cloth contact | 8F |
| `phase_08_cloth_solver_improvements_design.md` | Cloth solver improvements (LRA tethers, Kawabata-derived fabric materials, preset library) | 8 (cloth improvements) |

## Reading order

For a reader new to the physics codebase:

1. Start with **`docs/engine/physics/spec.md`** for the current API surface and shipped state.
2. Read `phase_08_design.md` for the high-level scope of what Phase 8 set out to build.
3. Drop into the sub-phase docs that match the area you're working on:
   - Rigid bodies / constraints / character controller → 8 + 8G + 8C.
   - Cloth → 8D, then 8E + 8F + 8-cloth-improvements (in order).
   - Forward-looking advanced physics → 8 (advanced).

## Cross-cutting cross-references

- The **current Jolt version pin** is recorded in `external/CMakeLists.txt` and discussed in `docs/engine/physics/spec.md` §13. The phase docs predate any version drift; treat them as design rationale, not API reference.
- **CODING_STANDARDS §30 Jolt conventions** (body creation through `PhysicsWorld` facade, layer constants in `physics_layers.h`, fixed-timestep, no `-ffast-math`) supersedes anything stated in the phase docs that conflicts. CODING_STANDARDS is the canonical contract.
- **Phase 9B GPU cloth** (`phase_09b_gpu_cloth_design.md`) is the GPU-runtime descendant of the CPU cloth design in `phase_08d_design.md` — they share the `IClothSolverBackend` contract; see physics spec §6.
- **Phase 10.9 audit landings** (Ph2 / Ph6 / Ph7 / Ph9 / Cl1-Cl8) are the *current* state of constraint / raycast / cloth determinism; the phase docs above describe the pre-audit design intent. Refer to physics spec §15 for the audit-driven open questions.

## Why not consolidate?

A single synthetic `PHASE8_DESIGN.md` would lose the per-slice landing context (which sub-phase introduced which decision, what the alternatives were when each landed). The 8 docs together preserve that. This index gives readers the navigation aid without erasing the history.

If a future reader needs a single document that *just describes today's physics code*, that's the spec at `docs/engine/physics/spec.md` — by design.

---

## Change log

| Date | Author | Change |
|------|--------|--------|
| 2026-04-30 | milnet01 | Initial index — 8 Phase 8 sub-design docs grouped by topic. |
