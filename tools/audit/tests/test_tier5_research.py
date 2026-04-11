"""Tests for lib.tier5_research — _cache_key, _search_with_cache, run()."""

from __future__ import annotations

import json
from datetime import datetime, timedelta
from pathlib import Path
from unittest.mock import MagicMock, patch

import pytest

from lib.config import Config, DEFAULTS, _deep_merge
from lib.tier5_research import _cache_key, _search_with_cache


# ---------------------------------------------------------------------------
# _cache_key
# ---------------------------------------------------------------------------


class TestCacheKey:
    """_cache_key should produce deterministic hex strings."""

    def test_deterministic(self):
        assert _cache_key("opengl pbr") == _cache_key("opengl pbr")

    def test_different_queries_different_keys(self):
        assert _cache_key("query A") != _cache_key("query B")

    def test_returns_hex_string(self):
        key = _cache_key("test query")
        assert len(key) == 16
        int(key, 16)  # Should be valid hex

    def test_empty_string(self):
        key = _cache_key("")
        assert len(key) == 16

    def test_unicode_query(self):
        key = _cache_key("query with unicode: \u00e9\u00e8\u00ea")
        assert len(key) == 16


# ---------------------------------------------------------------------------
# _search_with_cache — cache hit
# ---------------------------------------------------------------------------


class TestSearchWithCacheHit:
    """_search_with_cache should return cached results when valid."""

    def test_cache_hit_returns_cached(self, tmp_path: Path):
        query = "test query"
        cache_file = tmp_path / f"{_cache_key(query)}.json"
        cache_data = {
            "query": query,
            "timestamp": datetime.now().isoformat(),
            "results": [{"title": "Cached Result", "url": "http://example.com", "snippet": "cached"}],
        }
        cache_file.write_text(json.dumps(cache_data))

        result = _search_with_cache(query, tmp_path, timedelta(days=7), max_results=3)
        assert result.cached is True
        assert len(result.results) == 1
        assert result.results[0]["title"] == "Cached Result"

    def test_cache_hit_does_not_call_ddgs(self, tmp_path: Path):
        query = "test query"
        cache_file = tmp_path / f"{_cache_key(query)}.json"
        cache_data = {
            "query": query,
            "timestamp": datetime.now().isoformat(),
            "results": [{"title": "X"}],
        }
        cache_file.write_text(json.dumps(cache_data))

        with patch("lib.tier5_research.DDGS") as mock_ddgs:
            _search_with_cache(query, tmp_path, timedelta(days=7), max_results=3)
        mock_ddgs.assert_not_called()


# ---------------------------------------------------------------------------
# _search_with_cache — cache miss
# ---------------------------------------------------------------------------


class TestSearchWithCacheMiss:
    """_search_with_cache should call DDGS when cache is empty or expired."""

    def test_no_cache_calls_ddgs(self, tmp_path: Path):
        mock_ddgs_instance = MagicMock()
        mock_ddgs_instance.text.return_value = [
            {"title": "Result 1", "href": "http://example.com", "body": "Body text"},
        ]

        with patch("lib.tier5_research.DDGS", return_value=mock_ddgs_instance):
            result = _search_with_cache("fresh query", tmp_path, timedelta(days=7), max_results=3)

        assert result.cached is False
        assert len(result.results) == 1
        assert result.results[0]["title"] == "Result 1"

    def test_writes_cache_file(self, tmp_path: Path):
        mock_ddgs_instance = MagicMock()
        mock_ddgs_instance.text.return_value = [
            {"title": "Result", "href": "http://example.com", "body": "Snippet"},
        ]

        query = "cache write test"
        with patch("lib.tier5_research.DDGS", return_value=mock_ddgs_instance):
            _search_with_cache(query, tmp_path, timedelta(days=7), max_results=3)

        cache_file = tmp_path / f"{_cache_key(query)}.json"
        assert cache_file.exists()
        data = json.loads(cache_file.read_text())
        assert data["query"] == query
        assert "timestamp" in data


# ---------------------------------------------------------------------------
# _search_with_cache — cache expired
# ---------------------------------------------------------------------------


class TestSearchWithCacheExpired:
    """_search_with_cache should refetch when cache TTL has expired."""

    def test_expired_cache_calls_ddgs(self, tmp_path: Path):
        query = "expired query"
        cache_file = tmp_path / f"{_cache_key(query)}.json"
        old_time = datetime(2020, 1, 1)
        cache_data = {
            "query": query,
            "timestamp": old_time.isoformat(),
            "results": [{"title": "Old Result"}],
        }
        cache_file.write_text(json.dumps(cache_data))

        mock_ddgs_instance = MagicMock()
        mock_ddgs_instance.text.return_value = [
            {"title": "Fresh Result", "href": "http://new.com", "body": "New"},
        ]

        with patch("lib.tier5_research.DDGS", return_value=mock_ddgs_instance):
            result = _search_with_cache(query, tmp_path, timedelta(days=7), max_results=3)

        assert result.cached is False
        assert result.results[0]["title"] == "Fresh Result"

    def test_corrupt_cache_triggers_refetch(self, tmp_path: Path):
        query = "corrupt cache query"
        cache_file = tmp_path / f"{_cache_key(query)}.json"
        cache_file.write_text("{invalid json")

        mock_ddgs_instance = MagicMock()
        mock_ddgs_instance.text.return_value = []

        with patch("lib.tier5_research.DDGS", return_value=mock_ddgs_instance):
            result = _search_with_cache(query, tmp_path, timedelta(days=7), max_results=3)

        assert result.cached is False


# ---------------------------------------------------------------------------
# _search_with_cache — error handling
# ---------------------------------------------------------------------------


class TestSearchWithCacheErrors:
    """_search_with_cache should handle DDGS errors gracefully."""

    def test_ddgs_exception_returns_error_result(self, tmp_path: Path):
        with patch("lib.tier5_research.DDGS", side_effect=Exception("Network error")):
            result = _search_with_cache("error query", tmp_path, timedelta(days=7), max_results=3)

        assert result.error != ""
        assert "Network error" in result.error


# ---------------------------------------------------------------------------
# run() with mocked DDGS
# ---------------------------------------------------------------------------


class TestTier5ResearchRun:
    """run() should execute research queries based on config."""

    def _make_config(self, tmp_path: Path, enabled: bool = True, topics: list | None = None) -> Config:
        raw = _deep_merge(DEFAULTS, {
            "project": {"name": "Test", "root": str(tmp_path), "source_dirs": ["src/"]},
            "research": {
                "enabled": enabled,
                "cache_dir": str(tmp_path / ".cache"),
                "cache_ttl_days": 7,
                "max_results_per_query": 3,
                "topics": topics or [],
                "custom_queries": [],
            },
        })
        return Config(raw=raw, root=tmp_path)

    def test_disabled_returns_empty(self, tmp_path: Path):
        from lib.tier5_research import run
        cfg = self._make_config(tmp_path, enabled=False)
        results = run(cfg)
        assert results == []

    def test_no_queries_returns_empty(self, tmp_path: Path):
        from lib.tier5_research import run
        src = tmp_path / "src"
        src.mkdir()
        cfg = self._make_config(tmp_path, enabled=True, topics=[])
        with patch("lib.tier5_research.HAS_DDGS", True), \
             patch("lib.tier5_improvements.get_improvement_queries", return_value=[]):
            results = run(cfg)
        assert results == []
