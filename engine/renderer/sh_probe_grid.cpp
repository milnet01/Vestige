/// @file sh_probe_grid.cpp
/// @brief SH probe grid implementation — cubemap projection, convolution, GPU upload.
#include "renderer/sh_probe_grid.h"
#include "core/logger.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace Vestige
{

// SH L2 basis function constants
static constexpr float SH_C00 = 0.282095f;    // 1 / (2*sqrt(pi))
static constexpr float SH_C1  = 0.488603f;    // sqrt(3) / (2*sqrt(pi))
static constexpr float SH_C20 = 0.315392f;    // sqrt(5) / (4*sqrt(pi))
static constexpr float SH_C21 = 1.092548f;    // sqrt(15) / (2*sqrt(pi))
static constexpr float SH_C22 = 0.546274f;    // sqrt(15) / (4*sqrt(pi))

// Cosine lobe convolution constants (Ramamoorthi & Hanrahan 2001)
static constexpr float COSINE_A0 = 3.141593f;  // pi
static constexpr float COSINE_A1 = 2.094395f;  // 2*pi/3
static constexpr float COSINE_A2 = 0.785398f;  // pi/4

static constexpr float PI = 3.14159265358979323846f;

/// @brief Evaluate 9 L2 SH basis functions for a unit direction.
static void evaluateSHBasis(const glm::vec3& dir, float basis[9])
{
    basis[0] = SH_C00;
    basis[1] = SH_C1 * dir.y;
    basis[2] = SH_C1 * dir.z;
    basis[3] = SH_C1 * dir.x;
    basis[4] = SH_C21 * dir.x * dir.y;
    basis[5] = SH_C21 * dir.y * dir.z;
    basis[6] = SH_C20 * (3.0f * dir.z * dir.z - 1.0f);
    basis[7] = SH_C21 * dir.x * dir.z;
    basis[8] = SH_C22 * (dir.x * dir.x - dir.y * dir.y);
}

/// @brief Convert cubemap face + texel (u,v in [-1,1]) to unit direction.
/// Uses OpenGL cubemap face convention (spec table 8.19).
static glm::vec3 faceToDirection(int face, float u, float v)
{
    // OpenGL cubemap: sc/tc mapping (face u→sc, face v→tc inverted for image rows)
    switch (face)
    {
        case 0: return glm::normalize(glm::vec3( 1.0f,   -v,   -u)); // +X: sc=-z, tc=-y
        case 1: return glm::normalize(glm::vec3(-1.0f,   -v,    u)); // -X: sc=+z, tc=-y
        case 2: return glm::normalize(glm::vec3(    u, 1.0f,    v)); // +Y: sc=+x, tc=+z
        case 3: return glm::normalize(glm::vec3(    u, -1.0f,  -v)); // -Y: sc=+x, tc=-z
        case 4: return glm::normalize(glm::vec3(    u,   -v, 1.0f)); // +Z: sc=+x, tc=-y
        case 5: return glm::normalize(glm::vec3(   -u,   -v, -1.0f)); // -Z: sc=-x, tc=-y
        default: return glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

SHProbeGrid::SHProbeGrid() = default;

SHProbeGrid::~SHProbeGrid()
{
    for (int i = 0; i < SH_TEXTURE_COUNT; i++)
    {
        if (m_textures[i] != 0)
        {
            glDeleteTextures(1, &m_textures[i]);
        }
    }
}

bool SHProbeGrid::initialize(const SHGridConfig& config)
{
    m_config = config;

    int totalProbes = config.resolution.x * config.resolution.y * config.resolution.z;
    m_probeData.resize(totalProbes * SH_COEFF_COUNT, glm::vec3(0.0f));

    Logger::info("SH probe grid initialized: "
        + std::to_string(config.resolution.x) + "x"
        + std::to_string(config.resolution.y) + "x"
        + std::to_string(config.resolution.z) + " = "
        + std::to_string(totalProbes) + " probes");
    return true;
}

int SHProbeGrid::probeIndex(int x, int y, int z) const
{
    return (z * m_config.resolution.y + y) * m_config.resolution.x + x;
}

void SHProbeGrid::setProbeIrradiance(int x, int y, int z, const glm::vec3 coeffs[9])
{
    int idx = probeIndex(x, y, z) * SH_COEFF_COUNT;
    for (int i = 0; i < SH_COEFF_COUNT; i++)
    {
        m_probeData[idx + i] = coeffs[i];
    }
}

void SHProbeGrid::getProbeIrradiance(int x, int y, int z, glm::vec3 coeffs[9]) const
{
    int idx = probeIndex(x, y, z) * SH_COEFF_COUNT;
    for (int i = 0; i < SH_COEFF_COUNT; i++)
    {
        coeffs[i] = m_probeData[idx + i];
    }
}

void SHProbeGrid::projectCubemapToSH(const float* cubemapData, int faceSize,
                                       glm::vec3 outCoeffs[9])
{
    for (int i = 0; i < 9; i++)
    {
        outCoeffs[i] = glm::vec3(0.0f);
    }

    float weightSum = 0.0f;
    float pixelSize = 2.0f / static_cast<float>(faceSize);

    for (int face = 0; face < 6; face++)
    {
        const float* faceData = cubemapData + face * faceSize * faceSize * 3;

        for (int ty = 0; ty < faceSize; ty++)
        {
            for (int tx = 0; tx < faceSize; tx++)
            {
                // Texel center in [-1, 1] face coordinates
                float u = -1.0f + (static_cast<float>(tx) + 0.5f) * pixelSize;
                float v = -1.0f + (static_cast<float>(ty) + 0.5f) * pixelSize;

                // Unit direction for this texel
                glm::vec3 dir = faceToDirection(face, u, v);

                // Solid angle weight (accounts for cubemap projection distortion)
                float tmp = 1.0f + u * u + v * v;
                float weight = 4.0f / (std::sqrt(tmp) * tmp);

                // Sample color
                int pixelIdx = (ty * faceSize + tx) * 3;
                glm::vec3 color(faceData[pixelIdx], faceData[pixelIdx + 1],
                                faceData[pixelIdx + 2]);

                // Clamp to prevent extreme HDR values
                color = glm::clamp(color, glm::vec3(0.0f), glm::vec3(50.0f));

                // Evaluate SH basis and accumulate
                float basis[9];
                evaluateSHBasis(dir, basis);

                for (int i = 0; i < 9; i++)
                {
                    outCoeffs[i] += color * basis[i] * weight;
                }

                weightSum += weight;
            }
        }
    }

    // Normalize by sphere surface area / accumulated weight
    float normFactor = (4.0f * PI) / weightSum;
    for (int i = 0; i < 9; i++)
    {
        outCoeffs[i] *= normFactor;
    }
}

void SHProbeGrid::convolveRadianceToIrradiance(glm::vec3 coeffs[9])
{
    // Band 0 (1 coefficient)
    coeffs[0] *= COSINE_A0;

    // Band 1 (3 coefficients)
    coeffs[1] *= COSINE_A1;
    coeffs[2] *= COSINE_A1;
    coeffs[3] *= COSINE_A1;

    // Band 2 (5 coefficients)
    coeffs[4] *= COSINE_A2;
    coeffs[5] *= COSINE_A2;
    coeffs[6] *= COSINE_A2;
    coeffs[7] *= COSINE_A2;
    coeffs[8] *= COSINE_A2;
}

void SHProbeGrid::upload()
{
    // Delete old textures
    for (int i = 0; i < SH_TEXTURE_COUNT; i++)
    {
        if (m_textures[i] != 0)
        {
            glDeleteTextures(1, &m_textures[i]);
            m_textures[i] = 0;
        }
    }

    int rx = m_config.resolution.x;
    int ry = m_config.resolution.y;
    int rz = m_config.resolution.z;
    int totalProbes = rx * ry * rz;

    // Create 7 RGBA16F 3D textures
    for (int t = 0; t < SH_TEXTURE_COUNT; t++)
    {
        glCreateTextures(GL_TEXTURE_3D, 1, &m_textures[t]);
        glTextureStorage3D(m_textures[t], 1, GL_RGBA16F, rx, ry, rz);
        glTextureParameteri(m_textures[t], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(m_textures[t], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(m_textures[t], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_textures[t], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_textures[t], GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }

    // Pack SH data into 7 RGBA textures
    // Layout: 27 channels (9 coeffs × 3 RGB) → 7 vec4s (28 channels, 1 unused)
    //
    // tex0: L[0].r, L[0].g, L[0].b, L[1].r
    // tex1: L[1].g, L[1].b, L[2].r, L[2].g
    // tex2: L[2].b, L[3].r, L[3].g, L[3].b
    // tex3: L[4].r, L[4].g, L[4].b, L[5].r
    // tex4: L[5].g, L[5].b, L[6].r, L[6].g
    // tex5: L[6].b, L[7].r, L[7].g, L[7].b
    // tex6: L[8].r, L[8].g, L[8].b, 0.0

    std::vector<float> texData(totalProbes * 4); // 4 floats per texel

    for (int t = 0; t < SH_TEXTURE_COUNT; t++)
    {
        for (int p = 0; p < totalProbes; p++)
        {
            int baseCoeff = t * 4;  // Which channel offset (0, 4, 8, 12, 16, 20, 24)
            float rgba[4] = {0.0f, 0.0f, 0.0f, 0.0f};

            // Map 4 channels from the 27-channel SH data
            for (int c = 0; c < 4; c++)
            {
                int globalChannel = baseCoeff + c;
                if (globalChannel >= 27) break;  // tex6 channel 3 is unused

                int coeffIdx = globalChannel / 3;  // Which SH coefficient (0-8)
                int colorIdx = globalChannel % 3;  // Which color channel (R=0, G=1, B=2)

                const glm::vec3& val = m_probeData[p * SH_COEFF_COUNT + coeffIdx];
                rgba[c] = val[colorIdx];
            }

            texData[p * 4 + 0] = rgba[0];
            texData[p * 4 + 1] = rgba[1];
            texData[p * 4 + 2] = rgba[2];
            texData[p * 4 + 3] = rgba[3];
        }

        glTextureSubImage3D(m_textures[t], 0, 0, 0, 0, rx, ry, rz,
                            GL_RGBA, GL_FLOAT, texData.data());
    }

    m_ready = true;
    Logger::info("SH probe grid uploaded: " + std::to_string(totalProbes)
        + " probes in " + std::to_string(SH_TEXTURE_COUNT) + " RGBA16F 3D textures");
}

void SHProbeGrid::bind() const
{
    for (int i = 0; i < SH_TEXTURE_COUNT; i++)
    {
        glBindTextureUnit(FIRST_TEXTURE_UNIT + i, m_textures[i]);
    }
}

bool SHProbeGrid::isReady() const
{
    return m_ready;
}

bool SHProbeGrid::isInitialized() const
{
    return !m_probeData.empty();
}

int SHProbeGrid::getProbeCount() const
{
    return m_config.resolution.x * m_config.resolution.y * m_config.resolution.z;
}

} // namespace Vestige
