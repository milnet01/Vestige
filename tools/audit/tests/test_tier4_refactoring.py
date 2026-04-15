# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tests for lib.tier4_refactoring — refactoring opportunity detection."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import pytest

from lib.config import Config, DEFAULTS, _deep_merge
from lib.tier4_refactoring import (
    RefactoringResult,
    analyze_refactoring,
    _count_params,
    _detect_long_params,
    _detect_god_files,
    _detect_switch_chains,
    _detect_deep_nesting,
    _extract_param_types,
    _detect_similar_signatures,
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
            "source_extensions": [".cpp", ".h", ".py", ".rs", ".go"],
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
# _count_params
# ---------------------------------------------------------------------------

class TestCountParams:
    def test_cpp_basic(self):
        assert _count_params("int x, float y, const std::string& z", "cpp") == 3

    def test_empty_params(self):
        assert _count_params("", "cpp") == 0
        assert _count_params("   ", "python") == 0

    def test_python_excludes_self(self):
        assert _count_params("self, a, b, c", "python") == 3

    def test_python_excludes_cls(self):
        assert _count_params("cls, a, b", "python") == 2

    def test_python_excludes_star_args(self):
        assert _count_params("self, a, *args, **kwargs", "python") == 1

    def test_rust_excludes_self(self):
        assert _count_params("&self, x: i32, y: f64", "rust") == 2
        assert _count_params("&mut self, x: i32", "rust") == 1


# ---------------------------------------------------------------------------
# _detect_long_params
# ---------------------------------------------------------------------------

class TestDetectLongParams:
    def test_cpp_6_params_flagged(self):
        code = "void foo(int a, int b, int c, int d, int e, int f) {"
        result = _detect_long_params(code, "cpp", "test.cpp", max_params=5)
        assert len(result) == 1
        assert result[0]["name"] == "foo"
        assert result[0]["params"] == 6

    def test_cpp_3_params_ok(self):
        code = "void foo(int a, int b, int c) {"
        result = _detect_long_params(code, "cpp", "test.cpp", max_params=5)
        assert len(result) == 0

    def test_python_function_detected(self):
        code = "def process(a, b, c, d, e, f, g):"
        result = _detect_long_params(code, "python", "test.py", max_params=5)
        assert len(result) == 1
        assert result[0]["params"] == 7

    def test_python_self_not_counted(self):
        code = "def method(self, a, b, c, d):"
        result = _detect_long_params(code, "python", "test.py", max_params=5)
        assert len(result) == 0  # 4 params after excluding self

    def test_rust_function_detected(self):
        code = "pub fn compute(a: i32, b: f64, c: &str, d: Vec<u8>, e: bool, f: usize) -> i32 {"
        result = _detect_long_params(code, "rust", "test.rs", max_params=5)
        assert len(result) == 1

    def test_threshold_configurable(self):
        code = "void foo(int a, int b, int c, int d) {"
        assert len(_detect_long_params(code, "cpp", "t.cpp", max_params=3)) == 1
        assert len(_detect_long_params(code, "cpp", "t.cpp", max_params=5)) == 0


# ---------------------------------------------------------------------------
# _detect_god_files
# ---------------------------------------------------------------------------

class TestDetectGodFiles:
    def test_cpp_16_functions_flagged(self):
        funcs = "\n".join(f"void func_{i}(int x) {{  }}" for i in range(16))
        result = _detect_god_files(funcs, "cpp", "big.cpp", max_functions=15)
        assert result is not None
        assert result["functions"] == 16

    def test_cpp_10_functions_ok(self):
        funcs = "\n".join(f"void func_{i}(int x) {{  }}" for i in range(10))
        result = _detect_god_files(funcs, "cpp", "ok.cpp", max_functions=15)
        assert result is None

    def test_python_defs_counted(self):
        funcs = "\n".join(f"def func_{i}():\n    pass" for i in range(16))
        result = _detect_god_files(funcs, "python", "big.py", max_functions=15)
        assert result is not None

    def test_python_async_def_counted(self):
        funcs = "\n".join(f"async def func_{i}():\n    pass" for i in range(16))
        result = _detect_god_files(funcs, "python", "big.py", max_functions=15)
        assert result is not None


# ---------------------------------------------------------------------------
# _detect_switch_chains
# ---------------------------------------------------------------------------

class TestDetectSwitchChains:
    def test_cpp_8_case_labels_flagged(self):
        cases = "\n".join(f"    case {i}: break;" for i in range(8))
        code = f"switch(x) {{\n{cases}\n}}"
        result = _detect_switch_chains(code, "cpp", "test.cpp",
                                        max_cases=7, max_elifs=5)
        assert len(result) == 1
        assert result[0]["count"] == 8
        assert result[0]["type"] == "case labels"

    def test_cpp_5_case_labels_ok(self):
        cases = "\n".join(f"    case {i}: break;" for i in range(5))
        code = f"switch(x) {{\n{cases}\n}}"
        result = _detect_switch_chains(code, "cpp", "test.cpp",
                                        max_cases=7, max_elifs=5)
        assert len(result) == 0

    def test_python_elif_chain_flagged(self):
        code = "if x == 1:\n    pass\n"
        code += "\n".join(f"elif x == {i}:\n    pass" for i in range(2, 8))
        result = _detect_switch_chains(code, "python", "test.py",
                                        max_cases=7, max_elifs=5)
        assert len(result) == 1
        assert result[0]["type"] == "else-if chain"

    def test_cpp_else_if_chain_flagged(self):
        code = "if (x == 1) {\n} "
        code += " ".join(f"else if (x == {i}) {{\n}}" for i in range(2, 8))
        result = _detect_switch_chains(code, "cpp", "test.cpp",
                                        max_cases=7, max_elifs=5)
        assert len(result) == 1

    def test_separate_switches_independent(self):
        cases1 = "\n".join(f"    case {i}: break;" for i in range(3))
        cases2 = "\n".join(f"    case {i}: break;" for i in range(3))
        code = f"switch(a) {{\n{cases1}\n}}\nswitch(b) {{\n{cases2}\n}}"
        result = _detect_switch_chains(code, "cpp", "test.cpp",
                                        max_cases=7, max_elifs=5)
        assert len(result) == 0  # Neither reaches threshold


# ---------------------------------------------------------------------------
# _detect_deep_nesting
# ---------------------------------------------------------------------------

class TestDetectDeepNesting:
    def test_cpp_5_levels_flagged(self):
        code = ("void deep() {\n"
                "    if (a) {\n"
                "        if (b) {\n"
                "            if (c) {\n"
                "                if (d) {\n"
                "                    if (e) {\n"
                "                        x();\n"
                "                    }\n"
                "                }\n"
                "            }\n"
                "        }\n"
                "    }\n"
                "}\n")
        result = _detect_deep_nesting(code, "cpp", "test.cpp", max_depth=4)
        assert len(result) >= 1
        assert result[0]["name"] == "deep"
        assert result[0]["depth"] > 4

    def test_cpp_2_levels_ok(self):
        code = ("void shallow() {\n"
                "    if (a) {\n"
                "        x();\n"
                "    }\n"
                "}\n")
        result = _detect_deep_nesting(code, "cpp", "test.cpp", max_depth=4)
        assert len(result) == 0

    def test_python_deep_nesting_detected(self):
        code = ("def deep():\n"
                "    if a:\n"
                "        if b:\n"
                "            if c:\n"
                "                if d:\n"
                "                    if e:\n"
                "                        x()\n")
        result = _detect_deep_nesting(code, "python", "test.py", max_depth=4)
        assert len(result) >= 1
        assert result[0]["name"] == "deep"


# ---------------------------------------------------------------------------
# _extract_param_types / _detect_similar_signatures
# ---------------------------------------------------------------------------

class TestExtractParamTypes:
    def test_cpp_extracts_types(self):
        types = _extract_param_types("int x, float y, const std::string& z", "cpp")
        assert types is not None
        assert len(types) == 3

    def test_rust_extracts_types(self):
        types = _extract_param_types("x: i32, y: f64, z: &str", "rust")
        assert types is not None
        assert len(types) == 3

    def test_python_returns_none(self):
        assert _extract_param_types("a, b, c", "python") is None

    def test_empty_returns_none(self):
        assert _extract_param_types("", "cpp") is None


class TestDetectSimilarSignatures:
    def test_identical_types_detected(self):
        sigs = [
            {"name": "foo", "file": "a.cpp", "line": 1, "types": ("int", "float")},
            {"name": "bar", "file": "b.cpp", "line": 1, "types": ("int", "float")},
        ]
        result = _detect_similar_signatures(sigs)
        assert len(result) == 1
        assert result[0]["name_a"] == "foo"
        assert result[0]["name_b"] == "bar"

    def test_different_types_not_flagged(self):
        sigs = [
            {"name": "foo", "file": "a.cpp", "line": 1, "types": ("int", "float")},
            {"name": "bar", "file": "b.cpp", "line": 1, "types": ("double", "char")},
        ]
        result = _detect_similar_signatures(sigs)
        assert len(result) == 0

    def test_same_file_not_flagged(self):
        sigs = [
            {"name": "foo", "file": "a.cpp", "line": 1, "types": ("int", "float")},
            {"name": "bar", "file": "a.cpp", "line": 10, "types": ("int", "float")},
        ]
        result = _detect_similar_signatures(sigs)
        assert len(result) == 0

    def test_single_param_not_flagged(self):
        sigs = [
            {"name": "foo", "file": "a.cpp", "line": 1, "types": ("int",)},
            {"name": "bar", "file": "b.cpp", "line": 1, "types": ("int",)},
        ]
        result = _detect_similar_signatures(sigs)
        assert len(result) == 0  # Requires 2+ params


# ---------------------------------------------------------------------------
# analyze_refactoring (integration)
# ---------------------------------------------------------------------------

class TestAnalyzeRefactoring:
    def test_disabled_returns_empty(self, tmp_path: Path):
        _write_file(tmp_path, "a.cpp", "void f(int a, int b, int c, int d, int e, int f) {}")
        config = _make_config(tmp_path, tier4={"refactoring": {"enabled": False}})
        result, findings = analyze_refactoring(config)
        assert result.total_smells == 0
        assert len(findings) == 0

    def test_returns_correct_structure(self, tmp_path: Path):
        _write_file(tmp_path, "a.cpp", "void f() {}")
        config = _make_config(tmp_path)
        result, _ = analyze_refactoring(config)
        d = result.to_dict()
        assert "long_param_lists" in d
        assert "god_files" in d
        assert "large_switch_chains" in d
        assert "deep_nesting" in d
        assert "similar_signatures" in d
        assert "total_smells" in d

    def test_findings_have_correct_category(self, tmp_path: Path):
        funcs = "\n".join(f"void func_{i}(int x) {{  }}" for i in range(20))
        _write_file(tmp_path, "big.cpp", funcs)
        config = _make_config(tmp_path)
        _, findings = analyze_refactoring(config)
        for f in findings:
            assert f.category == "refactoring"
            assert f.source_tier == 4

    def test_total_smells_accurate(self, tmp_path: Path):
        # Create file with both long params and god file smell
        funcs = "\n".join(
            f"void func_{i}(int a, int b, int c, int d, int e, int f) {{  }}"
            for i in range(20)
        )
        _write_file(tmp_path, "smelly.cpp", funcs)
        config = _make_config(tmp_path)
        result, _ = analyze_refactoring(config)
        expected = (
            len(result.long_param_lists) + len(result.god_files)
            + len(result.large_switch_chains) + len(result.deep_nesting)
            + len(result.similar_signatures)
        )
        assert result.total_smells == expected


class TestRefactoringResult:
    def test_to_dict_caps_lists(self):
        result = RefactoringResult()
        result.long_param_lists = [{"name": f"f{i}", "file": "x", "line": i, "params": 10}
                                    for i in range(30)]
        d = result.to_dict()
        assert len(d["long_param_lists"]) == 20  # capped at 20

    def test_to_dict_includes_all_fields(self):
        result = RefactoringResult(total_smells=5)
        d = result.to_dict()
        assert d["total_smells"] == 5
        assert "long_param_lists" in d
        assert "god_files" in d
        assert "large_switch_chains" in d
        assert "deep_nesting" in d
        assert "similar_signatures" in d
