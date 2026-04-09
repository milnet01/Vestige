/// @file hdri_viewer_panel.cpp
/// @brief HDRI viewer panel implementation.
#include "editor/panels/hdri_viewer_panel.h"
#include "core/logger.h"
#include "renderer/renderer.h"
#include "resource/resource_manager.h"

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace Vestige
{

// ============================================================================
// Constants
// ============================================================================

static constexpr float MIN_EXPOSURE = -10.0f;
static constexpr float MAX_EXPOSURE = 10.0f;
static constexpr float MIN_PITCH = -90.0f;
static constexpr float MAX_PITCH = 90.0f;

// ============================================================================
// Initialization
// ============================================================================

bool HdriViewerPanel::initialize(const std::string& assetPath)
{
    if (m_initialized)
    {
        return true;
    }

    m_assetPath = assetPath;

    // Load equirectangular preview shader
    std::string eqVertPath = assetPath + "/shaders/equirect_preview.vert.glsl";
    std::string eqFragPath = assetPath + "/shaders/equirect_preview.frag.glsl";
    if (!m_equirectShader.loadFromFiles(eqVertPath, eqFragPath))
    {
        Logger::error("[HdriViewer] Failed to load equirect_preview shaders");
        return false;
    }

    // Load skybox shader for cubemap preview
    std::string sbVertPath = assetPath + "/shaders/skybox.vert.glsl";
    std::string sbFragPath = assetPath + "/shaders/skybox.frag.glsl";
    if (!m_skyboxShader.loadFromFiles(sbVertPath, sbFragPath))
    {
        Logger::error("[HdriViewer] Failed to load skybox shaders");
        return false;
    }

    // Create skybox preview FBO
    FramebufferConfig config;
    config.width = m_previewSize;
    config.height = m_previewSize;
    config.samples = 1;
    config.hasColorAttachment = true;
    config.hasDepthAttachment = true;
    config.isFloatingPoint = false;
    m_skyboxFbo = std::make_unique<Framebuffer>(config);

    // Empty VAO for fullscreen triangle
    glGenVertexArrays(1, &m_dummyVao);

    m_initialized = true;
    return true;
}

void HdriViewerPanel::cleanup()
{
    m_skyboxFbo.reset();
    m_equirectFbo.reset();
    m_previewSkybox.reset();
    m_equirectTexture.reset();
    if (m_dummyVao)
    {
        glDeleteVertexArrays(1, &m_dummyVao);
        m_dummyVao = 0;
    }
    m_initialized = false;
}

// ============================================================================
// Open HDRI
// ============================================================================

void HdriViewerPanel::openHdri(const std::string& path, ResourceManager* resources)
{
    m_hdriPath = path;
    m_open = true;
    m_skyboxDirty = true;

    // Reset view state
    m_exposure = 0.0f;
    m_previewYaw = 0.0f;
    m_previewPitch = 0.0f;
    m_equirectYaw = 0.0f;
    m_equirectPitch = 0.0f;

    // Load equirectangular texture for flat preview
    if (resources)
    {
        m_equirectTexture = std::make_shared<Texture>();
        if (!m_equirectTexture->loadFromFile(path, true))
        {
            Logger::warning("[HdriViewer] Failed to load HDRI: " + path);
            m_equirectTexture.reset();
        }
    }

    // Convert to cubemap for skybox preview
    m_previewSkybox = std::make_unique<Skybox>();
    if (!m_previewSkybox->loadEquirectangular(path))
    {
        Logger::warning("[HdriViewer] Failed to convert HDRI to cubemap: " + path);
        m_previewSkybox.reset();
    }
}

// ============================================================================
// Setters with clamping
// ============================================================================

void HdriViewerPanel::setExposure(float ev)
{
    m_exposure = std::clamp(ev, MIN_EXPOSURE, MAX_EXPOSURE);
    m_skyboxDirty = true;
}

void HdriViewerPanel::setPreviewAngles(float yaw, float pitch)
{
    // Wrap yaw to [0, 360)
    m_previewYaw = std::fmod(yaw, 360.0f);
    if (m_previewYaw < 0.0f)
    {
        m_previewYaw += 360.0f;
    }

    m_previewPitch = std::clamp(pitch, MIN_PITCH, MAX_PITCH);
    m_skyboxDirty = true;
}

// ============================================================================
// Draw
// ============================================================================

void HdriViewerPanel::draw(Renderer* renderer)
{
    if (!m_open)
    {
        return;
    }

    ImGui::Begin("HDRI Viewer", &m_open);

    if (!m_equirectTexture || !m_equirectTexture->isLoaded())
    {
        ImGui::TextDisabled("No HDRI loaded. Double-click an .hdr file in the Asset Browser.");
        ImGui::End();
        return;
    }

    drawControls(renderer);
    ImGui::Separator();

    // Two-column layout: equirect left, skybox preview right
    float totalWidth = ImGui::GetContentRegionAvail().x;
    float skyboxSize = static_cast<float>(m_previewSize);
    float equirectWidth = totalWidth - skyboxSize - 16.0f;
    if (equirectWidth < 200.0f)
    {
        equirectWidth = totalWidth;
        skyboxSize = 0.0f;
    }

    if (equirectWidth > 0.0f)
    {
        ImGui::BeginChild("##equirect_view", ImVec2(equirectWidth, 0), false);
        drawEquirectPreview();
        ImGui::EndChild();
    }

    if (skyboxSize > 0.0f)
    {
        ImGui::SameLine();
        ImGui::BeginChild("##skybox_view", ImVec2(skyboxSize, skyboxSize + 20.0f), false);
        ImGui::Text("Skybox Preview");
        drawSkyboxPreview();
        ImGui::EndChild();
    }

    ImGui::End();
}

// ============================================================================
// Controls toolbar
// ============================================================================

void HdriViewerPanel::drawControls(Renderer* renderer)
{
    // Exposure slider
    ImGui::Text("Exposure:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    float ev = m_exposure;
    if (ImGui::SliderFloat("##exposure", &ev, MIN_EXPOSURE, MAX_EXPOSURE, "%.1f EV"))
    {
        setExposure(ev);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##exp"))
    {
        setExposure(0.0f);
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // Set as Environment button
    if (renderer)
    {
        if (ImGui::Button("Set as Scene Environment"))
        {
            if (renderer->loadSkyboxHDRI(m_hdriPath))
            {
                Logger::info("[HdriViewer] Set scene environment: " + m_hdriPath);
            }
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Load this HDRI as the scene skybox and generate IBL maps");
        }
    }

    // File info
    if (!m_hdriPath.empty())
    {
        std::string filename = std::filesystem::path(m_hdriPath).filename().string();
        ImGui::SameLine();
        ImGui::TextDisabled("| %s (%dx%d)", filename.c_str(),
                             m_equirectTexture->getWidth(),
                             m_equirectTexture->getHeight());
    }
}

// ============================================================================
// Equirectangular flat preview
// ============================================================================

void HdriViewerPanel::drawEquirectPreview()
{
    if (!m_equirectTexture || !m_initialized)
    {
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float imgH = avail.x * 0.5f;  // 2:1 aspect ratio for equirectangular
    if (imgH > avail.y - 10.0f)
    {
        imgH = avail.y - 10.0f;
    }
    float imgW = imgH * 2.0f;

    // Render equirect with exposure via shader to an FBO
    // For simplicity, display the raw texture with exposure applied via
    // the equirect_preview shader rendered to a temp FBO approach.
    // However, since ImGui cannot apply shaders, we render to the channel FBO.

    // Save GL state
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    // Use a simple approach: render equirect with exposure to a temp area
    // Since we need to show it in ImGui, use a small FBO
    int fboW = std::min(static_cast<int>(imgW), 1024);
    int fboH = std::min(static_cast<int>(imgH), 512);
    if (fboW < 64) fboW = 64;
    if (fboH < 64) fboH = 64;

    // Reuse equirect FBO, recreating if size changed
    if (!m_equirectFbo || m_equirectFbo->getWidth() != fboW ||
        m_equirectFbo->getHeight() != fboH)
    {
        FramebufferConfig config;
        config.width = fboW;
        config.height = fboH;
        config.samples = 1;
        config.hasColorAttachment = true;
        config.hasDepthAttachment = false;
        config.isFloatingPoint = false;
        m_equirectFbo = std::make_unique<Framebuffer>(config);
    }

    m_equirectFbo->bind();
    glViewport(0, 0, fboW, fboH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    m_equirectShader.use();
    m_equirectShader.setFloat("u_exposure", m_exposure);
    m_equirectShader.setFloat("u_yaw", m_equirectYaw);
    m_equirectShader.setFloat("u_pitch", m_equirectPitch);

    m_equirectTexture->bind(0);
    m_equirectShader.setInt("u_equirectMap", 0);

    glBindVertexArray(m_dummyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    // Restore state
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    // Display the rendered equirect preview
    GLuint texId = m_equirectFbo->getColorAttachmentId();
    ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(texId)),
                  ImVec2(imgW, imgH));

    // Handle drag for rotation
    if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        m_equirectYaw += delta.x * 0.005f;
        m_equirectPitch += delta.y * 0.005f;
        m_equirectPitch = std::clamp(m_equirectPitch,
                                      -glm::radians(90.0f), glm::radians(90.0f));
    }
}

// ============================================================================
// Skybox mini-viewport
// ============================================================================

void HdriViewerPanel::drawSkyboxPreview()
{
    if (!m_previewSkybox || !m_previewSkybox->hasTexture() || !m_initialized)
    {
        ImGui::TextDisabled("No cubemap available");
        return;
    }

    // Render skybox to FBO
    if (m_skyboxDirty)
    {
        renderSkyboxFbo();
        m_skyboxDirty = false;
    }

    // Display the skybox preview
    GLuint texId = m_skyboxFbo->getColorAttachmentId();
    float size = static_cast<float>(m_previewSize);
    ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(texId)),
                  ImVec2(size, size));

    // Handle orbit drag
    if (ImGui::IsItemHovered())
    {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            float yaw = m_previewYaw + delta.x * 0.5f;
            float pitch = m_previewPitch + delta.y * 0.5f;
            setPreviewAngles(yaw, pitch);
        }
    }
}

void HdriViewerPanel::renderSkyboxFbo()
{
    if (!m_previewSkybox || !m_skyboxFbo)
    {
        return;
    }

    // Save GL state
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    GLint prevDepthFunc;
    glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);

    // Bind FBO
    m_skyboxFbo->bind();
    glViewport(0, 0, m_previewSize, m_previewSize);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    // Set up view matrix from orbit angles
    float yawRad = glm::radians(m_previewYaw);
    float pitchRad = glm::radians(m_previewPitch);
    float cosPitch = std::cos(pitchRad);
    glm::vec3 dir(
        cosPitch * std::sin(yawRad),
        std::sin(pitchRad),
        cosPitch * std::cos(yawRad)
    );
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f), dir, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

    // Use skybox shader
    m_skyboxShader.use();
    m_skyboxShader.setMat4("u_view", view);
    m_skyboxShader.setMat4("u_projection", proj);
    m_skyboxShader.setBool("u_hasCubemap", true);

    // Bind the cubemap
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_previewSkybox->getTextureId());
    m_skyboxShader.setInt("u_skyboxTexture", 0);

    // Draw skybox (depth test at <=, skybox renders at far plane)
    glDepthFunc(GL_LEQUAL);
    m_previewSkybox->draw();

    // Restore GL state
    glDepthFunc(static_cast<GLenum>(prevDepthFunc));
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}

} // namespace Vestige
