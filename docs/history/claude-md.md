# CLAUDE.md — rule history

Genre: record

Why the project rules read as they do. The rules themselves are in
`../../CLAUDE.md`. Nothing here is in force.

This file is short because `CLAUDE.md` was written lean and never
accumulated dated corrections. It holds what was removed, not a pedigree
for every rule.

## 2026-09-21 — the restated global rules became a pointer

`CLAUDE.md` carried a paragraph that re-listed the global rules the project
inherits. It read:

> All rules in `~/.claude/CLAUDE.md` are in force here and are not repeated
> below — no workarounds without a root-cause fix, shortest correct
> implementation, reuse before rewriting, six-month test, latest library
> versions with current idioms, push-cadence rules, surface ambiguity
> instead of guessing, push back when a simpler path exists,
> reproduce-before-fix for bugs (use `/feature-test` to scaffold the failing
> test first), stay in your lane on edits, and state a verify-step plan for
> multi-step work. The project-specific rules below specialise — they don't
> replace.

It was replaced by a pointer. A rule stated in two places is two rules that
will disagree, and this copy had already disagreed: `/feature-test` names a
skill that no longer exists, and several of the rules it lists were retired
into the global standards while this list went on naming them.

The instruction it carried is not lost. The global file and the standards
under `~/.claude/standards/` own every rule in that paragraph.

## 2026-09-21 — three dead skill names replaced

| Was | Now |
|---|---|
| `/feature-test` | `write-test` |
| `/indie-review` | `review-code` |
| `/cold-eyes` | `review-contract` |

Each name was deleted by the promotion that replaced it. A deleted skill
ships no description, so nothing loaded at session start says the name is
gone — which is why a stale name in a project file survives unnoticed.

`DEPENDENCY_STANDARDS.md` § 6 still names `/cold-eyes` and `/indie-review`
as of this entry.

## Why the workaround rule is a project rule

Project rule 5 extends the global no-workarounds rule rather than replacing
it. The global rule forbids a workaround without a root-cause fix; the
project rule adds that a workaround which genuinely ships is named in the
commit message and in `CHANGELOG.md`, so it stays discoverable afterwards.
