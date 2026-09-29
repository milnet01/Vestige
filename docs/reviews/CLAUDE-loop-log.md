# CLAUDE.md — review-contract loop log

Review history of the project `CLAUDE.md`, kept outside it because that file
loads on every turn (review-contract 4d).

| Loop | Date | Lanes | Verified / fixed | Q tally | Outcome |
|---|---|---|---|---|---|
| 1 | 2026-09-29 | 2 (neutral-lane, each holding every question; genre standard, pinned) | 3 / 3 | Q2 1 · Q3 2 | Armed by 3D_E-0688 (60 FPS measurement, rules 4 and 5, shader-copy registry). Both lanes: rule 4's "fix plan approved" vs AUDIT_STANDARDS §1 "findings resolved" [Q2]. Lane B: "CI covers the per-push tiers" named no tier [Q3]; `Workaround:` line form unusable in a bulleted CHANGELOG [Q3]. 3 open questions settled, none became a finding. Lanes arrived holding the global CLAUDE.md, not the subject. |
| 2 | 2026-09-29 | 2 (neutral-lane, each holding every question) | 4 / 4 | Q2 1 · Q3 3 | Every finding landed on loop 1's own fix text: the CI carve-out read two ways and CI's job skips Tier 1's sanitizer and test steps [Q2/Q3, merged]; the CHANGELOG pattern missed `**Workaround**:` [Q3]; "read phase as release" inverted §1's timing [Q2]. From a lane's open question: "`0.x.0`" excluded 1.0.0 [Q3]. Fixed by deleting the carve-out (every tier runs on the release tree), widening the pattern, and replacing AUDIT_STANDARDS' timing outright. |
| 3 | 2026-09-29 | 2 (neutral-lane, each holding every question) | 1 / 1 | Q3 1 | Both lanes: the CHANGELOG form allowed a nested bullet the stated grep misses [Q3], on loop 1's text; fixed by requiring a top-level bullet. Open questions settled clean: `--profile-log` overwrites (`fopen "w"`, profile_log.cpp), `tools/fps_floor.py` is committed 100755. Cap reached (standard, 3). Final loop's one finding landed on this run's own text (1/1), but it was a single narrowing with no repair-of-repair behind it; read as a calm cap. Gated span: the four passages 3D_E-0688 edited; 8 of 8 verified findings fell inside it. Shipped. |
