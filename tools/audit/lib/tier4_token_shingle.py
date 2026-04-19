# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT

"""Tier 4: DRY token-shingle similarity analysis.

Closes out idea #28 from AUDIT_TOOL_IMPROVEMENTS.md. Complements the
existing line-based duplication detector (``tier4_duplication``) which
finds *line-aligned* clones via a Rabin-Karp window. This one operates
at the *token* level — every word in the source is a token, and we hash
overlapping windows ("shingles") of K consecutive tokens — so it can
catch near-duplicates that the line-based scan misses:

* Two functions that differ only in formatting / brace style.
* A copy-pasted block whose body was reflowed by clang-format.
* Two switch arms whose token sequences match but whose line-shape
  diverges by one wrapped statement.

The output is a Jaccard similarity score per file pair (intersection
over union of shingle hashes). Pairs above ``similarity_threshold`` are
flagged. Severity is LOW — a high score is a *prompt* to look, not
proof of duplication; the pair may be two genuinely different functions
that happen to share boilerplate.

Intentionally cheap: tokenization is a regex split, hashing is Python's
``hash()`` (per-process stable is enough — we never persist these). On
a 100k-line corpus this runs in ~2 seconds with K=7.
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


# Tokenizer: identifiers, numbers, operators, punctuation. Whitespace and
# comments are stripped first; what remains is split into atomic tokens.
_TOKEN_RE = re.compile(
    r"[A-Za-z_]\w*"            # identifiers
    r"|0[xX][0-9A-Fa-f]+"      # hex literals
    r"|\d+(?:\.\d+)?"          # numeric literals
    r"|[+\-*/%=<>!&|^~?:]+"    # operator runs
    r"|[(){}\[\];,.]"          # punctuation
)

# Strip C/C++ comments and string literals before tokenizing. String
# literals would otherwise inflate similarity for files that share
# error-message templates.
_STRIP_RE = re.compile(
    r'//.*?$'                 # line comment
    r'|/\*.*?\*/'             # block comment
    r'|"(?:\\.|[^"\\])*"'     # string literal (replaced with empty)
    r"|'(?:\\.|[^'\\])*'",    # char literal (replaced with empty)
    re.DOTALL | re.MULTILINE,
)

_PY_STRIP_RE = re.compile(
    r'#.*?$'                   # line comment
    r'|""".*?"""'              # triple-double docstring
    r"|'''.*?'''"              # triple-single docstring
    r'|"(?:\\.|[^"\\])*"'      # string literal
    r"|'(?:\\.|[^'\\])*'",
    re.DOTALL | re.MULTILINE,
)

_C_FAMILY_EXTS = frozenset((
    ".cpp", ".hpp", ".cxx", ".cc", ".c", ".h", ".hh", ".hxx",
    ".glsl", ".vert", ".frag",
))

_EXEMPT_BASENAMES: frozenset[str] = frozenset({
    "auto_config.py",
    "tier2_patterns.py",
    "tier4_token_shingle.py",  # this file
    "AUDIT_TOOL_IMPROVEMENTS.md",
})


@dataclass
class ShinglePair:
    """A near-duplicate file pair surfaced by the Jaccard scan."""
    file_a: str
    file_b: str
    similarity: float
    shared_shingles: int
    shingles_a: int
    shingles_b: int

    def to_dict(self) -> dict[str, Any]:
        return {
            "file_a": self.file_a,
            "file_b": self.file_b,
            "similarity": round(self.similarity, 3),
            "shared_shingles": self.shared_shingles,
            "shingles_a": self.shingles_a,
            "shingles_b": self.shingles_b,
        }


@dataclass
class TokenShingleResult:
    """Result of the token-shingle similarity sweep."""
    pairs: list[ShinglePair] = field(default_factory=list)
    files_scanned: int = 0
    total_tokens: int = 0

    def to_dict(self) -> dict[str, Any]:
        return {
            "pairs": [p.to_dict() for p in self.pairs[:30]],
            "pair_count": len(self.pairs),
            "files_scanned": self.files_scanned,
            "total_tokens": self.total_tokens,
        }


def _strip_for_lang(text: str, ext: str) -> str:
    """Drop comments and string literals; return the cleaned text."""
    if ext == ".py":
        pattern = _PY_STRIP_RE
    elif ext in _C_FAMILY_EXTS:
        pattern = _STRIP_RE
    else:
        pattern = _STRIP_RE  # fallback — most projects are C-family

    def _replace(m: re.Match) -> str:
        # Preserve newlines so token line numbers stay roughly aligned.
        return re.sub(r"[^\n]", "", m.group(0))

    return pattern.sub(_replace, text)


def _tokenize(text: str, ext: str) -> list[str]:
    """Return the token sequence for the file (comments and literals removed)."""
    cleaned = _strip_for_lang(text, ext)
    return _TOKEN_RE.findall(cleaned)


def _shingle_hashes(tokens: list[str], k: int) -> set[int]:
    """Return the set of K-token shingle hashes for the token sequence."""
    if len(tokens) < k:
        return set()
    hashes: set[int] = set()
    for i in range(len(tokens) - k + 1):
        # tuple is hashable; Python's hash() is process-stable which is
        # all we need — these are never persisted.
        hashes.add(hash(tuple(tokens[i:i + k])))
    return hashes


def analyze_token_shingle(
    config: Config,
) -> tuple[TokenShingleResult, list[Finding]]:
    """Detect near-duplicate file pairs via token-shingle Jaccard similarity."""
    result = TokenShingleResult()
    findings: list[Finding] = []

    if not config.get("tier4", "token_shingle", "enabled", default=True):
        log.info("Tier 4 token_shingle: disabled")
        return result, findings

    k = int(config.get("tier4", "token_shingle", "shingle_size", default=7))
    threshold = float(config.get(
        "tier4", "token_shingle", "similarity_threshold", default=0.6,
    ))
    min_tokens = int(config.get(
        "tier4", "token_shingle", "min_tokens", default=80,
    ))
    max_findings = int(config.get(
        "tier4", "token_shingle", "max_findings", default=30,
    ))

    extensions = list(config.source_extensions) + [".glsl"]

    all_files = enumerate_files(
        root=config.root,
        source_dirs=config.source_dirs + config.shader_dirs,
        extensions=extensions,
        exclude_dirs=config.exclude_dirs,
    )
    scanned = [f for f in all_files if f.name not in _EXEMPT_BASENAMES]

    log.info(
        "Tier 4 token_shingle: scanning %d files (k=%d, threshold=%.2f)",
        len(scanned), k, threshold,
    )

    # Tokenize and shingle each file once.
    file_shingles: list[tuple[str, set[int]]] = []
    for fpath in scanned:
        try:
            text = fpath.read_text(errors="replace")
        except OSError:
            continue
        tokens = _tokenize(text, fpath.suffix.lower())
        if len(tokens) < min_tokens:
            continue
        shingles = _shingle_hashes(tokens, k)
        if not shingles:
            continue
        rel = relative_path(fpath, config.root)
        file_shingles.append((rel, shingles))
        result.total_tokens += len(tokens)

    result.files_scanned = len(file_shingles)
    if len(file_shingles) < 2:
        log.info("Tier 4 token_shingle: fewer than 2 files past min_tokens")
        return result, findings

    # All-pairs Jaccard. O(N^2) in file count; for the engine's ~250 files
    # this is ~30k comparisons of small sets, runs in well under a second.
    pairs: list[ShinglePair] = []
    for i in range(len(file_shingles)):
        rel_a, set_a = file_shingles[i]
        for j in range(i + 1, len(file_shingles)):
            rel_b, set_b = file_shingles[j]
            shared = len(set_a & set_b)
            if shared == 0:
                continue
            union = len(set_a | set_b)
            similarity = shared / union if union else 0.0
            if similarity < threshold:
                continue
            pairs.append(ShinglePair(
                file_a=rel_a, file_b=rel_b,
                similarity=similarity,
                shared_shingles=shared,
                shingles_a=len(set_a),
                shingles_b=len(set_b),
            ))

    pairs.sort(key=lambda p: -p.similarity)
    result.pairs = pairs

    for pair in pairs[:max_findings]:
        findings.append(Finding(
            file=pair.file_a,
            line=1,
            severity=Severity.LOW,
            category="duplication",
            source_tier=4,
            title=(
                f"Token-shingle similarity {pair.similarity:.0%} "
                f"with {pair.file_b}"
            ),
            detail=(
                f"Files share {pair.shared_shingles} of "
                f"{pair.shingles_a + pair.shingles_b - pair.shared_shingles} "
                f"unique {k}-token shingles "
                f"(Jaccard={pair.similarity:.3f}). Review for refactoring "
                f"opportunities — may be near-duplicates that the "
                f"line-aligned duplication scan missed."
            ),
            pattern_name="token_shingle_similarity",
        ))

    log.info(
        "Tier 4 token_shingle: %d file pairs above %.2f similarity",
        len(pairs), threshold,
    )
    return result, findings
