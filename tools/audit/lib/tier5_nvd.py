"""Tier 5: NVD (National Vulnerability Database) API integration for CVE queries."""

from __future__ import annotations

import hashlib
import json
import logging
import os
import re
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from datetime import datetime, timedelta
from pathlib import Path
from typing import Any

from .findings import ResearchResult

log = logging.getLogger("audit")

NVD_API_URL = "https://services.nvd.nist.gov/rest/json/cves/2.0"


# D6 (2.3.0) — version parsing tuned to the formats we actually see in
# FetchContent_Declare GIT_TAGs and NVD CPE version strings across
# Vestige's deps. Handles "3.4", "v2.9.4", "1.24.1", "VER-2-13-3"
# (FreeType), "3.12.0". Returns None for inputs we can't make sense of
# (e.g. branch names "master", "docking") so the caller can treat those
# as "unknown version, can't tag affects_pinned".
_VERSION_TOKEN_RE = re.compile(r"\d+")


def parse_semver(s: str | None) -> tuple[int, ...] | None:
    """Normalise a version string to a tuple of ints for comparison.

    Examples::

        parse_semver("3.4")         -> (3, 4)
        parse_semver("v2.9.4")      -> (2, 9, 4)
        parse_semver("1.24.1")      -> (1, 24, 1)
        parse_semver("VER-2-13-3")  -> (2, 13, 3)
        parse_semver("3.12.0")      -> (3, 12, 0)
        parse_semver("master")      -> None
        parse_semver("")            -> None

    The parse is intentionally permissive because CPEs sometimes embed
    versions in lowered-dot or dashed forms (e.g. "2_13_3"), and
    FreeType's own "VER-N-N-N" tag style would reject a strict semver
    regex. We just extract ascending numeric tokens and return them as
    a tuple; tuple comparison then gives the right ordering.

    Pre-release / build metadata suffixes ("1.0.0-rc1", "2.3.4+build5")
    are dropped — everything after the first non-digit/non-separator is
    ignored. This is good enough for NVD range filtering (which uses
    plain numeric versions) and avoids a full PEP 440 / SemVer 2.0
    implementation just for tagging CVEs.
    """
    if not s:
        return None
    # Drop common prefix noise.
    s_clean = s.strip().lstrip("vV").removeprefix("VER-")
    tokens = _VERSION_TOKEN_RE.findall(s_clean)
    if not tokens:
        return None
    return tuple(int(t) for t in tokens)


def version_in_range(
    pinned: tuple[int, ...],
    start_including: str | None = None,
    start_excluding: str | None = None,
    end_including: str | None = None,
    end_excluding: str | None = None,
    exact: str | None = None,
) -> bool:
    """Return True if *pinned* falls inside the NVD-shaped version range.

    NVD cpeMatch nodes use four optional bounds:
        - ``versionStartIncluding`` / ``versionStartExcluding``
        - ``versionEndIncluding`` / ``versionEndExcluding``

    Plus an exact version embedded in the CPE criteria string itself
    (field 5 of the 2.3 CPE URI). ``exact`` is only consulted when none
    of the range bounds are set — the presence of a start/end bound is
    the NVD's signal that the match is a range, not a pin.

    All bounds are parsed via :func:`parse_semver`; unparseable bounds
    are ignored (treated as "no bound on this side"). This is the
    defensive choice — emitting a False on an unparseable CPE would
    silently hide a potentially-relevant CVE.
    """
    def _parse_bound(v: str | None) -> tuple[int, ...] | None:
        return parse_semver(v) if v else None

    si = _parse_bound(start_including)
    se = _parse_bound(start_excluding)
    ei = _parse_bound(end_including)
    ee = _parse_bound(end_excluding)

    # If any range bound is present, use it — otherwise fall through to
    # the exact-pin check so a CPE like ``cpe:2.3:a:glfw:glfw:3.3::*``
    # (no range, exact 3.3) correctly returns True only for pinned=3.3.
    any_range = any(b is not None for b in (si, se, ei, ee))
    if any_range:
        if si is not None and pinned < si:
            return False
        if se is not None and pinned <= se:
            return False
        if ei is not None and pinned > ei:
            return False
        if ee is not None and pinned >= ee:
            return False
        return True

    exact_parsed = _parse_bound(exact)
    if exact_parsed is None:
        # No bounds and no parseable exact version — we can't decide.
        # Caller treats this as "unknown" (affects_pinned=None).
        return False
    return pinned == exact_parsed


def _extract_cpe_version(criteria: str) -> str:
    """Pull field 5 (version) out of a CPE 2.3 URI string.

    CPE 2.3 URIs have a fixed 13-field colon-separated structure:
    ``cpe:2.3:{part}:{vendor}:{product}:{version}:...``. We tolerate
    malformed strings by returning an empty string — the caller treats
    that the same as "no exact version supplied".
    """
    try:
        parts = criteria.split(":")
        # Field 5 is the version field (0-indexed in Python).
        return parts[5] if len(parts) > 5 else ""
    except (AttributeError, IndexError):
        return ""


def cve_affects_version(
    cve_dict: dict,
    pinned_version: str | None,
) -> bool | None:
    """Decide whether *pinned_version* is affected by this CVE.

    Returns:
        - ``True``  — at least one ``vulnerable`` cpeMatch range covers
          the pinned version.
        - ``False`` — configurations were present and parseable, but
          none of the vulnerable ranges match.
        - ``None``  — cannot determine (no pinned version, no parseable
          configurations, or unparseable pinned version).

    This three-state return is deliberate. Surfacing "False" for a
    keyword-matched CVE that doesn't actually affect the pinned version
    is the whole point of D6; but conflating False with None (the
    "couldn't tell" case) would mask missing-data situations and let a
    potentially-relevant CVE slip past review unflagged.
    """
    pinned = parse_semver(pinned_version)
    if pinned is None:
        return None

    configs = cve_dict.get("configurations", [])
    if not configs:
        return None

    any_match_seen = False
    for cfg in configs:
        for node in cfg.get("nodes", []):
            for m in node.get("cpeMatch", []):
                if not m.get("vulnerable", False):
                    continue
                any_match_seen = True
                exact = _extract_cpe_version(m.get("criteria", ""))
                if version_in_range(
                    pinned,
                    start_including=m.get("versionStartIncluding"),
                    start_excluding=m.get("versionStartExcluding"),
                    end_including=m.get("versionEndIncluding"),
                    end_excluding=m.get("versionEndExcluding"),
                    exact=exact if exact and exact != "*" else None,
                ):
                    return True

    # Configurations present but no vulnerable cpeMatch examined → unknown.
    # Configurations present and every range was examined → definitely False.
    return False if any_match_seen else None


@dataclass
class CVEResult:
    """A single CVE from the NVD.

    D6 (2.3.0): ``affects_pinned`` tags whether a project-specific
    pinned version falls into the CVE's affected-version range.
    ``None`` is the "unknown" state (no pinned version supplied, no
    parseable CPE config); ``True``/``False`` are confident verdicts.
    ``pinned_version`` is the version string we compared against, kept
    for rendering in the title prefix so a reviewer sees *what* was
    matched, not just that a match happened.
    """
    cve_id: str = ""
    description: str = ""
    published: str = ""
    base_score: float | None = None
    severity: str = ""
    affects_pinned: bool | None = None
    pinned_version: str = ""

    def to_dict(self) -> dict[str, Any]:
        # Surface the affects_pinned tag as a title prefix so it shows
        # up verbatim in the markdown report without any renderer
        # changes. `[AFFECTS PINNED X.Y]` draws the eye for the case
        # the reviewer actually cares about; `[unaffected@X.Y]` marks
        # false-positive noise explicitly without hiding it.
        if self.affects_pinned is True and self.pinned_version:
            tag = f"[AFFECTS PINNED {self.pinned_version}] "
        elif self.affects_pinned is False and self.pinned_version:
            tag = f"[unaffected@{self.pinned_version}] "
        else:
            tag = ""
        return {
            "title": f"{tag}{self.cve_id} (CVSS: {self.base_score or 'N/A'}, {self.severity})",
            "url": f"https://nvd.nist.gov/vuln/detail/{self.cve_id}",
            "snippet": self.description[:200],
            "affects_pinned": self.affects_pinned,
            "pinned_version": self.pinned_version,
        }


def query_nvd(
    keyword: str,
    max_results: int = 5,
    api_key: str | None = None,
    pinned_version: str | None = None,
) -> list[CVEResult]:
    """Query NVD API 2.0 for CVEs matching keyword.

    D6 (2.3.0): when *pinned_version* is supplied, each returned CVE
    is post-filtered via :func:`cve_affects_version` against the CVE's
    ``configurations`` tree. The result is stored on
    :attr:`CVEResult.affects_pinned` (``True``/``False``/``None``) so
    the report can highlight CVEs that actually hit the project's
    pinned version and de-emphasise the keyword-match noise.
    """
    params = {
        "keywordSearch": keyword,
        "resultsPerPage": str(max_results),
    }
    url = f"{NVD_API_URL}?{urllib.parse.urlencode(params)}"

    req = urllib.request.Request(url)
    req.add_header("User-Agent", "AuditTool/1.0")
    if api_key:
        req.add_header("apiKey", api_key)

    # AUDIT.md §L7 / FIXPLAN J: cap response body size. The NVD API
    # normally returns <1 MB per keyword but a compromised upstream or
    # gzip bomb could wedge the process. 16 MB is generous but still
    # bounds worst-case memory.
    MAX_NVD_BODY_BYTES = 16 * 1024 * 1024
    try:
        response = urllib.request.urlopen(req, timeout=30)
        body = response.read(MAX_NVD_BODY_BYTES + 1)
        if len(body) > MAX_NVD_BODY_BYTES:
            log.warning("NVD response for '%s' exceeded %d bytes — "
                        "truncated, skipping", keyword, MAX_NVD_BODY_BYTES)
            return []
        data = json.loads(body)
    except urllib.error.HTTPError as e:
        if e.code == 429:
            log.warning("NVD API rate limited for '%s' — backing off", keyword)
        else:
            log.warning("NVD API HTTP %d for '%s': %s", e.code, keyword, e)
        return []
    except (urllib.error.URLError, OSError, json.JSONDecodeError) as e:
        log.warning("NVD API query failed for '%s': %s", keyword, e)
        return []

    results: list[CVEResult] = []
    for vuln in data.get("vulnerabilities", []):
        cve = vuln.get("cve", {})

        # Description (English)
        desc = ""
        for d in cve.get("descriptions", []):
            if d.get("lang") == "en":
                desc = d.get("value", "")
                break

        # CVSS score (try v3.1, v3.0, then v2)
        score = None
        severity = ""
        metrics = cve.get("metrics", {})
        for cvss_key in ("cvssMetricV31", "cvssMetricV30", "cvssMetricV2"):
            cvss_list = metrics.get(cvss_key, [])
            if cvss_list:
                cvss_data = cvss_list[0].get("cvssData", {})
                score = cvss_data.get("baseScore")
                severity = cvss_data.get("baseSeverity", "")
                break

        # D6: compute affects_pinned from the CVE's configurations.
        # cve_affects_version handles the None (unknown) case when no
        # pinned_version was supplied.
        affects = cve_affects_version(cve, pinned_version)

        results.append(CVEResult(
            cve_id=cve.get("id", ""),
            description=desc[:300],
            published=cve.get("published", "")[:10],
            base_score=score,
            severity=severity,
            affects_pinned=affects,
            pinned_version=pinned_version or "",
        ))

    # Sort affects_pinned=True first, then unknown (None), then False.
    # Secondary sort by CVSS score descending so the most severe
    # affects-pinned CVE leads the list. None < number comparisons
    # would raise in Python 3, so coerce missing scores to 0.
    def _sort_key(c: CVEResult) -> tuple[int, float]:
        rank = {True: 0, None: 1, False: 2}[c.affects_pinned]
        return (rank, -(c.base_score or 0.0))

    results.sort(key=_sort_key)
    return results


def _resolve_api_key(nvd_config: dict) -> str | None:
    """Resolve the NVD API key from config or environment variable.

    Checks config ``api_key`` first, then falls back to the environment
    variable named by ``api_key_env`` (default ``NVD_API_KEY``).

    Emits a warning if ``api_key`` is a literal in the YAML — committing a
    key to the repo is a credential-leak vector (see AUDIT.md §C2).

    Shape-validates the resolved key (AUDIT.md §H4): refuses keys with
    characters that could CRLF-inject the request header.
    """
    key = nvd_config.get("api_key")
    if key:
        log.warning(
            "NVD api_key is set as a literal in audit_config.yaml — this is a "
            "credential-leak risk. Set it to null and use the NVD_API_KEY env "
            "var instead."
        )
    else:
        env_var = nvd_config.get("api_key_env", "NVD_API_KEY")
        key = os.environ.get(env_var)

    if key and not _API_KEY_SHAPE.fullmatch(key):
        log.warning(
            "NVD api_key has unexpected shape (CRLF / non-alphanumeric?) — "
            "ignoring and falling back to unauthenticated requests."
        )
        return None
    return key


# NVD issues UUID-style keys: 36 chars, hex with dashes. Be slightly
# permissive in case the format evolves, but reject anything that could
# inject into an HTTP header (\r\n, spaces, control chars).
_API_KEY_SHAPE = re.compile(r"^[A-Za-z0-9-]{16,64}$")


def _validate_api_key(api_key: str) -> bool:
    """Make a single lightweight test query to verify authenticated access.

    Returns ``True`` if the key is accepted (HTTP 200), ``False`` otherwise.
    """
    params = {"keywordSearch": "test", "resultsPerPage": "1"}
    url = f"{NVD_API_URL}?{urllib.parse.urlencode(params)}"
    req = urllib.request.Request(url)
    req.add_header("User-Agent", "AuditTool/1.0")
    req.add_header("apiKey", api_key)
    try:
        response = urllib.request.urlopen(req, timeout=15)
        log.info("NVD API key validated — authenticated access confirmed")
        return True
    except urllib.error.HTTPError as e:
        log.warning("NVD API key validation failed (HTTP %d) — falling back to unauthenticated", e.code)
        return False
    except (urllib.error.URLError, OSError) as e:
        log.warning("NVD API key validation failed (%s) — falling back to unauthenticated", e)
        return False


def run_nvd_queries(
    config_research: dict,
    cache_dir: Path,
    cache_ttl: timedelta,
) -> list[ResearchResult]:
    """Run NVD queries for configured dependencies. Returns ResearchResult list."""
    nvd_config = config_research.get("nvd", {})
    if not nvd_config.get("enabled", False):
        return []

    # Resolve API key: config value, then environment variable
    api_key = _resolve_api_key(nvd_config)

    # Validate the key if present
    if api_key:
        if not _validate_api_key(api_key):
            api_key = None  # Fall back to unauthenticated
    else:
        log.info("NVD: no API key configured — using unauthenticated rate limits")

    # Rate limit delay: 0.6s with key (safely under 50 req/30s), 7.0s without
    rate_delay = 0.6 if api_key else 7.0

    dependencies = nvd_config.get("dependencies", [])
    max_results = config_research.get("max_results_per_query", 5)

    if not dependencies:
        return []

    log.info("NVD: querying %d dependencies (authenticated=%s, delay=%.1fs)",
             len(dependencies), bool(api_key), rate_delay)
    cache_dir.mkdir(parents=True, exist_ok=True)

    results: list[ResearchResult] = []

    for dep in dependencies:
        # D6 (2.3.0): a dep can be either a bare string (legacy,
        # name-only) or a dict {name, version} that narrows CVE results
        # to those actually affecting the pinned version. Reject
        # malformed entries without crashing the whole tier.
        if isinstance(dep, str):
            dep_name = dep
            pinned_version: str | None = None
        elif isinstance(dep, dict):
            dep_name = str(dep.get("name", "")).strip()
            pinned_version_raw = dep.get("version")
            pinned_version = str(pinned_version_raw) if pinned_version_raw else None
        else:
            log.warning("NVD: skipping malformed dependency entry: %r", dep)
            continue

        if not dep_name:
            log.warning("NVD: skipping dependency with empty name: %r", dep)
            continue

        # Cache key includes the version so the same dep at different
        # pinned versions caches separately (otherwise a version bump
        # would return a stale affects_pinned verdict).
        cache_ident = f"{dep_name}@{pinned_version}" if pinned_version else dep_name
        query = f"NVD: {cache_ident}"
        cache_file = cache_dir / f"nvd_{hashlib.sha256(cache_ident.encode()).hexdigest()[:16]}.json"

        # Check cache
        if cache_file.exists():
            try:
                cached = json.loads(cache_file.read_text())
                cached_time = datetime.fromisoformat(cached.get("timestamp", ""))
                if datetime.now() - cached_time < cache_ttl:
                    log.debug("NVD cache hit: %s", cache_ident)
                    results.append(ResearchResult(
                        query=query,
                        results=cached.get("results", []),
                        cached=True,
                    ))
                    continue
            except (json.JSONDecodeError, ValueError, KeyError):
                pass

        # Live query
        cves = query_nvd(
            dep_name,
            max_results=max_results,
            api_key=api_key,
            pinned_version=pinned_version,
        )

        formatted = [c.to_dict() for c in cves]

        # Cache
        cache_data = {
            "query": cache_ident,
            "timestamp": datetime.now().isoformat(),
            "results": formatted,
        }
        try:
            cache_file.write_text(json.dumps(cache_data, indent=2))
        except OSError:
            pass

        results.append(ResearchResult(query=query, results=formatted))
        time.sleep(rate_delay)

    cached_count = sum(1 for r in results if r.cached)
    log.info("NVD: %d results (%d cached, %d fresh)",
             len(results), cached_count, len(results) - cached_count)

    return results
