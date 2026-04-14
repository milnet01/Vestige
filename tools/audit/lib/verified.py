"""Finding verification via .audit_verified file.

D3 (2.5.0): Verification is a human-review signal. When a maintainer reviews
an audit report and confirms a finding is a real issue, they add its
``dedup_key`` to ``.audit_verified`` (either by hand or via
``--verified-add KEY``). On subsequent runs, findings whose key is in the
file carry a ``verified`` flag and render with a ``[VERIFIED]`` prefix —
letting downstream reviewers distinguish "reviewed, real, still needs
fixing" from "not yet looked at".

The file format mirrors ``.audit_suppress``: one ``dedup_key`` per line,
with optional inline ``# annotation`` after whitespace. Lines starting with
``#`` and blank lines are ignored.

Unlike suppressions, verification does **not** filter findings — verified
findings remain in the report so they stay visible. If a verified finding
is *also* suppressed, suppression wins (the finding is removed): a
reviewer who both verified and suppressed has explicitly said "yes this
is real, but I'm choosing to hide it for now", which is a valid pattern.
"""

from __future__ import annotations

import logging
from pathlib import Path

from .findings import Finding

log = logging.getLogger("audit")


def load_verified(root: Path, filename: str = ".audit_verified") -> set[str]:
    """Read dedup_key hashes from the verified file.

    The file format is one key per line. Lines starting with ``#`` are
    comments. Blank lines are skipped. Only the first whitespace-delimited
    token on each non-comment line is treated as a key so that inline
    annotations (e.g. ``# confirmed real 2026-04-14``) are allowed after
    the hash.
    """
    path = root / filename
    if not path.exists():
        return set()

    keys: set[str] = set()
    for raw_line in path.read_text().splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        # Take only the first token (the hash); anything after is annotation
        parts = line.split()
        if parts:
            keys.add(parts[0])

    log.info("Loaded %d verified keys from %s", len(keys), path)
    return keys


def apply_verified_tags(
    findings: list[Finding],
    verified: set[str],
) -> int:
    """Mark findings whose dedup_key is in *verified* as ``verified=True``.

    Returns the count of findings that were tagged. Mutates the findings
    in place. Findings not in the verified set are left untouched (their
    ``verified`` flag keeps its current value, which is ``False`` by
    default).
    """
    if not verified:
        return 0

    count = 0
    for f in findings:
        if f.dedup_key in verified:
            f.verified = True
            count += 1
    return count


def save_verified(
    root: Path,
    key: str,
    annotation: str,
    filename: str = ".audit_verified",
) -> None:
    """Append *key* with a comment *annotation* to the verified file.

    Creates the file if it does not exist yet. A header comment is
    written on first creation so anyone reading the file by hand
    understands what it is.
    """
    path = root / filename
    if not path.exists():
        path.write_text(
            "# Audit finding verifications\n"
            "# Format: <dedup_key_hash>  # optional annotation\n"
            "#\n"
            "# Add keys here (or use --verified-add) to mark findings as\n"
            "# reviewed-and-confirmed-real. Verification does NOT filter\n"
            "# the finding — it only tags it with [VERIFIED] so reviewers\n"
            "# can distinguish reviewed vs unreviewed issues at a glance.\n"
            "\n"
        )
    with path.open("a") as fh:
        fh.write(f"{key}  # {annotation}\n")
    log.info("Saved verified key %s to %s", key, path)


def remove_verified(
    root: Path,
    key: str,
    filename: str = ".audit_verified",
) -> bool:
    """Remove *key* from the verified file. Returns True if removed.

    If the key doesn't exist in the file, returns False without
    modifying the file. Preserves comments, blank lines, and inline
    annotations on other entries.
    """
    path = root / filename
    if not path.exists():
        return False

    lines = path.read_text().splitlines(keepends=True)
    out: list[str] = []
    removed = False
    for raw_line in lines:
        stripped = raw_line.strip()
        if not stripped or stripped.startswith("#"):
            out.append(raw_line)
            continue
        parts = stripped.split()
        if parts and parts[0] == key:
            removed = True
            continue  # drop this line
        out.append(raw_line)

    if removed:
        path.write_text("".join(out))
        log.info("Removed verified key %s from %s", key, path)
    return removed
