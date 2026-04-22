# Contributing to Vestige

Thank you for your interest in contributing! Vestige is a solo-maintained,
open-source 3D engine ([MIT License](LICENSE)). Contributions are welcome,
and this document explains how to participate effectively.

> **Not a developer?** You can still help — Vestige needs **testers** on
> GPUs and OSes the maintainer doesn't own. See [`TESTING.md`](TESTING.md)
> for a 10-minute smoke-test script and how to file reports. No build
> environment required; pre-built binaries ship on every release.

> **Project status**: Vestige is early-stage. The API is unstable until 1.0.
> Expect breaking changes between minor versions during the 0.x series.
> See [ROADMAP.md](ROADMAP.md) for the current direction.

---

## TL;DR for first-time contributors

1. **Discuss first** — open an issue describing what you'd like to change before
   investing significant time. Avoids wasted effort on changes that don't fit
   the architecture or roadmap.
2. **Build it from source** (instructions below). Confirm the engine compiles
   and the tests pass on your machine.
3. **Make your change** following the [Coding Standards](CODING_STANDARDS.md)
   and the [project rules in CLAUDE.md](CLAUDE.md).
4. **Add tests** for new features and bug fixes ([feedback policy](CLAUDE.md#key-rules)
   — every feature update should ship with tests).
5. **Run the audit tool** locally and confirm it reports nothing new from
   your change (`python3 tools/audit/audit.py -t 1 2 3`).
6. **Sign off** every commit (`git commit -s`) — this is the DCO (Developer
   Certificate of Origin) attestation. See [DCO Sign-Off](#dco-sign-off) below.
7. **Open a PR** with a clear description of *what* changed and *why*.

---

## Building from source

### Requirements

- **C++17** compiler (GCC 9+, Clang 10+, MSVC 2019+)
- **CMake** 3.20 or newer
- **OpenGL 4.5** capable GPU and driver
- **Linux**: `libgl1-mesa-dev`, `xorg-dev`, ALSA dev headers (for OpenAL Soft)
- **Windows**: Visual Studio 2019+ with C++ workload

### Quick build

```bash
git clone https://github.com/milnet01/Vestige.git
cd Vestige
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The first build pulls third-party deps via CMake `FetchContent` and takes
several minutes. Subsequent builds are incremental.

### Running the demo scene

```bash
./build/Vestige
```

Note: a fresh public clone may render with placeholder materials for two
demo blocks (Gold, Wood) because their original textures are not
redistributable in the open-source repo. See [ASSET_LICENSES.md](ASSET_LICENSES.md).

---

## Coding standards

All C++ code must follow [CODING_STANDARDS.md](CODING_STANDARDS.md):

- Files: `snake_case.cpp` / `snake_case.h`, one class per file
- Classes: `PascalCase`
- Functions: `camelCase`
- Members: `m_camelCase`
- Constants: `UPPER_SNAKE_CASE`
- Braces: Allman style (opening brace on new line)
- Indentation: 4 spaces
- `#pragma once` for include guards

In addition, the project rules in [CLAUDE.md](CLAUDE.md) apply to all
contributors (not just AI-assisted work):

- **Research before implementation** — major features start with a design
  document before code is written.
- **No workarounds without root-cause investigation** — symptom-suppressing
  patches (capping iterations, swallowing exceptions, disabling checks)
  require explicit justification and a comment explaining why a clean fix
  is not possible.
- **60 FPS minimum** — rendering and per-frame logic must sustain ≥60 FPS
  on the reference hardware (AMD RX 6600 / Ryzen 5 5600).
- **Use the Formula Workbench** for numerical formulas instead of
  hand-coded magic constants. See `tools/formula_workbench/`.
- **Security first** — see [SECURITY.md](SECURITY.md). Memory safety,
  input validation at boundaries, no shell injection.

---

## Tests

Every new feature or bug fix must ship with tests. We use **Google Test**.

```bash
# Build and run all tests
ctest --test-dir build --output-on-failure

# Run a specific test
./build/tests/Vestige_tests --gtest_filter='YourTestName*'
```

Test files live in `tests/` and follow `test_<thing>.cpp` naming. Each
top-level engine subsystem (`engine/<name>/`) should be referenced by at
least one test file. The audit tool's Tier 6 sweep flags subsystems
that lack coverage.

For UI/visual changes, run the engine and verify the change in a browser
or screenshot — automated tests verify code correctness, not visual
correctness.

---

## Run the audit tool before opening a PR

The repository ships with a custom audit tool at `tools/audit/`. Run it
against your change to catch issues before review:

```bash
# Quick local check (build + static analysis + pattern scan)
python3 tools/audit/audit.py -t 1 2 3

# Full audit (adds statistics + research + feature coverage)
python3 tools/audit/audit.py
```

Reports go to `docs/AUTOMATED_AUDIT_REPORT.md`. PRs that add or modify
audit-tool code must also bump the tool's `VERSION` and `CHANGELOG.md`.

If the audit tool flags something in your change that you believe is a
false positive, you can suppress it via `.audit_suppress` (with
justification) — see `tools/audit/lib/suppress.py`. If the audit
flags something that is intentional but worth tracking, mark it
verified via `--verified-add <key>` instead.

---

## DCO sign-off

Vestige uses the **Developer Certificate of Origin** (DCO) instead of a
heavyweight Contributor License Agreement (CLA). Every commit must
include a `Signed-off-by:` line attesting that you have the right to
contribute the code:

```
Signed-off-by: Your Name <your.email@example.com>
```

The easiest way to add it is to commit with `-s`:

```bash
git commit -s -m "your commit message"
```

By signing off, you confirm the [DCO 1.1 statement](https://developercertificate.org/):
that the contribution was created in whole or in part by you and you
have the right to submit it under MIT.

PRs without sign-off will be asked to add it before merging.

---

## AI-assisted contributions (welcome, must be disclosed)

Vestige itself is developed with heavy use of Anthropic's Claude Code,
and AI-assisted contributions are explicitly welcome. Two things are
expected:

1. **Disclose** AI use in the PR description. A one-line note like
   "drafted with Claude Code, reviewed and tested locally by me" is
   sufficient.
2. **You own the outcome.** "The AI wrote it" is never an excuse for an
   unreviewed change. The committer is expected to have read,
   understood, and validated every line they sign off on. AI is a
   tool, not a contributor — you are the contributor.

This is a transparency norm, not a filter. AI-assisted PRs are evaluated
on the same merits as any other PR.

---

## What to expect after opening a PR

- **Triage cadence**: this is a solo-maintained project. Expect a first
  response within a week or so. No formal SLA.
- **CI must be green** before review. Push fixes if CI fails.
- **Focused, narrow PRs** are easier to review than sweeping refactors.
  If your change touches many subsystems, consider splitting it.
- **Constructive feedback** — reviewers will ask for revisions. Don't
  take it personally; the goal is a maintainable engine, not a
  competition.

---

## What kinds of contributions are most useful

In rough order of usefulness:

1. **Bug fixes** with reproducer tests
2. **Platform compatibility** — getting Vestige building/running on
   GPUs and OS configurations the maintainer doesn't have access to
3. **Performance improvements** with profiler measurements
4. **Test coverage** for under-tested subsystems (see audit tool's Tier 6)
5. **Documentation** improvements
6. **New features** — discuss in an issue first; not all features fit
   the architecture or roadmap

---

## Reporting security issues

**Do not** open a public issue for security vulnerabilities. See
[SECURITY.md](SECURITY.md) for the disclosure process.

---

## License

By contributing to Vestige, you agree that your contribution is
licensed under the MIT License (see [LICENSE](LICENSE)) and you have
attested to the [DCO](https://developercertificate.org/) by signing
off your commits. The project will not be relicensed away from MIT —
contributors can rely on their work staying under permissive open-source
terms.
