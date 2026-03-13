/// @file renderer.cpp
/// @brief Renderer implementation with Blinn-Phong lighting.
#include "renderer/renderer.h"
#include "scene/scene.h"
#include "core/logger.h"

#include <glad/gl.h>

namespace Vestige
{

Renderer::Renderer(EventBus& eventBus)
    : m_eventBus(eventBus)
    , m_hasDirectionalLight(true)
    , m_isWireframe(false)
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

bool Renderer::loadShaders(const std::string& assetPath)
{
    // Load Blinn-Phong shader
    std::string vertPath = assetPath + "/shaders/blinn_phong.vert.glsl";
    std::string fragPath = assetPath + "/shaders/blinn_phong.frag.glsl";

    if (!m_blinnPhongShader.loadFromFiles(vertPath, fragPath))
    {
        Logger::error("Failed to load Blinn-Phong shaders");
        return false;
    }

    Logger::info("Shaders loaded successfully");
    return true;
}

void Renderer::beginFrame()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::drawMesh(const Mesh& mesh, const glm::mat4& modelMatrix,
                         const Material& material, const Camera& camera,
                         float aspectRatio)
{
    m_blinnPhongShader.use();

    // Transform matrices
    m_blinnPhongShader.setMat4("u_model", modelMatrix);
    m_blinnPhongShader.setMat4("u_view", camera.getViewMatrix());
    m_blinnPhongShader.setMat4("u_projection", camera.getProjectionMatrix(aspectRatio));

    // Material
    m_blinnPhongShader.setVec3("u_materialDiffuse", material.getDiffuseColor());
    m_blinnPhongShader.setVec3("u_materialSpecular", material.getSpecularColor());
    m_blinnPhongShader.setFloat("u_materialShininess", material.getShininess());

    // Texture
    bool hasTexture = material.hasDiffuseTexture();
    m_blinnPhongShader.setBool("u_hasTexture", hasTexture);
    if (hasTexture)
    {
        material.getDiffuseTexture()->bind(0);
        m_blinnPhongShader.setInt("u_diffuseTexture", 0);
    }

    // Wireframe
    m_blinnPhongShader.setBool("u_wireframe", m_isWireframe);

    // Lights
    uploadLightUniforms(camera);

    // Set polygon mode
    if (m_isWireframe)
    {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }

    // Draw
    mesh.bind();
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.getIndexCount()),
                   GL_UNSIGNED_INT, nullptr);
    mesh.unbind();

    // Restore polygon mode
    if (m_isWireframe)
    {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    // Unbind texture
    if (hasTexture)
    {
        Texture::unbind(0);
    }
}

void Renderer::setClearColor(const glm::vec3& color)
{
    glClearColor(color.r, color.g, color.b, 1.0f);
}

void Renderer::setDirectionalLight(const DirectionalLight& light)
{
    m_directionalLight = light;
    m_hasDirectionalLight = true;
}

void Renderer::clearPointLights()
{
    m_pointLights.clear();
}

bool Renderer::addPointLight(const PointLight& light)
{
    if (static_cast<int>(m_pointLights.size()) >= MAX_POINT_LIGHTS)
    {
        Logger::warning("Maximum point lights (" + std::to_string(MAX_POINT_LIGHTS) + ") reached");
        return false;
    }
    m_pointLights.push_back(light);
    return true;
}

void Renderer::clearSpotLights()
{
    m_spotLights.clear();
}

bool Renderer::addSpotLight(const SpotLight& light)
{
    if (static_cast<int>(m_spotLights.size()) >= MAX_SPOT_LIGHTS)
    {
        Logger::warning("Maximum spot lights (" + std::to_string(MAX_SPOT_LIGHTS) + ") reached");
        return false;
    }
    m_spotLights.push_back(light);
    return true;
}

void Renderer::setWireframeMode(bool isEnabled)
{
    m_isWireframe = isEnabled;
    Logger::debug(std::string("Wireframe mode: ") + (isEnabled ? "ON" : "OFF"));
}

bool Renderer::isWireframeMode() const
{
    return m_isWireframe;
}

void Renderer::setDirectionalLightEnabled(bool isEnabled)
{
    m_hasDirectionalLight = isEnabled;
}

void Renderer::renderScene(const SceneRenderData& renderData, const Camera& camera, float aspectRatio)
{
    // Apply lights from scene data
    m_hasDirectionalLight = renderData.hasDirectionalLight;
    if (renderData.hasDirectionalLight)
    {
        m_directionalLight = renderData.directionalLight;
    }

    m_pointLights.clear();
    for (const auto& pl : renderData.pointLights)
    {
        if (static_cast<int>(m_pointLights.size()) < MAX_POINT_LIGHTS)
        {
            m_pointLights.push_back(pl);
        }
    }

    m_spotLights.clear();
    for (const auto& sl : renderData.spotLights)
    {
        if (static_cast<int>(m_spotLights.size()) < MAX_SPOT_LIGHTS)
        {
            m_spotLights.push_back(sl);
        }
    }

    // Draw all render items
    for (const auto& item : renderData.renderItems)
    {
        drawMesh(*item.mesh, item.worldMatrix, *item.material, camera, aspectRatio);
    }
}

void Renderer::uploadLightUniforms(const Camera& camera)
{
    // Camera position for specular calculation
    m_blinnPhongShader.setVec3("u_viewPosition", camera.getPosition());

    // Directional light
    m_blinnPhongShader.setBool("u_hasDirLight", m_hasDirectionalLight);
    if (m_hasDirectionalLight)
    {
        m_blinnPhongShader.setVec3("u_dirLight_direction", m_directionalLight.direction);
        m_blinnPhongShader.setVec3("u_dirLight_ambient", m_directionalLight.ambient);
        m_blinnPhongShader.setVec3("u_dirLight_diffuse", m_directionalLight.diffuse);
        m_blinnPhongShader.setVec3("u_dirLight_specular", m_directionalLight.specular);
    }

    // Point lights
    int pointCount = static_cast<int>(m_pointLights.size());
    m_blinnPhongShader.setInt("u_pointLightCount", pointCount);
    for (int i = 0; i < pointCount; i++)
    {
        std::string prefix = "u_pointLights_";
        std::string idx = "[" + std::to_string(i) + "]";
        m_blinnPhongShader.setVec3(prefix + "position" + idx, m_pointLights[static_cast<size_t>(i)].position);
        m_blinnPhongShader.setVec3(prefix + "ambient" + idx, m_pointLights[static_cast<size_t>(i)].ambient);
        m_blinnPhongShader.setVec3(prefix + "diffuse" + idx, m_pointLights[static_cast<size_t>(i)].diffuse);
        m_blinnPhongShader.setVec3(prefix + "specular" + idx, m_pointLights[static_cast<size_t>(i)].specular);
        m_blinnPhongShader.setFloat(prefix + "constant" + idx, m_pointLights[static_cast<size_t>(i)].constant);
        m_blinnPhongShader.setFloat(prefix + "linear" + idx, m_pointLights[static_cast<size_t>(i)].linear);
        m_blinnPhongShader.setFloat(prefix + "quadratic" + idx, m_pointLights[static_cast<size_t>(i)].quadratic);
    }

    // Spot lights
    int spotCount = static_cast<int>(m_spotLights.size());
    m_blinnPhongShader.setInt("u_spotLightCount", spotCount);
    for (int i = 0; i < spotCount; i++)
    {
        std::string prefix = "u_spotLights_";
        std::string idx = "[" + std::to_string(i) + "]";
        m_blinnPhongShader.setVec3(prefix + "position" + idx, m_spotLights[static_cast<size_t>(i)].position);
        m_blinnPhongShader.setVec3(prefix + "direction" + idx, m_spotLights[static_cast<size_t>(i)].direction);
        m_blinnPhongShader.setVec3(prefix + "ambient" + idx, m_spotLights[static_cast<size_t>(i)].ambient);
        m_blinnPhongShader.setVec3(prefix + "diffuse" + idx, m_spotLights[static_cast<size_t>(i)].diffuse);
        m_blinnPhongShader.setVec3(prefix + "specular" + idx, m_spotLights[static_cast<size_t>(i)].specular);
        m_blinnPhongShader.setFloat(prefix + "innerCutoff" + idx, m_spotLights[static_cast<size_t>(i)].innerCutoff);
        m_blinnPhongShader.setFloat(prefix + "outerCutoff" + idx, m_spotLights[static_cast<size_t>(i)].outerCutoff);
        m_blinnPhongShader.setFloat(prefix + "constant" + idx, m_spotLights[static_cast<size_t>(i)].constant);
        m_blinnPhongShader.setFloat(prefix + "linear" + idx, m_spotLights[static_cast<size_t>(i)].linear);
        m_blinnPhongShader.setFloat(prefix + "quadratic" + idx, m_spotLights[static_cast<size_t>(i)].quadratic);
    }
}

} // namespace Vestige
