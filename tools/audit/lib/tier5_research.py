"""Tier 5: Online research via DuckDuckGo with file-based caching."""

from __future__ import annotations

import hashlib
import json
import logging
import time
from datetime import datetime, timedelta
from pathlib import Path

from .config import Config
from .findings import ResearchResult

log = logging.getLogger("audit")

# Try importing duckduckgo_search — graceful fallback if not installed
try:
    from duckduckgo_search import DDGS
    HAS_DDGS = True
except ImportError:
    HAS_DDGS = False


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
    """Search with file-based caching."""
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

    # Live search
    try:
        log.debug("Searching: %s", query)
        ddgs = DDGS()
        raw_results = ddgs.text(query, max_results=max_results)

        formatted = [
            {
                "title": r.get("title", ""),
                "url": r.get("href", r.get("link", "")),
                "snippet": r.get("body", r.get("snippet", ""))[:200],
            }
            for r in raw_results
        ]

        # Write to cache
        cache_data = {
            "query": query,
            "timestamp": datetime.now().isoformat(),
            "results": formatted,
        }
        cache_file.write_text(json.dumps(cache_data, indent=2))

        return ResearchResult(query=query, results=formatted)

    except Exception as e:
        log.warning("Search failed for '%s': %s", query, e)
        return ResearchResult(query=query, error=str(e))
