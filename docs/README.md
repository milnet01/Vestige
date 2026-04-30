# Vestige docs/

Documentation hub. Project-wide policy and standards live at the **repository
root** (README, ARCHITECTURE, CHANGELOG, ROADMAP, CODING_STANDARDS,
AUDIT_STANDARDS, SECURITY, CONTRIBUTING, TESTING, ASSET_LICENSES,
THIRD_PARTY_NOTICES, CODE_OF_CONDUCT, LICENSE, VERSION). Everything in
`docs/` is per-feature, per-phase, per-subsystem, or process-level.

## Folder layout

```
docs/
├── README.md                   ← this file
├── PRE_OPEN_SOURCE_AUDIT.md    ← active open-source launch checklist
├── RECOMMENDED_ROUTINES.md     ← maintainer's process / routine reference
├── engine/                     ← per-subsystem specs (Phase B shipped 2026-04-28 → 04-30; 15 specs + SPEC_TEMPLATE.md)
├── phases/                     ← all phase design + research docs (Phase 11A/11B + 12-26 stubs index + Phase 8 / 9 indexes shipped)
├── research/                   ← cross-cutting research and topic-level designs
├── architecture/               ← deep-dive architecture docs (placeholder — content lands as Phase 13 / Vulkan-backend design docs queue up)
├── superpowers/                ← superpowers skill plans + specs (auto-managed)
└── archive/
    ├── audits/                 ← gitignored audit reports & trend snapshots
    └── superseded/             ← old docs retained for history (placeholder until Phase D consolidation moves any docs in)
```

`engine/` is now populated with 15 subsystem specs (animation, audio, core, editor, environment, formula, input, navigation, physics, renderer, resource, scene, scripting, systems, ui — one per shipped engine subsystem); `phases/` carries the active phase design docs plus indexes for the multi-doc Phase 8 / Phase 9 sub-phase clusters. `architecture/` and `archive/superseded/` remain placeholders — they receive content as future architecture deep-dives and any consolidated-out historical docs accrue.

## Where new docs go

| You're writing… | Where it goes | Filename pattern |
|---|---|---|
| Phase design doc | `docs/phases/` | `phase_NN[letter]_[topic_]design.md` |
| Phase research / pre-design | `docs/phases/` | `phase_NN[letter]_[topic_]research.md` |
| Cross-cutting research (no phase yet) | `docs/research/` | `<topic>_research.md` |
| Topic-level design (pairs with research) | `docs/research/` | `<topic>_design.md` |
| Roadmap / planning artefact | `docs/research/` | `<topic>_roadmap.md` |
| Per-subsystem `spec.md` (Phase B+) | `docs/engine/<subsystem>/` | `spec.md` |
| Architecture deep-dive | `docs/architecture/` | `<topic>.md` |
| Audit-tool report (automated) | `docs/archive/audits/` | (tool-managed, gitignored) |
| Superseded doc you don't want to delete | `docs/archive/superseded/` | original filename |

Filenames are **lowercase `snake_case`**, two-digit zero-padded phase
numbers (`phase_03`, not `phase_3`). Sub-phase letters stay lowercase
adjacent to the number (`phase_05a`, `phase_09e3`). The phase number
mirrors `ROADMAP.md`'s numbering; sub-phases and topic suffixes are
optional.

## Phase docs (`docs/phases/`)

56 files. Every numbered phase from Phase 3 through Phase 24 has at least
one design doc here, with paired `_research.md` files where the phase did
its own research pass. Look up the phase number you're after in
`ROADMAP.md` and the file will be `phase_NN[letter][_topic]_design.md`.

Notable groupings:

- **Phase 5 (editor):** `phase_05a` … `phase_05i_*` — editor architectural
  tools, scene serialization, undo/redo, gaps consolidation.
- **Phase 7 (animation):** facial, lip-sync, motion matching across
  `phase_07a` … `phase_07d`.
- **Phase 8 (physics):** advanced physics, cloth solver, cloth collision,
  physics foundation across `phase_08` … `phase_08g`.
- **Phase 9 (GPU + foliage):** GPU cloth, foliage, the four-part
  `phase_09e3_*` GI pipeline.
- **Phase 10 (UX):** fog, settings, UI, first-run wizard, camera modes.
- **Phase 24:** structural physics design.

## Research notes (`docs/research/`)

30 files. Cross-cutting tech/feature research and topic-level designs that
don't bind to a single phase number. Topics include cloth, foliage, GI,
particles, water, BVH collision, character control, motion matching, scene
serialization, undo/redo, perf overlay, the Tabernacle reference set, and
the Formula Workbench self-learning roadmap. Browse the directory or grep
by keyword.

Two roadmap docs live here too:

- `gi_roadmap.md` — Phase 13 GI plan (SH probe grid, radiosity, etc.).
- `self_learning_roadmap.md` — Formula Workbench self-improving fitter.

## Audit reports (`docs/archive/audits/`)

The audit tool (`tools/audit/audit.py`) writes its automated reports here.
Files are gitignored — they're per-run, machine-local, and embed
absolute paths from the run host. The directory is tracked via
`.gitkeep` so the path exists for fresh clones.

Old per-phase audit reports (`PHASE*_AUDIT*.md`, `AUDIT_YYYY-MM-DD.md`)
are also matched by the gitignore pattern under this directory and
inherited from the legacy `docs/` root pattern for the transition window.

## Superseded docs (`docs/archive/superseded/`)

Currently empty (placeholder). Phase D's documentation consolidation
sweep will move historical / retired docs here so they remain accessible
without cluttering the active-doc directories.

## Project-root docs

These stay at the repo root and are referenced from every contributor's
first read:

- [`README.md`](../README.md) — project intro and quickstart
- [`ROADMAP.md`](../ROADMAP.md) — phase plan and status
- [`ARCHITECTURE.md`](../ARCHITECTURE.md) — engine architecture overview
- [`CHANGELOG.md`](../CHANGELOG.md) — release history
- [`CONTRIBUTING.md`](../CONTRIBUTING.md) — how to contribute
- [`TESTING.md`](../TESTING.md) — test layout and how to run
- [`CODING_STANDARDS.md`](../CODING_STANDARDS.md) — style and conventions
- [`AUDIT_STANDARDS.md`](../AUDIT_STANDARDS.md) — 5-tier audit process
- [`SECURITY.md`](../SECURITY.md) — disclosure policy
- [`ASSET_LICENSES.md`](../ASSET_LICENSES.md) — per-asset license map
- [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md) — pinned-deps notes
- [`CODE_OF_CONDUCT.md`](../CODE_OF_CONDUCT.md)
- [`LICENSE`](../LICENSE), [`VERSION`](../VERSION)

## Active checklists at this level

- [`PRE_OPEN_SOURCE_AUDIT.md`](PRE_OPEN_SOURCE_AUDIT.md) — open-source
  launch checklist (still tracking post-launch follow-up items).
- [`RECOMMENDED_ROUTINES.md`](RECOMMENDED_ROUTINES.md) — maintainer's
  recurring-task reference (tag policy, audit cadence, etc.).
