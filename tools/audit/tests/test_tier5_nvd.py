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
    _extract_cpe_version,
    _resolve_api_key,
    _validate_api_key,
    cve_affects_version,
    parse_semver,
    query_nvd,
    run_nvd_queries,
    version_in_range,
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


# ---------------------------------------------------------------------------
# D6 — version parsing
# ---------------------------------------------------------------------------


class TestParseSemver:
    """parse_semver must handle the FetchContent_Declare GIT_TAG formats."""

    @pytest.mark.parametrize(
        "inp,expected",
        [
            ("3.4",         (3, 4)),
            ("v2.9.4",      (2, 9, 4)),
            ("1.24.1",      (1, 24, 1)),
            ("VER-2-13-3",  (2, 13, 3)),   # FreeType convention
            ("3.12.0",      (3, 12, 0)),
            ("V5.2.0",      (5, 2, 0)),    # uppercase V
            ("0.9.3",       (0, 9, 3)),
            ("10.0.0",      (10, 0, 0)),   # multi-digit major
        ],
    )
    def test_parses_known_formats(self, inp, expected):
        assert parse_semver(inp) == expected

    @pytest.mark.parametrize("inp", ["", None, "master", "docking", "HEAD"])
    def test_unparseable_returns_none(self, inp):
        assert parse_semver(inp) is None

    def test_trailing_suffix_dropped(self):
        """Pre-release tags keep the numeric prefix."""
        # "1.0.0-rc1" → (1, 0, 0, 1) because we extract all digit runs.
        # That's fine for tuple comparison; 1.0.0 < 1.0.0-rc1 tuple-wise.
        result = parse_semver("1.0.0-rc1")
        assert result is not None
        assert result[:3] == (1, 0, 0)


class TestVersionInRange:
    """version_in_range mirrors NVD's four-bound cpeMatch shape."""

    def test_within_start_including_end_excluding(self):
        # [3.0.0, 3.3.7) — 3.3.0 is in range, 3.3.7 is not.
        assert version_in_range((3, 3, 0), start_including="3.0.0", end_excluding="3.3.7")
        assert not version_in_range((3, 3, 7), start_including="3.0.0", end_excluding="3.3.7")

    def test_start_excluding_boundary(self):
        # (3.0.0, 3.3.0] — 3.0.0 itself excluded.
        assert not version_in_range((3, 0, 0), start_excluding="3.0.0", end_including="3.3.0")
        assert version_in_range((3, 0, 1), start_excluding="3.0.0", end_including="3.3.0")

    def test_end_including_boundary(self):
        # [1.0.0, 1.2.3] — 1.2.3 included.
        assert version_in_range((1, 2, 3), start_including="1.0.0", end_including="1.2.3")
        assert not version_in_range((1, 2, 4), start_including="1.0.0", end_including="1.2.3")

    def test_below_start_returns_false(self):
        assert not version_in_range((2, 9, 9), start_including="3.0.0")

    def test_above_end_returns_false(self):
        assert not version_in_range((3, 4, 0), end_excluding="3.4.0")

    def test_exact_match_when_no_range(self):
        """If no range bounds, fall through to exact-pin comparison."""
        assert version_in_range((3, 4), exact="3.4")
        assert not version_in_range((3, 4), exact="3.3")

    def test_no_bounds_no_exact_returns_false(self):
        """Defensive: caller treats as unknown/no-match."""
        assert not version_in_range((3, 4))

    def test_unparseable_bound_ignored(self):
        """Unparseable bounds default to "no bound on that side"."""
        # end_excluding="garbage" → ignored, so only start bound checked.
        assert version_in_range((99, 0), start_including="3.0.0", end_excluding="garbage")


class TestExtractCpeVersion:
    """CPE 2.3 URIs have a fixed 13-field structure; we need field 5."""

    def test_extracts_version_field(self):
        cpe = "cpe:2.3:a:glfw:glfw:3.3.0:*:*:*:*:*:*:*"
        assert _extract_cpe_version(cpe) == "3.3.0"

    def test_wildcard_version(self):
        cpe = "cpe:2.3:a:vendor:product:*:*:*:*:*:*:*:*"
        assert _extract_cpe_version(cpe) == "*"

    def test_short_string_returns_empty(self):
        assert _extract_cpe_version("cpe:2.3:a") == ""

    def test_empty_returns_empty(self):
        assert _extract_cpe_version("") == ""


# ---------------------------------------------------------------------------
# D6 — cve_affects_version
# ---------------------------------------------------------------------------


class TestCveAffectsVersion:
    """cve_affects_version tri-state returns True / False / None."""

    def _cve_with_range(self, start_incl: str, end_excl: str) -> dict:
        return {
            "configurations": [{
                "nodes": [{
                    "operator": "OR",
                    "negate": False,
                    "cpeMatch": [{
                        "vulnerable": True,
                        "criteria": "cpe:2.3:a:glfw:glfw:*:*:*:*:*:*:*:*",
                        "versionStartIncluding": start_incl,
                        "versionEndExcluding": end_excl,
                    }],
                }],
            }],
        }

    def test_pinned_in_range_returns_true(self):
        cve = self._cve_with_range("3.0.0", "3.3.7")
        assert cve_affects_version(cve, "3.3.0") is True

    def test_pinned_above_range_returns_false(self):
        cve = self._cve_with_range("3.0.0", "3.3.7")
        assert cve_affects_version(cve, "3.4") is False

    def test_pinned_below_range_returns_false(self):
        cve = self._cve_with_range("3.0.0", "3.3.7")
        assert cve_affects_version(cve, "2.9.0") is False

    def test_no_pinned_version_returns_none(self):
        cve = self._cve_with_range("3.0.0", "3.3.7")
        assert cve_affects_version(cve, None) is None

    def test_unparseable_pinned_returns_none(self):
        cve = self._cve_with_range("3.0.0", "3.3.7")
        assert cve_affects_version(cve, "master") is None

    def test_no_configurations_returns_none(self):
        cve = {"id": "CVE-2024-0001"}
        assert cve_affects_version(cve, "3.4") is None

    def test_non_vulnerable_cpe_ignored(self):
        """cpeMatch entries with vulnerable=False are runtime-env notes."""
        cve = {
            "configurations": [{
                "nodes": [{
                    "cpeMatch": [{
                        "vulnerable": False,
                        "criteria": "cpe:2.3:o:linux:kernel:*:*:*:*:*:*:*:*",
                        "versionStartIncluding": "3.0.0",
                        "versionEndExcluding": "99.0.0",
                    }],
                }],
            }],
        }
        # No vulnerable entries → no affirmative match, return None
        # (configurations present but no vulnerable range examined).
        assert cve_affects_version(cve, "3.4") is None

    def test_exact_cpe_version_match(self):
        """A CPE with a pinned version (no range bounds) requires exact match."""
        cve = {
            "configurations": [{
                "nodes": [{
                    "cpeMatch": [{
                        "vulnerable": True,
                        "criteria": "cpe:2.3:a:glfw:glfw:3.3.0:*:*:*:*:*:*:*",
                    }],
                }],
            }],
        }
        assert cve_affects_version(cve, "3.3.0") is True
        assert cve_affects_version(cve, "3.4") is False

    def test_multiple_nodes_any_match_wins(self):
        """If any vulnerable cpeMatch hits, affects_pinned is True."""
        cve = {
            "configurations": [{
                "nodes": [{
                    "cpeMatch": [
                        {
                            "vulnerable": True,
                            "criteria": "cpe:2.3:a:glfw:glfw:*:*:*:*:*:*:*:*",
                            "versionStartIncluding": "1.0.0",
                            "versionEndExcluding": "2.0.0",
                        },
                        {
                            "vulnerable": True,
                            "criteria": "cpe:2.3:a:glfw:glfw:*:*:*:*:*:*:*:*",
                            "versionStartIncluding": "3.0.0",
                            "versionEndExcluding": "3.5.0",
                        },
                    ],
                }],
            }],
        }
        assert cve_affects_version(cve, "3.4") is True


# ---------------------------------------------------------------------------
# D6 — CVEResult title prefix + sort
# ---------------------------------------------------------------------------


class TestCVEResultAffectsPinnedTag:
    """CVEResult.to_dict surfaces the affects_pinned tag in the title."""

    def test_affects_pinned_true_prefix(self):
        cve = CVEResult(
            cve_id="CVE-2024-1234",
            base_score=7.5, severity="HIGH",
            affects_pinned=True, pinned_version="3.4",
        )
        d = cve.to_dict()
        assert d["title"].startswith("[AFFECTS PINNED 3.4]")
        assert d["affects_pinned"] is True
        assert d["pinned_version"] == "3.4"

    def test_affects_pinned_false_prefix(self):
        cve = CVEResult(
            cve_id="CVE-2024-9999",
            base_score=5.5, severity="MEDIUM",
            affects_pinned=False, pinned_version="3.4",
        )
        d = cve.to_dict()
        assert d["title"].startswith("[unaffected@3.4]")

    def test_affects_pinned_none_no_prefix(self):
        """None (unknown) state should not add a prefix to avoid noise."""
        cve = CVEResult(cve_id="CVE-2024-0001", affects_pinned=None)
        d = cve.to_dict()
        assert not d["title"].startswith("[")

    def test_no_pinned_version_no_prefix_even_if_true(self):
        """Edge: affects_pinned=True with empty pinned_version → skip prefix.

        Shouldn't happen in practice (query_nvd sets both together) but
        the renderer must not crash or emit "[AFFECTS PINNED ]" with a
        trailing space.
        """
        cve = CVEResult(cve_id="CVE-2024-0001", affects_pinned=True, pinned_version="")
        d = cve.to_dict()
        assert "[AFFECTS PINNED" not in d["title"]


class TestQueryNvdSortsAffectsPinnedFirst:
    """query_nvd must sort affects_pinned=True before False/None."""

    def _mock_response(self, data: dict) -> MagicMock:
        response = MagicMock()
        response.read.return_value = json.dumps(data).encode()
        return response

    def test_affects_pinned_cves_lead(self):
        # Two CVEs: first keyword-matches but doesn't hit 3.4, second does.
        api_data = {
            "vulnerabilities": [
                {
                    "cve": {
                        "id": "CVE-OLD",
                        "descriptions": [{"lang": "en", "value": "old stuff"}],
                        "metrics": {"cvssMetricV31": [{"cvssData":
                            {"baseScore": 9.8, "baseSeverity": "CRITICAL"}}]},
                        "configurations": [{"nodes": [{"cpeMatch": [{
                            "vulnerable": True,
                            "criteria": "cpe:2.3:a:glfw:glfw:*:*:*:*:*:*:*:*",
                            "versionStartIncluding": "1.0.0",
                            "versionEndExcluding": "2.0.0",
                        }]}]}],
                    }
                },
                {
                    "cve": {
                        "id": "CVE-HITS",
                        "descriptions": [{"lang": "en", "value": "hits 3.4"}],
                        "metrics": {"cvssMetricV31": [{"cvssData":
                            {"baseScore": 5.5, "baseSeverity": "MEDIUM"}}]},
                        "configurations": [{"nodes": [{"cpeMatch": [{
                            "vulnerable": True,
                            "criteria": "cpe:2.3:a:glfw:glfw:*:*:*:*:*:*:*:*",
                            "versionStartIncluding": "3.0.0",
                            "versionEndExcluding": "3.5.0",
                        }]}]}],
                    }
                },
            ]
        }
        with patch("lib.tier5_nvd.urllib.request.urlopen",
                   return_value=self._mock_response(api_data)):
            results = query_nvd("glfw", pinned_version="3.4")
        # CVE-HITS (affects_pinned=True) must come first despite lower CVSS.
        assert results[0].cve_id == "CVE-HITS"
        assert results[0].affects_pinned is True
        assert results[1].cve_id == "CVE-OLD"
        assert results[1].affects_pinned is False

    def test_no_pinned_version_returns_unsorted_by_affects(self):
        """Without pinned_version, every affects_pinned is None (rank 1).

        Secondary sort by CVSS descending kicks in.
        """
        api_data = {
            "vulnerabilities": [
                {
                    "cve": {
                        "id": "CVE-LOW",
                        "descriptions": [{"lang": "en", "value": "low"}],
                        "metrics": {"cvssMetricV31": [{"cvssData":
                            {"baseScore": 3.0, "baseSeverity": "LOW"}}]},
                    }
                },
                {
                    "cve": {
                        "id": "CVE-HIGH",
                        "descriptions": [{"lang": "en", "value": "high"}],
                        "metrics": {"cvssMetricV31": [{"cvssData":
                            {"baseScore": 9.0, "baseSeverity": "CRITICAL"}}]},
                    }
                },
            ]
        }
        with patch("lib.tier5_nvd.urllib.request.urlopen",
                   return_value=self._mock_response(api_data)):
            results = query_nvd("foo")
        # All affects_pinned=None → secondary sort by CVSS descending.
        assert results[0].cve_id == "CVE-HIGH"
        assert results[1].cve_id == "CVE-LOW"


# ---------------------------------------------------------------------------
# D6 — run_nvd_queries dict-form dependencies
# ---------------------------------------------------------------------------


class TestRunNvdQueriesDictForm:
    """Dependencies can be strings (legacy) or {name, version} dicts."""

    def test_dict_form_passes_version_to_query(self, tmp_path: Path):
        config = {
            "nvd": {
                "enabled": True, "api_key": None, "api_key_env": "NVD_MISSING",
                "dependencies": [{"name": "GLFW", "version": "3.4"}],
            },
            "max_results_per_query": 5,
        }
        with patch.dict(os.environ, {}, clear=True), \
             patch("lib.tier5_nvd.query_nvd", return_value=[]) as mock_query, \
             patch("lib.tier5_nvd.time.sleep"), \
             patch("lib.tier5_nvd._validate_api_key"):
            run_nvd_queries(config, tmp_path, timedelta(days=7))

        mock_query.assert_called_once()
        call_kwargs = mock_query.call_args.kwargs
        assert call_kwargs.get("pinned_version") == "3.4"

    def test_string_form_passes_none_version(self, tmp_path: Path):
        """Legacy string form must continue working (no version filter)."""
        config = {
            "nvd": {
                "enabled": True, "api_key": None, "api_key_env": "NVD_MISSING",
                "dependencies": ["GLFW"],
            },
            "max_results_per_query": 5,
        }
        with patch.dict(os.environ, {}, clear=True), \
             patch("lib.tier5_nvd.query_nvd", return_value=[]) as mock_query, \
             patch("lib.tier5_nvd.time.sleep"), \
             patch("lib.tier5_nvd._validate_api_key"):
            run_nvd_queries(config, tmp_path, timedelta(days=7))

        mock_query.assert_called_once()
        assert mock_query.call_args.kwargs.get("pinned_version") is None

    def test_mixed_dep_forms_all_queried(self, tmp_path: Path):
        config = {
            "nvd": {
                "enabled": True, "api_key": None, "api_key_env": "NVD_MISSING",
                "dependencies": [
                    {"name": "GLFW", "version": "3.4"},
                    "stb image",  # legacy string
                    {"name": "OpenAL", "version": "1.24.1"},
                ],
            },
            "max_results_per_query": 5,
        }
        with patch.dict(os.environ, {}, clear=True), \
             patch("lib.tier5_nvd.query_nvd", return_value=[]) as mock_query, \
             patch("lib.tier5_nvd.time.sleep"), \
             patch("lib.tier5_nvd._validate_api_key"):
            run_nvd_queries(config, tmp_path, timedelta(days=7))

        assert mock_query.call_count == 3

    def test_malformed_dep_skipped_not_crashed(self, tmp_path: Path):
        """An int/list/None entry must be logged and skipped, not raised."""
        config = {
            "nvd": {
                "enabled": True, "api_key": None, "api_key_env": "NVD_MISSING",
                "dependencies": [42, {"name": "GLFW"}, None],
            },
            "max_results_per_query": 5,
        }
        with patch.dict(os.environ, {}, clear=True), \
             patch("lib.tier5_nvd.query_nvd", return_value=[]) as mock_query, \
             patch("lib.tier5_nvd.time.sleep"), \
             patch("lib.tier5_nvd._validate_api_key"):
            results = run_nvd_queries(config, tmp_path, timedelta(days=7))

        # Only the GLFW entry (dict with name, no version) is valid → one call.
        assert mock_query.call_count == 1

    def test_dict_without_name_skipped(self, tmp_path: Path):
        config = {
            "nvd": {
                "enabled": True, "api_key": None, "api_key_env": "NVD_MISSING",
                "dependencies": [{"version": "3.4"}, {"name": "", "version": "1.0"}],
            },
            "max_results_per_query": 5,
        }
        with patch.dict(os.environ, {}, clear=True), \
             patch("lib.tier5_nvd.query_nvd", return_value=[]) as mock_query, \
             patch("lib.tier5_nvd.time.sleep"), \
             patch("lib.tier5_nvd._validate_api_key"):
            run_nvd_queries(config, tmp_path, timedelta(days=7))

        mock_query.assert_not_called()

    def test_version_scoped_cache_separate_from_unversioned(self, tmp_path: Path):
        """Cache key changes with pinned_version so a version bump busts the cache."""
        config_v1 = {
            "nvd": {
                "enabled": True, "api_key": None, "api_key_env": "NVD_MISSING",
                "dependencies": [{"name": "GLFW", "version": "3.3"}],
            },
            "max_results_per_query": 5,
        }
        config_v2 = {
            "nvd": {
                "enabled": True, "api_key": None, "api_key_env": "NVD_MISSING",
                "dependencies": [{"name": "GLFW", "version": "3.4"}],
            },
            "max_results_per_query": 5,
        }

        with patch.dict(os.environ, {}, clear=True), \
             patch("lib.tier5_nvd.query_nvd", return_value=[]) as mock_query, \
             patch("lib.tier5_nvd.time.sleep"), \
             patch("lib.tier5_nvd._validate_api_key"):
            run_nvd_queries(config_v1, tmp_path, timedelta(days=7))
            run_nvd_queries(config_v2, tmp_path, timedelta(days=7))

        # Different versions → different cache keys → both call query_nvd.
        assert mock_query.call_count == 2
