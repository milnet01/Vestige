# Dependency Standards

How Vestige keeps every dependency current — and how it records the rare,
deliberate exception so the exception never rots into permanent neglect.

This standard specialises project `CLAUDE.md` Rule 8 and global Rule 5/5a–5c.
It does not replace them; it adds the **Breaking-Version Registry** (§4) and a
mandatory re-test trigger for every **Category (A)** pin (§2(A)). Category (B)
branch-commit pins carry a re-evaluate trigger in `THIRD_PARTY_NOTICES.md`
instead (§2(B)).

---

## 1. The rule — latest, always

Every dependency stays on its **latest stable release**. This covers:

- **Build-time source deps** — CMake `FetchContent` in `external/CMakeLists.txt`
  (GLFW, GLM, Dear ImGui, Jolt, OpenAL Soft, GoogleTest, …).
- **Vendored single-headers** committed into the tree.
- **System packages** the build/CI assume (cppcheck, clang-tidy, Python libs).
- **CI actions** pinned by commit SHA in `.github/workflows/*.yml`.
- **Runtimes / runner images** (`ubuntu-24.04`, Python, the cmake version pins).

"Latest" is a **security** requirement, not only a features one: a stale
dependency is a stale set of unpatched CVEs. Currency is the default posture;
falling behind is the thing that needs a justification, never the reverse.

## 2. When a non-latest pin is allowed

One of two conditions — nothing else — split on a single question: **does a
suitable released tag exist to move to?**

**(A) A suitable released tag exists, but the newest one breaks.** That newer
*release* was actually tried and it **breaks the build, a test, or a runtime
behaviour** — with evidence (a CI run URL, an error excerpt, a repro) — and the
breakage can't be absorbed with a caller-side change (per Rule 5b,
bump-and-fix-callers is the first choice, not the pin). You hold below that
release. Recorded in the **Breaking-Version Registry (§4)** with a re-test
trigger. Example: a CI action whose new release changes a default that reddens
the build.

**(B) No suitable released tag exists at all.** Upstream publishes no tag that
carries what the project needs, so the pin is an **exact commit** (a
reproducible, byte-stable pin — not a moving branch ref). This covers both
directions: pinning *forward* to a commit for a fix/feature that lives only on a
branch, and pinning *back* to an older commit because a newer commit breaks.
Recorded in `THIRD_PARTY_NOTICES.md`'s "Branch-commit pins" section. Examples:
Dear ImGui's docking branch (`master` lacks it); ImGuizmo held below a newer
commit that broke GCC 14; ImPlot whose last tag predates the ImGui API it needs.

Not valid reasons (each is an instruction to bump, not to pin):
"we haven't tested it yet", "it's newer so it might break", "it works today",
"nobody asked for the update".

## 3. What a pin must record

**Both categories — a reason at the pin site.** A one-line comment next to the
`FetchContent_Declare`, the action SHA, or the package line, so a reader in that
file knows the pin is deliberate, not stale.

**Category (A) — additionally a Registry row (§4) with a re-test trigger.** The
trigger names the exact **breaking version**, so when upstream ships anything
*newer than that* the pin is re-tested and lifted if the breakage is gone. Where
the dep *is* also carried in `THIRD_PARTY_NOTICES.md` (source / vendored libs),
keep its version row there current too and mark it a held pin pointing to the
Registry, so the older version there reads as deliberate. A CI action is **not**
listed in `THIRD_PARTY_NOTICES.md`. So for actions the Registry row is the
reason-of-record; add a short pointer in the workflow comment (e.g. `# v1.6.1 —
held, see DEPENDENCY_STANDARDS.md`) so a reader in the workflow sees it too.

**Category (B) — the reason + re-evaluate trigger live in
`THIRD_PARTY_NOTICES.md`'s "Branch-commit pins" section.** If a newer commit also
breaks, re-pin to the latest good commit and update the trigger there.

A trigger is mandatory because a pin with none silently becomes permanent rot —
nobody knows when to re-check.

## 4. Breaking-Version Registry

One row per **Category (A)** dependency — pinned below a *released version*
because that release breaks. It deliberately **excludes Category (B)
branch-commit pins**: those live in `THIRD_PARTY_NOTICES.md`'s "Branch-commit
pins" section, which already carries each one's reason + re-evaluate trigger, so
a row here would be a second home for one fact. Remove a row the moment its pin
is lifted (the registry lists only *live* exceptions; lifted pins live in git
history + the CHANGELOG).

| Dependency | Latest avail. | Pinned at | Breaking version | What breaks (evidence) | Re-test when | Last checked |
|------------|---------------|-----------|------------------|------------------------|--------------|--------------|
| `awalsh128/cache-apt-pkgs-action` (CI) | 1.6.2 | **1.6.1** | **1.6.2** | New `empty_packages_behavior: error` default → the "Install … dependencies" step exits code 3 before the build runs; reddens every apt-using job. Dependabot PR #14, jobs failed in ~10 s. | `> 1.6.2` ships (1.6.3+) | 2026-07-03 |

Column meanings:
- **Pinned at** — the version the project currently holds. May use the version
  label the SHA maps to (e.g. `1.6.1` for a `@<sha>  # v1.6.1` action pin).
- **Breaking version** — the exact release that failed.
- **Re-test when** — the condition that reopens the pin (usually "> breaking
  version ships"; sometimes "upstream issue #N closed").
- **Last checked** — date the trigger was last evaluated. On any dep-adjacent
  work, re-evaluate rows whose upstream has moved past the breaking version and
  bump the date (or lift the pin).

Re-testing a row: when the trigger fires, try the latest release. If it's fixed,
lift the pin and delete the row. **If the newer release still breaks — for any
reason — update Latest avail. + Breaking version + Re-test when to that release
and bump Last checked** — the row stays live, now blocking on the newer failing
version.

## 5. Sweep posture — check, don't wait

Don't wait for a break to force a bump (global Rule 5c):

- Dependabot (`.github/dependabot.yml`) opens bump PRs for the `github-actions`
  ecosystem automatically — triage them promptly, don't let them pile up. (A
  `pip` block is configured too but dormant until `tools/audit/` gains a Python
  manifest — it is stdlib-only today.)
- On any bump-adjacent work (touching `external/CMakeLists.txt`, a CI workflow,
  `THIRD_PARTY_NOTICES.md`), glance at the version pins you pass and re-check any
  live registry row.
- For deps without a bot (FetchContent tags, system tools), run the ecosystem's
  `outdated` equivalent at the start of a release cycle.

## 6. Every bump gets a cold-eyes review

Per Rule 8, a dependency upgrade is reviewed by a **fresh subagent with no
authoring context** (`/cold-eyes` for the notes/registry change, `/indie-review`
for any caller-code the bump touches). Lifting a registry pin is a bump — it
gets the same review. The bump and its caller-idiom refresh (Rule 5b) ship in
one change, so the codebase never rots into "compiles but nobody meant it."

## See also

- `CLAUDE.md` Rule 8 (the one-line rule) · global `~/.claude/CLAUDE.md` Rule 5.
- `THIRD_PARTY_NOTICES.md` — the dependency + license + version list.
- `.github/dependabot.yml` — automated bump PRs.
- `AUDIT_STANDARDS.md` Tier 5 (Online Research) — includes CVE research on current deps.
