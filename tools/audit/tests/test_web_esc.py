# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""The web page's esc() must be safe inside value="..." attributes."""

from __future__ import annotations

import re
import shutil
import subprocess
from pathlib import Path

import pytest

TEMPLATE = Path(__file__).resolve().parent.parent / "web" / "templates" / "index.html"

# A stand-in for the browser: textContent then innerHTML escapes & < > only,
# which is what a real element does.
HARNESS = """
const document = { createElement: () => ({
  set textContent(v) { this._t = String(v); },
  get innerHTML() {
    return this._t.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  } }) };
%s
process.stdout.write(esc(process.argv[1]));
"""


def _esc_source() -> str:
    text = TEMPLATE.read_text(encoding="utf-8")
    m = re.search(r"function esc\(s\)\{.*?\n(?:\}|(?=function ))", text, re.S)
    if m is None:
        m = re.search(r"function esc\(s\)\{[^\n]*\}", text)
    assert m is not None
    return m.group(0)


@pytest.mark.skipif(shutil.which("node") is None, reason="node not installed")
def test_esc_escapes_quotes():
    payload = '" onfocus="alert(1)" x=\'y'
    out = subprocess.run(
        ["node", "-e", HARNESS % _esc_source(), "--", payload],
        capture_output=True, text=True, check=True,
    ).stdout
    assert '"' not in out
    assert "'" not in out
