# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tests for lib.tier4_per_frame_alloc — per-frame heap allocations."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from lib.config import Config, DEFAULTS, _deep_merge
from lib.tier4_per_frame_alloc import (
    PerFrameAllocResult,
    analyze_per_frame_alloc,
    _find_function_bodies,
    _scan_body,
    _scan_file,
    _function_name_matches,
    _DEFAULT_PER_FRAME_PATTERNS,
)


def _make_config(tmp_path: Path, **overrides) -> Config:
    raw = _deep_merge(DEFAULTS, {
        "project": {
            "name": "test",
            "root": str(tmp_path),
            "source_dirs": ["."],
            "source_extensions": [".cpp", ".h"],
            "exclude_dirs": [],
        },
        "tier4": _deep_merge(DEFAULTS["tier4"], overrides.get("tier4", {})),
    })
    return Config(raw=raw, root=tmp_path)


def _write(tmp_path: Path, rel: str, content: str) -> Path:
    p = tmp_path / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(content)
    return p


# ---------------------------------------------------------------------------
# _function_name_matches
# ---------------------------------------------------------------------------

class TestFunctionNameMatches:
    def test_render_matches(self):
        assert _function_name_matches("renderScene", _DEFAULT_PER_FRAME_PATTERNS)

    def test_update_matches(self):
        assert _function_name_matches("updatePhysics", _DEFAULT_PER_FRAME_PATTERNS)

    def test_load_does_not_match(self):
        assert not _function_name_matches("loadAssets", _DEFAULT_PER_FRAME_PATTERNS)

    def test_case_insensitive(self):
        assert _function_name_matches("Tick", _DEFAULT_PER_FRAME_PATTERNS)


# ---------------------------------------------------------------------------
# _find_function_bodies
# ---------------------------------------------------------------------------

class TestFindFunctionBodies:
    def test_single_render_body(self):
        text = (
            "void Renderer::render() {\n"
            "    int x = 1;\n"
            "}\n"
        )
        bodies = _find_function_bodies(text, _DEFAULT_PER_FRAME_PATTERNS)
        assert len(bodies) == 1
        name, _, _, line = bodies[0]
        assert name == "render"
        assert line == 1

    def test_skips_unrelated_function(self):
        text = (
            "int load() { return 0; }\n"
            "void update() {\n"
            "    int y = 2;\n"
            "}\n"
        )
        bodies = _find_function_bodies(text, _DEFAULT_PER_FRAME_PATTERNS)
        assert len(bodies) == 1
        assert bodies[0][0] == "update"

    def test_handles_nested_braces(self):
        text = (
            "void render() {\n"
            "    if (true) { do_thing(); }\n"
            "    for (int i = 0; i < 10; ++i) {\n"
            "        x += i;\n"
            "    }\n"
            "}\n"
        )
        bodies = _find_function_bodies(text, _DEFAULT_PER_FRAME_PATTERNS)
        assert len(bodies) == 1
        name, start, end, _ = bodies[0]
        # Body should span the whole render() function.
        assert text[start] == "{"
        assert text[end] == "}"

    def test_handles_string_with_brace(self):
        text = (
            'void update() {\n'
            '    log("close }");\n'
            '    other();\n'
            '}\n'
        )
        bodies = _find_function_bodies(text, _DEFAULT_PER_FRAME_PATTERNS)
        assert len(bodies) == 1


# ---------------------------------------------------------------------------
# _scan_body
# ---------------------------------------------------------------------------

class TestScanBody:
    def test_flags_make_unique_in_loop(self):
        body = (
            "{\n"
            "    for (int i = 0; i < 10; ++i) {\n"
            "        auto p = std::make_unique<Foo>();\n"
            "    }\n"
            "}\n"
        )
        hits = _scan_body(body, body_start_line=1)
        assert len(hits) == 1
        line_no, kind, _, in_loop = hits[0]
        assert kind == "make_unique"
        assert in_loop is True

    def test_flags_new_outside_loop(self):
        body = (
            "{\n"
            "    auto p = new Foo();\n"
            "}\n"
        )
        hits = _scan_body(body, body_start_line=10)
        assert len(hits) == 1
        line_no, kind, _, in_loop = hits[0]
        assert kind == "new"
        assert in_loop is False
        # Body started on line 10; "auto p = new Foo()" is line 11.
        assert line_no == 11

    def test_static_const_is_ignored(self):
        body = (
            "{\n"
            "    static const auto pool = new Pool();\n"
            "}\n"
        )
        hits = _scan_body(body, body_start_line=1)
        assert len(hits) == 0

    def test_alloc_ok_marker_is_ignored(self):
        body = (
            "{\n"
            "    auto p = std::make_unique<Foo>();  // ALLOC-OK justified\n"
            "}\n"
        )
        hits = _scan_body(body, body_start_line=1)
        assert len(hits) == 0

    def test_string_concat_pattern(self):
        body = (
            "{\n"
            '    log("frame=" + std::to_string(frameId));\n'
            "}\n"
        )
        hits = _scan_body(body, body_start_line=1)
        assert len(hits) == 1
        assert hits[0][1] == "string_concat"

    def test_loop_exits_decrement_depth(self):
        body = (
            "{\n"
            "    for (int i = 0; i < 5; ++i) {\n"
            "        x += i;\n"
            "    }\n"
            "    auto p = new Foo();\n"   # OUTSIDE the loop now
            "}\n"
        )
        hits = _scan_body(body, body_start_line=1)
        assert len(hits) == 1
        assert hits[0][3] is False, "alloc after the loop closes is not in_loop"

    def test_empty_vector_default_ctor_not_flagged(self):
        body = (
            "{\n"
            "    std::vector<int> v;\n"
            "}\n"
        )
        hits = _scan_body(body, body_start_line=1)
        assert len(hits) == 0


# ---------------------------------------------------------------------------
# analyze_per_frame_alloc (integration)
# ---------------------------------------------------------------------------

class TestAnalyzePerFrameAlloc:
    def test_flags_render_with_alloc(self, tmp_path: Path):
        _write(tmp_path, "renderer.cpp",
               "void Renderer::render() {\n"
               "    for (int i = 0; i < 100; ++i) {\n"
               "        auto p = std::make_unique<Mesh>();\n"
               "    }\n"
               "}\n")
        config = _make_config(tmp_path)
        result, findings = analyze_per_frame_alloc(config)
        assert len(result.findings) == 1
        assert result.functions_scanned == 1
        # Loop-nested = MEDIUM
        from lib.findings import Severity
        assert findings[0].severity == Severity.MEDIUM

    def test_does_not_flag_load_function(self, tmp_path: Path):
        _write(tmp_path, "loader.cpp",
               "void load() {\n"
               "    auto p = new Foo();\n"   # not on per-frame path
               "}\n")
        config = _make_config(tmp_path)
        result, findings = analyze_per_frame_alloc(config)
        assert len(result.findings) == 0
        assert result.functions_scanned == 0

    def test_disabled_returns_empty(self, tmp_path: Path):
        _write(tmp_path, "r.cpp",
               "void render() {\n"
               "    auto p = new Foo();\n"
               "}\n")
        config = _make_config(
            tmp_path,
            tier4={"per_frame_alloc": {"enabled": False}},
        )
        result, findings = analyze_per_frame_alloc(config)
        assert len(result.findings) == 0
        assert len(findings) == 0

    def test_custom_function_patterns(self, tmp_path: Path):
        _write(tmp_path, "physics.cpp",
               "void simulate() {\n"
               "    auto p = new Body();\n"
               "}\n")
        config = _make_config(
            tmp_path,
            tier4={"per_frame_alloc": {"function_patterns": ["simulate"]}},
        )
        result, findings = analyze_per_frame_alloc(config)
        assert len(result.findings) == 1
        assert result.findings[0]["function"] == "simulate"

    def test_self_exempt_basename(self, tmp_path: Path):
        # File literally named tier4_per_frame_alloc.py (rule source) is
        # exempt to avoid false-positives on its own description strings.
        _write(tmp_path, "tier4_per_frame_alloc.py",
               "# render() new pattern description\n")
        # And drop one real C++ violation so we know the scanner ran.
        _write(tmp_path, "real.cpp",
               "void render() {\n"
               "    auto p = new Foo();\n"
               "}\n")
        config = _make_config(tmp_path)
        config.raw["project"]["source_extensions"] = [".cpp", ".h", ".py"]
        result, findings = analyze_per_frame_alloc(config)
        assert all("tier4_per_frame_alloc" not in f["file"]
                   for f in result.findings)
        assert any("real.cpp" in f["file"] for f in result.findings)


class TestPerFrameAllocResult:
    def test_to_dict_caps_list(self):
        result = PerFrameAllocResult()
        result.findings = [
            {"file": f"f{i}.cpp", "line": i, "function": "render",
             "alloc_kind": "new", "in_loop": False, "text": ""}
            for i in range(50)
        ]
        d = result.to_dict()
        assert len(d["findings"]) == 30
        assert d["count"] == 50
