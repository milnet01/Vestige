---
paths: ["external/**","**/CMakeLists.txt","THIRD_PARTY_NOTICES.md","DEPENDENCY_STANDARDS.md"]
---

# Dependency pins

Moved verbatim from `CLAUDE.md` on 2026-10-10.

8. **A non-latest pin carries a written reason at the pin site.** `~/.claude/standards/dependencies.md` owns the latest-stable rule and what counts as a dependency. What this project adds is the recording: a non-latest pin is only allowed in the two cases `DEPENDENCY_STANDARDS.md` defines, each carrying a reason at the pin site: **(A)** a newer *release* provably breaks — record a row in the Breaking-Version Registry with a re-test trigger (the exact breaking version, so a newer release is re-tested and the pin lifted once fixed); or **(B)** upstream has no suitable release yet, so an exact branch commit is pinned — record it in `THIRD_PARTY_NOTICES.md`'s Branch-commit-pins section with a re-evaluate trigger. "We haven't tested it yet" is not a valid reason. Every dependency upgrade gets an independent review by a fresh subagent with no authoring context. Full process + routing: `DEPENDENCY_STANDARDS.md`.
