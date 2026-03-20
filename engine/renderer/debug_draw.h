/// @file debug_draw.h
/// @brief Immediate-mode debug line rendering for editor overlays (light gizmos, etc.).
#pragma once

#include "renderer/shader.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Vestige
{

/// @brief Immediate-mode debug line renderer using GL_LINES.
///
/// Usage: call static methods (line, circle, wireSphere, cone, arrow) to queue
/// line segments during the frame, then call flush() once to render them all
/// and clear the buffer.
class DebugDraw
{
public:
    /// @brief Initializes GPU resources (VAO, VBO, shader).
    /// @param assetPath Base path to the assets directory (for shader loading).
    /// @return True if initialization succeeded.
    bool initialize(const std::string& assetPath);

    /// @brief Releases GPU resources.
    void cleanup();

    /// @brief Queues a line segment.
    static void line(const glm::vec3& from, const glm::vec3& to,
                     const glm::vec3& color);

    /// @brief Queues a circle (line segments in a plane).
    /// @param center Center of the circle.
    /// @param normal Normal of the circle's plane.
    /// @param radius Radius of the circle.
    /// @param color Line color.
    /// @param segments Number of line segments (more = smoother).
    static void circle(const glm::vec3& center, const glm::vec3& normal,
                       float radius, const glm::vec3& color, int segments = 32);

    /// @brief Queues 3 orthogonal great circles (XY, XZ, YZ planes).
    static void wireSphere(const glm::vec3& center, float radius,
                           const glm::vec3& color, int segments = 32);

    /// @brief Queues a cone wireframe (apex + far circle + rib lines).
    /// @param apex Tip of the cone (light position).
    /// @param direction Direction the cone points.
    /// @param length Distance from apex to the far circle.
    /// @param angleDeg Half-angle of the cone in degrees.
    /// @param color Line color.
    /// @param ribs Number of lines from apex to the far circle.
    static void cone(const glm::vec3& apex, const glm::vec3& direction,
                     float length, float angleDeg,
                     const glm::vec3& color, int ribs = 8);

    /// @brief Queues an arrow (line + arrowhead).
    /// @param from Start point.
    /// @param to End point (tip of arrow).
    /// @param color Line color.
    /// @param headSize Length of arrowhead lines as fraction of arrow length.
    static void arrow(const glm::vec3& from, const glm::vec3& to,
                      const glm::vec3& color, float headSize = 0.15f);

    /// @brief Renders all queued lines and clears the buffer.
    /// @param viewProjection Combined VP matrix from the camera.
    void flush(const glm::mat4& viewProjection);

    /// @brief Returns the number of queued vertices (for diagnostics).
    static size_t getQueuedVertexCount();

private:
    /// @brief Per-vertex data for debug lines.
    struct DebugVertex
    {
        glm::vec3 position;
        glm::vec3 color;
    };

    /// @brief Shared line buffer — static so static methods can append to it.
    static std::vector<DebugVertex> s_vertices;

    Shader m_shader;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    size_t m_vboCapacity = 0;   ///< Current VBO allocation in vertices.
    bool m_initialized = false;
};

} // namespace Vestige
