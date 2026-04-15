# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tests for lib.tier4_duplication — code clone detection."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import pytest

from lib.config import Config, DEFAULTS, _deep_merge
from lib.tier4_duplication import (
    DuplicationResult,
    analyze_duplication,
    _rabin_karp_hashes,
    _strip_comments,
    _tokenize_file,
    _find_clones,
    _merge_overlapping,
    ClonePair,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_config(tmp_path: Path, **overrides) -> Config:
    """Build a minimal Config pointing at tmp_path."""
    raw = _deep_merge(DEFAULTS, {
        "project": {
            "name": "test",
            "root": str(tmp_path),
            "source_dirs": ["."],
            "source_extensions": [".cpp", ".py"],
            "exclude_dirs": [],
        },
        "tier4": _deep_merge(DEFAULTS["tier4"], overrides.get("tier4", {})),
    })
    return Config(raw=raw, root=tmp_path)


def _write_file(tmp_path: Path, name: str, content: str) -> Path:
    p = tmp_path / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(content)
    return p


# ---------------------------------------------------------------------------
# _strip_comments
# ---------------------------------------------------------------------------

class TestStripComments:
    def test_strips_cpp_line_comments(self):
        result = _strip_comments("int x = 1; // a comment\nint y = 2;", ".cpp")
        assert "// a comment" not in result
        assert "int x = 1;" in result
        assert "int y = 2;" in result

    def test_strips_cpp_block_comments(self):
        result = _strip_comments("int x = 1; /* block\ncomment */ int y = 2;", ".cpp")
        assert "block" not in result
        assert "comment" not in result

    def test_strips_python_comments(self):
        result = _strip_comments("x = 1  # comment\ny = 2", ".py")
        assert "# comment" not in result
        assert "x = 1" in result

    def test_preserves_strings(self):
        result = _strip_comments('x = "// not a comment"', ".cpp")
        assert "// not a comment" in result

    def test_preserves_string_with_hash(self):
        result = _strip_comments('x = "# not a comment"', ".py")
        assert "# not a comment" in result


# ---------------------------------------------------------------------------
# _tokenize_file
# ---------------------------------------------------------------------------

class TestTokenizeFile:
    def test_normalizes_whitespace(self, tmp_path: Path):
        _write_file(tmp_path, "a.cpp", "int   x  =   1;\nint y = 2;")
        result = _tokenize_file(tmp_path / "a.cpp")
        assert result is not None
        assert result[0][1] == "int x = 1;"
        assert result[1][1] == "int y = 2;"

    def test_skips_empty_lines(self, tmp_path: Path):
        _write_file(tmp_path, "a.cpp", "int x;\n\n\nint y;")
        result = _tokenize_file(tmp_path / "a.cpp")
        assert result is not None
        assert len(result) == 2

    def test_preserves_line_numbers(self, tmp_path: Path):
        _write_file(tmp_path, "a.cpp", "line1\n\nline3\n\nline5")
        result = _tokenize_file(tmp_path / "a.cpp")
        assert result is not None
        assert result[0][0] == 1
        assert result[1][0] == 3
        assert result[2][0] == 5

    def test_normalizes_identifiers(self, tmp_path: Path):
        _write_file(tmp_path, "a.cpp", "int foo = bar + baz;")
        result = _tokenize_file(tmp_path / "a.cpp", normalize_ids=True)
        assert result is not None
        assert result[0][1] == "$ID $ID = $ID + $ID;"

    def test_returns_none_for_binary(self, tmp_path: Path):
        p = tmp_path / "a.bin"
        p.write_bytes(b"\x00\x01\x02binary data")
        assert _tokenize_file(p) is None

    def test_returns_none_for_missing_file(self, tmp_path: Path):
        assert _tokenize_file(tmp_path / "nonexistent.cpp") is None


# ---------------------------------------------------------------------------
# _rabin_karp_hashes
# ---------------------------------------------------------------------------

class TestRabinKarpHashes:
    def test_identical_windows_same_hash(self):
        lines = ["a", "b", "c", "d", "a", "b", "c"]
        result = _rabin_karp_hashes(lines, 3)
        # First window (a,b,c) at index 0, last window (a,b,c) at index 4
        # They should share the same hash
        found_shared = False
        for starts in result.values():
            if 0 in starts and 4 in starts:
                found_shared = True
        assert found_shared

    def test_window_larger_than_input(self):
        result = _rabin_karp_hashes(["a", "b"], 5)
        assert result == {}

    def test_single_window(self):
        result = _rabin_karp_hashes(["a", "b", "c"], 3)
        assert len(result) == 1
        starts = list(result.values())[0]
        assert starts == [0]


# ---------------------------------------------------------------------------
# analyze_duplication (integration)
# ---------------------------------------------------------------------------

class TestAnalyzeDuplication:
    def test_exact_clone_detected(self, tmp_path: Path):
        # Use identical content (not wrapped in different function names)
        block = "\n".join(f"statement_{i}();" for i in range(8))
        _write_file(tmp_path, "a.cpp", f"// file a\n{block}\n// end a")
        _write_file(tmp_path, "b.cpp", f"// file b\n{block}\n// end b")

        config = _make_config(tmp_path)
        result, findings = analyze_duplication(config)

        assert len(result.clone_pairs) > 0
        assert result.clone_pairs[0].clone_type == 1
        assert result.duplicated_lines > 0

    def test_type2_clone_detected(self, tmp_path: Path):
        _write_file(tmp_path, "a.cpp",
                     "void foo() {\n"
                     "    int alpha = getX();\n"
                     "    int beta = getY();\n"
                     "    int gamma = alpha + beta;\n"
                     "    process(gamma);\n"
                     "    store(gamma);\n"
                     "}\n")
        _write_file(tmp_path, "b.cpp",
                     "void bar() {\n"
                     "    int xx = getX();\n"
                     "    int yy = getY();\n"
                     "    int zz = xx + yy;\n"
                     "    process(zz);\n"
                     "    store(zz);\n"
                     "}\n")

        config = _make_config(tmp_path, tier4={"duplication": {"normalize_ids": True, "min_lines": 4}})
        result, findings = analyze_duplication(config)

        # Should find Type-2 clone after ID normalization
        type2 = [c for c in result.clone_pairs if c.clone_type == 2]
        assert len(type2) > 0

    def test_below_threshold_not_detected(self, tmp_path: Path):
        _write_file(tmp_path, "a.cpp", "int x = 1;\nint y = 2;")
        _write_file(tmp_path, "b.cpp", "int x = 1;\nint y = 2;")

        config = _make_config(tmp_path, tier4={"duplication": {"min_lines": 5}})
        result, findings = analyze_duplication(config)

        assert len(result.clone_pairs) == 0

    def test_disabled_returns_empty(self, tmp_path: Path):
        _write_file(tmp_path, "a.cpp", "code\n" * 20)
        _write_file(tmp_path, "b.cpp", "code\n" * 20)

        config = _make_config(tmp_path, tier4={"duplication": {"enabled": False}})
        result, findings = analyze_duplication(config)

        assert len(result.clone_pairs) == 0
        assert len(findings) == 0

    def test_findings_have_correct_category(self, tmp_path: Path):
        block = "\n".join(f"    line_{i}();" for i in range(10))
        _write_file(tmp_path, "a.cpp", f"void f() {{\n{block}\n}}")
        _write_file(tmp_path, "b.cpp", f"void g() {{\n{block}\n}}")

        config = _make_config(tmp_path)
        _, findings = analyze_duplication(config)

        for f in findings:
            assert f.category == "duplication"
            assert f.source_tier == 4
            assert f.pattern_name == "code_clone"


class TestDuplicationResult:
    def test_to_dict_caps_at_30(self):
        result = DuplicationResult()
        result.clone_pairs = [
            ClonePair("a", "b", 1, 10, 1, 10, 10, 1)
            for _ in range(50)
        ]
        d = result.to_dict()
        assert len(d["clone_pairs"]) == 30

    def test_to_dict_includes_all_fields(self):
        result = DuplicationResult(
            duplicated_lines=100,
            total_lines_scanned=1000,
            duplication_pct=10.0,
            files_scanned=5,
        )
        d = result.to_dict()
        assert d["duplicated_lines"] == 100
        assert d["total_lines_scanned"] == 1000
        assert d["duplication_pct"] == 10.0
        assert d["files_scanned"] == 5


class TestMergeOverlapping:
    def test_merges_adjacent_pairs(self):
        pairs = [
            ClonePair("a.cpp", "b.cpp", 1, 5, 1, 5, 5, 1),
            ClonePair("a.cpp", "b.cpp", 4, 8, 4, 8, 5, 1),
        ]
        merged = _merge_overlapping(pairs)
        assert len(merged) == 1
        assert merged[0].start_a == 1
        assert merged[0].end_a == 8

    def test_keeps_non_overlapping_separate(self):
        pairs = [
            ClonePair("a.cpp", "b.cpp", 1, 5, 1, 5, 5, 1),
            ClonePair("a.cpp", "b.cpp", 20, 25, 20, 25, 5, 1),
        ]
        merged = _merge_overlapping(pairs)
        assert len(merged) == 2
