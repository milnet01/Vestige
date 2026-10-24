// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file terrain_collider_sync.h
/// @brief Decides when the terrain's physics collider must be rebuilt.
#pragma once

#include <cstdint>

namespace Vestige
{

/// @brief Rebuild the terrain collider once the heights stop changing.
///
/// Fed `Terrain::getHeightRevision()` once a frame. It asks for a rebuild when
/// the revision differs from the one last built AND has not moved since the
/// previous frame. A brush stroke changes the heights every frame, so the
/// collider is rebuilt once, the frame after the stroke ends; an undo or a
/// panel edit changes them once, so it is rebuilt the frame after (3D_E-0780).
class TerrainColliderSync
{
public:
    /// @brief True when the collider should be rebuilt for @p revision.
    bool shouldRebuild(std::uint64_t revision)
    {
        const bool settled = (revision == m_lastSeen);
        m_lastSeen = revision;
        return settled && revision != m_built;
    }

    /// @brief Records that the collider now matches @p revision.
    void markBuilt(std::uint64_t revision)
    {
        m_built    = revision;
        m_lastSeen = revision;
    }

private:
    std::uint64_t m_built    = 0;
    std::uint64_t m_lastSeen = 0;
};

} // namespace Vestige
