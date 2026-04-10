/// @file water_fbo.h
/// @brief Manages reflection and refraction FBOs for water rendering.
#pragma once

#include <glad/gl.h>

namespace Vestige
{

/// @brief Manages separate reflection and refraction render targets for water.
///
/// Reflection FBO: color + depth renderbuffer (half resolution).
/// Refraction FBO: color + depth texture (half resolution, depth sampled for
/// water thickness / Beer's law absorption).
class WaterFbo
{
public:
    WaterFbo() = default;
    ~WaterFbo();

    // Non-copyable
    WaterFbo(const WaterFbo&) = delete;
    WaterFbo& operator=(const WaterFbo&) = delete;

    WaterFbo(WaterFbo&& other) noexcept;
    WaterFbo& operator=(WaterFbo&& other) noexcept;

    /// @brief Creates reflection and refraction FBOs.
    /// @param reflW Reflection FBO width.
    /// @param reflH Reflection FBO height.
    /// @param refrW Refraction FBO width.
    /// @param refrH Refraction FBO height.
    /// @return True if both FBOs are complete.
    bool init(int reflW, int reflH, int refrW, int refrH);

    /// @brief Resizes both FBOs (destroys and recreates).
    void resize(int reflW, int reflH, int refrW, int refrH);

    /// @brief Releases all GPU resources.
    void shutdown();

    /// @brief Binds the reflection FBO and sets viewport.
    void bindReflection();

    /// @brief Binds the refraction FBO and sets viewport.
    void bindRefraction();

    /// @brief Unbinds FBO and restores the given viewport.
    void unbind(int viewportWidth, int viewportHeight);

    GLuint getReflectionTexture() const { return m_reflectionColorTex; }
    GLuint getRefractionTexture() const { return m_refractionColorTex; }
    GLuint getRefractionDepthTexture() const { return m_refractionDepthTex; }

    int getReflectionWidth() const { return m_reflectionWidth; }
    int getReflectionHeight() const { return m_reflectionHeight; }

private:
    void createReflectionFbo(int width, int height);
    void createRefractionFbo(int width, int height);
    void destroyReflectionFbo();
    void destroyRefractionFbo();

    // Reflection
    GLuint m_reflectionFbo = 0;
    GLuint m_reflectionColorTex = 0;
    GLuint m_reflectionDepthRbo = 0;
    int m_reflectionWidth = 0;
    int m_reflectionHeight = 0;

    // Refraction
    GLuint m_refractionFbo = 0;
    GLuint m_refractionColorTex = 0;
    GLuint m_refractionDepthTex = 0;
    int m_refractionWidth = 0;
    int m_refractionHeight = 0;
};

} // namespace Vestige
