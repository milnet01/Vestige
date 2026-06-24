#!/usr/bin/env python3
# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT
"""Shader standards lint — keeps every GLSL shader on the engine's GL 4.5 target.

The engine targets OpenGL 4.5 (CLAUDE.md). A shader that declares a higher
`#version` — or uses a built-in that is only core in a higher version — still
compiles on the RX 6600 dev card (GL 4.6) but fails on genuine 4.5 hardware and
on CI's software renderer (Mesa llvmpipe, GLSL 4.50 max). That gap is invisible
locally, so this static line-scan catches it before it ships. (It is exactly
the regression that broke run 28092225316: scene.vert.glsl at `#version 460`.)

STRICT by default. Fails the build (exit 1) on:

  1. A `#version` directive that is not `450 core`. The engine's single GL 4.5
     target means every shader is `#version 450 core`. A shader that genuinely
     needs a different version must say why with a trailing
     `// shader-lint-exempt: <reason>` on the `#version` line (CLAUDE.md Rule 8 —
     an off-target pin needs a written reason).

  2. A suffixless draw-parameter built-in — `gl_BaseInstance`, `gl_BaseVertex`,
     or `gl_DrawID`. These are core only in GLSL 4.60. At 450 the
     `GL_ARB_shader_draw_parameters` extension provides the ARB-suffixed names
     (`gl_BaseInstanceARB`, …), which carry the identical value and compile on
     strict and lenient 4.5 drivers alike. The same
     `// shader-lint-exempt: <reason>` escape hatch applies per line.

Detection is a lightweight regex line-scan (same approach as
tools/localization_audit.py), not a GLSL parse. `--lint` downgrades the checks
to warnings (exit 0) for local pre-commit runs.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# The one allowed version directive. Engine targets GL 4.5 → GLSL 4.50 core.
_ALLOWED_VERSION = "450 core"
_VERSION = re.compile(r"^\s*#version\s+(.+?)\s*$")

# Draw-parameter built-ins that are core only in GLSL 4.60. The negative
# lookahead lets the ARB-suffixed forms (gl_BaseInstanceARB, …) pass.
_BARE_DRAW_PARAM = re.compile(r"\bgl_(?:BaseInstance|BaseVertex|DrawID)(?!ARB)\b")

_EXEMPT = "// shader-lint-exempt:"

_SHADER_SUFFIXES = (".glsl", ".vert", ".frag", ".comp", ".geom", ".tesc", ".tese")


def scan_shaders(roots: list[Path]) -> tuple[list[str], int]:
    """Return (violations, shader_count).

    violations: human-readable "file:line: message" strings.
    """
    violations: list[str] = []
    count = 0
    for root in roots:
        for path in sorted(root.rglob("*")):
            if path.suffix not in _SHADER_SUFFIXES or not path.is_file():
                continue
            try:
                lines = path.read_text(encoding="utf-8").splitlines()
            except (UnicodeDecodeError, OSError):
                continue
            count += 1
            for n, line in enumerate(lines, start=1):
                if _EXEMPT in line:
                    continue
                loc = f"{path}:{n}"

                m = _VERSION.match(line)
                if m and m.group(1) != _ALLOWED_VERSION:
                    violations.append(
                        f"{loc}: #version is `{m.group(1)}`, expected "
                        f"`{_ALLOWED_VERSION}` (engine targets GL 4.5). Add a "
                        f"trailing `{_EXEMPT} <reason>` if intentional.")

                if _BARE_DRAW_PARAM.search(line):
                    violations.append(
                        f"{loc}: suffixless draw-parameter built-in (4.60-core); "
                        f"use the ARB-suffixed form (e.g. gl_BaseInstanceARB) "
                        f"valid at 450 with GL_ARB_shader_draw_parameters.")
    return violations, count


def main(argv: list[str] | None = None) -> int:
    repo = Path(__file__).resolve().parent.parent

    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--strict", action="store_true", default=True,
                      help="fail the build on any violation (default)")
    mode.add_argument("--lint", dest="strict", action="store_false",
                      help="downgrade violations to warnings (exit 0)")
    ap.add_argument("--root", action="append", type=Path, default=None,
                    help="shader root to scan (repeatable; "
                         "default: <repo>/assets/shaders)")
    args = ap.parse_args(argv)

    roots = args.root or [repo / "assets" / "shaders"]
    for root in roots:
        if not root.is_dir():
            print(f"shader_lint: root not found: {root}", file=sys.stderr)
            return 2

    violations, count = scan_shaders(roots)

    if violations:
        label = "ERROR" if args.strict else "warning"
        print(f"[{label}] {len(violations)} shader-standard violation(s):")
        for v in violations:
            print(f"  {v}")
        if args.strict:
            return 1
        return 0

    print(f"shader_lint: clean — {count} shader(s) on `#version "
          f"{_ALLOWED_VERSION}`.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
