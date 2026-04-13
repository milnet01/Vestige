"""Tests for lib.sarif_output — SARIF 2.1.0 generation."""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import pytest

from lib.findings import Finding, Severity
from lib.sarif_output import (
    generate_sarif,
    write_sarif,
    _severity_to_level,
    _build_rules,
    _build_result,
    _pattern_to_rule_name,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_finding(
    *,
    file: str = "src/main.cpp",
    line: int = 10,
    severity: Severity = Severity.HIGH,
    category: str = "memory",
    source_tier: int = 1,
    title: str = "Test finding",
    detail: str = "Test detail",
    pattern_name: str = "test_pattern",
) -> Finding:
    return Finding(
        file=file,
        line=line,
        severity=severity,
        category=category,
        source_tier=source_tier,
        title=title,
        detail=detail,
        pattern_name=pattern_name,
    )


# ---------------------------------------------------------------------------
# _severity_to_level
# ---------------------------------------------------------------------------

class TestSeverityToLevel:
    def test_critical_maps_to_error(self):
        assert _severity_to_level(Severity.CRITICAL) == "error"

    def test_high_maps_to_error(self):
        assert _severity_to_level(Severity.HIGH) == "error"

    def test_medium_maps_to_warning(self):
        assert _severity_to_level(Severity.MEDIUM) == "warning"

    def test_low_maps_to_note(self):
        assert _severity_to_level(Severity.LOW) == "note"

    def test_info_maps_to_note(self):
        assert _severity_to_level(Severity.INFO) == "note"


# ---------------------------------------------------------------------------
# generate_sarif
# ---------------------------------------------------------------------------

class TestGenerateSarif:
    def test_empty_findings_valid_structure(self):
        sarif = generate_sarif([], version="1.0.0")
        assert sarif["version"] == "2.1.0"
        assert "$schema" in sarif
        assert len(sarif["runs"]) == 1
        run = sarif["runs"][0]
        assert run["tool"]["driver"]["name"] == "vestige-audit"
        assert run["results"] == []
        assert run["tool"]["driver"]["rules"] == []

    def test_single_finding_maps_correctly(self):
        finding = _make_finding(
            file="src/render.cpp",
            line=42,
            severity=Severity.HIGH,
            pattern_name="buffer_overrun",
            title="Buffer overrun detected",
            detail="Array index out of bounds",
        )
        sarif = generate_sarif([finding])
        run = sarif["runs"][0]

        assert len(run["results"]) == 1
        result = run["results"][0]
        assert result["ruleId"] == "buffer_overrun"
        assert result["level"] == "error"
        assert "Buffer overrun detected" in result["message"]["text"]

        loc = result["locations"][0]["physicalLocation"]
        assert loc["artifactLocation"]["uri"] == "src/render.cpp"
        assert loc["region"]["startLine"] == 42

    def test_version_field_matches_passed_version(self):
        sarif = generate_sarif([], version="1.7.0")
        driver = sarif["runs"][0]["tool"]["driver"]
        assert driver["version"] == "1.7.0"

    def test_rules_deduplicated(self):
        f1 = _make_finding(file="a.cpp", line=1, pattern_name="raw_new")
        f2 = _make_finding(file="b.cpp", line=5, pattern_name="raw_new")
        f3 = _make_finding(file="c.cpp", line=10, pattern_name="buffer_overrun")

        sarif = generate_sarif([f1, f2, f3])
        rules = sarif["runs"][0]["tool"]["driver"]["rules"]
        rule_ids = [r["id"] for r in rules]

        assert rule_ids.count("raw_new") == 1
        assert rule_ids.count("buffer_overrun") == 1
        assert len(rules) == 2

    def test_multiple_findings_from_different_tiers(self):
        findings = [
            _make_finding(source_tier=1, category="cppcheck", pattern_name="p1"),
            _make_finding(source_tier=2, category="pattern", pattern_name="p2"),
            _make_finding(source_tier=4, category="cognitive_complexity", pattern_name="p3"),
        ]
        sarif = generate_sarif(findings)
        run = sarif["runs"][0]
        assert len(run["results"]) == 3
        assert len(run["tool"]["driver"]["rules"]) == 3

    def test_location_uri_and_region(self):
        # AUDIT.md §M23 / FIXPLAN J4: uriBaseId is the symbolic name
        # "SRCROOT" (matching run.originalUriBaseIds), NOT the legacy
        # SARIF-1 placeholder form `%SRCROOT%` that SARIF 2.1 consumers
        # (GitHub Advanced Security, VS Code viewer) reject.
        finding = _make_finding(file="engine/core/event.h", line=99)
        sarif = generate_sarif([finding])
        loc = sarif["runs"][0]["results"][0]["locations"][0]["physicalLocation"]
        assert loc["artifactLocation"]["uri"] == "engine/core/event.h"
        assert loc["artifactLocation"]["uriBaseId"] == "SRCROOT"
        assert loc["region"]["startLine"] == 99

        # The same run must define SRCROOT in originalUriBaseIds so
        # consumers can resolve the relative URI.
        run = sarif["runs"][0]
        assert "originalUriBaseIds" in run
        assert "SRCROOT" in run["originalUriBaseIds"]
        assert "uri" in run["originalUriBaseIds"]["SRCROOT"]

    def test_finding_without_line_has_no_region(self):
        finding = _make_finding(line=None)
        sarif = generate_sarif([finding])
        loc = sarif["runs"][0]["results"][0]["locations"][0]["physicalLocation"]
        assert "region" not in loc


# ---------------------------------------------------------------------------
# write_sarif
# ---------------------------------------------------------------------------

class TestWriteSarif:
    def test_creates_file_with_valid_json(self, tmp_path: Path):
        findings = [_make_finding()]
        out = tmp_path / "report.sarif"
        write_sarif(findings, out, version="1.7.0")

        assert out.exists()
        data = json.loads(out.read_text())
        assert data["version"] == "2.1.0"
        assert data["runs"][0]["tool"]["driver"]["version"] == "1.7.0"
        assert len(data["runs"][0]["results"]) == 1

    def test_creates_parent_directories(self, tmp_path: Path):
        out = tmp_path / "nested" / "dir" / "report.sarif"
        write_sarif([], out)
        assert out.exists()
        data = json.loads(out.read_text())
        assert data["runs"][0]["results"] == []


# ---------------------------------------------------------------------------
# _pattern_to_rule_name
# ---------------------------------------------------------------------------

class TestPatternToRuleName:
    def test_converts_snake_to_pascal(self):
        assert _pattern_to_rule_name("buffer_overrun") == "BufferOverrun"

    def test_single_word(self):
        assert _pattern_to_rule_name("todo") == "Todo"

    def test_empty_string(self):
        assert _pattern_to_rule_name("") == ""
