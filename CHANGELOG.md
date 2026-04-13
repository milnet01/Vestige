# Vestige Engine Changelog

All notable engine-level changes are documented here. Per-tool changelogs
live alongside each tool (`tools/audit/CHANGELOG.md`,
`tools/formula_workbench/CHANGELOG.md`).

The engine version tracks phase milestones, not SEMVER. Pre-1.0 commits
may change any interface without notice.

## [Unreleased]

### Security
- Flask web UI of the audit tool hardened against path-traversal and shell-injection (affects local-dev setups that ran the web UI only; no public deployment). Details in `tools/audit/CHANGELOG.md` v2.0.1–2.0.4.

### Fixed
- **Shader SH basis constant** (AUDIT.md §H14). `assets/shaders/scene.frag.glsl:553` used `c3 = 0.743125` on the L_22·(x²−y²) band-2 term; Ramamoorthi-Hanrahan Eq. 13 specifies `c1 = 0.429043`. The ~1.73× over-weight caused wrong chromatic tilt on horizontal surfaces under indoor ambient bakes with asymmetric sky HDRIs.
- **Blackboard `fromJson` cap bypass** (AUDIT.md §H6). `Blackboard::fromJson` wrote directly into `m_values`, bypassing the `MAX_KEYS = 1024` invariant documented in the header. Now routes through `set()` and clamps long keys to 256 bytes on load.
- **Curve fitter non-finite residuals** (AUDIT.md §H13). Levenberg-Marquardt now bails on NaN/Inf initial residuals with an explanatory `statusMessage`, and rejects trial steps that produce non-finite residuals rather than silently corrupting the accumulator. Also switched error accumulators to `double` for precision on large datasets.

## [0.1.0] - 2026-04-13

Initial changelog entry. Prior history captured in `ROADMAP.md` Phase
notes and `docs/PHASE*.md` design documents.

Subsystems in place as of this release:
- Core (engine/window/input/event-bus/system-registry)
- Renderer (OpenGL 4.5 PBR forward, IBL, TAA, SSAO, bloom, shadows, SH probe grid)
- Animation (skeleton, IK, morph, motion matching, lip sync)
- Physics (rigid body, constraints, character controller, cloth)
- Scripting (Phase 9E visual scripting, 60+ node types)
- Formula (template library, Levenberg-Marquardt curve fitter, codegen)
- Editor (ImGui dock-based; Phase 9E-3 node editor panel in progress)
- Scene / Resource / Navigation / Profiler / UI / Audio
