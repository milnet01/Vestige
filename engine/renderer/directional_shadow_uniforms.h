// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file directional_shadow_uniforms.h
/// @brief 3D_E-0870 — the sun and cascade-shadow uniforms that the terrain,
///        foliage, grass and tree shaders share.
///
/// Each of those shaders declares the same uniforms (`u_lightDirection`,
/// `u_cascadeShadowMap`, `u_cascadeSplits[]`, ...). Setting them here keeps
/// the four renderers in step: the cascade cap and the Mesa fallback live
/// once.
#pragma once

namespace Vestige
{

class CascadedShadowMap;
class Shader;
struct DirectionalLight;

/// @brief The texture unit every shadow-receiving shader reads
///        `u_cascadeShadowMap` from.
inline constexpr int kCascadeShadowUnit = 3;

/// @brief How many cascades the shaders' `u_cascadeSplits[]` and
///        `u_cascadeLightSpaceMatrices[]` arrays hold.
inline constexpr int kMaxShaderCascades = 4;

/// @brief Sets `u_hasDirectionalLight` and, when @a light is not null, the
///        sun's direction, colour and ambient. Foliage, grass and trees use
///        it; terrain sets the same three vectors itself, with a default sun.
void setDirectionalLightUniforms(Shader& shader, const DirectionalLight* light);

/// @brief Sets `u_hasShadows` and the cascade uniforms from @a csm, or turns
///        shadows off when @a csm is null.
///
/// With no shadow map it still points `u_cascadeShadowMap` at
/// `kCascadeShadowUnit` and binds a 1x1 array there. The uniform is a
/// sampler2DArray; left unset it reads unit 0, which every one of these
/// shaders uses as a sampler2D, and Mesa fails the draw. The bound array keeps
/// the unit complete for drivers that also require that (sampler_fallback.h).
/// The caller passes null when the scene has no sun to cast the shadows.
void setCascadeShadowUniforms(Shader& shader, CascadedShadowMap* csm);

} // namespace Vestige
