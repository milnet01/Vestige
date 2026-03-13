/// @file renderer.cpp
/// @brief Renderer implementation with Blinn-Phong lighting, shadows, and FBO pipeline.
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

    // Subscribe to window resize events
    m_eventBus.subscribe<WindowResizeEvent>([this](const WindowResizeEvent& event)
    {
        onWindowResize(event.width, event.height);
    });

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

    // Load screen quad shader (for the FBO → screen pass)
    std::string screenVertPath = assetPath + "/shaders/screen_quad.vert.glsl";
    std::string screenFragPath = assetPath + "/shaders/screen_quad.frag.glsl";

    if (!m_screenShader.loadFromFiles(screenVertPath, screenFragPath))
    {
        Logger::error("Failed to load screen quad shaders");
        return false;
    }

    // Load shadow depth shader (for the shadow pass)
    std::string shadowVertPath = assetPath + "/shaders/shadow_depth.vert.glsl";
    std::string shadowFragPath = assetPath + "/shaders/shadow_depth.frag.glsl";

    if (!m_shadowDepthShader.loadFromFiles(shadowVertPath, shadowFragPath))
    {
        Logger::error("Failed to load shadow depth shaders");
        return false;
    }

    Logger::info("Shaders loaded successfully");
    return true;
}

void Renderer::initFramebuffers(int width, int height, int msaaSamples)
{
    m_windowWidth = width;
    m_windowHeight = height;
    m_msaaSamples = msaaSamples;

    // MSAA framebuffer — scene is rendered here
    FramebufferConfig msaaConfig;
    msaaConfig.width = width;
    msaaConfig.height = height;
    msaaConfig.samples = msaaSamples;
    msaaConfig.hasColorAttachment = true;
    msaaConfig.hasDepthAttachment = true;
    m_msaaFbo = std::make_unique<Framebuffer>(msaaConfig);

    // Resolve framebuffer — MSAA is resolved to this non-multisampled FBO
    FramebufferConfig resolveConfig;
    resolveConfig.width = width;
    resolveConfig.height = height;
    resolveConfig.samples = 1;
    resolveConfig.hasColorAttachment = true;
    resolveConfig.hasDepthAttachment = false;
    m_resolveFbo = std::make_unique<Framebuffer>(resolveConfig);

    // Fullscreen quad for the screen pass
    m_screenQuad = std::make_unique<FullscreenQuad>();

    // Shadow map for the directional light
    m_shadowMap = std::make_unique<ShadowMap>();

    Logger::info("Framebuffer pipeline initialized: "
        + std::to_string(width) + "x" + std::to_string(height)
        + " with " + std::to_string(msaaSamples) + "x MSAA + shadow mapping");
}

void Renderer::beginFrame()
{
    if (m_msaaFbo)
    {
        // Render to the MSAA framebuffer
        m_msaaFbo->bind();
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::endFrame()
{
    if (!m_msaaFbo || !m_resolveFbo || !m_screenQuad)
    {
        // FBOs not initialized — nothing to resolve
        return;
    }

    // Resolve MSAA → non-multisampled FBO
    m_msaaFbo->resolve(*m_resolveFbo);

    // Draw to the default framebuffer (screen)
    Framebuffer::unbind();
    glViewport(0, 0, m_windowWidth, m_windowHeight);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    // Draw the resolved texture to the screen via fullscreen quad
    m_screenShader.use();
    m_resolveFbo->bindColorTexture(0);
    m_screenShader.setInt("u_screenTexture", 0);
    m_screenQuad->draw();

    glEnable(GL_DEPTH_TEST);
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

    // --- Shadow pass ---
    if (m_shadowMap && m_hasDirectionalLight)
    {
        renderShadowPass(renderData);
    }

    // Re-bind the MSAA FBO (shadow pass changed the bound framebuffer)
    if (m_msaaFbo)
    {
        m_msaaFbo->bind();
    }

    // --- Set shadow uniforms for the lighting pass ---
    m_blinnPhongShader.use();
    if (m_shadowMap && m_hasDirectionalLight)
    {
        m_shadowMap->bindShadowTexture(3);  // Texture unit 3 for shadow map
        m_blinnPhongShader.setInt("u_shadowMap", 3);
        m_blinnPhongShader.setMat4("u_lightSpaceMatrix", m_shadowMap->getLightSpaceMatrix());
        m_blinnPhongShader.setBool("u_hasShadows", true);
    }
    else
    {
        m_blinnPhongShader.setBool("u_hasShadows", false);
    }

    // --- Scene pass: draw all render items with full lighting + shadows ---
    for (const auto& item : renderData.renderItems)
    {
        drawMesh(*item.mesh, item.worldMatrix, *item.material, camera, aspectRatio);
    }
}

void Renderer::renderShadowPass(const SceneRenderData& renderData)
{
    // Update shadow map light-space matrix from the directional light
    m_shadowMap->update(m_directionalLight, glm::vec3(0.0f, 0.0f, 0.0f));

    // Begin shadow pass (binds depth FBO, sets front-face culling)
    m_shadowMap->beginShadowPass();

    // Render all scene geometry using the shadow depth shader
    m_shadowDepthShader.use();
    m_shadowDepthShader.setMat4("u_lightSpaceMatrix", m_shadowMap->getLightSpaceMatrix());

    for (const auto& item : renderData.renderItems)
    {
        m_shadowDepthShader.setMat4("u_model", item.worldMatrix);
        item.mesh->bind();
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(item.mesh->getIndexCount()),
                       GL_UNSIGNED_INT, nullptr);
        item.mesh->unbind();
    }

    // End shadow pass (restores culling, unbinds FBO)
    m_shadowMap->endShadowPass();
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

void Renderer::onWindowResize(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }

    m_windowWidth = width;
    m_windowHeight = height;

    if (m_msaaFbo)
    {
        m_msaaFbo->resize(width, height);
    }
    if (m_resolveFbo)
    {
        m_resolveFbo->resize(width, height);
    }
    // Shadow map does not resize with the window — it has a fixed resolution
}

} // namespace Vestige
