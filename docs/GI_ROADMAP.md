# Global Illumination Roadmap

## Overview

This document outlines all global illumination (GI) techniques planned for the Vestige engine, their implementation priority, and how they compose together. The goal is to provide physically plausible indirect lighting for architectural walkthroughs.

## Technique Inventory

### 1. Light Probes (Cubemap) — IMPLEMENTED
**Status:** Basic version in engine (single probe per zone)
**Files:** `light_probe.h/cpp`, `light_probe_manager.h/cpp`
**How it works:** Captures local environment into cubemap, convolves irradiance + prefilter maps. Per-entity assignment based on AABB influence volume.
**Limitations:** Coarse spatial resolution, no smooth transitions between many probes, high memory per probe (~2.5 MB each).

### 2. SH Probe Grid — IMPLEMENTED (2026-03-29)
**Status:** IMPLEMENTED
**Files:** `engine/renderer/sh_probe_grid.h/cpp`; shader integration in `assets/shaders/scene.frag.glsl`; captured via `Renderer::captureSHGrid()`.
**How it works:** 3D grid of Spherical Harmonic probes (L2, 9 coefficients × 3 channels = 27 floats per probe). Stored in 7× RGBA16F 3D textures bound at units 17–23 with hardware trilinear interpolation for smooth spatial blending. ~81 KB for a 10×5×15 grid. Design notes in `docs/SH_PROBE_GRID_DESIGN.md`.
**Replaces:** Per-entity cubemap probe assignment for diffuse ambient.

### 3. Radiosity — IMPLEMENTED (2026-03-30)
**Status:** IMPLEMENTED
**Files:** `engine/renderer/radiosity_baker.h/cpp`.
**How it works:** Iterative gathering approach — each iteration re-captures the SH probe grid with the previous bounce's indirect lighting visible in the scene. After N iterations the grid contains N bounces of indirect light. Default config: 4 bounces, 0.02 energy-change convergence threshold, 0.3 m normal bias for anti-leak. Converges in 2–3 bounces (~12 s on RX 6600).
**Use case:** Bake at scene load. Tent interior gets correct doorway bounce light, altar fire illumination on nearby surfaces.
**Future enhancement:** hemicube-based per-patch form factor computation if fully static scenes ever need lightmap-quality output (see item 7).

### 4. Screen-Space Global Illumination (SSGI) — PRIORITY: HIGH (NEXT)
**Status:** Planned
**How it works:** Real-time post-process. Traces rays in screen space using depth buffer to find nearby lit surfaces, gathers their reflected color as indirect light.
**Advantages:** Fully dynamic, no baking, responds to moving lights and objects.
**Limitations:** Only sees on-screen content, can produce artifacts at screen edges.
**Complements:** SH probe grid (provides fallback for off-screen content).

### 5. Voxel Cone Tracing (VXGI) — PRIORITY: LOW
**Status:** Research phase (see CUTTING_EDGE_FEATURES_RESEARCH.md)
**How it works:** Voxelizes scene into 3D grid, traces cones through voxels to gather indirect light. Multi-bounce capable.
**Advantages:** Dynamic, handles specular + diffuse indirect, no baking.
**Limitations:** High VRAM usage, complex implementation, requires sparse voxel octree for large scenes.
**When:** Consider after Vulkan backend is available.

### 6. Light Propagation Volumes (LPV) — PRIORITY: LOW
**Status:** Not started
**How it works:** Inject direct light into 3D grid of SH cells, propagate iteratively to neighbors. Similar to SH probe grid but with real-time propagation.
**Advantages:** Dynamic bounce light, medium cost.
**Limitations:** Light leaking through thin walls, low spatial resolution.
**When:** Consider if SSGI proves insufficient for dynamic scenes.

### 7. Lightmaps (UV-based Radiosity) — PRIORITY: MEDIUM
**Status:** Not started
**How it works:** Bake radiosity results into per-surface UV-mapped textures. Each surface gets a unique lightmap with pre-computed indirect illumination.
**Advantages:** Highest visual quality for static scenes, zero runtime cost.
**Limitations:** Requires unique UV layouts (UV2), large texture memory, static only.
**When:** After radiosity core is working, as an alternative output format.

## Composition Strategy

Multiple systems can work together:

```
Static scenes:  Radiosity → SH Probe Grid (baked bounce light)
                           + Lightmaps (optional, higher detail)

Dynamic scenes: SH Probe Grid (baked base) + SSGI (dynamic overlay)

Future:         VXGI (replaces both SH grid + SSGI for fully dynamic GI)
```

## Implementation Order

1. ~~**SH Probe Grid** — Replace cubemap probes with SH grid for diffuse ambient~~ **SHIPPED 2026-03-29**
2. ~~**Radiosity** — Compute actual bounce light to fill the SH grid~~ **SHIPPED 2026-03-30**
3. **SSGI** — Add real-time indirect for dynamic content (**next**)
4. **Lightmaps** — UV-based radiosity output for maximum static quality
5. **VXGI** — Full dynamic GI (post-Vulkan)
6. **LPV** — Evaluate if needed alongside VXGI

## References

- Ramamoorthi & Hanrahan (2001) — SH for irradiance environment maps
- Sloan (2008) — Stupid Spherical Harmonics Tricks (GDC)
- Cohen & Wallace (1993) — Radiosity and Realistic Image Synthesis
- Ritschel et al. (2012) — The State of the Art in Interactive Global Illumination
- Kaplanyan (2009) — Light Propagation Volumes (CryEngine)
- Crassin et al. (2011) — Interactive Indirect Illumination Using Voxel Cone Tracing
