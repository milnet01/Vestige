#!/usr/bin/env python3
"""Print the apt packages ci.yml's Linux jobs install, one list, deduplicated.

scripts/ci-container/Containerfile builds the local Ubuntu 24.04 CI image from
this output, so the image installs what GitHub installs and there is one package
list: the `packages:` inputs of every ./.github/actions/apt-install step in
.github/workflows/ci.yml. Exits 1 if it finds none, so a renamed step cannot
quietly yield an image with nothing in it.
"""
from __future__ import annotations

import sys
from pathlib import Path

import yaml

WORKFLOW = Path(__file__).resolve().parent.parent / ".github/workflows/ci.yml"


def main() -> int:
    data = yaml.safe_load(WORKFLOW.read_text(encoding="utf-8"))
    packages: list[str] = []
    for job in data["jobs"].values():
        for step in job.get("steps", []):
            if step.get("uses") == "./.github/actions/apt-install":
                packages.extend(str(step["with"]["packages"]).split())
    if not packages:
        print("ci_apt_packages: no apt-install steps found in ci.yml", file=sys.stderr)
        return 1
    print(" ".join(sorted(set(packages))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
