# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tier 1: clang-tidy static analysis with text output parsing."""

from __future__ import annotations

import json
import logging
import os
import re
import shlex
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from .config import Config
from .findings import Finding, Severity
from .utils import run_cmd, enumerate_files, relative_path

log = logging.getLogger("audit")


# CMake emits these two flags for a precompiled header. They are a build-speed
# artefact with no bearing on what the code means, and clang cannot read the
# GCC .gch the build produced -- under the project's -Werror that abandons the
# whole translation unit (3D_E-0637). Strip them before analysing.
_PCH_HEADER_SUFFIX = "cmake_pch.hxx"
_PCH_FLAGS = frozenset({"-Winvalid-pch", "-fpch-preprocess"})


def _strip_pch_tokens(tokens: list[str]) -> list[str]:
    """Drop the PCH flags from one compile command's argument list.

    Only a ``-include`` naming a CMake PCH header is removed; a forced include
    of a real project header is a semantic flag and is kept.
    """
    out: list[str] = []
    skip_next = False
    for i, tok in enumerate(tokens):
        if skip_next:
            skip_next = False
            continue
        if tok in _PCH_FLAGS:
            continue
        if tok == "-include" and i + 1 < len(tokens) \
                and tokens[i + 1].endswith(_PCH_HEADER_SUFFIX):
            skip_next = True
            continue
        if tok.startswith("-include") and tok.endswith(_PCH_HEADER_SUFFIX):
            continue
        out.append(tok)
    return out


def _write_pch_free_compile_db(cc_dir: Path, out_dir: Path) -> Path | None:
    """Copy ``cc_dir``'s compile database to ``out_dir`` without PCH flags.

    Returns the directory holding the rewritten database, or None when the
    source database is missing or unreadable -- the caller then falls back to
    the original directory rather than analysing nothing.
    """
    src = Path(cc_dir) / "compile_commands.json"
    try:
        entries = json.loads(src.read_text())
    except (OSError, ValueError) as exc:
        log.warning("clang-tidy: cannot read %s (%s) — analysing against the "
                    "build's own database, which may carry a GCC PCH", src, exc)
        return None

    for entry in entries:
        if "arguments" in entry:
            entry["arguments"] = _strip_pch_tokens(list(entry["arguments"]))
        elif "command" in entry:
            stripped = _strip_pch_tokens(shlex.split(entry["command"]))
            entry["command"] = " ".join(shlex.quote(t) for t in stripped)

    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "compile_commands.json").write_text(json.dumps(entries))
    return out_dir


# clang-tidy reports an abandoned translation unit with lines that carry no
# ``file:line:col:`` prefix, so the diagnostic parser below never matches them.
# That is what let a run analyse nothing, report zero findings and exit clean.
_ABANDONED_FILE_RE = re.compile(r"^Error while processing (.+?)\.?$", re.MULTILINE)
_BARE_ERROR_RE = re.compile(r"^error: (.+)$", re.MULTILINE)


def _detect_analysis_failures(output: str) -> list[Finding]:
    """Turn "the analyser gave up" into a finding.

    A tool that analysed nothing must not be indistinguishable from a tool
    that found nothing. Two shapes, measured on this tree:

    * clang-tidy prints ``Error while processing <file>.`` and exits 1.
    * clazy prints the bare ``error:`` alone and exits 0, with no other
      output at all -- the silent zero this exists to catch.

    A bare ``error:`` carries no ``file:line:col:`` prefix, so it is a
    driver- or configuration-level failure rather than a code diagnostic,
    and the diagnostic parser below never matches it.
    """
    reasons = sorted(set(_BARE_ERROR_RE.findall(output)))
    files = sorted(set(_ABANDONED_FILE_RE.findall(output)))
    if not reasons and not files:
        return []

    detail = reasons[0] if reasons else "no diagnostic text"
    if files:
        shown = ", ".join(files[:3]) + (" ..." if len(files) > 3 else "")
        where = f"{len(files)} file(s): {shown}"
    else:
        where = "an unnamed file (the tool reported no location)"

    return [Finding(
        file=files[0] if files else "<compile database>",
        line=None,
        severity=Severity.HIGH,
        category="clang_tidy",
        source_tier=1,
        title=(f"clang-based analysis failed on {where} — the run analysed "
               f"less than it appears to have. First cause: {detail}"),
        pattern_name="clangtidy_analysis_failed",
    )]


def _clangtidy_workers(ct_config: dict) -> int:
    """Pick a parallelism level for clang-tidy.

    clang-tidy re-parses a full translation unit (all transitive headers), so
    each process peaks around 1.5-2 GB on this codebase. An explicit ``jobs``
    config wins; otherwise cap by both CPU count and total RAM (~2 GB/worker)
    so the step doesn't OOM small CI runners or pile onto a loaded dev box.
    The hard ceiling of 6 mirrors the build job-pool's memory budget.
    """
    configured = ct_config.get("jobs", 0)
    if configured:
        return max(1, int(configured))
    workers = os.cpu_count() or 2
    try:
        total_gb = (os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES")
                    / (1024 ** 3))
        workers = min(workers, max(1, int(total_gb // 2)))
    except (ValueError, OSError, AttributeError):
        pass
    return max(1, min(workers, 6))


def _ensure_compile_commands(config: Config, cc_path: Path | None) -> bool:
    """Ensure compile_commands.json exists, auto-generating via CMake if needed.

    Returns ``True`` if a usable compile database is present after this call.
    """
    if cc_path is not None and cc_path.exists():
        return True

    build_system = config.get("build", "system", default="cmake")
    if build_system != "cmake":
        return False

    build_dir = config.get("build", "build_dir", default="build")
    abs_build_dir = config.root / build_dir

    log.info("compile_commands.json not found — attempting CMake auto-generation "
             "in %s", abs_build_dir)

    rc, stdout, stderr = run_cmd(
        ["cmake", "-B", str(abs_build_dir),
         "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"],
        cwd=config.root,
        timeout=120,
    )

    generated = (abs_build_dir / "compile_commands.json").exists()
    if generated:
        log.info("Auto-generated compile_commands.json in %s", abs_build_dir)
    else:
        log.warning("CMake completed (rc=%d) but compile_commands.json was not created", rc)
        if stderr:
            log.debug("CMake stderr: %s", stderr[:500])

    return generated


def run(config: Config) -> list[Finding]:
    """Run clang-tidy and parse results into findings."""
    findings: list[Finding] = []
    ct_config = config.get("static_analysis", "clang_tidy", default={})

    if not ct_config.get("enabled", False):
        log.info("clang-tidy disabled — skipping")
        return findings

    if config.language not in ("cpp", "c"):
        log.info("clang-tidy only applies to C/C++ — skipping")
        return findings

    binary = ct_config.get("binary", "clang-tidy")
    checks = ct_config.get("checks", "bugprone-*,performance-*")
    compile_commands = ct_config.get("compile_commands")
    fallback_flags = ct_config.get("fallback_flags", "-std=c++17")
    max_files = ct_config.get("max_files", 50)
    timeout = ct_config.get("timeout", 600)

    # Determine source files to analyze
    source_files = _get_target_files(config, max_files)
    if not source_files:
        log.warning("No source files found for clang-tidy")
        return findings

    # Check for compile_commands.json — auto-generate if possible
    cc_path = (config.root / compile_commands) if compile_commands else None
    has_compile_db = _ensure_compile_commands(config, cc_path)

    if not has_compile_db:
        log.warning("compile_commands.json not found — clang-tidy results will have "
                     "many false positives from missing includes. Consider running: "
                     "cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON")

    # Build the base argv (shell=False per AUDIT.md §C1 / FIXPLAN C1b). Each
    # file is analysed in its own process so they can run concurrently — a
    # single clang-tidy invocation processes positional files serially, which
    # made this step the Tier 1 long pole.
    if has_compile_db:
        if cc_path is not None and cc_path.exists():
            cc_dir = cc_path.parent
        else:
            # Auto-generated — lives in the build directory
            build_dir = config.get("build", "build_dir", default="build")
            cc_dir = config.root / build_dir
        # Analyse against a PCH-free copy of the database. The build's own
        # database names a GCC precompiled header that clang cannot read, and
        # -Werror turns that into an abandoned translation unit (3D_E-0637).
        analysis_dir = _write_pch_free_compile_db(
            cc_dir, cc_dir / "audit-clang-tidy")
        base_cmd: list[str] = [binary, "-p", str(analysis_dir or cc_dir),
                               f"--checks={checks}"]
        cmd_suffix: list[str] = []
    else:
        # Trailing `--` separates clang-tidy options from compiler flags.
        import shlex as _shlex
        base_cmd = [binary, f"--checks={checks}"]
        cmd_suffix = ["--"] + _shlex.split(fallback_flags)

    workers = _clangtidy_workers(ct_config)
    log.info("Running clang-tidy on %d files (%d parallel workers)...",
             len(source_files), workers)

    def _analyze(src: str) -> tuple[str, int, str, str]:
        rc, stdout, stderr = run_cmd(
            base_cmd + [src] + cmd_suffix, cwd=config.root, timeout=timeout)
        return src, rc, stdout, stderr

    outputs: list[str] = []
    timed_out: list[str] = []
    with ThreadPoolExecutor(max_workers=workers,
                            thread_name_prefix="clang-tidy") as executor:
        for src, rc, stdout, stderr in executor.map(_analyze, source_files):
            if rc == -1 and "TIMEOUT" in stderr:
                timed_out.append(src)
                continue
            outputs.append(stdout + "\n" + stderr)

    if timed_out:
        # A per-file timeout is still a real problem worth gating on, but it no
        # longer aborts analysis of the other files.
        findings.append(Finding(
            file=timed_out[0],
            line=None,
            severity=Severity.HIGH,
            category="clang_tidy",
            source_tier=1,
            title=(f"clang-tidy timed out after {timeout}s on "
                   f"{len(timed_out)} file(s): "
                   + ", ".join(timed_out[:3])
                   + (" ..." if len(timed_out) > 3 else "")),
            pattern_name="clangtidy_timeout",
        ))

    merged = "\n".join(outputs)

    # Before the diagnostics: did clang-tidy actually analyse anything? An
    # abandoned file produces no parseable diagnostic, so without this a run
    # that analysed nothing reports zero findings and exits clean.
    findings.extend(_detect_analysis_failures(merged))

    # Parse merged output — clang-tidy writes diagnostics to stdout
    findings.extend(_parse_output(merged, config))

    log.info("clang-tidy: %d findings", len(findings))
    return findings


def _get_target_files(config: Config, max_files: int) -> list[str]:
    """Get list of .cpp files to analyze (limited by max_files)."""
    files = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs,
        extensions=[".cpp"],
        exclude_dirs=config.exclude_dirs,
    )
    # Prioritize engine/ over tests/
    engine_files = [f for f in files if "engine/" in str(f)]
    app_files = [f for f in files if "app/" in str(f)]
    test_files = [f for f in files if "tests/" in str(f)]

    ordered = engine_files + app_files + test_files
    selected = ordered[:max_files]
    return [relative_path(f, config.root) for f in selected]


def _parse_output(output: str, config: Config) -> list[Finding]:
    """Parse clang-tidy text output."""
    findings: list[Finding] = []
    exclude_dirs = config.exclude_dirs

    # Pattern: file:line:col: warning: message [check-name]
    pattern = re.compile(
        r"(.+?):(\d+):\d+:\s*(warning|error|note):\s*(.*?)(?:\s*\[([^\]]+)\])?\s*$"
    )

    seen_keys: set[str] = set()

    for line in output.splitlines():
        m = pattern.match(line)
        if not m:
            continue

        file_path = m.group(1)
        line_num = int(m.group(2))
        level = m.group(3)
        message = m.group(4).strip()
        check_name = m.group(5) or ""

        # Skip external files and notes
        if any(ex in file_path for ex in exclude_dirs):
            continue
        if level == "note":
            continue

        # Dedup by file:line:check
        key = f"{file_path}:{line_num}:{check_name}"
        if key in seen_keys:
            continue
        seen_keys.add(key)

        severity = Severity.HIGH if level == "error" else Severity.MEDIUM

        findings.append(Finding(
            file=file_path,
            line=line_num,
            severity=severity,
            category="clang_tidy",
            source_tier=1,
            title=f"[{check_name}] {message[:120]}" if check_name else message[:120],
            pattern_name=f"clang-tidy:{check_name}" if check_name else "clang-tidy",
        ))

    return findings
