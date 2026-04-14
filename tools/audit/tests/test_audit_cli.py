"""Tests for audit.py CLI entry-point helpers.

Covers ``apply_no_color`` — the NO_COLOR convention handler wired into
``audit.py --no-color``. The audit tool itself emits no ANSI today, but
it spawns cppcheck, clang-tidy, and git as subprocesses; the flag's job
is to set ``NO_COLOR=1`` in the environment so those children inherit
the preference. See https://no-color.org.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

# audit.py lives one level above tools/audit/tests/
SCRIPT_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SCRIPT_DIR))

from audit import apply_no_color  # noqa: E402


class TestApplyNoColor:
    """apply_no_color should honour explicit flag, env var, and non-TTY stdout."""

    def test_explicit_flag_sets_no_color(self):
        env: dict[str, str] = {}
        assert apply_no_color(explicit=True, env=env, stdout_is_tty=True) is True
        assert env["NO_COLOR"] == "1"

    def test_no_flag_with_tty_leaves_env_untouched(self):
        env: dict[str, str] = {}
        assert apply_no_color(explicit=False, env=env, stdout_is_tty=True) is False
        assert "NO_COLOR" not in env

    def test_non_tty_stdout_triggers_no_color(self):
        """CI and piped invocations default to no colour."""
        env: dict[str, str] = {}
        assert apply_no_color(explicit=False, env=env, stdout_is_tty=False) is True
        assert env["NO_COLOR"] == "1"

    def test_preexisting_no_color_is_respected(self):
        """If NO_COLOR is already set to any non-empty value, stay disabled."""
        env = {"NO_COLOR": "yes-please"}
        assert apply_no_color(explicit=False, env=env, stdout_is_tty=True) is True
        # Preserved (we normalise on disable but the important bit is it's set)
        assert env["NO_COLOR"] == "1"

    def test_empty_no_color_env_is_not_triggered(self):
        """Per spec: presence with non-empty value signals disable. Empty = not set."""
        env = {"NO_COLOR": ""}
        assert apply_no_color(explicit=False, env=env, stdout_is_tty=True) is False
        # Empty string is left as-is; we only write "1" when we decide to disable
        assert env.get("NO_COLOR", "") == ""

    def test_explicit_beats_everything(self):
        """Explicit flag disables even with a TTY and no env var."""
        env: dict[str, str] = {}
        assert apply_no_color(explicit=True, env=env, stdout_is_tty=True) is True
        assert env["NO_COLOR"] == "1"

    def test_default_env_is_os_environ(self, monkeypatch):
        """Smoke-test that the default ``env=None`` path hits os.environ."""
        monkeypatch.delenv("NO_COLOR", raising=False)
        # Force non-TTY deterministically via the kwarg so this test doesn't
        # depend on how pytest captures stdout.
        result = apply_no_color(explicit=True, stdout_is_tty=False)
        assert result is True
        import os
        assert os.environ.get("NO_COLOR") == "1"


class TestAuditCliAcceptsNoColor:
    """End-to-end: the failing CI invocation ``-t 1 --no-color`` no longer
    raises ``unrecognized arguments``. We only check argparse acceptance
    here (via ``--help``-style early exit), not the full audit run, which
    is covered by the runner tests."""

    def test_no_color_in_help_text(self):
        """``--help`` should mention the new flag."""
        result = subprocess.run(
            [sys.executable, str(SCRIPT_DIR / "audit.py"), "--help"],
            capture_output=True,
            text=True,
            timeout=30,
        )
        assert result.returncode == 0
        assert "--no-color" in result.stdout

    def test_no_color_parses_without_error(self, tmp_path):
        """``--no-color`` combined with a harmless early-exit flag
        (``--list-patterns``) must not trigger the argparse error that
        was failing CI."""
        # Minimal config so load_config + --list-patterns short-circuits cleanly.
        cfg = tmp_path / "audit_config.yaml"
        cfg.write_text(
            "project:\n"
            "  name: TestProject\n"
            f"  root: {tmp_path}\n"
            "  source_dirs: [src/]\n"
            "  source_extensions: ['.cpp', '.h']\n"
            "tiers: [1]\n"
            "patterns:\n"
            "  memory: []\n"
            "report:\n"
            "  output_path: out.md\n"
        )
        (tmp_path / "src").mkdir()

        result = subprocess.run(
            [
                sys.executable, str(SCRIPT_DIR / "audit.py"),
                "-c", str(cfg),
                "--list-patterns",
                "--no-color",
            ],
            capture_output=True,
            text=True,
            timeout=30,
        )
        # The critical assertion: no argparse rejection.
        assert "unrecognized arguments" not in result.stderr
        assert result.returncode == 0
