"""Tests for lib.tier4_includes — include ordering and circular detection."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import pytest

from lib.config import Config, DEFAULTS, _deep_merge
from lib.tier4_includes import (
    IncludeAnalysis,
    analyze_includes,
    _classify_include,
    _check_include_order,
    _detect_circular,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_config(tmp_path: Path, **overrides) -> Config:
    """Build a minimal Config pointing at tmp_path."""
    tier4 = _deep_merge(DEFAULTS["tier4"], overrides.get("tier4", {}))
    raw = _deep_merge(DEFAULTS, {
        "project": {
            "name": "test",
            "root": str(tmp_path),
            "source_dirs": ["."],
            "source_extensions": [".cpp", ".h", ".hpp"],
            "exclude_dirs": [],
        },
        "tier4": tier4,
    })
    return Config(raw=raw, root=tmp_path)


def _write_file(tmp_path: Path, name: str, content: str) -> Path:
    p = tmp_path / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(content)
    return p


# ---------------------------------------------------------------------------
# _classify_include
# ---------------------------------------------------------------------------

class TestClassifyInclude:
    def test_corresponding_header(self):
        # foo.cpp includes "foo.h" -> category 0
        assert _classify_include('"', "foo.h", "foo") == 0

    def test_c_system_header(self):
        # <stdio.h> -> category 1
        assert _classify_include("<", "stdio.h", "main") == 1

    def test_cpp_stdlib_header(self):
        # <vector> -> category 2
        assert _classify_include("<", "vector", "main") == 2

    def test_third_party_header(self):
        # <imgui/imgui.h> -> category 3 (angle bracket with path separator)
        assert _classify_include("<", "imgui/imgui.h", "main") == 3

    def test_project_local_header(self):
        # "renderer/shader.h" -> category 4
        assert _classify_include('"', "renderer/shader.h", "main") == 4

    def test_corresponding_header_with_path(self):
        # foo.cpp includes "something/foo.h" -> category 0
        assert _classify_include('"', "something/foo.h", "foo") == 0

    def test_non_corresponding_quoted_header(self):
        # bar.cpp includes "foo.h" -> category 4 (not corresponding)
        assert _classify_include('"', "foo.h", "bar") == 4

    def test_cpp_stdlib_no_extension(self):
        # <string> -> category 2
        assert _classify_include("<", "string", "main") == 2

    def test_c_system_with_extension(self):
        # <math.h> -> category 1
        assert _classify_include("<", "math.h", "main") == 1


# ---------------------------------------------------------------------------
# _check_include_order
# ---------------------------------------------------------------------------

class TestCheckIncludeOrder:
    def test_correct_order_no_violations(self, tmp_path: Path):
        _write_file(tmp_path, "foo.h", "#pragma once\n")
        _write_file(tmp_path, "foo.cpp",
                     '#include "foo.h"\n'      # 0 - corresponding
                     '#include <stdio.h>\n'    # 1 - C system
                     '#include <vector>\n'     # 2 - C++ stdlib
                     '#include <GL/gl.h>\n'    # 3 - third-party
                     '#include "bar.h"\n'      # 4 - project local
                     )
        config = _make_config(tmp_path)
        files = [tmp_path / "foo.cpp"]
        violations = _check_include_order(files, config)
        assert len(violations) == 0

    def test_project_before_system_violation(self, tmp_path: Path):
        _write_file(tmp_path, "main.cpp",
                     '#include "bar.h"\n'      # 4 - project local
                     '#include <vector>\n'     # 2 - C++ stdlib (violation: 2 < 4)
                     )
        config = _make_config(tmp_path)
        files = [tmp_path / "main.cpp"]
        violations = _check_include_order(files, config)
        assert len(violations) == 1
        assert violations[0]["file"] is not None
        assert violations[0]["include"] == "vector"

    def test_one_violation_per_file_max(self, tmp_path: Path):
        _write_file(tmp_path, "main.cpp",
                     '#include "bar.h"\n'      # 4
                     '#include <stdio.h>\n'    # 1 (violation)
                     '#include "baz.h"\n'      # 4
                     '#include <vector>\n'     # 2 (would also be violation)
                     )
        config = _make_config(tmp_path)
        files = [tmp_path / "main.cpp"]
        violations = _check_include_order(files, config)
        assert len(violations) == 1

    def test_empty_file_no_violations(self, tmp_path: Path):
        _write_file(tmp_path, "empty.cpp", "// no includes\n")
        config = _make_config(tmp_path)
        files = [tmp_path / "empty.cpp"]
        violations = _check_include_order(files, config)
        assert len(violations) == 0


# ---------------------------------------------------------------------------
# _detect_circular — Tarjan's SCC
# ---------------------------------------------------------------------------

class TestDetectCircular:
    def test_triangle_cycle_detected(self):
        # A -> B -> C -> A
        graph = {
            "a.h": ["b.h"],
            "b.h": ["c.h"],
            "c.h": ["a.h"],
        }
        circular = _detect_circular(graph)
        assert len(circular) > 0
        # All three should appear in the pairs
        nodes_in_cycles = set()
        for a, b in circular:
            nodes_in_cycles.add(a)
            nodes_in_cycles.add(b)
        assert "a.h" in nodes_in_cycles
        assert "b.h" in nodes_in_cycles
        assert "c.h" in nodes_in_cycles

    def test_no_false_positives_on_acyclic_graph(self):
        # A -> B -> C (no cycle)
        graph = {
            "a.h": ["b.h"],
            "b.h": ["c.h"],
            "c.h": [],
        }
        circular = _detect_circular(graph)
        assert len(circular) == 0

    def test_direct_mutual_inclusion(self):
        # A <-> B
        graph = {
            "a.h": ["b.h"],
            "b.h": ["a.h"],
        }
        circular = _detect_circular(graph)
        assert len(circular) == 1
        pair = circular[0]
        assert set(pair) == {"a.h", "b.h"}

    def test_empty_graph(self):
        circular = _detect_circular({})
        assert len(circular) == 0

    def test_single_node_no_cycle(self):
        graph = {"a.h": []}
        circular = _detect_circular(graph)
        assert len(circular) == 0
