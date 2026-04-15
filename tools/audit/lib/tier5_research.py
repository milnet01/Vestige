# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tier 5: Online research via DuckDuckGo with file-based caching."""

from __future__ import annotations

import hashlib
import json
import logging
import re
import time
from datetime import datetime, timedelta
from pathlib import Path

from .config import Config
from .findings import ResearchResult

log = logging.getLogger("audit")

# Try importing ddgs (new name) or duckduckgo_search (old name)
try:
    from ddgs import DDGS
    HAS_DDGS = True
except ImportError:
    try:
        from duckduckgo_search import DDGS
        HAS_DDGS = True
    except ImportError:
        HAS_DDGS = False

# ---------------------------------------------------------------------------
# Result quality filtering
# ---------------------------------------------------------------------------

# Domains that reliably produce relevant technical results
_TRUSTED_DOMAINS = frozenset({
    "github.com", "stackoverflow.com", "nvd.nist.gov", "cve.org",
    "cwe.mitre.org", "wiki.sei.cmu.edu", "owasp.org",
    "learn.microsoft.com", "developer.mozilla.org", "doc.qt.io",
    "docs.python.org", "docs.rs", "pkg.go.dev",
    "man7.org", "lwn.net", "pubs.opengroup.org",
    "khronos.org", "opengl.org", "learnopengl.com",
    "en.cppreference.com", "cplusplus.com",
    "security.googleblog.com", "blog.cloudflare.com",
    "hackerone.com", "portswigger.net",
})

# Patterns indicating non-English or irrelevant results
_IRRELEVANT_PATTERNS = re.compile(
    r"[\u4e00-\u9fff]|"           # Chinese characters
    r"[\u3040-\u309f]|"           # Hiragana
    r"[\u30a0-\u30ff]|"           # Katakana
    r"[\uac00-\ud7af]|"           # Korean
    r"[\u0600-\u06ff]|"           # Arabic
    r"[\u0400-\u04ff]{10,}|"      # Long Cyrillic blocks
    r"zhihu\.com|"
    r"csdn\.net|"
    r"qiita\.com|"
    r"habr\.com|"
    r"tistory\.com|"
    r"nocache|"                    # Generic caching noise
    r"coupon|discount|buy now"     # Spam
)


def _is_relevant_result(result: dict) -> bool:
    """Filter out non-English and irrelevant search results."""
    title = result.get("title", "")
    url = result.get("url", "")
    snippet = result.get("snippet", "")
    combined = f"{title} {url} {snippet}"

    if _IRRELEVANT_PATTERNS.search(combined):
        return False

    # Reject results with mostly non-ASCII titles (likely non-English)
    if title:
        ascii_ratio = sum(1 for c in title if ord(c) < 128) / max(len(title), 1)
        if ascii_ratio < 0.5:
            return False

    return True


def run(config: Config, findings: list | None = None) -> list[ResearchResult]:
    """Execute web research queries with caching."""
    results: list[ResearchResult] = []
    research_config = config.get("research", default={})

    if not research_config.get("enabled", True):
        log.info("Research disabled — skipping Tier 5")
        return results

    if not HAS_DDGS:
        log.warning("duckduckgo-search not installed — skipping Tier 5")
        log.warning("Install with: pip install duckduckgo-search")
        results.append(ResearchResult(
            query="(library not installed)",
            error="duckduckgo-search not installed. Run: pip install duckduckgo-search",
        ))
        return results

    cache_dir = config.root / research_config.get("cache_dir", ".audit_cache")
    cache_ttl = timedelta(days=research_config.get("cache_ttl_days", 7))
    max_results = research_config.get("max_results_per_query", 3)

    # Collect all queries
    queries: list[str] = []

    # Config-defined topics
    for topic in research_config.get("topics", []):
        q = topic.get("query", "")
        if q:
            queries.append(q)

    # Custom queries
    for q in research_config.get("custom_queries", []):
        if q:
            queries.append(q)

    # Auto-generated domain-specific security queries
    try:
        from .tier5_improvements import get_security_queries
        sec_queries = get_security_queries(config)
        if sec_queries:
            log.info("Tier 5: %d domain-specific security queries", len(sec_queries))
            queries.extend(sec_queries)
    except Exception as e:
        log.warning("Security query generation failed: %s", e)

    # Auto-detected improvement queries (scan codebase for technologies)
    try:
        from .tier5_improvements import get_improvement_queries
        improvement_queries = get_improvement_queries(config)
        if improvement_queries:
            log.info("Tier 5: %d improvement queries from tech detection", len(improvement_queries))
            queries.extend(improvement_queries)
    except Exception as e:
        log.warning("Tech detection failed: %s", e)

    if not queries:
        log.info("No research queries configured — skipping Tier 5")
        return results

    log.info("Tier 5: running %d research queries", len(queries))

    # Ensure cache directory exists
    cache_dir.mkdir(parents=True, exist_ok=True)

    for query in queries:
        result = _search_with_cache(query, cache_dir, cache_ttl, max_results)
        results.append(result)
        # Be polite to DuckDuckGo — small delay between requests
        if not result.cached:
            time.sleep(1.0)

    # NVD API queries (if configured)
    try:
        from .tier5_nvd import run_nvd_queries
        nvd_results = run_nvd_queries(research_config, cache_dir, cache_ttl)
        results.extend(nvd_results)
    except Exception as e:
        log.warning("NVD queries failed: %s", e)

    cached_count = sum(1 for r in results if r.cached)
    log.info("Tier 5: %d results (%d cached, %d fresh)",
             len(results), cached_count, len(results) - cached_count)
    return results


def _cache_key(query: str) -> str:
    """Generate a cache filename from the query string."""
    return hashlib.sha256(query.encode()).hexdigest()[:16]


def _search_with_cache(
    query: str,
    cache_dir: Path,
    cache_ttl: timedelta,
    max_results: int,
) -> ResearchResult:
    """Search with file-based caching and result quality filtering."""
    cache_file = cache_dir / f"{_cache_key(query)}.json"

    # Check cache
    if cache_file.exists():
        try:
            data = json.loads(cache_file.read_text())
            cached_time = datetime.fromisoformat(data.get("timestamp", ""))
            if datetime.now() - cached_time < cache_ttl:
                log.debug("Cache hit: %s", query)
                return ResearchResult(
                    query=query,
                    results=data.get("results", []),
                    cached=True,
                )
        except (json.JSONDecodeError, ValueError, KeyError):
            pass  # Stale or corrupt cache — re-fetch

    # Live search — request extra results so we have enough after filtering
    try:
        log.debug("Searching: %s", query)
        ddgs = DDGS()
        # Request 2x results to account for filtering, enforce English region
        raw_results = ddgs.text(query, region="us-en", max_results=max_results * 2)

        formatted = [
            {
                "title": r.get("title", ""),
                "url": r.get("href", r.get("link", "")),
                "snippet": r.get("body", r.get("snippet", ""))[:200],
            }
            for r in raw_results
        ]

        # Filter out non-English and irrelevant results
        filtered = [r for r in formatted if _is_relevant_result(r)]

        # If filtering removed everything, fall back to unfiltered (better than nothing)
        if not filtered and formatted:
            log.debug("All results filtered for '%s' — keeping original", query)
            filtered = formatted

        # Cap to requested max
        filtered = filtered[:max_results]

        # Write to cache
        cache_data = {
            "query": query,
            "timestamp": datetime.now().isoformat(),
            "results": filtered,
        }
        cache_file.write_text(json.dumps(cache_data, indent=2))

        return ResearchResult(query=query, results=filtered)

    except Exception as e:
        log.warning("Search failed for '%s': %s", query, e)
        return ResearchResult(query=query, error=str(e))
