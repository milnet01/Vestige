# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tests for the run_cmd / run_shell_cmd split (AUDIT.md §C1 / FIXPLAN C1b)."""

from __future__ import annotations

import os
import tempfile
from pathlib import Path

import pytest

from lib.utils import run_cmd, run_shell_cmd


def test_run_cmd_rejects_string():
    """run_cmd must refuse str input — that's what run_shell_cmd is for."""
    with pytest.raises(TypeError):
        run_cmd("echo hi")


def test_run_shell_cmd_rejects_list():
    """run_shell_cmd must refuse list input — use run_cmd."""
    with pytest.raises(TypeError):
        run_shell_cmd(["echo", "hi"])


def test_run_cmd_executes_list_form():
    """run_cmd with a list runs under shell=False."""
    rc, out, _err = run_cmd(["echo", "hello"])
    assert rc == 0
    assert "hello" in out


def test_run_cmd_does_not_interpret_shell_metachars():
    """With shell=False, `;` in an argument is a literal character, not a
    command separator. This is the whole point of the refactor."""
    # If shell=False is honoured, echo receives `foo; touch /tmp/pwn_c1b`
    # as a SINGLE argument string — no second command runs.
    pwn = Path(tempfile.gettempdir()) / "pwn_c1b_refactor_test"
    if pwn.exists():
        pwn.unlink()
    rc, out, _err = run_cmd(["echo", f"foo; touch {pwn}"])
    assert rc == 0
    assert f"foo; touch {pwn}" in out
    assert not pwn.exists(), (
        "shell metacharacter was interpreted — run_cmd is not actually "
        "shell=False any more!"
    )


def test_run_shell_cmd_does_interpret_metachars():
    """run_shell_cmd is the explicit opt-in — metachars ARE interpreted."""
    rc, out, _err = run_shell_cmd("echo first && echo second")
    assert rc == 0
    # Both echoes should have fired under the shell.
    assert "first" in out
    assert "second" in out


def test_run_cmd_returns_on_nonexistent_binary():
    """Missing binary returns (-1, '', err) rather than raising."""
    rc, out, err = run_cmd(["/definitely/not/a/real/binary", "arg"])
    assert rc == -1
    assert out == ""
    assert "not" in err.lower() or "no such" in err.lower()


def test_run_cmd_respects_cwd():
    """cwd kwarg is honoured (verifies we didn't drop it in the refactor)."""
    with tempfile.TemporaryDirectory() as tmp:
        rc, out, _err = run_cmd(["pwd"], cwd=tmp)
        assert rc == 0
        # On macOS /tmp is a symlink to /private/tmp; compare resolved.
        assert Path(out.strip()).resolve() == Path(tmp).resolve()
