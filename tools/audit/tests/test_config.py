# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tests for lib.config — load_config, Config accessors, _detect_tools."""

from __future__ import annotations

from pathlib import Path
from unittest.mock import patch

import pytest
import yaml

from lib.config import Config, DEFAULTS, _deep_merge, _detect_tools, load_config


# ---------------------------------------------------------------------------
# load_config with None path
# ---------------------------------------------------------------------------


class TestLoadConfigDefaults:
    """load_config(None) should produce a Config using DEFAULTS."""

    def test_none_path_uses_defaults(self):
        cfg = load_config(None)
        assert cfg.project_name == "Project"
        assert cfg.language == "cpp"

    def test_none_path_source_dirs(self):
        cfg = load_config(None)
        assert cfg.source_dirs == ["src/"]

    def test_none_path_root_resolves(self):
        cfg = load_config(None)
        # root should be an absolute resolved path
        assert cfg.root.is_absolute()


# ---------------------------------------------------------------------------
# load_config with YAML file
# ---------------------------------------------------------------------------


class TestLoadConfigYAML:
    """load_config with a YAML file should merge over defaults."""

    def test_yaml_overrides_project_name(self, tmp_path: Path):
        yaml_file = tmp_path / "audit.yaml"
        yaml_file.write_text(yaml.dump({
            "project": {"name": "MyEngine"},
        }))
        cfg = load_config(str(yaml_file), project_root=tmp_path)
        assert cfg.project_name == "MyEngine"

    def test_yaml_preserves_defaults_for_missing_keys(self, tmp_path: Path):
        yaml_file = tmp_path / "audit.yaml"
        yaml_file.write_text(yaml.dump({
            "project": {"name": "Custom"},
        }))
        cfg = load_config(str(yaml_file), project_root=tmp_path)
        # language is not in the YAML, should come from defaults
        assert cfg.language == "cpp"

    def test_yaml_deep_merge(self, tmp_path: Path):
        yaml_file = tmp_path / "audit.yaml"
        yaml_file.write_text(yaml.dump({
            "project": {"name": "Merged", "shader_dirs": ["shaders/"]},
        }))
        cfg = load_config(str(yaml_file), project_root=tmp_path)
        assert cfg.project_name == "Merged"
        assert cfg.shader_dirs == ["shaders/"]
        # source_dirs should still be default
        assert cfg.source_dirs == ["src/"]

    def test_missing_yaml_raises_system_exit(self, tmp_path: Path):
        with pytest.raises(SystemExit):
            load_config(str(tmp_path / "nonexistent.yaml"))

    def test_project_root_override(self, tmp_path: Path):
        yaml_file = tmp_path / "audit.yaml"
        yaml_file.write_text(yaml.dump({"project": {"name": "X"}}))
        cfg = load_config(str(yaml_file), project_root=tmp_path)
        assert cfg.root == tmp_path.resolve()


# ---------------------------------------------------------------------------
# Config.get() dot-path access
# ---------------------------------------------------------------------------


class TestConfigGet:
    """Config.get() should navigate nested dicts."""

    def test_single_key(self):
        cfg = Config(raw={"tiers": [1, 2]})
        assert cfg.get("tiers") == [1, 2]

    def test_nested_keys(self):
        cfg = Config(raw={"project": {"name": "TestProj"}})
        assert cfg.get("project", "name") == "TestProj"

    def test_deeply_nested(self):
        cfg = Config(raw={"static_analysis": {"cppcheck": {"enabled": True}}})
        assert cfg.get("static_analysis", "cppcheck", "enabled") is True

    def test_missing_key_returns_default(self):
        cfg = Config(raw={"project": {"name": "X"}})
        assert cfg.get("project", "nonexistent", default="fallback") == "fallback"

    def test_missing_intermediate_key(self):
        cfg = Config(raw={})
        assert cfg.get("a", "b", "c", default=42) == 42

    def test_default_is_none_when_not_specified(self):
        cfg = Config(raw={})
        assert cfg.get("missing") is None


# ---------------------------------------------------------------------------
# Config properties
# ---------------------------------------------------------------------------


class TestConfigProperties:
    """Config properties should map to expected raw dict paths."""

    def test_project_name(self, sample_config):
        assert sample_config.project_name == "TestProject"

    def test_language(self, sample_config):
        assert sample_config.language == "cpp"

    def test_source_dirs(self, sample_config):
        assert sample_config.source_dirs == ["src/"]

    def test_shader_dirs_default_empty(self, sample_config):
        assert sample_config.shader_dirs == []

    def test_source_extensions(self, sample_config):
        assert ".cpp" in sample_config.source_extensions
        assert ".h" in sample_config.source_extensions

    def test_exclude_dirs(self, sample_config):
        assert sample_config.exclude_dirs == []

    def test_enabled_tiers(self, sample_config):
        assert sample_config.enabled_tiers == [1, 2, 3, 4, 5]

    def test_patterns(self, sample_config):
        pats = sample_config.patterns
        assert "memory" in pats
        assert "maintenance" in pats

    def test_report_path(self, sample_config):
        rp = sample_config.report_path
        assert rp.name == "AUTOMATED_AUDIT_REPORT.md"
        assert "docs" in str(rp)


# ---------------------------------------------------------------------------
# _detect_tools
# ---------------------------------------------------------------------------


class TestDetectTools:
    """_detect_tools should enable/disable tools based on shutil.which."""

    def test_cppcheck_found(self):
        raw = _deep_merge(DEFAULTS, {})
        with patch("lib.config.shutil.which", return_value="/usr/bin/cppcheck"):
            _detect_tools(raw)
        cpp = raw["static_analysis"]["cppcheck"]
        assert cpp["enabled"] is True
        assert cpp["binary"] == "/usr/bin/cppcheck"

    def test_cppcheck_not_found_disables(self):
        import copy
        raw = copy.deepcopy(DEFAULTS)
        raw["static_analysis"]["cppcheck"]["enabled"] = True
        raw["static_analysis"]["cppcheck"]["binary"] = None
        with patch("lib.config.shutil.which", return_value=None):
            _detect_tools(raw)
        cpp = raw["static_analysis"]["cppcheck"]
        assert cpp["enabled"] is False

    def test_cppcheck_already_has_binary_skips_detection(self):
        raw = _deep_merge(DEFAULTS, {
            "static_analysis": {"cppcheck": {"binary": "/custom/cppcheck"}},
        })
        with patch("lib.config.shutil.which") as mock_which:
            _detect_tools(raw)
            # shutil.which should NOT be called for cppcheck because binary is set
            # (it may be called for clang-tidy though)
            for call_args in mock_which.call_args_list:
                assert call_args[0][0] != "cppcheck"

    def test_clang_tidy_found_when_enabled(self):
        raw = _deep_merge(DEFAULTS, {
            "static_analysis": {"clang_tidy": {"enabled": True}},
        })
        with patch("lib.config.shutil.which", return_value="/usr/bin/clang-tidy"):
            _detect_tools(raw)
        ct = raw["static_analysis"]["clang_tidy"]
        assert ct["enabled"] is True
        assert ct["binary"] == "/usr/bin/clang-tidy"

    def test_clang_tidy_not_found_disables(self):
        raw = _deep_merge(DEFAULTS, {
            "static_analysis": {"clang_tidy": {"enabled": True}},
        })
        with patch("lib.config.shutil.which", return_value=None):
            _detect_tools(raw)
        ct = raw["static_analysis"]["clang_tidy"]
        assert ct["enabled"] is False

    def test_clang_tidy_disabled_by_default_not_touched(self):
        raw = _deep_merge(DEFAULTS, {})
        assert raw["static_analysis"]["clang_tidy"]["enabled"] is False
        with patch("lib.config.shutil.which") as mock_which:
            _detect_tools(raw)
        # When clang-tidy is disabled, shutil.which should not be called for it
        for call_args in mock_which.call_args_list:
            assert call_args[0][0] != "clang-tidy"


# ---------------------------------------------------------------------------
# _deep_merge helper
# ---------------------------------------------------------------------------


class TestDeepMerge:

    def test_overlay_overrides_scalar(self):
        result = _deep_merge({"a": 1}, {"a": 2})
        assert result["a"] == 2

    def test_nested_dicts_merge(self):
        base = {"x": {"a": 1, "b": 2}}
        over = {"x": {"b": 3, "c": 4}}
        result = _deep_merge(base, over)
        assert result["x"] == {"a": 1, "b": 3, "c": 4}

    def test_new_keys_added(self):
        result = _deep_merge({"a": 1}, {"b": 2})
        assert result == {"a": 1, "b": 2}

    def test_base_not_mutated(self):
        base = {"a": 1}
        _deep_merge(base, {"a": 2})
        assert base["a"] == 1
