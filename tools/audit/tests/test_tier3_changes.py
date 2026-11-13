# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tests for lib.tier3_changes — what changed since a base ref."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))


from lib import tier3_changes
from lib.config import DEFAULTS, Config, _deep_merge


def _make_config(tmp_path: Path, base_ref: str) -> Config:
    """A Config whose root looks like a git repository."""
    (tmp_path / ".git").mkdir()
    raw = _deep_merge(DEFAULTS, {
        "project": {"name": "test", "root": str(tmp_path), "source_dirs": ["."],
                    "source_extensions": [".cpp", ".py"], "exclude_dirs": []},
        "changes": {"base_ref": base_ref},
    })
    return Config(raw=raw, root=tmp_path)


def _record_git(monkeypatch) -> list[list[str]]:
    """Replace run_cmd with one that records each command and reports no changes."""
    calls: list[list[str]] = []

    def fake_run_cmd(cmd, cwd=None, **_kwargs):
        calls.append(list(cmd))
        return 0, "", ""

    monkeypatch.setattr(tier3_changes, "run_cmd", fake_run_cmd)
    return calls


def test_a_rename_is_filed_under_its_new_name():
    # git writes a rename as "R<score>\told\tnew". The entry used to keep
    # "old\tnew" as its file, which matches no later path.
    entries = tier3_changes._parse_name_status(
        "M\tengine/a.cpp\nR100\tengine/old.cpp\tengine/new.cpp\nC75\tx.py\ty.py\n")
    assert [(e["status"], e["file"]) for e in entries] == [
        ("M", "engine/a.cpp"), ("R", "engine/new.cpp"), ("C", "y.py")]


def test_a_base_ref_that_reads_as_an_option_reaches_no_git_command(tmp_path, monkeypatch):
    # The CLI and the YAML pass base_ref unchecked; only the web UI validates
    # it. "--output=FILE" would make git diff write FILE.
    calls = _record_git(monkeypatch)
    summary, findings = tier3_changes.run(_make_config(tmp_path, "--output=/tmp/x"))
    assert calls == []
    assert summary.changed_files == [] and findings == []


def test_the_base_ref_is_closed_off_from_the_paths(tmp_path, monkeypatch):
    # Without "--" a base_ref that also names a file is ambiguous to git.
    calls = _record_git(monkeypatch)
    tier3_changes.run(_make_config(tmp_path, "HEAD~1"))
    assert calls[0] == ["git", "diff", "--name-status", "HEAD~1", "--"]
    assert ["git", "diff", "--numstat", "HEAD~1", "--"] in calls
