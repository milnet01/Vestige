# Subsystem Specification — Template

> **How to use this template.** Copy this file to `docs/engine/<subsystem>/spec.md` and fill in every section. Sections marked **(required)** must be present and substantive; **(conditional)** sections are required only when the subsystem actually has something to say there. Do not delete a section to "skip" it — delete only the placeholder text and write *"Not applicable — <one-line reason>"*. Keep it terse and table-driven where possible (matches CODING_STANDARDS.md tone). When in doubt, the existing CODING_STANDARDS.md is the style reference.

---

## Header **(required)**

| Field | Value |
|-------|-------|
| Subsystem | `<engine/<subsystem>>` |
| Status | `shipped` / `in-progress` / `planned` / `experimental` / `deprecated` |
| Spec version | `1.0` (bump on every substantive edit; not the engine VERSION) |
| Last reviewed | `YYYY-MM-DD` (date of the most recent cold-eyes review pass) |
| Owners | git usernames or "unassigned" |
| Engine version range | first version this spec describes; bump when the API breaks |

---

## 1. Purpose **(required)**

One paragraph. What does this subsystem *do*? What problem does it solve? Why does it exist as a separate subsystem rather than living inside something else? Read CLAUDE.md before writing this — the engine's primary use case (architectural walkthroughs of biblical structures) should ground every "why."

## 2. Scope **(required)**

| In scope | Out of scope |
|----------|--------------|
| What this subsystem owns | What lives in another subsystem |
| Specific concrete responsibilities | Specific concrete non-responsibilities |

If a reader can't tell which side of the line a feature falls on after reading this table, the table needs more rows.

## 3. Architecture **(required)**

High-level structure. Use a Mermaid diagram or ASCII art if the relationships are non-trivial. List the **key abstractions** as a small table:

| Abstraction | Type | Purpose |
|-------------|------|---------|
| `ClassName` | class | one-line description |
| `function_name()` | free function | one-line description |
| `ConceptName` | concept / interface | one-line description |

Link to source: `engine/<subsystem>/<file>.h:<line>` — readers should be able to jump straight to code.

## 4. Public API **(required)**

The subsystem's facade — the headers that downstream code is allowed to `#include`. Per CODING_STANDARDS section 18, that's `engine/<subsystem>/<subsystem>.h` and any explicitly-public siblings. List each public function / class with:

```cpp
/// brief signature
ReturnType functionName(ParamType param);
```

Plus a one-line behavioural contract. Don't reproduce Doxygen — link to it. Do call out the *non-obvious* contract details: ownership transfer, threading constraints, ordering requirements, idempotence.

**Stability:** state explicitly which APIs are semver-frozen vs which may evolve. Per CODING_STANDARDS section 18 the facade is semver-respecting; if any function in this list isn't, flag it.

## 5. Data Flow **(required)**

How does data enter, move through, and exit this subsystem? A sequence diagram or numbered prose works. Be concrete about *who calls whom*:

1. Caller → `Subsystem::initialize(...)` ← sets up state
2. Per-frame: `Subsystem::update(dt)` → reads from EventBus → publishes events
3. Caller → `Subsystem::shutdown()` ← tears down

Cover the **steady state** path first, then exception paths separately.

## 6. CPU / GPU placement **(conditional — required if subsystem touches GPU)**

Per CLAUDE.md Rule 7. State the placement choice and the reason. If dual-implementation (CPU spec + GPU runtime), name the parity test that pins them.

| Workload | Placement | Reason |
|----------|-----------|--------|
| ... | CPU / GPU | per the heuristic in CODING_STANDARDS section 17 |

Subsystems with no GPU interaction write *"Not applicable — pure CPU subsystem."*

## 7. Threading model **(required)**

Per CODING_STANDARDS section 13. State which threads enter the subsystem, which locks they hold, and what's safe to call from where:

| Caller thread | Allowed APIs | Locks held |
|---------------|--------------|------------|
| Main thread | all | none |
| Job-system worker | `query*`, `read*` | shared (read-only) |
| Render thread | `submit*` | exclusive (during submit) |

If the subsystem is main-thread-only, say so explicitly — that's a valid answer and saves callers guessing.

## 8. Performance budget **(required for shipped subsystems)**

The 60 FPS hard requirement (CLAUDE.md) means a 16.6 ms budget per frame. State this subsystem's slice:

| Path | Budget | Measured (RX 6600, 1080p) |
|------|--------|----------------------------|
| Per-frame update | `< X ms` | `Y ms` (date, scene, conditions) |
| One-shot init | `< X ms` | `Y ms` |
| Worst-case operation | `< X ms` | `Y ms` |

If you don't have measurements yet, write *"TBD — to be measured before exit-Phase-X review"* and file an issue. Don't fabricate numbers.

Profiler markers / RenderDoc capture points: list the named markers a profiler can search for.

## 9. Memory **(required)**

Per CODING_STANDARDS section 12. State:
- **Allocation pattern**: arena / pool / per-frame transient / heap
- **Peak working set** (rough order: KB, MB, GB)
- **Ownership**: who owns long-lived objects
- **Lifetimes**: scene-load duration / engine-lifetime / per-frame

Subsystems with negligible memory write *"Negligible — handful of small structs on the stack."*

## 10. Error handling **(required)**

Per CODING_STANDARDS section 11. What can fail, how, and what the caller should expect:

| Failure mode | Reported as | Caller's recourse |
|--------------|-------------|-------------------|
| File missing | `Result<T, IoError>` with `IoError::NotFound` | substitute fallback / abort scene load |
| Invalid arg (programmer error) | `VESTIGE_ASSERT` (debug) / silent (release) | fix the caller |
| Out of memory | `Result<T, AllocError>` | abort scene load |

No exceptions in the steady-state path (CODING_STANDARDS section 11). If this subsystem does throw anywhere, justify it.

## 11. Testing **(required)**

| Concern | Test file | Coverage |
|---------|-----------|----------|
| Public API contract | `tests/test_<subsystem>.cpp` | feature parity |
| Integration with X | `tests/test_<subsystem>_X_integration.cpp` | smoke + regression |
| Determinism / replay | `tests/test_<subsystem>_determinism.cpp` | seeded |

Per the project rule (every feature + bug fix gets a test), no untested public functions. State explicitly how a new contributor adds a test for this subsystem (fixture name, helper headers).

## 12. Accessibility **(required)**

The user is partially sighted (per memory). Subsystems that produce user-facing output (UI, audio cues, captions, focus rings, color choices) must list accessibility constraints here. Subsystems with no user-facing output write *"Not applicable — no user-facing output"* and the spec is good.

For UI / rendering / audio subsystems specifically:
- Color: contrast ratio targets, never color-only encoding
- Audio: caption hooks, separate music/voice/SFX/UI buses
- Input: rebindable, no time-pressure puzzles, support gamepad + keyboard
- Motion: respect a "reduce motion" setting (Phase 10.7 accessibility surface)

## 13. Dependencies **(required)**

| Dependency | Type | Why |
|------------|------|-----|
| `engine/foo/` | engine subsystem | one-line reason |
| `<glm/glm.hpp>` | external | math primitives |
| `Jolt/Physics/...` | external (third-party) | physics |

Direction: this subsystem may `#include` from these; nothing in this list `#include`s from this subsystem (unless explicitly bidirectional, which is a smell).

## 14. References **(required)**

Cited research / authoritative external sources:
- Academic papers (SIGGRAPH, GDC, etc.)
- Library documentation (Jolt, GLFW, OpenGL spec)
- ISOCpp Core Guidelines
- Other engines' design docs (idTech, Source 2, Anvil)

Web-research at least one current-2026-source for any subsystem that hasn't been comprehensively researched in the last year. CLAUDE.md Rule 1 ("research → design → review → code") applies retroactively when filling backlogged specs.

## 15. Open questions **(conditional)**

Known unknowns, deferred decisions, things flagged for future review. Each one becomes an issue or a TODO in code with date + owner per CODING_STANDARDS section 20.

If there are no open questions, omit the section.

## 16. Spec change log **(required)**

| Date | Spec version | Author | Change |
|------|--------------|--------|--------|
| YYYY-MM-DD | 1.0 | name | initial spec |

Bump spec version on substantive change. Link the commit hash if helpful.

---

## Review checklist

Before declaring a spec ready for cold-eyes review:

- [ ] Every required section is filled (no placeholder text)
- [ ] Conditional sections either contain content or explicitly state "Not applicable — <reason>"
- [ ] All `engine/<path>:<line>` references resolve (paths exist)
- [ ] All external links work
- [ ] Performance budget either has numbers or "TBD — measure by <date>"
- [ ] At least one current (≤ 1 year old) reference cited
- [ ] Cross-doc consistency: no contradictions with CLAUDE.md, CODING_STANDARDS.md, ARCHITECTURE.md, ROADMAP.md

After cold-eyes review pass returns, apply fixes and update the change log row.
