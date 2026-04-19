# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tests for lib.tier4_token_shingle — DRY token-shingle similarity."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from lib.config import Config, DEFAULTS, _deep_merge
from lib.tier4_token_shingle import (
    TokenShingleResult,
    analyze_token_shingle,
    _tokenize,
    _shingle_hashes,
    _strip_for_lang,
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
# _tokenize / _strip_for_lang
# ---------------------------------------------------------------------------

class TestTokenize:
    def test_strips_line_comment(self):
        text = "int x = 1;  // comment\nint y = 2;\n"
        toks = _tokenize(text, ".cpp")
        assert "comment" not in toks
        assert "x" in toks and "y" in toks

    def test_strips_block_comment(self):
        text = "int x = 1; /* block\n   spans */ int y = 2;\n"
        toks = _tokenize(text, ".cpp")
        assert "block" not in toks
        assert "spans" not in toks

    def test_strips_string_literal(self):
        text = 'std::string s = "hello world";\n'
        toks = _tokenize(text, ".cpp")
        # The string contents should be gone — only the variable name
        # and identifiers / punctuation remain.
        assert "hello" not in toks
        assert "world" not in toks
        assert "s" in toks

    def test_python_strips_hash_comment(self):
        text = "x = 1  # a comment\ny = 2\n"
        toks = _tokenize(text, ".py")
        assert "comment" not in toks


# ---------------------------------------------------------------------------
# _shingle_hashes
# ---------------------------------------------------------------------------

class TestShingleHashes:
    def test_empty_when_too_short(self):
        toks = ["a", "b", "c"]
        assert _shingle_hashes(toks, k=5) == set()

    def test_count_matches_window(self):
        toks = ["a", "b", "c", "d", "e"]
        # 5 tokens, k=3 → 3 shingles (abc, bcd, cde).
        assert len(_shingle_hashes(toks, k=3)) == 3

    def test_identical_inputs_share_hashes(self):
        a = _shingle_hashes(["x", "y", "z", "w"], k=3)
        b = _shingle_hashes(["x", "y", "z", "w"], k=3)
        assert a == b


# ---------------------------------------------------------------------------
# analyze_token_shingle (integration)
# ---------------------------------------------------------------------------

class TestAnalyzeTokenShingle:
    def test_flags_near_duplicate_files(self, tmp_path: Path):
        # Two files with the same token sequence (only formatting differs).
        body = "\n".join([
            f"int compute_{{}}(int a, int b, int c) {{",
            "    int x = a + b;",
            "    int y = b + c;",
            "    int z = a * b * c;",
            "    int w = (x + y) * z;",
            "    return w + x + y + z;",
            "}",
        ])
        _write(tmp_path, "a.cpp", body.replace("{{}}", "_a"))
        _write(tmp_path, "b.cpp", body.replace("{{}}", "_a"))   # IDENTICAL body
        config = _make_config(
            tmp_path,
            tier4={"token_shingle": {
                "min_tokens": 10, "shingle_size": 4,
                "similarity_threshold": 0.5,
            }},
        )
        result, findings = analyze_token_shingle(config)
        # The two files should appear as one pair with high similarity.
        assert len(result.pairs) >= 1
        # Find a pair that involves a.cpp and b.cpp.
        match = [p for p in result.pairs
                 if {p.file_a, p.file_b} == {"a.cpp", "b.cpp"}]
        assert len(match) == 1
        assert match[0].similarity > 0.9

    def test_different_files_below_threshold(self, tmp_path: Path):
        _write(tmp_path, "a.cpp",
               "int compute_a(int x) { return x * 2 + 1; }\n")
        _write(tmp_path, "b.cpp",
               "void render_loop(Frame& f) { f.draw(); f.flush(); }\n")
        config = _make_config(
            tmp_path,
            tier4={"token_shingle": {
                "min_tokens": 5, "shingle_size": 3,
                "similarity_threshold": 0.6,
            }},
        )
        result, findings = analyze_token_shingle(config)
        # Different token sequences → no high-similarity pair.
        match = [p for p in result.pairs
                 if {p.file_a, p.file_b} == {"a.cpp", "b.cpp"}]
        assert len(match) == 0

    def test_min_tokens_skips_small_files(self, tmp_path: Path):
        _write(tmp_path, "tiny.cpp", "int x;\n")
        _write(tmp_path, "tiny2.cpp", "int x;\n")
        config = _make_config(
            tmp_path,
            tier4={"token_shingle": {"min_tokens": 100}},
        )
        result, findings = analyze_token_shingle(config)
        # Both files filtered out; no pairs possible.
        assert result.files_scanned == 0
        assert len(result.pairs) == 0

    def test_disabled_returns_empty(self, tmp_path: Path):
        _write(tmp_path, "a.cpp",
               "int compute(int x) { return x * 2 + 1; }\n")
        _write(tmp_path, "b.cpp",
               "int compute(int x) { return x * 2 + 1; }\n")
        config = _make_config(
            tmp_path,
            tier4={"token_shingle": {"enabled": False}},
        )
        result, findings = analyze_token_shingle(config)
        assert len(result.pairs) == 0
        assert len(findings) == 0

    def test_self_exempt_basename(self, tmp_path: Path):
        # A file literally named tier4_token_shingle.py would otherwise
        # match its own description strings — exempt it.
        _write(tmp_path, "tier4_token_shingle.py",
               "# token shingle similarity description\n" * 50)
        _write(tmp_path, "real.cpp",
               "void render() { draw(); }\n" * 30)
        config = _make_config(tmp_path)
        config.raw["project"]["source_extensions"] = [".cpp", ".h", ".py"]
        result, findings = analyze_token_shingle(config)
        for p in result.pairs:
            assert "tier4_token_shingle" not in p.file_a
            assert "tier4_token_shingle" not in p.file_b


class TestTokenShingleResult:
    def test_to_dict_caps_list(self):
        from lib.tier4_token_shingle import ShinglePair
        result = TokenShingleResult()
        result.pairs = [
            ShinglePair(
                file_a=f"a{i}.cpp", file_b=f"b{i}.cpp",
                similarity=0.9, shared_shingles=10,
                shingles_a=20, shingles_b=20,
            )
            for i in range(50)
        ]
        d = result.to_dict()
        assert len(d["pairs"]) == 30
        assert d["pair_count"] == 50
