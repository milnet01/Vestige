"""Tests for lib.tier5_nvd — CVEResult, query_nvd, run_nvd_queries."""

from __future__ import annotations

import json
import os
import urllib.error
from datetime import datetime, timedelta
from pathlib import Path
from unittest.mock import MagicMock, patch

import pytest

from lib.findings import ResearchResult
from lib.tier5_nvd import (
    CVEResult,
    NVD_API_URL,
    _resolve_api_key,
    _validate_api_key,
    query_nvd,
    run_nvd_queries,
)


# ---------------------------------------------------------------------------
# CVEResult.to_dict
# ---------------------------------------------------------------------------


class TestCVEResultToDict:
    """CVEResult.to_dict should format as expected."""

    def test_basic_to_dict(self):
        cve = CVEResult(
            cve_id="CVE-2024-1234",
            description="A vulnerability in FooLib",
            published="2024-03-15",
            base_score=7.5,
            severity="HIGH",
        )
        d = cve.to_dict()
        assert "CVE-2024-1234" in d["title"]
        assert "7.5" in d["title"]
        assert "HIGH" in d["title"]
        assert d["url"] == "https://nvd.nist.gov/vuln/detail/CVE-2024-1234"
        assert "A vulnerability" in d["snippet"]

    def test_no_score(self):
        cve = CVEResult(cve_id="CVE-2024-0001", description="Desc", base_score=None)
        d = cve.to_dict()
        assert "N/A" in d["title"]

    def test_long_description_truncated(self):
        cve = CVEResult(cve_id="CVE-2024-0001", description="A" * 500)
        d = cve.to_dict()
        assert len(d["snippet"]) <= 200

    def test_empty_cve(self):
        cve = CVEResult()
        d = cve.to_dict()
        assert "url" in d
        assert "title" in d
        assert "snippet" in d

    def test_url_format(self):
        cve = CVEResult(cve_id="CVE-2025-9999")
        d = cve.to_dict()
        assert d["url"].endswith("CVE-2025-9999")


# ---------------------------------------------------------------------------
# _resolve_api_key
# ---------------------------------------------------------------------------


class TestResolveApiKey:
    """_resolve_api_key should check config first, then environment.

    Keys must match `[A-Za-z0-9-]{16,64}` (AUDIT.md §H4) to prevent CRLF
    header injection; real NVD keys are UUID-shaped. Tests use realistic
    UUIDs so the shape-validator doesn't reject them.
    """

    CONFIG_KEY = "11111111-2222-3333-4444-555555555555"
    ENV_KEY = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"
    CUSTOM_ENV_KEY = "99999999-8888-7777-6666-555555555555"

    def test_config_key_takes_priority(self):
        nvd_config = {"api_key": self.CONFIG_KEY, "api_key_env": "NVD_API_KEY"}
        with patch.dict(os.environ, {"NVD_API_KEY": self.ENV_KEY}):
            result = _resolve_api_key(nvd_config)
        assert result == self.CONFIG_KEY

    def test_falls_back_to_env(self):
        nvd_config = {"api_key": None, "api_key_env": "NVD_API_KEY"}
        with patch.dict(os.environ, {"NVD_API_KEY": self.ENV_KEY}):
            result = _resolve_api_key(nvd_config)
        assert result == self.ENV_KEY

    def test_custom_env_var_name(self):
        nvd_config = {"api_key": None, "api_key_env": "CUSTOM_NVD_KEY"}
        with patch.dict(os.environ, {"CUSTOM_NVD_KEY": self.CUSTOM_ENV_KEY}):
            result = _resolve_api_key(nvd_config)
        assert result == self.CUSTOM_ENV_KEY

    def test_no_key_available(self):
        nvd_config = {"api_key": None, "api_key_env": "NVD_API_KEY"}
        with patch.dict(os.environ, {}, clear=True):
            result = _resolve_api_key(nvd_config)
        assert result is None

    def test_empty_config_key_falls_back(self):
        nvd_config = {"api_key": "", "api_key_env": "NVD_API_KEY"}
        with patch.dict(os.environ, {"NVD_API_KEY": self.ENV_KEY}):
            result = _resolve_api_key(nvd_config)
        # Empty string is falsy, so should fall back
        assert result == self.ENV_KEY

    def test_crlf_injection_rejected(self):
        """Keys with \\r\\n must be rejected (AUDIT.md §H4)."""
        nvd_config = {"api_key": None, "api_key_env": "NVD_API_KEY"}
        with patch.dict(os.environ, {"NVD_API_KEY": "foo\r\nX-Admin: 1"}):
            result = _resolve_api_key(nvd_config)
        assert result is None

    def test_too_short_key_rejected(self):
        """A <16-char key is rejected as implausibly short."""
        nvd_config = {"api_key": None, "api_key_env": "NVD_API_KEY"}
        with patch.dict(os.environ, {"NVD_API_KEY": "short"}):
            result = _resolve_api_key(nvd_config)
        assert result is None

    def test_space_in_key_rejected(self):
        """Spaces must be rejected — not in the allowed character set."""
        nvd_config = {"api_key": None, "api_key_env": "NVD_API_KEY"}
        with patch.dict(os.environ, {"NVD_API_KEY": "11111111 22222222 33333333"}):
            result = _resolve_api_key(nvd_config)
        assert result is None


# ---------------------------------------------------------------------------
# query_nvd (mocked urllib)
# ---------------------------------------------------------------------------


class TestQueryNvd:
    """query_nvd should parse NVD API responses."""

    def _mock_response(self, data: dict) -> MagicMock:
        """Create a mock urlopen response."""
        response = MagicMock()
        response.read.return_value = json.dumps(data).encode()
        response.__enter__ = lambda s: s
        response.__exit__ = MagicMock(return_value=False)
        return response

    def test_successful_query(self):
        api_data = {
            "vulnerabilities": [
                {
                    "cve": {
                        "id": "CVE-2024-1234",
                        "descriptions": [{"lang": "en", "value": "Test vuln"}],
                        "metrics": {
                            "cvssMetricV31": [
                                {"cvssData": {"baseScore": 7.5, "baseSeverity": "HIGH"}}
                            ]
                        },
                        "published": "2024-03-15T00:00:00.000",
                    }
                }
            ]
        }
        with patch("lib.tier5_nvd.urllib.request.urlopen", return_value=self._mock_response(api_data)):
            results = query_nvd("opengl", max_results=5)

        assert len(results) == 1
        assert results[0].cve_id == "CVE-2024-1234"
        assert results[0].base_score == 7.5
        assert results[0].severity == "HIGH"

    def test_http_429_returns_empty(self):
        error = urllib.error.HTTPError(
            url="http://test", code=429, msg="Rate limited",
            hdrs=None, fp=None,  # type: ignore[arg-type]
        )
        with patch("lib.tier5_nvd.urllib.request.urlopen", side_effect=error):
            results = query_nvd("opengl")
        assert results == []

    def test_timeout_returns_empty(self):
        with patch("lib.tier5_nvd.urllib.request.urlopen",
                   side_effect=urllib.error.URLError("timeout")):
            results = query_nvd("opengl")
        assert results == []

    def test_api_key_added_to_header(self):
        with patch("lib.tier5_nvd.urllib.request.urlopen",
                   return_value=self._mock_response({"vulnerabilities": []})) as mock_open:
            query_nvd("opengl", api_key="test_key_123")

        call_args = mock_open.call_args
        req = call_args[0][0]
        assert req.get_header("Apikey") == "test_key_123"

    def test_empty_vulnerabilities(self):
        with patch("lib.tier5_nvd.urllib.request.urlopen",
                   return_value=self._mock_response({"vulnerabilities": []})):
            results = query_nvd("opengl")
        assert results == []


# ---------------------------------------------------------------------------
# run_nvd_queries (with cache)
# ---------------------------------------------------------------------------


class TestRunNvdQueriesCacheHits:
    """run_nvd_queries should use cached results when available."""

    def test_disabled_returns_empty(self, tmp_path: Path):
        config = {"nvd": {"enabled": False}}
        results = run_nvd_queries(config, tmp_path, timedelta(days=7))
        assert results == []

    def test_no_dependencies_returns_empty(self, tmp_path: Path):
        config = {
            "nvd": {"enabled": True, "api_key": None, "api_key_env": "NVD_MISSING", "dependencies": []},
        }
        with patch.dict(os.environ, {}, clear=True):
            results = run_nvd_queries(config, tmp_path, timedelta(days=7))
        assert results == []

    def test_cache_hit_avoids_api_call(self, tmp_path: Path):
        import hashlib
        dep_name = "opengl"
        cache_hash = hashlib.sha256(dep_name.encode()).hexdigest()[:16]
        cache_file = tmp_path / f"nvd_{cache_hash}.json"
        cache_data = {
            "query": dep_name,
            "timestamp": datetime.now().isoformat(),
            "results": [{"title": "cached result"}],
        }
        cache_file.write_text(json.dumps(cache_data))

        config = {
            "nvd": {"enabled": True, "api_key": None, "api_key_env": "NVD_MISSING", "dependencies": [dep_name]},
            "max_results_per_query": 5,
        }

        with patch.dict(os.environ, {}, clear=True), \
             patch("lib.tier5_nvd.query_nvd") as mock_query, \
             patch("lib.tier5_nvd._validate_api_key"):
            results = run_nvd_queries(config, tmp_path, timedelta(days=7))

        mock_query.assert_not_called()
        assert len(results) == 1
        assert results[0].cached is True

    def test_expired_cache_triggers_api_call(self, tmp_path: Path):
        import hashlib
        dep_name = "glfw"
        cache_hash = hashlib.sha256(dep_name.encode()).hexdigest()[:16]
        cache_file = tmp_path / f"nvd_{cache_hash}.json"
        # Old timestamp
        old_time = datetime(2020, 1, 1)
        cache_data = {
            "query": dep_name,
            "timestamp": old_time.isoformat(),
            "results": [{"title": "old"}],
        }
        cache_file.write_text(json.dumps(cache_data))

        config = {
            "nvd": {"enabled": True, "api_key": None, "api_key_env": "NVD_MISSING", "dependencies": [dep_name]},
            "max_results_per_query": 5,
        }

        with patch.dict(os.environ, {}, clear=True), \
             patch("lib.tier5_nvd.query_nvd", return_value=[]) as mock_query, \
             patch("lib.tier5_nvd.time.sleep"), \
             patch("lib.tier5_nvd._validate_api_key"):
            results = run_nvd_queries(config, tmp_path, timedelta(days=7))

        mock_query.assert_called_once()
