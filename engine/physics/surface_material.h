// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file surface_material.h
/// @brief Physics surface-material tag + Jolt body user-data packing (AX4 S2).
///
/// A dense, stable enum naming what a floor / object is *made of when struck*.
/// The procedural-audio bundle (AX4) reads it off a collision to pick a
/// footstep / impact sound bank. It is stored in the Jolt body's user-data
/// word (`uint64`, otherwise unused) alongside the owning entity's id, so the
/// collision-event bus (S3) and the scripting bridge (S8) can recover both
/// from a contact callback without a side map (which would not be safe to
/// mutate from Jolt's job threads).
///
/// Distinct from `AudioOcclusionMaterialPreset` (`audio/audio_occlusion.h`):
/// occlusion describes what a *wall is made of for sound to pass through*;
/// `SurfaceMaterial` describes what a *surface is made of when struck*. The
/// two overlap (Stone / Wood / Metal / Glass / Water / Cloth) but stay
/// independent — physics must not depend on the audio layer — so each adds
/// members the other does not need (`SurfaceMaterial` adds Sand / Grass /
/// Dirt; occlusion has Concrete / Air).
#pragma once

#include <cstdint>

namespace Vestige
{

/// @brief What a floor / object is made of when struck.
///        Append-only; persisted as the underlying integer in scene files,
///        so members must never be reordered or removed.
enum class SurfaceMaterial : std::uint8_t
{
    Default = 0,   ///< Unknown / untagged → generic dull-thud bank.
    Stone,
    Wood,
    Metal,
    Cloth,
    Sand,
    Water,
    Grass,
    Dirt,
    Glass,
};

/// @brief Entity handle as stored in body user-data. 0 == none
///        (`Entity::getId()` is never 0). This is the plain numeric id, NOT
///        the heavyweight, non-copyable `Entity` scene-graph object.
using EntityId = std::uint32_t;

/// @brief Packs an entity id + surface material into the Jolt body user-data
///        word.
///
/// Layout: `[63..40 reserved(0) | 39..8 entityId (u32) | 7..0 material (u8)]`.
/// The reserved high bits are deliberate headroom (e.g. a future flags byte)
/// and are left zeroed.
inline std::uint64_t packBodyTags(EntityId entityId, SurfaceMaterial material)
{
    return (static_cast<std::uint64_t>(entityId) << 8)
         | static_cast<std::uint64_t>(material);
}

/// @brief Unpacks the surface material from a body user-data word.
inline SurfaceMaterial unpackMaterial(std::uint64_t userData)
{
    return static_cast<SurfaceMaterial>(userData & 0xFFull);
}

/// @brief Unpacks the owning entity id from a body user-data word (0 == none).
inline EntityId unpackEntity(std::uint64_t userData)
{
    return static_cast<EntityId>((userData >> 8) & 0xFFFFFFFFull);
}

/// @brief Stable, human-readable label — debug panels, the editor material
///        picker, and scene-serialisation diagnostics.
inline const char* surfaceMaterialLabel(SurfaceMaterial material)
{
    switch (material)
    {
    case SurfaceMaterial::Default: return "Default";
    case SurfaceMaterial::Stone:   return "Stone";
    case SurfaceMaterial::Wood:    return "Wood";
    case SurfaceMaterial::Metal:   return "Metal";
    case SurfaceMaterial::Cloth:   return "Cloth";
    case SurfaceMaterial::Sand:    return "Sand";
    case SurfaceMaterial::Water:   return "Water";
    case SurfaceMaterial::Grass:   return "Grass";
    case SurfaceMaterial::Dirt:    return "Dirt";
    case SurfaceMaterial::Glass:   return "Glass";
    }
    return "Default";
}

} // namespace Vestige
