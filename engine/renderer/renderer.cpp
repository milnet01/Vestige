/// @file renderer.cpp
/// @brief Renderer implementation.
#include "renderer/renderer.h"
#include "core/logger.h"

#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>

namespace Vestige
{

Renderer::Renderer(EventBus& eventBus)
    : m_eventBus(eventBus)
{
    // Enable depth testing — closer objects obscure farther ones
    glEnable(GL_DEPTH_TEST);

    // Enable back-face culling — skip drawing the back side of triangles
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // Default clear color (dark grey)
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);

    Logger::info("Renderer initialized (OpenGL 4.5)");
}

Renderer::~Renderer()
{
    Logger::debug("Renderer destroyed");
}

void Renderer::beginFrame()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::drawMesh(const Mesh& mesh, const glm::mat4& modelMatrix,
                         const Camera& camera, float aspectRatio)
{
    m_basicShader.use();
    m_basicShader.setMat4("u_model", modelMatrix);
    m_basicShader.setMat4("u_view", camera.getViewMatrix());
    m_basicShader.setMat4("u_projection", camera.getProjectionMatrix(aspectRatio));

    mesh.bind();
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.getIndexCount()),
                   GL_UNSIGNED_INT, nullptr);
    mesh.unbind();
}

void Renderer::setClearColor(const glm::vec3& color)
{
    glClearColor(color.r, color.g, color.b, 1.0f);
}

Shader& Renderer::getBasicShader()
{
    return m_basicShader;
}

bool Renderer::loadShaders(const std::string& assetPath)
{
    std::string vertexPath = assetPath + "/shaders/basic.vert.glsl";
    std::string fragmentPath = assetPath + "/shaders/basic.frag.glsl";

    if (!m_basicShader.loadFromFiles(vertexPath, fragmentPath))
    {
        Logger::error("Failed to load basic shaders");
        return false;
    }

    Logger::info("Shaders loaded successfully");
    return true;
}

} // namespace Vestige
