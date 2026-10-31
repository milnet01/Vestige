#!/usr/bin/env python3
# Copyright (c) 2026 Anthony Schemel
# SPDX-License-Identifier: MIT
"""Build the meadow pond's water lilies from nothing but this script (3D_E-0804).

The bought lotus clusters these replace were AI-generated (Tripo) lumps: pads
heaped 0.43 m high, blooms that read as scattered shards. Real lily pads lie flat
on the water, each one round with a notch, so this models them directly. Nothing
is downloaded, so the output carries no third-party licence.

Runs under Blender headless (invisible):

    blender-5.2 --background --python tools/asset_prep/make_water_lilies.py
    blender-5.2 --background --python tools/asset_prep/make_water_lilies.py -- --preview DIR

Each run writes, from fixed seeds, so the same script gives the same models:

  * Models/Nature/Plants/water_lily/ in the asset library: each glTF, the shared
    atlas and a .blend per model, for hand editing.
  * assets/models/nature/gameready/water/ in this repository: the glTFs and atlas
    the engine loads.

--preview also renders each model on a blue plane with Cycles on the CPU, so a
look at the shape needs neither the GPU nor the engine.

Every model shares one atlas (albedo and normal, four tiles: three leaf colours
and one for petals and stamens) and one material, so the pond costs one set of
textures however many variants it scatters. Models sit base-at-origin in metres,
glTF Y up, for the meadow to seat on its water plane.
"""
import argparse
import math
import pathlib
import random
import shutil
import sys

import bpy
import numpy as np

LIB = pathlib.Path("/mnt/Games/3D Engine Assets/Models/Nature/Plants/water_lily")
REPO_OUT = (pathlib.Path(__file__).resolve().parents[2]
            / "assets/models/nature/gameready/water")

ALBEDO_FILE = "water_lily_albedo.png"
NORMAL_FILE = "water_lily_normal.png"
TILE_PX = 1024         # atlas is 2 x 2 tiles; the engine uploads uncompressed
NOISE_SCALE = TILE_PX / 512.0    # noise sizes below are in pixels of a 512 px tile
TILE_INSET = 0.03      # keeps mipmaps from bleeding between tiles
LEAF_TILES = [(0, 0), (1, 0), (0, 1)]
LEAF_TILE_WEIGHTS = [5, 4, 2]
FLOWER_TILE = (1, 1)
PETAL_RECT = (0.0, 0.72)    # share of the flower tile's width holding the petal
STAMEN_RECT = (0.76, 1.0)   # ...and the stamens

# Budget: no cluster above the roughly 18 k triangles of the lotus props it replaces.
PAD_SEGMENTS = 56      # around the rim; the notch takes its share out of this
PAD_RINGS = 7
PAD_THICKNESS = 0.003  # the visible edge of the leaf, metres
PAD_NOTCH_DEG = 22.0
PAD_RADIUS = (0.08, 0.17)
PAD_GAP = 0.006        # pads touch but never overlap: same height would z-fight
PAD_LIFT_MAX = 0.012   # rim curl, metres

# name: (seed, pad count, cluster radius, flowers, closed buds)
MODELS = {
    "lily_pads_a": (11, 16, 0.70, 0, 0),
    "lily_pads_b": (23, 12, 0.60, 0, 1),
    "lily_pads_c": (37, 19, 0.80, 0, 1),
    "lily_flowers_a": (41, 6, 0.42, 1, 1),
    "lily_flowers_b": (59, 8, 0.50, 2, 1),
}

# Petal rings from the outside in: count, elevation at base and tip (degrees), length.
PETAL_RINGS = [
    (10, 4.0, 24.0, 0.088),
    (10, 16.0, 42.0, 0.084),
    (9, 32.0, 58.0, 0.076),
    (8, 48.0, 72.0, 0.066),
    (6, 62.0, 84.0, 0.052),
]
# A bud is the same blade folded shut: elevations past 90 lean back in over the top.
BUD_RINGS = [
    (4, 66.0, 104.0, 0.060),
    (4, 72.0, 110.0, 0.056),
    (3, 78.0, 116.0, 0.050),
]
SEPAL_COUNT = 4
STAMEN_COUNT = 36
PETAL_SEGMENTS, PETAL_ACROSS = 10, 4
HEART_RINGS, HEART_SEGMENTS = 3, 16


# ---- Textures ------------------------------------------------------------------

def smoothstep(lo, hi, x):
    t = np.clip((x - lo) / (hi - lo), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def smooth_noise(rng, size, sigma):
    """White noise blurred by a Gaussian in frequency space, scaled to 0..1."""
    sigma *= NOISE_SCALE
    white = rng.standard_normal((size, size))
    fx = np.fft.fftfreq(size)[:, None]
    fy = np.fft.rfftfreq(size)[None, :]
    falloff = np.exp(-2.0 * (math.pi * sigma) ** 2 * (fx * fx + fy * fy))
    out = np.fft.irfft2(np.fft.rfft2(white) * falloff, s=(size, size))
    return (out - out.min()) / (out.max() - out.min())


def mix(a, b, t):
    return a + (b - a) * t[..., None]


def vein_set(theta, r, count, width, wobble):
    """Radial lines: 1 on a vein, falling away with distance across the leaf."""
    frac = (theta + wobble) * count / (2.0 * math.pi)
    across = np.abs(frac - np.round(frac)) * 2.0 * math.pi * r / count
    return np.exp(-((across / width) ** 2))


def leaf_tile(rng, size, dark, light, vein, rim=None, blotch=None):
    """One round leaf filling the tile. Returns albedo (sRGB 0..1) and a height map."""
    axis = (np.arange(size) + 0.5) / size * 2.0 - 1.0
    x, y = np.meshgrid(axis, axis)
    r = np.hypot(x, y)
    theta = np.arctan2(y, x)
    wobble = (smooth_noise(rng, size, 40) - 0.5) * 0.10

    # Veins run out from the stalk at the centre and fork towards the rim.
    v1 = vein_set(theta, r, 22, 0.009, wobble) * smoothstep(0.03, 0.08, r)
    v2 = vein_set(theta + 0.07, r, 44, 0.005, wobble) * smoothstep(0.40, 0.55, r)
    v3 = vein_set(theta + 0.03, r, 88, 0.003, wobble) * smoothstep(0.68, 0.80, r)
    v4 = vein_set(theta + 0.015, r, 176, 0.0018, wobble) * smoothstep(0.84, 0.90, r)
    veins = np.clip(v1 + 0.7 * v2 + 0.4 * v3 + 0.25 * v4, 0.0, 1.0)
    # The fine net between the veins, and the pores of the waxy surface.
    net = smoothstep(0.52, 0.60, smooth_noise(rng, size, 1.6))
    pores = smooth_noise(rng, size, 0.5)

    mottle = 0.6 * smooth_noise(rng, size, 30) + 0.4 * smooth_noise(rng, size, 6)
    albedo = mix(np.array(dark), np.array(light), smoothstep(0.25, 0.75, mottle))
    albedo = mix(albedo, np.array(vein), veins * 0.32 + net * 0.06)
    albedo = mix(albedo, np.array(vein), 1.0 - smoothstep(0.015, 0.035, r))  # stalk joint
    if rim is not None:
        edge = smooth_noise(rng, size, 10) * 0.12
        albedo = mix(albedo, np.array(rim), smoothstep(0.86, 1.0, r + edge))
    if blotch is not None:
        patches = smooth_noise(rng, size, 14)
        albedo = mix(albedo, np.array(blotch), smoothstep(0.66, 0.74, patches) * 0.4)

    height = (-veins + 0.15 * net + 0.08 * pores + 0.35 * smooth_noise(rng, size, 2)
              + 0.8 * smooth_noise(rng, size, 14))
    return albedo, height


def flower_tile(rng, size):
    """Petal on the left (base at the bottom, tip at the top), stamens on the right."""
    u = (np.arange(size) + 0.5) / size
    x, y = np.meshgrid(u, u)
    petal_u = np.clip(x / PETAL_RECT[1], 0.0, 1.0)

    base = np.array((0.98, 0.90, 0.90))
    middle = np.array((0.94, 0.62, 0.74))
    tip = np.array((0.85, 0.36, 0.58))
    albedo = mix(base, middle, smoothstep(0.05, 0.60, y))
    albedo = mix(albedo, tip, smoothstep(0.60, 1.0, y))

    ribs = 0.5 + 0.5 * np.cos(petal_u * 2.0 * math.pi * 7.0)
    streak = smooth_noise(rng, size, 3) * 0.08
    albedo = albedo * (1.0 - (0.07 * ribs + streak))[..., None]
    height = 0.45 * ribs + 0.25 * smooth_noise(rng, size, 5)

    stamens = x >= (PETAL_RECT[1] + STAMEN_RECT[0]) / 2.0
    yellow = mix(np.array((0.93, 0.62, 0.10)), np.array((1.0, 0.86, 0.25)),
                 smooth_noise(rng, size, 3))
    albedo[stamens] = yellow[stamens]
    height[stamens] = (0.5 * smooth_noise(rng, size, 2))[stamens]
    return albedo, height


def height_to_normal(height, strength):
    """Tangent-space normals, Y up. The arrays run bottom row first, as Blender's do."""
    dy, dx = np.gradient(height)
    n = np.dstack((-dx * strength, -dy * strength, np.ones_like(height)))
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    return n * 0.5 + 0.5


def save_image(pixels, path, colorspace):
    """Write an H x W x 3 array (bottom row first) to PNG; returns the loaded image."""
    h, w, _ = pixels.shape
    rgba = np.dstack((pixels, np.ones((h, w))))
    img = bpy.data.images.new(path.stem, w, h, alpha=False, float_buffer=False)
    img.colorspace_settings.name = colorspace
    img.pixels.foreach_set(rgba.astype(np.float32).ravel())
    img.filepath_raw = str(path)
    img.file_format = "PNG"
    img.save()
    bpy.data.images.remove(img)
    loaded = bpy.data.images.load(str(path))
    loaded.colorspace_settings.name = colorspace
    return loaded


def write_atlas():
    rng = np.random.default_rng(804)
    tiles = {
        (0, 0): leaf_tile(rng, TILE_PX, (0.13, 0.28, 0.09), (0.25, 0.42, 0.14),
                          (0.45, 0.60, 0.28)),
        (1, 0): leaf_tile(rng, TILE_PX, (0.17, 0.36, 0.13), (0.27, 0.47, 0.17),
                          (0.45, 0.60, 0.30), rim=(0.42, 0.20, 0.15)),
        (0, 1): leaf_tile(rng, TILE_PX, (0.20, 0.33, 0.10), (0.36, 0.47, 0.15),
                          (0.56, 0.64, 0.30), rim=(0.56, 0.47, 0.15),
                          blotch=(0.40, 0.29, 0.12)),
        FLOWER_TILE: flower_tile(rng, TILE_PX),
    }
    size = TILE_PX * 2
    albedo = np.zeros((size, size, 3))
    normal = np.zeros((size, size, 3))
    for (tx, ty), (tile_albedo, tile_height) in tiles.items():
        row, col = ty * TILE_PX, tx * TILE_PX
        albedo[row:row + TILE_PX, col:col + TILE_PX] = np.clip(tile_albedo, 0.0, 1.0)
        normal[row:row + TILE_PX, col:col + TILE_PX] = height_to_normal(tile_height, 1.5 * NOISE_SCALE)
    return (save_image(albedo, LIB / ALBEDO_FILE, "sRGB"),
            save_image(normal, LIB / NORMAL_FILE, "Non-Color"))


# ---- Geometry ------------------------------------------------------------------

def tile_uv(tile, s, t):
    """(s, t) in 0..1 across a tile, to atlas UV."""
    span = 1.0 - 2.0 * TILE_INSET
    return ((tile[0] + TILE_INSET + s * span) / 2.0,
            (tile[1] + TILE_INSET + t * span) / 2.0)


class MeshBuilder:
    def __init__(self):
        self.verts = []
        self.faces = []
        self.uvs = []      # one per vertex: a vertex is never shared across a UV seam

    def vert(self, co, uv):
        self.verts.append(co)
        self.uvs.append(uv)
        return len(self.verts) - 1

    def grid(self, rows):
        """Join rows of vertex indices into quads; rows run along the radius."""
        for a, b in zip(rows, rows[1:]):
            for j in range(len(a) - 1):
                self.faces.append((a[j], b[j], b[j + 1], a[j + 1]))

    def build(self, name, material):
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata(self.verts, [], self.faces)
        layer = mesh.uv_layers.new(name="UVMap")
        for poly in mesh.polygons:
            for loop_index in poly.loop_indices:
                layer.data[loop_index].uv = self.uvs[mesh.loops[loop_index].vertex_index]
        mesh.shade_smooth()
        mesh.materials.append(material)
        obj = bpy.data.objects.new(name, mesh)
        bpy.context.scene.collection.objects.link(obj)
        return obj


def add_pad(mb, rng, cx, cy, radius):
    """A flat round leaf with one notch, its rim curling up a little."""
    tile = rng.choices(LEAF_TILES, LEAF_TILE_WEIGHTS)[0]
    spin = rng.uniform(0.0, 2.0 * math.pi)
    notch = math.radians(PAD_NOTCH_DEG) * rng.uniform(0.8, 1.2)
    sweep = 2.0 * math.pi - notch
    lift = rng.uniform(0.35, 1.0) * PAD_LIFT_MAX * radius / PAD_RADIUS[1]
    phase1, phase2 = rng.uniform(0.0, 6.28), rng.uniform(0.0, 6.28)

    def point(frac, a):
        """Position on the leaf at this share of the radius and angle round the sweep."""
        # Slightly out of round; the lobes round off where they meet the notch.
        to_notch = min(a, sweep - a)
        rim = (1.0 + 0.025 * math.sin(2.0 * a + phase1) + 0.015 * math.sin(5.0 * a + phase2)
               - 0.07 * math.exp(-((to_notch / 0.13) ** 2)))
        rho = radius * frac * rim
        angle = spin + notch / 2.0 + a
        # Low folds along the veins, a ruffle at the rim, lobes lifting at the notch.
        z = (PAD_THICKNESS
             + lift * frac * frac * (0.75 + 0.25 * math.sin(3.0 * a + phase2))
             + 0.0012 * frac * (0.5 + 0.5 * math.cos(14.0 * a))
             + 0.0028 * frac ** 4 * (0.5 + 0.5 * math.sin(9.0 * a + phase1))
             + 0.0040 * frac * math.exp(-((to_notch / 0.35) ** 2)))
        s = 0.5 + 0.5 * frac * rim * math.cos(a) / 1.05
        t = 0.5 + 0.5 * frac * rim * math.sin(a) / 1.05
        return (cx + rho * math.cos(angle), cy + rho * math.sin(angle), z), tile_uv(tile, s, t)

    def lowered(co):
        return (co[0], co[1], co[2] - PAD_THICKNESS)

    def edge_uv(path, i):
        """UV a step sideways of the path: with no UV width the edge's tangent, and so
        its normal map, is undefined and it shades as black teeth."""
        before, after = path[max(i - 1, 0)][1], path[min(i + 1, len(path) - 1)][1]
        du, dv = after[0] - before[0], after[1] - before[1]
        length = math.hypot(du, dv)
        uv = path[i][1]
        return (uv[0] - dv / length * 0.003, uv[1] + du / length * 0.003)

    centre = mb.vert(*point(0.0, 0.0))
    rows = []
    for ring in range(1, PAD_RINGS + 1):
        frac = ring / PAD_RINGS
        rows.append([mb.vert(*point(frac, sweep * j / PAD_SEGMENTS))
                     for j in range(PAD_SEGMENTS + 1)])
    for j in range(PAD_SEGMENTS):
        mb.faces.append((centre, rows[0][j], rows[0][j + 1]))
    mb.grid(rows)

    # The leaf's edge: the rim and both sides of the notch. Its vertices are copies,
    # so the edge shades as a crisp fold rather than rounding the top surface off.
    edge_paths = [
        [point(1.0, sweep * j / PAD_SEGMENTS) for j in range(PAD_SEGMENTS + 1)],
        [point(ring / PAD_RINGS, 0.0) for ring in range(PAD_RINGS + 1)],
        [point(ring / PAD_RINGS, sweep) for ring in range(PAD_RINGS + 1)],
    ]
    for path in edge_paths:
        top = [mb.vert(co, uv) for co, uv in path]
        bottom = [mb.vert(lowered(co), edge_uv(path, i)) for i, (co, _) in enumerate(path)]
        mb.grid([top, bottom])


def add_blade(mb, cx, cy, z0, angle, start_radius, length, elevation, half_width,
              rect, segments, across, tile=FLOWER_TILE, ruffle=0.0):
    """A pointed blade rising out from a centre: one petal, sepal or stamen."""
    span = rect[1] - rect[0]
    radial = (math.cos(angle), math.sin(angle))
    tangent = (-radial[1], radial[0])
    rho, z = start_radius, z0
    rows = []
    for i in range(segments + 1):
        t = i / segments
        # Narrow at the base, widest past the middle, closing to a point.
        width = half_width * max(math.sin(math.pi * t ** 0.75) ** 0.8, 0.04)
        row = []
        for k in range(across + 1):
            side = 2.0 * k / across - 1.0
            # A shallow trough along the middle; the edges waver a little.
            sag = -0.22 * width * (1.0 - side * side)
            wave = ruffle * abs(side) * math.sin(t * 9.0 + side * 2.0 + angle * 3.0)
            px = cx + radial[0] * rho + tangent[0] * width * side
            py = cy + radial[1] * rho + tangent[1] * width * side
            uv = tile_uv(tile, rect[0] + span * (0.5 + 0.5 * side), t)
            row.append(mb.vert((px, py, z + sag + wave), uv))
        rows.append(row)
        step = length / segments
        a = math.radians(elevation[0] + (elevation[1] - elevation[0]) * t)
        rho += step * math.cos(a)
        z += step * math.sin(a)
    mb.grid(rows)


def add_petal_rings(mb, rng, cx, cy, rings, base_radius):
    for ring_index, (count, e_base, e_tip, length) in enumerate(rings):
        offset = rng.uniform(0.0, 2.0 * math.pi)
        for k in range(count):
            angle = offset + 2.0 * math.pi * (k + rng.uniform(-0.12, 0.12)) / count
            scale = rng.uniform(0.92, 1.08)
            add_blade(mb, cx, cy, 0.012 + 0.002 * ring_index, angle,
                      max(base_radius - 0.003 * ring_index, 0.003), length * scale,
                      (e_base + rng.uniform(-3.0, 3.0), e_tip + rng.uniform(-4.0, 4.0)),
                      0.255 * length * scale, PETAL_RECT, PETAL_SEGMENTS, PETAL_ACROSS,
                      ruffle=0.0012)


def add_sepals(mb, rng, cx, cy, length, elevation):
    """The green outer leaves that hold a bud or back a flower."""
    offset = rng.uniform(0.0, 2.0 * math.pi)
    for k in range(SEPAL_COUNT):
        add_blade(mb, cx, cy, 0.008, offset + 2.0 * math.pi * k / SEPAL_COUNT, 0.012, length,
                  elevation, 0.24 * length, (0.42, 0.58), PETAL_SEGMENTS, PETAL_ACROSS,
                  tile=LEAF_TILES[0])


def add_flower(mb, rng, cx, cy):
    add_sepals(mb, rng, cx, cy, 0.08, (2.0, 14.0))
    add_petal_rings(mb, rng, cx, cy, PETAL_RINGS, 0.020)
    for k in range(STAMEN_COUNT):
        angle = 2.0 * math.pi * k / STAMEN_COUNT + rng.uniform(-0.06, 0.06)
        inner = k % 2 == 0
        add_blade(mb, cx, cy, 0.016, angle, 0.004 if inner else 0.008,
                  0.018 if inner else 0.026,
                  (74.0, 88.0) if inner else (58.0, 76.0), 0.0032, STAMEN_RECT, 3, 2)
    # The flower's golden heart: a low dome closing the hole the rings leave.
    heart_uv = tile_uv(FLOWER_TILE, sum(STAMEN_RECT) / 2.0, 0.5)
    centre = mb.vert((cx, cy, 0.024), heart_uv)
    rows = []
    for ring in range(1, HEART_RINGS + 1):
        frac = ring / HEART_RINGS
        row = []
        for k in range(HEART_SEGMENTS + 1):
            a = 2.0 * math.pi * k / HEART_SEGMENTS
            row.append(mb.vert((cx + 0.012 * frac * math.cos(a), cy + 0.012 * frac * math.sin(a),
                                0.024 - 0.008 * frac * frac),
                               tile_uv(FLOWER_TILE, sum(STAMEN_RECT) / 2.0 + 0.1 * frac * math.cos(a),
                                       0.5 + 0.4 * frac * math.sin(a))))
        rows.append(row)
    for k in range(HEART_SEGMENTS):
        mb.faces.append((centre, rows[0][k], rows[0][k + 1]))
    mb.grid(rows)


def add_bud(mb, rng, cx, cy):
    add_sepals(mb, rng, cx, cy, 0.066, (64.0, 102.0))
    add_petal_rings(mb, rng, cx, cy, BUD_RINGS, 0.014)


def scatter_pads(rng, count, cluster_radius, keep_clear):
    """Non-overlapping pads, largest first, pressed in towards the middle.

    keep_clear lists (x, y, r) spots the pads must leave open, for the flowers.
    """
    placed = []
    radii = sorted((rng.uniform(*PAD_RADIUS) for _ in range(count)), reverse=True)
    for radius in radii:
        for _ in range(400):
            a = rng.uniform(0.0, 2.0 * math.pi)
            d = cluster_radius * math.sqrt(rng.random())
            x, y = d * math.cos(a), d * math.sin(a)
            if all(math.hypot(x - ox, y - oy) >= radius + r + PAD_GAP
                   for ox, oy, r in placed + keep_clear):
                placed.append((x, y, radius))
                break
    return placed


def make_material(albedo, normal):
    mat = bpy.data.materials.new("water_lily")
    mat.use_backface_culling = False    # exported as doubleSided: petals show both faces
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes["Principled BSDF"]
    bsdf.inputs["Metallic"].default_value = 0.0
    bsdf.inputs["Roughness"].default_value = 0.42    # a waxy leaf
    tex = nodes.new("ShaderNodeTexImage")
    tex.image = albedo
    links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    ntex = nodes.new("ShaderNodeTexImage")
    ntex.image = normal
    nmap = nodes.new("ShaderNodeNormalMap")
    links.new(ntex.outputs["Color"], nmap.inputs["Color"])
    links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])
    return mat


def clear_objects():
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj)
    for mesh in list(bpy.data.meshes):
        bpy.data.meshes.remove(mesh)


def make_model(name, spec, material):
    seed, pad_count, cluster_radius, flowers, buds = spec
    rng = random.Random(seed)
    clear_objects()
    mb = MeshBuilder()

    # Open water kept for each flower and bud: flowers rise from a gap between pads.
    blooms = []
    for clearance in [0.10] * flowers + [0.03] * buds:
        for _ in range(100):
            a = rng.uniform(0.0, 2.0 * math.pi)
            d = cluster_radius * 0.55 * rng.random()
            x, y = d * math.cos(a), d * math.sin(a)
            if all(math.hypot(x - bx, y - by) > clearance + r + 0.04 for bx, by, r in blooms):
                blooms.append((x, y, clearance))
                break

    pads = scatter_pads(rng, pad_count, cluster_radius, blooms)
    for x, y, radius in pads:
        add_pad(mb, rng, x, y, radius)
    for x, y, clearance in blooms:
        (add_flower if clearance > 0.05 else add_bud)(mb, rng, x, y)

    obj = mb.build(name, material)
    bpy.context.view_layer.update()
    tris = sum(len(f) - 2 for f in mb.faces)
    print(f"[{name}] {len(pads)}/{pad_count} pads, {flowers} flowers, {buds} buds ({len(blooms)} placed), "
          f"{tris} tris, {obj.dimensions.x:.2f} x {obj.dimensions.y:.2f} x "
          f"{obj.dimensions.z:.3f} m")
    if len(pads) < pad_count:
        print(f"[{name}] WARNING: only {len(pads)} of {pad_count} pads fitted")
    return obj


def export_model(name, obj):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.gltf(filepath=str(LIB / f"{name}.gltf"),
                              export_format="GLTF_SEPARATE",
                              use_selection=True, export_apply=True, export_yup=True,
                              export_tangents=True, export_keep_originals=True,
                              export_extras=False, export_cameras=False,
                              export_lights=False)
    bpy.ops.wm.save_as_mainfile(filepath=str(LIB / f"{name}.blend"), compress=True)


def render_preview(name, obj, out_dir):
    """Cycles on the CPU: a look at the shape that leaves the GPU alone."""
    scene = bpy.context.scene
    extras = []

    water_mat = bpy.data.materials.new("preview_water")
    water_mat.use_nodes = True
    water_mat.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (
        0.03, 0.09, 0.13, 1.0)
    water_mat.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.25
    bpy.ops.mesh.primitive_plane_add(size=20.0, location=(0.0, 0.0, -0.002))
    water = bpy.context.active_object
    water.data.materials.append(water_mat)
    extras.append(water)

    sun_data = bpy.data.lights.new("preview_sun", "SUN")
    sun_data.energy = 3.5
    sun_data.angle = math.radians(4.0)
    sun = bpy.data.objects.new("preview_sun", sun_data)
    sun.rotation_euler = (math.radians(38.0), 0.0, math.radians(40.0))
    scene.collection.objects.link(sun)
    extras.append(sun)

    world = bpy.data.worlds.new("preview_world")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.68, 0.85, 1.0)
    world.node_tree.nodes["Background"].inputs[1].default_value = 0.7
    scene.world = world

    cam_data = bpy.data.cameras.new("preview_cam")
    cam_data.lens = 45.0
    cam = bpy.data.objects.new("preview_cam", cam_data)
    scene.collection.objects.link(cam)
    extras.append(cam)
    scene.camera = cam

    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 24
    scene.render.threads_mode = "FIXED"
    scene.render.threads = 4     # the machine is shared
    scene.render.resolution_x, scene.render.resolution_y = 800, 500
    scene.render.image_settings.file_format = "PNG"

    span = max(obj.dimensions.x, obj.dimensions.y)
    views = {"low": (0.0, -1.55, 0.42), "top": (0.0, -0.45, 1.65)}
    for view, (x, y, z) in views.items():
        cam.location = (x * span, y * span, z * span)
        direction = -cam.location.copy().normalized()
        cam.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = str(pathlib.Path(out_dir) / f"{name}_{view}.png")
        bpy.ops.render.render(write_still=True)

    for extra in extras:
        bpy.data.objects.remove(extra)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(description="Build the meadow pond's water lilies.")
    parser.add_argument("--preview", metavar="DIR", help="also render each model into DIR")
    args = parser.parse_args(argv)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    LIB.mkdir(parents=True, exist_ok=True)
    REPO_OUT.mkdir(parents=True, exist_ok=True)
    albedo, normal = write_atlas()
    material = make_material(albedo, normal)

    for name, spec in MODELS.items():
        obj = make_model(name, spec, material)
        export_model(name, obj)
        if args.preview:
            pathlib.Path(args.preview).mkdir(parents=True, exist_ok=True)
            render_preview(name, obj, args.preview)
        for suffix in (".gltf", ".bin"):
            shutil.copy2(LIB / f"{name}{suffix}", REPO_OUT)
    for image in (ALBEDO_FILE, NORMAL_FILE):
        shutil.copy2(LIB / image, REPO_OUT)
    print(f"DONE -> {LIB} and {REPO_OUT}")


if __name__ == "__main__":
    main()
