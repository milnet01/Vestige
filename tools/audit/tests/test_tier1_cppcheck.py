# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tests for lib.tier1_cppcheck — which parser reads cppcheck's output."""

from __future__ import annotations

from types import SimpleNamespace

import lib.tier1_cppcheck as cppcheck

_XML_ALL_FILTERED = (
    '<?xml version="1.0" encoding="UTF-8"?>\n'
    '<results version="2"><cppcheck version="2.16"/><errors>'
    '<error id="missingIncludeSystem" severity="information" msg="m">'
    '<location file="engine/a.cpp" line="1"/></error>'
    '</errors></results>\n'
)


def _config() -> SimpleNamespace:
    settings = {"enabled": True, "targets": ["engine"]}
    return SimpleNamespace(
        root=".", exclude_dirs=[],
        get=lambda *keys, default=None: settings)


def _run_with_stderr(monkeypatch, stderr: str) -> list[str]:
    """Run cppcheck.run on canned stderr; return the text parser's inputs."""
    text_calls: list[str] = []
    monkeypatch.setattr(cppcheck, "run_cmd",
                        lambda cmd, cwd, timeout: (0, "", stderr))
    monkeypatch.setattr(cppcheck, "_parse_text",
                        lambda text, config: text_calls.append(text) or [])
    cppcheck.run(_config())
    return text_calls


def test_xml_with_every_finding_filtered_is_not_reparsed_as_text(
        monkeypatch):
    assert _run_with_stderr(monkeypatch, _XML_ALL_FILTERED) == []


def test_plain_text_output_still_reaches_the_text_parser(monkeypatch):
    text = "[engine/a.cpp:3]: (error) Null pointer dereference\n"
    assert _run_with_stderr(monkeypatch, text) == [text]
