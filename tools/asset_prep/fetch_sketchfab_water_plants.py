#!/usr/bin/env python3
"""Fetch realistic pond plants from Sketchfab as glTF (asset prep, slice T6 part 2).

Replaces the Kenney low-poly `lily_{small,large}.glb` pads on the meadow pond with
photo-textured lotus foliage. Searched the CC-licensed catalogue for a realistic
water lily first: everything else downloadable is hand-painted or stylised (a
lateral move from the Kenney pads), so these two Galaxy Abundant packs are the
realistic option that exists. Botanical note: lotus (Nemumbo) rather than a
temperate water lily (Nymphaea) — the silhouette reads correctly for a pond, and
no photoreal CC-licensed Nymphaea was found.

Unlike the LOLIPOP tree packs these ship NO artist LODs and are ~470-490 k faces
each, which is a photoscan-class budget the v2 tree design already rejected for
LOD0. They are therefore DECIMATED by tools/asset_prep/split_water_plants.py
before the engine ever sees them — a lily pad's detail is in its texture, not its
silhouette, so it decimates far better than a leaf-card tree would.

Sketchfab requires authentication even for free CC downloads, so this needs the
account's API token (Settings -> Password & API). Pass it via SKETCHFAB_API_TOKEN
(NOT an argv, to keep it out of shell history):
    SKETCHFAB_API_TOKEN=<token> tools/asset_prep/fetch_sketchfab_water_plants.py

LICENCE: both packs are CC-BY 4.0 — free for commercial use (Steam-safe) but
attribution is REQUIRED. The repo-tracked home of that obligation is the
ASSET_LICENSES.md + THIRD_PARTY_NOTICES.md rows; the SOURCES manifest this script
writes beside the downloads is a convenience record. See
docs/phases/phase_10_meadow_realism_c_trees_plants_design.md §6.3.

Downloads land in the categorised asset library (Models/Nature/Plants/); the
engine's git-ignored assets/models/nature_local/ gets symlinks. Stdlib only.
"""
from __future__ import annotations

import os
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from fetch_sketchfab_trees import NATURE_LOCAL, fetch_pack  # noqa: E402

# Galaxy Abundant (@galaxyabundant) pond plants — {folder_name: (uid, model_page)}.
ASSETS = {
    "lotus_pads_ga": (
        "79024211401e4cf1a26f2ef8c71fe1be",
        "https://sketchfab.com/3d-models/ga-free-201-small-lotus-leaf-cluster--tt-79024211401e4cf1a26f2ef8c71fe1be"),
    "lotus_flowers_ga": (
        "53d081e40cae4b5ebc66c6e06b5f19ef",
        "https://sketchfab.com/3d-models/ga-free-151-pink-lotus-flower-cluster-53d081e40cae4b5ebc66c6e06b5f19ef"),
}

LIB_PLANTS = pathlib.Path("/mnt/Games/3D Engine Assets/Models/Nature/Plants")


def main() -> None:
    token = os.environ.get("SKETCHFAB_API_TOKEN")
    if not token:
        sys.exit("SKETCHFAB_API_TOKEN not set. Get it from https://sketchfab.com/settings/password "
                 "and run:  SKETCHFAB_API_TOKEN=<token> "
                 "tools/asset_prep/fetch_sketchfab_water_plants.py")

    NATURE_LOCAL.mkdir(parents=True, exist_ok=True)
    records = [fetch_pack(folder, uid, page, token, LIB_PLANTS)
               for folder, (uid, page) in ASSETS.items()]

    manifest = LIB_PLANTS / "SOURCES_sketchfab_water.md"
    lines = ["# Vestige pond-plant assets — Sketchfab sources & REQUIRED credits (3D_E-0033 T6)", "",
             "Photo-textured lotus foliage by **Galaxy Abundant** (https://sketchfab.com/galaxyabundant),",
             "fetched by `tools/asset_prep/fetch_sketchfab_water_plants.py`, decimated by",
             "`split_water_plants.py`. Loaded by the engine via symlinks in `assets/models/nature_local/`.", "",
             "## ⚠ CC-BY 4.0 — attribution REQUIRED (must appear in the game credits screen)", "",
             "> 3D pond-plant models by **Galaxy Abundant** (sketchfab.com/galaxyabundant), "
             "licensed under CC BY 4.0 (creativecommons.org/licenses/by/4.0/).", ""]
    for r in records:
        lines.append(f"- `{r['folder']}/{r['gltf']}` — CC-BY 4.0 — {r['page']}")
    manifest.write_text("\n".join(lines) + "\n")
    print(f"Wrote {manifest}")
    print("REMINDER: CC-BY — add the credits block above to the game's credits screen.")
    print("NEXT: tools/asset_prep/split_water_plants.py (decimate + export gameready/water/)")


if __name__ == "__main__":
    main()
