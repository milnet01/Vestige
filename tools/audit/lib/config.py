"""Configuration loading, validation, and defaults."""

from __future__ import annotations

import logging
import shutil
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml

log = logging.getLogger("audit")


def _deep_merge(base: dict, overlay: dict) -> dict:
    """Recursively merge overlay into base, returning a new dict."""
    result = dict(base)
    for key, val in overlay.items():
        if key in result and isinstance(result[key], dict) and isinstance(val, dict):
            result[key] = _deep_merge(result[key], val)
        else:
            result[key] = val
    return result


# ---------------------------------------------------------------------------
# Defaults — every field has a sensible fallback
# ---------------------------------------------------------------------------

DEFAULTS: dict[str, Any] = {
    "project": {
        "name": "Project",
        "root": ".",
        "language": "cpp",
        "source_dirs": ["src/"],
        "shader_dirs": [],
        "shader_glob": "*.glsl",
        "source_extensions": [".cpp", ".h"],
        "exclude_dirs": ["external/", "build/", ".git/", ".claude/"],
        "exclude_file_patterns": [],
    },
    "build": {
        "system": "cmake",
        "build_dir": "build",
        "configure_cmd": None,
        "build_cmd": None,
        "warning_regex": r"warning:|error:",
        "test_cmd": None,
        "sanitizer": {
            "enabled": False,
            "configure_cmd": None,
            "build_cmd": None,
            "test_cmd": None,
        },
    },
    "static_analysis": {
        "cppcheck": {
            "enabled": True,
            "binary": None,
            "args": "--enable=all --std=c++17 --suppress=missingIncludeSystem --suppress=unusedFunction",
            "output_format": "xml",
            "targets": [],
            "timeout": 600,
        },
        "clang_tidy": {
            "enabled": False,
            "binary": None,
            "compile_commands": None,
            "checks": "bugprone-*,performance-*,modernize-*",
            "fallback_flags": "-std=c++17",
            "max_files": 50,
            "timeout": 600,
        },
    },
    "patterns": {},
    "changes": {
        "base_ref": "HEAD~1",
    },
    "tier4": {
        "gpu_resource_pattern": r"GLuint\s+m_",
        "event_subscribe_pattern": r"subscribe<",
        "event_unsubscribe_pattern": r"unsubscribe",
        "complexity_threshold": 500,
    },
    "research": {
        "enabled": True,
        "cache_dir": ".audit_cache",
        "cache_ttl_days": 7,
        "max_results_per_query": 3,
        "topics": [],
        "custom_queries": [],
    },
    "report": {
        "output_path": "docs/AUTOMATED_AUDIT_REPORT.md",
        "max_findings_per_category": 100,
        "include_json_blocks": True,
        "include_token_estimate": True,
    },
    "tiers": [1, 2, 3, 4, 5],
}


@dataclass
class Config:
    """Resolved audit configuration."""
    raw: dict[str, Any] = field(default_factory=dict)
    root: Path = field(default_factory=lambda: Path("."))

    # Convenience accessors
    def get(self, *keys: str, default: Any = None) -> Any:
        """Dot-path access: config.get('project', 'name')"""
        node = self.raw
        for k in keys:
            if isinstance(node, dict) and k in node:
                node = node[k]
            else:
                return default
        return node

    @property
    def project_name(self) -> str:
        return self.get("project", "name", default="Project")

    @property
    def language(self) -> str:
        return self.get("project", "language", default="cpp")

    @property
    def source_dirs(self) -> list[str]:
        return self.get("project", "source_dirs", default=["src/"])

    @property
    def shader_dirs(self) -> list[str]:
        return self.get("project", "shader_dirs", default=[])

    @property
    def source_extensions(self) -> list[str]:
        return self.get("project", "source_extensions", default=[".cpp", ".h"])

    @property
    def exclude_dirs(self) -> list[str]:
        return self.get("project", "exclude_dirs", default=[])

    @property
    def enabled_tiers(self) -> list[int]:
        return self.get("tiers", default=[1, 2, 3, 4, 5])

    @property
    def patterns(self) -> dict[str, list[dict]]:
        return self.get("patterns", default={})

    @property
    def report_path(self) -> Path:
        return self.root / self.get("report", "output_path",
                                     default="docs/AUTOMATED_AUDIT_REPORT.md")


def load_config(path: str | Path | None, project_root: str | Path | None = None) -> Config:
    """Load YAML config, merge with defaults, resolve paths."""
    raw = dict(DEFAULTS)

    if path is not None:
        config_path = Path(path)
        if not config_path.exists():
            log.error("Config file not found: %s", config_path)
            raise SystemExit(1)
        with open(config_path, "r") as f:
            user_raw = yaml.safe_load(f) or {}
        raw = _deep_merge(raw, user_raw)

    # Resolve project root
    if project_root:
        root = Path(project_root).resolve()
    else:
        root = Path(raw.get("project", {}).get("root", ".")).resolve()

    # Auto-detect tools
    _detect_tools(raw)

    config = Config(raw=raw, root=root)
    log.info("Config loaded: project=%s, root=%s", config.project_name, config.root)
    return config


def _detect_tools(raw: dict) -> None:
    """Auto-detect binary paths for analysis tools."""
    sa = raw.get("static_analysis", {})

    # cppcheck
    cpp = sa.get("cppcheck", {})
    if cpp.get("enabled") and not cpp.get("binary"):
        found = shutil.which("cppcheck")
        if found:
            cpp["binary"] = found
        else:
            log.warning("cppcheck not found — Tier 1 cppcheck will be skipped")
            cpp["enabled"] = False

    # clang-tidy
    ct = sa.get("clang_tidy", {})
    if ct.get("enabled") and not ct.get("binary"):
        found = shutil.which("clang-tidy")
        if found:
            ct["binary"] = found
        else:
            log.warning("clang-tidy not found — Tier 1 clang-tidy will be skipped")
            ct["enabled"] = False


def apply_pattern_preset(raw: dict, preset: str, language: str) -> None:
    """Override the patterns section based on a named preset.

    Presets: strict (all), relaxed (HIGH+ only), security, performance.
    """
    from .findings import Severity
    from .auto_config import _get_language_defaults

    lang_defaults = _get_language_defaults(language)
    all_patterns = lang_defaults.get("patterns", {})

    if preset == "strict":
        raw["patterns"] = all_patterns
    elif preset == "relaxed":
        filtered: dict = {}
        for cat, pats in all_patterns.items():
            high_pats = [p for p in pats
                         if Severity.from_string(p.get("severity", "info")) <= Severity.HIGH]
            if high_pats:
                filtered[cat] = high_pats
        raw["patterns"] = filtered
    elif preset == "security":
        raw["patterns"] = {k: v for k, v in all_patterns.items()
                           if k in ("memory_safety", "security")}
    elif preset == "performance":
        raw["patterns"] = {k: v for k, v in all_patterns.items()
                           if k in ("performance",)}
