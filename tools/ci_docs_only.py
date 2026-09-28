#!/usr/bin/env python3
"""Is this push documentation-only? Answered from ci.yml's own path list.

.github/workflows/ci.yml's `on.push.paths-ignore` names the paths no ci.yml job
builds, tests or lints. GitHub uses it to skip ci.yml on a push touching only
those paths; the local push hook uses this script, as ants.gate.docsCommand in
.ants/gate.conf, to reach the same verdict from the same list (3D_E-0709). One
list, so the two cannot drift.

Reads the pushed paths on stdin, one per line. Exits 0 only when there is at
least one path and every path matches a pattern. Anything else exits 1: a path
that matches nothing, no paths, a missing or unreadable workflow, or a pattern
form this reader does not evaluate. A wrong answer then costs a full gate run,
never a skipped check.

Pattern semantics follow GitHub's filter syntax for the forms used: `*` matches
within one path segment, `**` across segments, `**/` also matches no directory,
`?` one character. Negation (`!`), character classes (`[`) and `+` are refused.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

import yaml

WORKFLOW = Path(__file__).resolve().parent.parent / ".github/workflows/ci.yml"


def pattern_to_regex(pattern: str) -> re.Pattern[str]:
    if pattern.startswith("!") or any(c in pattern for c in "[]+"):
        raise ValueError(f"unsupported paths-ignore pattern: {pattern!r}")
    out = []
    i = 0
    while i < len(pattern):
        if pattern.startswith("**/", i):
            out.append("(?:.*/)?")
            i += 3
        elif pattern.startswith("**", i):
            out.append(".*")
            i += 2
        elif pattern[i] == "*":
            out.append("[^/]*")
            i += 1
        elif pattern[i] == "?":
            out.append("[^/]")
            i += 1
        else:
            out.append(re.escape(pattern[i]))
            i += 1
    return re.compile("".join(out) + r"\Z")


def load_patterns(workflow: Path) -> list[re.Pattern[str]]:
    data = yaml.safe_load(workflow.read_text(encoding="utf-8"))
    # PyYAML reads the bare key `on` as the boolean True.
    triggers = data.get("on", data.get(True))
    patterns = triggers["push"]["paths-ignore"]
    if not isinstance(patterns, list) or not patterns:
        raise ValueError("on.push.paths-ignore is missing or empty")
    return [pattern_to_regex(str(p)) for p in patterns]


def main() -> int:
    try:
        patterns = load_patterns(WORKFLOW)
    except Exception as exc:  # noqa: BLE001 — any failure means "not docs-only"
        print(f"ci_docs_only: {exc}", file=sys.stderr)
        return 1
    paths = [line.strip() for line in sys.stdin if line.strip()]
    if not paths:
        return 1
    for path in paths:
        if not any(p.match(path) for p in patterns):
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
