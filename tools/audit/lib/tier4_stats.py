"""Tier 4: Codebase statistics — LOC, Rule-of-Five audit, event lifecycle, complexity."""

from __future__ import annotations

import logging
import re
from collections import defaultdict
from pathlib import Path

from .config import Config
from .findings import AuditData
from .utils import enumerate_files, relative_path, count_lines

log = logging.getLogger("audit")


def run(config: Config) -> tuple[AuditData, list]:
    """Collect codebase statistics and structural audits."""
    data = AuditData()

    all_files = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs + config.shader_dirs,
        extensions=config.source_extensions + [".glsl"],
        exclude_dirs=config.exclude_dirs,
    )
    data.file_count = len(all_files)

    log.info("Tier 4: collecting stats on %d files", data.file_count)

    # LOC by subsystem
    loc_map: dict[str, int] = defaultdict(int)
    for f in all_files:
        subsystem = _get_subsystem(f, config.root)
        lines = count_lines(f)
        loc_map[subsystem] += lines
        data.total_loc += lines

    data.loc_by_subsystem = dict(sorted(loc_map.items(), key=lambda x: -x[1]))

    # Large files (above threshold)
    threshold = config.get("tier4", "complexity_threshold", default=500)
    for f in all_files:
        lines = count_lines(f)
        if lines >= threshold:
            data.large_files.append({
                "file": relative_path(f, config.root),
                "lines": lines,
            })
    data.large_files.sort(key=lambda x: -x["lines"])

    # Header-only files for Rule-of-Five and event lifecycle
    header_files = [f for f in all_files if f.suffix == ".h"]

    # Rule-of-Five audit (GPU resource classes)
    gpu_pattern_str = config.get("tier4", "gpu_resource_pattern", default=r"GLuint\s+m_")
    data.gpu_resource_classes = _audit_rule_of_five(header_files, gpu_pattern_str, config)

    # Event lifecycle audit
    sub_pat = config.get("tier4", "event_subscribe_pattern", default=r"subscribe<")
    unsub_pat = config.get("tier4", "event_unsubscribe_pattern", default=r"unsubscribe")
    data.event_lifecycle = _audit_event_lifecycle(all_files, sub_pat, unsub_pat, config)

    # Deferred work markers (TODO, FIXME, HACK, etc.)
    data.deferred_markers, data.deferred_by_subsystem = _collect_deferred_markers(
        all_files, config
    )

    # Uniform-shader sync analysis
    if config.shader_dirs:
        from . import tier4_uniforms
        data.uniform_sync = tier4_uniforms.analyze_uniforms(config).to_dict()

    # Include dependency analysis
    from . import tier4_includes
    data.include_analysis = tier4_includes.analyze_includes(config).to_dict()

    # Complexity analysis (requires lizard)
    from . import tier4_complexity
    complexity_result, complexity_findings = tier4_complexity.analyze_complexity(config)
    if complexity_result:
        data.complexity = complexity_result.to_dict()

    # Dead code detection
    from . import tier4_deadcode
    deadcode_result, deadcode_findings = tier4_deadcode.analyze_dead_code(config)
    data.dead_code = deadcode_result.to_dict()
    complexity_findings.extend(deadcode_findings)

    # Build system audit
    from . import tier4_build_audit
    build_audit_result, build_audit_findings = tier4_build_audit.analyze_build_system(config)
    data.build_audit = build_audit_result.to_dict()
    complexity_findings.extend(build_audit_findings)

    # Code duplication detection
    from . import tier4_duplication
    dup_result, dup_findings = tier4_duplication.analyze_duplication(config)
    data.duplication = dup_result.to_dict()
    complexity_findings.extend(dup_findings)

    # Refactoring opportunity analysis
    from . import tier4_refactoring
    refactor_result, refactor_findings = tier4_refactoring.analyze_refactoring(config)
    data.refactoring = refactor_result.to_dict()
    complexity_findings.extend(refactor_findings)

    log.info("Tier 4: %d LOC, %d GPU classes, %d event files, %d deferred markers, "
             "%d dead code, %d build audit, %d clone pairs, %d refactoring smells",
             data.total_loc, len(data.gpu_resource_classes),
             len(data.event_lifecycle), len(data.deferred_markers),
             len(deadcode_result.unused_functions) + len(deadcode_result.unused_includes),
             len(build_audit_findings),
             len(dup_result.clone_pairs), refactor_result.total_smells)
    return data, complexity_findings


def _get_subsystem(path: Path, root: Path) -> str:
    """Determine subsystem from file path."""
    try:
        rel = path.resolve().relative_to(root.resolve())
    except ValueError:
        return "other"
    parts = rel.parts
    if len(parts) >= 2 and parts[0] == "engine":
        return parts[1]
    if len(parts) >= 2 and parts[0] == "assets":
        return f"assets/{parts[1]}"
    if len(parts) >= 1:
        return parts[0]
    return "other"


def _audit_rule_of_five(
    headers: list[Path],
    gpu_pattern: str,
    config: Config,
) -> list[dict]:
    """Find classes owning GPU resources and check Rule-of-Five compliance."""
    results: list[dict] = []
    gpu_re = re.compile(gpu_pattern)

    for hfile in headers:
        try:
            content = hfile.read_text(errors="replace")
        except OSError:
            continue

        if not gpu_re.search(content):
            continue

        # Find class name
        class_m = re.search(r"class\s+(\w+)", content)
        if not class_m:
            continue

        class_name = class_m.group(1)
        rel = relative_path(hfile, config.root)

        # Check for the five special members
        has_dtor = bool(re.search(rf"~{class_name}\s*\(", content))
        has_copy_ctor = bool(re.search(rf"{class_name}\s*\(\s*const\s+{class_name}\s*&", content))
        has_copy_assign = bool(re.search(r"operator\s*=\s*\(\s*const\s+" + class_name, content))
        has_move_ctor = bool(re.search(rf"{class_name}\s*\(\s*{class_name}\s*&&", content))
        has_move_assign = bool(re.search(r"operator\s*=\s*\(\s*" + class_name + r"\s*&&", content))

        # Check for deleted specials
        copy_deleted = bool(re.search(rf"{class_name}\s*\(.*\)\s*=\s*delete", content))
        assign_deleted = bool(re.search(r"operator\s*=.*=\s*delete", content))

        status = "OK"
        if has_dtor and not (has_move_ctor and has_move_assign):
            if not (copy_deleted and assign_deleted):
                status = "INCOMPLETE"

        results.append({
            "class": class_name,
            "file": rel,
            "destructor": has_dtor,
            "copy_ctor": "deleted" if copy_deleted else has_copy_ctor,
            "copy_assign": "deleted" if assign_deleted else has_copy_assign,
            "move_ctor": has_move_ctor,
            "move_assign": has_move_assign,
            "status": status,
        })

    return results


def _audit_event_lifecycle(
    files: list[Path],
    subscribe_pattern: str,
    unsubscribe_pattern: str,
    config: Config,
) -> list[dict]:
    """Find files with event subscriptions and check for matching unsubscriptions."""
    results: list[dict] = []
    sub_re = re.compile(subscribe_pattern)
    unsub_re = re.compile(unsubscribe_pattern)

    # Group by file — look at both .h and .cpp with same stem
    cpp_files = [f for f in files if f.suffix == ".cpp"]

    for fpath in cpp_files:
        try:
            content = fpath.read_text(errors="replace")
        except OSError:
            continue

        sub_count = len(sub_re.findall(content))
        if sub_count == 0:
            continue

        unsub_count = len(unsub_re.findall(content))
        rel = relative_path(fpath, config.root)

        status = "OK" if unsub_count >= sub_count else "POSSIBLE_LEAK"
        results.append({
            "file": rel,
            "subscribes": sub_count,
            "unsubscribes": unsub_count,
            "status": status,
        })

    return results


def _collect_deferred_markers(
    files: list[Path],
    config: Config,
) -> tuple[list[dict], dict[str, int]]:
    """Collect TODO/FIXME/HACK markers with file locations."""
    markers: list[dict] = []
    by_subsystem: dict[str, int] = defaultdict(int)
    pattern = re.compile(r"\b(TODO|FIXME|HACK|WORKAROUND|XXX)\b")

    for fpath in files:
        subsystem = _get_subsystem(fpath, config.root)
        try:
            with open(fpath, "r", errors="replace") as f:
                for i, line in enumerate(f, start=1):
                    m = pattern.search(line)
                    if m:
                        markers.append({
                            "file": relative_path(fpath, config.root),
                            "line": i,
                            "marker": m.group(1),
                            "text": line.strip()[:100],
                        })
                        by_subsystem[subsystem] += 1
        except OSError:
            continue

    return markers, dict(sorted(by_subsystem.items(), key=lambda x: -x[1]))
