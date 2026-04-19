# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tier 4: Per-frame heap allocations inside hot-path functions.

Closes out idea #18 from AUDIT_TOOL_IMPROVEMENTS.md.

The 60 FPS budget is 16.6 ms per frame. A single ``new`` / ``make_unique``
inside a per-frame call path is rarely fatal in isolation, but they add
up: the 2026-04-19 audit found a half-dozen render/update paths that
allocated transient ``std::string`` / ``std::vector`` per draw, several
of which were inside tight inner loops and caused measurable jank on the
RX 6600 reference machine. The proper fix is almost always to reserve()
once, hoist the buffer, or use a string_view overload — but first you
have to *find* the allocations, which is what this detector does.

Strategy (deliberately heuristic, not a real AST walk):

1. Identify "per-frame" functions by name. Any C++ free function or
   member whose name matches one of ``per_frame_function_patterns``
   (default: ``render``, ``draw``, ``update``, ``onFrame``, ``onUpdate``,
   ``tick``) is treated as on the per-frame call path. The patterns are
   case-insensitive substrings of the function identifier.
2. For each matching function body, walk the brace-balanced range and
   look for allocation tokens: ``new T``, ``std::make_unique``,
   ``std::make_shared``, ``std::vector<...>(`` constructor calls with a
   non-zero size argument, ``std::string(`` ctor calls, and the
   ``"..." + std::to_string(...)`` idiom.
3. Inside the function body, additionally check whether the allocation
   sits inside a ``for``/``while``/``do`` loop. Loop-nested allocations
   are reported at MEDIUM; non-loop allocations at LOW.

This is intentionally a *grep-with-context* detector, not a clang-based
AST analysis. Tier 4 ships zero hard dependencies; pulling in libclang
would be a large addition. The trade-off is documented FPs:

* Lambdas or nested classes share the outer function's name window —
  false positives possible if e.g. a callback's body allocates and the
  outer function is called once at startup. Reviewers should look at
  the flagged line, not the function header.
* ``new T`` inside a ``static`` initializer (one-shot allocation) is
  flagged. Treat the LOW severity as a "verify, don't auto-fix" prompt.
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


# Function-signature regex: matches ``ReturnType name(...)`` plus optional
# ``const`` / ``noexcept`` / ``override`` / ``= 0`` / etc, terminated by
# ``{`` (definition, not pure declaration). Captures the function name.
# Deliberately permissive — we'd rather trace into a ctor than miss it.
_FUNC_DEF_RE = re.compile(
    r"^\s*"                               # leading indentation
    r"(?:[\w:<>,\s\*&]+?\s+)?"            # optional return type / qualifiers
    r"(?:[A-Za-z_]\w*::)*"                # optional Class:: qualifier(s)
    r"([A-Za-z_]\w*)"                     # function name (captured)
    r"\s*\(([^;{}]*)\)\s*"                # parameter list — no ; or { inside
    r"(?:const\s*)?"
    r"(?:noexcept\s*(?:\([^)]*\))?\s*)?"
    r"(?:override\s*)?"
    r"(?:final\s*)?"
    r"\{",                                # opening brace marks definition
    re.MULTILINE,
)

# Allocation patterns. Each entry: (name, regex). The detector reports
# the first match per line.
_ALLOC_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    ("new",            re.compile(r"=\s*new\s+\w")),
    ("make_unique",    re.compile(r"\bstd::make_unique\s*<")),
    ("make_shared",    re.compile(r"\bstd::make_shared\s*<")),
    # std::vector<T>(N, ...) ctor — vector default-ctor (no args) is
    # zero-cost so we require at least one argument.
    ("vector_ctor",    re.compile(r"\bstd::vector\s*<[^>]+>\s*\(\s*\S")),
    # std::string ctor with arguments. Empty std::string s; is fine
    # (small-string optimization), so require at least one arg inside
    # the parens.
    ("string_ctor",    re.compile(r"\bstd::string\s*\(\s*\S")),
    # "literal" + std::to_string(...) — heap-allocates a temporary.
    ("string_concat",  re.compile(r'"\s*\+\s*std::to_string\s*\(')),
)

# Loop-construct keywords. A line containing one of these inside the
# function body opens a loop scope; we track brace depth to know when
# we leave it.
_LOOP_KEYWORD_RE = re.compile(r"\b(for|while|do)\s*[\(\{]")

# Substrings on a line that opt it out of all checks. Comments are
# stripped separately; this catches `static const auto foo = new ...`
# and `[[maybe_unused]]` style markers where the heap alloc is
# deliberate one-shot init.
_EXCLUDE_LINE_SUBSTRINGS: tuple[str, ...] = (
    "static const ",
    "static inline ",
    "// ALLOC-OK",          # explicit reviewer ack
    "// per-frame-ok",      # explicit reviewer ack
)

# Files that exist solely to describe the patterns this detector matches.
# Skip them to avoid the rule firing on its own description strings.
_EXEMPT_BASENAMES: frozenset[str] = frozenset({
    "auto_config.py",
    "tier2_patterns.py",
    "tier4_per_frame_alloc.py",   # this file
    "AUDIT_TOOL_IMPROVEMENTS.md",
})

# Default per-frame function-name patterns. Substring match against the
# function identifier captured by ``_FUNC_DEF_RE``, case-insensitive.
_DEFAULT_PER_FRAME_PATTERNS: tuple[str, ...] = (
    "render",
    "draw",
    "update",
    "onframe",
    "onupdate",
    "tick",
)


@dataclass
class PerFrameAllocResult:
    """Result of the per-frame heap-alloc sweep."""
    findings: list[dict[str, Any]] = field(default_factory=list)
    functions_scanned: int = 0

    def to_dict(self) -> dict[str, Any]:
        return {
            "findings": self.findings[:30],
            "count": len(self.findings),
            "functions_scanned": self.functions_scanned,
        }


def _strip_line_comment(line: str) -> str:
    """Remove a trailing ``// ...`` comment; preserves the rest of the line."""
    # Don't strip if the // is inside a string literal — count quotes
    # before the //.
    idx = line.find("//")
    while idx != -1:
        prefix = line[:idx].replace(r"\"", "")
        if prefix.count('"') % 2 == 0:
            return line[:idx].rstrip()
        idx = line.find("//", idx + 2)
    return line


def _function_name_matches(name: str, patterns: tuple[str, ...]) -> bool:
    """Case-insensitive substring match against per-frame patterns."""
    lname = name.lower()
    return any(pat in lname for pat in patterns)


def _find_function_bodies(
    text: str, patterns: tuple[str, ...],
) -> list[tuple[str, int, int, int]]:
    """Return (function_name, start_offset, end_offset, start_line) tuples.

    ``start_offset`` points at the opening ``{`` of the body; ``end_offset``
    at the matching closing ``}`` (exclusive). Brace tracking is
    string- and comment-aware enough to handle the common cases.
    """
    bodies: list[tuple[str, int, int, int]] = []
    for m in _FUNC_DEF_RE.finditer(text):
        name = m.group(1)
        if not _function_name_matches(name, patterns):
            continue
        # Some matches will be control-flow keywords (``if``, ``while``, ...)
        # — _function_name_matches handles "while" via "tick"/"render"
        # not matching, but explicitly skip the C/C++ keyword set so a
        # pattern like ``"if"`` (a user override) doesn't blow up.
        if name in {"if", "for", "while", "do", "switch", "catch", "return"}:
            continue
        brace_open = m.end() - 1     # position of the '{'
        depth = 0
        i = brace_open
        in_string = False
        in_char = False
        in_line_comment = False
        in_block_comment = False
        while i < len(text):
            c = text[i]
            nxt = text[i + 1] if i + 1 < len(text) else ""
            if in_line_comment:
                if c == "\n":
                    in_line_comment = False
            elif in_block_comment:
                if c == "*" and nxt == "/":
                    in_block_comment = False
                    i += 1
            elif in_string:
                if c == "\\":
                    i += 1
                elif c == '"':
                    in_string = False
            elif in_char:
                if c == "\\":
                    i += 1
                elif c == "'":
                    in_char = False
            else:
                if c == "/" and nxt == "/":
                    in_line_comment = True
                    i += 1
                elif c == "/" and nxt == "*":
                    in_block_comment = True
                    i += 1
                elif c == '"':
                    in_string = True
                elif c == "'":
                    in_char = True
                elif c == "{":
                    depth += 1
                elif c == "}":
                    depth -= 1
                    if depth == 0:
                        # Compute line of the opening brace.
                        start_line = text.count("\n", 0, brace_open) + 1
                        bodies.append((name, brace_open, i, start_line))
                        break
            i += 1
    return bodies


def _scan_body(
    body: str, body_start_line: int,
) -> list[tuple[int, str, str, bool]]:
    """Scan a function body for allocations.

    Returns a list of ``(line_number, alloc_kind, line_text, in_loop)``.
    Loop nesting is tracked by counting brace-depth changes after a line
    that contains a ``for``/``while``/``do`` keyword.
    """
    hits: list[tuple[int, str, str, bool]] = []

    loop_stack: list[int] = []      # brace depth at each loop entry
    depth = 0                       # current brace depth relative to body

    for offset, raw_line in enumerate(body.splitlines()):
        # Check the raw line for opt-out markers first — they live in
        # comments (e.g. ``// ALLOC-OK``) which the comment stripper
        # would otherwise erase before the substring check could see them.
        if any(sub in raw_line for sub in _EXCLUDE_LINE_SUBSTRINGS):
            line = _strip_line_comment(raw_line)
            depth += line.count("{") - line.count("}")
            while loop_stack and depth <= loop_stack[-1]:
                loop_stack.pop()
            continue

        line = _strip_line_comment(raw_line)
        if not line.strip():
            depth += line.count("{") - line.count("}")
            while loop_stack and depth <= loop_stack[-1]:
                loop_stack.pop()
            continue

        # Detect loop opening on this line. We push a marker at the
        # current depth so the next "}" that drops us back to it pops.
        if _LOOP_KEYWORD_RE.search(line):
            loop_stack.append(depth)

        # Allocation check
        in_loop = bool(loop_stack)
        for kind, regex in _ALLOC_PATTERNS:
            if regex.search(line):
                hits.append((
                    body_start_line + offset,
                    kind,
                    line.strip()[:120],
                    in_loop,
                ))
                break

        # Update depth at end of line, then pop loops once we drop
        # back to the depth at which they were opened. Using ``<=`` so
        # that single-line braced loops (``for (...) { stmt; }``) pop
        # in the same line they open.
        depth += line.count("{") - line.count("}")
        while loop_stack and depth <= loop_stack[-1]:
            loop_stack.pop()

    return hits


def _scan_file(
    path: Path, rel: str, patterns: tuple[str, ...],
) -> tuple[list[dict[str, Any]], int]:
    """Scan one source file for per-frame heap allocations.

    Returns (findings_list, functions_scanned).
    """
    try:
        text = path.read_text(errors="replace")
    except OSError:
        return [], 0

    bodies = _find_function_bodies(text, patterns)
    if not bodies:
        return [], 0

    results: list[dict[str, Any]] = []
    for name, body_start, body_end, body_start_line in bodies:
        body = text[body_start:body_end]
        for line_no, kind, snippet, in_loop in _scan_body(body, body_start_line):
            results.append({
                "file": rel,
                "line": line_no,
                "function": name,
                "alloc_kind": kind,
                "in_loop": in_loop,
                "text": snippet,
            })
    return results, len(bodies)


def analyze_per_frame_alloc(
    config: Config,
) -> tuple[PerFrameAllocResult, list[Finding]]:
    """Detect heap allocations inside per-frame functions."""
    result = PerFrameAllocResult()
    findings: list[Finding] = []

    if not config.get("tier4", "per_frame_alloc", "enabled", default=True):
        log.info("Tier 4 per_frame_alloc: disabled")
        return result, findings

    user_patterns = config.get(
        "tier4", "per_frame_alloc", "function_patterns",
        default=list(_DEFAULT_PER_FRAME_PATTERNS),
    )
    patterns = tuple(p.lower() for p in user_patterns)

    max_findings = int(
        config.get("tier4", "per_frame_alloc", "max_findings", default=50)
    )

    # C++ only — header inline definitions are scanned too.
    extensions = [
        e for e in config.source_extensions
        if e.lower() in (".cpp", ".cxx", ".cc", ".c", ".h", ".hpp")
    ]
    if not extensions:
        extensions = [".cpp", ".h"]

    all_files = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs,
        extensions=extensions,
        exclude_dirs=config.exclude_dirs,
    )
    scanned = [f for f in all_files if f.name not in _EXEMPT_BASENAMES]

    log.info(
        "Tier 4 per_frame_alloc: scanning %d files (patterns=%s)",
        len(scanned), list(patterns),
    )

    for fpath in scanned:
        rel = relative_path(fpath, config.root)
        hits, n_funcs = _scan_file(fpath, rel, patterns)
        result.findings.extend(hits)
        result.functions_scanned += n_funcs

    # Emit findings — loop-nested allocations get MEDIUM, others LOW.
    for item in result.findings[:max_findings]:
        sev = Severity.MEDIUM if item["in_loop"] else Severity.LOW
        scope = "inside loop" if item["in_loop"] else "in function body"
        findings.append(Finding(
            file=item["file"],
            line=item["line"],
            severity=sev,
            category="performance",
            source_tier=4,
            title=(
                f"Per-frame heap alloc ({item['alloc_kind']}) "
                f"in {item['function']}() {scope}"
            ),
            detail=(
                f"Allocation kind '{item['alloc_kind']}' on per-frame call "
                f"path. Hoist the buffer, reserve() once, or use a "
                f"string_view overload to avoid the per-frame heap hit. "
                f"Line: {item['text']}"
            ),
            pattern_name="per_frame_heap_alloc",
        ))

    log.info(
        "Tier 4 per_frame_alloc: %d allocations across %d per-frame functions",
        len(result.findings), result.functions_scanned,
    )
    return result, findings
