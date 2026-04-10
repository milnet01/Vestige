"""Finding suppression via .audit_suppress file."""

from __future__ import annotations

import logging
from pathlib import Path

from .findings import Finding

log = logging.getLogger("audit")


def load_suppressions(root: Path, filename: str = ".audit_suppress") -> set[str]:
    """Read dedup_key hashes from the suppress file.

    The file format is one key per line.  Lines starting with ``#`` are
    comments.  Blank lines are skipped.  Only the first whitespace-delimited
    token on each non-comment line is treated as a key so that inline
    annotations (e.g. ``# reason``) are allowed after the hash.
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
        token = line.split()[0]
        keys.add(token)

    log.info("Loaded %d suppression keys from %s", len(keys), path)
    return keys


def filter_suppressed(
    findings: list[Finding],
    suppressed: set[str],
) -> tuple[list[Finding], int]:
    """Return ``(active_findings, suppressed_count)``.

    Findings whose :pyattr:`dedup_key` appears in *suppressed* are removed
    from the active list.
    """
    active: list[Finding] = []
    count = 0
    for f in findings:
        if f.dedup_key in suppressed:
            count += 1
        else:
            active.append(f)
    return active, count


def save_suppression(
    root: Path,
    key: str,
    annotation: str,
    filename: str = ".audit_suppress",
) -> None:
    """Append *key* with a comment *annotation* to the suppress file.

    Creates the file if it does not exist yet.  A header comment is written
    on first creation.
    """
    path = root / filename
    if not path.exists():
        path.write_text(
            "# Audit finding suppressions\n"
            "# Format: <dedup_key_hash>  # optional annotation\n"
            "#\n"
            "# Add keys here (or use --suppress-add) to silence known findings.\n"
            "\n"
        )
    with path.open("a") as fh:
        fh.write(f"{key}  # {annotation}\n")
    log.info("Saved suppression key %s to %s", key, path)
