# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Self-contained HTML report generator for audit results."""

from __future__ import annotations

import html
import json
import logging
from collections import defaultdict
from datetime import datetime
from typing import Any

from .config import Config
from .findings import Finding, Severity, deduplicate
from .runner import AuditResults

log = logging.getLogger("audit")


# ---------------------------------------------------------------------------
# Inline CSS — dark theme matching the web UI
# ---------------------------------------------------------------------------

_CSS = """
:root {
    --bg: #1a1a2e;
    --surface: #16213e;
    --surface2: #0f3460;
    --text: #e0e0e0;
    --text-muted: #a0a0a0;
    --accent: #e94560;
    --accent-green: #4ecca3;
    --accent-yellow: #f0c040;
    --accent-blue: #53a8f0;
    --border: #2a2a4a;
}
* { margin: 0; padding: 0; box-sizing: border-box; }
body {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, sans-serif;
    background: var(--bg);
    color: var(--text);
    line-height: 1.6;
    padding: 2rem;
    max-width: 1200px;
    margin: 0 auto;
}
h1 { color: var(--accent); margin-bottom: 0.5rem; font-size: 1.8rem; }
h2 { color: var(--accent-blue); margin: 2rem 0 0.75rem; border-bottom: 1px solid var(--border); padding-bottom: 0.5rem; }
h3 { color: var(--text); margin: 1.5rem 0 0.5rem; }
.meta { color: var(--text-muted); margin-bottom: 1.5rem; }
.badge {
    display: inline-block;
    padding: 0.25rem 0.75rem;
    border-radius: 4px;
    font-weight: 600;
    font-size: 0.9rem;
    margin: 0.2rem;
}
.badge-critical { background: #d32f2f; color: #fff; }
.badge-high { background: #e65100; color: #fff; }
.badge-medium { background: #f57c00; color: #fff; }
.badge-low { background: #1976d2; color: #fff; }
.badge-info { background: #455a64; color: #fff; }
.summary-cards {
    display: flex;
    gap: 1rem;
    flex-wrap: wrap;
    margin: 1rem 0;
}
.card {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 1rem 1.5rem;
    min-width: 150px;
    flex: 1;
}
.card .label { color: var(--text-muted); font-size: 0.85rem; }
.card .value { font-size: 1.5rem; font-weight: 700; }
table {
    width: 100%;
    border-collapse: collapse;
    margin: 1rem 0;
    background: var(--surface);
    border-radius: 8px;
    overflow: hidden;
}
th {
    background: var(--surface2);
    color: var(--accent-blue);
    padding: 0.75rem 1rem;
    text-align: left;
    font-size: 0.85rem;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    cursor: pointer;
    user-select: none;
}
th:hover { color: var(--accent); }
td { padding: 0.6rem 1rem; border-top: 1px solid var(--border); font-size: 0.9rem; }
tr:hover td { background: rgba(255,255,255,0.03); }
code { background: var(--surface2); padding: 0.15rem 0.4rem; border-radius: 3px; font-size: 0.85rem; }
.trend-bar {
    display: inline-block;
    height: 16px;
    border-radius: 2px;
    margin-right: 2px;
    vertical-align: middle;
}
svg { margin: 1rem 0; }
footer { margin-top: 3rem; padding-top: 1rem; border-top: 1px solid var(--border); color: var(--text-muted); font-size: 0.85rem; }
"""

# ---------------------------------------------------------------------------
# Inline JavaScript — sortable tables
# ---------------------------------------------------------------------------

_JS = """
document.addEventListener('DOMContentLoaded', function() {
    document.querySelectorAll('th[data-sort]').forEach(function(th) {
        th.addEventListener('click', function() {
            var table = th.closest('table');
            var tbody = table.querySelector('tbody');
            var rows = Array.from(tbody.querySelectorAll('tr'));
            var col = parseInt(th.dataset.sort);
            var asc = th.dataset.dir !== 'asc';
            th.dataset.dir = asc ? 'asc' : 'desc';
            var sevOrder = {critical:0, high:1, medium:2, low:3, info:4};
            rows.sort(function(a, b) {
                var av = a.children[col].textContent.trim().toLowerCase();
                var bv = b.children[col].textContent.trim().toLowerCase();
                if (col === 0 && sevOrder[av] !== undefined) {
                    return asc ? sevOrder[av] - sevOrder[bv] : sevOrder[bv] - sevOrder[av];
                }
                return asc ? av.localeCompare(bv) : bv.localeCompare(av);
            });
            rows.forEach(function(r) { tbody.appendChild(r); });
        });
    });
});
"""


def generate_html_report(
    results: AuditResults,
    config: Config,
    trend_report: Any = None,
) -> str:
    """Generate a self-contained HTML report string.

    The report includes inline CSS and JS with no external dependencies.
    """
    findings = deduplicate(results.findings)
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    parts: list[str] = []
    parts.append("<!DOCTYPE html>")
    parts.append('<html lang="en">')
    parts.append("<head>")
    parts.append('<meta charset="UTF-8">')
    parts.append('<meta name="viewport" content="width=device-width, initial-scale=1.0">')
    parts.append(f"<title>Audit Report: {_esc(config.project_name)}</title>")
    parts.append(f"<style>{_CSS}</style>")
    parts.append("</head>")
    parts.append("<body>")

    # Header
    parts.append(f"<h1>Audit Report: {_esc(config.project_name)}</h1>")
    parts.append(f'<p class="meta">Generated: {now} | Tiers: {results.tiers_run} '
                 f'| Duration: {results.duration:.1f}s</p>')

    # Executive summary badges
    parts.append(_build_summary_section(findings, results))

    # Tier breakdown cards
    parts.append(_build_tier_breakdown(results))

    # Findings table
    parts.append(_build_findings_table(findings))

    # Trend chart
    if trend_report is not None:
        parts.append(_build_trend_section(trend_report))

    # Footer
    parts.append(f'<footer>Generated by Vestige Audit Tool | {now}</footer>')

    parts.append(f"<script>{_JS}</script>")
    parts.append("</body>")
    parts.append("</html>")

    return "\n".join(parts)


def _esc(text: str) -> str:
    """Escape text for HTML."""
    return html.escape(str(text))


def _severity_badge(severity: str) -> str:
    """Return an HTML badge span for a severity level."""
    return f'<span class="badge badge-{severity}">{severity.upper()}</span>'


def _build_summary_section(findings: list[Finding], results: AuditResults) -> str:
    """Build the executive summary with severity badges and stat cards."""
    counts: dict[str, int] = defaultdict(int)
    for f in findings:
        counts[f.severity.name.lower()] += 1

    lines: list[str] = ["<h2>Executive Summary</h2>"]

    # Severity badges
    lines.append('<div style="margin: 1rem 0;">')
    for sev in ("critical", "high", "medium", "low", "info"):
        count = counts.get(sev, 0)
        if count > 0:
            lines.append(f'<span class="badge badge-{sev}">{sev.upper()}: {count}</span>')
    lines.append("</div>")

    # Stat cards
    lines.append('<div class="summary-cards">')
    lines.append(_card("Total Findings", str(len(findings))))
    lines.append(_card("Duration", f"{results.duration:.1f}s"))
    lines.append(_card("Tiers Run", ", ".join(str(t) for t in results.tiers_run)))

    build = results.tier1_summary.get("build", {})
    if build.get("build_ok") is not None:
        status = "Pass" if build["build_ok"] else "FAIL"
        lines.append(_card("Build", status))

    tests = results.tier1_summary.get("tests", {})
    if tests:
        lines.append(_card("Tests", f"{tests.get('passed', 0)}P / {tests.get('failed', 0)}F"))

    lines.append("</div>")
    return "\n".join(lines)


def _card(label: str, value: str) -> str:
    """Return an HTML stat card."""
    return (f'<div class="card">'
            f'<div class="label">{_esc(label)}</div>'
            f'<div class="value">{_esc(value)}</div>'
            f'</div>')


def _build_tier_breakdown(results: AuditResults) -> str:
    """Build tier-by-tier breakdown section."""
    lines: list[str] = ["<h2>Tier Breakdown</h2>"]

    tier_names = {1: "Build & Static Analysis", 2: "Pattern Scanning",
                  3: "Changed Files", 4: "Statistics", 5: "Online Research"}

    tier_counts: dict[int, int] = defaultdict(int)
    for f in results.findings:
        tier_counts[f.source_tier] += 1

    lines.append('<div class="summary-cards">')
    for t in results.tiers_run:
        name = tier_names.get(t, f"Tier {t}")
        count = tier_counts.get(t, 0)
        lines.append(_card(f"Tier {t}: {name}", f"{count} findings"))
    lines.append("</div>")

    return "\n".join(lines)


def _build_findings_table(findings: list[Finding]) -> str:
    """Build a sortable findings table."""
    lines: list[str] = ["<h2>Findings</h2>"]

    if not findings:
        lines.append("<p>No findings.</p>")
        return "\n".join(lines)

    lines.append("<table>")
    lines.append("<thead><tr>")
    headers = ["Severity", "File", "Line", "Category", "Title", "Tier"]
    for i, h in enumerate(headers):
        lines.append(f'<th data-sort="{i}">{h}</th>')
    lines.append("</tr></thead>")
    lines.append("<tbody>")

    for f in findings:
        sev = f.severity.name.lower()
        lines.append("<tr>")
        lines.append(f"<td>{_severity_badge(sev)}</td>")
        lines.append(f"<td><code>{_esc(f.file)}</code></td>")
        lines.append(f"<td>{f.line or '-'}</td>")
        lines.append(f"<td>{_esc(f.category)}</td>")
        lines.append(f"<td>{_esc(f.title[:120])}</td>")
        lines.append(f"<td>{f.source_tier}</td>")
        lines.append("</tr>")

    lines.append("</tbody></table>")
    return "\n".join(lines)


def _build_trend_section(trend_report: Any) -> str:
    """Build an SVG bar chart and summary for trend data."""
    lines: list[str] = ["<h2>Finding Trends</h2>"]

    direction = trend_report.direction
    delta = trend_report.finding_delta
    delta_str = f"+{delta}" if delta > 0 else str(delta)

    color = "#4ecca3" if direction == "improving" else (
        "#e94560" if direction == "worsening" else "#f0c040")

    lines.append(f'<p>Overall: <strong style="color: {color}">{direction}</strong> '
                 f'({delta_str} findings)</p>')

    # Simple SVG bar chart for per-category trends
    categories = trend_report.categories
    if categories:
        bar_h = 24
        chart_h = len(categories) * (bar_h + 4) + 20
        lines.append(f'<svg width="600" height="{chart_h}" '
                     f'style="background: var(--surface); border-radius: 8px; padding: 10px;">')
        y = 10
        for cat, direction_val in sorted(categories.items()):
            cat_color = "#4ecca3" if direction_val == "improving" else (
                "#e94560" if direction_val == "worsening" else "#f0c040")
            bar_w = 100 if direction_val == "stable" else (
                60 if direction_val == "improving" else 140)
            lines.append(f'<text x="0" y="{y + 16}" fill="#e0e0e0" '
                         f'font-size="12">{_esc(cat)}</text>')
            lines.append(f'<rect x="200" y="{y}" width="{bar_w}" height="{bar_h}" '
                         f'rx="3" fill="{cat_color}" opacity="0.8"/>')
            lines.append(f'<text x="{205 + bar_w}" y="{y + 16}" fill="#a0a0a0" '
                         f'font-size="11">{direction_val}</text>')
            y += bar_h + 4
        lines.append("</svg>")

    return "\n".join(lines)
