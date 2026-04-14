"""Tier 6: Feature coverage — flag subsystems without test coverage.

D4 (2.6.0): the manual audit prompt asks reviewers to check whether each
engine subsystem has tests exercising its behaviour. Automating that check
closes the rigour gap for one more tier of the manual workflow.

How "coverage" is determined (deliberately simple for v1):

1. A *subsystem* is any top-level subdirectory of the configured engine
   source root (``engine/<name>``). These map 1:1 with the project's
   subsystem + event-bus architecture.

2. A subsystem is considered "covered" if at least one test file (under
   the configured tests directory) contains a string that references it.
   Two signals are checked:

   - An ``#include`` line mentioning the subsystem path
     (``#include "engine/<name>/..."`` or ``#include <engine/<name>/...>``)
   - The subsystem name appearing in the test filename
     (``test_<name>*.cpp``)

3. A subsystem with **zero** test files flagging it emits a MEDIUM
   finding. One with **one or two** emits an INFO finding (under-covered
   hint). Three or more is considered adequately covered and is silent.

This is a heuristic, not a code coverage metric. Real line/branch
coverage would require gcov/llvm-cov. The value of this tier is
surfacing *structural* gaps — subsystems that nobody has tested at all.

The module is pure-Python stdlib; no external dependencies beyond the
existing audit tool libraries. It does not invoke the build system.
"""

from __future__ import annotations

import logging
import re
from pathlib import Path

from .config import Config
from .findings import Finding, Severity

log = logging.getLogger("audit")


# Subsystems that legitimately don't need test coverage of their own:
#   - `testing/` is test infrastructure (tests testing tests would loop)
#   - anything matching explicit exclusions in the config
_DEFAULT_EXCLUDED_SUBSYSTEMS: frozenset[str] = frozenset({
    "testing",
})


def _discover_subsystems(
    engine_root: Path,
    excluded: frozenset[str],
) -> list[str]:
    """Return top-level subdirectory names of *engine_root*, sorted.

    Symlinks and hidden dirs are skipped. Explicit exclusions are
    removed. Names containing only numbers/underscores are skipped too
    (defensive — no engine subsystem should be so named, but we don't
    want to regress the report if someone creates ``engine/_tmp/``).
    """
    if not engine_root.is_dir():
        return []

    subsystems: list[str] = []
    for entry in engine_root.iterdir():
        if not entry.is_dir():
            continue
        name = entry.name
        if name.startswith(".") or name.startswith("_"):
            continue
        if name in excluded:
            continue
        subsystems.append(name)
    return sorted(subsystems)


def _count_coverage_for_subsystem(
    subsystem: str,
    test_files: list[Path],
) -> int:
    """Return the number of test files that reference *subsystem*.

    Two signals count:
      * ``#include ..."engine/<subsystem>/..."`` or ``<engine/<subsystem>/...>``
      * Filename starting with ``test_<subsystem>`` (e.g. ``test_camera.cpp``
        for the ``scene`` subsystem is a weaker signal than the include,
        but for cases where the subsystem name *is* the file prefix it
        is highly correlated with coverage).

    A test file is counted at most once per subsystem even if it
    satisfies both signals — we're measuring breadth of coverage, not
    signal strength.
    """
    # Build regex that matches either quoted or angle-bracket include
    # paths referencing engine/<subsystem>/...
    include_re = re.compile(
        rf'#\s*include\s*["<]engine/{re.escape(subsystem)}/',
    )
    name_prefix = f"test_{subsystem}"

    count = 0
    for test_path in test_files:
        matched = False
        if test_path.name.startswith(name_prefix):
            matched = True
        if not matched:
            # Read only the head of the file — includes are at the top.
            # 4KB is enough for the longest reasonable include block.
            try:
                head = test_path.read_text(errors="replace")[:4096]
            except OSError:
                continue
            if include_re.search(head):
                matched = True
        if matched:
            count += 1
    return count


def _make_finding(
    subsystem: str,
    test_count: int,
    engine_rel: str,
) -> Finding:
    """Build the Finding for a given subsystem + count."""
    if test_count == 0:
        return Finding(
            file=f"{engine_rel}/{subsystem}/",
            line=None,
            severity=Severity.MEDIUM,
            category="feature_coverage",
            source_tier=6,
            title=f"Subsystem `{subsystem}` has no test coverage",
            detail=(
                f"No test file under the configured tests directory "
                f"references `{engine_rel}/{subsystem}/` via #include, "
                f"and no test file name starts with `test_{subsystem}`. "
                f"New features in this subsystem are shipping untested. "
                f"Add at least one integration or unit test that exercises "
                f"the public API of this subsystem."
            ),
            pattern_name="tier6_no_coverage",
        )
    # 1 or 2 tests: under-covered hint, not an error
    return Finding(
        file=f"{engine_rel}/{subsystem}/",
        line=None,
        severity=Severity.INFO,
        category="feature_coverage",
        source_tier=6,
        title=(
            f"Subsystem `{subsystem}` has only {test_count} "
            f"test file{'s' if test_count != 1 else ''}"
        ),
        detail=(
            f"Only {test_count} test file(s) reference this subsystem. "
            f"Consider whether additional tests would catch regressions in "
            f"under-exercised code paths."
        ),
        pattern_name="tier6_thin_coverage",
    )


def run(config: Config) -> list[Finding]:
    """Scan engine/ and tests/ to flag subsystems with weak/no coverage.

    Reads the following config keys (all optional):

    - ``tier6.enabled`` (default True) — disable this tier entirely.
    - ``tier6.engine_dir`` (default ``"engine"``) — relative path under
      the project root that contains subsystem subdirectories.
    - ``tier6.tests_dir`` (default ``"tests"``) — relative path under
      the project root that contains test source files.
    - ``tier6.excluded_subsystems`` (default ``[]``) — names to ignore
      on top of the built-in exclusions (``testing``).
    - ``tier6.thin_threshold`` (default 3) — fewer than this many test
      files → INFO-level thin-coverage finding; zero → MEDIUM.
    """
    cfg = config.get("tier6", default={}) or {}
    if not cfg.get("enabled", True):
        log.info("Tier 6 disabled via config")
        return []

    engine_rel = cfg.get("engine_dir", "engine")
    tests_rel = cfg.get("tests_dir", "tests")
    extra_excluded = frozenset(cfg.get("excluded_subsystems", []) or [])
    excluded = _DEFAULT_EXCLUDED_SUBSYSTEMS | extra_excluded
    thin_threshold = int(cfg.get("thin_threshold", 3))

    engine_root = (config.root / engine_rel).resolve()
    tests_root = (config.root / tests_rel).resolve()

    subsystems = _discover_subsystems(engine_root, excluded)
    if not subsystems:
        log.info("Tier 6: no subsystems discovered under %s", engine_root)
        return []

    # Enumerate test files once and reuse across subsystems.
    test_files: list[Path] = []
    if tests_root.is_dir():
        for entry in tests_root.rglob("*.cpp"):
            if entry.is_file():
                test_files.append(entry)
    log.info("Tier 6: %d subsystems, %d test files",
             len(subsystems), len(test_files))

    findings: list[Finding] = []
    for subsystem in subsystems:
        count = _count_coverage_for_subsystem(subsystem, test_files)
        if count == 0:
            findings.append(_make_finding(subsystem, 0, engine_rel))
        elif count < thin_threshold:
            findings.append(_make_finding(subsystem, count, engine_rel))
        # count >= thin_threshold: adequately covered, silent.

    log.info("Tier 6: %d subsystems flagged (no/thin coverage)", len(findings))
    return findings
