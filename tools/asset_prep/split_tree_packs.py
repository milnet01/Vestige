#!/usr/bin/env python3
"""Split LOLIPOP Sketchfab tree packs into per-tree, per-LOD glTF props with shared textures (slice T1).

Each downloaded pack (tools/asset_prep/fetch_sketchfab_trees.py) bundles 3-15 trees, each with LOD0-2
meshes + (usually) a billboard, all in one scene sharing one per-species bark+foliage texture set. The
engine wants one tree per file, at a game VRAM budget. This tool runs under Blender headless (invisible):

    blender-5.2 --background --python tools/asset_prep/split_tree_packs.py

For every tree it exports three tiers as **glTF-separate** (.gltf + .bin + external textures):
  <species>_<tree>_lod0.gltf   near mesh (LOD0)
  <species>_<tree>_lod2.gltf   mid mesh (LOD2)
  <species>_<tree>_billboard.gltf   far flat card (LOD3)   — absent for birch (ships LOD0-2 only)
recentred so the trunk base sits at the origin, into Models/Nature/Trees/gameready/<species>/, symlinked
into the engine's git-ignored assets/models/nature_local/gameready/.

VRAM discipline (design §6.2/§9 — the engine uploads textures UNCOMPRESSED with mips, so pixel
resolution sets VRAM, and there is no BC/KTX2 path): before exporting, the per-species image datablocks
are downscaled ONCE in-Blender — mesh maps to <=1024 on the longest edge, billboard maps to <=512 — and
all of a species' tiers are exported into the SAME directory so the shared bark/foliage maps are written
once (billboard maps are legitimately per-variant). `export_keep_originals` is deliberately NOT used: it
would reference the un-downscaled 4K source maps and blow the budget (e.g. birch 4x4096 ~= 340 MiB).
See docs/phases/phase_10_meadow_realism_c_trees_plants_design.md §6.2/§9.
"""
import bpy
import re
import pathlib
import shutil
import subprocess
from mathutils import Vector

LIB = pathlib.Path("/mnt/Games/3D Engine Assets/Models/Nature/Trees")
OUT = LIB / "gameready"
PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[2]
NATURE_LOCAL = PROJECT_ROOT / "assets" / "models" / "nature_local"

MESH_CAP = 1024   # longest edge, shared per-species mesh maps
BILLBOARD_CAP = 512  # per-variant billboard cards (distant)

# Junk artist props to skip.
SKIP = re.compile(r"(Ref_plane|Back_Checker|Ground_Checker|^Back$|^Ground$|Checker)", re.I)


# Each classifier maps a mesh-object name -> (tree_id, tier) or None. tier in {"lod0","lod2","billboard"};
# LOD1 (barely lighter than LOD0) is intentionally skipped -> the renderer's 3-bucket scheme (§4.1).
def classify_generic(name):
    # Pine_big_1_LOD0_Bark_Mat_0 / Acer_Sapling_2_LOD2_Cluster_Mat_0 / Pine_big_1_Billboard_LOD3_..._0
    m = re.match(r"^(?P<tree>(?:Pine|Acer)_[A-Za-z]+_\d+)_(?P<bb>Billboard_)?LOD(?P<lod>\d)_", name)
    if not m:
        return None
    return _tier(m.group("tree"), m.group("lod"), m.group("bb"))


def classify_fir(name):
    # Christmas tree_LOD0_Bark_Mat_0 / Christmas tree_2_LOD3_..._Billboard_Mat_0  (LOD3 = billboard)
    m = re.match(r"^(?P<tree>Christmas tree(?:_\d)?)_LOD(?P<lod>\d)_", name)
    if not m:
        return None
    bb = "Billboard" if m.group("lod") == "3" else None
    return _tier(m.group("tree"), m.group("lod"), bb)


def classify_birch(name):
    # LOD2:  Birch_2_LOD2_Birch_bark_0     LOD0 (no tag):  Birch_2_Birch_bark_0     (no billboard)
    m = re.match(r"^(?P<tree>Birch(?:_\d)?)_LOD(?P<lod>\d)_Birch_", name)
    if m:
        return _tier(m.group("tree"), m.group("lod"), None)
    m = re.match(r"^(?P<tree>Birch(?:_\d)?)_Birch_(?:bark|Atlas)_0$", name)
    return (m.group("tree"), "lod0") if m else None


def _tier(tree, lod, bb):
    if bb:
        return (tree, "billboard")
    if lod == "0":
        return (tree, "lod0")
    if lod == "2":
        return (tree, "lod2")
    return None  # LOD1 skipped


PACKS = {
    "pine": ("pine_lolipop_sketchfab_gltf", classify_generic),
    "fir": ("fir_lolipop_sketchfab_gltf", classify_fir),
    "maple": ("maple_lolipop_sketchfab_gltf", classify_generic),
    "birch": ("birch_lolipop_sketchfab_gltf", classify_birch),
}


def clear_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def downscale_written():
    """Downscale the exported textures on disk (mesh <=MESH_CAP longest edge, billboard <=BILLBOARD_CAP).

    Done as a post-export pass with ImageMagick rather than in-Blender: `bpy.types.Image.scale()` on the
    freshly-imported *packed* glTF images silently does not carry through the glTF-separate export, so the
    written PNGs come out full-res. `mogrify -resize 'WxH>'` only shrinks (never upscales) and preserves
    aspect (longest edge <= cap). Required for the §9 VRAM budget (~145 MiB); without it a birch bark map
    alone is 4096^2 = ~85 MiB uncompressed."""
    mogrify = shutil.which("mogrify") or "/usr/bin/mogrify"
    for species_dir in OUT.iterdir():
        if not species_dir.is_dir():
            continue
        pngs = list(species_dir.glob("*.png"))
        mesh = [str(p) for p in pngs if "billboard" not in p.name.lower()]
        bb = [str(p) for p in pngs if "billboard" in p.name.lower()]
        if mesh:
            subprocess.run([mogrify, "-resize", f"{MESH_CAP}x{MESH_CAP}>"] + mesh, check=True)
        if bb:
            subprocess.run([mogrify, "-resize", f"{BILLBOARD_CAP}x{BILLBOARD_CAP}>"] + bb, check=True)


def world_bbox(objs):
    lo = Vector((1e30, 1e30, 1e30))
    hi = Vector((-1e30, -1e30, -1e30))
    for o in objs:
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c)
            lo = Vector((min(lo[i], w[i]) for i in range(3)))
            hi = Vector((max(hi[i], w[i]) for i in range(3)))
    return lo, hi


def export_tier(species, tree, tier, objs):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
    # Recentre base to origin (Blender is Z-up here; export_yup maps Z->glTF Y).
    lo, hi = world_bbox(objs)
    shift = Vector((-(lo.x + hi.x) / 2, -(lo.y + hi.y) / 2, -lo.z))
    for o in objs:
        o.location += shift

    safe = re.sub(r"[^A-Za-z0-9]+", "_", tree).strip("_").lower()
    out_dir = OUT / species
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"{species}_{safe}_{tier}.gltf"
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    # GLTF_SEPARATE, keep_originals=False -> Blender writes the DOWNSCALED datablocks as external files
    # into out_dir; same-named shared mesh maps overwrite to one file (shared), billboards are per-variant.
    bpy.ops.export_scene.gltf(filepath=str(out), export_format="GLTF_SEPARATE",
                              use_selection=True, export_apply=True, export_yup=True,
                              export_keep_originals=False)
    return out.name


def process_pack(species, folder, classify):
    gltf = next((LIB / folder).rglob("*.gltf"))
    clear_scene()
    bpy.ops.import_scene.gltf(filepath=str(gltf))
    # Group mesh objects by (tree, tier).
    groups = {}
    for o in bpy.context.scene.objects:
        if o.type != "MESH" or SKIP.search(o.name):
            continue
        c = classify(o.name)
        if c:
            groups.setdefault(c, []).append(o)
    made = []
    for (tree, tier), objs in sorted(groups.items()):
        made.append(export_tier(species, tree, tier, objs))
    trees = sorted({t for (t, _) in groups})
    print(f"[{species}] {len(trees)} trees, {len(made)} tier files exported")
    return made


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for species, (folder, classify) in PACKS.items():
        process_pack(species, folder, classify)
    downscale_written()
    # Symlink gameready/ into the engine's nature_local/.
    NATURE_LOCAL.mkdir(parents=True, exist_ok=True)
    link = NATURE_LOCAL / "gameready"
    if link.is_symlink() or link.exists():
        link.unlink()
    link.symlink_to(OUT)
    print(f"DONE -> {OUT} (symlinked into nature_local/gameready/)")


main()
