# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tests for lib.tier1_build — the build step's findings."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))


from lib import tier1_build
from lib.config import DEFAULTS, Config, _deep_merge
from lib.findings import Severity


def _build(tmp_path: Path, monkeypatch, rc: int, output: str):
    raw = _deep_merge(DEFAULTS, {
        "project": {"name": "test", "root": str(tmp_path)},
        "build": {"build_cmd": "make"},
    })
    monkeypatch.setattr(tier1_build, "run_shell_cmd",
                        lambda *_args, **_kwargs: (rc, output, ""))
    return tier1_build.run_build(Config(raw=raw, root=tmp_path))


def test_a_failed_link_is_reported(tmp_path, monkeypatch):
    # A linker error carries no file:line, so it added to the error count but
    # made no finding, and the count then suppressed "Build failed" too: a
    # failed build produced no finding at all.
    findings, summary = _build(
        tmp_path, monkeypatch, 1,
        "/usr/bin/ld: error: undefined reference to `foo()'\n"
        "collect2: error: ld returned 1 exit status\n")
    assert summary["build_ok"] is False
    assert [f.pattern_name for f in findings] == ["build_failure"]
    assert findings[0].severity is Severity.CRITICAL


def test_a_compile_error_with_a_location_is_reported_once(tmp_path, monkeypatch):
    findings, _ = _build(tmp_path, monkeypatch, 1,
                         "engine/a.cpp:10:5: error: 'x' was not declared\n")
    assert [(f.pattern_name, f.file, f.line) for f in findings] == [
        ("compiler", "engine/a.cpp", 10)]
