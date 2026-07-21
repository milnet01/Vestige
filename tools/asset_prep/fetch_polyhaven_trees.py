#!/usr/bin/env python3
"""Fetch CC0 tree models from Poly Haven as game-ready glTF (Blender-free asset prep, slice T1).

Poly Haven serves the same 4K library assets as glTF at a chosen texture resolution via its public
API (https://api.polyhaven.com/files/<asset>), skipping the local .blend.zip extraction step. NOTE:
these are FULL photoscan meshes (fir_tree_01.bin is ~478 MB, ~millions of triangles) — unusable as a
close-up LOD0 as-is. They MUST be decimated to the design's hero budget (~20-40k tris) before use:
run `blender-5.2 --background --python tools/asset_prep/decimate_trees.py` (Blender is installed).
This fetcher only pulls the canonical source; decimation is the next asset-prep step.
See docs/phases/phase_10_meadow_realism_c_trees_plants_design.md §6.

Canonical downloads live in the categorised asset library (Models/Nature/Trees/<name>_<res>_gltf/);
the engine's git-ignored assets/models/nature_local/ gets symlinks, keeping the repo clean. The API's
per-file `include` map is authoritative for texture URLs (they live under /jpg/<res>/, not beside the
glTF), so we resolve every buffer/image from it rather than guessing paths.

All assets are CC0 (public domain) — no attribution required. A SOURCES manifest is written beside
the downloads. Stdlib only; no third-party deps.

Usage:  tools/asset_prep/fetch_polyhaven_trees.py [--res 1k|2k|4k]
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import urllib.request

API = "https://api.polyhaven.com/files/{}"

# Temperate-coherent Poly Haven trees only (biome gate D10): conifers fir + pine. Desert/tropical
# library assets (quiver_tree, jacaranda, island_tree) are deliberately excluded.
ASSETS = ["fir_tree_01", "pine_tree_01"]

LIB_TREES = pathlib.Path("/mnt/Games/3D Engine Assets/Models/Nature/Trees")
PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[2]
NATURE_LOCAL = PROJECT_ROOT / "assets" / "models" / "nature_local"


# Poly Haven's CDN 403s the default Python-urllib User-Agent; send a normal browser UA.
_UA = "Mozilla/5.0 (X11; Linux x86_64) VestigeAssetPrep/1.0"


def _get(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": _UA})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def fetch_asset(name: str, res: str) -> dict:
    """Download one asset's glTF + all referenced files; return a SOURCES record."""
    meta = json.loads(_get(API.format(name)))
    node = meta["gltf"][res]["gltf"]
    folder = f"{name}_{res}_gltf"
    out_dir = LIB_TREES / folder
    (out_dir / "textures").mkdir(parents=True, exist_ok=True)

    gltf_rel = f"{name}_{res}.gltf"
    (out_dir / gltf_rel).write_bytes(_get(node["url"]))

    files = 1
    for rel, info in node.get("include", {}).items():
        dst = out_dir / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(_get(info["url"]))
        files += 1

    # Symlink the library asset folder into the engine's nature_local/ (git-ignored).
    link = NATURE_LOCAL / folder
    if link.is_symlink() or link.exists():
        link.unlink()
    link.symlink_to(out_dir)

    print(f"  ✓ {name} ({res}): {files} files → library/{folder}/ (symlinked into nature_local/)")
    return {"asset": name, "folder": folder, "res": res, "gltf": gltf_rel,
            "license": "CC0", "source": f"https://polyhaven.com/a/{name}"}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--res", default="2k", choices=["1k", "2k", "4k"])
    args = ap.parse_args()

    NATURE_LOCAL.mkdir(parents=True, exist_ok=True)
    records = [fetch_asset(name, args.res) for name in ASSETS]

    manifest = LIB_TREES / "SOURCES_vestige.md"
    lines = ["# Vestige tree assets — sources & licences (3D_E-0033)", "",
             "Game-ready glTF fetched from Poly Haven (CC0, public domain, no attribution required)",
             "by `tools/asset_prep/fetch_polyhaven_trees.py`. Canonical copies live here; the engine",
             "loads them via symlinks in `assets/models/nature_local/`.", ""]
    for r in records:
        lines.append(f"- **{r['asset']}** ({r['res']}) — `{r['folder']}/{r['gltf']}` — {r['license']} — {r['source']}")
    manifest.write_text("\n".join(lines) + "\n")
    print(f"Wrote {manifest}")


if __name__ == "__main__":
    main()
