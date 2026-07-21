#!/usr/bin/env python3
"""Fetch game-ready realistic tree packs from Sketchfab as glTF (asset prep, slice T1).

Unlike the Poly Haven photoscans (film-quality, ~7-17M triangles — unusable as a real-time LOD0),
these LOLIPOP packs are BUILT for game engines: each ships LOD0-3 meshes + a billboard, PBR-textured,
at a game triangle budget (~8-22k LOD0). That matches the engine's TreeRenderer design directly
(real mesh LOD0 + impostor/billboard LOD1). Temperate species only (biome gate D10): pine, fir,
maple, birch — conifers + broadleaves coherent for a green meadow.

Sketchfab requires authentication even for free CC downloads, so this needs the account's API token
(Settings -> Password & API). Pass it via the SKETCHFAB_API_TOKEN env var (NOT an argv, to keep it
out of shell history):
    SKETCHFAB_API_TOKEN=<token> tools/asset_prep/fetch_sketchfab_trees.py [--res-note]

LICENCE: every pack is CC-BY 4.0 (Creative Commons Attribution) — free for commercial use (Steam-safe)
but attribution is REQUIRED. A CREDITS block is written to the SOURCES manifest; those lines must appear
in the game's credits screen. See docs/phases/phase_10_meadow_realism_c_trees_plants_design.md §6.

Canonical downloads land in the categorised asset library (Models/Nature/Trees/<name>_sketchfab_gltf/);
the engine's git-ignored assets/models/nature_local/ gets symlinks, keeping the repo clean. Stdlib only.
"""
from __future__ import annotations

import json
import os
import pathlib
import sys
import tempfile
import urllib.request
import zipfile

DOWNLOAD_API = "https://api.sketchfab.com/v3/models/{}/download"

# LOLIPOP (@lolipop_1707) temperate game-ready packs — {folder_name: (uid, model_page)}.
ASSETS = {
    "pine_lolipop": ("e1e9c07b8e2e445c943fec660beefba2",
                     "https://sketchfab.com/3d-models/pine-trees-pack-lowpoly-game-ready-lods-e1e9c07b8e2e445c943fec660beefba2"),
    "fir_lolipop": ("f58e8b6d733e4b0586e5b7db847b89e7",
                    "https://sketchfab.com/3d-models/realistic-fir-trees-pack-lods-gameready-f58e8b6d733e4b0586e5b7db847b89e7"),
    "maple_lolipop": ("b5d2833c258f4054a01ee2b4ef85adf0",
                      "https://sketchfab.com/3d-models/maple-trees-pack-lowpoly-game-ready-lods-b5d2833c258f4054a01ee2b4ef85adf0"),
    "birch_lolipop": ("08fe5117138e4fdaa7ca440ef1201e07",
                      "https://sketchfab.com/3d-models/five-birch-trees-pack-lowpoly-lods-08fe5117138e4fdaa7ca440ef1201e07"),
}

LIB_TREES = pathlib.Path("/mnt/Games/3D Engine Assets/Models/Nature/Trees")
PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[2]
NATURE_LOCAL = PROJECT_ROOT / "assets" / "models" / "nature_local"

_UA = "Mozilla/5.0 (X11; Linux x86_64) VestigeAssetPrep/1.0"


def _get(url: str, token: str | None = None) -> bytes:
    headers = {"User-Agent": _UA}
    if token:
        headers["Authorization"] = f"Token {token}"
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req, timeout=120) as r:
        return r.read()


def fetch_pack(folder: str, uid: str, page: str, token: str) -> dict:
    """Resolve the temporary glTF download URL, fetch the zip, extract into the library."""
    info = json.loads(_get(DOWNLOAD_API.format(uid), token))
    if "gltf" not in info:
        raise RuntimeError(f"{folder}: no glTF download offered (keys: {list(info)})")
    zip_bytes = _get(info["gltf"]["url"])  # signed URL, no auth header needed

    out_dir = LIB_TREES / f"{folder}_sketchfab_gltf"
    out_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(suffix=".zip") as tmp:
        tmp.write(zip_bytes)
        tmp.flush()
        with zipfile.ZipFile(tmp.name) as z:
            z.extractall(out_dir)

    link = NATURE_LOCAL / out_dir.name
    if link.is_symlink() or link.exists():
        link.unlink()
    link.symlink_to(out_dir)

    gltf = next((p.name for p in out_dir.rglob("*.gltf")), "?")
    print(f"  ✓ {folder}: {len(zip_bytes) // 1024} KiB → library/{out_dir.name}/ (gltf: {gltf}, symlinked)")
    return {"folder": out_dir.name, "gltf": gltf, "page": page}


def main() -> None:
    token = os.environ.get("SKETCHFAB_API_TOKEN")
    if not token:
        sys.exit("SKETCHFAB_API_TOKEN not set. Get it from https://sketchfab.com/settings/password "
                 "and run:  SKETCHFAB_API_TOKEN=<token> tools/asset_prep/fetch_sketchfab_trees.py")

    NATURE_LOCAL.mkdir(parents=True, exist_ok=True)
    records = [fetch_pack(folder, uid, page, token) for folder, (uid, page) in ASSETS.items()]

    manifest = LIB_TREES / "SOURCES_sketchfab.md"
    lines = ["# Vestige tree assets — Sketchfab sources & REQUIRED credits (3D_E-0033)", "",
             "Game-ready realistic tree packs by **LOLIPOP** (https://sketchfab.com/lolipop_1707),",
             "fetched by `tools/asset_prep/fetch_sketchfab_trees.py`. Each ships LOD0-3 + billboard, PBR.",
             "Loaded by the engine via symlinks in `assets/models/nature_local/`.", "",
             "## ⚠ CC-BY 4.0 — attribution REQUIRED (must appear in the game credits screen)", "",
             "> 3D tree models by **LOLIPOP** (sketchfab.com/lolipop_1707), "
             "licensed under CC BY 4.0 (creativecommons.org/licenses/by/4.0/).", ""]
    for r in records:
        lines.append(f"- `{r['folder']}/{r['gltf']}` — CC-BY 4.0 — {r['page']}")
    manifest.write_text("\n".join(lines) + "\n")
    print(f"Wrote {manifest}")
    print("REMINDER: CC-BY — add the credits block above to the game's credits screen.")


if __name__ == "__main__":
    main()
