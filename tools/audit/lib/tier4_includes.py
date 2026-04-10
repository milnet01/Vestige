"""Tier 4: Include dependency analysis — detect heavy headers, forward-decl candidates, circular includes."""

from __future__ import annotations

import logging
import re
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .config import Config
from .utils import enumerate_files, relative_path

log = logging.getLogger("audit")

INCLUDE_RE = re.compile(r'#include\s*"([^"]+)"')
FORWARD_DECL_RE = re.compile(r"^\s*class\s+(\w+)\s*;", re.MULTILINE)
POINTER_REF_RE = re.compile(r"\b{}\s*[*&]")
TEMPLATE_USE_RE = re.compile(r"<\s*{}\s*[>,]")


@dataclass
class IncludeAnalysis:
    """Results of include dependency analysis."""
    include_counts: dict[str, int] = field(default_factory=dict)
    heavy_headers: list[dict[str, Any]] = field(default_factory=list)
    forward_decl_candidates: list[dict[str, str]] = field(default_factory=list)
    circular_pairs: list[tuple[str, str]] = field(default_factory=list)
    total_includes: int = 0

    def to_dict(self) -> dict[str, Any]:
        return {
            "heavy_headers": self.heavy_headers,
            "forward_decl_candidates": self.forward_decl_candidates[:20],
            "circular_pairs": [list(p) for p in self.circular_pairs],
            "total_includes": self.total_includes,
        }


def analyze_includes(config: Config) -> IncludeAnalysis:
    """Build include graph and identify optimization opportunities."""
    result = IncludeAnalysis()

    if not config.get("tier4", "include_analysis", "enabled", default=True):
        return result

    threshold = config.get("tier4", "include_analysis", "heavy_header_threshold", default=15)

    header_files = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs,
        extensions=[".h", ".hpp"],
        exclude_dirs=config.exclude_dirs,
    )

    # Build include graph (header -> list of included headers)
    graph: dict[str, list[str]] = {}
    for hfile in header_files:
        rel = relative_path(hfile, config.root)
        includes = _parse_includes(hfile, config.root)
        graph[rel] = includes
        result.include_counts[rel] = len(includes)
        result.total_includes += len(includes)

    # Find heavy headers
    for file_path, count in sorted(result.include_counts.items(), key=lambda x: -x[1]):
        if count >= threshold:
            result.heavy_headers.append({"file": file_path, "includes": count})

    # Find circular includes
    result.circular_pairs = _detect_circular(graph)

    # Find forward declaration candidates
    result.forward_decl_candidates = _find_forward_decl_candidates(header_files, graph, config)

    log.info("Include analysis: %d headers, %d total includes, "
             "%d heavy headers, %d circular pairs, %d forward-decl candidates",
             len(header_files), result.total_includes,
             len(result.heavy_headers), len(result.circular_pairs),
             len(result.forward_decl_candidates))

    return result


def _parse_includes(file_path: Path, root: Path) -> list[str]:
    """Extract #include "..." directives (project includes only, not system <...>)."""
    includes: list[str] = []
    try:
        content = file_path.read_text(errors="replace")
        for m in INCLUDE_RE.finditer(content):
            includes.append(m.group(1))
    except OSError:
        pass
    return includes


def _detect_circular(graph: dict[str, list[str]]) -> list[tuple[str, str]]:
    """Find direct circular includes (A includes B AND B includes A)."""
    circular: list[tuple[str, str]] = []
    seen: set[tuple[str, str]] = set()

    for file_a, includes_a in graph.items():
        for inc in includes_a:
            # Resolve the include to a graph key
            matching_key = _resolve_include(inc, graph)
            if matching_key and matching_key in graph:
                includes_b = graph[matching_key]
                # Check if B includes A
                for inc_b in includes_b:
                    b_key = _resolve_include(inc_b, graph)
                    if b_key == file_a:
                        pair = tuple(sorted((file_a, matching_key)))
                        if pair not in seen:
                            seen.add(pair)
                            circular.append(pair)

    return circular


def _resolve_include(include_path: str, graph: dict[str, list[str]]) -> str | None:
    """Try to match an include path to a graph key."""
    # Direct match
    if include_path in graph:
        return include_path
    # Try matching the suffix (e.g., "renderer/shader.h" matches "engine/renderer/shader.h")
    for key in graph:
        if key.endswith(include_path) or key.endswith("/" + include_path):
            return key
    return None


def _find_forward_decl_candidates(
    headers: list[Path],
    graph: dict[str, list[str]],
    config: Config,
) -> list[dict[str, str]]:
    """Find includes in headers that could be replaced with forward declarations.

    Heuristic: if a header includes another, and only uses the included class
    as a pointer or reference, a forward declaration would suffice.
    """
    candidates: list[dict[str, str]] = []

    for hfile in headers:
        rel = relative_path(hfile, config.root)
        if rel not in graph:
            continue

        try:
            content = hfile.read_text(errors="replace")
        except OSError:
            continue

        # Find what classes are already forward-declared in this file
        existing_fwd = set(FORWARD_DECL_RE.findall(content))

        for inc in graph[rel]:
            # Try to determine the primary class in the included header
            class_name = _guess_class_from_include(inc)
            if not class_name or class_name in existing_fwd:
                continue

            # Check how the class is used in this header
            # If only as pointer/reference, it's a candidate
            ptr_ref_pattern = re.compile(rf"\b{re.escape(class_name)}\s*[*&]")
            value_pattern = re.compile(rf"\b{re.escape(class_name)}\s+\w")
            inherit_pattern = re.compile(rf":\s*(?:public|protected|private)\s+{re.escape(class_name)}")

            has_ptr_ref = bool(ptr_ref_pattern.search(content))
            has_value = bool(value_pattern.search(content))
            has_inherit = bool(inherit_pattern.search(content))

            # Only suggest forward decl if used as pointer/ref and NOT as value or base class
            if has_ptr_ref and not has_value and not has_inherit:
                candidates.append({
                    "file": rel,
                    "includes": inc,
                    "class": class_name,
                    "reason": "Only used as pointer/reference",
                })

    return candidates


def _guess_class_from_include(include_path: str) -> str | None:
    """Guess the primary class name from an include path.

    e.g., "renderer/shader.h" -> "Shader", "core/event_bus.h" -> "EventBus"
    """
    stem = Path(include_path).stem  # "shader", "event_bus"
    # Convert snake_case to PascalCase
    parts = stem.split("_")
    if not parts:
        return None
    return "".join(p.capitalize() for p in parts)
