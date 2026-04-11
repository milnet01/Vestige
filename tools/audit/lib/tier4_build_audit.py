"""Tier 4: Build system audit — check CMakeLists.txt for security hardening, warnings, best practices."""

from __future__ import annotations

import logging
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .config import Config
from .findings import Finding, Severity

log = logging.getLogger("audit")


@dataclass
class BuildAuditResult:
    """Results of build system analysis."""
    security_flags: list[dict[str, Any]] = field(default_factory=list)
    warning_flags: list[dict[str, Any]] = field(default_factory=list)
    best_practices: list[dict[str, Any]] = field(default_factory=list)
    cmake_files_checked: int = 0

    def to_dict(self) -> dict[str, Any]:
        return {
            "security_flags": self.security_flags,
            "warning_flags": self.warning_flags,
            "best_practices": self.best_practices,
            "cmake_files_checked": self.cmake_files_checked,
        }


# Expected security hardening flags for C/C++ projects
_SECURITY_FLAGS: list[dict[str, Any]] = [
    {
        "flag": "-fstack-protector-strong",
        "alt_flags": ["-fstack-protector", "-fstack-protector-all"],
        "description": "Stack buffer overflow protection",
        "severity": "high",
        "cwe": "CWE-121",
        "reference": "https://wiki.ubuntu.com/ToolChain/CompilerFlags#A-fstack-protector-strong",
    },
    {
        "flag": "-D_FORTIFY_SOURCE=2",
        "alt_flags": ["-D_FORTIFY_SOURCE=3", "FORTIFY_SOURCE"],
        "description": "Runtime buffer overflow detection for libc functions",
        "severity": "high",
        "cwe": "CWE-120",
        "reference": "https://www.redhat.com/en/blog/enhance-application-security-fortifysource",
    },
    {
        "flag": "-fstack-clash-protection",
        "alt_flags": [],
        "description": "Stack clash attack mitigation",
        "severity": "medium",
        "cwe": "CWE-121",
    },
    {
        "flag": "-Wl,-z,relro",
        "alt_flags": ["relro"],
        "description": "Read-only relocations (partial RELRO)",
        "severity": "high",
        "cwe": "CWE-119",
    },
    {
        "flag": "-Wl,-z,now",
        "alt_flags": [],
        "description": "Full RELRO — all GOT entries resolved at load time",
        "severity": "medium",
        "cwe": "CWE-119",
    },
    {
        "flag": "-Wl,-z,noexecstack",
        "alt_flags": ["noexecstack"],
        "description": "Non-executable stack",
        "severity": "medium",
        "cwe": "CWE-119",
    },
    {
        "flag": "-fPIE",
        "alt_flags": ["-fPIC", "-pie", "POSITION_INDEPENDENT_CODE"],
        "description": "Position-independent executable for ASLR",
        "severity": "medium",
        "cwe": "CWE-119",
    },
    {
        "flag": "-Wformat-security",
        "alt_flags": ["-Wformat=2"],
        "description": "Format string vulnerability detection",
        "severity": "medium",
        "cwe": "CWE-134",
    },
]

# Expected warning flags
_WARNING_FLAGS: list[dict[str, Any]] = [
    {
        "flag": "-Wall",
        "description": "Standard compiler warnings",
        "severity": "medium",
    },
    {
        "flag": "-Wextra",
        "description": "Extra compiler warnings",
        "severity": "medium",
    },
    {
        "flag": "-Wpedantic",
        "alt_flags": ["-pedantic"],
        "description": "Strict ISO C++ compliance warnings",
        "severity": "low",
    },
]

# CMake best practices
_BEST_PRACTICES: list[dict[str, Any]] = [
    {
        "check": "CMAKE_CXX_EXTENSIONS",
        "pattern": r"CMAKE_CXX_EXTENSIONS\s+(?:OFF|FALSE|0)",
        "description": "Disable compiler-specific extensions for portability",
        "severity": "low",
    },
    {
        "check": "CMAKE_EXPORT_COMPILE_COMMANDS",
        "pattern": r"CMAKE_EXPORT_COMPILE_COMMANDS\s+(?:ON|TRUE|1)",
        "description": "Generate compile_commands.json for IDE and tooling support",
        "severity": "info",
    },
    {
        "check": "CMAKE_BUILD_TYPE",
        "pattern": r"CMAKE_BUILD_TYPE",
        "description": "Default build type should be set (Release or RelWithDebInfo)",
        "severity": "low",
    },
]


def analyze_build_system(config: Config) -> tuple[BuildAuditResult, list[Finding]]:
    """Check build system files for security hardening and best practices."""
    result = BuildAuditResult()
    findings: list[Finding] = []

    build_system = config.get("build", "system", default="cmake")

    if build_system == "cmake":
        _audit_cmake(config, result, findings)
    elif build_system == "meson":
        _audit_meson(config, result, findings)
    # Other build systems can be added here

    return result, findings


def _audit_cmake(config: Config, result: BuildAuditResult, findings: list[Finding]) -> None:
    """Audit CMakeLists.txt files for security and best practices."""
    cmake_files: list[Path] = []
    for cmake_path in config.root.rglob("CMakeLists.txt"):
        # Skip build directories and external
        rel = str(cmake_path.relative_to(config.root))
        if any(rel.startswith(ex.rstrip("/")) for ex in config.exclude_dirs):
            continue
        cmake_files.append(cmake_path)

    if not cmake_files:
        log.info("No CMakeLists.txt found — skipping build audit")
        return

    result.cmake_files_checked = len(cmake_files)
    log.info("Build audit: checking %d CMakeLists.txt files", len(cmake_files))

    # Concatenate all CMake content for flag checking
    all_cmake_content = ""
    for cmake_file in cmake_files:
        try:
            all_cmake_content += cmake_file.read_text(errors="replace") + "\n"
        except OSError:
            continue

    if not all_cmake_content.strip():
        return

    main_cmake_rel = "CMakeLists.txt"

    # Check security flags
    for flag_def in _SECURITY_FLAGS:
        flag = flag_def["flag"]
        alt_flags = flag_def.get("alt_flags", [])
        all_flags = [flag] + alt_flags

        found = False
        for f in all_flags:
            if f in all_cmake_content:
                found = True
                break

        status = "present" if found else "missing"
        result.security_flags.append({
            "flag": flag,
            "status": status,
            "description": flag_def["description"],
            "cwe": flag_def.get("cwe", ""),
        })

        if not found:
            sev = Severity.from_string(flag_def.get("severity", "medium"))
            findings.append(Finding(
                file=main_cmake_rel,
                line=None,
                severity=sev,
                category="build_security",
                source_tier=4,
                title=f"Missing security flag: {flag}",
                detail=f"{flag_def['description']}. {flag_def.get('cwe', '')}",
                pattern_name="missing_security_flag",
            ))

    # Check warning flags
    for flag_def in _WARNING_FLAGS:
        flag = flag_def["flag"]
        alt_flags = flag_def.get("alt_flags", [])
        all_flags = [flag] + alt_flags

        found = any(f in all_cmake_content for f in all_flags)
        status = "present" if found else "missing"
        result.warning_flags.append({
            "flag": flag,
            "status": status,
            "description": flag_def["description"],
        })

        if not found:
            sev = Severity.from_string(flag_def.get("severity", "low"))
            findings.append(Finding(
                file=main_cmake_rel,
                line=None,
                severity=sev,
                category="build_quality",
                source_tier=4,
                title=f"Missing warning flag: {flag}",
                detail=flag_def["description"],
                pattern_name="missing_warning_flag",
            ))

    # Check best practices
    for bp_def in _BEST_PRACTICES:
        pattern = bp_def.get("pattern", "")
        found = bool(re.search(pattern, all_cmake_content)) if pattern else False
        status = "present" if found else "missing"
        result.best_practices.append({
            "check": bp_def["check"],
            "status": status,
            "description": bp_def["description"],
        })

        if not found:
            sev = Severity.from_string(bp_def.get("severity", "info"))
            findings.append(Finding(
                file=main_cmake_rel,
                line=None,
                severity=sev,
                category="build_quality",
                source_tier=4,
                title=f"Missing CMake best practice: {bp_def['check']}",
                detail=bp_def["description"],
                pattern_name="cmake_best_practice",
            ))

    # Check for self-referential variables (common CMake mistake)
    self_ref_pattern = re.compile(r"set\(\s*(\w+)\s+\$\{\1\}\s*\)")
    for m in self_ref_pattern.finditer(all_cmake_content):
        line_num = all_cmake_content[:m.start()].count("\n") + 1
        findings.append(Finding(
            file=main_cmake_rel,
            line=line_num,
            severity=Severity.LOW,
            category="build_quality",
            source_tier=4,
            title=f"Self-referential CMake variable: {m.group(1)}",
            detail=f"set({m.group(1)} ${{{m.group(1)}}}) is a no-op",
            pattern_name="cmake_self_ref",
        ))

    log.info("Build audit: %d security flags checked, %d findings",
             len(_SECURITY_FLAGS), len(findings))


def _audit_meson(config: Config, result: BuildAuditResult, findings: list[Finding]) -> None:
    """Audit meson.build for security and best practices."""
    meson_file = config.root / "meson.build"
    if not meson_file.exists():
        return

    try:
        content = meson_file.read_text(errors="replace")
    except OSError:
        return

    result.cmake_files_checked = 1  # Reusing field for meson

    # Check for hardening flags in meson
    hardening_checks = [
        ("b_pie", "Position-independent executable", "high"),
        ("b_staticpic", "Static position-independent code", "medium"),
    ]

    for check_name, description, severity in hardening_checks:
        if check_name not in content:
            findings.append(Finding(
                file="meson.build",
                line=None,
                severity=Severity.from_string(severity),
                category="build_security",
                source_tier=4,
                title=f"Missing meson hardening: {check_name}",
                detail=description,
                pattern_name="missing_meson_hardening",
            ))
