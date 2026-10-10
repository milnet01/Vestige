---
paths: ["CHANGELOG.md","AUDIT_STANDARDS.md","docs/audits/**",".claude/bump.json"]
---

# Mandatory release audit

Moved verbatim from `CLAUDE.md` on 2026-10-10.

4. **Mandatory audit before each minor or major release.** Phases gave way to versions, so before each `x.y.0` release is cut, run the AUDIT_STANDARDS.md tier process on the tree about to be released: every tier it defines, not a subset, whatever CI already ran. This rule replaces AUDIT_STANDARDS.md's phase-based timing; the rest of it holds, so the fix plan is user-approved and its findings are resolved before the release is cut. File the report, with the fix plan and the date the user approved it, at `docs/audits/<YYYY-MM-DD>-<version>.md`; no file there means no audit ran. Research experimental features alongside.
