#!/usr/bin/env python3
"""Decimate the Sketchfab pond-plant packs into game-budget glTF props (slice T6 part 2).

Each downloaded pack (tools/asset_prep/fetch_sketchfab_water_plants.py) is one
lotus cluster split across 5 mesh objects sharing one material, at ~467-486 k
triangles total. That is a photoscan-class budget the v2 tree design already
rejected for a real-time LOD0, so nothing here reaches the engine un-decimated.
This tool runs under Blender headless (invisible):

    blender-5.2 --background --python tools/asset_prep/split_water_plants.py

Per pack it joins the cluster into one object, decimates it to ~TARGET_TRIS, and
exports one glTF-separate prop, recentred so the plant's base sits at the origin
(the meadow seats these on the pond's water plane), into
Models/Nature/Trees/gameready/water/ — the SAME gameready/ tree already symlinks
into assets/models/nature_local/, so the engine resolves `gameready/water/<f>`
through the existing hook with no new symlink.

Two things Blender is doing for us that are not incidental:

  * **specGloss -> metallic-roughness.** Both packs list
    KHR_materials_pbrSpecularGlossiness in `extensionsRequired`. That extension is
    deprecated and the engine's loader does not implement it, so the raw packs
    would not render correctly. Blender's importer converts to a Principled BSDF
    and the exporter writes standard metallic-roughness.
  * **UV cull.** The source carries TEXCOORD_0..3 (four UV sets) where one is used;
    the join+export keeps only the active set, dropping three float2 streams per
    vertex.

Decimation is safe here in a way it would NOT be for a leaf-card tree: these are
dense, uniformly-tessellated AI-generated (Tripo) surfaces whose detail lives
almost entirely in the baked albedo/normal maps, not in the silhouette. Collapse
decimation on a leaf-card canopy destroys the cards; on a smooth lotus pad it is
nearly invisible. TARGET_TRIS is deliberately a knob — re-run and eyeball the
pond if it reads too coarse.

Texture budget matches the tree pipeline (the engine uploads uncompressed, so
pixel resolution sets VRAM): maps are downscaled to <=1024 on the longest edge.
"""
import pathlib
import shutil
import subprocess

import bpy
from mathutils import Vector

LIB = pathlib.Path("/mnt/Games/3D Engine Assets/Models/Nature/Plants")
# gameready/ lives under Trees/ because that is what nature_local/gameready
# already symlinks to; water/ is a sibling species dir inside it.
OUT = pathlib.Path("/mnt/Games/3D Engine Assets/Models/Nature/Trees/gameready/water")

MESH_CAP = 1024      # longest edge for the shared maps
TARGET_TRIS = 6000   # per exported cluster prop

PACKS = {
    "lotus_pads": "lotus_pads_ga_sketchfab_gltf",
    "lotus_flowers": "lotus_flowers_ga_sketchfab_gltf",
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


def process_pack(name, folder):
    gltf = next((LIB / folder).rglob("*.gltf"))
    clear_scene()
    bpy.ops.import_scene.gltf(filepath=str(gltf))

    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        raise RuntimeError(f"{name}: no mesh objects imported from {gltf}")

    # Join the cluster into one object so it instantiates as a single prop.
    bpy.ops.object.select_all(action="DESELECT")
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
    bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active

    before = len(obj.data.polygons)
    ratio = min(1.0, TARGET_TRIS / max(1, before))
    if ratio < 1.0:
        mod = obj.modifiers.new(name="decimate", type="DECIMATE")
        mod.decimate_type = "COLLAPSE"
        mod.ratio = ratio
        bpy.ops.object.modifier_apply(modifier=mod.name)
    after = len(obj.data.polygons)

    # Recentre: X/Y to the cluster's centre, Z so the base sits at the origin
    # (Blender is Z-up here; export_yup maps Z -> glTF Y).
    lo, hi = world_bbox([obj])
    obj.location += Vector((-(lo.x + hi.x) / 2, -(lo.y + hi.y) / 2, -lo.z))

    OUT.mkdir(parents=True, exist_ok=True)
    out = OUT / f"{name}.gltf"
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.ops.export_scene.gltf(filepath=str(out), export_format="GLTF_SEPARATE",
                              use_selection=True, export_apply=True, export_yup=True,
                              export_keep_originals=False)
    lo2, hi2 = world_bbox([obj])
    print(f"[{name}] {before} -> {after} tris (ratio {ratio:.4f}), "
          f"extent {hi2.x - lo2.x:.2f} x {hi2.y - lo2.y:.2f} x {hi2.z - lo2.z:.2f} m -> {out.name}")
    return out.name


def downscale_written():
    """Shrink the exported maps to MESH_CAP on the longest edge (never upscale).

    Same post-export ImageMagick pass split_tree_packs.py uses, and for the same
    reason: scaling the packed image datablocks in-Blender does not carry through
    a GLTF_SEPARATE export, so the written files come out full-res.
    """
    mogrify = shutil.which("mogrify") or "/usr/bin/mogrify"
    imgs = [str(p) for p in OUT.iterdir()
            if p.suffix.lower() in (".png", ".jpg", ".jpeg")]
    if imgs:
        subprocess.run([mogrify, "-resize", f"{MESH_CAP}x{MESH_CAP}>"] + imgs, check=True)
        print(f"downscaled {len(imgs)} texture(s) to <={MESH_CAP}px")


def main():
    for name, folder in PACKS.items():
        process_pack(name, folder)
    downscale_written()
    print(f"DONE -> {OUT} (reachable as gameready/water/ via the existing nature_local symlink)")


if __name__ == "__main__":
    main()
