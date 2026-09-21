#!/usr/bin/env python3
# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT
"""Input-poll audit — no raw key poll reachable from gameplay code.

Automates the two greps specified in ``docs/engine/input/spec.md`` § 12
("Every binding rebindable"). That section defines the rule and says the
check must be wired into the suite as part of 3D_E-0628, in the same
change that makes it pass -- which is this one.

The rule: every game verb goes through ``InputManager::isActionDown``
against a registered ``InputActionMap``. A raw key poll bypasses the
binding layer, so rebinding does nothing and a non-QWERTY layout gets
the wrong physical keys.

Two checks, because one is not enough:

  1. No bare ``glfwGetKey(`` outside the single module allowed to make
     it. This one has always passed.

  2. No caller of a raw-poll wrapper -- ``isKeyDown(`` or
     ``isMouseButtonDown(`` -- outside that same module. This is the one
     that matters. Until 3D_E-0628 it reported 14 hits, all in
     first_person_controller.cpp: every movement verb in the engine
     bypassed the binding system through that wrapper.

Why check 2 covers ``isMouseButtonDown`` as well: it is a raw-poll
wrapper of exactly the same shape over a bindable device
(``InputDevice::Mouse``). It has no call site today, so it is latent --
which is precisely how the original one-grep check stayed green for
months while 100% of movement bypassed the bindings.

Why both checks scan headers as well as .cpp: a ``.cpp``-only search
cannot see a poll from an inline helper in a header.

The exemption is FILE-SCOPED BY CHOICE and is scoped to the path prefix
``engine/core/input_manager.`` -- that module's header and implementation
and nothing else. A file-scoped exemption cannot see a wrapper living
inside it, which is the trap the spec section documents: check 1 returned
green for months because the one bare ``glfwGetKey`` sits inside
``isKeyDown``, in the very file check 1 exempts. Check 2 exists to close
that.

Exit status: 0 clean, 1 on any violation, 2 on a usage error.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# Directories scanned, relative to --root. Mirrors the spec's greps.
SCAN_DIRS = ("engine", "app")

SCAN_SUFFIXES = (".cpp", ".h")

# Path prefix (POSIX, relative to --root) exempt from BOTH checks.
EXEMPT_PREFIX = "engine/core/input_manager."

CHECKS = (
    (
        "bare-glfw-poll",
        re.compile(r"\bglfwGetKey\("),
        "bare GLFW key poll outside engine/core/input_manager.",
    ),
    (
        "raw-poll-wrapper",
        re.compile(r"\b(?:isKeyDown|isMouseButtonDown)\("),
        "raw-poll wrapper call bypassing the InputActionMap binding layer",
    ),
)


def iter_sources(root: Path):
    """Yield (relative_posix_path, absolute_path) for every scanned file."""
    for directory in SCAN_DIRS:
        base = root / directory
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix in SCAN_SUFFIXES and path.is_file():
                yield path.relative_to(root).as_posix(), path


def scan(root: Path):
    """Return a list of (check_name, rel_path, line_no, text, description)."""
    violations = []
    for rel, path in iter_sources(root):
        if rel.startswith(EXEMPT_PREFIX):
            continue
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError as exc:  # unreadable file is a real failure, not a skip
            violations.append(("unreadable", rel, 0, str(exc), "could not read"))
            continue
        for number, text in enumerate(lines, start=1):
            for name, pattern, description in CHECKS:
                if pattern.search(text):
                    violations.append((name, rel, number, text.strip(), description))
    return violations


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Fail if a raw key poll is reachable from gameplay code."
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
        help="Tree to scan (default: the repository root).",
    )
    args = parser.parse_args(argv)

    root = args.root.resolve()
    if not root.is_dir():
        print(f"input_poll_audit: --root is not a directory: {root}", file=sys.stderr)
        return 2

    scanned = sum(1 for _ in iter_sources(root))
    if scanned == 0:
        print(
            f"input_poll_audit: no sources found under {root} -- "
            f"expected {'/, '.join(SCAN_DIRS)}/ with .cpp/.h files. "
            "A check that scans nothing passes vacuously, which is worse "
            "than no check.",
            file=sys.stderr,
        )
        return 2

    violations = scan(root)
    if not violations:
        print(f"input_poll_audit: clean ({scanned} files scanned).")
        return 0

    print(
        f"input_poll_audit: {len(violations)} violation(s) "
        f"({scanned} files scanned).",
        file=sys.stderr,
    )
    for name, rel, number, text, description in violations:
        print(f"  [{name}] {rel}:{number}: {text}", file=sys.stderr)
        print(f"      {description}", file=sys.stderr)
    print(
        "\nEvery game verb must be consumed via InputManager::isActionDown "
        "against a registered InputActionMap -- see docs/engine/input/spec.md "
        "§ 12. Do NOT widen the exemption to silence this: a file-scoped "
        "exemption that hides a wrapper is how this check stayed green while "
        "every movement verb bypassed the binding system.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
