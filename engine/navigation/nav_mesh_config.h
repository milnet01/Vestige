/// @file nav_mesh_config.h
/// @brief Configuration parameters for navmesh generation.
#pragma once

namespace Vestige
{

/// @brief Parameters controlling Recast navmesh generation.
///
/// Sensible defaults for human-scale (1.8m tall) agents walking on
/// architectural scenes. Adjust for different agent sizes or terrain.
struct NavMeshBuildConfig
{
    float cellSize = 0.3f;           ///< Voxel XZ size (meters). Smaller = more detail.
    float cellHeight = 0.2f;         ///< Voxel Y size (meters).
    float agentHeight = 1.8f;        ///< Agent height for walkability filtering.
    float agentRadius = 0.4f;        ///< Agent radius for obstacle clearance.
    float agentMaxClimb = 0.4f;      ///< Max step height the agent can climb.
    float agentMaxSlope = 45.0f;     ///< Max walkable slope in degrees.
    float regionMinSize = 8.0f;      ///< Min region area (in voxel units squared).
    float regionMergeSize = 20.0f;   ///< Region merge threshold.
    float edgeMaxLen = 12.0f;        ///< Max edge length (in world units).
    float edgeMaxError = 1.3f;       ///< Edge simplification error.
    int vertsPerPoly = 6;            ///< Max vertices per polygon (up to 6).
    float detailSampleDist = 6.0f;   ///< Detail mesh sample distance.
    float detailSampleMaxError = 1.0f; ///< Detail mesh max sample error.
};

} // namespace Vestige
