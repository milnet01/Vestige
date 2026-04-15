# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tier 4: Cognitive complexity analysis (SonarSource algorithm)."""

from __future__ import annotations

import logging
import re
from dataclasses import dataclass, field
from typing import Any

from .config import Config
from .findings import Finding, Severity
from .utils import enumerate_files, relative_path

log = logging.getLogger("audit")

# ---------------------------------------------------------------------------
# Language detection (shared with tier4_refactoring)
# ---------------------------------------------------------------------------

_EXT_TO_LANG: dict[str, str] = {
    ".cpp": "cpp", ".hpp": "cpp", ".cxx": "cpp", ".cc": "cpp",
    ".c": "cpp", ".h": "cpp",
    ".py": "python",
    ".rs": "rust",
    ".go": "go",
    ".java": "java",
    ".js": "javascript", ".jsx": "javascript", ".mjs": "javascript",
    ".ts": "typescript", ".tsx": "typescript",
}

# ---------------------------------------------------------------------------
# Function extraction patterns
# ---------------------------------------------------------------------------

# C-family function signature: captures function name
_CFAMILY_FUNC_RE = re.compile(
    r'^\s*(?:(?:static|virtual|inline|explicit|constexpr|const|unsigned|signed|'
    r'void|int|float|double|bool|char|auto|std::\w+|[\w:]+)\s+)*'
    r'[*&]*\s*(\w+)\s*\([^)]*\)\s*(?:const|override|noexcept|final|\s)*\{',
    re.MULTILINE,
)

_RUST_FUNC_RE = re.compile(
    r'^\s*(?:pub(?:\([\w:]+\))?\s+)?(?:async\s+)?fn\s+(\w+)\s*(?:<[^>]*>)?\s*\([^)]*\)'
    r'(?:\s*->\s*[^{]+)?\s*\{',
    re.MULTILINE,
)

_GO_FUNC_RE = re.compile(
    r'^\s*func\s+(?:\(\w+\s+\*?\w+\)\s+)?(\w+)\s*\([^)]*\)\s*(?:[^{]*)?\{',
    re.MULTILINE,
)

_JAVA_FUNC_RE = re.compile(
    r'^\s*(?:(?:public|private|protected|static|final|abstract|synchronized)\s+)*'
    r'[\w<>\[\],\s]+\s+(\w+)\s*\([^)]*\)\s*(?:throws\s+[\w,\s]+)?\s*\{',
    re.MULTILINE,
)

_JS_FUNC_RE = re.compile(
    r'(?:function\s+(\w+)\s*\([^)]*\)\s*\{)',
    re.MULTILINE,
)

_PY_FUNC_RE = re.compile(
    r'^(\s*)(?:async\s+)?def\s+(\w+)\s*\(', re.MULTILINE,
)

_FUNC_PATTERNS: dict[str, re.Pattern] = {
    "cpp": _CFAMILY_FUNC_RE,
    "rust": _RUST_FUNC_RE,
    "go": _GO_FUNC_RE,
    "java": _JAVA_FUNC_RE,
    "javascript": _JS_FUNC_RE,
    "typescript": _JS_FUNC_RE,
}

# ---------------------------------------------------------------------------
# Control flow keywords that increment cognitive complexity
# ---------------------------------------------------------------------------

# Keywords that add +1 (base) and also receive a nesting increment
_NESTING_KEYWORDS_RE = re.compile(
    r'\b(if|for|while|do|switch|catch|except)\b'
)

# Break/continue to label
_LABEL_JUMP_RE = re.compile(
    r'\b(break|continue)\s+\w+',
)

# Ternary operator
_TERNARY_RE = re.compile(
    r'\?(?!=)',  # ? not followed by = (to avoid ?= operators)
)

# Logical operator sequences
_LOGICAL_AND_RE = re.compile(r'&&')
_LOGICAL_OR_RE = re.compile(r'\|\|')
_PYTHON_AND_RE = re.compile(r'\band\b')
_PYTHON_OR_RE = re.compile(r'\bor\b')


# ---------------------------------------------------------------------------
# String/comment stripping
# ---------------------------------------------------------------------------

def _strip_strings_and_comments(line: str, lang: str) -> str:
    """Remove string literals and comments from a line for analysis."""
    if lang == "python":
        # Remove # comments
        result = re.sub(r'#.*$', '', line)
        # Remove string literals
        result = re.sub(r'""".*?"""', '""', result)
        result = re.sub(r"'''.*?'''", "''", result)
        result = re.sub(r'"(?:\\.|[^"\\])*"', '""', result)
        result = re.sub(r"'(?:\\.|[^'\\])*'", "''", result)
        return result

    # C-family: remove // comments and string literals
    result = re.sub(r'//.*$', '', line)
    result = re.sub(r'"(?:\\.|[^"\\])*"', '""', result)
    result = re.sub(r"'(?:\\.|[^'\\])*'", "''", result)
    return result


# ---------------------------------------------------------------------------
# Cognitive complexity scoring — brace-based languages
# ---------------------------------------------------------------------------

def _score_logical_operators(code: str, lang: str) -> int:
    """Score logical operator sequences that mix && and ||.

    Per SonarSource: +1 for each new operator type in a sequence.
    A sequence of the same operator type (e.g. a && b && c) scores +1 total.
    Mixing types (a && b || c) scores +2 (one for &&, one for ||).
    """
    if lang == "python":
        and_ops = _PYTHON_AND_RE.findall(code)
        or_ops = _PYTHON_OR_RE.findall(code)
    else:
        and_ops = _LOGICAL_AND_RE.findall(code)
        or_ops = _LOGICAL_OR_RE.findall(code)

    score = 0
    if and_ops:
        score += 1
    if or_ops:
        score += 1
    return score


def _compute_cognitive_brace(
    lines: list[str], func_name: str, func_line: int,
    func_start_idx: int, lang: str,
) -> dict[str, Any]:
    """Compute cognitive complexity for a brace-delimited function body.

    func_start_idx is the 0-based index into `lines` where the opening
    brace of the function body is located.
    """
    score = 0
    details: list[str] = []
    brace_depth = 0
    # Track nesting of control structures (not raw braces)
    control_nesting = 0
    # Stack to track which braces correspond to control flow
    # Each entry: True if the brace was opened by a control structure
    brace_stack: list[bool] = []

    i = func_start_idx
    # Find the opening brace
    while i < len(lines):
        stripped = _strip_strings_and_comments(lines[i], lang)
        if '{' in stripped:
            brace_depth = 1
            brace_stack.append(False)  # function body brace
            i += 1
            break
        i += 1
    else:
        return {
            "name": func_name, "file": "", "line": func_line,
            "score": 0, "details": [],
        }

    while i < len(lines) and brace_depth > 0:
        raw_line = lines[i]
        code = _strip_strings_and_comments(raw_line, lang)
        stripped = code.strip()

        if not stripped:
            i += 1
            continue

        # --- Logical operator scoring (before control flow) ---
        logical_score = _score_logical_operators(code, lang)
        if logical_score > 0:
            score += logical_score
            details.append(
                f"  L{func_line + (i - func_start_idx)}: "
                f"+{logical_score} (logical operators)"
            )

        # --- Detect control flow keywords ---
        # Check for "else if" first (before standalone "if" or "else")
        is_else_if = bool(re.search(r'\belse\s+if\b', stripped))
        is_else = (not is_else_if and bool(re.search(r'\belse\b', stripped)))

        if is_else_if:
            # else if: +1 base, no nesting increment (flat)
            score += 1
            details.append(
                f"  L{func_line + (i - func_start_idx)}: "
                f"+1 (else if)"
            )
        elif is_else:
            # else: +1 base, no nesting increment (flat)
            score += 1
            details.append(
                f"  L{func_line + (i - func_start_idx)}: "
                f"+1 (else)"
            )

        # Check nesting keywords (if, for, while, do, switch, catch)
        # But NOT "else if" — the "if" part of "else if" is already counted
        nesting_matches = _NESTING_KEYWORDS_RE.findall(stripped)
        for kw in nesting_matches:
            # Skip 'if' when it's part of 'else if'
            if kw == "if" and is_else_if:
                continue
            # +1 base + nesting penalty
            increment = 1 + control_nesting
            score += increment
            details.append(
                f"  L{func_line + (i - func_start_idx)}: "
                f"+{increment} ({kw}, nesting={control_nesting})"
            )

        # Check for ternary
        ternary_matches = _TERNARY_RE.findall(code)
        for _ in ternary_matches:
            increment = 1 + control_nesting
            score += increment
            details.append(
                f"  L{func_line + (i - func_start_idx)}: "
                f"+{increment} (ternary, nesting={control_nesting})"
            )

        # Check for goto
        if re.search(r'\bgoto\b', stripped):
            score += 1
            details.append(
                f"  L{func_line + (i - func_start_idx)}: +1 (goto)"
            )

        # Check for labeled break/continue
        label_jumps = _LABEL_JUMP_RE.findall(stripped)
        for jump in label_jumps:
            score += 1
            details.append(
                f"  L{func_line + (i - func_start_idx)}: +1 ({jump} to label)"
            )

        # --- Track braces for nesting depth ---
        # Determine if this line opens a control structure brace
        has_control = bool(
            nesting_matches
            or is_else_if
            or is_else
        )
        # Filter: only count 'if' in nesting_matches when not else-if
        effective_control = has_control and not (
            nesting_matches == ['if'] and is_else_if
        )

        open_braces = code.count('{')
        close_braces = code.count('}')

        for _ in range(open_braces):
            # First opening brace on a line with a control keyword is a
            # control brace — it increments the nesting level
            if effective_control:
                brace_stack.append(True)
                control_nesting += 1
                effective_control = False  # only first brace per line
            else:
                brace_stack.append(False)
            brace_depth += 1

        for _ in range(close_braces):
            brace_depth -= 1
            if brace_depth < 0:
                brace_depth = 0
                break
            if brace_stack:
                was_control = brace_stack.pop()
                if was_control:
                    control_nesting = max(0, control_nesting - 1)

        i += 1

    return {
        "name": func_name, "file": "", "line": func_line,
        "score": score, "details": details,
    }


def _analyze_brace_language(
    content: str, lang: str, rel_path: str,
) -> list[dict[str, Any]]:
    """Analyze cognitive complexity for all functions in a brace-delimited file."""
    results: list[dict[str, Any]] = []
    lines = content.splitlines()
    pattern = _FUNC_PATTERNS.get(lang)
    if not pattern:
        return results

    # Collect all function locations
    func_locs: list[tuple[str, int, int]] = []  # (name, line_num, line_idx)

    for m in pattern.finditer(content):
        if lang in ("javascript", "typescript"):
            name = m.group(1) or "anonymous"
        else:
            name = m.group(1) or "unknown"

        # Skip control flow keywords that look like function names
        if name in ("if", "else", "for", "while", "switch", "catch",
                     "return", "throw", "case", "do", "try"):
            continue

        line_num = content[:m.start()].count("\n") + 1
        # Find the line index (0-based)
        line_idx = line_num - 1
        func_locs.append((name, line_num, line_idx))

    # Process each function
    for name, line_num, line_idx in func_locs:
        result = _compute_cognitive_brace(lines, name, line_num, line_idx, lang)
        result["file"] = rel_path
        results.append(result)

    return results


# ---------------------------------------------------------------------------
# Cognitive complexity scoring — Python (indentation-based)
# ---------------------------------------------------------------------------

def _analyze_python(content: str, rel_path: str) -> list[dict[str, Any]]:
    """Analyze cognitive complexity for all functions in a Python file."""
    results: list[dict[str, Any]] = []
    lines = content.splitlines()

    # Find all function definitions
    func_locs: list[tuple[str, int, int, int]] = []  # (name, line_num, line_idx, indent)

    for m in _PY_FUNC_RE.finditer(content):
        indent = len(m.group(1))
        name = m.group(2)
        line_num = content[:m.start()].count("\n") + 1
        line_idx = line_num - 1
        func_locs.append((name, line_num, line_idx, indent))

    for fi, (name, line_num, line_idx, func_indent) in enumerate(func_locs):
        score = 0
        details: list[str] = []

        # Determine function body boundaries
        body_start = line_idx + 1

        # Find where function body ends (next line at same or lesser indent,
        # or next function, or end of file)
        body_end = len(lines)
        if fi + 1 < len(func_locs):
            # Next function at same or lesser indent ends this one
            next_name, next_line, next_idx, next_indent = func_locs[fi + 1]
            if next_indent <= func_indent:
                body_end = next_idx

        # Detect indent unit from first indented body line
        indent_unit = 4  # default
        for k in range(body_start, min(body_end, len(lines))):
            if lines[k].strip():
                body_indent = len(lines[k]) - len(lines[k].lstrip())
                if body_indent > func_indent:
                    indent_unit = body_indent - func_indent
                    break

        # Process body lines
        # Track nesting of control structures via indentation
        # Each control keyword pushes its indent; lines deeper are nested
        control_stack: list[int] = []  # indent levels of control structures

        for k in range(body_start, min(body_end, len(lines))):
            raw_line = lines[k]
            if not raw_line.strip():
                continue

            line_indent = len(raw_line) - len(raw_line.lstrip())

            # If we've dedented past function scope, stop
            if line_indent <= func_indent and raw_line.strip():
                break

            code = _strip_strings_and_comments(raw_line, "python")
            stripped = code.strip()

            # Pop control stack entries that are at or deeper than current indent
            while control_stack and control_stack[-1] >= line_indent:
                control_stack.pop()

            nesting_level = len(control_stack)

            # --- Logical operator scoring ---
            logical_score = _score_logical_operators(code, "python")
            if logical_score > 0:
                score += logical_score
                details.append(
                    f"  L{k + 1}: +{logical_score} (logical operators)"
                )

            # Check for elif (flat +1, no nesting penalty)
            if re.match(r'\s*elif\b', raw_line):
                score += 1
                details.append(f"  L{k + 1}: +1 (elif)")
                # Push to control stack for nesting of children
                control_stack.append(line_indent)
                continue

            # Check for else (flat +1, no nesting penalty)
            if re.match(r'\s*else\s*:', raw_line):
                score += 1
                details.append(f"  L{k + 1}: +1 (else)")
                control_stack.append(line_indent)
                continue

            # Check nesting keywords
            nesting_kw_match = re.match(
                r'\s*(if|for|while|except|with)\b', raw_line
            )
            if nesting_kw_match:
                kw = nesting_kw_match.group(1)
                increment = 1 + nesting_level
                score += increment
                details.append(
                    f"  L{k + 1}: +{increment} ({kw}, nesting={nesting_level})"
                )
                control_stack.append(line_indent)
                continue

            # Ternary-style: "x if cond else y" inline
            # Only count if it's not a standalone if statement
            if re.search(r'\bif\b.*\belse\b', stripped) and not re.match(r'\s*if\b', raw_line):
                increment = 1 + nesting_level
                score += increment
                details.append(
                    f"  L{k + 1}: +{increment} (inline if/else, nesting={nesting_level})"
                )

        results.append({
            "name": name, "file": rel_path, "line": line_num,
            "score": score, "details": details,
        })

    return results


# ---------------------------------------------------------------------------
# Data structure
# ---------------------------------------------------------------------------

@dataclass
class CognitiveResult:
    """Results of cognitive complexity analysis."""
    functions: list[dict[str, Any]] = field(default_factory=list)
    files_scanned: int = 0
    total_functions: int = 0
    avg_complexity: float = 0.0
    max_complexity: int = 0
    above_threshold: int = 0

    def to_dict(self) -> dict[str, Any]:
        return {
            "functions": self.functions[:30],
            "files_scanned": self.files_scanned,
            "total_functions": self.total_functions,
            "avg_complexity": round(self.avg_complexity, 2),
            "max_complexity": self.max_complexity,
            "above_threshold": self.above_threshold,
        }


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def analyze_cognitive_complexity(
    config: Config,
) -> tuple[CognitiveResult, list[Finding]]:
    """Compute cognitive complexity for all functions in the codebase.

    Returns (CognitiveResult, list of Finding).
    """
    result = CognitiveResult()
    findings: list[Finding] = []

    if not config.get("tier4", "cognitive", "enabled", default=True):
        log.info("Tier 4 cognitive complexity: disabled")
        return result, findings

    threshold = config.get("tier4", "cognitive", "threshold", default=15)
    max_findings = config.get("tier4", "cognitive", "max_findings", default=50)

    # Enumerate source files
    all_files = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs,
        extensions=config.source_extensions,
        exclude_dirs=config.exclude_dirs,
    )

    log.info("Tier 4 cognitive complexity: analyzing %d files", len(all_files))

    all_functions: list[dict[str, Any]] = []

    for fpath in all_files:
        try:
            content = fpath.read_text(errors="replace")
        except OSError:
            continue

        ext = fpath.suffix.lower()
        lang = _EXT_TO_LANG.get(ext, "")
        if not lang:
            continue

        rel = relative_path(fpath, config.root)
        result.files_scanned += 1

        if lang == "python":
            funcs = _analyze_python(content, rel)
        else:
            funcs = _analyze_brace_language(content, lang, rel)

        all_functions.extend(funcs)

    # Sort by score descending
    all_functions.sort(key=lambda f: -f["score"])

    # Compute statistics
    result.total_functions = len(all_functions)
    if all_functions:
        total_score = sum(f["score"] for f in all_functions)
        result.avg_complexity = total_score / len(all_functions) if all_functions else 0.0
        result.max_complexity = all_functions[0]["score"] if all_functions else 0
        result.above_threshold = sum(1 for f in all_functions if f["score"] >= threshold)

    result.functions = all_functions

    # Generate findings — top N above threshold, sorted by score descending
    finding_count = 0
    for func in all_functions:
        if func["score"] < threshold:
            break
        if finding_count >= max_findings:
            break

        sev = Severity.HIGH if func["score"] >= 2 * threshold else Severity.MEDIUM

        detail_lines = "\n".join(func.get("details", [])[:10])
        if len(func.get("details", [])) > 10:
            detail_lines += f"\n  ... and {len(func['details']) - 10} more increments"

        findings.append(Finding(
            file=func["file"],
            line=func["line"],
            severity=sev,
            category="cognitive_complexity",
            source_tier=4,
            title=(
                f"High cognitive complexity: {func['name']}() "
                f"score {func['score']} (threshold {threshold})"
            ),
            detail=(
                f"Cognitive complexity {func['score']} exceeds threshold {threshold}. "
                f"Consider extracting helper functions or simplifying logic.\n"
                f"{detail_lines}"
            ),
            pattern_name="high_cognitive_complexity",
        ))
        finding_count += 1

    log.info(
        "Tier 4 cognitive complexity: %d functions analyzed, "
        "%d above threshold (%d), max score %d, avg %.1f",
        result.total_functions, result.above_threshold,
        threshold, result.max_complexity, result.avg_complexity,
    )

    return result, findings
