/// @file water_renderer.cpp
/// @brief Water surface rendering implementation.
#include "renderer/water_renderer.h"
#include "core/logger.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>
#include <vector>

namespace Vestige
{

WaterRenderer::~WaterRenderer()
{
    shutdown();
}

bool WaterRenderer::init(const std::string& assetPath)
{
    // Load water shaders
    std::string vertPath = assetPath + "/shaders/water.vert.glsl";
    std::string fragPath = assetPath + "/shaders/water.frag.glsl";

    if (!m_waterShader.loadFromFiles(vertPath, fragPath))
    {
        Logger::error("Failed to load water shaders");
        return false;
    }

    generateDefaultNormalMap();
    generateDefaultDudvMap();
    generateDefaultFoamTexture();

    m_initialized = true;
    Logger::info("Water renderer initialized");
    return true;
}

void WaterRenderer::shutdown()
{
    if (m_defaultNormalMap != 0)
    {
        glDeleteTextures(1, &m_defaultNormalMap);
        m_defaultNormalMap = 0;
    }
    if (m_defaultDudvMap != 0)
    {
        glDeleteTextures(1, &m_defaultDudvMap);
        m_defaultDudvMap = 0;
    }
    if (m_defaultFoamTexture != 0)
    {
        glDeleteTextures(1, &m_defaultFoamTexture);
        m_defaultFoamTexture = 0;
    }
    m_waterShader.destroy();
    m_initialized = false;
}

void WaterRenderer::render(const std::vector<WaterRenderItem>& waterItems,
                           const Camera& camera,
                           float aspectRatio,
                           float time,
                           const glm::vec3& lightDir,
                           const glm::vec3& lightColor,
                           GLuint environmentCubemap,
                           GLuint reflectionTex,
                           GLuint refractionTex,
                           GLuint refractionDepthTex,
                           float cameraNear)
{
    if (!m_initialized || waterItems.empty())
    {
        return;
    }

    // Enable alpha blending for water transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);  // Don't write depth (water is transparent)

    m_waterShader.use();

    // Camera and matrices
    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 projection = camera.getProjectionMatrix(aspectRatio);

    m_waterShader.setMat4("u_view", view);
    m_waterShader.setMat4("u_projection", projection);
    m_waterShader.setVec3("u_cameraPos", camera.getPosition());
    m_waterShader.setFloat("u_time", time);
    m_waterShader.setFloat("u_cameraNear", cameraNear);

    // Directional light
    m_waterShader.setVec3("u_lightDirection", lightDir);
    m_waterShader.setVec3("u_lightColor", lightColor);

    // Quality tier
    m_waterShader.setInt("u_waterQualityTier", m_waterQualityTier);

    // Bind default normal map (texture unit 0)
    glBindTextureUnit(0, m_defaultNormalMap);
    m_waterShader.setInt("u_normalMap", 0);
    m_waterShader.setBool("u_hasNormalMap", m_defaultNormalMap != 0);

    // Bind default DuDv map (texture unit 1)
    glBindTextureUnit(1, m_defaultDudvMap);
    m_waterShader.setInt("u_dudvMap", 1);
    m_waterShader.setBool("u_hasDudvMap", m_defaultDudvMap != 0);

    // Bind environment cubemap (texture unit 2)
    if (environmentCubemap != 0)
    {
        glBindTextureUnit(2, environmentCubemap);
        m_waterShader.setBool("u_hasEnvironmentMap", true);
    }
    else
    {
        glBindTextureUnit(2, 0);
        m_waterShader.setBool("u_hasEnvironmentMap", false);
    }
    m_waterShader.setInt("u_environmentMap", 2);

    // Bind reflection texture (texture unit 3)
    bool hasReflection = (reflectionTex != 0);
    if (hasReflection)
    {
        glBindTextureUnit(3, reflectionTex);
    }
    m_waterShader.setInt("u_reflectionTex", 3);
    m_waterShader.setBool("u_hasReflectionTex", hasReflection);

    // Bind refraction texture + depth (texture units 4, 5)
    bool hasRefraction = (refractionTex != 0 && refractionDepthTex != 0);
    if (hasRefraction)
    {
        glBindTextureUnit(4, refractionTex);
        glBindTextureUnit(5, refractionDepthTex);
    }
    m_waterShader.setInt("u_refractionTex", 4);
    m_waterShader.setInt("u_refractionDepthTex", 5);
    m_waterShader.setBool("u_hasRefractionTex", hasRefraction);

    // Bind foam texture (texture unit 6)
    bool hasFoam = (m_defaultFoamTexture != 0);
    if (hasFoam)
    {
        glBindTextureUnit(6, m_defaultFoamTexture);
    }
    m_waterShader.setInt("u_foamTex", 6);
    m_waterShader.setBool("u_hasFoamTex", hasFoam);
    m_waterShader.setFloat("u_foamDistance", 0.5f);
    m_waterShader.setFloat("u_softEdgeDistance", 1.0f);

    // Draw each water surface
    for (const auto& item : waterItems)
    {
        const auto* water = item.component;
        const auto& config = water->getConfig();

        // Ensure mesh is built/up-to-date (mutable GPU cache)
        water->rebuildMeshIfNeeded();

        if (water->getVao() == 0 || water->getIndexCount() == 0)
        {
            continue;
        }

        // Model matrix
        m_waterShader.setMat4("u_model", item.worldMatrix);

        // Wave parameters
        m_waterShader.setInt("u_numWaves", config.numWaves);
        static const char* waveParamNames[WaterSurfaceConfig::MAX_WAVES] = {
            "u_waveParams[0]", "u_waveParams[1]", "u_waveParams[2]", "u_waveParams[3]"
        };
        for (int i = 0; i < config.numWaves && i < WaterSurfaceConfig::MAX_WAVES; ++i)
        {
            float dirRad = config.waves[i].direction * glm::pi<float>() / 180.0f;
            m_waterShader.setVec4(waveParamNames[i], glm::vec4(
                config.waves[i].amplitude,
                config.waves[i].wavelength,
                config.waves[i].speed,
                dirRad
            ));
        }

        // Water color and surface parameters
        m_waterShader.setVec4("u_shallowColor", config.shallowColor);
        m_waterShader.setVec4("u_deepColor", config.deepColor);
        m_waterShader.setFloat("u_dudvStrength", config.dudvStrength);
        m_waterShader.setFloat("u_normalStrength", config.normalStrength);
        m_waterShader.setFloat("u_flowSpeed", config.flowSpeed);
        m_waterShader.setFloat("u_specularPower", config.specularPower);

        // Draw the water mesh
        glBindVertexArray(water->getVao());
        glDrawElements(GL_TRIANGLES, water->getIndexCount(), GL_UNSIGNED_INT, nullptr);
    }

    // Restore state
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

// --- Tileable value noise for procedural water textures ---
namespace
{

/// Hash function for deterministic pseudo-random values at integer lattice points.
float hashGrid(int x, int y)
{
    // Use unsigned arithmetic — the overflows are intentional (integer hash function)
    unsigned int n = static_cast<unsigned int>(x + y * 57);
    n = (n << 13) ^ n;
    return (1.0f - static_cast<float>((n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffffu)
            / 1073741824.0f);
}

/// Smoothed value noise with bilinear interpolation and tileable wrapping.
float valueNoise(float x, float y, int wrap)
{
    int ix = static_cast<int>(std::floor(x)) % wrap;
    int iy = static_cast<int>(std::floor(y)) % wrap;
    if (ix < 0) ix += wrap;
    if (iy < 0) iy += wrap;

    float fx = x - std::floor(x);
    float fy = y - std::floor(y);

    // Smoothstep interpolation
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);

    int ix1 = (ix + 1) % wrap;
    int iy1 = (iy + 1) % wrap;

    float a = hashGrid(ix, iy);
    float b = hashGrid(ix1, iy);
    float c = hashGrid(ix, iy1);
    float d = hashGrid(ix1, iy1);

    return a + fx * (b - a) + fy * (c - a) + fx * fy * (a - b - c + d);
}

/// FBM (fractional Brownian motion) — 4 octaves of tileable value noise.
/// Creates organic, isotropic patterns with no directional bias.
float fbm(float x, float y, int baseWrap)
{
    float value = 0.0f;
    float amplitude = 0.5f;
    int wrap = baseWrap;

    for (int i = 0; i < 4; ++i)
    {
        value += amplitude * valueNoise(x, y, wrap);
        amplitude *= 0.5f;
        x *= 2.0f;
        y *= 2.0f;
        wrap *= 2;
    }
    return value;
}

} // anonymous namespace

void WaterRenderer::generateDefaultNormalMap()
{
    // Generate a tileable normal map from FBM noise height field.
    // The noise tiles every 8 grid cells — at 256 pixels, each cell = 32 pixels.
    constexpr int size = 256;
    constexpr int gridWrap = 8;   // Tiling period in noise grid cells
    constexpr float normalStr = 2.5f;  // Strength of normal perturbation

    // First generate a height field
    std::vector<float> heights(static_cast<size_t>(size * size));
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            float u = static_cast<float>(x) / static_cast<float>(size) * gridWrap;
            float v = static_cast<float>(y) / static_cast<float>(size) * gridWrap;
            heights[static_cast<size_t>(y * size + x)] = fbm(u, v, gridWrap);
        }
    }

    // Compute normals from height field via finite differences (wrapping for tileability)
    std::vector<unsigned char> pixels(static_cast<size_t>(size * size * 3));
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            int idx = (y * size + x) * 3;
            int xp = (x + 1) % size;
            int yp = (y + 1) % size;

            float h  = heights[static_cast<size_t>(y * size + x)];
            float hx = heights[static_cast<size_t>(y * size + xp)];
            float hy = heights[static_cast<size_t>(yp * size + x)];

            float nx = (h - hx) * normalStr;
            float nz = (h - hy) * normalStr;
            float ny = 1.0f;

            // Normalize
            float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= len;
            ny /= len;
            nz /= len;

            // Encode to [0, 255]
            pixels[static_cast<size_t>(idx + 0)] = static_cast<unsigned char>((nx * 0.5f + 0.5f) * 255.0f);
            pixels[static_cast<size_t>(idx + 1)] = static_cast<unsigned char>((ny * 0.5f + 0.5f) * 255.0f);
            pixels[static_cast<size_t>(idx + 2)] = static_cast<unsigned char>((nz * 0.5f + 0.5f) * 255.0f);
        }
    }

    glCreateTextures(GL_TEXTURE_2D, 1, &m_defaultNormalMap);
    GLsizei mipLevels = 1 + static_cast<GLsizei>(std::floor(std::log2(size)));
    glTextureStorage2D(m_defaultNormalMap, mipLevels, GL_RGB8, size, size);
    glTextureSubImage2D(m_defaultNormalMap, 0, 0, 0, size, size,
                        GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glTextureParameteri(m_defaultNormalMap, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(m_defaultNormalMap, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameteri(m_defaultNormalMap, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(m_defaultNormalMap, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateTextureMipmap(m_defaultNormalMap);
}

void WaterRenderer::generateDefaultDudvMap()
{
    // Generate a tileable DuDv distortion map from two independent FBM noise fields.
    // Uses a different grid offset (seed) from the normal map to avoid correlation.
    constexpr int size = 256;
    constexpr int gridWrap = 8;

    std::vector<unsigned char> pixels(static_cast<size_t>(size * size * 2));  // RG only

    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            int idx = (y * size + x) * 2;

            float u = static_cast<float>(x) / static_cast<float>(size) * gridWrap;
            float v = static_cast<float>(y) / static_cast<float>(size) * gridWrap;

            // Two independent noise fields offset in space to decorrelate
            float du = fbm(u + 5.2f, v + 1.3f, gridWrap) * 0.5f + 0.5f;
            float dv = fbm(u + 9.7f, v + 6.1f, gridWrap) * 0.5f + 0.5f;

            pixels[static_cast<size_t>(idx + 0)] = static_cast<unsigned char>(du * 255.0f);
            pixels[static_cast<size_t>(idx + 1)] = static_cast<unsigned char>(dv * 255.0f);
        }
    }

    glCreateTextures(GL_TEXTURE_2D, 1, &m_defaultDudvMap);
    GLsizei mipLevels = 1 + static_cast<GLsizei>(std::floor(std::log2(size)));
    glTextureStorage2D(m_defaultDudvMap, mipLevels, GL_RG8, size, size);
    glTextureSubImage2D(m_defaultDudvMap, 0, 0, 0, size, size,
                        GL_RG, GL_UNSIGNED_BYTE, pixels.data());
    glTextureParameteri(m_defaultDudvMap, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(m_defaultDudvMap, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameteri(m_defaultDudvMap, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(m_defaultDudvMap, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateTextureMipmap(m_defaultDudvMap);
}

void WaterRenderer::generateDefaultFoamTexture()
{
    // Procedural tileable foam texture — white noise clusters
    // that look like bubbly shore foam.
    constexpr int size = 256;
    std::vector<unsigned char> pixels(static_cast<size_t>(size * size));

    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            float u = static_cast<float>(x) / static_cast<float>(size);
            float v = static_cast<float>(y) / static_cast<float>(size);
            float pi2 = 2.0f * glm::pi<float>();

            // Overlapping bubbles: multiple sine patterns at different scales
            float f = 0.0f;
            f += std::sin(u * 12.0f * pi2 + v * 5.0f * pi2) * 0.5f + 0.5f;
            f *= std::sin(v * 9.0f * pi2 + u * 7.0f * pi2 + 0.8f) * 0.5f + 0.5f;
            f += (std::sin((u + v * 0.7f) * 18.0f * pi2 + 1.5f) * 0.5f + 0.5f) * 0.4f;
            f += (std::sin((u * 0.8f - v) * 14.0f * pi2 + 2.3f) * 0.5f + 0.5f) * 0.3f;

            // Threshold to create bubble clusters
            f = std::max(f - 0.4f, 0.0f) * 2.0f;
            f = std::min(f, 1.0f);

            pixels[static_cast<size_t>(y * size + x)] =
                static_cast<unsigned char>(f * 255.0f);
        }
    }

    glCreateTextures(GL_TEXTURE_2D, 1, &m_defaultFoamTexture);
    GLsizei mipLevels = 1 + static_cast<GLsizei>(std::floor(std::log2(size)));
    glTextureStorage2D(m_defaultFoamTexture, mipLevels, GL_R8, size, size);
    glTextureSubImage2D(m_defaultFoamTexture, 0, 0, 0, size, size,
                        GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    glTextureParameteri(m_defaultFoamTexture, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(m_defaultFoamTexture, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameteri(m_defaultFoamTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(m_defaultFoamTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateTextureMipmap(m_defaultFoamTexture);
}

} // namespace Vestige
