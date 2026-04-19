# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tests for lib.tier4_file_read_gcount — unchecked stream.read() detection."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import pytest

from lib.config import Config, DEFAULTS, _deep_merge
from lib.tier4_file_read_gcount import (
    FileReadGcountResult,
    analyze_file_read_gcount,
    _scan_file,
    _is_excluded_line,
    _read_is_inside_string_literal,
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
# _is_excluded_line
# ---------------------------------------------------------------------------

class TestExcludedLine:
    def test_read_text_excluded(self):
        assert _is_excluded_line('text = path.read_text("utf-8");')

    def test_filesystem_path_excluded(self):
        assert _is_excluded_line("std::filesystem::path p;")

    def test_plain_stream_read_not_excluded(self):
        assert not _is_excluded_line("stream.read(buf, 1024);")


# ---------------------------------------------------------------------------
# _scan_file
# ---------------------------------------------------------------------------

class TestScanFile:
    def test_flags_unchecked_read(self, tmp_path: Path):
        p = _write(tmp_path, "load.cpp",
                   "void load(std::istream& s) {\n"
                   "    char buf[1024];\n"
                   "    s.read(buf, 1024);\n"
                   "    process(buf);\n"
                   "}\n")
        hits = _scan_file(p, "load.cpp", window_lines=20)
        assert len(hits) == 1
        assert hits[0]["line"] == 3

    def test_gcount_within_window_clears_flag(self, tmp_path: Path):
        p = _write(tmp_path, "ok.cpp",
                   "void load(std::istream& s) {\n"
                   "    char buf[1024];\n"
                   "    s.read(buf, 1024);\n"
                   "    auto n = s.gcount();\n"
                   "    process(buf, n);\n"
                   "}\n")
        hits = _scan_file(p, "ok.cpp", window_lines=20)
        assert len(hits) == 0

    def test_gcount_outside_window_still_flagged(self, tmp_path: Path):
        extra = "\n".join("    // filler" for _ in range(25))
        p = _write(tmp_path, "far.cpp",
                   f"void load(std::istream& s) {{\n"
                   f"    char buf[1024];\n"
                   f"    s.read(buf, 1024);\n"
                   f"{extra}\n"
                   f"    auto n = s.gcount();\n"
                   f"}}\n")
        hits = _scan_file(p, "far.cpp", window_lines=5)
        assert len(hits) == 1

    def test_comment_line_ignored(self, tmp_path: Path):
        p = _write(tmp_path, "comment.cpp",
                   "// stream.read(buf, N) without .gcount() check\n"
                   "int main() { return 0; }\n")
        hits = _scan_file(p, "comment.cpp", window_lines=20)
        assert len(hits) == 0

    def test_good_state_check_clears_flag(self, tmp_path: Path):
        # istream::read() sets failbit on short reads, so a post-read
        # .good() / .fail() / .eof() check catches truncation just as
        # reliably as .gcount(). The detector must accept those.
        p = _write(tmp_path, "good.cpp",
                   "void load(std::ifstream& f) {\n"
                   "    char buf[256];\n"
                   "    f.read(buf, 256);\n"
                   "    if (!f.good()) return;\n"
                   "}\n")
        hits = _scan_file(p, "good.cpp", window_lines=20)
        assert len(hits) == 0

    def test_fail_state_check_clears_flag(self, tmp_path: Path):
        p = _write(tmp_path, "fail.cpp",
                   "void load(std::ifstream& f) {\n"
                   "    char buf[256];\n"
                   "    f.read(buf, 256);\n"
                   "    if (f.fail()) return;\n"
                   "}\n")
        hits = _scan_file(p, "fail.cpp", window_lines=20)
        assert len(hits) == 0

    def test_read_inside_string_literal_ignored(self, tmp_path: Path):
        # FP corner case: ``.read(`` inside a C++ string literal — for
        # example, an embedded Python snippet passed to a subprocess —
        # must not be flagged. tests/test_async_driver.cpp exercises
        # this pattern by piping ``sys.stdin.read()`` to a helper.
        p = _write(tmp_path, "literal.cpp",
                   'const auto script = writeScript("stdin",\n'
                   '    "import sys\\n"\n'
                   '    "sys.stdout.write(sys.stdin.read())\\n");\n')
        hits = _scan_file(p, "literal.cpp", window_lines=20)
        assert len(hits) == 0


class TestReadInsideStringLiteral:
    def test_inside_quotes_true(self):
        assert _read_is_inside_string_literal(
            '"sys.stdin.read()"'
        )

    def test_real_call_false(self):
        assert not _read_is_inside_string_literal(
            "    f.read(buf, 256);"
        )

    def test_escaped_quote_handled(self):
        # Line with an escaped quote before the read token — still in
        # a literal string.
        assert _read_is_inside_string_literal(
            r'"payload\"data.read(buf)"'
        )


# ---------------------------------------------------------------------------
# analyze_file_read_gcount (integration)
# ---------------------------------------------------------------------------

class TestAnalyzeFileReadGcount:
    def test_positive_flags_real_example(self, tmp_path: Path):
        _write(tmp_path, "loader.cpp",
               "#include <fstream>\n"
               "void load(std::ifstream& f) {\n"
               "    char buf[256];\n"
               "    f.read(buf, 256);\n"
               "    memcpy(dst, buf, 256);\n"
               "}\n")
        config = _make_config(tmp_path)
        result, findings = analyze_file_read_gcount(config)
        assert len(result.findings) == 1
        assert len(findings) == 1
        assert findings[0].pattern_name == "file_read_no_gcount"
        assert findings[0].category == "bug"
        assert findings[0].source_tier == 4

    def test_negative_gcount_present(self, tmp_path: Path):
        _write(tmp_path, "safe.cpp",
               "#include <fstream>\n"
               "void load(std::ifstream& f) {\n"
               "    char buf[256];\n"
               "    f.read(buf, 256);\n"
               "    if (f.gcount() != 256) return;\n"
               "    memcpy(dst, buf, 256);\n"
               "}\n")
        config = _make_config(tmp_path)
        result, findings = analyze_file_read_gcount(config)
        assert len(result.findings) == 0
        assert len(findings) == 0

    def test_read_text_not_flagged(self, tmp_path: Path):
        # FP corner case: Python-style path.read_text() isn't a binary
        # stream read and must not be flagged.
        _write(tmp_path, "text.cpp",
               "void load(const std::filesystem::path& p) {\n"
               "    auto text = p.read_text();\n"
               "    parse(text);\n"
               "}\n")
        config = _make_config(tmp_path)
        result, findings = analyze_file_read_gcount(config)
        assert len(result.findings) == 0
        assert len(findings) == 0

    def test_auto_config_not_flagged(self, tmp_path: Path):
        # FP corner case: the audit tool's own rule-source file contains
        # description strings that match .read(. It's in _EXEMPT_BASENAMES
        # so must not be flagged even when present in the scan root.
        _write(tmp_path, "auto_config.py",
               '# A rule description: "flag stream.read(buf, N) without gcount"\n'
               'RULES = {"pattern": r".read\\s*\\("}\n')
        # Also drop a real violation elsewhere to prove the scanner ran.
        _write(tmp_path, "real.cpp",
               "void load(std::ifstream& f) {\n"
               "    char buf[256];\n"
               "    f.read(buf, 256);\n"
               "}\n")
        config = _make_config(
            tmp_path,
            # Widen extensions to include .py so the scanner *could* hit
            # auto_config.py if it weren't exempt.
        )
        # Manually widen source_extensions for this test.
        config.raw["project"]["source_extensions"] = [".cpp", ".h", ".py"]
        result, findings = analyze_file_read_gcount(config)
        # Only the real.cpp hit — auto_config.py is exempt.
        assert all("auto_config" not in f["file"] for f in result.findings)
        assert any("real.cpp" in f["file"] for f in result.findings)

    def test_disabled_returns_empty(self, tmp_path: Path):
        _write(tmp_path, "loader.cpp",
               "void load(std::ifstream& f) {\n"
               "    char buf[256];\n"
               "    f.read(buf, 256);\n"
               "}\n")
        config = _make_config(
            tmp_path,
            tier4={"file_read_gcount": {"enabled": False}},
        )
        result, findings = analyze_file_read_gcount(config)
        assert len(result.findings) == 0
        assert len(findings) == 0


class TestFileReadGcountResult:
    def test_to_dict_caps_list(self):
        result = FileReadGcountResult()
        result.findings = [
            {"file": f"f{i}.cpp", "line": i, "text": "x.read(b, 10);"}
            for i in range(100)
        ]
        d = result.to_dict()
        assert len(d["findings"]) == 30
        assert d["count"] == 100
