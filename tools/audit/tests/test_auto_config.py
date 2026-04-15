# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tests for lib.auto_config — language detection, build system detection, _get_language_defaults."""

from __future__ import annotations

from pathlib import Path

import pytest

from lib.auto_config import (
    BUILD_SYSTEMS,
    LANGUAGE_SIGNATURES,
    _detect_build_system,
    _detect_language,
    _detect_source_dirs,
    _get_language_defaults,
    detect_project,
)


# ---------------------------------------------------------------------------
# Language detection
# ---------------------------------------------------------------------------


class TestDetectLanguage:
    """_detect_language should identify the primary language from marker files."""

    def test_rust_from_cargo_toml(self, tmp_path: Path):
        (tmp_path / "Cargo.toml").write_text("[package]\nname = 'test'")
        assert _detect_language(tmp_path) == "rust"

    def test_go_from_go_mod(self, tmp_path: Path):
        (tmp_path / "go.mod").write_text("module test")
        assert _detect_language(tmp_path) == "go"

    def test_cpp_from_cmakelists(self, tmp_path: Path):
        (tmp_path / "CMakeLists.txt").write_text("project(test)")
        assert _detect_language(tmp_path) == "cpp"

    def test_python_from_setup_py(self, tmp_path: Path):
        (tmp_path / "setup.py").write_text("from setuptools import setup")
        assert _detect_language(tmp_path) == "python"

    def test_python_from_pyproject(self, tmp_path: Path):
        (tmp_path / "pyproject.toml").write_text("[build-system]")
        assert _detect_language(tmp_path) == "python"

    def test_java_from_pom(self, tmp_path: Path):
        (tmp_path / "pom.xml").write_text("<project></project>")
        assert _detect_language(tmp_path) == "java"

    def test_js_from_package_json(self, tmp_path: Path):
        (tmp_path / "package.json").write_text("{}")
        assert _detect_language(tmp_path) == "js"

    def test_ts_from_tsconfig(self, tmp_path: Path):
        (tmp_path / "tsconfig.json").write_text("{}")
        assert _detect_language(tmp_path) == "ts"

    def test_fallback_to_cpp(self, tmp_path: Path):
        # No marker files at all
        assert _detect_language(tmp_path) == "cpp"

    def test_cpp_from_glob_match(self, tmp_path: Path):
        (tmp_path / "main.cpp").write_text("int main() {}")
        assert _detect_language(tmp_path) == "cpp"


# ---------------------------------------------------------------------------
# Build system detection
# ---------------------------------------------------------------------------


class TestDetectBuildSystem:
    """_detect_build_system should identify the build system from markers."""

    def test_cmake_detected(self, tmp_path: Path):
        (tmp_path / "CMakeLists.txt").write_text("project(test)")
        name, config = _detect_build_system(tmp_path, "cpp")
        assert name == "cmake"
        assert config["system"] == "cmake"
        assert "cmake" in config.get("build_cmd", "")

    def test_meson_detected(self, tmp_path: Path):
        (tmp_path / "meson.build").write_text("project('test')")
        name, config = _detect_build_system(tmp_path, "cpp")
        assert name == "meson"

    def test_makefile_detected(self, tmp_path: Path):
        (tmp_path / "Makefile").write_text("all: build")
        name, config = _detect_build_system(tmp_path, "cpp")
        assert name == "make"

    def test_cargo_detected(self, tmp_path: Path):
        (tmp_path / "Cargo.toml").write_text("[package]")
        name, config = _detect_build_system(tmp_path, "rust")
        assert name == "cargo"

    def test_npm_detected(self, tmp_path: Path):
        (tmp_path / "package.json").write_text("{}")
        name, config = _detect_build_system(tmp_path, "js")
        assert name == "npm"

    def test_no_build_system(self, tmp_path: Path):
        name, config = _detect_build_system(tmp_path, "cpp")
        assert name is None
        assert config["system"] == "none"

    def test_build_config_has_required_keys(self, tmp_path: Path):
        (tmp_path / "CMakeLists.txt").write_text("project(test)")
        _, config = _detect_build_system(tmp_path, "cpp")
        required_keys = ["system", "build_dir", "build_cmd", "warning_regex"]
        for key in required_keys:
            assert key in config


# ---------------------------------------------------------------------------
# _get_language_defaults
# ---------------------------------------------------------------------------


class TestGetLanguageDefaults:
    """_get_language_defaults should return language-specific patterns and settings."""

    @pytest.mark.parametrize("lang", ["cpp", "c", "python", "rust", "js", "ts", "java", "go"])
    def test_known_language_returns_dict(self, lang: str):
        defaults = _get_language_defaults(lang)
        assert isinstance(defaults, dict)
        assert "source_extensions" in defaults

    def test_cpp_has_patterns(self):
        defaults = _get_language_defaults("cpp")
        assert "patterns" in defaults
        patterns = defaults["patterns"]
        assert "memory_safety" in patterns

    def test_python_has_security_patterns(self):
        defaults = _get_language_defaults("python")
        assert "patterns" in defaults
        patterns = defaults["patterns"]
        assert "security" in patterns

    def test_unknown_language_falls_back_to_cpp(self):
        defaults = _get_language_defaults("unknown_lang")
        cpp_defaults = _get_language_defaults("cpp")
        assert defaults == cpp_defaults

    def test_cpp_has_static_analysis(self):
        defaults = _get_language_defaults("cpp")
        sa = defaults.get("static_analysis", {})
        assert sa.get("cppcheck", {}).get("enabled") is True

    def test_python_disables_cppcheck(self):
        defaults = _get_language_defaults("python")
        sa = defaults.get("static_analysis", {})
        assert sa.get("cppcheck", {}).get("enabled") is False


# ---------------------------------------------------------------------------
# detect_project (integration)
# ---------------------------------------------------------------------------


class TestDetectProject:
    """detect_project should auto-detect project settings."""

    def test_cpp_project(self, tmp_path: Path):
        (tmp_path / "CMakeLists.txt").write_text("project(TestEngine)")
        src = tmp_path / "src"
        src.mkdir()
        (src / "main.cpp").write_text("int main() { return 0; }")

        config = detect_project(tmp_path)
        assert config["project"]["language"] == "cpp"
        assert config["build"]["system"] == "cmake"
        assert "src/" in config["project"]["source_dirs"]

    def test_python_project(self, tmp_path: Path):
        (tmp_path / "pyproject.toml").write_text("[build-system]")
        lib = tmp_path / "lib"
        lib.mkdir()
        (lib / "app.py").write_text("def main(): pass")

        config = detect_project(tmp_path)
        assert config["project"]["language"] == "python"


# ---------------------------------------------------------------------------
# _detect_source_dirs
# ---------------------------------------------------------------------------


class TestDetectSourceDirs:
    """_detect_source_dirs should find directories with source files."""

    def test_finds_src_dir(self, tmp_path: Path):
        src = tmp_path / "src"
        src.mkdir()
        (src / "main.cpp").write_text("int main() {}")
        dirs = _detect_source_dirs(tmp_path, [".cpp"])
        assert "src/" in dirs

    def test_excludes_build_dir(self, tmp_path: Path):
        build = tmp_path / "build"
        build.mkdir()
        (build / "generated.cpp").write_text("// generated")
        dirs = _detect_source_dirs(tmp_path, [".cpp"])
        assert "build/" not in dirs

    def test_empty_project_defaults_to_src(self, tmp_path: Path):
        dirs = _detect_source_dirs(tmp_path, [".cpp"])
        assert dirs == ["src/"]
