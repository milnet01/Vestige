// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file model_viewer_panel.cpp
/// @brief Model viewer panel implementation.
#include "editor/panels/model_viewer_panel.h"
#include "core/logger.h"
#include "renderer/debug_draw.h"
#include "renderer/material.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace Vestige
{

// ============================================================================
// Constants
// ============================================================================

static constexpr float MIN_PLAYBACK_SPEED = 0.0f;
static constexpr float MAX_PLAYBACK_SPEED = 10.0f;
static constexpr float ORBIT_SENSITIVITY = 0.5f;
static constexpr float PAN_SENSITIVITY = 0.005f;
static constexpr float ZOOM_SENSITIVITY = 0.15f;
static constexpr float MIN_PITCH = -89.0f;
static constexpr float MAX_PITCH = 89.0f;

// ============================================================================
// Initialization
// ============================================================================

bool ModelViewerPanel::initialize(const std::string& assetPath)
{
    if (m_initialized)
    {
        return true;
    }

    m_assetPath = assetPath;

    // Load model preview shader (model_preview.vert + material_preview.frag)
    std::string vertPath = assetPath + "/shaders/model_preview.vert.glsl";
    std::string fragPath = assetPath + "/shaders/material_preview.frag.glsl";
    if (!m_previewShader.loadFromFiles(vertPath, fragPath))
    {
        Logger::error("[ModelViewer] Failed to load preview shaders");
        return false;
    }

    // Create fallback white texture for Mesa sampler binding
    m_defaultTexture = std::make_shared<Texture>();
    m_defaultTexture->createSolidColor(255, 255, 255);

    // Create initial FBO
    FramebufferConfig config;
    config.width = m_fboWidth;
    config.height = m_fboHeight;
    config.samples = 1;
    config.hasColorAttachment = true;
    config.hasDepthAttachment = true;
    config.isFloatingPoint = false;
    m_fbo = std::make_unique<Framebuffer>(config);

    m_initialized = true;
    return true;
}

void ModelViewerPanel::cleanup()
{
    m_fbo.reset();
    m_defaultTexture.reset();
    m_model.reset();
    m_animator.reset();
    m_initialized = false;
}

// ============================================================================
// Open model
// ============================================================================

void ModelViewerPanel::openModel(std::shared_ptr<Model> model, const std::string& path)
{
    m_model = model;
    m_modelPath = path;
    m_open = true;
    m_dirty = true;

    // Compute stats
    m_totalVertices = 0;
    m_totalTriangles = 0;
    for (const auto& prim : model->m_primitives)
    {
        if (prim.mesh)
        {
            // Read vertex count from VBO size
            GLuint vao = prim.mesh->getVao();
            if (vao != 0)
            {
                m_totalTriangles += static_cast<int>(prim.mesh->getIndexCount()) / 3;
            }
        }
    }

    // Get model bounds
    m_modelBounds = model->getBounds();

    // Set up animation if available
    m_animator.reset();
    m_selectedClip = -1;
    if (model->m_skeleton && !model->m_animationClips.empty())
    {
        m_animator = std::make_unique<SkeletonAnimator>();
        m_animator->setSkeleton(model->m_skeleton);
        for (const auto& clip : model->m_animationClips)
        {
            m_animator->addClip(clip);
        }
    }

    // Auto-fit camera to model
    autoFitCamera();
}

void ModelViewerPanel::autoFitCamera()
{
    if (!m_model)
    {
        return;
    }

    glm::vec3 center = (m_modelBounds.min + m_modelBounds.max) * 0.5f;
    glm::vec3 extent = m_modelBounds.max - m_modelBounds.min;
    float diagonal = glm::length(extent);

    m_focusPoint = center;
    m_distance = std::max(diagonal * 1.5f, 0.5f);
    m_yaw = 30.0f;
    m_pitch = 20.0f;
}

// ============================================================================
// Setters
// ============================================================================

void ModelViewerPanel::setPlaybackSpeed(float speed)
{
    m_playbackSpeed = std::clamp(speed, MIN_PLAYBACK_SPEED, MAX_PLAYBACK_SPEED);
}

// ============================================================================
// Orbit camera math
// ============================================================================

glm::vec3 ModelViewerPanel::computeCameraPosition() const
{
    float yawRad = glm::radians(m_yaw);
    float pitchRad = glm::radians(m_pitch);
    float cosPitch = std::cos(pitchRad);

    glm::vec3 offset(
        cosPitch * std::sin(yawRad),
        std::sin(pitchRad),
        cosPitch * std::cos(yawRad)
    );

    return m_focusPoint + offset * m_distance;
}

glm::mat4 ModelViewerPanel::computeViewMatrix() const
{
    glm::vec3 eye = computeCameraPosition();
    return glm::lookAt(eye, m_focusPoint, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 ModelViewerPanel::computeProjectionMatrix(float aspectRatio) const
{
    return glm::perspective(glm::radians(45.0f), aspectRatio, 0.01f, 1000.0f);
}

// ============================================================================
// Draw
// ============================================================================

void ModelViewerPanel::draw(Scene* /*scene*/, ResourceManager* /*resources*/, float deltaTime)
{
    if (!m_open)
    {
        return;
    }

    ImGui::Begin("Model Viewer", &m_open);

    if (!m_model)
    {
        ImGui::TextDisabled("No model loaded. Double-click a model in the Asset Browser.");
        ImGui::End();
        return;
    }

    drawModelInfo();
    ImGui::Separator();
    drawViewport();

    if (m_animator && m_animator->getClipCount() > 0)
    {
        ImGui::Separator();
        drawAnimationControls(deltaTime);
    }

    ImGui::End();
}

// ============================================================================
// Model info header
// ============================================================================

void ModelViewerPanel::drawModelInfo()
{
    std::string filename = "Unknown";
    if (!m_modelPath.empty())
    {
        filename = std::filesystem::path(m_modelPath).filename().string();
    }

    ImGui::Text("%s", filename.c_str());
    ImGui::SameLine();

    glm::vec3 extent = m_modelBounds.max - m_modelBounds.min;
    ImGui::TextDisabled("| Tris: %d | Mats: %d | Bounds: %.1f x %.1f x %.1f",
                         m_totalTriangles,
                         static_cast<int>(m_model->getMaterialCount()),
                         static_cast<double>(extent.x),
                         static_cast<double>(extent.y),
                         static_cast<double>(extent.z));

    if (m_animator)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("| Clips: %d | Joints: %d",
                             m_animator->getClipCount(),
                             m_model->m_skeleton ? m_model->m_skeleton->getJointCount() : 0);
    }

    // Toolbar
    ImGui::Checkbox("Bounds", &m_showBounds);
    ImGui::SameLine();
    if (m_animator)
    {
        ImGui::Checkbox("Skeleton", &m_showSkeleton);
        ImGui::SameLine();
    }
    if (ImGui::SmallButton("Reset View"))
    {
        autoFitCamera();
    }

    // Drag-drop source for placing model into scene
    ImGui::SameLine();
    ImGui::Button("Drag to Place");
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
        ImGui::SetDragDropPayload("MODEL_PATH", m_modelPath.c_str(),
                                   m_modelPath.size() + 1);
        ImGui::Text("Place: %s", filename.c_str());
        ImGui::EndDragDropSource();
    }
}

// ============================================================================
// 3D Viewport
// ============================================================================

void ModelViewerPanel::drawViewport()
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float viewportH = avail.y;
    if (m_animator && m_animator->getClipCount() > 0)
    {
        viewportH -= 80.0f;  // Reserve space for animation controls
    }
    if (viewportH < 100.0f)
    {
        viewportH = avail.y * 0.6f;
    }
    float viewportW = avail.x;

    // Resize FBO if panel size changed
    int newW = std::max(static_cast<int>(viewportW), 64) & ~1;  // snap to 2px
    int newH = std::max(static_cast<int>(viewportH), 64) & ~1;
    if (m_fbo && (m_fboWidth != newW || m_fboHeight != newH))
    {
        m_fboWidth = newW;
        m_fboHeight = newH;
        m_fbo->resize(newW, newH);
        m_dirty = true;
    }

    // Render preview
    bool animating = m_animator && m_animator->isPlaying() && !m_animator->isPaused();
    if (m_dirty || animating)
    {
        renderPreview(animating ? ImGui::GetIO().DeltaTime : 0.0f);
        m_dirty = false;
    }

    // Display the preview texture
    ImVec2 canvasSize(viewportW, viewportH);

    GLuint texId = m_fbo ? m_fbo->getColorAttachmentId() : 0;
    if (texId != 0)
    {
        ImGui::Image(
            static_cast<ImTextureID>(static_cast<uintptr_t>(texId)),
            canvasSize, ImVec2(0, 1), ImVec2(1, 0));  // Flip Y for OpenGL
    }
    else
    {
        ImGui::InvisibleButton("##modelview", canvasSize);
    }

    bool isHovered = ImGui::IsItemHovered();

    // Handle orbit (left-drag)
    if (isHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        m_yaw += delta.x * ORBIT_SENSITIVITY;
        m_pitch += delta.y * ORBIT_SENSITIVITY;
        m_pitch = std::clamp(m_pitch, MIN_PITCH, MAX_PITCH);
        m_dirty = true;
    }

    // Handle pan (middle-drag)
    if (isHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        glm::mat4 view = computeViewMatrix();
        glm::vec3 right = glm::vec3(view[0][0], view[1][0], view[2][0]);
        glm::vec3 up = glm::vec3(view[0][1], view[1][1], view[2][1]);
        m_focusPoint -= right * delta.x * PAN_SENSITIVITY * m_distance;
        m_focusPoint += up * delta.y * PAN_SENSITIVITY * m_distance;
        m_dirty = true;
    }

    // Handle zoom (scroll)
    if (isHovered)
    {
        float scroll = ImGui::GetIO().MouseWheel;
        if (scroll != 0.0f)
        {
            m_distance *= (scroll > 0.0f) ? (1.0f - ZOOM_SENSITIVITY) :
                                             (1.0f + ZOOM_SENSITIVITY);
            m_distance = std::clamp(m_distance, m_minDistance, m_maxDistance);
            m_dirty = true;
        }
    }
}

// ============================================================================
// FBO Rendering
// ============================================================================

void ModelViewerPanel::renderPreview(float deltaTime)
{
    if (!m_model || !m_fbo || !m_initialized)
    {
        return;
    }

    // Update animation
    if (m_animator && deltaTime > 0.0f)
    {
        m_animator->setSpeed(m_playbackSpeed);
        m_animator->update(deltaTime);
    }

    // Save GL state
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    // Bind FBO
    m_fbo->bind();
    glViewport(0, 0, m_fboWidth, m_fboHeight);
    glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    float aspect = static_cast<float>(m_fboWidth) / static_cast<float>(m_fboHeight);
    glm::mat4 view = computeViewMatrix();
    glm::mat4 proj = computeProjectionMatrix(aspect);
    glm::vec3 cameraPos = computeCameraPosition();

    // Set up shader
    m_previewShader.use();
    m_previewShader.setMat4("u_view", view);
    m_previewShader.setMat4("u_projection", proj);
    m_previewShader.setVec3("u_viewPos", cameraPos);

    // Fixed light from upper-right
    glm::vec3 lightDir = glm::normalize(glm::vec3(-1.0f, -1.0f, -1.0f));
    m_previewShader.setVec3("u_lightDir", lightDir);

    // Render each primitive
    for (const auto& prim : m_model->m_primitives)
    {
        if (!prim.mesh)
        {
            continue;
        }

        // Model matrix (identity for now — primitives are in model space)
        m_previewShader.setMat4("u_model", glm::mat4(1.0f));

        // Set material properties
        if (prim.materialIndex >= 0 &&
            prim.materialIndex < static_cast<int>(m_model->m_materials.size()))
        {
            const auto& mat = m_model->m_materials[static_cast<size_t>(prim.materialIndex)];
            bool isPBR = (mat->getType() == MaterialType::PBR);
            m_previewShader.setBool("u_usePBR", isPBR);

            if (isPBR)
            {
                m_previewShader.setVec3("u_albedo", mat->getAlbedo());
                m_previewShader.setFloat("u_metallic", mat->getMetallic());
                m_previewShader.setFloat("u_roughness", mat->getRoughness());
                m_previewShader.setFloat("u_ao", mat->getAo());
            }
            else
            {
                m_previewShader.setVec3("u_diffuseColor", mat->getDiffuseColor());
                m_previewShader.setVec3("u_specularColor", mat->getSpecularColor());
                m_previewShader.setFloat("u_shininess", mat->getShininess());
            }

            // Bind albedo/diffuse texture
            bool hasAlbedoTex = mat->hasDiffuseTexture();
            m_previewShader.setBool("u_hasAlbedoTex", hasAlbedoTex);
            if (hasAlbedoTex)
            {
                mat->getDiffuseTexture()->bind(0);
            }
            else
            {
                m_defaultTexture->bind(0);
            }
            m_previewShader.setInt("u_albedoTex", 0);
        }
        else
        {
            // Default material
            m_previewShader.setBool("u_usePBR", true);
            m_previewShader.setVec3("u_albedo", glm::vec3(0.7f));
            m_previewShader.setFloat("u_metallic", 0.0f);
            m_previewShader.setFloat("u_roughness", 0.5f);
            m_previewShader.setFloat("u_ao", 1.0f);
            m_previewShader.setBool("u_hasAlbedoTex", false);
            m_defaultTexture->bind(0);
            m_previewShader.setInt("u_albedoTex", 0);
        }

        // Draw mesh
        prim.mesh->bind();
        glDrawElements(GL_TRIANGLES,
                        static_cast<GLsizei>(prim.mesh->getIndexCount()),
                        GL_UNSIGNED_INT, nullptr);
        prim.mesh->unbind();
    }

    // Draw bounding box wireframe
    glm::mat4 viewProj = proj * view;
    if (m_showBounds)
    {
        glm::vec3 mn = m_modelBounds.min;
        glm::vec3 mx = m_modelBounds.max;
        glm::vec3 c(0.4f, 0.8f, 0.4f);  // Green

        // 12 edges of the AABB
        DebugDraw::line({mn.x,mn.y,mn.z}, {mx.x,mn.y,mn.z}, c);
        DebugDraw::line({mx.x,mn.y,mn.z}, {mx.x,mx.y,mn.z}, c);
        DebugDraw::line({mx.x,mx.y,mn.z}, {mn.x,mx.y,mn.z}, c);
        DebugDraw::line({mn.x,mx.y,mn.z}, {mn.x,mn.y,mn.z}, c);

        DebugDraw::line({mn.x,mn.y,mx.z}, {mx.x,mn.y,mx.z}, c);
        DebugDraw::line({mx.x,mn.y,mx.z}, {mx.x,mx.y,mx.z}, c);
        DebugDraw::line({mx.x,mx.y,mx.z}, {mn.x,mx.y,mx.z}, c);
        DebugDraw::line({mn.x,mx.y,mx.z}, {mn.x,mn.y,mx.z}, c);

        DebugDraw::line({mn.x,mn.y,mn.z}, {mn.x,mn.y,mx.z}, c);
        DebugDraw::line({mx.x,mn.y,mn.z}, {mx.x,mn.y,mx.z}, c);
        DebugDraw::line({mx.x,mx.y,mn.z}, {mx.x,mx.y,mx.z}, c);
        DebugDraw::line({mn.x,mx.y,mn.z}, {mn.x,mx.y,mx.z}, c);
    }

    // Draw skeleton
    if (m_showSkeleton && m_animator && m_animator->hasBones())
    {
        const auto& skeleton = m_animator->getSkeleton();
        const auto& boneMatrices = m_animator->getBoneMatrices();
        int jointCount = skeleton->getJointCount();

        for (int i = 0; i < jointCount; ++i)
        {
            // Reconstruct global transform from bone matrix + inverse bind matrix
            // boneMatrix = globalTransform * inverseBindMatrix
            // We need globalTransform, so we'd need to invert inverseBindMatrix.
            // Simpler: access the global transforms directly if exposed, or
            // use the joint's bind pose for static display.
            // For animated display, the SkeletonAnimator stores m_globalTransforms
            // but they're private. Draw at bind pose positions as a fallback.

            // Use the joint bind transforms for now
            const auto& joint = skeleton->m_joints[static_cast<size_t>(i)];
            glm::vec3 jointPos = glm::vec3(0.0f);

            // Approximate: reconstruct from bone matrix × bindMatrix
            if (i < static_cast<int>(boneMatrices.size()))
            {
                glm::mat4 globalTransform = boneMatrices[static_cast<size_t>(i)] *
                                             glm::inverse(joint.inverseBindMatrix);
                jointPos = glm::vec3(globalTransform[3]);
            }

            // Draw joint sphere
            glm::vec3 color(1.0f, 0.3f, 0.3f);  // Red for joints
            if (joint.parentIndex < 0)
            {
                color = glm::vec3(0.3f, 0.3f, 1.0f);  // Blue for roots
            }
            float jointSize = m_distance * 0.003f;
            DebugDraw::wireSphere(jointPos, jointSize, color, 8);

            // Draw bone line to parent
            if (joint.parentIndex >= 0 &&
                joint.parentIndex < static_cast<int>(boneMatrices.size()))
            {
                const auto& parentJoint = skeleton->m_joints[static_cast<size_t>(joint.parentIndex)];
                glm::mat4 parentGlobal =
                    boneMatrices[static_cast<size_t>(joint.parentIndex)] *
                    glm::inverse(parentJoint.inverseBindMatrix);
                glm::vec3 parentPos = glm::vec3(parentGlobal[3]);
                DebugDraw::line(parentPos, jointPos, glm::vec3(0.8f, 0.8f, 0.2f));
            }
        }
    }

    // Flush debug draw into the preview FBO
    if (m_showBounds || m_showSkeleton)
    {
        // DebugDraw needs its own instance for flush — but we can use a temporary
        // The static line() calls queue vertices. We need a DebugDraw instance to flush.
        static DebugDraw previewDebugDraw;
        static bool debugInitialized = false;
        if (!debugInitialized)
        {
            debugInitialized = previewDebugDraw.initialize(m_assetPath);
        }
        if (debugInitialized)
        {
            previewDebugDraw.flush(viewProj);
        }
    }

    // Restore GL state
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1],
               prevViewport[2], prevViewport[3]);
}

// ============================================================================
// Animation controls
// ============================================================================

void ModelViewerPanel::drawAnimationControls(float /*deltaTime*/)
{
    if (!m_animator)
    {
        return;
    }

    ImGui::Text("Animation");

    // Clip selector
    int clipCount = m_animator->getClipCount();
    if (clipCount > 0)
    {
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::BeginCombo("##clip", m_selectedClip >= 0 ?
                               m_animator->getClip(m_selectedClip)->getName().c_str() :
                               "Select clip..."))
        {
            for (int i = 0; i < clipCount; ++i)
            {
                const auto& clip = m_animator->getClip(i);
                bool selected = (i == m_selectedClip);
                if (ImGui::Selectable(clip->getName().c_str(), selected))
                {
                    m_selectedClip = i;
                    m_animator->playIndex(i);
                    m_dirty = true;
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::SameLine();

    // Playback controls
    if (ImGui::SmallButton(m_animator->isPlaying() && !m_animator->isPaused() ?
                            "Pause" : "Play"))
    {
        if (m_animator->isPlaying())
        {
            m_animator->setPaused(!m_animator->isPaused());
        }
        else if (m_selectedClip >= 0)
        {
            m_animator->playIndex(m_selectedClip);
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Stop"))
    {
        m_animator->stop();
        m_dirty = true;
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // Loop toggle
    bool loop = m_animator->isLooping();
    if (ImGui::Checkbox("Loop", &loop))
    {
        m_animator->setLooping(loop);
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // Speed slider
    ImGui::Text("Speed:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    float speed = m_playbackSpeed;
    if (ImGui::SliderFloat("##speed", &speed, 0.0f, 5.0f, "%.1fx"))
    {
        setPlaybackSpeed(speed);
    }

    // Timeline (if a clip is active)
    if (m_animator->isPlaying() && m_animator->getActiveClipIndex() >= 0)
    {
        int activeIdx = m_animator->getActiveClipIndex();
        float duration = m_animator->getClip(activeIdx)->getDuration();
        float currentTime = m_animator->getCurrentTime();

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::ProgressBar(duration > 0.0f ? currentTime / duration : 0.0f,
                            ImVec2(0.0f, 0.0f),
                            (std::to_string(static_cast<float>(static_cast<int>(currentTime * 10)) / 10.0f) +
                             "s / " + std::to_string(static_cast<float>(static_cast<int>(duration * 10)) / 10.0f) +
                             "s").c_str());
    }
}

} // namespace Vestige
