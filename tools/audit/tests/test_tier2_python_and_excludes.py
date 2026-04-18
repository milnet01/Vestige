# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tests for tier2 Python comment stripping and rule-source exclusion (audit 2.11.0).

Covers:
  - A1: ``exclude_file_patterns`` glob + built-in ``auto_config.py`` skip.
  - A2: ``_classify_line_python`` — # comments, single/triple-quoted strings.
  - A3: ``utils.enumerate_files`` uses ``git ls-files`` when available.
"""

from __future__ import annotations

import re
import subprocess
from pathlib import Path

import pytest

from lib.tier2_patterns import (
    _classify_line_python,
    _is_python_like,
    _scan_file,
    run,
)
from lib.utils import _git_ls_files, enumerate_files
from lib.config import Config, DEFAULTS, _deep_merge


# ---------------------------------------------------------------------------
# A2 — Python comment + string stripping
# ---------------------------------------------------------------------------


class TestIsPythonLike:
    def test_py_extension(self, tmp_path: Path):
        assert _is_python_like(tmp_path / "foo.py") is True

    def test_shell_extensions(self, tmp_path: Path):
        assert _is_python_like(tmp_path / "foo.sh") is True
        assert _is_python_like(tmp_path / "foo.bash") is True

    def test_config_extensions(self, tmp_path: Path):
        assert _is_python_like(tmp_path / "config.yaml") is True
        assert _is_python_like(tmp_path / "config.toml") is True

    def test_c_extensions_are_not(self, tmp_path: Path):
        assert _is_python_like(tmp_path / "foo.cpp") is False
        assert _is_python_like(tmp_path / "foo.h") is False


class TestClassifyLinePython:
    def test_hash_comment_stripped(self):
        out, state = _classify_line_python("foo  # this is a comment\n", (False, ""))
        assert "#" not in out
        assert "comment" not in out
        assert "foo" in out
        assert state == (False, "")

    def test_double_quoted_string_masked(self):
        out, _ = _classify_line_python('x = "hello world"\n', (False, ""))
        # Masked contents shouldn't match an identifier-style regex.
        assert "hello" not in out
        assert "x" in out

    def test_single_quoted_string_masked(self):
        out, _ = _classify_line_python("x = 'hello world'\n", (False, ""))
        assert "hello" not in out

    def test_triple_quoted_string_masked_single_line(self):
        out, state = _classify_line_python(
            'x = """all text here""" + y\n', (False, ""))
        assert "all text here" not in out
        assert "y" in out
        assert state == (False, "")

    def test_triple_quoted_opens_stays_open(self):
        # Opening triple without closing — state carries into next line.
        out, state = _classify_line_python('"""start of docstring\n',
                                           (False, ""))
        assert "start of docstring" not in out
        assert state == (True, '"')

    def test_triple_quoted_resumes_and_closes(self):
        # First line opens, second line closes.
        _, state1 = _classify_line_python('"""start\n', (False, ""))
        assert state1 == (True, '"')
        out2, state2 = _classify_line_python('end""" + x\n', state1)
        assert "start" not in out2
        assert "end" not in out2
        assert "x" in out2
        assert state2 == (False, "")

    def test_command_injection_word_in_comment_is_stripped(self):
        # The actual regression — "system" in a Python docstring or
        # comment should NOT match a command-injection regex when
        # skip_comments is active.
        line = "# handles system() call validation\n"
        out, _ = _classify_line_python(line, (False, ""))
        assert "system" not in out


# ---------------------------------------------------------------------------
# A2 — integration: _scan_file routes Python files to the Python classifier
# ---------------------------------------------------------------------------


class TestScanFilePythonDispatch:
    def test_python_comment_not_matched(self, tmp_path: Path):
        p = tmp_path / "sample.py"
        p.write_text("# gets() is unsafe\nx = 1\n")
        regex = re.compile(r"\bgets\s*\(")
        hits = _scan_file(p, regex, exclude_re=None, skip_comments=True)
        assert hits == []

    def test_python_triple_string_not_matched(self, tmp_path: Path):
        p = tmp_path / "sample.py"
        p.write_text('x = """gets() is unsafe"""\ny = 2\n')
        regex = re.compile(r"\bgets\s*\(")
        hits = _scan_file(p, regex, exclude_re=None, skip_comments=True)
        assert hits == []

    def test_python_code_still_matched(self, tmp_path: Path):
        # A real code occurrence — no comment, no string literal —
        # should still hit. The classifier isn't a blanket suppressor.
        p = tmp_path / "sample.py"
        p.write_text("def foo():\n    return gets()\n")
        regex = re.compile(r"\bgets\s*\(")
        hits = _scan_file(p, regex, exclude_re=None, skip_comments=True)
        assert len(hits) == 1
        assert hits[0][0] == 2

    def test_c_file_still_uses_c_classifier(self, tmp_path: Path):
        # Regression test: don't route .cpp through the Python path.
        p = tmp_path / "sample.cpp"
        p.write_text("// gets() is unsafe\nint x = 1;\n")
        regex = re.compile(r"\bgets\s*\(")
        hits = _scan_file(p, regex, exclude_re=None, skip_comments=True)
        assert hits == []


# ---------------------------------------------------------------------------
# A1 — exclude_file_patterns + built-in rule-source skip
# ---------------------------------------------------------------------------


@pytest.fixture()
def python_project(tmp_path: Path) -> Path:
    """Tiny Python project with one file containing a rule-source-like line."""
    src = tmp_path / "src"
    src.mkdir()
    # Rule-source lookalike — description text contains tokens a
    # command-injection rule would match.
    (src / "auto_config.py").write_text(
        '{"description": "gets() is inherently unsafe"}\n'
        'x = 1\n'
    )
    (src / "real.py").write_text(
        "def foo():\n    return gets()\n"
    )
    return tmp_path


def _config_for(root: Path, source_dirs: list[str],
                extra_patterns: dict | None = None) -> Config:
    raw = _deep_merge(DEFAULTS, {
        "project": {
            "root": str(root),
            "source_dirs": source_dirs,
            "source_extensions": [".py"],
            "exclude_dirs": [],
        },
        "patterns": extra_patterns or {},
    })
    return Config(raw=raw, root=root)


class TestRuleSourceExclude:
    def test_auto_config_py_builtin_skipped(self, python_project: Path):
        cfg = _config_for(python_project, ["src/"], {
            "security": [{
                "name": "unsafe_gets",
                "pattern": r"\bgets\s*\(",
                "file_glob": "*.py",
                "severity": "high",
                "description": "gets() is unsafe",
                "skip_comments": False,
            }],
        })
        findings = run(cfg)
        # real.py:2 should still fire; auto_config.py should be skipped.
        files_hit = {f.file.split("/")[-1] for f in findings}
        assert "real.py" in files_hit
        assert "auto_config.py" not in files_hit

    def test_user_pattern_honoured(self, python_project: Path, tmp_path: Path):
        # Add a second rule-source file + a user exclude for it.
        (tmp_path / "src" / "custom_rules.py").write_text(
            '{"description": "gets() is inherently unsafe"}\n')
        cfg = _config_for(python_project, ["src/"], {
            "security": [{
                "name": "unsafe_gets",
                "pattern": r"\bgets\s*\(",
                "file_glob": "*.py",
                "severity": "high",
                "description": "gets() is unsafe",
                "skip_comments": False,
            }],
        })
        cfg.raw["project"]["exclude_file_patterns"] = ["custom_rules.py"]
        findings = run(cfg)
        files_hit = {f.file.split("/")[-1] for f in findings}
        assert "custom_rules.py" not in files_hit
        assert "auto_config.py" not in files_hit  # built-in still applies
        assert "real.py" in files_hit


# ---------------------------------------------------------------------------
# A3 — utils.enumerate_files uses git ls-files when in a git repo
# ---------------------------------------------------------------------------


class TestGitLsFilesIntegration:
    def test_non_git_root_falls_back(self, tmp_path: Path):
        # tmp_path is not a git repo; _git_ls_files should return None.
        assert _git_ls_files(tmp_path) is None

    def test_git_repo_returns_list(self, tmp_path: Path):
        subprocess.run(["git", "init", "-q", str(tmp_path)], check=True)
        (tmp_path / "tracked.py").write_text("x = 1\n")
        subprocess.run(["git", "-C", str(tmp_path), "add", "tracked.py"],
                       check=True)
        got = _git_ls_files(tmp_path)
        assert got is not None
        assert "tracked.py" in got

    def test_gitignored_pyc_excluded(self, tmp_path: Path):
        # __pycache__/foo.pyc should NOT appear — it's in the default
        # gitignore-standard behaviour.
        subprocess.run(["git", "init", "-q", str(tmp_path)], check=True)
        (tmp_path / ".gitignore").write_text("__pycache__/\n")
        (tmp_path / "src").mkdir()
        (tmp_path / "src" / "real.py").write_text("x = 1\n")
        cache = tmp_path / "src" / "__pycache__"
        cache.mkdir()
        (cache / "real.cpython-311.pyc").write_bytes(b"\x00\x00")
        subprocess.run(["git", "-C", str(tmp_path), "add",
                        ".gitignore", "src/real.py"], check=True)

        files = enumerate_files(
            root=tmp_path,
            source_dirs=["src/"],
            extensions=[".py", ".pyc"],
            exclude_dirs=[],
        )
        names = {f.name for f in files}
        assert "real.py" in names
        assert not any(n.endswith(".pyc") for n in names), \
            f"expected no .pyc files, got: {names}"

    def test_source_dir_filtering_still_applied(self, tmp_path: Path):
        subprocess.run(["git", "init", "-q", str(tmp_path)], check=True)
        (tmp_path / "src").mkdir()
        (tmp_path / "out").mkdir()
        (tmp_path / "src" / "a.py").write_text("a = 1\n")
        (tmp_path / "out" / "b.py").write_text("b = 2\n")
        subprocess.run(
            ["git", "-C", str(tmp_path), "add", "src/a.py", "out/b.py"],
            check=True,
        )
        files = enumerate_files(
            root=tmp_path,
            source_dirs=["src/"],
            extensions=[".py"],
            exclude_dirs=[],
        )
        names = {f.name for f in files}
        assert "a.py" in names
        assert "b.py" not in names
