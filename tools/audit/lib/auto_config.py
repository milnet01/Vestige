"""Auto-detect project settings and generate audit_config.yaml."""

from __future__ import annotations

import json
import logging
import re
import shutil
from pathlib import Path

import yaml

log = logging.getLogger("audit")

# ---------------------------------------------------------------------------
# Language detection — order matters (first match wins)
# ---------------------------------------------------------------------------

LANGUAGE_SIGNATURES: list[tuple[str, list[str]]] = [
    ("rust",   ["Cargo.toml"]),
    ("go",     ["go.mod"]),
    ("cpp",    ["CMakeLists.txt", "Makefile", "meson.build", "*.cpp", "*.cxx"]),
    ("c",      ["*.c"]),
    ("python", ["setup.py", "pyproject.toml", "setup.cfg", "requirements.txt"]),
    ("java",   ["pom.xml", "build.gradle", "build.gradle.kts"]),
    ("js",     ["package.json"]),
    ("ts",     ["tsconfig.json"]),
]

# ---------------------------------------------------------------------------
# Build system detection
# ---------------------------------------------------------------------------

BUILD_SYSTEMS: dict[str, dict] = {
    "cmake": {
        "marker": "CMakeLists.txt",
        "build_dir": "build",
        "configure_cmd": "cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "build_cmd": "cmake --build build 2>&1",
        "test_cmd": "cd build && ctest --output-on-failure 2>&1",
    },
    "meson": {
        "marker": "meson.build",
        "build_dir": "builddir",
        "configure_cmd": "meson setup builddir",
        "build_cmd": "meson compile -C builddir 2>&1",
        "test_cmd": "meson test -C builddir 2>&1",
    },
    "make": {
        "marker": "Makefile",
        "build_dir": ".",
        "configure_cmd": None,
        "build_cmd": "make 2>&1",
        "test_cmd": "make test 2>&1",
    },
    "cargo": {
        "marker": "Cargo.toml",
        "build_dir": "target",
        "configure_cmd": None,
        "build_cmd": "cargo build 2>&1",
        "test_cmd": "cargo test 2>&1",
    },
    "npm": {
        "marker": "package.json",
        "build_dir": "node_modules",
        "configure_cmd": "npm install",
        "build_cmd": "npm run build 2>&1",
        "test_cmd": "npm test 2>&1",
    },
    "pip": {
        "marker": "setup.py",
        "build_dir": ".",
        "configure_cmd": None,
        "build_cmd": None,
        "test_cmd": "pytest --tb=short 2>&1",
    },
    "pyproject": {
        "marker": "pyproject.toml",
        "build_dir": ".",
        "configure_cmd": None,
        "build_cmd": None,
        "test_cmd": "pytest --tb=short 2>&1",
    },
    "gradle": {
        "marker": "build.gradle",
        "build_dir": "build",
        "configure_cmd": None,
        "build_cmd": "./gradlew build 2>&1",
        "test_cmd": "./gradlew test 2>&1",
    },
    "maven": {
        "marker": "pom.xml",
        "build_dir": "target",
        "configure_cmd": None,
        "build_cmd": "mvn compile 2>&1",
        "test_cmd": "mvn test 2>&1",
    },
}

# ---------------------------------------------------------------------------
# Source extensions and patterns per language
# ---------------------------------------------------------------------------

def _get_language_defaults(lang: str) -> dict:
    """Get language-specific defaults (lazily built to avoid forward-reference issues)."""
    all_defaults: dict[str, dict] = {
        "cpp": {
            "source_extensions": [".cpp", ".h", ".hpp", ".cxx", ".cc"],
            "exclude_dirs": ["external/", "build/", ".git/", ".claude/", "third_party/",
                             "vendor/", "deps/", "node_modules/"],
            "static_analysis": {
                "cppcheck": {
                    "enabled": True,
                    "args": "--enable=all --std=c++17 --suppress=missingIncludeSystem --suppress=unusedFunction",
                },
                "clang_tidy": {
                    "enabled": True,
                    "checks": "bugprone-*,performance-*,modernize-*",
                },
            },
            "patterns": _cpp_patterns(),
        },
        "c": {
            "source_extensions": [".c", ".h"],
            "exclude_dirs": ["build/", ".git/", "third_party/", "vendor/"],
            "static_analysis": {
                "cppcheck": {
                    "enabled": True,
                    "args": "--enable=all --std=c11 --suppress=missingIncludeSystem",
                },
                "clang_tidy": {
                    "enabled": True,
                    "checks": "bugprone-*,performance-*",
                },
            },
            "patterns": _c_patterns(),
        },
        "python": {
            "source_extensions": [".py"],
            "exclude_dirs": [".git/", "__pycache__/", ".venv/", "venv/", "env/",
                             ".tox/", "dist/", "build/", "*.egg-info/"],
            "static_analysis": {
                "cppcheck": {"enabled": False},
                "clang_tidy": {"enabled": False},
            },
            "patterns": _python_patterns(),
        },
        "rust": {
            "source_extensions": [".rs"],
            "exclude_dirs": ["target/", ".git/"],
            "static_analysis": {
                "cppcheck": {"enabled": False},
                "clang_tidy": {"enabled": False},
            },
            "patterns": _rust_patterns(),
        },
        "js": {
            "source_extensions": [".js", ".jsx", ".mjs"],
            "exclude_dirs": ["node_modules/", ".git/", "dist/", "build/"],
            "static_analysis": {
                "cppcheck": {"enabled": False},
                "clang_tidy": {"enabled": False},
            },
            "patterns": _js_patterns(),
        },
        "ts": {
            "source_extensions": [".ts", ".tsx"],
            "exclude_dirs": ["node_modules/", ".git/", "dist/", "build/"],
            "static_analysis": {
                "cppcheck": {"enabled": False},
                "clang_tidy": {"enabled": False},
            },
            "patterns": _ts_patterns(),
        },
        "java": {
            "source_extensions": [".java"],
            "exclude_dirs": ["target/", "build/", ".git/", ".gradle/"],
            "static_analysis": {
                "cppcheck": {"enabled": False},
                "clang_tidy": {"enabled": False},
            },
            "patterns": _java_patterns(),
        },
        "go": {
            "source_extensions": [".go"],
            "exclude_dirs": ["vendor/", ".git/"],
            "static_analysis": {
                "cppcheck": {"enabled": False},
                "clang_tidy": {"enabled": False},
            },
            "patterns": _go_patterns(),
        },
    }
    return all_defaults.get(lang, all_defaults.get("cpp", {}))


# ---------------------------------------------------------------------------
# Pattern libraries per language
# ---------------------------------------------------------------------------

def _cpp_patterns() -> dict:
    return {
        "memory_safety": [
            {"name": "raw_new", "pattern": "\\bnew\\s+\\w+", "file_glob": "*.cpp,*.h",
             "severity": "high", "description": "Raw new — should use smart pointers",
             "exclude_pattern": "placement new|nothrow", "skip_comments": True},
            {"name": "raw_delete", "pattern": "\\bdelete\\s+", "file_glob": "*.cpp,*.h",
             "severity": "high", "description": "Raw delete — should use smart pointers",
             "skip_comments": True},
            {"name": "null_macro", "pattern": "\\bNULL\\b", "file_glob": "*.cpp,*.h",
             "severity": "low", "description": "NULL macro — use nullptr"},
            {"name": "unsafe_c_string", "pattern": "\\b(strcpy|strcat|sprintf|gets)\\b",
             "file_glob": "*.cpp,*.h", "severity": "high",
             "description": "Unsafe C string function", "skip_comments": True},
            {"name": "c_style_cast",
             "pattern": "\\(\\s*(int|float|double|char|void|unsigned|long|short|size_t|uint\\d+_t|int\\d+_t)\\s*\\*?\\s*\\)",
             "file_glob": "*.cpp,*.h", "severity": "medium",
             "description": "C-style cast — use static_cast/reinterpret_cast"},
        ],
        "performance": [
            {"name": "std_endl", "pattern": "std::endl", "file_glob": "*.cpp,*.h",
             "severity": "low", "description": "std::endl flushes stream — use '\\n'"},
            {"name": "shared_ptr", "pattern": "std::shared_ptr", "file_glob": "*.cpp,*.h",
             "severity": "low", "description": "shared_ptr — verify unique_ptr not sufficient"},
        ],
        "code_quality": [
            {"name": "todo_fixme", "pattern": "\\b(TODO|FIXME|HACK|WORKAROUND|XXX)\\b",
             "file_glob": "*.cpp,*.h", "severity": "info",
             "description": "Deferred work marker"},
            {"name": "empty_catch", "pattern": "catch\\s*\\([^)]*\\)\\s*\\{\\s*\\}",
             "file_glob": "*.cpp,*.h", "severity": "medium",
             "description": "Empty catch block — silently swallowed error"},
        ],
    }


def _c_patterns() -> dict:
    return {
        "memory_safety": [
            {"name": "unsafe_c_string", "pattern": "\\b(strcpy|strcat|sprintf|gets)\\b",
             "file_glob": "*.c,*.h", "severity": "high",
             "description": "Unsafe C string function", "skip_comments": True},
            {"name": "malloc_no_free", "pattern": "\\bmalloc\\s*\\(", "file_glob": "*.c",
             "severity": "medium", "description": "malloc — verify matching free()"},
        ],
        "code_quality": [
            {"name": "todo_fixme", "pattern": "\\b(TODO|FIXME|HACK|WORKAROUND|XXX)\\b",
             "file_glob": "*.c,*.h", "severity": "info",
             "description": "Deferred work marker"},
        ],
    }


def _python_patterns() -> dict:
    return {
        "security": [
            {"name": "eval_exec", "pattern": "\\b(eval|exec)\\s*\\(",
             "file_glob": "*.py", "severity": "high",
             "description": "eval/exec — code injection risk", "skip_comments": True},
            {"name": "shell_true", "pattern": "shell\\s*=\\s*True",
             "file_glob": "*.py", "severity": "high",
             "description": "subprocess shell=True — command injection risk"},
            {"name": "pickle_load", "pattern": "pickle\\.load",
             "file_glob": "*.py", "severity": "medium",
             "description": "pickle.load — deserialization risk"},
        ],
        "code_quality": [
            {"name": "bare_except", "pattern": "except\\s*:",
             "file_glob": "*.py", "severity": "medium",
             "description": "Bare except — catches SystemExit/KeyboardInterrupt"},
            {"name": "todo_fixme", "pattern": "\\b(TODO|FIXME|HACK|WORKAROUND|XXX)\\b",
             "file_glob": "*.py", "severity": "info",
             "description": "Deferred work marker"},
            {"name": "print_debug", "pattern": "\\bprint\\s*\\(",
             "file_glob": "*.py", "severity": "low",
             "description": "print() — use logging instead"},
        ],
    }


def _rust_patterns() -> dict:
    return {
        "safety": [
            {"name": "unsafe_block", "pattern": "\\bunsafe\\s*\\{",
             "file_glob": "*.rs", "severity": "medium",
             "description": "unsafe block — verify soundness"},
            {"name": "unwrap", "pattern": "\\.unwrap\\(\\)",
             "file_glob": "*.rs", "severity": "low",
             "description": "unwrap() — consider proper error handling"},
            {"name": "expect", "pattern": "\\.expect\\(",
             "file_glob": "*.rs", "severity": "low",
             "description": "expect() — may panic at runtime"},
        ],
        "code_quality": [
            {"name": "todo_fixme", "pattern": "\\b(TODO|FIXME|HACK|WORKAROUND|XXX)\\b",
             "file_glob": "*.rs", "severity": "info",
             "description": "Deferred work marker"},
        ],
    }


def _js_patterns() -> dict:
    return {
        "security": [
            {"name": "eval", "pattern": "\\beval\\s*\\(", "file_glob": "*.js,*.jsx,*.mjs",
             "severity": "high", "description": "eval() — code injection risk",
             "skip_comments": True},
            {"name": "innerhtml", "pattern": "\\.innerHTML\\s*=",
             "file_glob": "*.js,*.jsx", "severity": "high",
             "description": "innerHTML assignment — XSS risk"},
        ],
        "code_quality": [
            {"name": "console_log", "pattern": "console\\.log\\(",
             "file_glob": "*.js,*.jsx,*.mjs", "severity": "low",
             "description": "console.log — remove before production"},
            {"name": "todo_fixme", "pattern": "\\b(TODO|FIXME|HACK|WORKAROUND|XXX)\\b",
             "file_glob": "*.js,*.jsx,*.mjs", "severity": "info",
             "description": "Deferred work marker"},
        ],
    }


def _ts_patterns() -> dict:
    base = _js_patterns()
    # Add TS-specific patterns
    base.setdefault("code_quality", []).extend([
        {"name": "any_type", "pattern": ":\\s*any\\b", "file_glob": "*.ts,*.tsx",
         "severity": "low", "description": "any type — defeats type safety"},
        {"name": "ts_ignore", "pattern": "@ts-ignore|@ts-nocheck",
         "file_glob": "*.ts,*.tsx", "severity": "medium",
         "description": "TypeScript suppression — verify justification"},
    ])
    # Update file globs for TS
    for cat in base.values():
        for p in cat:
            if "*.js" in p.get("file_glob", ""):
                p["file_glob"] = p["file_glob"].replace("*.js", "*.ts").replace("*.jsx", "*.tsx").replace("*.mjs", "*.ts")
    return base


def _java_patterns() -> dict:
    return {
        "security": [
            {"name": "sql_concat", "pattern": "\"\\s*\\+.*(?:SELECT|INSERT|UPDATE|DELETE)",
             "file_glob": "*.java", "severity": "high",
             "description": "SQL string concatenation — use prepared statements",
             "skip_comments": True},
        ],
        "code_quality": [
            {"name": "system_out", "pattern": "System\\.out\\.print",
             "file_glob": "*.java", "severity": "low",
             "description": "System.out — use logger instead"},
            {"name": "todo_fixme", "pattern": "\\b(TODO|FIXME|HACK|WORKAROUND|XXX)\\b",
             "file_glob": "*.java", "severity": "info",
             "description": "Deferred work marker"},
        ],
    }


def _go_patterns() -> dict:
    return {
        "error_handling": [
            {"name": "ignored_error", "pattern": "\\b_\\s*=.*\\(",
             "file_glob": "*.go", "severity": "medium",
             "description": "Ignored return value — check if error"},
        ],
        "code_quality": [
            {"name": "fmt_print", "pattern": "fmt\\.Print",
             "file_glob": "*.go", "severity": "low",
             "description": "fmt.Print — use structured logging"},
            {"name": "todo_fixme", "pattern": "\\b(TODO|FIXME|HACK|WORKAROUND|XXX)\\b",
             "file_glob": "*.go", "severity": "info",
             "description": "Deferred work marker"},
        ],
    }


# ---------------------------------------------------------------------------
# Dependency detection (for CVE research)
# ---------------------------------------------------------------------------

def _detect_cmake_deps(root: Path) -> list[dict]:
    """Try to extract dependency names from CMakeLists.txt files."""
    deps: list[dict] = []
    for cmake_file in root.rglob("CMakeLists.txt"):
        try:
            content = cmake_file.read_text(errors="replace")
        except OSError:
            continue

        # find_package(Foo VERSION)
        for m in re.finditer(r"find_package\s*\(\s*(\w+)(?:\s+(\d[\d.]*))?\s*", content):
            deps.append({"name": m.group(1), "version": m.group(2) or ""})

        # FetchContent_Declare(name GIT_TAG ...)
        for m in re.finditer(r"FetchContent_Declare\s*\(\s*(\w+)", content):
            deps.append({"name": m.group(1), "version": ""})

    # Deduplicate by name
    seen: set[str] = set()
    unique: list[dict] = []
    for d in deps:
        if d["name"].lower() not in seen:
            seen.add(d["name"].lower())
            unique.append(d)
    return unique


def _detect_cargo_deps(root: Path) -> list[dict]:
    """Extract dependencies from Cargo.toml."""
    deps: list[dict] = []
    cargo_file = root / "Cargo.toml"
    if not cargo_file.exists():
        return deps
    try:
        content = cargo_file.read_text()
        for m in re.finditer(r'^(\w[\w-]*)\s*=\s*"(\d[\d.]*)"', content, re.MULTILINE):
            deps.append({"name": m.group(1), "version": m.group(2)})
        for m in re.finditer(r'^(\w[\w-]*)\s*=\s*\{.*version\s*=\s*"(\d[\d.]*)"', content, re.MULTILINE):
            deps.append({"name": m.group(1), "version": m.group(2)})
    except OSError:
        pass
    return deps


def _detect_npm_deps(root: Path) -> list[dict]:
    """Extract dependencies from package.json."""
    deps: list[dict] = []
    pkg_file = root / "package.json"
    if not pkg_file.exists():
        return deps
    try:
        data = json.loads(pkg_file.read_text())
        for section in ("dependencies", "devDependencies"):
            for name, version in data.get(section, {}).items():
                clean_ver = re.sub(r"[^0-9.]", "", version)
                deps.append({"name": name, "version": clean_ver})
    except (OSError, json.JSONDecodeError):
        pass
    return deps


def _detect_pip_deps(root: Path) -> list[dict]:
    """Extract dependencies from requirements.txt."""
    deps: list[dict] = []
    for req_file in ["requirements.txt", "requirements-dev.txt"]:
        path = root / req_file
        if not path.exists():
            continue
        try:
            for line in path.read_text().splitlines():
                line = line.strip()
                if not line or line.startswith("#") or line.startswith("-"):
                    continue
                m = re.match(r"([\w-]+)(?:[=<>!~]+(.+))?", line)
                if m:
                    deps.append({"name": m.group(1), "version": m.group(2) or ""})
        except OSError:
            pass
    return deps


# ---------------------------------------------------------------------------
# Main auto-detect + generate
# ---------------------------------------------------------------------------

def detect_project(root: Path) -> dict:
    """Auto-detect project settings from directory contents."""
    root = root.resolve()
    log.info("Auto-detecting project at %s", root)

    # Detect project name from directory
    project_name = root.name.replace("_", " ").replace("-", " ").title()

    # Detect language
    language = _detect_language(root)
    log.info("Detected language: %s", language)

    # Detect build system
    build_system, build_config = _detect_build_system(root, language)
    log.info("Detected build system: %s", build_system or "none")

    # Get language-specific defaults
    lang_defaults = _get_language_defaults(language)

    # Detect source directories
    source_dirs = _detect_source_dirs(root, lang_defaults.get("source_extensions", []))
    log.info("Detected source dirs: %s", source_dirs)

    # Detect shader directories (C++ projects)
    shader_dirs: list[str] = []
    if language in ("cpp", "c"):
        shader_dirs = _detect_shader_dirs(root)
        if shader_dirs:
            log.info("Detected shader dirs: %s", shader_dirs)

    # Detect dependencies for CVE research
    deps = _detect_dependencies(root, language, build_system)
    log.info("Detected %d dependencies", len(deps))

    # Build research topics from dependencies
    research_topics: list[dict] = []
    for dep in deps[:15]:  # Cap at 15 to avoid too many queries
        ver = dep.get("version", "")
        if ver:
            research_topics.append({"query": f"CVE {dep['name']} {ver} vulnerability"})
        else:
            research_topics.append({"query": f"CVE {dep['name']} vulnerability"})

    # Assemble config
    config: dict = {
        "project": {
            "name": project_name,
            "root": ".",
            "language": language,
            "source_dirs": source_dirs,
            "shader_dirs": shader_dirs,
            "shader_glob": "*.glsl",
            "source_extensions": lang_defaults.get("source_extensions", [".cpp", ".h"]),
            "exclude_dirs": lang_defaults.get("exclude_dirs",
                                               ["build/", ".git/", "external/"]),
        },
        "build": build_config,
        "static_analysis": lang_defaults.get("static_analysis", {
            "cppcheck": {"enabled": False},
            "clang_tidy": {"enabled": False},
        }),
        "patterns": lang_defaults.get("patterns", {}),
        "changes": {"base_ref": "HEAD~1"},
        "tier4": {
            "gpu_resource_pattern": "GLuint\\s+m_",
            "event_subscribe_pattern": "subscribe<",
            "event_unsubscribe_pattern": "unsubscribe",
            "complexity_threshold": 500,
        },
        "research": {
            "enabled": True,
            "cache_dir": ".audit_cache",
            "cache_ttl_days": 7,
            "max_results_per_query": 3,
            "topics": research_topics,
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

    return config


def generate_config(root: Path, output_path: Path) -> None:
    """Auto-detect project and write audit_config.yaml."""
    config = detect_project(root)

    # Write with a helpful header comment
    header = (
        "# =============================================================================\n"
        f"# {config['project']['name']} — Audit Configuration (auto-generated)\n"
        "# =============================================================================\n"
        "# Generated by: python3 audit.py --init\n"
        "# Review and adjust these settings for your project.\n"
        "# Run: python3 audit.py\n"
        "# =============================================================================\n\n"
    )

    yaml_str = yaml.dump(config, default_flow_style=False, sort_keys=False, width=100)
    output_path.write_text(header + yaml_str)
    log.info("Config written to %s", output_path)


# ---------------------------------------------------------------------------
# Internal detection helpers
# ---------------------------------------------------------------------------

def _detect_language(root: Path) -> str:
    """Detect the primary language from marker files."""
    for lang, markers in LANGUAGE_SIGNATURES:
        for marker in markers:
            if "*" in marker:
                if list(root.glob(marker)):
                    return lang
            elif (root / marker).exists():
                return lang
    return "cpp"  # Fallback


def _detect_build_system(root: Path, language: str) -> tuple[str | None, dict]:
    """Detect the build system and return (name, build_config)."""
    # Check each build system by marker file
    for name, bs in BUILD_SYSTEMS.items():
        marker = bs["marker"]
        if (root / marker).exists():
            return name, {
                "system": name,
                "build_dir": bs["build_dir"],
                "configure_cmd": bs.get("configure_cmd"),
                "build_cmd": bs.get("build_cmd"),
                "warning_regex": "warning:|error:",
                "test_cmd": bs.get("test_cmd"),
                "sanitizer": {"enabled": False},
            }

    # No build system detected
    return None, {
        "system": "none",
        "build_dir": ".",
        "configure_cmd": None,
        "build_cmd": None,
        "warning_regex": "warning:|error:",
        "test_cmd": None,
        "sanitizer": {"enabled": False},
    }


def _detect_source_dirs(root: Path, extensions: list[str]) -> list[str]:
    """Find directories containing source files."""
    source_dirs: set[str] = set()
    exclude_names = {".git", "build", "node_modules", "target", "external",
                     "third_party", "vendor", ".cache", ".claude", "__pycache__",
                     "dist", ".tox", ".venv", "venv", "env", "tools", "docs",
                     "cmake-build-debug", "cmake-build-release", "out"}

    for ext in extensions:
        for path in root.rglob(f"*{ext}"):
            # Get the first-level directory relative to root
            try:
                rel = path.relative_to(root)
            except ValueError:
                continue
            if rel.parts and rel.parts[0] not in exclude_names:
                source_dirs.add(f"{rel.parts[0]}/")

    return sorted(source_dirs) or ["src/"]


def _detect_shader_dirs(root: Path) -> list[str]:
    """Find directories containing shader files."""
    shader_dirs: set[str] = set()
    exclude_parents = {"build", ".git", ".claude", "node_modules", "target",
                       "external", "third_party"}
    for ext in ("*.glsl", "*.vert", "*.frag", "*.hlsl", "*.wgsl", "*.comp"):
        for path in root.rglob(ext):
            try:
                rel = path.relative_to(root)
            except ValueError:
                continue
            # Skip files under excluded top-level dirs
            if rel.parts and rel.parts[0] in exclude_parents:
                continue
            parent = str(rel.parent)
            if parent != ".":
                shader_dirs.add(f"{parent}/")
    return sorted(shader_dirs)


def _detect_dependencies(root: Path, language: str, build_system: str | None) -> list[dict]:
    """Detect project dependencies for CVE research."""
    if build_system == "cmake" or language in ("cpp", "c"):
        return _detect_cmake_deps(root)
    elif build_system == "cargo" or language == "rust":
        return _detect_cargo_deps(root)
    elif build_system in ("npm",) or language in ("js", "ts"):
        return _detect_npm_deps(root)
    elif language == "python":
        return _detect_pip_deps(root)
    return []
