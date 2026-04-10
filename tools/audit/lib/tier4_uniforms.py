"""Tier 4: Uniform-shader sync check — cross-reference GLSL declarations with C++ setter calls."""

from __future__ import annotations

import logging
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .config import Config
from .utils import enumerate_files, relative_path

log = logging.getLogger("audit")

# Regex patterns
GLSL_UNIFORM_RE = re.compile(r"uniform\s+\w+\s+(u_\w+)(?:\[(\d+)\])?\s*;")
CPP_SETTER_RE = re.compile(r"\.set(?:Bool|Int|Float|Vec[234]|Mat[34])\(\s*\"(u_[^\"]+)\"")
CPP_ARRAY_RE = re.compile(r"\"(u_\w+)\[")


@dataclass
class UniformSyncResult:
    """Results of uniform-shader sync analysis."""
    shader_uniforms: dict[str, set[str]] = field(default_factory=dict)
    cpp_uniforms: dict[str, set[str]] = field(default_factory=dict)
    declared_not_set: list[dict[str, str]] = field(default_factory=list)
    set_not_declared: list[dict[str, str]] = field(default_factory=list)
    total_shader_uniforms: int = 0
    total_cpp_uniforms: int = 0

    def to_dict(self) -> dict[str, Any]:
        return {
            "declared_not_set": self.declared_not_set,
            "set_not_declared": self.set_not_declared,
            "total_shader_uniforms": self.total_shader_uniforms,
            "total_cpp_uniforms": self.total_cpp_uniforms,
        }


def analyze_uniforms(config: Config) -> UniformSyncResult:
    """Cross-reference GLSL uniform declarations with C++ setter calls."""
    result = UniformSyncResult()

    if not config.get("tier4", "uniform_analysis", "enabled", default=True):
        return result

    # Collect shader uniforms
    shader_files = enumerate_files(
        root=config.root,
        source_dirs=config.shader_dirs,
        extensions=[".glsl"],
        exclude_dirs=config.exclude_dirs,
    )
    all_shader_uniforms: set[str] = set()
    for sf in shader_files:
        uniforms = _extract_glsl_uniforms(sf)
        if uniforms:
            result.shader_uniforms[relative_path(sf, config.root)] = uniforms
            all_shader_uniforms.update(uniforms)

    result.total_shader_uniforms = len(all_shader_uniforms)

    # Collect C++ uniform setter calls
    cpp_files = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs,
        extensions=[".cpp"],
        exclude_dirs=config.exclude_dirs,
    )
    all_cpp_uniforms: set[str] = set()
    for cf in cpp_files:
        uniforms = _extract_cpp_uniforms(cf)
        if uniforms:
            result.cpp_uniforms[relative_path(cf, config.root)] = uniforms
            all_cpp_uniforms.update(uniforms)

    result.total_cpp_uniforms = len(all_cpp_uniforms)

    # Cross-reference
    declared_not_set = all_shader_uniforms - all_cpp_uniforms
    set_not_declared = all_cpp_uniforms - all_shader_uniforms

    # Build detailed reports
    for uniform in sorted(declared_not_set):
        shader = _find_uniform_source(uniform, result.shader_uniforms)
        result.declared_not_set.append({
            "uniform": uniform,
            "shader": shader,
        })

    for uniform in sorted(set_not_declared):
        cpp_file = _find_uniform_source(uniform, result.cpp_uniforms)
        result.set_not_declared.append({
            "uniform": uniform,
            "cpp_file": cpp_file,
        })

    log.info("Uniform sync: %d shader uniforms, %d C++ setters, "
             "%d declared-not-set, %d set-not-declared",
             result.total_shader_uniforms, result.total_cpp_uniforms,
             len(result.declared_not_set), len(result.set_not_declared))

    return result


def _extract_glsl_uniforms(path: Path) -> set[str]:
    """Parse uniform declarations from a GLSL file."""
    uniforms: set[str] = set()
    try:
        content = path.read_text(errors="replace")
        for m in GLSL_UNIFORM_RE.finditer(content):
            name = m.group(1)
            uniforms.add(name)
    except OSError:
        pass
    return uniforms


def _extract_cpp_uniforms(path: Path) -> set[str]:
    """Parse setUniform calls from a C++ file."""
    uniforms: set[str] = set()
    try:
        content = path.read_text(errors="replace")
        # Direct uniform names
        for m in CPP_SETTER_RE.finditer(content):
            name = m.group(1)
            # Strip array index if present: "u_foo[0]" -> "u_foo"
            base = name.split("[")[0]
            uniforms.add(base)
        # Array uniform patterns via string concatenation
        for m in CPP_ARRAY_RE.finditer(content):
            uniforms.add(m.group(1))
    except OSError:
        pass
    return uniforms


def _find_uniform_source(uniform: str, mapping: dict[str, set[str]]) -> str:
    """Find which file declares/uses a uniform."""
    for file_path, uniforms in mapping.items():
        if uniform in uniforms:
            return file_path
    return "unknown"
