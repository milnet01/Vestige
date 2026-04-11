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
SYSTEM_INCLUDE_RE = re.compile(r'#include\s*<([^>]+)>')
ANY_INCLUDE_RE = re.compile(r'#include\s*([<"])([^>"]+)[>"]')
FORWARD_DECL_RE = re.compile(r"^\s*class\s+(\w+)\s*;", re.MULTILINE)


@dataclass
class IncludeAnalysis:
    """Results of include dependency analysis."""
    include_counts: dict[str, int] = field(default_factory=dict)
    heavy_headers: list[dict[str, Any]] = field(default_factory=list)
    forward_decl_candidates: list[dict[str, str]] = field(default_factory=list)
    circular_pairs: list[tuple[str, str]] = field(default_factory=list)
    order_violations: list[dict[str, Any]] = field(default_factory=list)
    total_includes: int = 0

    def to_dict(self) -> dict[str, Any]:
        return {
            "heavy_headers": self.heavy_headers,
            "forward_decl_candidates": self.forward_decl_candidates[:20],
            "circular_pairs": [list(p) for p in self.circular_pairs],
            "order_violations": self.order_violations[:20],
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

    # Check include ordering in .cpp files
    all_files = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs,
        extensions=[".cpp", ".cc", ".cxx"],
        exclude_dirs=config.exclude_dirs,
    )
    result.order_violations = _check_include_order(all_files, config)

    log.info("Include analysis: %d headers, %d total includes, "
             "%d heavy headers, %d circular pairs, %d forward-decl candidates, "
             "%d order violations",
             len(header_files), result.total_includes,
             len(result.heavy_headers), len(result.circular_pairs),
             len(result.forward_decl_candidates), len(result.order_violations))

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
    """Find circular includes using Tarjan's SCC algorithm.

    Returns pairs from all cycles found (not just direct A↔B).
    """
    # Build resolved adjacency list
    adj: dict[str, list[str]] = {}
    for node, includes in graph.items():
        resolved = []
        for inc in includes:
            key = _resolve_include(inc, graph)
            if key:
                resolved.append(key)
        adj[node] = resolved

    # Tarjan's SCC
    index_counter = [0]
    stack: list[str] = []
    on_stack: set[str] = set()
    indices: dict[str, int] = {}
    lowlinks: dict[str, int] = {}
    sccs: list[list[str]] = []

    def strongconnect(v: str) -> None:
        indices[v] = index_counter[0]
        lowlinks[v] = index_counter[0]
        index_counter[0] += 1
        stack.append(v)
        on_stack.add(v)

        for w in adj.get(v, []):
            if w not in indices:
                strongconnect(w)
                lowlinks[v] = min(lowlinks[v], lowlinks[w])
            elif w in on_stack:
                lowlinks[v] = min(lowlinks[v], indices[w])

        if lowlinks[v] == indices[v]:
            scc: list[str] = []
            while True:
                w = stack.pop()
                on_stack.discard(w)
                scc.append(w)
                if w == v:
                    break
            if len(scc) > 1:
                sccs.append(scc)

    for node in adj:
        if node not in indices:
            strongconnect(node)

    # Convert SCCs to pairs for backward compatibility
    circular: list[tuple[str, str]] = []
    seen: set[tuple[str, str]] = set()
    for scc in sccs:
        for i, a in enumerate(scc):
            for b in scc[i + 1:]:
                pair = tuple(sorted((a, b)))
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


def _classify_include(bracket: str, path: str, source_stem: str) -> int:
    """Classify an include into ordering categories (Google-style).

    Returns: 0 = corresponding header, 1 = C system, 2 = C++ stdlib,
             3 = third-party, 4 = project local
    """
    # Corresponding header (foo.cpp -> foo.h)
    inc_stem = Path(path).stem
    if bracket == '"' and (inc_stem == source_stem or
                           path.endswith(source_stem + ".h") or
                           path.endswith(source_stem + ".hpp")):
        return 0
    if bracket == "<":
        # C system headers have .h extension
        if path.endswith(".h") and "/" not in path:
            return 1
        # C++ stdlib headers have no extension
        if "/" not in path and "." not in path:
            return 2
        # Third-party (angle bracket with path separator)
        return 3
    # Project local headers (quoted)
    return 4


def _check_include_order(
    files: list[Path], config: Config,
) -> list[dict[str, Any]]:
    """Check include ordering follows Google-style conventions.

    Order: corresponding header, C system, C++ stdlib, third-party, project local.
    """
    violations: list[dict[str, Any]] = []
    for fpath in files:
        try:
            content = fpath.read_text(errors="replace")
        except OSError:
            continue

        rel = relative_path(fpath, config.root)
        source_stem = fpath.stem

        includes: list[tuple[int, int, str]] = []  # (line_num, category, path)
        for i, line in enumerate(content.splitlines(), 1):
            m = ANY_INCLUDE_RE.match(line.strip())
            if m:
                bracket, inc_path = m.group(1), m.group(2)
                cat = _classify_include(bracket, inc_path, source_stem)
                includes.append((i, cat, inc_path))

        # Check ordering: categories should be non-decreasing
        prev_cat = -1
        for line_num, cat, inc_path in includes:
            if cat < prev_cat:
                violations.append({
                    "file": rel,
                    "line": line_num,
                    "include": inc_path,
                    "expected_group": prev_cat,
                    "actual_group": cat,
                })
                break  # One violation per file is enough
            prev_cat = cat

    return violations
