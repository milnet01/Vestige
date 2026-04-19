# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tier 4: Dead-shader detection — flag .glsl files under shader_dirs whose
basename is never referenced from any engine/ or app/ source file.

Closes out idea #26 from AUDIT_TOOL_IMPROVEMENTS.md.

**FP caveat (load-bearing):** the 2026-04-19 manual audit flagged
``ssr.frag.glsl`` as dead because the only reference was via a runtime
computed path (``shaderDir + "ssr.frag.glsl"``). A naive check that looks
for ``"ssr.frag.glsl"`` as a literal string *inside* the source files
still wouldn't catch that either — the string constant in the source is
``"ssr.frag"`` (the extension is appended elsewhere). The fix is
**substring matching on the basename-minus-extension(s)**: for
``ssr.frag.glsl`` we accept any of ``ssr.frag.glsl``, ``ssr.frag``, or
``ssr`` appearing anywhere in a source file as evidence of use.

This is deliberately permissive: we'd rather miss a truly dead shader
than incorrectly flag a live one. Users can follow up with ``grep``
manually if they're suspicious of a specific shader. The detector's
job is to surface candidates, not be the source of truth.
"""

from __future__ import annotations

import logging
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .config import Config
from .findings import Finding, Severity
from .utils import enumerate_files, relative_path

log = logging.getLogger("audit")


@dataclass
class DeadShaderResult:
    """Result of the dead-shader sweep."""
    dead_shaders: list[dict[str, Any]] = field(default_factory=list)
    total_shaders: int = 0

    def to_dict(self) -> dict[str, Any]:
        return {
            "dead_shaders": self.dead_shaders[:30],
            "total_shaders": self.total_shaders,
            "dead_count": len(self.dead_shaders),
        }


def _shader_candidate_tokens(shader_path: Path) -> list[str]:
    """Return the set of substrings any of which, if present in a source
    file, counts as a reference to this shader.

    For ``ssr.frag.glsl`` this returns
    ``["ssr.frag.glsl", "ssr.frag", "ssr"]`` — a source file only needs
    to contain one of them to pass. Order is longest-first so the
    early-out in the caller short-circuits on the most specific token
    first.
    """
    name = shader_path.name  # e.g. "ssr.frag.glsl"
    tokens: list[str] = [name]
    # Strip suffixes one at a time: "ssr.frag.glsl" -> "ssr.frag" -> "ssr"
    stem = name
    while "." in stem:
        stem = stem.rsplit(".", 1)[0]
        if stem and stem not in tokens:
            tokens.append(stem)
    return tokens


def analyze_dead_shaders(config: Config) -> tuple[DeadShaderResult, list[Finding]]:
    """Detect GLSL shader files that are not referenced by any source file."""
    result = DeadShaderResult()
    findings: list[Finding] = []

    if not config.get("tier4", "dead_shaders", "enabled", default=True):
        log.info("Tier 4 dead_shaders: disabled")
        return result, findings

    if not config.shader_dirs:
        log.info("Tier 4 dead_shaders: no shader_dirs configured — skipping")
        return result, findings

    max_findings = config.get(
        "tier4", "dead_shaders", "max_findings", default=30
    )

    # Enumerate shader files under the configured shader dirs.
    shader_files = enumerate_files(
        root=config.root,
        source_dirs=config.shader_dirs,
        extensions=[".glsl"],
        exclude_dirs=config.exclude_dirs,
    )
    result.total_shaders = len(shader_files)

    if not shader_files:
        log.info("Tier 4 dead_shaders: no shader files found")
        return result, findings

    # Enumerate all source files (.cpp/.h by default) — the corpus we grep
    # against.
    source_files = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs,
        extensions=config.source_extensions,
        exclude_dirs=config.exclude_dirs,
    )

    # Slurp every source file into memory once — this avoids N*M disk reads
    # and lets us do a simple substring scan per shader. On a project of
    # a few hundred KLOC this is still trivially small (<50 MB).
    source_blob_parts: list[str] = []
    for f in source_files:
        try:
            source_blob_parts.append(f.read_text(errors="replace"))
        except OSError:
            continue
    source_blob = "\n".join(source_blob_parts)

    log.info(
        "Tier 4 dead_shaders: %d shaders vs %d source files (%d chars)",
        len(shader_files), len(source_files), len(source_blob),
    )

    for shader in shader_files:
        tokens = _shader_candidate_tokens(shader)
        referenced = any(tok in source_blob for tok in tokens)
        if referenced:
            continue

        rel = relative_path(shader, config.root)
        result.dead_shaders.append({
            "file": rel,
            "shader_name": shader.name,
            "checked_tokens": tokens,
        })

    # Emit findings (capped).
    for item in result.dead_shaders[:max_findings]:
        findings.append(Finding(
            file=item["file"],
            line=1,
            severity=Severity.LOW,
            category="dead_code",
            source_tier=4,
            title=f"Dead shader: {item['shader_name']} not referenced",
            detail=(
                f"Shader file appears unused — none of the tokens "
                f"{item['checked_tokens']} were found in any source file"
            ),
            pattern_name="dead_shader",
        ))

    log.info(
        "Tier 4 dead_shaders: %d/%d shaders appear dead",
        len(result.dead_shaders), result.total_shaders,
    )
    return result, findings
