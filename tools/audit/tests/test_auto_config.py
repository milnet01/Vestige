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
    _detect_cmake_deps,
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


class TestCppAudit202604019Rules:
    """Rules added as part of the 2026-04-19 Batch-5 detector close-out.

    Each rule's regex is exercised against one positive example that
    should fire and one negative example (often the same code with the
    Vestige::JsonSizeCap helper in scope) that must not. Names mirror
    the audit report's detector IDs. (AUDIT 2026-04-19 §detectors.)
    """

    def _find_rule(self, name: str) -> dict:
        patterns = _get_language_defaults("cpp")["patterns"]
        for category, rule_list in patterns.items():
            for rule in rule_list:
                if rule.get("name") == name:
                    return rule
        raise AssertionError(f"rule '{name}' not found in cpp patterns")

    def _hits(self, rule: dict, line: str) -> bool:
        import re
        pat = re.compile(rule["pattern"])
        excl_src = rule.get("exclude_pattern", "")
        excl = re.compile(excl_src) if excl_src else None
        if not pat.search(line):
            return False
        if excl and excl.search(line):
            return False
        return True

    def test_json_parse_no_size_cap_fires_on_bare_parse(self):
        rule = self._find_rule("json_parse_no_size_cap")
        assert self._hits(rule, "json j = json::parse(file);")
        assert self._hits(rule, "nlohmann::json j = nlohmann::json::parse(inputFile, nullptr, false);")

    def test_json_parse_no_size_cap_skips_jsonsizecap_wrapper(self):
        rule = self._find_rule("json_parse_no_size_cap")
        # Lines that route through the helper mention it on the same row;
        # the exclude_pattern must suppress the warning for those.
        assert not self._hits(rule, "auto j = JsonSizeCap::loadJsonWithSizeCap(path, \"X\");")

    def test_text_load_no_size_cap_fires_on_rdbuf_idiom(self):
        rule = self._find_rule("text_load_no_size_cap")
        assert self._hits(rule, "vertexStream << vertexFile.rdbuf();")

    def test_text_load_no_size_cap_skips_helper(self):
        rule = self._find_rule("text_load_no_size_cap")
        assert not self._hits(
            rule,
            "auto txt = JsonSizeCap::loadTextFileWithSizeCap(path, \"Shader\");",
        )

    def test_uniform_long_literal_fires_on_long_name(self):
        rule = self._find_rule("uniform_long_literal")
        assert self._hits(rule, 'shader.setFloat("u_pointLights_position_0", x);')
        assert self._hits(rule, 'm_sceneShader.setMat4("u_cascade_lightSpaceMatrices_0", m);')

    def test_uniform_long_literal_skips_short_name(self):
        rule = self._find_rule("uniform_long_literal")
        assert not self._hits(rule, 'shader.setInt("u_foo", 1);')

    def test_string_concat_to_string_fires(self):
        rule = self._find_rule("string_concat_to_string_in_loop")
        assert self._hits(rule, 'Logger::info("Loop iter: " + std::to_string(i));')

    def test_eventbus_subscribe_this_fires(self):
        rule = self._find_rule("eventbus_subscribe_this")
        assert self._hits(
            rule,
            "m_eventBus.subscribe<KeyPressedEvent>([this](const KeyPressedEvent& e) {});",
        )

    def test_gamepad_axis_no_clamp_fires_on_raw_read(self):
        rule = self._find_rule("gamepad_axis_no_clamp")
        assert self._hits(
            rule,
            "float lx = state.axes[GLFW_GAMEPAD_AXIS_LEFT_X];",
        )

    def test_gamepad_axis_no_clamp_skips_clamped_read(self):
        rule = self._find_rule("gamepad_axis_no_clamp")
        assert not self._hits(
            rule,
            "float lx = std::clamp(state.axes[GLFW_GAMEPAD_AXIS_LEFT_X], -1.0f, 1.0f);",
        )


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


# ---------------------------------------------------------------------------
# CMake dependency detection — D5 (2.8.0) version-pin extraction
# ---------------------------------------------------------------------------


class TestDetectCmakeDeps:
    """_detect_cmake_deps should pull names + pinned versions from
    find_package and FetchContent_Declare blocks."""

    def test_no_cmakelists_returns_empty(self, tmp_path: Path):
        assert _detect_cmake_deps(tmp_path) == []

    def test_find_package_with_version(self, tmp_path: Path):
        (tmp_path / "CMakeLists.txt").write_text(
            "find_package(Boost 1.74)\n"
        )
        deps = _detect_cmake_deps(tmp_path)
        assert deps == [{"name": "Boost", "version": "1.74"}]

    def test_find_package_without_version(self, tmp_path: Path):
        (tmp_path / "CMakeLists.txt").write_text(
            "find_package(Threads REQUIRED)\n"
        )
        deps = _detect_cmake_deps(tmp_path)
        assert deps == [{"name": "Threads", "version": ""}]

    def test_fetchcontent_with_git_tag(self, tmp_path: Path):
        """FetchContent_Declare with GIT_TAG must capture both name and version."""
        (tmp_path / "CMakeLists.txt").write_text(
            "FetchContent_Declare(\n"
            "    glfw\n"
            "    GIT_REPOSITORY https://github.com/glfw/glfw.git\n"
            "    GIT_TAG        3.4\n"
            "    GIT_SHALLOW    TRUE\n"
            ")\n"
        )
        deps = _detect_cmake_deps(tmp_path)
        assert deps == [{"name": "glfw", "version": "3.4"}]

    def test_fetchcontent_with_git_tag_v_prefix(self, tmp_path: Path):
        """A `v` prefix on GIT_TAG (e.g. v1.2.3) is captured verbatim."""
        (tmp_path / "CMakeLists.txt").write_text(
            "FetchContent_Declare(joltphysics GIT_TAG v5.2.0)\n"
        )
        deps = _detect_cmake_deps(tmp_path)
        assert deps == [{"name": "joltphysics", "version": "v5.2.0"}]

    def test_fetchcontent_with_freetype_style_tag(self, tmp_path: Path):
        """FreeType uses VER-N-N-N tag style; captured verbatim."""
        (tmp_path / "CMakeLists.txt").write_text(
            "FetchContent_Declare(\n"
            "    freetype\n"
            "    GIT_TAG VER-2-13-3\n"
            ")\n"
        )
        deps = _detect_cmake_deps(tmp_path)
        assert deps == [{"name": "freetype", "version": "VER-2-13-3"}]

    def test_fetchcontent_with_url_extracts_version(self, tmp_path: Path):
        """URL form (e.g. nlohmann/json release tarball) extracts the
        version-shaped path component."""
        (tmp_path / "CMakeLists.txt").write_text(
            "FetchContent_Declare(json\n"
            "    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz\n"
            ")\n"
        )
        deps = _detect_cmake_deps(tmp_path)
        assert deps == [{"name": "json", "version": "v3.12.0"}]

    def test_fetchcontent_branch_tag_yields_blank(self, tmp_path: Path):
        """A branch name like `master` or `docking` isn't a version; we
        capture it as the version literal anyway since git accepts it
        and the consumer (NVD lookup) will treat blank-or-non-numeric
        as 'no version filter'."""
        (tmp_path / "CMakeLists.txt").write_text(
            "FetchContent_Declare(imgui GIT_TAG docking)\n"
        )
        deps = _detect_cmake_deps(tmp_path)
        # We capture the literal — caller decides what to do with it.
        assert deps == [{"name": "imgui", "version": "docking"}]

    def test_multiple_fetchcontent_blocks(self, tmp_path: Path):
        """Multiple FetchContent_Declare blocks in one file must each be parsed."""
        (tmp_path / "CMakeLists.txt").write_text(
            "FetchContent_Declare(\n"
            "    glfw\n"
            "    GIT_TAG 3.4\n"
            ")\n"
            "FetchContent_Declare(\n"
            "    glm\n"
            "    GIT_TAG 1.0.1\n"
            ")\n"
            "FetchContent_Declare(\n"
            "    googletest\n"
            "    GIT_TAG v1.15.2\n"
            ")\n"
        )
        deps = _detect_cmake_deps(tmp_path)
        names = {d["name"] for d in deps}
        assert names == {"glfw", "glm", "googletest"}
        versions = {d["name"]: d["version"] for d in deps}
        assert versions["glfw"] == "3.4"
        assert versions["glm"] == "1.0.1"
        assert versions["googletest"] == "v1.15.2"

    def test_mixed_find_package_and_fetchcontent(self, tmp_path: Path):
        (tmp_path / "CMakeLists.txt").write_text(
            "find_package(OpenGL 4.5 REQUIRED)\n"
            "FetchContent_Declare(glfw GIT_TAG 3.4)\n"
        )
        deps = _detect_cmake_deps(tmp_path)
        assert {"name": "OpenGL", "version": "4.5"} in deps
        assert {"name": "glfw", "version": "3.4"} in deps

    def test_dedup_keeps_first_occurrence(self, tmp_path: Path):
        """If the same dep is declared twice (e.g. nested CMakeLists),
        the first occurrence wins."""
        (tmp_path / "CMakeLists.txt").write_text(
            "FetchContent_Declare(glfw GIT_TAG 3.4)\n"
            "FetchContent_Declare(glfw GIT_TAG 3.5)\n"
        )
        deps = _detect_cmake_deps(tmp_path)
        assert deps == [{"name": "glfw", "version": "3.4"}]

    def test_dedup_case_insensitive(self, tmp_path: Path):
        """`Boost` and `boost` shouldn't both appear."""
        (tmp_path / "CMakeLists.txt").write_text(
            "find_package(Boost 1.74)\n"
            "FetchContent_Declare(boost GIT_TAG 1.85)\n"
        )
        deps = _detect_cmake_deps(tmp_path)
        assert len(deps) == 1
        assert deps[0]["name"] == "Boost"

    def test_recurses_into_subdirs(self, tmp_path: Path):
        """Nested CMakeLists.txt files (typical for external/) are found."""
        (tmp_path / "CMakeLists.txt").write_text(
            "find_package(OpenGL REQUIRED)\n"
        )
        (tmp_path / "external").mkdir()
        (tmp_path / "external" / "CMakeLists.txt").write_text(
            "FetchContent_Declare(glfw GIT_TAG 3.4)\n"
        )
        deps = _detect_cmake_deps(tmp_path)
        names = {d["name"] for d in deps}
        assert "OpenGL" in names
        assert "glfw" in names

    def test_no_runaway_match_across_blocks(self, tmp_path: Path):
        """The regex must not match across separate FetchContent_Declare
        blocks (would happen with naive `.*` instead of `[^)]`)."""
        (tmp_path / "CMakeLists.txt").write_text(
            "FetchContent_Declare(\n"
            "    a\n"
            "    GIT_REPOSITORY https://example.com/a.git\n"
            ")\n"
            "FetchContent_Declare(\n"
            "    b\n"
            "    GIT_TAG 2.0\n"
            ")\n"
        )
        deps = _detect_cmake_deps(tmp_path)
        # `a` has no GIT_TAG; should yield "" not "2.0" from leaking
        # across to `b`'s block.
        a_dep = next(d for d in deps if d["name"] == "a")
        b_dep = next(d for d in deps if d["name"] == "b")
        assert a_dep["version"] == ""
        assert b_dep["version"] == "2.0"

    def test_skips_build_dir(self, tmp_path: Path):
        """find_package calls inside build/ shouldn't pollute the result."""
        (tmp_path / "CMakeLists.txt").write_text(
            "FetchContent_Declare(myproj GIT_TAG 1.0)\n"
        )
        # Simulate FetchContent extracting a third-party dep into
        # build/_deps/foo-src/ that has its own find_package noise.
        nested = tmp_path / "build" / "_deps" / "foo-src"
        nested.mkdir(parents=True)
        (nested / "CMakeLists.txt").write_text(
            "find_package(SDL2 REQUIRED)\n"
            "find_package(OpenGL REQUIRED)\n"
        )
        deps = _detect_cmake_deps(tmp_path)
        names = {d["name"] for d in deps}
        assert "myproj" in names
        assert "SDL2" not in names
        assert "OpenGL" not in names

    def test_keeps_external_top_level(self, tmp_path: Path):
        """external/CMakeLists.txt at the top of external/ should be
        scanned — that's where projects typically declare their
        FetchContent deps. Only NESTED external/<dep>/CMakeLists.txt
        gets skipped."""
        (tmp_path / "CMakeLists.txt").write_text(
            "find_package(OpenGL REQUIRED)\n"
        )
        ext = tmp_path / "external"
        ext.mkdir()
        (ext / "CMakeLists.txt").write_text(
            "FetchContent_Declare(glfw GIT_TAG 3.4)\n"
        )
        # Vendored sub-dir with its own CMakeLists — should be skipped
        vendor = ext / "vendored_dep"
        vendor.mkdir()
        (vendor / "CMakeLists.txt").write_text(
            "find_package(Boost 1.74)\n"
        )
        deps = _detect_cmake_deps(tmp_path)
        names = {d["name"] for d in deps}
        assert "OpenGL" in names
        assert "glfw" in names
        assert "Boost" not in names

    def test_skips_cmake_build_dirs(self, tmp_path: Path):
        """build_release/, cmake-build-debug/ etc. should be skipped too."""
        for build_name in ("build_release", "cmake-build-debug", "out"):
            d = tmp_path / build_name
            d.mkdir()
            (d / "CMakeLists.txt").write_text(
                "find_package(Polluter 9.9)\n"
            )
        (tmp_path / "CMakeLists.txt").write_text(
            "FetchContent_Declare(real GIT_TAG 1.0)\n"
        )
        deps = _detect_cmake_deps(tmp_path)
        names = {d["name"] for d in deps}
        assert names == {"real"}
