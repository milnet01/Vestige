"""SARIF 2.1.0 output — convert audit findings to SARIF format."""

from __future__ import annotations

import json
from pathlib import Path

from .findings import Finding, Severity

SARIF_SCHEMA = (
    "https://raw.githubusercontent.com/oasis-tcs/sarif-spec/main/"
    "sarif-2.1/schema/sarif-schema-2.1.0.json"
)
TOOL_INFO_URI = "https://github.com/vestige-engine/audit-tool"

_SEVERITY_MAP: dict[Severity, str] = {
    Severity.CRITICAL: "error",
    Severity.HIGH: "error",
    Severity.MEDIUM: "warning",
    Severity.LOW: "note",
    Severity.INFO: "note",
}


def _severity_to_level(severity: Severity) -> str:
    return _SEVERITY_MAP.get(severity, "note")


def _pattern_to_rule_name(pattern_name: str) -> str:
    """Convert snake_case pattern_name to PascalCase rule name."""
    return "".join(part.capitalize() for part in pattern_name.split("_")) if pattern_name else ""


def _build_rules(findings: list[Finding]) -> list[dict]:
    """Deduplicate findings by pattern_name and build SARIF rule entries."""
    seen: dict[str, dict] = {}
    for f in findings:
        key = f.pattern_name or f.title
        if key in seen:
            continue
        seen[key] = {
            "id": key,
            "name": _pattern_to_rule_name(key),
            "shortDescription": {"text": f.title},
            "defaultConfiguration": {"level": _severity_to_level(f.severity)},
            "properties": {
                "category": f.category,
                "tier": f.source_tier,
            },
        }
    return list(seen.values())


def _build_result(finding: Finding) -> dict:
    """Convert a single Finding to a SARIF result object."""
    message_parts = [finding.title]
    if finding.detail:
        message_parts.append(finding.detail)
    message_text = ": ".join(message_parts)

    result: dict = {
        "ruleId": finding.pattern_name or finding.title,
        "level": _severity_to_level(finding.severity),
        "message": {"text": message_text},
    }

    if finding.file:
        location: dict = {
            "physicalLocation": {
                "artifactLocation": {
                    "uri": finding.file,
                    "uriBaseId": "%SRCROOT%",
                },
            },
        }
        if finding.line is not None:
            location["physicalLocation"]["region"] = {"startLine": finding.line}
        result["locations"] = [location]

    return result


def generate_sarif(findings: list[Finding], version: str = "2.0.0") -> dict:
    """Convert findings list to SARIF 2.1.0 JSON structure."""
    return {
        "$schema": SARIF_SCHEMA,
        "version": "2.1.0",
        "runs": [
            {
                "tool": {
                    "driver": {
                        "name": "vestige-audit",
                        "version": version,
                        "informationUri": TOOL_INFO_URI,
                        "rules": _build_rules(findings),
                    },
                },
                "results": [_build_result(f) for f in findings],
            },
        ],
    }


def write_sarif(
    findings: list[Finding],
    output_path: Path,
    version: str = "2.0.0",
) -> None:
    """Write SARIF JSON to file."""
    sarif = generate_sarif(findings, version=version)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(sarif, indent=2) + "\n")
