// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file volumetric_fog_pass.h
/// @brief Phase 10 slice 11.6 — the volumetric fog GPU subsystem.
///
/// Owns the froxel 3D textures and drives the three compute passes
/// (inject → scatter → integrate) that produce the integrated
/// (inscatter, transmittance) volume the HDR composite samples. The froxel
/// coordinate math lives GL-free in `volumetric_fog.h`; this class is the
/// GL-resource owner that the design doc (§4.2) defers to "a later step of
/// slice 11.6".
///
/// Lifecycle mirrors the other compute subsystems (`DepthReducer`): construct,
/// `init()` once a GL context exists, `dispatch()` per frame, `destroy()`
/// before context teardown. Non-copyable (owns GL handles).
#pragma once

#include "renderer/shader.h"
#include "renderer/volumetric_fog.h"

#include <glad/gl.h>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Drives the three froxel compute passes and owns their textures.
class VolumetricFogPass
{
public:
    /// @brief Per-frame inputs for the inject + scatter passes.
    ///
    /// `scattering` (sigma_s) and `extinction` (sigma_t) are the uniform base
    /// medium; `noise` (11.8) modulates them per froxel; `volumes` (11.11) add
    /// localized tinted fog on top (capped at `MAX_FOG_VOLUMES`). The CSM block
    /// drives per-froxel sun shadowing; leave `csmCascadeCount` at 0 for the
    /// unshadowed sun lobe.
    struct FrameParams
    {
        glm::mat4 invProjection{1.0f};        ///< Clip → view, to place froxels.
        glm::vec3 sunDirViewSpace{0.0f, 1.0f, 0.0f}; ///< Normalised, toward the sun.
        glm::vec3 sunRadiance{1.0f};          ///< Linear HDR sun radiance.
        glm::vec3 ambient{0.0f};              ///< Ambient inscatter floor.
        glm::vec3 scattering{0.02f};          ///< sigma_s per channel, 1/m.
        float     extinction{0.02f};          ///< sigma_t, 1/m.
        float     anisotropy{0.0f};           ///< Henyey-Greenstein g.

        // Density-noise modulation (slice 11.8). `noise.enabled == false`
        // writes the uniform medium byte-for-byte (pre-11.8 equivalence).
        FogNoiseParams noise{};               ///< Per-froxel density noise.
        float          elapsedSeconds = 0.0f; ///< Wall-clock seconds for noise scroll.

        // Placeable mist / ground-fog volumes (slice 11.11). Beyond
        // MAX_FOG_VOLUMES the extras are dropped with a logged warning.
        std::vector<FogVolume> volumes{};     ///< Active fog volumes this frame.

        // Cascaded-shadow-map inputs for per-froxel sun shadowing (god rays).
        int       csmCascadeCount = 0;        ///< 0 = unshadowed sun lobe.
        std::array<float, 4>     csmCascadeSplits{};      ///< View-space depth splits.
        std::array<glm::mat4, 4> csmLightSpaceMatrices{}; ///< World → light clip, per cascade.
        glm::mat4 invView{1.0f};              ///< View → world, for the froxel shadow lookup.
        GLuint    csmShadowTexture = 0;       ///< Depth sampler2DArray; 0 → fallback (lit).
        float     csmDepthBias = 0.0015f;     ///< Constant shadow bias (empirical; matches
                                              ///  the scene pass's 0.0002–0.001 slope-bias floor).
    };

    /// @brief Per-frame inputs for the dynamic-GI inject pass (Slice R4,
    ///        Variant A — design §11.2). All matrices are this frame's except
    ///        the two `prev*` (last frame's), used for the reprojected history
    ///        read. `alpha`/`decay` are the EMA constants (both 0 in
    ///        reduce-motion → frozen cache). Textures are the att3 injection
    ///        source and the resolved scene depth (reversed-Z).
    struct GiFrameParams
    {
        glm::mat4 invProjection{1.0f};      ///< Current clip → view.
        glm::mat4 invView{1.0f};            ///< Current view → world.
        glm::mat4 prevViewProjection{1.0f}; ///< Previous world → clip.
        glm::mat4 prevView{1.0f};           ///< Previous world → view.
        GLuint    injectionSourceTex = 0;   ///< att3: albedo · direct-diffuse.
        GLuint    sceneDepthTex      = 0;   ///< Resolved scene depth texture.
        float     alpha = 0.1f;             ///< EMA blend weight (GI_ALPHA).
        float     decay = 0.05f;            ///< Confidence bleed (GI_DECAY).
    };

    VolumetricFogPass();
    ~VolumetricFogPass();

    VolumetricFogPass(const VolumetricFogPass&)            = delete;
    VolumetricFogPass& operator=(const VolumetricFogPass&) = delete;

    /// @brief Loads the three compute shaders and allocates the froxel
    ///        textures. @p shaderDir is the directory holding
    ///        `volumetric_{inject,scatter,integrate}.comp.glsl`.
    /// @return True on success; false (with a logged error) on shader-load
    ///         failure, leaving the object uninitialised.
    bool init(const std::string& shaderDir, const FroxelGridConfig& cfg = {});

    /// @brief Releases GL resources. Safe to call repeatedly / before teardown.
    void destroy();

    bool isInitialized() const { return m_initialized; }

    /// @brief Runs inject → scatter → integrate for this frame. No-op if not
    ///        initialised. Leaves the integrated volume in @ref integratedTexture.
    void dispatch(const FrameParams& params);

    /// @brief Runs the dynamic-GI inject compute (Slice R4): reads
    ///        m_giHistoryTex (reprojected), splats this frame's att3
    ///        injection source, writes the EMA-blended result to m_giTex.
    ///        No-op if uninitialised. The caller dispatches this in the same
    ///        pre-composite window as @ref dispatch (after the fog passes,
    ///        before the composite binds unit 0).
    void dispatchGi(const GiFrameParams& params);

    /// @brief Swap the GI ping-pong textures (Slice R4). Called end-of-frame,
    ///        like `Taa::swapBuffers`: the just-written `m_giTex` becomes next
    ///        frame's `m_giHistoryTex` (the read source). No-op if uninitialised.
    void swapGiBuffers();

    /// @brief The integrated froxel volume: RGBA16F, rgb = accumulated
    ///        inscatter, a = transmittance. 0 until `init()` succeeds.
    GLuint integratedTexture() const { return m_integratedTex; }

    /// @brief The GI cache the scene pass samples (Slice R4): RGBA16F 3D,
    ///        `.rgb` = accumulated indirect radiance, `.a` = confidence in [0,1].
    ///        This is the *previous frame's completed* cache (the inject reads it
    ///        as history and writes the other ping-pong texture); 0 until
    ///        `init()` succeeds. Always a valid texture once initialised, so the
    ///        scene shader's declared sampler stays complete even when GI is off.
    GLuint giReadTexture() const { return m_giHistoryTex; }

    const FroxelGridConfig& config() const { return m_cfg; }

private:
    void createTextures();

    Shader m_inject;
    Shader m_scatter;
    Shader m_integrate;
    Shader m_giInject;  ///< Dynamic-GI inject compute (Slice R4).

    GLuint m_volumeTex     = 0;  ///< Scratch: inject writes, scatter rewrites in place.
    GLuint m_integratedTex = 0;  ///< Final integrated volume the composite samples.
    GLuint m_fallbackShadowTex = 0;  ///< 1×1 lit depth array — bound when no CSM is supplied.
    GLuint m_volumeSsbo    = 0;  ///< std430 SSBO of up to MAX_FOG_VOLUMES packed volumes (11.11).
    int    m_lastOverCap   = -1; ///< Last logged over-cap count (-1 = none) — throttles the warning.

    // Dynamic-GI froxel cache (Slice R4, Variant A — design §11.2). Ping-pong
    // RGBA16F 3D textures co-located with the fog grid (same dims/filtering), so
    // the GI cache is addressed identically to the fog volume. m_giHistoryTex is
    // the previous frame's completed cache (read by the scene pass + the inject's
    // reprojected history fetch); m_giTex is the inject's write target. Swapped
    // end-of-frame (@ref swapGiBuffers). Both GL-cleared to 0 at allocation so
    // frame 0 reads cold everywhere (confidence 0). 0 until `init()` succeeds.
    GLuint m_giTex        = 0;  ///< Inject write target (this frame's cache).
    GLuint m_giHistoryTex = 0;  ///< Previous frame's completed cache (read source).

    FroxelGridConfig m_cfg{};
    bool m_initialized = false;
};

} // namespace Vestige
