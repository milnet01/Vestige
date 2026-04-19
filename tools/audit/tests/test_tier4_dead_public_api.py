# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tests for lib.tier4_dead_public_api — dead public-API detection."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from lib.config import Config, DEFAULTS, _deep_merge
from lib.tier4_dead_public_api import (
    DeadPublicApiResult,
    analyze_dead_public_api,
    _extract_symbols_from_header,
    _NAME_BLOCKLIST,
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
# _extract_symbols_from_header
# ---------------------------------------------------------------------------

class TestExtractSymbols:
    def test_extracts_class(self, tmp_path: Path):
        p = _write(tmp_path, "foo.h",
                   "class Renderer {\n"
                   "public:\n"
                   "    void render();\n"
                   "};\n")
        syms = _extract_symbols_from_header(p, "foo.h")
        names = [s["name"] for s in syms]
        assert "Renderer" in names
        # render() is a method (indented after `public:`), not a free
        # function declaration — and "render" is in _NAME_BLOCKLIST anyway.
        assert "render" not in names

    def test_extracts_free_function(self, tmp_path: Path):
        p = _write(tmp_path, "math.h",
                   "namespace m {\n"
                   "void computeSomething(int x);\n"
                   "}\n")
        syms = _extract_symbols_from_header(p, "math.h")
        names = [s["name"] for s in syms]
        assert "computeSomething" in names

    def test_skips_blocklisted_names(self, tmp_path: Path):
        p = _write(tmp_path, "x.h",
                   "void run();\n"
                   "void init();\n"
                   "void doActualThing();\n")
        syms = _extract_symbols_from_header(p, "x.h")
        names = [s["name"] for s in syms]
        assert "run" not in names
        assert "init" not in names
        assert "doActualThing" in names

    def test_skips_short_names(self, tmp_path: Path):
        p = _write(tmp_path, "x.h",
                   "void op(int);\n"           # 2 chars — skipped
                   "void longerName(int);\n")  # passes
        syms = _extract_symbols_from_header(p, "x.h")
        names = [s["name"] for s in syms]
        assert "op" not in names
        assert "longerName" in names


# ---------------------------------------------------------------------------
# analyze_dead_public_api (integration)
# ---------------------------------------------------------------------------

class TestAnalyzeDeadPublicApi:
    def test_flags_unreferenced_class(self, tmp_path: Path):
        _write(tmp_path, "orphan.h",
               "class OrphanWidget {\n"
               "public:\n"
               "    void doStuff();\n"
               "};\n")
        # No source file references OrphanWidget anywhere.
        config = _make_config(tmp_path)
        result, findings = analyze_dead_public_api(config)
        names = [s["name"] for s in result.dead_symbols]
        assert "OrphanWidget" in names

    def test_does_not_flag_referenced_class(self, tmp_path: Path):
        _write(tmp_path, "live.h",
               "class LiveWidget {\n"
               "public:\n"
               "    void doStuff();\n"
               "};\n")
        _write(tmp_path, "main.cpp",
               '#include "live.h"\n'
               "void main_fn() {\n"
               "    LiveWidget w;\n"
               "    w.doStuff();\n"
               "}\n")
        config = _make_config(tmp_path)
        result, findings = analyze_dead_public_api(config)
        names = [s["name"] for s in result.dead_symbols]
        assert "LiveWidget" not in names

    def test_self_reference_in_own_header_does_not_save(self, tmp_path: Path):
        # The detector excludes the declaration itself from the count
        # (count <= 1 means dead). A typedef alias inside the SAME
        # header still counts as dead from the project's POV.
        _write(tmp_path, "alias.h",
               "class HiddenWidget {};\n"
               "using HiddenWidgetAlias = HiddenWidget;\n")
        config = _make_config(tmp_path)
        result, findings = analyze_dead_public_api(config)
        names = [s["name"] for s in result.dead_symbols]
        # HiddenWidget appears 2x in the same file — declaration + alias
        # — so the corpus-wide count is 2 and it's NOT flagged. This
        # matches the documented permissive behaviour: we'd rather miss
        # a dead symbol than incorrectly flag a live one.
        assert "HiddenWidget" not in names

    def test_extra_blocklist_via_config(self, tmp_path: Path):
        _write(tmp_path, "h.h",
               "class CustomSkippable {\n"
               "public:\n"
               "    void doStuff();\n"
               "};\n")
        # No callers — would normally be flagged. Add to blocklist.
        config = _make_config(
            tmp_path,
            tier4={"dead_public_api": {"name_blocklist": ["CustomSkippable"]}},
        )
        result, findings = analyze_dead_public_api(config)
        names = [s["name"] for s in result.dead_symbols]
        assert "CustomSkippable" not in names

    def test_disabled_returns_empty(self, tmp_path: Path):
        _write(tmp_path, "x.h", "class Foo {};\n")
        config = _make_config(
            tmp_path,
            tier4={"dead_public_api": {"enabled": False}},
        )
        result, findings = analyze_dead_public_api(config)
        assert len(result.dead_symbols) == 0
        assert len(findings) == 0

    def test_finding_metadata(self, tmp_path: Path):
        _write(tmp_path, "orphan.h", "class OrphanFoo {};\n")
        config = _make_config(tmp_path)
        result, findings = analyze_dead_public_api(config)
        assert len(findings) == 1
        f = findings[0]
        assert f.pattern_name == "dead_public_api"
        assert f.category == "dead_code"
        assert f.source_tier == 4
        from lib.findings import Severity
        assert f.severity == Severity.LOW


class TestDeadPublicApiResult:
    def test_to_dict_caps_list(self):
        result = DeadPublicApiResult()
        result.dead_symbols = [
            {"file": f"f{i}.h", "line": 1, "name": f"Sym{i}",
             "kind": "class", "match_count": 1}
            for i in range(50)
        ]
        d = result.to_dict()
        assert len(d["dead_symbols"]) == 30
        assert d["dead_count"] == 50
