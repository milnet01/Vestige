# Phase 9 (Domain-Driven System Architecture) — Design-Doc Index

Phase 9 was implemented across six sub-phases (9A through 9F) plus a separate 9E3 wave. Each sub-phase landed its own design doc; this index points readers at the right doc per topic.

For the *current shipped state* of the domain ISystem layer, see `docs/engine/systems/spec.md`. The Phase 9 docs below capture the design rationale for *why* the architecture chose ISystem + SystemRegistry over alternatives like full archetype-storage ECS; the spec captures *what* the wrapper layer looks like today.

| Doc | Topic | Phase 9 sub-area |
|-----|-------|-------------------|
| `phase_09a_design.md` | Phase 9A foundation — `ISystem` interface design, `SystemRegistry` lifecycle, owned-component-types auto-activation | 9A |
| `phase_09b_design.md` | Phase 9B Domain Wrappers — wrapping `EnvironmentForces`, `Renderer`, `AudioMixer`, etc. as ISystem implementations | 9B |
| `phase_09b_gpu_cloth_design.md` | GPU cloth pipeline (compute-shader backend behind `IClothSolverBackend`) — landed inside Phase 9B | 9B (GPU cloth) |
| `phase_09c_design.md` | Phase 9C — additional domain wrappers + system-events catalogue + `SystemRegistry::activateSystemsForScene` | 9C |
| `phase_09e_design.md` | Phase 9E foundation — node-based visual scripting (graphs, nodes, blackboard) | 9E |
| `phase_09e3_design.md` | Phase 9E3 — visual scripting node-pack expansion (event / flow / action / latent / pure node packs) | 9E3 |
| `phase_09e3_research.md` | Phase 9E3 research notes — type compatibility, memoisation, ABA guards | 9E3 (research) |
| `phase_09f_design.md` | Phase 9F — final Phase 9 polish + audit-driven cleanup | 9F |

## Reading order

For a reader new to the system architecture:

1. Start with **`docs/engine/systems/spec.md`** for the current 14-system catalogue + force-active flags + update phases.
2. Read `phase_09a_design.md` for the original ISystem design rationale.
3. Drop into the sub-phase docs that match the area you're working on:
   - Adding a new ISystem → 9A + 9B (the wrapping pattern).
   - Visual scripting → 9E + 9E3 (and `docs/engine/scripting/spec.md` for current state).
   - GPU cloth → 9B-gpu-cloth + `docs/engine/physics/spec.md` §6 (cloth dual-implementation).

## Cross-cutting cross-references

- **`docs/engine/systems/spec.md`** is the current canonical reference for which systems exist, which are force-active, and which are empty-pump stubs (per the Phase 10.9 W13 audit).
- **`docs/engine/scripting/spec.md`** is the canonical reference for the current visual-scripting runtime — replaces the design docs as the live API source.
- **CODING_STANDARDS** alignment: the wrapper layer threading model lives in §13; the wrapper-vs-interface boundary in §18.
- The original phase docs predate the Phase 10.9 W13 audit; some claimed features are now relocated to `engine/experimental/` (the systems spec §3 status table records the current placement).

## Why not consolidate?

Same rationale as Phase 8: each sub-phase doc captures the design rationale at the time it landed. A synthetic merged doc would erase the per-decision context. This index gives readers the navigation aid; the spec gives them the live state.

---

## Change log

| Date | Author | Change |
|------|--------|--------|
| 2026-04-30 | milnet01 | Initial index — 8 Phase 9 sub-design + research docs grouped by topic. |
