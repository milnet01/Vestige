/// @file obj_loader.h
/// @brief Wavefront OBJ file loader.
#pragma once

#include "renderer/mesh.h"

#include <string>
#include <vector>

namespace Vestige
{

/// @brief Loads 3D geometry from Wavefront OBJ files.
class ObjLoader
{
public:
    /// @brief Loads an OBJ file and returns the mesh data.
    /// @param filePath Path to the .obj file.
    /// @param outVertices Output vertex data.
    /// @param outIndices Output index data.
    /// @return True if loading succeeded.
    static bool load(const std::string& filePath,
                     std::vector<Vertex>& outVertices,
                     std::vector<uint32_t>& outIndices);

    /// @brief Loads an OBJ file and creates a Mesh directly.
    /// @param filePath Path to the .obj file.
    /// @param outMesh Output mesh (uploaded to GPU).
    /// @return True if loading succeeded.
    static bool loadMesh(const std::string& filePath, Mesh& outMesh);
};

} // namespace Vestige
