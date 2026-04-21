# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

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
# Defaults — every field has a sensible fallback.
#
# Defaults are split into per-section module-level dicts and assembled
# into the public ``DEFAULTS`` symbol at the bottom of this block. This
# keeps each section's edit surface small (adding a new tier-4 detector
# is one localised change to ``_DEFAULTS_TIER4``, not a scroll-and-find
# in a 140-line dict).
# ---------------------------------------------------------------------------

_DEFAULTS_PROJECT: dict[str, Any] = {
    "name": "Project",
    "root": ".",
    "language": "cpp",
    "source_dirs": ["src/"],
    "shader_dirs": [],
    "shader_glob": "*.glsl",
    "source_extensions": [".cpp", ".h"],
    "exclude_dirs": ["external/", "build/", ".git/", ".claude/"],
    "exclude_file_patterns": [],
}

_DEFAULTS_BUILD: dict[str, Any] = {
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
}

_DEFAULTS_STATIC_ANALYSIS: dict[str, Any] = {
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
        # `-modernize-use-trailing-return-type` is deliberately excluded:
        # the rule is the most controversial in `modernize-*` (LLVM,
        # Chromium, Unreal, Godot, Folly all leave it off), and CODING_
        # STANDARDS.md mandates classical return types except where
        # trailing-return genuinely helps (templates with dependent
        # returns, lambdas, nested-type scoping). Keeping the rest of
        # `modernize-*` active so `use-nullptr`, `use-override`,
        # `use-auto` etc. still fire.
        "checks": "bugprone-*,performance-*,modernize-*,-modernize-use-trailing-return-type",
        "fallback_flags": "-std=c++17",
        "max_files": 50,
        "timeout": 600,
    },
}

_DEFAULTS_CHANGES: dict[str, Any] = {
    "base_ref": "HEAD~1",
}

_DEFAULTS_TIER4: dict[str, Any] = {
    "gpu_resource_pattern": r"GLuint\s+m_",
    "event_subscribe_pattern": r"subscribe<",
    "event_unsubscribe_pattern": r"unsubscribe",
    "complexity_threshold": 500,
    "duplication": {
        "enabled": True,
        "min_lines": 5,
        "min_tokens": 50,
        "normalize_ids": True,
        "max_findings": 50,
    },
    "refactoring": {
        "enabled": True,
        "max_params": 5,
        "max_functions_per_file": 15,
        "max_case_labels": 7,
        "max_elif_chain": 5,
        "max_nesting_depth": 4,
        "max_findings": 50,
    },
    "cognitive": {
        "enabled": True,
        "threshold": 15,
        "max_findings": 50,
    },
    # audit 2.13.0 — three new detectors (ideas #10, #26, #27)
    "copyright": {
        "enabled": True,
        "year_range_min": 2020,
        "max_findings": 50,
    },
    "dead_shaders": {
        "enabled": True,
        "max_findings": 30,
    },
    "file_read_gcount": {
        "enabled": True,
        "window_lines": 20,
        "max_findings": 30,
    },
    # audit 2.14.0 — three new detectors (ideas #18, #25, #28)
    "per_frame_alloc": {
        "enabled": True,
        "function_patterns": [
            "render", "draw", "update",
            "onframe", "onupdate", "tick",
        ],
        "max_findings": 50,
    },
    "dead_public_api": {
        "enabled": True,
        "name_blocklist": [],
        "max_findings": 50,
    },
    "token_shingle": {
        "enabled": True,
        "shingle_size": 7,
        "similarity_threshold": 0.6,
        "min_tokens": 80,
        "max_findings": 30,
    },
}

_DEFAULTS_RESEARCH: dict[str, Any] = {
    "enabled": True,
    "cache_dir": ".audit_cache",
    "cache_ttl_days": 7,
    "max_results_per_query": 3,
    "topics": [],
    "custom_queries": [],
    "nvd": {
        "enabled": False,
        "api_key": None,
        "api_key_env": "NVD_API_KEY",
        "dependencies": [],
    },
}

# Phase 2 of the audit self-learning loop (tool 2.10.0). Reads
# .audit_stats.json written by Phase 1 and lowers the severity of
# findings whose historical noise_ratio exceeds `noise_threshold`
# over at least `min_hits` observations. Safety default
# `require_zero_verified: true` means any verified hit blocks
# demotion — a rule with one real find per 50 noisy hits is still
# producing real signal, and silencing it would lose that signal.
# See lib/stats.py `compute_demotions` + `DEFAULT_DEMOTION_POLICY`.
_DEFAULTS_AUTO_DEMOTE: dict[str, Any] = {
    "enabled": True,
    "min_hits": 10,
    "noise_threshold": 0.9,
    "demote_steps": 1,
    "require_zero_verified": True,
    "exempt": [],
}

# D4 (2.6.0): Tier 6 — feature coverage sweep. Heuristic check that
# every engine/<subsystem>/ directory has at least one test file
# referencing it via #include or filename prefix.
_DEFAULTS_TIER6: dict[str, Any] = {
    "enabled": True,
    "engine_dir": "engine",
    "tests_dir": "tests",
    "excluded_subsystems": [],
    "thin_threshold": 3,
}

_DEFAULTS_REPORT: dict[str, Any] = {
    "output_path": "docs/AUTOMATED_AUDIT_REPORT.md",
    "max_findings_per_category": 100,
    "include_json_blocks": True,
    "include_token_estimate": True,
}

DEFAULTS: dict[str, Any] = {
    "project": _DEFAULTS_PROJECT,
    "build": _DEFAULTS_BUILD,
    "static_analysis": _DEFAULTS_STATIC_ANALYSIS,
    "patterns": {},
    "changes": _DEFAULTS_CHANGES,
    "tier4": _DEFAULTS_TIER4,
    "research": _DEFAULTS_RESEARCH,
    "severity_overrides": [],
    "auto_demote": _DEFAULTS_AUTO_DEMOTE,
    "tier6": _DEFAULTS_TIER6,
    "report": _DEFAULTS_REPORT,
    "tiers": [1, 2, 3, 4, 5, 6],
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
        return self.get("tiers", default=[1, 2, 3, 4, 5, 6])

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
        # Resolve root relative to the config file's directory, not CWD,
        # so the tool works regardless of where it's invoked from.
        config_dir = Path(path).resolve().parent if path else Path.cwd()
        root = (config_dir / raw.get("project", {}).get("root", ".")).resolve()

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
