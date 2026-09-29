#!/usr/bin/env python3
"""Turn Poly Haven CC0 scans into game-ready meadow props (3D_E-0702).

Replaces the Kenney low-poly bushes, rocks, reeds and log in the meadow demo.

    blender-5.2 --background --python tools/asset_prep/prep_polyhaven_props.py

Each Poly Haven pack is a SET of variants (four shrubs, six nettles, eleven sorrel
sprigs...) laid out side by side, so every mesh in a pack is exported as its own
prop, <mesh name>.gltf. Each is optionally decimated to a triangle cap and
recentred so its base sits at the origin, then written into gameready/<group>/ (reachable from the engine as
assets/models/nature_local/gameready/<group>/ through the existing symlink).

Decimation is per model, not global. Rocks and logs are dense photogrammetry whose
detail lives in the normal map, so collapse decimation barely shows. The plants
are NOT decimated: their leaves are alpha-cut cards, and collapse decimation on a
card canopy destroys the cards (the same finding as split_water_plants.py). The
meadow budgets plants by scatter density instead.

Poly Haven models are authored in real-world metres, so no rescale is applied.
Sources were fetched at the 1k texture tier, which is also the engine budget.
"""
import pathlib

import bpy
from mathutils import Vector

LIB = pathlib.Path("/mnt/Games/3D Engine Assets/Models/Nature")
# gameready/ lives under Trees/ because that is what nature_local/gameready
# already symlinks to.
OUT = LIB / "Trees" / "gameready"

# name -> (source folder under LIB, output group, triangle cap or None)
PROPS = {
    "boulder_01":       ("Rocks/boulder_01_polyhaven_gltf",        "rocks",  4000),
    "rock_07":          ("Rocks/rock_07_polyhaven_gltf",           "rocks",  4000),
    "rock_09":          ("Rocks/rock_09_polyhaven_gltf",           "rocks",  4000),
    "rock_moss_set_01": ("Rocks/rock_moss_set_01_polyhaven_gltf",  "rocks",  6000),
    "stone_01":         ("Rocks/stone_01_polyhaven_gltf",          "rocks",  3000),
    "dead_tree_trunk":  ("Logs/dead_tree_trunk_polyhaven_gltf",    "logs",   8000),
    "tree_stump_01":    ("Logs/tree_stump_01_polyhaven_gltf",      "logs",   5000),
    "shrub_02":         ("Plants/shrub_02_polyhaven_gltf",         "plants", None),
    "shrub_03":         ("Plants/shrub_03_polyhaven_gltf",         "plants", None),
    "shrub_04":         ("Plants/shrub_04_polyhaven_gltf",         "plants", None),
    "fern_02":          ("Plants/fern_02_polyhaven_gltf",          "plants", None),
    "nettle_plant":     ("Plants/nettle_plant_polyhaven_gltf",     "plants", None),
    "weed_plant_02":    ("Plants/weed_plant_02_polyhaven_gltf",    "plants", None),
    "dandelion_01":     ("Plants/dandelion_01_polyhaven_gltf",     "plants", None),
    "shrub_sorrel_01":  ("Plants/shrub_sorrel_01_polyhaven_gltf",  "plants", None),
}


def world_bbox(objs):
    lo = Vector((1e30, 1e30, 1e30))
    hi = Vector((-1e30, -1e30, -1e30))
    for o in objs:
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c)
            lo = Vector((min(lo[i], w[i]) for i in range(3)))
            hi = Vector((max(hi[i], w[i]) for i in range(3)))
    return lo, hi


def process(name, folder, group, tri_cap):
    gltf = next((LIB / folder).rglob("*.gltf"))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(gltf))

    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        raise RuntimeError(f"{name}: no mesh objects imported from {gltf}")

    for o in meshes:
        export_one(o, group, tri_cap)


def export_one(obj, group, tri_cap):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")

    before = sum(len(p.vertices) - 2 for p in obj.data.polygons)
    if tri_cap is not None and before > tri_cap:
        mod = obj.modifiers.new(name="decimate", type="DECIMATE")
        mod.decimate_type = "COLLAPSE"
        mod.ratio = tri_cap / before
        bpy.ops.object.modifier_apply(modifier=mod.name)
    after = sum(len(p.vertices) - 2 for p in obj.data.polygons)

    # Base at the origin, centred in X/Y (Blender is Z-up; export_yup maps Z -> glTF Y).
    lo, hi = world_bbox([obj])
    obj.location += Vector((-(lo.x + hi.x) / 2, -(lo.y + hi.y) / 2, -lo.z))

    name = obj.name.lower().removesuffix("_lod0")
    out_dir = OUT / group
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"{name}.gltf"
    bpy.ops.export_scene.gltf(filepath=str(out), export_format="GLTF_SEPARATE",
                              use_selection=True, export_apply=True, export_yup=True,
                              export_keep_originals=False)
    lo2, hi2 = world_bbox([obj])
    print(f"[{name}] {before} -> {after} tris, extent "
          f"{hi2.x - lo2.x:.2f} x {hi2.y - lo2.y:.2f} x {hi2.z - lo2.z:.2f} m -> {group}/{out.name}")


def main():
    for name, (folder, group, cap) in PROPS.items():
        process(name, folder, group, cap)
    print(f"DONE -> {OUT}")


if __name__ == "__main__":
    main()
