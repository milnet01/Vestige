"""Tier 4: Code duplication detection using Rabin-Karp rolling hash."""

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

# ---------------------------------------------------------------------------
# Comment-stripping patterns (covers all supported languages)
# ---------------------------------------------------------------------------

# C-family: C, C++, Java, Go, Rust, JS, TS, GLSL
_C_COMMENT_RE = re.compile(
    r'//.*?$'           # line comment
    r'|/\*.*?\*/'       # block comment
    r'|"(?:\\.|[^"\\])*"'  # double-quoted string (preserve)
    r"|'(?:\\.|[^'\\])*'",  # single-quoted string (preserve)
    re.DOTALL | re.MULTILINE,
)

# Python
_PY_COMMENT_RE = re.compile(
    r'#.*?$'               # line comment
    r'|""".*?"""'          # triple-double docstring
    r"|'''.*?'''"          # triple-single docstring
    r'|"(?:\\.|[^"\\])*"'  # double-quoted string (preserve)
    r"|'(?:\\.|[^'\\])*'",  # single-quoted string (preserve)
    re.DOTALL | re.MULTILINE,
)

_IDENTIFIER_RE = re.compile(r'\b[a-zA-Z_]\w*\b')

_C_FAMILY_EXTS = frozenset((
    ".cpp", ".hpp", ".cxx", ".cc", ".c", ".h",
    ".java", ".js", ".jsx", ".mjs", ".ts", ".tsx",
    ".go", ".rs", ".glsl", ".vert", ".frag",
))

# Rabin-Karp constants
_BASE = 257
_MOD = (1 << 61) - 1  # Mersenne prime


# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------

@dataclass
class ClonePair:
    """A pair of duplicated code blocks."""
    file_a: str
    file_b: str
    start_a: int
    end_a: int
    start_b: int
    end_b: int
    lines: int
    clone_type: int  # 1 = exact, 2 = renamed identifiers

    def to_dict(self) -> dict[str, Any]:
        return {
            "file_a": self.file_a,
            "file_b": self.file_b,
            "start_a": self.start_a,
            "end_a": self.end_a,
            "start_b": self.start_b,
            "end_b": self.end_b,
            "lines": self.lines,
            "clone_type": self.clone_type,
        }


@dataclass
class DuplicationResult:
    """Results of code duplication analysis."""
    clone_pairs: list[ClonePair] = field(default_factory=list)
    duplicated_lines: int = 0
    total_lines_scanned: int = 0
    duplication_pct: float = 0.0
    files_scanned: int = 0

    def to_dict(self) -> dict[str, Any]:
        return {
            "clone_pairs": [c.to_dict() for c in self.clone_pairs[:30]],
            "duplicated_lines": self.duplicated_lines,
            "total_lines_scanned": self.total_lines_scanned,
            "duplication_pct": round(self.duplication_pct, 1),
            "files_scanned": self.files_scanned,
        }


# ---------------------------------------------------------------------------
# Tokenizer
# ---------------------------------------------------------------------------

def _strip_comments(content: str, ext: str) -> str:
    """Remove comments from source code, preserving string literals."""
    if ext == ".py":
        pattern = _PY_COMMENT_RE
    elif ext in _C_FAMILY_EXTS:
        pattern = _C_COMMENT_RE
    else:
        pattern = _C_COMMENT_RE  # fallback

    def _replace(m: re.Match) -> str:
        text = m.group(0)
        # Preserve string literals
        if text.startswith(("'", '"')):
            return text
        # Replace comment with space (preserves line count for block comments)
        return re.sub(r'[^\n]', '', text)

    return pattern.sub(_replace, content)


def _tokenize_file(
    path: Path, normalize_ids: bool = False,
) -> list[tuple[int, str]] | None:
    """Tokenize a file, returning (original_line_number, normalized_line) pairs.

    Returns None if the file is binary or unreadable.
    """
    try:
        raw = path.read_bytes()
    except OSError:
        return None

    # Skip binary files
    if b'\x00' in raw[:512]:
        return None

    try:
        content = raw.decode("utf-8", errors="replace")
    except Exception:
        return None

    ext = path.suffix.lower()
    stripped = _strip_comments(content, ext)

    result: list[tuple[int, str]] = []
    for i, line in enumerate(stripped.splitlines(), start=1):
        normalized = " ".join(line.split())  # collapse whitespace
        if not normalized:
            continue
        if normalize_ids:
            normalized = _IDENTIFIER_RE.sub("$ID", normalized)
        result.append((i, normalized))

    return result


# ---------------------------------------------------------------------------
# Rabin-Karp rolling hash
# ---------------------------------------------------------------------------

def _rabin_karp_hashes(
    lines: list[str], window: int,
) -> dict[int, list[int]]:
    """Compute rolling hashes over windows of consecutive normalized lines.

    Returns: hash_value -> [list of starting indices into `lines`].
    """
    n = len(lines)
    if n < window:
        return {}

    index: dict[int, list[int]] = {}

    # Pre-compute BASE^(window-1) mod MOD
    base_pow = pow(_BASE, window - 1, _MOD)

    # Compute initial hash for the first window
    h = 0
    for j in range(window):
        h = (h * _BASE + hash(lines[j])) % _MOD

    index.setdefault(h, []).append(0)

    # Slide the window
    for i in range(1, n - window + 1):
        old_hash = hash(lines[i - 1])
        new_hash = hash(lines[i + window - 1])
        h = ((h - old_hash * base_pow) * _BASE + new_hash) % _MOD
        index.setdefault(h, []).append(i)

    return index


# ---------------------------------------------------------------------------
# Clone detection
# ---------------------------------------------------------------------------

def _find_clones(
    file_data: dict[str, tuple[list[int], list[str]]],
    window: int,
    clone_type: int,
) -> list[ClonePair]:
    """Detect cloned blocks across all files using hash-bucket approach.

    file_data: {rel_path: (original_line_numbers, normalized_lines)}
    """
    # Build per-file hash indices
    file_hashes: dict[str, dict[int, list[int]]] = {}
    for rel_path, (_, norm_lines) in file_data.items():
        file_hashes[rel_path] = _rabin_karp_hashes(norm_lines, window)

    # Build global hash index: hash -> [(file, start_idx)]
    global_index: dict[int, list[tuple[str, int]]] = {}
    for rel_path, hash_map in file_hashes.items():
        for h, starts in hash_map.items():
            for s in starts:
                global_index.setdefault(h, []).append((rel_path, s))

    # Find matches from hash collisions
    raw_pairs: list[ClonePair] = []
    seen_pairs: set[tuple[str, int, str, int]] = set()

    for entries in global_index.values():
        if len(entries) < 2:
            continue

        for i in range(len(entries)):
            for j in range(i + 1, len(entries)):
                fa, sa = entries[i]
                fb, sb = entries[j]

                # Skip self-matches at same position in same file
                if fa == fb and sa == sb:
                    continue

                # Canonical order to avoid duplicate pairs
                if (fa, sa) > (fb, sb):
                    fa, sa, fb, sb = fb, sb, fa, sa

                pair_key = (fa, sa, fb, sb)
                if pair_key in seen_pairs:
                    continue

                # Verify exact match
                lines_a = file_data[fa][1]
                lines_b = file_data[fb][1]

                if lines_a[sa:sa + window] != lines_b[sb:sb + window]:
                    continue

                # Extend match forward
                end_a = sa + window
                end_b = sb + window
                while (end_a < len(lines_a) and end_b < len(lines_b)
                       and lines_a[end_a] == lines_b[end_b]):
                    end_a += 1
                    end_b += 1

                match_len = end_a - sa

                # Mark all sub-windows as seen to avoid overlapping reports
                for offset in range(match_len - window + 1):
                    sub_key = (fa, sa + offset, fb, sb + offset)
                    seen_pairs.add(sub_key)

                # Map back to original line numbers
                orig_lines_a = file_data[fa][0]
                orig_lines_b = file_data[fb][0]
                orig_start_a = orig_lines_a[sa]
                orig_end_a = orig_lines_a[min(end_a - 1, len(orig_lines_a) - 1)]
                orig_start_b = orig_lines_b[sb]
                orig_end_b = orig_lines_b[min(end_b - 1, len(orig_lines_b) - 1)]

                raw_pairs.append(ClonePair(
                    file_a=fa, file_b=fb,
                    start_a=orig_start_a, end_a=orig_end_a,
                    start_b=orig_start_b, end_b=orig_end_b,
                    lines=match_len,
                    clone_type=clone_type,
                ))

    return raw_pairs


def _merge_overlapping(pairs: list[ClonePair]) -> list[ClonePair]:
    """Merge overlapping clone pairs that share the same file pair."""
    if not pairs:
        return []

    # Group by (file_a, file_b)
    groups: dict[tuple[str, str], list[ClonePair]] = {}
    for p in pairs:
        key = (p.file_a, p.file_b)
        groups.setdefault(key, []).append(p)

    merged: list[ClonePair] = []
    for _, group in groups.items():
        group.sort(key=lambda p: (p.start_a, p.start_b))

        current = group[0]
        for nxt in group[1:]:
            # Check if nxt overlaps with current in file A
            if nxt.start_a <= current.end_a + 1:
                # Merge: extend current to cover both
                current = ClonePair(
                    file_a=current.file_a, file_b=current.file_b,
                    start_a=current.start_a,
                    end_a=max(current.end_a, nxt.end_a),
                    start_b=current.start_b,
                    end_b=max(current.end_b, nxt.end_b),
                    lines=max(current.end_a, nxt.end_a) - current.start_a + 1,
                    clone_type=current.clone_type,
                )
            else:
                merged.append(current)
                current = nxt
        merged.append(current)

    return merged


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def analyze_duplication(config: Config) -> tuple[DuplicationResult, list[Finding]]:
    """Detect code clones across the project.

    Returns (DuplicationResult, list of Finding).
    """
    result = DuplicationResult()
    findings: list[Finding] = []

    if not config.get("tier4", "duplication", "enabled", default=True):
        log.info("Tier 4 duplication: disabled")
        return result, findings

    min_lines = config.get("tier4", "duplication", "min_lines", default=5)
    normalize_ids = config.get("tier4", "duplication", "normalize_ids", default=True)
    max_findings = config.get("tier4", "duplication", "max_findings", default=50)

    # Enumerate source files
    all_files = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs + config.shader_dirs,
        extensions=config.source_extensions + [".glsl"],
        exclude_dirs=config.exclude_dirs,
    )

    log.info("Tier 4 duplication: scanning %d files (min_lines=%d)", len(all_files), min_lines)

    # Tokenize all files (Type-1: no ID normalization)
    file_data: dict[str, tuple[list[int], list[str]]] = {}
    for fpath in all_files:
        tokens = _tokenize_file(fpath, normalize_ids=False)
        if tokens and len(tokens) >= min_lines:
            rel = relative_path(fpath, config.root)
            line_nums = [t[0] for t in tokens]
            norm_lines = [t[1] for t in tokens]
            file_data[rel] = (line_nums, norm_lines)
            result.total_lines_scanned += len(tokens)

    result.files_scanned = len(file_data)

    if len(file_data) < 2:
        log.info("Tier 4 duplication: fewer than 2 scannable files, skipping")
        return result, findings

    # Type-1 clone detection (exact matches)
    window = min_lines
    type1_clones = _find_clones(file_data, window, clone_type=1)
    type1_clones = _merge_overlapping(type1_clones)

    # Type-2 clone detection (renamed identifiers) if enabled
    type2_clones: list[ClonePair] = []
    if normalize_ids:
        # Build normalized file data
        norm_file_data: dict[str, tuple[list[int], list[str]]] = {}
        for fpath in all_files:
            tokens = _tokenize_file(fpath, normalize_ids=True)
            if tokens and len(tokens) >= min_lines:
                rel = relative_path(fpath, config.root)
                line_nums = [t[0] for t in tokens]
                norm_lines = [t[1] for t in tokens]
                norm_file_data[rel] = (line_nums, norm_lines)

        type2_raw = _find_clones(norm_file_data, window, clone_type=2)
        type2_raw = _merge_overlapping(type2_raw)

        # Exclude Type-2 clones that overlap with Type-1 (already reported)
        type1_keys = {(c.file_a, c.start_a, c.file_b, c.start_b) for c in type1_clones}
        type2_clones = [c for c in type2_raw
                        if (c.file_a, c.start_a, c.file_b, c.start_b) not in type1_keys]

    all_clones = type1_clones + type2_clones
    all_clones.sort(key=lambda c: -c.lines)  # largest first

    result.clone_pairs = all_clones
    result.duplicated_lines = sum(c.lines for c in all_clones)
    if result.total_lines_scanned > 0:
        result.duplication_pct = (result.duplicated_lines / result.total_lines_scanned) * 100

    # Generate findings
    for clone in all_clones[:max_findings]:
        sev = Severity.MEDIUM if clone.lines >= 20 else Severity.LOW
        findings.append(Finding(
            file=clone.file_a,
            line=clone.start_a,
            severity=sev,
            category="duplication",
            source_tier=4,
            title=(f"Duplicated block ({clone.lines} lines) "
                   f"also in {clone.file_b}:{clone.start_b}"),
            detail=f"Type-{clone.clone_type} clone: "
                   f"{clone.file_a}:{clone.start_a}-{clone.end_a} "
                   f"<-> {clone.file_b}:{clone.start_b}-{clone.end_b}",
            pattern_name="code_clone",
        ))

    log.info("Tier 4 duplication: %d clone pairs (%d Type-1, %d Type-2), "
             "%d duplicated lines (%.1f%%)",
             len(all_clones), len(type1_clones), len(type2_clones),
             result.duplicated_lines, result.duplication_pct)

    return result, findings
