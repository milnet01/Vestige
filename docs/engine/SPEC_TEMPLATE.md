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

The subsystem's facade — the headers that downstream code is allowed to `#include`. Per CODING_STANDARDS section 18, that's `engine/<subsystem>/<subsystem>.h` and any explicitly-public siblings.

**Goal of this section: convey the *shape* of the surface area, not catalogue every signature.** A reader should leave knowing what they can call and the non-obvious contract details — they consult the headers (and Doxygen) for parameter detail.

Two patterns:

1. **Small surface (≤ 7 public headers)** — list public functions / classes inline. Header count is the primary axis; function count is illustrative. When in doubt at the boundary, lean toward grouping (clarity > exhaustiveness).

   ```cpp
   /// brief signature
   ReturnType functionName(ParamType param);
   ```

   Plus a one-line behavioural contract per non-trivial function. Always call out *non-obvious* contract details: ownership transfer, threading constraints, ordering requirements, idempotence.

2. **Facade subsystems with many headers** (≥ 8 public headers, e.g. `engine/core`, `engine/renderer`): group the API by header. One code block per public header showing its types + headline functions — typically 5-15 lines each — plus a `// see <header>:NN for full surface` pointer.

Either pattern: end the section with "Non-obvious contract details" as a bullet list (the contract details are universally interesting; the per-function signatures are not).

**Stability:** state explicitly which APIs are semver-frozen vs which may evolve. Per CODING_STANDARDS section 18 the facade is semver-respecting; if any function in this list isn't, flag it.

## 5. Data Flow **(required)**

How does data enter, move through, and exit this subsystem? A sequence diagram or numbered prose works. Be concrete about *who calls whom*:

1. Caller → `Subsystem::initialize(...)` ← sets up state
2. Per-frame: `Subsystem::update(dt)` → reads from EventBus → publishes events
3. Caller → `Subsystem::shutdown()` ← tears down

Cover the **steady state** path first, then exception paths separately.

## 6. CPU / GPU placement **(conditional — required if subsystem touches GPU)**

Per CLAUDE.md Rule 7. State the placement choice and the reason. If dual-implementation (CPU spec + GPU runtime), name the parity test that pins them.

Pick one of three patterns based on the subsystem's relationship to the GPU:

**A. Pure CPU subsystem (no GPU touch at all)** — write *"Not applicable — pure CPU subsystem"* and move on.

**B. Infrastructure plumbing (GPU touch only via context / swap-chain creation, no per-frame compute or rasterization workload)** — single-row table:

| Workload | Placement | Reason |
|----------|-----------|--------|
| GL context creation + framebuffer swap (window pump) | CPU (main thread) | OS / driver call; GL context affinity is single-thread. The plumbing itself is CPU work — no GPU compute originated here. |

**C. GPU-active subsystem (per-frame compute / rasterization / dispatch)** — full table per workload:

| Workload | Placement | Reason |
|----------|-----------|--------|
| Per-pixel `<thing>` shading | GPU (frag shader) | per CODING_STANDARDS §17 heuristic |
| Per-vertex `<thing>` transform | GPU (vert shader) | ditto |
| Sparse decision (which thing to draw) | CPU | branching / I/O |
| Reduction over `<thing>` buffer | GPU compute | data-parallel |

Dual implementations (CPU spec + GPU runtime) name the parity test (`tests/test_<subsystem>_parity.cpp`) that pins bit-equivalent behaviour.

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

**Honesty rules:**
- If you have measurements: cite the date, scene, and hardware. Numbers without provenance rot fast.
- If you don't have measurements: write `TBD — measure by <Phase X / date>`. Never fabricate.
- **All-pending special case:** if every cell is `TBD`, replace the table with a single line: *"Not yet measured — will be filled by <Phase X audit / date>; tracked as Open Q in §15."* and reference §15 — the empty-table form is just visual padding. Restore the table the moment any cell has a real number.

Profiler markers / RenderDoc capture points: list the named markers a profiler can search for. (`glPushDebugGroup` labels per CODING_STANDARDS §29.)

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
| Resource missing (file / asset / scene) | `Result<T, IoError>` with specific `IoError` enum | substitute fallback / abort scene load |
| Schema violation (parse error, unknown version) | `Result<T, ParseError>` + log + corrupt-sidecar pattern (per `engine/utils/atomic_write.h`) | log + use defaults |
| Save failure (disk full, permission) | `Result<void, WriteError>` (atomic-write didn't commit; old file intact) | surface to user; in-memory state stays dirty |
| Subsystem init failure | `bool` return + log + rollback prefix (per `SystemRegistry::initializeAll`) | caller (Engine) decides whether to abort or continue |
| Invalid argument (programmer error) | `VESTIGE_ASSERT` (debug) / UB (release) | fix the caller |
| Out of memory | `std::bad_alloc` propagates | app aborts (matches CODING_STANDARDS §11 — OOM is fatal during init; steady-state allocations bounded) |
| Subscriber callback throws inside `EventBus::publish` | propagates to publisher (no wrapper) | **policy: callbacks must not throw** — fix the callback |

No exceptions in the steady-state path (CODING_STANDARDS §11). If this subsystem does throw anywhere, justify it. State explicitly whether `Result<T, E>` / `std::expected` is in use yet; if not (subsystem predates the policy), say so honestly and link the migration plan in §15.

## 11. Testing **(required)**

| Concern | Test file | Coverage |
|---------|-----------|----------|
| Public API contract | `tests/test_<subsystem>.cpp` | feature parity |
| Integration with X | `tests/test_<subsystem>_X_integration.cpp` | smoke + regression |
| Determinism / replay | `tests/test_<subsystem>_determinism.cpp` | seeded |

Per the project rule (every feature + bug fix gets a test), no untested public functions.

**Adding a test for this subsystem (worked-example template):**

> Drop a new `tests/test_<thing>.cpp` next to its peers; link against `vestige_engine` + `gtest_main` in `tests/CMakeLists.txt` (auto-discovered via `gtest_discover_tests`). Use `<key public type>` directly without an `Engine` instance — every primitive in this subsystem **except `<the GPU/window-bound types if any>`** is unit-testable headlessly. Visual / GLFW-bound paths exercise via `engine/testing/visual_test_runner.h`. Deterministic seeding for randomness (`engine/testing/random_helpers.h` if present, else inline a fixed seed).

Replace the bracketed bits with the actual subsystem detail. State explicitly any **coverage gap** (paths that only the visual-test runner can exercise, or that depend on a display server in CI).

## 12. Accessibility **(required)**

The user is partially sighted (per memory). Subsystems that produce user-facing output (UI, audio cues, captions, focus rings, color choices) must list accessibility constraints here. Subsystems with no user-facing output write *"Not applicable — no user-facing output"* and the spec is good.

**Infrastructure subsystems whose role is to *route* accessibility settings** (e.g. core, settings, input, event-bus) — list every downstream surface that consumes the routing here, even though the subsystem itself produces no UX. The rule is "if a regression in this subsystem could break accessibility downstream, document the routing."

Worked-example shape for a routing subsystem:

> `<subsystem>` itself produces no user-facing pixels or sound. **However**, it is the *route* every accessibility surface flows through:
>
> - `<DataStruct>::accessibility` carries the persisted state (UI scale, high contrast, reduced motion, subtitles, color-vision filter, post-process toggles, photosensitive caps).
> - `<sink-or-event-X>` is the sole writeable path from "user toggled a checkbox" to "subsystem `<X>` behaves differently" — list every sink + the downstream subsystem each writes to.
> - `<carrier-Y>` carries `<a11y-relevant-field>` so downstream `<consumer>` can `<thing>` without re-querying the source.
>
> Constraint summary for downstream UIs that consume this subsystem: …

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

## 15. Open questions **(conditional; required for `shipped` subsystems with known unknowns)**

Known unknowns, deferred decisions, things flagged for future review.

| # | Question | Owner | Target |
|---|----------|-------|--------|
| 1 | … | git username (not "unassigned" for `shipped` subsystems) | concrete phase / date / `triage` |

**Owner discipline for shipped subsystems:** every row gets a real owner — a git username, never "unassigned." If no one owns it, the open question is either stale and should be deleted, or an action item that needs assigning before the spec ships canonical. The sole exception is rows tagged `Target: triage` — those are explicitly parked, not forgotten.

Each row also becomes an issue or a TODO in code with `// TODO(YYYY-MM-DD owner)` per CODING_STANDARDS §20.

If there are no open questions, omit the section.

## 16. Spec change log **(required)**

| Date | Spec version | Author | Change |
|------|--------------|--------|--------|
| YYYY-MM-DD | 1.0 | name | initial spec |

Bump spec version on substantive change. Link the commit hash if helpful.

---

## Style conventions

**First-use jargon expansion.** Engine acronyms and shader-specific terms (FBO, NDC, std140, RAII, FPC, FOV, IBL, SSBO, …) are expanded on first use within the spec — even when they're explained elsewhere in the docs tree. A reader landing on this spec cold should not need CODING_STANDARDS.md or ARCHITECTURE.md open in another tab.

The convention: `Frame Buffer Object (FBO)` first time, `FBO` thereafter. Common C++ standards-library / language acronyms (`UB`, `RAII`, `ABI`) expand once per spec; project-specific acronyms (`FPC` for `FirstPersonController`, `ISystem`) expand once per spec.

---

## Review checklist

Before declaring a spec ready for cold-eyes review:

- [ ] Every required section is filled (no placeholder text)
- [ ] Conditional sections either contain content or explicitly state "Not applicable — <reason>"
- [ ] All `engine/<path>:<line>` references resolve (paths exist)
- [ ] All external links work
- [ ] Performance budget either has numbers or `TBD — measure by <date>` (or the all-pending one-liner per §8)
- [ ] At least one current (≤ 1 year old) reference cited
- [ ] All §15 open questions on a `shipped` subsystem have a real owner (no "unassigned")
- [ ] First-use acronyms expanded per "Style conventions" above
- [ ] Cross-doc consistency: no contradictions with CLAUDE.md, CODING_STANDARDS.md, ARCHITECTURE.md, ROADMAP.md

After cold-eyes review pass returns, apply fixes and update the change log row.
