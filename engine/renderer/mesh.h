/// @file mesh.h
/// @brief Vertex data and GPU buffer management for 3D geometry.
#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <vector>

namespace Vestige
{

/// @brief A single vertex with position, normal, and color data.
struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
};

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

    /// @brief Creates a unit cube mesh (1x1x1, centered at origin).
    /// @return A mesh containing a colored cube.
    static Mesh createCube();

private:
    void cleanup();

    GLuint m_vao;  // Vertex Array Object
    GLuint m_vbo;  // Vertex Buffer Object
    GLuint m_ebo;  // Element Buffer Object (indices)
    uint32_t m_indexCount;
};

} // namespace Vestige
