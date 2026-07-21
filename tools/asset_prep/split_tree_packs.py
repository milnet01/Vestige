#!/usr/bin/env python3
"""Split LOLIPOP Sketchfab tree packs into clean per-tree LOD0 glb props (asset prep, slice T1).

Each downloaded pack (tools/asset_prep/fetch_sketchfab_trees.py) bundles 3-15 trees, each with LOD0-3
meshes + a billboard, all in one scene. The engine wants ONE tree per file. This tool runs under
Blender headless (invisible — no GUI):

    blender-5.2 --background --python tools/asset_prep/split_tree_packs.py

For every tree it exports the LOD0 (up-close) mesh, recentred so the trunk base sits at the origin
(y=0, centred in x/z), as a self-contained .glb into the categorised library
(Models/Nature/Trees/gameready/<species>/<species>_<tree>_lod0.glb), symlinked into the engine's
git-ignored assets/models/nature_local/gameready/. Textures are embedded in each glb.

Only LOD0 is exported here (the near mesh — the unambiguous T1 need). Billboards / LOD2 are a later
step once the LOD scheme is settled. See docs/phases/phase_10_meadow_realism_c_trees_plants_design.md.
Junk artist props (reference planes / ground checkers) are skipped by name.

PROTOTYPE — pending finalisation per design §6.2 (v2): this pass exports self-contained .glb with
EMBEDDED textures, which duplicates a species' bark/foliage maps across its variant files (fir ~64 MB
each) and would blow the VRAM budget. The finalised tool must export glTF-SEPARATE into a per-species
dir with a single SHARED textures/ folder (ResourceManager dedupes by path), also export LOD2 +
billboard, and match size words case-insensitively ([A-Za-z]+, currently drops Acer_Sapling_*).
"""
import bpy
import re
import pathlib
from mathutils import Vector

LIB = pathlib.Path("/mnt/Games/3D Engine Assets/Models/Nature/Trees")
OUT = LIB / "gameready"
PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[2]
NATURE_LOCAL = PROJECT_ROOT / "assets" / "models" / "nature_local"

# species -> (pack folder, per-pack classifier). Classifier(name) -> (tree_id, lod:int) or None to skip.
# lod 0 = near mesh; 3 = billboard. Junk names return None.
SKIP = re.compile(r"(Ref_plane|Back_Checker|Ground_Checker|^Back$|^Ground$|Checker)", re.I)


def classify_generic(name):
    # Pine_big_1_LOD0_Bark_Mat_0 / Acer_large_2_LOD2_Cluster_Mat_0 / *_Billboard_LOD3_*
    m = re.match(r"^(?P<tree>(?:Pine|Acer)_[a-z]+_\d+)_(?:Billboard_)?LOD(?P<lod>\d)_", name)
    return (m.group("tree"), int(m.group("lod"))) if m else None


def classify_fir(name):
    # Christmas tree_LOD0_Bark_Mat_0 / Christmas tree_2_LOD3_..._Billboard_Mat_0
    m = re.match(r"^(?P<tree>Christmas tree(?:_\d)?)_LOD(?P<lod>\d)_", name)
    return (m.group("tree"), int(m.group("lod"))) if m else None


def classify_birch(name):
    # LOD1/2:  Birch_2_LOD1_Birch_bark_0     LOD0 (no tag):  Birch_2_Birch_bark_0
    m = re.match(r"^(?P<tree>Birch(?:_\d)?)_LOD(?P<lod>\d)_Birch_", name)
    if m:
        return (m.group("tree"), int(m.group("lod")))
    m = re.match(r"^(?P<tree>Birch(?:_\d)?)_Birch_(?:bark|Atlas)_0$", name)
    return (m.group("tree"), 0) if m else None


PACKS = {
    "pine": ("pine_lolipop_sketchfab_gltf", classify_generic),
    "fir": ("fir_lolipop_sketchfab_gltf", classify_fir),
    "maple": ("maple_lolipop_sketchfab_gltf", classify_generic),
    "birch": ("birch_lolipop_sketchfab_gltf", classify_birch),
}


def clear_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def world_bbox(objs):
    lo = Vector((1e30, 1e30, 1e30))
    hi = Vector((-1e30, -1e30, -1e30))
    for o in objs:
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c)
            lo = Vector((min(lo[i], w[i]) for i in range(3)))
            hi = Vector((max(hi[i], w[i]) for i in range(3)))
    return lo, hi


def export_tree(species, tree, objs):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    # Detach from parent empties so translation/export is self-contained.
    bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
    # Recentre: base to y=0, centred in x/z (glTF is Y-up after export).
    lo, hi = world_bbox(objs)
    shift = Vector((-(lo.x + hi.x) / 2, -(lo.y + hi.y) / 2, -lo.z))  # Blender is Z-up here
    for o in objs:
        o.location += shift

    safe = re.sub(r"[^A-Za-z0-9]+", "_", tree).strip("_").lower()
    out_dir = OUT / species
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"{species}_{safe}_lod0.glb"
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.ops.export_scene.gltf(filepath=str(out), export_format="GLB",
                              use_selection=True, export_apply=True, export_yup=True)
    return out.name


def process_pack(species, folder, classify):
    gltf = next((LIB / folder).rglob("*.gltf"))
    clear_scene()
    bpy.ops.import_scene.gltf(filepath=str(gltf))
    # Group LOD0 mesh objects by tree.
    trees = {}
    for o in bpy.context.scene.objects:
        if o.type != "MESH" or SKIP.search(o.name):
            continue
        c = classify(o.name)
        if c and c[1] == 0:
            trees.setdefault(c[0], []).append(o)
    made = []
    for tree, objs in sorted(trees.items()):
        made.append(export_tree(species, tree, objs))
    print(f"[{species}] {len(made)} LOD0 trees exported: {', '.join(made)}")
    return made


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    total = 0
    for species, (folder, classify) in PACKS.items():
        total += len(process_pack(species, folder, classify))
    # Symlink gameready/ into the engine's nature_local/.
    NATURE_LOCAL.mkdir(parents=True, exist_ok=True)
    link = NATURE_LOCAL / "gameready"
    if link.is_symlink() or link.exists():
        link.unlink()
    link.symlink_to(OUT)
    print(f"DONE: {total} LOD0 tree glb exported to {OUT} (symlinked into nature_local/gameready/)")


main()
