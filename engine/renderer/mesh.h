/// @file mesh.h
/// @brief Vertex data and GPU buffer management for 3D geometry.
#pragma once

#include "utils/aabb.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <vector>

namespace Vestige
{

/// @brief A single vertex with position, normal, color, texture coordinate, tangent, and bone data.
struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec3 tangent;
    glm::vec3 bitangent;
    glm::ivec4 boneIds = glm::ivec4(0);      ///< Joint indices (up to 4 per vertex)
    glm::vec4 boneWeights = glm::vec4(0.0f);  ///< Corresponding blend weights (sum to 1.0)
};

/// @brief Computes tangent and bitangent vectors for normal mapping.
/// Uses per-triangle edge/UV math with Gram-Schmidt orthogonalization.
/// @param vertices The vertex data (modified in-place).
/// @param indices The index data (triangles).
void calculateTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

/// @brief Manages vertex/index data and OpenGL buffer objects for a piece of geometry.
class Mesh
{
public:
    Mesh();
    ~Mesh();

    // Non-copyable (owns GPU resources)
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    // Movable
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    /// @brief Uploads vertex and index data to the GPU.
    /// @param vertices The vertex data.
    /// @param indices The index data (triangles).
    void upload(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    /// @brief Binds the vertex array for rendering.
    void bind() const;

    /// @brief Unbinds the vertex array.
    void unbind() const;

    /// @brief Gets the number of indices (for glDrawElements).
    uint32_t getIndexCount() const;

    /// @brief Configures instance attribute slots (locations 6-9) for instanced rendering.
    /// @param instanceVbo Handle to a VBO containing per-instance mat4 matrices.
    void setupInstanceAttributes(GLuint instanceVbo) const;

    /// @brief Gets the VAO handle.
    GLuint getVao() const;

    /// @brief Gets the local-space bounding box computed during upload().
    /// Returns a zero-sized AABB if no geometry has been uploaded.
    const AABB& getLocalBounds() const;

    /// @brief Creates a unit cube mesh (1x1x1, centered at origin).
    /// @return A mesh containing a colored cube.
    static Mesh createCube();

    /// @brief Creates a ground plane mesh.
    /// @param size Half-size of the plane.
    /// @return A mesh containing a flat ground plane.
    static Mesh createPlane(float size = 10.0f);

    /// @brief Creates a UV sphere mesh (diameter 1m, centered at origin).
    /// @param sectors Number of longitude slices (default 32).
    /// @param stacks Number of latitude rings (default 16).
    /// @return A mesh containing a UV sphere.
    static Mesh createSphere(uint32_t sectors = 32, uint32_t stacks = 16);

    /// @brief Creates a capped cylinder mesh (diameter 1m, height 1m, centered at origin).
    /// @param sectors Number of circumference segments (default 32).
    /// @return A mesh containing a capped cylinder.
    static Mesh createCylinder(uint32_t sectors = 32);

    /// @brief Creates a capped cone mesh (base diameter 1m, height 1m, centered at origin).
    /// @param sectors Number of circumference segments (default 32).
    /// @param stacks Number of height segments on the side surface (default 4).
    /// @return A mesh containing a capped cone.
    static Mesh createCone(uint32_t sectors = 32, uint32_t stacks = 4);

    /// @brief Creates a wedge (triangular prism / ramp) mesh (1x1x1 bounding box).
    /// @return A mesh containing a wedge shape.
    static Mesh createWedge();

private:
    void cleanup();

    GLuint m_vao;  // Vertex Array Object
    GLuint m_vbo;  // Vertex Buffer Object
    GLuint m_ebo;  // Element Buffer Object (indices)
    uint32_t m_indexCount;
    AABB m_localBounds;  // Computed from vertex positions during upload()
};

} // namespace Vestige
