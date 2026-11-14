# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tier 4: Dead public-API detection.

Closes out idea #25 from AUDIT_TOOL_IMPROVEMENTS.md.

Scans public header files (those under ``source_dirs`` ending in ``.h``
or ``.hpp``) for class names and free-function declarations, then checks
whether each name is referenced anywhere in the source corpus outside
its own header and that header's own ``.cpp`` (same directory, same
stem). Symbols with zero external references are flagged as candidates
for removal (strict rule, user decision 2026-11-14: the declaration and
its definition are not uses).

This is intentionally a *grep-with-context* detector, not a clang-based
AST analysis. Tier 4 ships zero hard dependencies; bringing in libclang
would be a step change in the install surface. Trade-offs:

* Templates instantiated only via ADL or via a typedef in another
  header may appear dead, and so may a name used only through a string
  (a script binding). Outside the symbol's own two files every match
  counts as evidence of use, including matches inside comments and
  string literals.
* Names shorter than 4 characters are skipped (``id``, ``cb``, ``op``
  etc.) because their substring-match noise overwhelms any signal.
* Common base-class verbs (``init``, ``run``, ``begin``, ``end``,
  ``size``) are skipped for the same reason — see ``_NAME_BLOCKLIST``.

Severity: LOW — dead-code findings are cleanup hints, not bugs.
"""

from __future__ import annotations

import logging
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .config import Config
from .findings import Finding, Severity
from .utils import enumerate_files, relative_path

log = logging.getLogger("audit")


# Match a class/struct declaration. Captures the type name. Skips
# ``class Foo;`` forward declarations (we want only the definition with
# an opening ``{``).
_CLASS_DEF_RE = re.compile(
    r"^\s*(?:class|struct)\s+(?:\w+\s+)?"   # optional alignas/attribute
    r"([A-Z][A-Za-z0-9_]{3,})"              # PascalCase, length >= 4
    r"\s*(?:final\s*)?"
    r"(?::\s*[^{]+)?"                       # optional inheritance
    r"\{",
    re.MULTILINE,
)

# Match a free-function declaration in a header — the most common shape
# is `ReturnType identifier(...);`. We capture the identifier. The check
# requires the line to *not* start with `class`/`struct`/`enum`/`namespace`
# and to end with `;` (declaration, not inline definition — inline
# definitions are caught by the broader function-name walk in
# tier4_per_frame_alloc but here we only care about exported APIs).
_FREE_FUNC_DECL_RE = re.compile(
    r"^\s*"
    r"(?!(?:class|struct|enum|namespace|using|typedef|template|public|"
    r"private|protected|return|if|while|for|switch|case|default|else)\b)"
    r"(?:[\w:<>,\s\*&]+?\s+)?"              # return type
    r"([a-z][A-Za-z0-9_]{3,})"              # camelCase function name
    r"\s*\([^)]*\)\s*"                      # parameter list
    r"(?:const\s*)?"
    r"(?:noexcept\s*(?:\([^)]*\))?\s*)?"
    r";",                                   # declaration ends with ;
    re.MULTILINE,
)

# Names that look like identifiers but are too noisy to grep for. Many
# of these are virtual-method names ubiquitous across the codebase; if
# they really were dead, the broader ``unused_functions`` deadcode pass
# would catch them anyway.
_NAME_BLOCKLIST: frozenset[str] = frozenset({
    "init", "run", "stop", "start", "step", "tick", "main",
    "begin", "end", "size", "data", "empty", "clear",
    "reset", "update", "render", "draw", "load", "save",
    "set", "get", "has", "is", "to_string", "operator",
    # C++ keywords / operators that can otherwise leak through.
    "true", "false", "nullptr",
})

# Files that exist solely to describe the patterns this detector matches.
_EXEMPT_BASENAMES: frozenset[str] = frozenset({
    "auto_config.py",
    "tier2_patterns.py",
    "tier4_dead_public_api.py",   # this file
    "AUDIT_TOOL_IMPROVEMENTS.md",
})


@dataclass
class DeadPublicApiResult:
    """Result of the dead-public-API sweep."""
    dead_symbols: list[dict[str, Any]] = field(default_factory=list)
    total_symbols: int = 0
    headers_scanned: int = 0
    sources_scanned: int = 0

    def to_dict(self) -> dict[str, Any]:
        return {
            "dead_symbols": self.dead_symbols[:30],
            "total_symbols": self.total_symbols,
            "dead_count": len(self.dead_symbols),
            "headers_scanned": self.headers_scanned,
            "sources_scanned": self.sources_scanned,
        }


def _class_body_spans(text: str) -> list[tuple[int, int]]:
    """Return (open brace, close brace) offsets of each class/struct body.

    Braces are counted without regard to comments or strings; an
    unbalanced body runs to the end of the text.
    """
    spans: list[tuple[int, int]] = []
    for m in _CLASS_DEF_RE.finditer(text):
        open_at = m.end() - 1
        depth = 0
        close_at = len(text)
        for i in range(open_at, len(text)):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    close_at = i
                    break
        spans.append((open_at, close_at))
    return spans


def _extract_symbols_from_header(
    path: Path, rel: str,
) -> list[dict[str, Any]]:
    """Return the list of public symbols declared in this header.

    Each entry: ``{file, line, name, kind}``.
    """
    try:
        text = path.read_text(errors="replace")
    except OSError:
        return []

    symbols: list[dict[str, Any]] = []
    class_bodies = _class_body_spans(text)

    for m in _CLASS_DEF_RE.finditer(text):
        name = m.group(1)
        if name in _NAME_BLOCKLIST:
            continue
        line = text.count("\n", 0, m.start()) + 1
        symbols.append({
            "file": rel, "line": line, "name": name, "kind": "class",
        })

    for m in _FREE_FUNC_DECL_RE.finditer(text):
        name = m.group(1)
        if name in _NAME_BLOCKLIST:
            continue
        # A declaration inside a class body is a method, not a free
        # function: methods are used through their object.
        if any(start < m.start(1) < end for start, end in class_bodies):
            continue
        line = text.count("\n", 0, m.start()) + 1
        symbols.append({
            "file": rel, "line": line, "name": name, "kind": "function",
        })

    return symbols


def analyze_dead_public_api(
    config: Config,
) -> tuple[DeadPublicApiResult, list[Finding]]:
    """Detect public class / function declarations with no external callers."""
    result = DeadPublicApiResult()
    findings: list[Finding] = []

    if not config.get("tier4", "dead_public_api", "enabled", default=True):
        log.info("Tier 4 dead_public_api: disabled")
        return result, findings

    max_findings = int(
        config.get("tier4", "dead_public_api", "max_findings", default=50)
    )
    extra_blocklist = config.get(
        "tier4", "dead_public_api", "name_blocklist", default=[]
    )
    blocklist = _NAME_BLOCKLIST | {n for n in extra_blocklist}

    # Header files only — the detector treats these as the API surface.
    header_exts = [
        e for e in config.source_extensions
        if e.lower() in (".h", ".hpp", ".hh", ".hxx")
    ]
    if not header_exts:
        log.info("Tier 4 dead_public_api: no header extensions configured")
        return result, findings

    headers = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs,
        extensions=header_exts,
        exclude_dirs=config.exclude_dirs,
    )
    headers = [h for h in headers if h.name not in _EXEMPT_BASENAMES]
    result.headers_scanned = len(headers)

    # All sources (cpp + headers) form the corpus we grep against.
    sources = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs,
        extensions=config.source_extensions,
        exclude_dirs=config.exclude_dirs,
    )
    sources = [s for s in sources if s.name not in _EXEMPT_BASENAMES]
    result.sources_scanned = len(sources)

    # Extract symbols from each header, then check for references in the
    # corpus minus the declaring header and its own .cpp. This way, a class
    # that's only referenced inside its own header (typedef alias, friend
    # declaration, etc.) or defined in its own .cpp still counts as dead
    # from the project's perspective.
    all_symbols: list[dict[str, Any]] = []
    for hpath in headers:
        rel = relative_path(hpath, config.root)
        for sym in _extract_symbols_from_header(hpath, rel):
            if sym["name"] in blocklist:
                continue
            sym["_declaring_file"] = str(hpath)
            all_symbols.append(sym)
    result.total_symbols = len(all_symbols)

    if not all_symbols:
        log.info("Tier 4 dead_public_api: no public symbols extracted")
        return result, findings

    log.info(
        "Tier 4 dead_public_api: %d symbols across %d headers vs %d sources",
        result.total_symbols, result.headers_scanned, result.sources_scanned,
    )

    texts: dict[str, str] = {}
    for f in sources:
        try:
            texts[str(f)] = f.read_text(errors="replace")
        except OSError:
            continue
    full_corpus = "\n".join(texts.values())

    def own_files(header: str) -> list[str]:
        """The header itself and its .cpp: same directory, same stem."""
        h = Path(header)
        return [header] + [
            path for path in texts
            if path != header
            and Path(path).parent == h.parent
            and Path(path).stem == h.stem
            and Path(path).suffix.lower() not in header_exts
        ]

    for sym in all_symbols:
        # Word-boundary match — substring would hit `myFooBar` for `Foo`.
        pattern = re.compile(r"\b" + re.escape(sym["name"]) + r"\b")
        own_count = sum(
            len(pattern.findall(texts.get(path, "")))
            for path in own_files(sym["_declaring_file"])
        )
        external = len(pattern.findall(full_corpus)) - own_count
        if external <= 0:
            result.dead_symbols.append({
                "file": sym["file"],
                "line": sym["line"],
                "name": sym["name"],
                "kind": sym["kind"],
                "match_count": own_count,
            })

    # Emit findings (capped).
    for item in result.dead_symbols[:max_findings]:
        findings.append(Finding(
            file=item["file"],
            line=item["line"],
            severity=Severity.LOW,
            category="dead_code",
            source_tier=4,
            title=(
                f"Dead public {item['kind']}: '{item['name']}' "
                f"has no external callers"
            ),
            detail=(
                f"Symbol '{item['name']}' ({item['kind']}) is declared in "
                f"{item['file']}:{item['line']} but never referenced "
                f"outside its own header and .cpp ({item['match_count']} "
                f"match{'es' if item['match_count'] != 1 else ''} inside "
                f"them). Candidate for removal."
            ),
            pattern_name="dead_public_api",
        ))

    log.info(
        "Tier 4 dead_public_api: %d/%d public symbols appear unused",
        len(result.dead_symbols), result.total_symbols,
    )
    return result, findings
