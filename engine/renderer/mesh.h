/// @file mesh.h
/// @brief Vertex data and GPU buffer management for 3D geometry.
#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <vector>

namespace Vestige
{

/// @brief A single vertex with position, normal, color, texture coordinate, and tangent data.
struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec3 tangent;
    glm::vec3 bitangent;
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

    /// @brief Creates a unit cube mesh (1x1x1, centered at origin).
    /// @return A mesh containing a colored cube.
    static Mesh createCube();

    /// @brief Creates a ground plane mesh.
    /// @param size Half-size of the plane.
    /// @return A mesh containing a flat ground plane.
    static Mesh createPlane(float size = 10.0f);

private:
    void cleanup();

    GLuint m_vao;  // Vertex Array Object
    GLuint m_vbo;  // Vertex Buffer Object
    GLuint m_ebo;  // Element Buffer Object (indices)
    uint32_t m_indexCount;
};

} // namespace Vestige
