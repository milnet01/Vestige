# Phase 10 (Polish and Features) — Design-Doc Index

Phase 10 grew into a mesh of sub-phases (10.5 through 10.9) plus several
topic-scoped design docs that don't carry a numeric sub-phase. Each landed its
own design doc; this index points readers at the right doc per topic so the
10.x sprawl is navigable (the gap this index closes — there was no Phase 10
index to mirror `phase_08_index.md` / `phase_09_index.md`).

For the *current shipped state* of any subsystem, prefer its spec under
`docs/engine/<area>/spec.md`; the Phase 10 docs below capture the design
rationale for *why* each feature is shaped the way it is.

| Doc | Topic | Phase 10 sub-area |
|-----|-------|-------------------|
| `phase_10_5_first_run_wizard_design.md` | First-run wizard — onboarding flow, settings-schema seeding | 10.5 |
| `phase_10_7_design.md` | Accessibility + Audio integration — bus routing, gain chain, subtitles, photosensitive retrofits | 10.7 |
| `phase_10_8_camera_modes_design.md` | Camera modes — orbit / follow / first-person / cinematic | 10.8 |
| `phase_10_audio_music_player_design.md` | Streaming music player (W8 part 2/2) — playlist, crossfade, scene-driven music | 10.audio-music |
| `phase_10_fog_design.md` | Fog, mist & volumetric lighting | 10.fog |
| `phase_10_fog_research.md` | Fog & atmospheric-scattering research notes | 10.fog (research) |
| `phase_10_localization_design.md` | i18n + RTL + multi-script text rendering | 10.localization |
| `phase_10_settings_design.md` | Settings system — schema, persistence, quality presets | 10.settings |
| `phase_10_ui_design.md` | In-game UI system — widgets, layout, input routing | 10.ui |

> The `10.<topic>` labels in the **Phase 10 sub-area** column are doc-local tags
> for navigation, not ROADMAP IDs. Numbered sub-phases (10.5–10.9) trace to the
> matching `## Phase 10.N` ROADMAP section; topic tags (`10.fog`, `10.ui`,
> `10.audio-music`, …) map to a feature cluster. The audio-music doc, for
> example, is ROADMAP "Phase 10.9 Slice 8 W8 (part 2/2)" — cross-referenced as
> "Slice 8" below.

### Sub-phases tracked in ROADMAP without a standalone design doc

- **Phase 10.6 — Multi-Threading & Concurrency Architecture:** job system, render/sim/IO paths. Design lives in the ROADMAP section "Phase 10.6: Multi-Threading & Concurrency Architecture" (no separate doc yet — promote to one when the phase enters implementation).
- **Phase 10.9 — Post-Ultrareview Remediation:** the W-slice remediation waves. Tracked entirely in the ROADMAP section "Phase 10.9: Post-Ultrareview Remediation" (W-slices), not a design doc.

## Reading order

For a reader new to the Phase 10 work:

1. Start with the ROADMAP "Phase 10: Polish and Features" section for the feature list + status.
2. Drop into the design doc that matches the area you're working on (table above).
3. For the live API/behaviour, read the matching `docs/engine/<area>/spec.md` — the design docs predate later remediation passes and capture rationale, not current state.

## Cross-cutting cross-references

- **Settings-schema versioning** (`kCurrentSchemaVersion`) is touched by both `phase_10_5_first_run_wizard_design.md` and `phase_10_localization_design.md`; the migration-ownership convention is recorded in both docs (see the Phase 10.5 / Localization "Schema migration ownership" notes).
- **Scene-format versioning** (`CURRENT_FORMAT_VERSION`) is bumped by `phase_10_audio_music_player_design.md` Slice 8; the bump-ownership convention is recorded there.
- **`docs/engine/audio/spec.md`** is the canonical reference for the current audio runtime — supersedes the 10.7 / audio-music design docs as the live source.

## Why not consolidate?

Same rationale as Phases 8 and 9: each sub-phase doc captures the design
rationale at the time it landed. A synthetic merged doc would erase the
per-decision context. This index gives readers the navigation aid; the specs
give them the live state.

---

## Change log

| Date | Author | Change |
|------|--------|--------|
| 2026-06-01 | milnet01 | Initial index — 9 Phase 10 design/research docs grouped by topic, plus 10.6 / 10.9 ROADMAP-only sub-phases (CE8). |
