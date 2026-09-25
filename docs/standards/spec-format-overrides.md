# Spec-format overrides — Vestige

Deltas to `~/.claude/standards/spec-format.md` for this project. That file
governs everything not named here. `write-spec` applies these deltas when it
drafts. `spec_lint` does not read this file.

## 1. CPU / GPU placement — conditionally required

**Delta to spec-format.md § 4.** Add a `CPU / GPU placement` section, appended
after § 3's twelve like any § 4 section, to every spec whose work touches the
GPU or matches a GPU row of `CODING_STANDARDS.md` § 17's table (per-pixel,
per-vertex, per-particle, per-froxel, reduction over a large buffer). It is
required in that case, not
recommended. A spec for other work (a save schema, localisation) omits it,
as § 4 already says of a section that does not apply.

The section states the choice and the reason. `CODING_STANDARDS.md` § 17
owns the heuristic and the dual-implementation rule, and the section cites
it rather than restating it. `CLAUDE.md` rule 7 is the policy this delta
serves.

**Why a delta.** spec-format.md's required sections carry no placement
section, so without this a spec could pass every check and still breach
rule 7.

## What checks this

Nothing checks it. `write-spec` drafts the section. `review-contract` does
not hand this file to its lanes unless the spec names it, so a missing
section can pass the gate.

## Cold-eyes loop log

Written by `review-contract`. One row per loop, oldest first. Never
back-filled; a correction goes in the current loop's row.

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Verified | Fixed | Outcome |
|---|---|---|---|---|---|---|---|---|
| 1 | 2026-09-25 | 3 | 1 | 0 | 1 | 2 | 2 | Genre `standard` (pinned), so Q4 not asked. Every lane held every question. Lanes arrived holding a pre-a5ffbae CLAUDE.md injected by the harness and said so; they reviewed against the packet. Q1, found by two lanes: "What checks this" claimed the rule-14 cold read is handed this file, but `review-contract` windows only documents a spec names, so a missing section could pass the gate. Replaced with a true statement that nothing checks it. Q3, one lane: the trigger "runs per frame or per element" literally covered the save-schema and localisation examples it exempted. Narrowed to the GPU rows of `CODING_STANDARDS.md` § 17's table, here and in CLAUDE.md rule 7. One open question resolved clean: the stale rule 7 a lane held was the harness copy, not disk. |
| 2 | 2026-09-25 | 3 | 0 | 1 | 0 | 1 | 1 | Every lane held every question; all three found the same Q2, merged as one. CLAUDE.md rule 7 pointed at "the heuristic below", its own inline list, which lacks `CODING_STANDARDS.md` § 17's reduction row that this file names, so a reduction-only spec would be drafted two ways. The defect was loop 1's own collateral in rule 7. Fixed there: rule 7 now names § 17's table and its inline copy is deleted. The subject itself needed no edit. Sweep for other copies of rule 7's list found only two closed `docs/phases/` designs citing "the Rule 7 heuristic", which § 17 still answers. Two open questions resolved clean: `review-contract` 1b windows a document a spec names, and `spec_lint`'s documented checks include none on appended sections. Lanes again disclosed a stale harness-injected CLAUDE.md and commit subjects naming loop 1. |
