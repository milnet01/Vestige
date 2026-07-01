// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file occlusion_material_map.h
/// @brief Maps a physics surface tag to an audio occlusion preset (AX1 S1).
///
/// The two enums are deliberately independent (`surface_material.h:15-21`):
/// `SurfaceMaterial` describes what a surface is made of *when struck*;
/// `AudioOcclusionMaterialPreset` describes what a wall is made of *for sound
/// to pass through*. This is the one place they meet — the AX1 occlusion
/// driver resolves an occluding body's `SurfaceMaterial` and needs the audio
/// preset that feeds `computeObstructionGain` / `computeObstructionLowPass`.
///
/// Design of record: docs/phases/phase_10_audio_occlusion_design.md §3.
#pragma once

#include "physics/surface_material.h"
#include "audio/audio_occlusion.h"

namespace Vestige
{

/// @brief Maps the physics surface tag of an occluding body to the audio
///        occlusion preset used for transmission + low-pass.
///
/// Untagged geometry (`SurfaceMaterial::Default`) maps to `Concrete` — a
/// generic solid wall — so a level's plain, un-tagged walls still occlude
/// (closes the AX1 "sound passes through my new wall" gap without requiring the
/// level designer to tag every surface first). No `SurfaceMaterial` maps to
/// `Air`: a body that blocked a ray is by definition not air.
///
/// The non-1:1 rows (`Grass`/`Sand`/`Dirt`/`Default`) are perceptual mappings,
/// not fitted curves — no reference transmission-loss dataset exists for "sand
/// as an acoustic wall", so they are hand-picked.
/// TODO: revisit via Formula Workbench once measured transmission data exists.
inline AudioOcclusionMaterialPreset occlusionPresetForSurface(SurfaceMaterial m)
{
    switch (m)
    {
    case SurfaceMaterial::Stone: return AudioOcclusionMaterialPreset::Stone;
    case SurfaceMaterial::Wood:  return AudioOcclusionMaterialPreset::Wood;
    case SurfaceMaterial::Metal: return AudioOcclusionMaterialPreset::Metal;
    case SurfaceMaterial::Glass: return AudioOcclusionMaterialPreset::Glass;
    case SurfaceMaterial::Water: return AudioOcclusionMaterialPreset::Water;
    case SurfaceMaterial::Cloth: return AudioOcclusionMaterialPreset::Cloth;
    case SurfaceMaterial::Grass: return AudioOcclusionMaterialPreset::Cloth;    // thin, porous → soft
    case SurfaceMaterial::Sand:  return AudioOcclusionMaterialPreset::Concrete; // dense, absorptive
    case SurfaceMaterial::Dirt:  return AudioOcclusionMaterialPreset::Concrete;
    case SurfaceMaterial::Default: return AudioOcclusionMaterialPreset::Concrete;
    }
    // Unreachable today (all 10 enumerators cased → -Wswitch-clean); guards a
    // future append-only SurfaceMaterial member, mapping it to the generic wall.
    return AudioOcclusionMaterialPreset::Concrete;
}

} // namespace Vestige
