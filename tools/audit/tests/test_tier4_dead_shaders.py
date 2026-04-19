# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tests for lib.tier4_dead_shaders — unreferenced GLSL shader detection.

The load-bearing FP test is ``test_computed_path_substring_is_not_dead``
— the 2026-04-19 manual audit incorrectly flagged ``ssr.frag.glsl`` as
dead because the only reference was via ``shaderDir + "ssr.frag"``. The
detector must accept the basename-minus-extension as a valid reference.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import pytest

from lib.config import Config, DEFAULTS, _deep_merge
from lib.tier4_dead_shaders import (
    DeadShaderResult,
    analyze_dead_shaders,
    _shader_candidate_tokens,
)


def _make_config(tmp_path: Path, **overrides) -> Config:
    raw = _deep_merge(DEFAULTS, {
        "project": {
            "name": "test",
            "root": str(tmp_path),
            "source_dirs": ["engine/"],
            "shader_dirs": ["assets/shaders/"],
            "source_extensions": [".cpp", ".h"],
            "exclude_dirs": [],
        },
        "tier4": _deep_merge(DEFAULTS["tier4"], overrides.get("tier4", {})),
    })
    return Config(raw=raw, root=tmp_path)


def _write(tmp_path: Path, rel: str, content: str) -> Path:
    p = tmp_path / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(content)
    return p


# ---------------------------------------------------------------------------
# _shader_candidate_tokens
# ---------------------------------------------------------------------------

class TestCandidateTokens:
    def test_double_suffix(self):
        p = Path("assets/shaders/ssr.frag.glsl")
        tokens = _shader_candidate_tokens(p)
        assert tokens == ["ssr.frag.glsl", "ssr.frag", "ssr"]

    def test_single_suffix(self):
        p = Path("assets/shaders/basic.glsl")
        tokens = _shader_candidate_tokens(p)
        assert tokens == ["basic.glsl", "basic"]


# ---------------------------------------------------------------------------
# analyze_dead_shaders (integration)
# ---------------------------------------------------------------------------

class TestAnalyzeDeadShaders:
    def test_positive_unused_shader_flagged(self, tmp_path: Path):
        _write(tmp_path, "assets/shaders/ghost.glsl",
               "// fragment shader\nvoid main() {}\n")
        _write(tmp_path, "engine/renderer/renderer.cpp",
               'int main() { return 0; }\n')
        config = _make_config(tmp_path)
        result, findings = analyze_dead_shaders(config)
        assert len(result.dead_shaders) == 1
        assert result.dead_shaders[0]["shader_name"] == "ghost.glsl"
        assert len(findings) == 1
        assert findings[0].pattern_name == "dead_shader"
        assert findings[0].category == "dead_code"
        assert findings[0].source_tier == 4

    def test_negative_referenced_shader_not_flagged(self, tmp_path: Path):
        _write(tmp_path, "assets/shaders/basic.glsl",
               "// fragment shader\n")
        _write(tmp_path, "engine/renderer/renderer.cpp",
               'auto s = loadShader("basic.glsl");\n')
        config = _make_config(tmp_path)
        result, findings = analyze_dead_shaders(config)
        assert len(result.dead_shaders) == 0
        assert len(findings) == 0

    def test_computed_path_substring_is_not_dead(self, tmp_path):
        # Shader referenced only via runtime path concatenation should not
        # be flagged as dead — the basename (minus extension) must still
        # appear as a substring somewhere.
        shader_dir = tmp_path / "assets" / "shaders"
        shader_dir.mkdir(parents=True)
        (shader_dir / "ssr.frag.glsl").write_text("// fragment shader\n")
        src_dir = tmp_path / "engine" / "renderer"
        src_dir.mkdir(parents=True)
        (src_dir / "ssr.cpp").write_text(
            'std::string path = shaderDir + "ssr.frag";\n'
        )
        config = _make_config(tmp_path)
        result, findings = analyze_dead_shaders(config)
        # ssr.frag.glsl must NOT be flagged — "ssr.frag" appears in the cpp.
        assert len(result.dead_shaders) == 0, (
            f"FP: ssr.frag.glsl was flagged despite "
            f"'ssr.frag' appearing in source. Flagged: {result.dead_shaders}"
        )
        assert len(findings) == 0

    def test_disabled_returns_empty(self, tmp_path: Path):
        _write(tmp_path, "assets/shaders/ghost.glsl", "// unused\n")
        _write(tmp_path, "engine/renderer/renderer.cpp", "int main() {}\n")
        config = _make_config(
            tmp_path,
            tier4={"dead_shaders": {"enabled": False}},
        )
        result, findings = analyze_dead_shaders(config)
        assert len(result.dead_shaders) == 0
        assert len(findings) == 0
        assert result.total_shaders == 0

    def test_basename_stem_substring_match(self, tmp_path: Path):
        # A reference by bare stem (no extension at all) still counts —
        # e.g. shader "post_bloom.glsl" referenced as "post_bloom" in a
        # registration table.
        _write(tmp_path, "assets/shaders/post_bloom.glsl", "// bloom\n")
        _write(tmp_path, "engine/renderer/effects.cpp",
               'registerEffect("post_bloom");\n')
        config = _make_config(tmp_path)
        result, _ = analyze_dead_shaders(config)
        assert len(result.dead_shaders) == 0


class TestDeadShaderResult:
    def test_to_dict_caps_list(self):
        result = DeadShaderResult()
        result.dead_shaders = [
            {"file": f"s{i}.glsl", "shader_name": f"s{i}.glsl",
             "checked_tokens": [f"s{i}.glsl", f"s{i}"]}
            for i in range(50)
        ]
        result.total_shaders = 50
        d = result.to_dict()
        assert len(d["dead_shaders"]) == 30
        assert d["dead_count"] == 50
        assert d["total_shaders"] == 50
