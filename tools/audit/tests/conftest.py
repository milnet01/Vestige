# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Shared fixtures for the audit tool test suite."""

from __future__ import annotations

import sys
from pathlib import Path

# Ensure the lib package is importable via `from lib.xxx import ...`
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import pytest

from lib.findings import Finding, Severity


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------


@pytest.fixture()
def sample_config(tmp_path: Path):
    """Create a minimal Config pointing at tmp_path with a small source tree."""
    from lib.config import Config, DEFAULTS, _deep_merge

    # Create a tiny source directory with one .cpp file
    src_dir = tmp_path / "src"
    src_dir.mkdir()
    (src_dir / "main.cpp").write_text(
        '#include <iostream>\n'
        'int main() {\n'
        '    int* p = new int(42); // raw new\n'
        '    // TODO: fix this\n'
        '    return 0;\n'
        '}\n'
    )

    raw = _deep_merge(DEFAULTS, {
        "project": {
            "name": "TestProject",
            "root": str(tmp_path),
            "source_dirs": ["src/"],
            "source_extensions": [".cpp", ".h"],
            "exclude_dirs": [],
        },
        "tiers": [1, 2, 3, 4, 5],
        "patterns": {
            "memory": [
                {
                    "name": "raw_new",
                    "pattern": r"\bnew\b",
                    "file_glob": "*.cpp",
                    "severity": "high",
                    "description": "Raw new detected",
                    "skip_comments": False,
                },
            ],
            "maintenance": [
                {
                    "name": "todo",
                    "pattern": r"TODO",
                    "file_glob": "*.cpp,*.h",
                    "severity": "info",
                    "description": "TODO marker found",
                    "skip_comments": False,
                },
            ],
        },
        "report": {
            "output_path": "docs/AUTOMATED_AUDIT_REPORT.md",
            "max_findings_per_category": 100,
            "include_json_blocks": True,
            "include_token_estimate": True,
        },
    })

    return Config(raw=raw, root=tmp_path)


@pytest.fixture()
def sample_findings() -> list[Finding]:
    """Return a list of Finding objects across multiple severities and tiers."""
    return [
        Finding(
            file="src/main.cpp",
            line=10,
            severity=Severity.CRITICAL,
            category="memory",
            source_tier=1,
            title="Use-after-free detected",
            detail="ptr used after delete",
            pattern_name="use_after_free",
        ),
        Finding(
            file="src/main.cpp",
            line=20,
            severity=Severity.HIGH,
            category="cppcheck",
            source_tier=1,
            title="Buffer overrun",
            detail="Array index out of bounds",
            pattern_name="buffer_overrun",
        ),
        Finding(
            file="src/render.cpp",
            line=55,
            severity=Severity.MEDIUM,
            category="performance",
            source_tier=2,
            title="Inefficient loop",
            detail="Consider using range-based for",
            pattern_name="slow_loop",
        ),
        Finding(
            file="src/render.cpp",
            line=100,
            severity=Severity.LOW,
            category="style",
            source_tier=2,
            title="Magic number",
            detail="Use named constant instead of 3.14",
            pattern_name="magic_number",
        ),
        Finding(
            file="src/utils.h",
            line=5,
            severity=Severity.INFO,
            category="maintenance",
            source_tier=2,
            title="TODO marker found",
            detail="// TODO: refactor",
            pattern_name="todo",
        ),
    ]


@pytest.fixture()
def sample_cpp_file(tmp_path: Path) -> Path:
    """Write a small C++ file with known patterns (raw new, TODO, etc.)."""
    cpp = tmp_path / "test_sample.cpp"
    cpp.write_text(
        '#include <vector>\n'
        '\n'
        '// TODO: remove raw allocation\n'
        'void leaky() {\n'
        '    int* p = new int(7);\n'
        '    /* This is a\n'
        '       block comment with new inside */\n'
        '    delete p;\n'
        '}\n'
        '\n'
        '// This line has new but is a comment\n'
        'void ok() {\n'
        '    std::vector<int> v;\n'
        '}\n'
    )
    return cpp


@pytest.fixture()
def sample_header_file(tmp_path: Path) -> Path:
    """Write a header with a GLuint member for Rule-of-Five testing."""
    hdr = tmp_path / "gpu_resource.h"
    hdr.write_text(
        '#pragma once\n'
        '#include <GL/gl.h>\n'
        '\n'
        'class Framebuffer {\n'
        'public:\n'
        '    Framebuffer() = default;\n'
        '    ~Framebuffer();\n'
        '    Framebuffer(Framebuffer&& other) noexcept;\n'
        '    Framebuffer& operator=(Framebuffer&& other) noexcept;\n'
        '    Framebuffer(const Framebuffer&) = delete;\n'
        '    Framebuffer& operator=(const Framebuffer&) = delete;\n'
        '\n'
        'private:\n'
        '    GLuint m_fbo = 0;\n'
        '    GLuint m_texture = 0;\n'
        '};\n'
    )
    return hdr
