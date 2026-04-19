# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tests for lib.tier4_copyright — copyright/SPDX header audit."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import pytest

from lib.config import Config, DEFAULTS, _deep_merge
from lib.tier4_copyright import (
    CopyrightResult,
    analyze_copyright,
    _check_header,
    _COPYRIGHT_RE,
    _SPDX_RE,
)


def _make_config(tmp_path: Path, **overrides) -> Config:
    raw = _deep_merge(DEFAULTS, {
        "project": {
            "name": "test",
            "root": str(tmp_path),
            "source_dirs": ["."],
            "source_extensions": [".cpp", ".h", ".py"],
            "shader_dirs": [],
            "exclude_dirs": [],
        },
        "tier4": _deep_merge(DEFAULTS["tier4"], overrides.get("tier4", {})),
    })
    return Config(raw=raw, root=tmp_path)


def _write(tmp_path: Path, name: str, content: str) -> Path:
    p = tmp_path / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(content)
    return p


# ---------------------------------------------------------------------------
# Regex sanity
# ---------------------------------------------------------------------------

class TestCopyrightRegex:
    def test_matches_cpp_comment(self):
        assert _COPYRIGHT_RE.match("// Copyright (c) 2026 Anthony Schemel")

    def test_matches_python_comment(self):
        assert _COPYRIGHT_RE.match("# Copyright (c) 2026 Anthony Schemel")

    def test_matches_year_range(self):
        assert _COPYRIGHT_RE.match("// Copyright (c) 2024-2026 Anthony Schemel")

    def test_rejects_no_author(self):
        assert not _COPYRIGHT_RE.match("// Copyright (c) 2026")

    def test_spdx_matches(self):
        assert _SPDX_RE.search("// SPDX-License-Identifier: MIT")
        assert _SPDX_RE.search("# SPDX-License-Identifier: Apache-2.0")


# ---------------------------------------------------------------------------
# _check_header
# ---------------------------------------------------------------------------

class TestCheckHeader:
    def test_valid_header_returns_none(self, tmp_path: Path):
        p = _write(tmp_path, "ok.cpp",
                   "// Copyright (c) 2026 Anthony Schemel\n"
                   "// SPDX-License-Identifier: MIT\n"
                   "\nint main() {}\n")
        assert _check_header(p, "ok.cpp") is None

    def test_missing_header_is_flagged(self, tmp_path: Path):
        p = _write(tmp_path, "bad.cpp", "int main() { return 0; }\n")
        result = _check_header(p, "bad.cpp")
        assert result is not None
        assert "Copyright" in result["reason"]

    def test_missing_spdx_is_flagged(self, tmp_path: Path):
        p = _write(tmp_path, "mid.cpp",
                   "// Copyright (c) 2026 Anthony Schemel\n"
                   "\nint main() {}\n")
        result = _check_header(p, "mid.cpp")
        assert result is not None
        assert "SPDX" in result["reason"]

    def test_shebang_offset(self, tmp_path: Path):
        p = _write(tmp_path, "script.py",
                   "#!/usr/bin/env python3\n"
                   "# Copyright (c) 2026 Anthony Schemel\n"
                   "# SPDX-License-Identifier: MIT\n"
                   "print('hi')\n")
        assert _check_header(p, "script.py") is None


# ---------------------------------------------------------------------------
# analyze_copyright (integration)
# ---------------------------------------------------------------------------

class TestAnalyzeCopyright:
    def test_positive_flags_missing_header(self, tmp_path: Path):
        _write(tmp_path, "missing.cpp", "int main() { return 0; }\n")
        config = _make_config(tmp_path)
        result, findings = analyze_copyright(config)
        assert len(result.missing_files) == 1
        assert result.missing_files[0]["file"] == "missing.cpp"
        assert len(findings) == 1
        assert findings[0].pattern_name == "missing_copyright_header"
        assert findings[0].category == "copyright"
        assert findings[0].source_tier == 4

    def test_negative_valid_header_not_flagged(self, tmp_path: Path):
        _write(tmp_path, "good.cpp",
               "// Copyright (c) 2026 Anthony Schemel\n"
               "// SPDX-License-Identifier: MIT\n"
               "\nint main() {}\n")
        config = _make_config(tmp_path)
        result, findings = analyze_copyright(config)
        assert len(result.missing_files) == 0
        assert len(findings) == 0

    def test_shebang_file_with_header_not_flagged(self, tmp_path: Path):
        # False-positive corner case: a script with shebang whose header
        # starts on line 2 must still be recognised as valid.
        _write(tmp_path, "tool.py",
               "#!/usr/bin/env python3\n"
               "# Copyright (c) 2026 Anthony Schemel\n"
               "# SPDX-License-Identifier: MIT\n"
               "print('hello')\n")
        config = _make_config(tmp_path)
        result, findings = analyze_copyright(config)
        assert len(result.missing_files) == 0
        assert len(findings) == 0

    def test_disabled_returns_empty(self, tmp_path: Path):
        _write(tmp_path, "missing.cpp", "int main() {}\n")
        config = _make_config(
            tmp_path,
            tier4={"copyright": {"enabled": False}},
        )
        result, findings = analyze_copyright(config)
        assert result.total_files == 0
        assert len(result.missing_files) == 0
        assert len(findings) == 0


class TestCopyrightResult:
    def test_to_dict_caps_list(self):
        result = CopyrightResult()
        result.missing_files = [{"file": f"x{i}.cpp", "reason": "no header"}
                                for i in range(100)]
        result.total_files = 100
        d = result.to_dict()
        assert len(d["missing_files"]) == 50
        assert d["missing_count"] == 100
        assert d["total_files"] == 100
