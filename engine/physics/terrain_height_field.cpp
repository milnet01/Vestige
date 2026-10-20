// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file terrain_height_field.cpp
/// @brief Implementation of createTerrainHeightFieldShape (see terrain_height_field.h).

#include "physics/terrain_height_field.h"

#include "core/logger.h"

#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>

#include <string>

namespace Vestige
{

JPH::ShapeRefC createTerrainHeightFieldShape(const TerrainHeightFieldDesc& desc)
{
    // Jolt needs at least two samples per side; its own sample-count rounding
    // pads anything that is not a multiple of the block size (257 → 258).
    const size_t expected = static_cast<size_t>(desc.sampleCount) *
                            static_cast<size_t>(desc.sampleCount);
    if (desc.sampleCount < 2 || desc.heights01.size() != expected)
    {
        Logger::warning("Terrain collider: malformed heightmap (" +
                        std::to_string(desc.heights01.size()) + " samples for side " +
                        std::to_string(desc.sampleCount) + ")");
        return nullptr;
    }

    // Jolt's surface is offset + scale * (x, sample, z), so the normalised
    // samples go in unchanged and heightScale goes in the scale's Y.
    const JPH::HeightFieldShapeSettings settings(
        desc.heights01.data(),
        JPH::Vec3(desc.origin.x, 0.0f, desc.origin.z),
        JPH::Vec3(desc.spacingX, desc.heightScale, desc.spacingZ),
        static_cast<JPH::uint32>(desc.sampleCount));

    JPH::ShapeSettings::ShapeResult result = settings.Create();
    if (result.HasError())
    {
        Logger::warning("Terrain collider: Jolt refused the height field: " +
                        std::string(result.GetError().c_str()));  // JPH::String has its own allocator
        return nullptr;
    }
    return result.Get();
}

} // namespace Vestige
