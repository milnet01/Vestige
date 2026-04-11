"""Tests for lib.tier4_cognitive — cognitive complexity analysis."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import pytest

from lib.config import Config, DEFAULTS, _deep_merge
from lib.tier4_cognitive import (
    CognitiveResult,
    analyze_cognitive_complexity,
    _analyze_brace_language,
    _analyze_python,
    _score_logical_operators,
    _strip_strings_and_comments,
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
            "source_extensions": [".cpp", ".py"],
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
# _strip_strings_and_comments
# ---------------------------------------------------------------------------

class TestStripStringsAndComments:
    def test_strips_cpp_line_comment(self):
        result = _strip_strings_and_comments("int x = 1; // a comment", "cpp")
        assert "//" not in result
        assert "int x = 1;" in result

    def test_strips_python_hash_comment(self):
        result = _strip_strings_and_comments("x = 1  # comment", "python")
        assert "#" not in result
        assert "x = 1" in result

    def test_preserves_string_content(self):
        result = _strip_strings_and_comments('x = "hello world"', "cpp")
        assert 'x = ""' == result.strip()


# ---------------------------------------------------------------------------
# _score_logical_operators
# ---------------------------------------------------------------------------

class TestScoreLogicalOperators:
    def test_single_and(self):
        assert _score_logical_operators("a && b", "cpp") == 1

    def test_single_or(self):
        assert _score_logical_operators("a || b", "cpp") == 1

    def test_mixed_and_or(self):
        assert _score_logical_operators("a && b || c", "cpp") == 2

    def test_no_operators(self):
        assert _score_logical_operators("a + b", "cpp") == 0

    def test_python_and(self):
        assert _score_logical_operators("a and b", "python") == 1

    def test_python_or(self):
        assert _score_logical_operators("a or b", "python") == 1

    def test_python_mixed(self):
        assert _score_logical_operators("a and b or c", "python") == 2


# ---------------------------------------------------------------------------
# _analyze_brace_language — C++ function scoring
# ---------------------------------------------------------------------------

class TestAnalyzeBraceLanguage:
    def test_simple_function_score_0(self):
        code = (
            "void simple() {\n"
            "    int x = 1;\n"
            "    int y = 2;\n"
            "    return;\n"
            "}\n"
        )
        funcs = _analyze_brace_language(code, "cpp", "test.cpp")
        assert len(funcs) == 1
        assert funcs[0]["name"] == "simple"
        assert funcs[0]["score"] == 0

    def test_single_if_score_1(self):
        code = (
            "void check(int x) {\n"
            "    if (x > 0) {\n"
            "        return;\n"
            "    }\n"
            "}\n"
        )
        funcs = _analyze_brace_language(code, "cpp", "test.cpp")
        assert len(funcs) == 1
        assert funcs[0]["score"] == 1

    def test_nested_if_2_levels(self):
        # if (+1, nesting=0) -> nested if (+1 base + 1 nesting = +2)
        # Total: 3
        code = (
            "void nested(int x, int y) {\n"
            "    if (x > 0) {\n"
            "        if (y > 0) {\n"
            "            return;\n"
            "        }\n"
            "    }\n"
            "}\n"
        )
        funcs = _analyze_brace_language(code, "cpp", "test.cpp")
        assert len(funcs) == 1
        assert funcs[0]["score"] == 3

    def test_for_with_nested_if(self):
        # for (+1, nesting=0) -> if (+1 base + 1 nesting = +2)
        # Total: 3
        code = (
            "void loop(int* arr, int n) {\n"
            "    for (int i = 0; i < n; i++) {\n"
            "        if (arr[i] > 0) {\n"
            "            process(arr[i]);\n"
            "        }\n"
            "    }\n"
            "}\n"
        )
        funcs = _analyze_brace_language(code, "cpp", "test.cpp")
        assert len(funcs) == 1
        assert funcs[0]["score"] == 3

    def test_logical_operators_add_score(self):
        # if (+1) + logical && (+1) = 2
        code = (
            "void check(int a, int b) {\n"
            "    if (a > 0 && b > 0) {\n"
            "        return;\n"
            "    }\n"
            "}\n"
        )
        funcs = _analyze_brace_language(code, "cpp", "test.cpp")
        assert len(funcs) == 1
        assert funcs[0]["score"] == 2

    def test_else_if_chain(self):
        # if (+1) + else if (+1) + else (+1) = 3
        code = (
            "void classify(int x) {\n"
            "    if (x > 10) {\n"
            "        big();\n"
            "    } else if (x > 0) {\n"
            "        small();\n"
            "    } else {\n"
            "        negative();\n"
            "    }\n"
            "}\n"
        )
        funcs = _analyze_brace_language(code, "cpp", "test.cpp")
        assert len(funcs) == 1
        assert funcs[0]["score"] == 3

    def test_ternary_operator(self):
        # ternary (+1, nesting=0) = 1
        code = (
            "int pick(int a, int b) {\n"
            "    return a > b ? a : b;\n"
            "}\n"
        )
        funcs = _analyze_brace_language(code, "cpp", "test.cpp")
        assert len(funcs) == 1
        assert funcs[0]["score"] == 1

    def test_switch_counts(self):
        # switch (+1, nesting=0) = 1
        code = (
            "void sw(int x) {\n"
            "    switch (x) {\n"
            "        case 1: break;\n"
            "        case 2: break;\n"
            "    }\n"
            "}\n"
        )
        funcs = _analyze_brace_language(code, "cpp", "test.cpp")
        assert len(funcs) == 1
        assert funcs[0]["score"] == 1


# ---------------------------------------------------------------------------
# _analyze_python — indentation-based scoring
# ---------------------------------------------------------------------------

class TestAnalyzePython:
    def test_simple_python_function_score_0(self):
        code = (
            "def simple():\n"
            "    x = 1\n"
            "    return x\n"
        )
        funcs = _analyze_python(code, "test.py")
        assert len(funcs) == 1
        assert funcs[0]["score"] == 0

    def test_python_single_if(self):
        code = (
            "def check(x):\n"
            "    if x > 0:\n"
            "        return True\n"
            "    return False\n"
        )
        funcs = _analyze_python(code, "test.py")
        assert len(funcs) == 1
        assert funcs[0]["score"] == 1

    def test_python_nested_if(self):
        # if (+1, nesting=0) -> if (+1 base + 1 nesting = +2) = 3
        code = (
            "def nested(x, y):\n"
            "    if x > 0:\n"
            "        if y > 0:\n"
            "            return True\n"
            "    return False\n"
        )
        funcs = _analyze_python(code, "test.py")
        assert len(funcs) == 1
        assert funcs[0]["score"] == 3

    def test_python_elif_chain(self):
        # if (+1) + elif (+1) + else (+1) = 3
        code = (
            "def classify(x):\n"
            "    if x > 10:\n"
            "        return 'big'\n"
            "    elif x > 0:\n"
            "        return 'small'\n"
            "    else:\n"
            "        return 'negative'\n"
        )
        funcs = _analyze_python(code, "test.py")
        assert len(funcs) == 1
        assert funcs[0]["score"] == 3

    def test_python_logical_operators(self):
        # if (+1) + logical and (+1) = 2
        code = (
            "def check(a, b):\n"
            "    if a > 0 and b > 0:\n"
            "        return True\n"
        )
        funcs = _analyze_python(code, "test.py")
        assert len(funcs) == 1
        assert funcs[0]["score"] == 2

    def test_python_for_with_nested_if(self):
        # for (+1, nesting=0) -> if (+1 + 1 nesting = 2) = 3
        code = (
            "def loop(items):\n"
            "    for item in items:\n"
            "        if item > 0:\n"
            "            process(item)\n"
        )
        funcs = _analyze_python(code, "test.py")
        assert len(funcs) == 1
        assert funcs[0]["score"] == 3


# ---------------------------------------------------------------------------
# analyze_cognitive_complexity (integration)
# ---------------------------------------------------------------------------

class TestAnalyzeCognitiveComplexity:
    def test_disabled_returns_empty(self, tmp_path: Path):
        _write_file(tmp_path, "a.cpp",
                     "void complex(int x) {\n"
                     "    if (x) {\n"
                     "        if (x > 1) {\n"
                     "            for (int i = 0; i < x; i++) {\n"
                     "                if (i > 2) { process(); }\n"
                     "            }\n"
                     "        }\n"
                     "    }\n"
                     "}\n")
        config = _make_config(tmp_path, tier4={"cognitive": {"enabled": False}})
        result, findings = analyze_cognitive_complexity(config)

        assert result.total_functions == 0
        assert len(findings) == 0

    def test_findings_have_correct_category(self, tmp_path: Path):
        # Build a function above threshold (threshold=1 for test)
        _write_file(tmp_path, "a.cpp",
                     "void complex(int x) {\n"
                     "    if (x > 0) {\n"
                     "        if (x > 1) {\n"
                     "            return;\n"
                     "        }\n"
                     "    }\n"
                     "}\n")
        config = _make_config(tmp_path, tier4={"cognitive": {"threshold": 1}})
        result, findings = analyze_cognitive_complexity(config)

        assert len(findings) > 0
        for f in findings:
            assert f.category == "cognitive_complexity"
            assert f.source_tier == 4

    def test_severity_high_for_double_threshold(self, tmp_path: Path):
        # Score = 3 (nested if), threshold = 1 -> score >= 2*threshold -> HIGH
        _write_file(tmp_path, "a.cpp",
                     "void nested(int x, int y) {\n"
                     "    if (x > 0) {\n"
                     "        if (y > 0) {\n"
                     "            return;\n"
                     "        }\n"
                     "    }\n"
                     "}\n")
        config = _make_config(tmp_path, tier4={"cognitive": {"threshold": 1}})
        _, findings = analyze_cognitive_complexity(config)

        assert len(findings) > 0
        from lib.findings import Severity
        assert findings[0].severity == Severity.HIGH

    def test_severity_medium_at_threshold(self, tmp_path: Path):
        # Single if -> score = 1, threshold = 1 -> score < 2*threshold -> MEDIUM
        _write_file(tmp_path, "a.cpp",
                     "void check(int x) {\n"
                     "    if (x > 0) {\n"
                     "        return;\n"
                     "    }\n"
                     "}\n")
        config = _make_config(tmp_path, tier4={"cognitive": {"threshold": 1}})
        _, findings = analyze_cognitive_complexity(config)

        assert len(findings) > 0
        from lib.findings import Severity
        assert findings[0].severity == Severity.MEDIUM

    def test_below_threshold_not_in_findings(self, tmp_path: Path):
        _write_file(tmp_path, "a.cpp",
                     "void simple() {\n"
                     "    int x = 1;\n"
                     "}\n")
        config = _make_config(tmp_path, tier4={"cognitive": {"threshold": 5}})
        _, findings = analyze_cognitive_complexity(config)

        assert len(findings) == 0

    def test_python_file_analyzed(self, tmp_path: Path):
        _write_file(tmp_path, "mod.py",
                     "def nested(x, y):\n"
                     "    if x > 0:\n"
                     "        if y > 0:\n"
                     "            return True\n"
                     "    return False\n")
        config = _make_config(tmp_path, tier4={"cognitive": {"threshold": 1}})
        result, findings = analyze_cognitive_complexity(config)

        assert result.files_scanned >= 1
        assert result.total_functions >= 1
        assert len(findings) > 0


# ---------------------------------------------------------------------------
# CognitiveResult
# ---------------------------------------------------------------------------

class TestCognitiveResult:
    def test_to_dict_caps_at_30(self):
        result = CognitiveResult()
        result.functions = [
            {"name": f"f{i}", "file": "a.cpp", "line": i, "score": i, "details": []}
            for i in range(50)
        ]
        d = result.to_dict()
        assert len(d["functions"]) == 30

    def test_to_dict_includes_stats(self):
        result = CognitiveResult(
            files_scanned=10,
            total_functions=50,
            avg_complexity=5.5,
            max_complexity=42,
            above_threshold=3,
        )
        d = result.to_dict()
        assert d["files_scanned"] == 10
        assert d["total_functions"] == 50
        assert d["avg_complexity"] == 5.5
        assert d["max_complexity"] == 42
        assert d["above_threshold"] == 3
