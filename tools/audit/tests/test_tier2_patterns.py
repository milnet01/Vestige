# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tests for lib.tier2_patterns — _scan_file, _filter_files, run()."""

from __future__ import annotations

import re
from pathlib import Path

import pytest

from lib.config import Config, DEFAULTS, _deep_merge
from lib.tier2_patterns import _filter_files, _scan_file, run


# ---------------------------------------------------------------------------
# _scan_file — basic regex matching
# ---------------------------------------------------------------------------


class TestScanFileBasic:
    """_scan_file should find regex matches and return (line_number, line_text)."""

    def test_finds_match_with_line_numbers(self, sample_cpp_file: Path):
        regex = re.compile(r"\bnew\b")
        hits = _scan_file(sample_cpp_file, regex, exclude_re=None, skip_comments=False)
        # Should find 'new' on line 5 (int* p = new int(7);)
        # Also in block comment on line 7 and comment on line 11
        line_nums = [h[0] for h in hits]
        assert 5 in line_nums

    def test_returns_full_line_text(self, sample_cpp_file: Path):
        regex = re.compile(r"\bnew\b")
        hits = _scan_file(sample_cpp_file, regex, exclude_re=None, skip_comments=False)
        # Find the hit for line 5
        for lnum, text in hits:
            if lnum == 5:
                assert "new int(7)" in text
                break
        else:
            pytest.fail("Line 5 not found in hits")

    def test_no_match_returns_empty(self, sample_cpp_file: Path):
        regex = re.compile(r"ZZZZNOTFOUND")
        hits = _scan_file(sample_cpp_file, regex, exclude_re=None, skip_comments=False)
        assert hits == []

    def test_nonexistent_file_returns_empty(self, tmp_path: Path):
        regex = re.compile(r"anything")
        hits = _scan_file(tmp_path / "nope.cpp", regex, exclude_re=None)
        assert hits == []


# ---------------------------------------------------------------------------
# _scan_file — skip_comments
# ---------------------------------------------------------------------------


class TestScanFileSkipComments:
    """skip_comments=True should skip // line comments and /* */ block comments."""

    def test_skips_single_line_comment(self, sample_cpp_file: Path):
        regex = re.compile(r"\bnew\b")
        hits = _scan_file(sample_cpp_file, regex, exclude_re=None, skip_comments=True)
        line_nums = [h[0] for h in hits]
        # Line 11: "// This line has new but is a comment" — should be skipped
        assert 11 not in line_nums

    def test_skips_block_comment(self, sample_cpp_file: Path):
        regex = re.compile(r"\bnew\b")
        hits = _scan_file(sample_cpp_file, regex, exclude_re=None, skip_comments=True)
        line_nums = [h[0] for h in hits]
        # Line 7: inside /* */ block comment — should be skipped
        assert 7 not in line_nums

    def test_code_line_still_matched(self, sample_cpp_file: Path):
        regex = re.compile(r"\bnew\b")
        hits = _scan_file(sample_cpp_file, regex, exclude_re=None, skip_comments=True)
        line_nums = [h[0] for h in hits]
        # Line 5: actual code with 'new' — should still be found
        assert 5 in line_nums

    def test_todo_in_comment_skipped(self, sample_cpp_file: Path):
        regex = re.compile(r"TODO")
        hits = _scan_file(sample_cpp_file, regex, exclude_re=None, skip_comments=True)
        # Line 3 is "// TODO: remove raw allocation" — should be skipped
        assert len(hits) == 0

    def test_block_comment_start_line_skipped(self, tmp_path: Path):
        """The line with /* itself should be skipped."""
        cpp = tmp_path / "block.cpp"
        cpp.write_text(
            'int x = 1;\n'
            '/* new forbidden\n'
            '   new also here\n'
            '*/\n'
            'int y = new int;\n'
        )
        regex = re.compile(r"\bnew\b")
        hits = _scan_file(cpp, regex, exclude_re=None, skip_comments=True)
        line_nums = [h[0] for h in hits]
        assert 2 not in line_nums
        assert 3 not in line_nums
        assert 5 in line_nums


# ---------------------------------------------------------------------------
# _scan_file — exclude_re
# ---------------------------------------------------------------------------


class TestScanFileExclude:
    """exclude_re should filter out matches whose line also matches the exclude regex."""

    def test_exclude_filters_matching_lines(self, tmp_path: Path):
        cpp = tmp_path / "excl.cpp"
        cpp.write_text(
            'auto p = new int(1);       // NOLINT\n'
            'auto q = new int(2);\n'
        )
        regex = re.compile(r"\bnew\b")
        exclude_re = re.compile(r"NOLINT")
        hits = _scan_file(cpp, regex, exclude_re=exclude_re, skip_comments=False)
        assert len(hits) == 1
        assert hits[0][0] == 2

    def test_exclude_none_keeps_all(self, tmp_path: Path):
        cpp = tmp_path / "noexcl.cpp"
        cpp.write_text(
            'auto p = new int(1);\n'
            'auto q = new int(2);\n'
        )
        regex = re.compile(r"\bnew\b")
        hits = _scan_file(cpp, regex, exclude_re=None, skip_comments=False)
        assert len(hits) == 2


# ---------------------------------------------------------------------------
# _filter_files
# ---------------------------------------------------------------------------


class TestFilterFiles:
    """_filter_files should match files by fnmatch glob patterns."""

    def test_matches_cpp_glob(self, tmp_path: Path):
        files = [
            tmp_path / "main.cpp",
            tmp_path / "utils.h",
            tmp_path / "shader.glsl",
        ]
        result = _filter_files(files, ["*.cpp"])
        assert len(result) == 1
        assert result[0].name == "main.cpp"

    def test_matches_multiple_globs(self, tmp_path: Path):
        files = [
            tmp_path / "main.cpp",
            tmp_path / "utils.h",
            tmp_path / "shader.glsl",
        ]
        result = _filter_files(files, ["*.cpp", "*.h"])
        names = {f.name for f in result}
        assert names == {"main.cpp", "utils.h"}

    def test_wildcard_matches_all(self, tmp_path: Path):
        files = [tmp_path / "a.cpp", tmp_path / "b.h"]
        result = _filter_files(files, ["*"])
        assert len(result) == 2

    def test_no_match_returns_empty(self, tmp_path: Path):
        files = [tmp_path / "main.cpp"]
        result = _filter_files(files, ["*.py"])
        assert result == []

    def test_empty_file_list(self):
        result = _filter_files([], ["*.cpp"])
        assert result == []


# ---------------------------------------------------------------------------
# run() — integration with Config
# ---------------------------------------------------------------------------


class TestTier2Run:
    """run() should scan files according to config patterns."""

    def test_finds_raw_new(self, sample_config):
        findings = run(sample_config)
        new_findings = [f for f in findings if f.pattern_name == "raw_new"]
        assert len(new_findings) >= 1
        assert new_findings[0].category == "memory"
        assert new_findings[0].severity.name == "HIGH"

    def test_finds_todo(self, sample_config):
        findings = run(sample_config)
        todo_findings = [f for f in findings if f.pattern_name == "todo"]
        assert len(todo_findings) >= 1
        assert todo_findings[0].category == "maintenance"

    def test_empty_patterns_returns_empty(self, tmp_path: Path):
        raw = _deep_merge(DEFAULTS, {
            "project": {
                "name": "Empty",
                "root": str(tmp_path),
                "source_dirs": ["src/"],
                "exclude_dirs": [],
            },
            "patterns": {},
        })
        src = tmp_path / "src"
        src.mkdir()
        (src / "a.cpp").write_text("int x = 1;\n")
        cfg = Config(raw=raw, root=tmp_path)
        findings = run(cfg)
        assert findings == []

    def test_respects_max_findings_per_category(self, tmp_path: Path):
        """run() should cap findings per category at the configured maximum."""
        src = tmp_path / "src"
        src.mkdir()
        # Write a file with many TODO lines
        lines = [f"// TODO item {i}\n" for i in range(200)]
        (src / "many_todos.cpp").write_text("".join(lines))

        max_cap = 5
        raw = _deep_merge(DEFAULTS, {
            "project": {
                "name": "CapTest",
                "root": str(tmp_path),
                "source_dirs": ["src/"],
                "exclude_dirs": [],
            },
            "patterns": {
                "maintenance": [
                    {
                        "name": "todo",
                        "pattern": r"TODO",
                        "file_glob": "*.cpp",
                        "severity": "info",
                        "description": "TODO found",
                    },
                ],
            },
            "report": {
                "max_findings_per_category": max_cap,
                "output_path": "docs/REPORT.md",
                "include_json_blocks": True,
                "include_token_estimate": True,
            },
        })
        cfg = Config(raw=raw, root=tmp_path)
        findings = run(cfg)
        assert len(findings) == max_cap
