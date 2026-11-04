// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file directional_shadow_uniforms.cpp
/// @brief 3D_E-0870 — shared sun and cascade-shadow uniforms.
#include "renderer/directional_shadow_uniforms.h"

#include "renderer/cascaded_shadow_map.h"
#include "renderer/light.h"
#include "renderer/sampler_fallback.h"
#include "renderer/shader.h"

#include <algorithm>
#include <array>

namespace Vestige
{

void setDirectionalLightUniforms(Shader& shader, const DirectionalLight* light)
{
    shader.setBool("u_hasDirectionalLight", light != nullptr);
    if (light != nullptr)
    {
        shader.setVec3("u_lightDirection", light->direction);
        shader.setVec3("u_lightColor", light->diffuse);
        shader.setVec3("u_ambientColor", light->ambient);
    }
}

void setCascadeShadowUniforms(Shader& shader, CascadedShadowMap* csm)
{
    shader.setBool("u_hasShadows", csm != nullptr);
    shader.setInt("u_cascadeShadowMap", kCascadeShadowUnit);
    if (csm == nullptr)
    {
        glBindTextureUnit(kCascadeShadowUnit, sharedSamplerFallback().getSampler2DArray());
        shader.setInt("u_cascadeCount", 0);
        return;
    }

    static constexpr std::array<const char*, kMaxShaderCascades> kSplitNames = {
        "u_cascadeSplits[0]", "u_cascadeSplits[1]",
        "u_cascadeSplits[2]", "u_cascadeSplits[3]",
    };
    static constexpr std::array<const char*, kMaxShaderCascades> kMatrixNames = {
        "u_cascadeLightSpaceMatrices[0]", "u_cascadeLightSpaceMatrices[1]",
        "u_cascadeLightSpaceMatrices[2]", "u_cascadeLightSpaceMatrices[3]",
    };

    csm->bindShadowTexture(kCascadeShadowUnit);
    const int cascadeCount = csm->getCascadeCount();
    shader.setInt("u_cascadeCount", cascadeCount);
    const int uploaded = std::min(cascadeCount, kMaxShaderCascades);
    for (int i = 0; i < uploaded; ++i)
    {
        const auto slot = static_cast<std::size_t>(i);
        shader.setFloat(kSplitNames[slot], csm->getCascadeSplit(i));
        shader.setMat4(kMatrixNames[slot], csm->getLightSpaceMatrix(i));
    }
}

} // namespace Vestige
