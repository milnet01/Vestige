"""Tier 5: NVD (National Vulnerability Database) API integration for CVE queries."""

from __future__ import annotations

import hashlib
import json
import logging
import time
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from datetime import datetime, timedelta
from pathlib import Path
from typing import Any

from .findings import ResearchResult

log = logging.getLogger("audit")

NVD_API_URL = "https://services.nvd.nist.gov/rest/json/cves/2.0"


@dataclass
class CVEResult:
    """A single CVE from the NVD."""
    cve_id: str = ""
    description: str = ""
    published: str = ""
    base_score: float | None = None
    severity: str = ""

    def to_dict(self) -> dict[str, Any]:
        return {
            "title": f"{self.cve_id} (CVSS: {self.base_score or 'N/A'}, {self.severity})",
            "url": f"https://nvd.nist.gov/vuln/detail/{self.cve_id}",
            "snippet": self.description[:200],
        }


def query_nvd(
    keyword: str,
    max_results: int = 5,
    api_key: str | None = None,
) -> list[CVEResult]:
    """Query NVD API 2.0 for CVEs matching keyword."""
    params = {
        "keywordSearch": keyword,
        "resultsPerPage": str(max_results),
    }
    url = f"{NVD_API_URL}?{urllib.parse.urlencode(params)}"

    req = urllib.request.Request(url)
    req.add_header("User-Agent", "AuditTool/1.0")
    if api_key:
        req.add_header("apiKey", api_key)

    try:
        response = urllib.request.urlopen(req, timeout=30)
        data = json.loads(response.read())
    except Exception as e:
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

        results.append(CVEResult(
            cve_id=cve.get("id", ""),
            description=desc[:300],
            published=cve.get("published", "")[:10],
            base_score=score,
            severity=severity,
        ))

    return results


def run_nvd_queries(
    config_research: dict,
    cache_dir: Path,
    cache_ttl: timedelta,
) -> list[ResearchResult]:
    """Run NVD queries for configured dependencies. Returns ResearchResult list."""
    nvd_config = config_research.get("nvd", {})
    if not nvd_config.get("enabled", False):
        return []

    api_key = nvd_config.get("api_key")
    dependencies = nvd_config.get("dependencies", [])
    max_results = config_research.get("max_results_per_query", 5)

    if not dependencies:
        return []

    log.info("NVD: querying %d dependencies", len(dependencies))
    cache_dir.mkdir(parents=True, exist_ok=True)

    results: list[ResearchResult] = []

    for dep_name in dependencies:
        query = f"NVD: {dep_name}"
        cache_file = cache_dir / f"nvd_{hashlib.sha256(dep_name.encode()).hexdigest()[:16]}.json"

        # Check cache
        if cache_file.exists():
            try:
                cached = json.loads(cache_file.read_text())
                cached_time = datetime.fromisoformat(cached.get("timestamp", ""))
                if datetime.now() - cached_time < cache_ttl:
                    log.debug("NVD cache hit: %s", dep_name)
                    results.append(ResearchResult(
                        query=query,
                        results=cached.get("results", []),
                        cached=True,
                    ))
                    continue
            except (json.JSONDecodeError, ValueError, KeyError):
                pass

        # Live query
        cves = query_nvd(dep_name, max_results=max_results, api_key=api_key)

        formatted = [c.to_dict() for c in cves]

        # Cache
        cache_data = {
            "query": dep_name,
            "timestamp": datetime.now().isoformat(),
            "results": formatted,
        }
        try:
            cache_file.write_text(json.dumps(cache_data, indent=2))
        except OSError:
            pass

        results.append(ResearchResult(query=query, results=formatted))

        # Rate limit: 6 seconds between requests without API key
        if not api_key:
            time.sleep(6.0)
        else:
            time.sleep(1.0)

    cached_count = sum(1 for r in results if r.cached)
    log.info("NVD: %d results (%d cached, %d fresh)",
             len(results), cached_count, len(results) - cached_count)

    return results
