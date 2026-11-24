// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_mirrored_winding_render.cpp
/// @brief 3D_E-1001 — a mirrored one-sided mesh draws its front and casts
///        its twin's shadow.
///
/// A model matrix with a negative determinant (a mirror, such as a scale of
/// -1 on one axis) reverses the order its triangles' corners reach the
/// screen in. The renderer culls back faces by that order, so unless it
/// flips which order counts as the front for such a draw, a mirrored
/// one-sided mesh is culled from the side it should show and drawn from
/// behind. The test draws a one-sided plane facing the camera and the sun,
/// once as is and once mirrored across x, through the real Renderer: the
/// scene pass (read back from a colour target) and the directional shadow
/// bake (read back from cascade 0's depth layer).
#include "core/event_bus.h"
#include "gl_test_fixture.h"
#include "renderer/camera.h"
#include "renderer/cascaded_shadow_map.h"
#include "renderer/material.h"
#include "renderer/mesh.h"
#include "renderer/renderer.h"
#include "scene/scene.h"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace Vestige;
using Vestige::Test::GLTestFixture;

namespace
{

constexpr int kRes = 32;

/// The assets directory: VESTIGE_SHADER_DIR is <source>/assets/shaders.
std::string assetDir()
{
    std::string shaders = VESTIGE_SHADER_DIR;
    return shaders.substr(0, shaders.rfind('/'));
}

const glm::mat4 kIdentity(1.0f);
const glm::mat4 kMirrorX = glm::scale(glm::mat4(1.0f), glm::vec3(-1.0f, 1.0f, 1.0f));

class MirroredWindingRenderTest : public GLTestFixture
{
protected:
    void SetUp() override
    {
        GLTestFixture::SetUp();
        if (IsSkipped())
        {
            return;
        }
        m_renderer = std::make_unique<Renderer>(m_bus);
        ASSERT_TRUE(m_renderer->loadShaders(assetDir()));
        // Larger than the test target: bloom needs room for its mip chain.
        m_renderer->initFramebuffers(256, 256, 1);

        // createPlane faces +y: its front is seen from above.
        m_plane = std::make_unique<Mesh>(Mesh::createPlane(1.0f));

        glCreateTextures(GL_TEXTURE_2D, 1, &m_colour);
        glTextureStorage2D(m_colour, 1, GL_RGBA16F, kRes, kRes);
        glCreateRenderbuffers(1, &m_depth);
        glNamedRenderbufferStorage(m_depth, GL_DEPTH_COMPONENT32F, kRes, kRes);
        glCreateFramebuffers(1, &m_fbo);
        glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT0, m_colour, 0);
        glNamedFramebufferRenderbuffer(m_fbo, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth);
        ASSERT_EQ(glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER),
                  static_cast<GLenum>(GL_FRAMEBUFFER_COMPLETE));
    }

    void TearDown() override
    {
        if (m_fbo != 0) glDeleteFramebuffers(1, &m_fbo);
        if (m_depth != 0) glDeleteRenderbuffers(1, &m_depth);
        if (m_colour != 0) glDeleteTextures(1, &m_colour);
        m_plane.reset();
        m_renderer.reset();
    }

    /// One plane with this model matrix, lit by a sun from almost overhead.
    /// The bounds are wider than the plane, so a shadow bake fitted to them
    /// leaves the plane in the middle of the layer with empty texels round it.
    SceneRenderData sceneWith(const glm::mat4* model) const
    {
        SceneRenderData data;
        data.hasDirectionalLight = true;
        data.directionalLight.direction = glm::normalize(glm::vec3(0.2f, -1.0f, 0.1f));
        if (model != nullptr)
        {
            SceneRenderData::RenderItem item{};
            item.mesh = m_plane.get();
            item.material = &m_material;
            item.worldMatrix = *model;
            item.worldBounds = AABB{glm::vec3(-4.0f, -1.0f, -4.0f), glm::vec3(4.0f, 1.0f, 4.0f)};
            item.entityId = 1;
            data.renderItems.push_back(item);
        }
        return data;
    }

    /// The centre pixel of the scene pass, seen from above and in front.
    glm::vec4 sceneCentre(const glm::mat4* model)
    {
        const SceneRenderData data = sceneWith(model);
        // Above and in front of the plane, looking down 45 degrees at its centre.
        Camera camera(glm::vec3(0.0f, 3.0f, 3.0f), -90.0f, -45.0f);

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glViewport(0, 0, kRes, kRes);
        const float clearColour[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        const float clearDepth = 0.0f;  // reverse-Z: far is 0
        glClearNamedFramebufferfv(m_fbo, GL_COLOR, 0, clearColour);
        glClearNamedFramebufferfv(m_fbo, GL_DEPTH, 0, &clearDepth);
        m_renderer->renderScene(data, camera, 1.0f, glm::vec4(0.0f), /*geometryOnly=*/true);

        glm::vec4 pixel(0.0f);
        glGetTextureSubImage(m_colour, 0, kRes / 2, kRes / 2, 0, 1, 1, 1,
                             GL_RGBA, GL_FLOAT, sizeof(pixel), &pixel[0]);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return pixel;
    }

    /// Cascade 0's depth layer after one normal frame. Cascade 0 is rebuilt
    /// every frame and covers the view nearest the camera, where the plane is.
    std::vector<float> shadowDepthAfterFrame(const glm::mat4* model)
    {
        const SceneRenderData data = sceneWith(model);
        Camera camera(glm::vec3(0.0f, 3.0f, 3.0f), -90.0f, -45.0f);
        m_renderer->renderScene(data, camera, 1.0f);

        const GLuint tex = m_renderer->getCascadedShadowMap()->depthTextureArray();
        GLint width = 0;
        GLint height = 0;
        glGetTextureLevelParameteriv(tex, 0, GL_TEXTURE_WIDTH, &width);
        glGetTextureLevelParameteriv(tex, 0, GL_TEXTURE_HEIGHT, &height);
        std::vector<float> depth(static_cast<size_t>(width) * static_cast<size_t>(height));
        glGetTextureSubImage(tex, 0, 0, 0, 0, width, height, 1, GL_DEPTH_COMPONENT, GL_FLOAT,
                             static_cast<GLsizei>(depth.size() * sizeof(float)), depth.data());
        return depth;
    }

    EventBus m_bus;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<Mesh> m_plane;
    Material m_material;
    GLuint m_colour = 0;
    GLuint m_depth = 0;
    GLuint m_fbo = 0;
};

float distance(const glm::vec4& a, const glm::vec4& b)
{
    return glm::length(glm::vec3(a) - glm::vec3(b));
}

} // namespace

TEST_F(MirroredWindingRenderTest, MirroredPlaneShowsItsFrontInTheScenePass)
{
    const glm::vec4 empty = sceneCentre(nullptr);
    const glm::vec4 plain = sceneCentre(&kIdentity);
    const glm::vec4 mirrored = sceneCentre(&kMirrorX);

    ASSERT_GT(distance(plain, empty), 0.05f)
        << "the unmirrored plane did not draw, so nothing was tested";
    EXPECT_LT(distance(mirrored, plain), 0.02f)
        << "mirrored centre (" << mirrored.r << ", " << mirrored.g << ", " << mirrored.b
        << ") differs from its twin (" << plain.r << ", " << plain.g << ", " << plain.b
        << "); empty scene is (" << empty.r << ", " << empty.g << ", " << empty.b << ")";
}

TEST_F(MirroredWindingRenderTest, MirroredPlaneCastsItsTwinsShadow)
{
    const std::vector<float> empty = shadowDepthAfterFrame(nullptr);
    const std::vector<float> plain = shadowDepthAfterFrame(&kIdentity);
    const std::vector<float> mirrored = shadowDepthAfterFrame(&kMirrorX);
    ASSERT_FALSE(empty.empty());
    ASSERT_EQ(plain.size(), empty.size());
    ASSERT_EQ(mirrored.size(), empty.size());

    // Texels the plane wrote: those that differ from the empty frame's layer.
    const auto written = [&empty](const std::vector<float>& layer)
    {
        int count = 0;
        for (size_t i = 0; i < layer.size(); ++i)
        {
            if (std::abs(layer[i] - empty[i]) > 1e-4f) ++count;
        }
        return count;
    };
    const int plainCount = written(plain);
    const int mirroredCount = written(mirrored);
    ASSERT_GT(plainCount, 0) << "the unmirrored plane wrote no shadow depth, so nothing was tested";
    // A mirror across x maps the plane onto itself, so the same texels are covered;
    // allow a few for rasterising the mirrored triangles' edges.
    EXPECT_NEAR(mirroredCount, plainCount, plainCount / 50 + 2)
        << "the mirrored plane wrote " << mirroredCount << " shadow texels; its twin "
        << plainCount << " of " << empty.size();
}
