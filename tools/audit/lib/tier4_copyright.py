# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tier 4: Copyright-header audit — ensure every source file starts with a
copyright + SPDX identifier within its first 3 (shebang-adjusted) lines.

Closes out idea #27 from AUDIT_TOOL_IMPROVEMENTS.md. See the
2026-04-19 audit write-up for the original motivation.
"""

from __future__ import annotations

import logging
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .config import Config
from .findings import Finding, Severity
from .utils import enumerate_files, relative_path

log = logging.getLogger("audit")


# Match a copyright line starting with any of the common single-line comment
# tokens: "//" (C-family, GLSL), "#" (Python, shell, YAML, CMake), "--" (Lua,
# SQL). Year may be a single year, a range "YYYY-YYYY", or a list "YYYY, YYYY".
_COPYRIGHT_RE = re.compile(
    r"^(?://|#|--)\s*Copyright\s*\(c\)\s*"
    r"(\d{4}(?:[-,\s]\d{4})*)\s+\S.*$",
    re.IGNORECASE,
)

_SPDX_RE = re.compile(r"SPDX-License-Identifier\s*:", re.IGNORECASE)

# Files/globs that are intrinsically exempt (generated, third-party boilerplate,
# or not "source" in a meaningful sense). These are skipped regardless of what
# exclude_dirs says — the detector is only ever interested in hand-written
# source files.
_EXEMPT_BASENAMES: frozenset[str] = frozenset({
    "__init__.py",   # empty package markers commonly have no header
})


@dataclass
class CopyrightResult:
    """Result of the copyright-header audit."""
    missing_files: list[dict[str, Any]] = field(default_factory=list)
    total_files: int = 0

    def to_dict(self) -> dict[str, Any]:
        return {
            "missing_files": self.missing_files[:50],
            "total_files": self.total_files,
            "missing_count": len(self.missing_files),
        }


def _check_header(path: Path, rel: str) -> dict[str, Any] | None:
    """Return a finding dict if the file's header is missing/malformed, else None.

    A valid header is:
      * a copyright line matching `_COPYRIGHT_RE`, AND
      * an SPDX-License-Identifier line
    both appearing within the first 3 lines of the file (or the first 3
    lines *after* a shebang, i.e. lines 2–4 when line 1 starts with ``#!``).
    """
    try:
        with open(path, "r", errors="replace") as f:
            # Read at most the first 5 lines — shebang + 3 header lines + 1
            # slack line. No need to pull in an arbitrary file.
            lines = [next(f, "").rstrip("\n") for _ in range(5)]
    except OSError:
        return None

    # If the first line is a shebang, the header check window shifts by one.
    offset = 1 if lines and lines[0].startswith("#!") else 0
    window = lines[offset:offset + 3]

    copyright_ok = any(_COPYRIGHT_RE.match(line) for line in window)
    spdx_ok = any(_SPDX_RE.search(line) for line in window)

    if copyright_ok and spdx_ok:
        return None

    reason_parts: list[str] = []
    if not copyright_ok:
        reason_parts.append("no Copyright line in first 3 lines")
    if not spdx_ok:
        reason_parts.append("no SPDX-License-Identifier in first 3 lines")

    return {
        "file": rel,
        "reason": "; ".join(reason_parts),
    }


def analyze_copyright(config: Config) -> tuple[CopyrightResult, list[Finding]]:
    """Detect source files missing a Copyright + SPDX header."""
    result = CopyrightResult()
    findings: list[Finding] = []

    if not config.get("tier4", "copyright", "enabled", default=True):
        log.info("Tier 4 copyright: disabled")
        return result, findings

    max_findings = config.get(
        "tier4", "copyright", "max_findings", default=50
    )

    # Enumerate all configured source files. Shader directories are handled
    # transparently — the regex accepts "//" too, which covers .glsl. Python
    # files are picked up via the "#" branch, CMakeLists.txt likewise.
    extensions = list(config.source_extensions)
    # Include shader extensions if shader dirs are configured — a shader's
    # header is just as important for license attribution.
    shader_exts = [".glsl", ".vert", ".frag", ".geom", ".comp", ".tesc", ".tese"]
    for ext in shader_exts:
        if ext not in extensions:
            extensions.append(ext)
    # Include CMakeLists.txt so build scripts get the same check.
    if ".txt" not in extensions:
        extensions.append(".txt")
    # Include .py for Python-heavy projects (the audit tool itself).
    if ".py" not in extensions:
        extensions.append(".py")

    source_roots = list(config.source_dirs) + list(config.shader_dirs)
    all_files = enumerate_files(
        root=config.root,
        source_dirs=source_roots,
        extensions=extensions,
        exclude_dirs=config.exclude_dirs,
    )

    # Post-filter: only hand-written source files. Drop .txt files that
    # aren't CMakeLists, and exempt basenames.
    filtered: list[Path] = []
    for f in all_files:
        if f.name in _EXEMPT_BASENAMES:
            continue
        if f.suffix == ".txt" and f.name != "CMakeLists.txt":
            continue
        filtered.append(f)

    result.total_files = len(filtered)
    log.info("Tier 4 copyright: checking %d files", len(filtered))

    for fpath in filtered:
        rel = relative_path(fpath, config.root)
        problem = _check_header(fpath, rel)
        if problem is None:
            continue
        result.missing_files.append(problem)

    # Emit findings (capped).
    for item in result.missing_files[:max_findings]:
        findings.append(Finding(
            file=item["file"],
            line=1,
            severity=Severity.LOW,
            category="copyright",
            source_tier=4,
            title=f"Missing copyright header: {item['file']}",
            detail=item["reason"],
            pattern_name="missing_copyright_header",
        ))

    log.info(
        "Tier 4 copyright: %d/%d files missing header",
        len(result.missing_files), result.total_files,
    )
    return result, findings
