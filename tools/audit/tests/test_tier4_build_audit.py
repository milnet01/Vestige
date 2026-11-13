# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tests for lib.tier4_build_audit — the CMake audit."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))


from lib.config import DEFAULTS, Config, _deep_merge
from lib.tier4_build_audit import analyze_build_system


def test_a_self_referential_variable_names_its_own_file_and_line(tmp_path):
    # The line was counted across every CMakeLists.txt joined together but
    # reported against the top-level one.
    (tmp_path / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.20)\nproject(t)\nadd_subdirectory(sub)\n")
    (tmp_path / "sub").mkdir()
    (tmp_path / "sub" / "CMakeLists.txt").write_text("# sub\nset(FOO ${FOO})\n")
    raw = _deep_merge(DEFAULTS, {
        "project": {"name": "test", "root": str(tmp_path), "exclude_dirs": []}})
    _, findings = analyze_build_system(Config(raw=raw, root=tmp_path))
    self_refs = [(f.file, f.line) for f in findings if f.pattern_name == "cmake_self_ref"]
    assert self_refs == [("sub/CMakeLists.txt", 2)]
