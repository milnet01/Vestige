#!/usr/bin/env python3
"""Delete superseded GitHub Actions cache entries, keeping the newest per family.

ccache-action saves a new timestamped key on every run
(ccache-<job>-<config>-<ISO time>) and the FetchContent caches a new hashed key
whenever a CMakeLists changes (cmake-deps-...-<sha256>). Nothing deleted the
old ones, so the repository sat over GitHub's 10 GB cache limit (10.4 GB on
2026-09-28) and GitHub evicted by age, which can drop the entry a job is about
to restore (3D_E-0713).

A family is (git ref, key minus its timestamp or hash suffix). The newest entry
of each family is kept: it is the one a restore-keys prefix match picks anyway.
An entry whose key has neither suffix is not a family member and is kept. So is
everything when the listing fails: the script deletes only what it has
positively classified as superseded.

Usage: prune_actions_cache.py [--dry-run]. Needs `gh` authenticated with
actions: write (GH_TOKEN in CI). Run by .github/workflows/cache-prune.yml.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from collections import defaultdict

SUFFIX = re.compile(r"^(?P<family>.+)-(?:\d{4}-\d{2}-\d{2}T[\d:.]+Z|[0-9a-f]{64})$")


def list_caches() -> list[dict]:
    out = subprocess.run(
        ["gh", "cache", "list", "--limit", "1000",
         "--json", "id,key,ref,createdAt,sizeInBytes"],
        check=True, capture_output=True, text=True).stdout
    return json.loads(out)


def superseded(entries: list[dict]) -> list[dict]:
    families: dict[tuple[str, str], list[dict]] = defaultdict(list)
    for entry in entries:
        match = SUFFIX.match(entry["key"])
        if match:
            families[(entry["ref"], match["family"])].append(entry)
    stale = []
    for members in families.values():
        members.sort(key=lambda e: e["createdAt"], reverse=True)
        stale.extend(members[1:])
    return stale


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true",
                    help="list what would be deleted, delete nothing")
    args = ap.parse_args()

    entries = list_caches()
    stale = superseded(entries)
    total = sum(e["sizeInBytes"] for e in entries)
    freed = sum(e["sizeInBytes"] for e in stale)
    mb = 1024 * 1024
    print(f"{len(entries)} entries, {total // mb} MB; "
          f"{len(stale)} superseded, {freed // mb} MB")
    failed = 0
    for entry in stale:
        print(f"  {'would delete' if args.dry_run else 'delete'} "
              f"{entry['ref']} {entry['key']} ({entry['sizeInBytes'] // mb} MB)")
        if args.dry_run:
            continue
        result = subprocess.run(["gh", "cache", "delete", str(entry["id"])], check=False,
                                capture_output=True, text=True)
        if result.returncode != 0:
            failed += 1
            print(f"    failed: {result.stderr.strip()}", file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
