"""Shared utilities: subprocess execution, file enumeration, token estimation.

Subprocess policy (AUDIT.md §C1 / FIXPLAN C1b):
- ``run_cmd(cmd: list[str], ...)`` is the default — ``shell=False``, no
  shell metachar interpretation, no string concatenation → no shell
  injection surface.
- ``run_shell_cmd(cmd: str, ...)`` is an explicit opt-in for commands
  the *user* authors in their config (``build_cmd``, ``test_cmd``) which
  may legitimately need `cd`, `&&`, or redirection. All user-facing
  shell commands should flow through this function so the shell=True
  surface is grep-able and auditable. The Flask web UI validates
  user-reachable fields (``base_ref``, paths) at the HTTP boundary
  (see web/app.py `_is_safe_path`, `_GIT_REF_RE`) so the attacker can
  no longer steer config values into shell metachar territory.
"""

from __future__ import annotations

import logging
import subprocess
import time
from pathlib import Path

log = logging.getLogger("audit")


def run_cmd(
    cmd: list[str],
    cwd: str | Path | None = None,
    timeout: int = 300,
) -> tuple[int, str, str]:
    """Run a command without a shell and return (returncode, stdout, stderr).

    ``cmd`` MUST be a list of strings. Passing a single string will raise
    ``TypeError`` — that's deliberate; use ``run_shell_cmd`` if you
    really need a shell.
    """
    if not isinstance(cmd, list):
        raise TypeError(
            f"run_cmd requires a list[str] (got {type(cmd).__name__}); "
            f"use run_shell_cmd for shell-interpreted commands"
        )
    log.debug("Running (no shell): %s", cmd)
    start = time.monotonic()
    try:
        result = subprocess.run(
            cmd,
            cwd=cwd,
            shell=False,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        elapsed = time.monotonic() - start
        log.debug("  -> %d in %.1fs", result.returncode, elapsed)
        return result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired:
        elapsed = time.monotonic() - start
        log.warning("Command timed out after %.1fs: %s", elapsed, cmd)
        return -1, "", f"TIMEOUT after {timeout}s"
    except FileNotFoundError as e:
        log.warning("Command not found: %s", e)
        return -1, "", str(e)


def run_shell_cmd(
    cmd: str,
    cwd: str | Path | None = None,
    timeout: int = 300,
) -> tuple[int, str, str]:
    """Run a shell-interpreted command; intended for user-authored strings.

    Use ONLY when the caller explicitly needs shell metacharacter
    interpretation (e.g., user config like ``cd build && ctest``). All
    other callers must use ``run_cmd`` with a ``list[str]``.
    """
    if not isinstance(cmd, str):
        raise TypeError(
            f"run_shell_cmd requires a str (got {type(cmd).__name__}); "
            f"use run_cmd for list-form commands"
        )
    log.debug("Running (shell=True): %s", cmd)
    start = time.monotonic()
    try:
        result = subprocess.run(
            cmd,
            cwd=cwd,
            shell=True,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        elapsed = time.monotonic() - start
        log.debug("  -> %d in %.1fs", result.returncode, elapsed)
        return result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired:
        elapsed = time.monotonic() - start
        log.warning("Command timed out after %.1fs: %s", elapsed, cmd)
        return -1, "", f"TIMEOUT after {timeout}s"
    except FileNotFoundError as e:
        log.warning("Command not found: %s", e)
        return -1, "", str(e)


def enumerate_files(
    root: Path,
    source_dirs: list[str],
    extensions: list[str],
    exclude_dirs: list[str],
) -> list[Path]:
    """Enumerate source files matching extensions, excluding directories."""
    files: list[Path] = []
    exclude_set = {(root / d).resolve() for d in exclude_dirs}

    for src_dir in source_dirs:
        base = root / src_dir
        if not base.exists():
            log.warning("Source directory not found: %s", base)
            continue
        for ext in extensions:
            for path in base.rglob(f"*{ext}"):
                if any(path.resolve().is_relative_to(ex) for ex in exclude_set):
                    continue
                files.append(path)

    return sorted(set(files))


def relative_path(path: Path, root: Path) -> str:
    """Return a path relative to root, or the absolute path if not relative."""
    try:
        return str(path.resolve().relative_to(root.resolve()))
    except ValueError:
        return str(path)


def estimate_tokens(text: str) -> int:
    """Approximate token count (1 token ~ 4 characters for English/code)."""
    return len(text) // 4


def count_lines(path: Path) -> int:
    """Count non-empty lines in a file."""
    try:
        with open(path, "r", errors="replace") as f:
            return sum(1 for line in f if line.strip())
    except (OSError, UnicodeDecodeError):
        return 0


def format_duration(seconds: float) -> str:
    """Format seconds into a human-readable string."""
    if seconds < 60:
        return f"{seconds:.1f}s"
    minutes = int(seconds // 60)
    secs = seconds % 60
    return f"{minutes}m {secs:.0f}s"
