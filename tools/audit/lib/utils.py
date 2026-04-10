"""Shared utilities: subprocess execution, file enumeration, token estimation."""

from __future__ import annotations

import logging
import subprocess
import time
from pathlib import Path

log = logging.getLogger("audit")


def run_cmd(
    cmd: str,
    cwd: str | Path | None = None,
    timeout: int = 300,
    shell: bool = True,
) -> tuple[int, str, str]:
    """Run a shell command and return (returncode, stdout, stderr)."""
    log.debug("Running: %s", cmd)
    start = time.monotonic()
    try:
        result = subprocess.run(
            cmd,
            cwd=cwd,
            shell=shell,
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
