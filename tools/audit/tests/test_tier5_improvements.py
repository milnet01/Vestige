"""Tests for lib.tier5_improvements — TECHNOLOGY_SIGNATURES, detect_technologies, get_improvement_queries."""

from __future__ import annotations

from pathlib import Path
from unittest.mock import patch

import pytest

from lib.config import Config, DEFAULTS, _deep_merge
from lib.tier5_improvements import (
    LANGUAGE_IMPROVEMENT_QUERIES,
    TECHNOLOGY_SIGNATURES,
    TechDetection,
    detect_technologies,
    get_improvement_queries,
)


# ---------------------------------------------------------------------------
# TECHNOLOGY_SIGNATURES format
# ---------------------------------------------------------------------------


class TestTechnologySignaturesFormat:
    """TECHNOLOGY_SIGNATURES entries should have the correct structure."""

    def test_all_entries_are_4_tuples(self):
        for entry in TECHNOLOGY_SIGNATURES:
            assert len(entry) == 4, f"Entry should be (regex, globs, category, query): {entry}"

    def test_regex_is_string(self):
        for regex, _, _, _ in TECHNOLOGY_SIGNATURES:
            assert isinstance(regex, str)

    def test_globs_is_string(self):
        for _, globs, _, _ in TECHNOLOGY_SIGNATURES:
            assert isinstance(globs, str)
            # Should contain at least one glob pattern
            assert "*" in globs or "." in globs

    def test_category_is_nonempty(self):
        for _, _, category, _ in TECHNOLOGY_SIGNATURES:
            assert isinstance(category, str)
            assert len(category) > 0

    def test_query_template_is_string(self):
        for _, _, _, query in TECHNOLOGY_SIGNATURES:
            assert isinstance(query, str)
            assert len(query) > 0

    def test_has_minimum_signatures(self):
        assert len(TECHNOLOGY_SIGNATURES) >= 20

    def test_language_queries_exist(self):
        assert "cpp" in LANGUAGE_IMPROVEMENT_QUERIES
        assert len(LANGUAGE_IMPROVEMENT_QUERIES["cpp"]) >= 1


# ---------------------------------------------------------------------------
# detect_technologies (with mock files)
# ---------------------------------------------------------------------------


class TestDetectTechnologies:
    """detect_technologies should find known patterns in source files."""

    def _make_config(self, tmp_path: Path, files: dict[str, str]) -> Config:
        """Create config with specified source files."""
        src = tmp_path / "src"
        src.mkdir(exist_ok=True)
        for name, content in files.items():
            (src / name).write_text(content)

        raw = _deep_merge(DEFAULTS, {
            "project": {
                "name": "TestProject",
                "root": str(tmp_path),
                "source_dirs": ["src/"],
                "shader_dirs": [],
                "source_extensions": [".cpp", ".h", ".glsl"],
                "exclude_dirs": [],
                "language": "cpp",
            },
        })
        return Config(raw=raw, root=tmp_path)

    def test_detects_pbr(self, tmp_path: Path):
        cfg = self._make_config(tmp_path, {
            "renderer.cpp": "// PBR physically based rendering\nvoid renderPBR() {}",
        })
        detections = detect_technologies(cfg)
        categories = [d.category for d in detections]
        assert any("Rendering" in c for c in categories)

    def test_detects_ssao(self, tmp_path: Path):
        cfg = self._make_config(tmp_path, {
            "effects.cpp": "void applySSAO() { /* ssao pass */ }",
        })
        detections = detect_technologies(cfg)
        queries = [d.query for d in detections]
        assert any("SSAO" in q for q in queries)

    def test_no_detections_for_empty_files(self, tmp_path: Path):
        cfg = self._make_config(tmp_path, {
            "empty.cpp": "int main() { return 0; }",
        })
        detections = detect_technologies(cfg)
        # Should only have language-specific generic queries, no tech detections
        tech_detections = [d for d in detections if d.files_matched > 0]
        assert len(tech_detections) == 0

    def test_deduplicates_queries(self, tmp_path: Path):
        cfg = self._make_config(tmp_path, {
            "a.cpp": "PBR rendering code",
            "b.cpp": "more PBR stuff",
        })
        detections = detect_technologies(cfg)
        queries = [d.query for d in detections]
        # No exact duplicate queries
        assert len(queries) == len(set(queries))

    def test_includes_language_queries(self, tmp_path: Path):
        cfg = self._make_config(tmp_path, {"main.cpp": "int main() {}"})
        detections = detect_technologies(cfg)
        queries = [d.query for d in detections]
        assert any("C++17" in q or "C++20" in q for q in queries)


# ---------------------------------------------------------------------------
# get_improvement_queries
# ---------------------------------------------------------------------------


class TestGetImprovementQueries:
    """get_improvement_queries should return capped query list."""

    def _make_config(self, tmp_path: Path, max_queries: int = 10, enabled: bool = True) -> Config:
        src = tmp_path / "src"
        src.mkdir(exist_ok=True)
        (src / "main.cpp").write_text("PBR SSAO TAA bloom deferred frustum_cull")

        raw = _deep_merge(DEFAULTS, {
            "project": {
                "name": "Test",
                "root": str(tmp_path),
                "source_dirs": ["src/"],
                "shader_dirs": [],
                "source_extensions": [".cpp", ".h"],
                "exclude_dirs": [],
                "language": "cpp",
            },
            "research": {
                "improvements": {
                    "enabled": enabled,
                    "max_queries": max_queries,
                },
            },
        })
        return Config(raw=raw, root=tmp_path)

    def test_returns_queries(self, tmp_path: Path):
        cfg = self._make_config(tmp_path)
        queries = get_improvement_queries(cfg)
        assert len(queries) > 0
        assert all(isinstance(q, str) for q in queries)

    def test_respects_max_queries_cap(self, tmp_path: Path):
        cfg = self._make_config(tmp_path, max_queries=2)
        queries = get_improvement_queries(cfg)
        assert len(queries) <= 2

    def test_disabled_returns_empty(self, tmp_path: Path):
        cfg = self._make_config(tmp_path, enabled=False)
        queries = get_improvement_queries(cfg)
        assert queries == []

    def test_cap_at_zero(self, tmp_path: Path):
        cfg = self._make_config(tmp_path, max_queries=0)
        queries = get_improvement_queries(cfg)
        assert queries == []

    def test_queries_are_strings(self, tmp_path: Path):
        cfg = self._make_config(tmp_path)
        queries = get_improvement_queries(cfg)
        for q in queries:
            assert isinstance(q, str)
            assert len(q) > 0
