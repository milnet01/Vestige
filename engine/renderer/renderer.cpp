// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file renderer.cpp
/// @brief Renderer implementation with Blinn-Phong/PBR lighting, shadows, and FBO pipeline.
#include "renderer/renderer.h"
#include "renderer/dynamic_mesh.h"
#include "renderer/foliage_renderer.h"
#include "renderer/motion_overlay_prev_world.h"
#include "renderer/normal_matrix.h"
#include "renderer/sampler_fallback.h"
#include "renderer/scoped_forward_z.h"
#include "renderer/scoped_shadow_depth_state.h"
#include "environment/foliage_manager.h"
#include "scene/scene.h"
#include "core/logger.h"
#include "utils/frustum.h"

#include <glad/gl.h>

#include <algorithm>
#include <unordered_set>
#include <array>
#include <random>

namespace Vestige
{

#ifndef NDEBUG
/// @brief OpenGL debug message callback — routes driver messages to the engine logger.
static void GLAPIENTRY glDebugCallback(
    GLenum source, GLenum type, GLuint id, GLenum severity,
    GLsizei /*length*/, const GLchar* message, const void* /*userParam*/)
{
    // Skip insignificant notifications (buffer info, shader recompile hints)
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
    {
        return;
    }

    const char* sourceStr = "Unknown";
    switch (source)
    {
        case GL_DEBUG_SOURCE_API:             sourceStr = "API"; break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   sourceStr = "Window"; break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER: sourceStr = "Shader"; break;
        case GL_DEBUG_SOURCE_THIRD_PARTY:     sourceStr = "3rdParty"; break;
        case GL_DEBUG_SOURCE_APPLICATION:     sourceStr = "App"; break;
        case GL_DEBUG_SOURCE_OTHER:           sourceStr = "Other"; break;
    }

    const char* typeStr = "Unknown";
    switch (type)
    {
        case GL_DEBUG_TYPE_ERROR:               typeStr = "Error"; break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: typeStr = "Deprecated"; break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  typeStr = "UB"; break;
        case GL_DEBUG_TYPE_PORTABILITY:         typeStr = "Portability"; break;
        case GL_DEBUG_TYPE_PERFORMANCE:         typeStr = "Performance"; break;
        case GL_DEBUG_TYPE_MARKER:              typeStr = "Marker"; break;
        case GL_DEBUG_TYPE_OTHER:               typeStr = "Other"; break;
    }

    std::string msg = "[GL " + std::string(sourceStr) + "/" + typeStr
                    + " #" + std::to_string(id) + "] " + message;

    switch (severity)
    {
        case GL_DEBUG_SEVERITY_HIGH:
            Logger::error(msg);
            break;
        case GL_DEBUG_SEVERITY_MEDIUM:
            Logger::warning(msg);
            break;
        case GL_DEBUG_SEVERITY_LOW:
            Logger::debug(msg);
            break;
        default:
            Logger::debug(msg);
            break;
    }
}
#endif // NDEBUG

// Frustum plane extraction and AABB-vs-frustum testing moved to utils/frustum.h

Renderer::Renderer(EventBus& eventBus)
    : m_eventBus(eventBus)
    , m_hasDirectionalLight(true)
    , m_isWireframe(false)
{
    // Enable OpenGL debug output (debug builds only — zero cost in release)
#ifndef NDEBUG
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(glDebugCallback, nullptr);
    // Suppress notifications — they are too noisy (buffer allocation hints etc.)
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION,
                          0, nullptr, GL_FALSE);
    Logger::debug("OpenGL debug output enabled");
#endif

    // Enable depth testing — closer objects obscure farther ones
    glEnable(GL_DEPTH_TEST);

    // Reverse-Z: map clip-space Z to [0, 1] instead of [-1, 1], and use
    // GEQUAL depth test so near=1.0, far=0.0. This distributes floating-point
    // precision evenly across the entire depth range, eliminating Z-fighting.
    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    glDepthFunc(GL_GEQUAL);
    glClearDepth(0.0);

    // Enable back-face culling — skip drawing the back side of triangles
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // Default clear color (dark grey)
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);

    // Subscribe to window resize events. Store the subscription token so
    // the dtor can tear it down — otherwise the lambda's captured ``this``
    // becomes a dangling reference when ``~Renderer`` runs before
    // ``~EventBus`` (engine.h declaration order). (AUDIT M9.)
    m_windowResizeSubscription = m_eventBus.subscribe<WindowResizeEvent>(
        [this](const WindowResizeEvent& event)
        {
            onWindowResize(event.width, event.height);
        });

    // PMR arena initialized in-class (m_frameResource)

    // Dummy SSBO for model matrices (binding point 0) — Mesa requires all declared
    // SSBOs to have valid buffers bound, even when the MDI code path is not taken.
    glCreateBuffers(1, &m_dummyModelSSBO);
    glNamedBufferStorage(m_dummyModelSSBO,
        static_cast<GLsizeiptr>(sizeof(glm::mat4)),
        nullptr, 0);

    // Fallback textures — Mesa requires ALL declared samplers to have valid textures
    // bound at draw time, even when the shader code path doesn't sample them.
    {
        unsigned char white[] = {255, 255, 255, 255};
        unsigned char black[] = {0, 0, 0, 255};

        // 1x1 white 2D texture (fallback for sampler2D)
        glCreateTextures(GL_TEXTURE_2D, 1, &m_fallbackTexture);
        glTextureStorage2D(m_fallbackTexture, 1, GL_RGBA8, 1, 1);
        glTextureSubImage2D(m_fallbackTexture, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white);

        // 1x1 black cubemap (fallback for samplerCube)
        glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &m_fallbackCubemap);
        glTextureStorage2D(m_fallbackCubemap, 1, GL_RGBA8, 1, 1);
        for (int face = 0; face < 6; face++)
        {
            glTextureSubImage3D(m_fallbackCubemap, 0, 0, 0, face, 1, 1, 1,
                                GL_RGBA, GL_UNSIGNED_BYTE, black);
        }

        // 1x1x1 3D texture (fallback for sampler3D — SH probe grid)
        glCreateTextures(GL_TEXTURE_3D, 1, &m_fallbackTex3D);
        glTextureStorage3D(m_fallbackTex3D, 1, GL_RGBA16F, 1, 1, 1);
        float black4f[] = {0.0f, 0.0f, 0.0f, 0.0f};
        glTextureSubImage3D(m_fallbackTex3D, 0, 0, 0, 0, 1, 1, 1,
                            GL_RGBA, GL_FLOAT, black4f);

        // 1x1x1 2D array texture (fallback for sampler2DArray)
        glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &m_fallbackTexArray);
        glTextureStorage3D(m_fallbackTexArray, 1, GL_RGBA8, 1, 1, 1);
        glTextureSubImage3D(m_fallbackTexArray, 0, 0, 0, 0, 1, 1, 1,
                            GL_RGBA, GL_UNSIGNED_BYTE, white);
    }

    // Create bone matrix SSBO for skeletal animation (binding point 2)
    glCreateBuffers(1, &m_boneMatrixSSBO);
    glNamedBufferStorage(m_boneMatrixSSBO,
        static_cast<GLsizeiptr>(MAX_BONES * sizeof(glm::mat4)),
        nullptr, GL_DYNAMIC_STORAGE_BIT);

    // Dummy morph target SSBO (binding point 3) — Mesa requires all declared
    // SSBOs to have valid buffers bound, even when no morph targets are active.
    glCreateBuffers(1, &m_dummyMorphSSBO);
    glNamedBufferStorage(m_dummyMorphSSBO,
        static_cast<GLsizeiptr>(sizeof(glm::vec4)),
        nullptr, 0);

    Logger::info("Renderer initialized (OpenGL 4.5, reverse-Z)");
}

void Renderer::resetFrameAllocator()
{
    m_frameResource.release();
}

Renderer::~Renderer()
{
    // Drop the EventBus subscription before any GL teardown — otherwise
    // a WindowResizeEvent published during shutdown would call a member
    // on half-destroyed state. (AUDIT M9.)
    if (m_windowResizeSubscription != 0)
    {
        m_eventBus.unsubscribe(m_windowResizeSubscription);
        m_windowResizeSubscription = 0;
    }

    if (m_ssaoNoiseTexture != 0)
    {
        glDeleteTextures(1, &m_ssaoNoiseTexture);
    }
    if (m_bloomTexture != 0)
    {
        glDeleteTextures(1, &m_bloomTexture);
    }
    if (m_bloomFbo != 0)
    {
        glDeleteFramebuffers(1, &m_bloomFbo);
    }
    if (m_luminanceTexture != 0)
    {
        glDeleteTextures(1, &m_luminanceTexture);
    }
    if (m_luminancePbo[0] != 0)
    {
        glDeleteBuffers(2, m_luminancePbo);
    }
    if (m_outlineStencilRbo != 0)
    {
        glDeleteRenderbuffers(1, &m_outlineStencilRbo);
    }
    if (m_causticsTexture != 0)
    {
        glDeleteTextures(1, &m_causticsTexture);
    }
    if (m_dummyModelSSBO != 0)
    {
        glDeleteBuffers(1, &m_dummyModelSSBO);
    }
    if (m_fallbackTexture != 0) glDeleteTextures(1, &m_fallbackTexture);
    if (m_fallbackCubemap != 0) glDeleteTextures(1, &m_fallbackCubemap);
    if (m_fallbackTexArray != 0) glDeleteTextures(1, &m_fallbackTexArray);
    if (m_fallbackTex3D != 0) glDeleteTextures(1, &m_fallbackTex3D);
    if (m_boneMatrixSSBO != 0)
    {
        glDeleteBuffers(1, &m_boneMatrixSSBO);
    }
    if (m_dummyMorphSSBO != 0)
    {
        glDeleteBuffers(1, &m_dummyMorphSSBO);
    }
    Logger::debug("Renderer destroyed");
}

bool Renderer::loadShaders(const std::string& assetPath)
{
    m_assetPath = assetPath;

    // Load scene shader (handles both Blinn-Phong and PBR)
    std::string vertPath = assetPath + "/shaders/scene.vert.glsl";
    std::string fragPath = assetPath + "/shaders/scene.frag.glsl";

    if (!m_sceneShader.loadFromFiles(vertPath, fragPath))
    {
        Logger::error("Failed to load scene shaders");
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

    // Load skybox shader
    std::string skyboxVertPath = assetPath + "/shaders/skybox.vert.glsl";
    std::string skyboxFragPath = assetPath + "/shaders/skybox.frag.glsl";

    if (!m_skyboxShader.loadFromFiles(skyboxVertPath, skyboxFragPath))
    {
        Logger::error("Failed to load skybox shaders");
        return false;
    }

    // Load point shadow depth shader
    std::string pointShadowVertPath = assetPath + "/shaders/point_shadow_depth.vert.glsl";
    std::string pointShadowFragPath = assetPath + "/shaders/point_shadow_depth.frag.glsl";

    if (!m_pointShadowDepthShader.loadFromFiles(pointShadowVertPath, pointShadowFragPath))
    {
        Logger::error("Failed to load point shadow depth shaders");
        return false;
    }

    // Load mip-chain bloom shaders (reuse screen_quad.vert.glsl)
    std::string bloomDownFragPath = assetPath + "/shaders/bloom_downsample.frag.glsl";
    if (!m_bloomDownsampleShader.loadFromFiles(screenVertPath, bloomDownFragPath))
    {
        Logger::error("Failed to load bloom downsample shader");
        return false;
    }

    std::string bloomUpFragPath = assetPath + "/shaders/bloom_upsample.frag.glsl";
    if (!m_bloomUpsampleShader.loadFromFiles(screenVertPath, bloomUpFragPath))
    {
        Logger::error("Failed to load bloom upsample shader");
        return false;
    }

    // Load SSAO shaders (reuse screen_quad.vert.glsl)
    std::string ssaoFragPath = assetPath + "/shaders/ssao.frag.glsl";
    if (!m_ssaoShader.loadFromFiles(screenVertPath, ssaoFragPath))
    {
        Logger::error("Failed to load SSAO shader");
        return false;
    }

    std::string ssaoBlurFragPath = assetPath + "/shaders/ssao_blur.frag.glsl";
    if (!m_ssaoBlurShader.loadFromFiles(screenVertPath, ssaoBlurFragPath))
    {
        Logger::error("Failed to load SSAO blur shader");
        return false;
    }

    // Load TAA shaders (reuse screen_quad.vert.glsl)
    std::string taaResolveFragPath = assetPath + "/shaders/taa_resolve.frag.glsl";
    if (!m_taaResolveShader.loadFromFiles(screenVertPath, taaResolveFragPath))
    {
        Logger::error("Failed to load TAA resolve shader");
        return false;
    }

    std::string motionVectorFragPath = assetPath + "/shaders/motion_vectors.frag.glsl";
    if (!m_motionVectorShader.loadFromFiles(screenVertPath, motionVectorFragPath))
    {
        Logger::error("Failed to load motion vector shader");
        return false;
    }

    // AUDIT.md §H15 / FIXPLAN G1: per-object motion vector shader, used
    // for the overlay pass after the camera-motion pass.
    std::string motionVecObjVertPath =
        assetPath + "/shaders/motion_vectors_object.vert.glsl";
    std::string motionVecObjFragPath =
        assetPath + "/shaders/motion_vectors_object.frag.glsl";
    if (!m_motionVectorObjectShader.loadFromFiles(motionVecObjVertPath,
                                                   motionVecObjFragPath))
    {
        Logger::error("Failed to load per-object motion vector shader");
        return false;
    }

    // Load SMAA shaders
    std::string smaaEdgeVertPath = assetPath + "/shaders/smaa_edge.vert.glsl";
    std::string smaaEdgeFragPath = assetPath + "/shaders/smaa_edge.frag.glsl";
    if (!m_smaaEdgeShader.loadFromFiles(smaaEdgeVertPath, smaaEdgeFragPath))
    {
        Logger::error("Failed to load SMAA edge detection shaders");
        return false;
    }

    std::string smaaBlendVertPath = assetPath + "/shaders/smaa_blend.vert.glsl";
    std::string smaaBlendFragPath = assetPath + "/shaders/smaa_blend.frag.glsl";
    if (!m_smaaBlendShader.loadFromFiles(smaaBlendVertPath, smaaBlendFragPath))
    {
        Logger::error("Failed to load SMAA blend weight shaders");
        return false;
    }

    std::string smaaNeighborVertPath = assetPath + "/shaders/smaa_neighborhood.vert.glsl";
    std::string smaaNeighborFragPath = assetPath + "/shaders/smaa_neighborhood.frag.glsl";
    if (!m_smaaNeighborhoodShader.loadFromFiles(smaaNeighborVertPath, smaaNeighborFragPath))
    {
        Logger::error("Failed to load SMAA neighborhood blending shaders");
        return false;
    }

    // Load SDSM depth reduction compute shader
    m_depthReducer = std::make_unique<DepthReducer>();
    std::string depthReducePath = assetPath + "/shaders/depth_reduce.comp.glsl";
    if (!m_depthReducer->init(depthReducePath))
    {
        Logger::warning("SDSM depth reducer failed to initialize — SDSM disabled");
        m_sdsmEnabled = false;
        m_depthReducer.reset();
    }

    // Load screen-space contact shadow shader
    std::string contactShadowFragPath = assetPath + "/shaders/contact_shadows.frag.glsl";
    if (!m_contactShadowShader.loadFromFiles(screenVertPath, contactShadowFragPath))
    {
        Logger::error("Failed to load contact shadow shader");
        return false;
    }

    // Load ID buffer shader (for mouse picking)
    std::string idBufferVertPath = assetPath + "/shaders/id_buffer.vert.glsl";
    std::string idBufferFragPath = assetPath + "/shaders/id_buffer.frag.glsl";
    if (!m_idBufferShader.loadFromFiles(idBufferVertPath, idBufferFragPath))
    {
        Logger::error("Failed to load ID buffer shaders");
        return false;
    }

    // Load outline shader (for selection highlight)
    std::string outlineVertPath = assetPath + "/shaders/outline.vert.glsl";
    std::string outlineFragPath = assetPath + "/shaders/outline.frag.glsl";
    if (!m_outlineShader.loadFromFiles(outlineVertPath, outlineFragPath))
    {
        Logger::error("Failed to load outline shaders");
        return false;
    }

    Logger::info("Shaders loaded successfully");

    // Assign every sampler uniform in the scene shader to its designated texture unit
    // ONCE at load time. OpenGL sampler uniforms default to 0, which causes "active
    // samplers with a different type refer to the same texture image unit" errors on
    // Mesa when different sampler types (sampler2D, samplerCube, sampler2DArray) all
    // point to unit 0.
    m_sceneShader.use();
    m_sceneShader.setInt("u_diffuseTexture", 0);
    m_sceneShader.setInt("u_normalMap", 1);
    m_sceneShader.setInt("u_heightMap", 2);
    m_sceneShader.setInt("u_cascadeShadowMap", 3);
    m_sceneShader.setInt("u_pointShadowMaps[0]", 4);
    m_sceneShader.setInt("u_pointShadowMaps[1]", 5);
    m_sceneShader.setInt("u_metallicRoughnessMap", 6);
    m_sceneShader.setInt("u_emissiveMap", 7);
    m_sceneShader.setInt("u_aoMap", 8);
    m_sceneShader.setInt("u_causticsTex", 9);
    m_sceneShader.setInt("u_probeIrradianceMap", 10);
    m_sceneShader.setInt("u_probePrefilterMap", 11);
    m_sceneShader.setInt("u_irradianceMap", 14);

    // SH probe grid: 7 sampler3D on units 17-23
    for (int i = 0; i < SHProbeGrid::SH_TEXTURE_COUNT; i++)
    {
        m_sceneShader.setInt("u_shTex[" + std::to_string(i) + "]",
                             SHProbeGrid::FIRST_TEXTURE_UNIT + i);
    }
    m_sceneShader.setInt("u_prefilterMap", 15);
    m_sceneShader.setInt("u_brdfLUT", 16);

    return true;
}

void Renderer::initFramebuffers(int width, int height, int msaaSamples)
{
    m_windowWidth = width;
    m_windowHeight = height;
    m_msaaSamples = msaaSamples;

    // MSAA framebuffer — scene is rendered here (HDR floating-point)
    FramebufferConfig msaaConfig;
    msaaConfig.width = width;
    msaaConfig.height = height;
    msaaConfig.samples = msaaSamples;
    msaaConfig.hasColorAttachment = true;
    msaaConfig.hasDepthAttachment = true;
    msaaConfig.isFloatingPoint = true;
    m_msaaFbo = std::make_unique<Framebuffer>(msaaConfig);

    // Resolve framebuffer — MSAA is resolved to this non-multisampled FBO (HDR floating-point)
    FramebufferConfig resolveConfig;
    resolveConfig.width = width;
    resolveConfig.height = height;
    resolveConfig.samples = 1;
    resolveConfig.hasColorAttachment = true;
    resolveConfig.hasDepthAttachment = false;
    resolveConfig.isFloatingPoint = true;
    m_resolveFbo = std::make_unique<Framebuffer>(resolveConfig);

    // Post-tonemapped LDR output (for editor viewport — ImGui displays this texture)
    FramebufferConfig outputConfig;
    outputConfig.width = width;
    outputConfig.height = height;
    outputConfig.samples = 1;
    outputConfig.hasColorAttachment = true;
    outputConfig.hasDepthAttachment = false;
    outputConfig.isFloatingPoint = false;  // LDR RGBA8 — post-tonemapped
    m_outputFbo = std::make_unique<Framebuffer>(outputConfig);

    // Attach a depth-stencil renderbuffer to the output FBO for selection outlines.
    // The existing screen quad pass disables depth test so this doesn't interfere.
    glGenRenderbuffers(1, &m_outlineStencilRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_outlineStencilRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, m_outputFbo->getId());
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, m_outlineStencilRbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ID buffer FBO for mouse picking (RGBA8 + depth, rendered on demand)
    FramebufferConfig idBufferConfig;
    idBufferConfig.width = width;
    idBufferConfig.height = height;
    idBufferConfig.samples = 1;
    idBufferConfig.hasColorAttachment = true;
    idBufferConfig.hasDepthAttachment = true;
    idBufferConfig.isFloatingPoint = false;  // RGBA8 for ID encoding
    m_idBufferFbo = std::make_unique<Framebuffer>(idBufferConfig);

    // Fullscreen quad for the screen pass
    m_screenQuad = std::make_unique<FullscreenQuad>();

    // Cascaded shadow maps for the directional light
    m_cascadedShadowMap = std::make_unique<CascadedShadowMap>();

    // Point light shadow maps (pool of MAX_POINT_SHADOW_LIGHTS)
    for (int i = 0; i < MAX_POINT_SHADOW_LIGHTS; i++)
    {
        m_pointShadowMaps.push_back(std::make_unique<PointShadowMap>());
    }

    // Skybox (procedural gradient — no cubemap texture)
    m_skybox = std::make_unique<Skybox>();

    // Bloom mip-chain texture (DSA, immutable storage)
    {
        // Compute mip dimensions (base = half resolution)
        int mipW = width / 2;
        int mipH = height / 2;
        if (mipW < 1) mipW = 1;
        if (mipH < 1) mipH = 1;

        glCreateTextures(GL_TEXTURE_2D, 1, &m_bloomTexture);
        glTextureStorage2D(m_bloomTexture, BLOOM_MIP_COUNT, GL_R11F_G11F_B10F, mipW, mipH);

        // Record mip dimensions for later use
        for (int i = 0; i < BLOOM_MIP_COUNT; i++)
        {
            m_bloomMipWidths[i] = mipW;
            m_bloomMipHeights[i] = mipH;
            mipW = std::max(1, mipW / 2);
            mipH = std::max(1, mipH / 2);
        }

        glTextureParameteri(m_bloomTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTextureParameteri(m_bloomTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(m_bloomTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_bloomTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_bloomTexture, GL_TEXTURE_BASE_LEVEL, 0);
        glTextureParameteri(m_bloomTexture, GL_TEXTURE_MAX_LEVEL, BLOOM_MIP_COUNT - 1);

        // Single FBO, we'll attach different mip levels as needed
        glCreateFramebuffers(1, &m_bloomFbo);

        // Verify completeness with a placeholder mip-0 attachment. The bloom
        // passes re-attach different mips at draw time; this just ensures the
        // base configuration is valid once, at creation.
        glNamedFramebufferTexture(m_bloomFbo, GL_COLOR_ATTACHMENT0, m_bloomTexture, 0);
        {
            GLenum status = glCheckNamedFramebufferStatus(m_bloomFbo, GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE)
            {
                Logger::error("Bloom FBO incomplete — status: 0x"
                    + std::to_string(status));
            }
        }

        Logger::debug("Bloom mip-chain created: " + std::to_string(BLOOM_MIP_COUNT)
            + " levels from " + std::to_string(m_bloomMipWidths[0]) + "x"
            + std::to_string(m_bloomMipHeights[0]));
    }

    // Auto-exposure luminance texture (DSA, immutable storage with full mip chain)
    {
        glCreateTextures(GL_TEXTURE_2D, 1, &m_luminanceTexture);
        // 256x256 with 9 mip levels (256 → 1)
        glTextureStorage2D(m_luminanceTexture, 9, GL_RGB16F, 256, 256);
        glTextureParameteri(m_luminanceTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTextureParameteri(m_luminanceTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(m_luminanceTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_luminanceTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    // Async PBO readback for auto-exposure (DSA, avoids GPU→CPU sync stall)
    glCreateBuffers(2, m_luminancePbo);
    for (int i = 0; i < 2; i++)
    {
        glNamedBufferStorage(m_luminancePbo[i], 3 * sizeof(float), nullptr,
                             GL_MAP_READ_BIT);
    }
    m_pboWriteIndex = 0;
    m_pboReady = false;

    // Resolved depth FBO (for SSAO — depth-only, sampleable texture)
    FramebufferConfig depthResolveConfig;
    depthResolveConfig.width = width;
    depthResolveConfig.height = height;
    depthResolveConfig.samples = 1;
    depthResolveConfig.hasColorAttachment = false;
    depthResolveConfig.hasDepthAttachment = true;
    depthResolveConfig.isFloatingPoint = false;
    depthResolveConfig.isDepthTexture = true;
    m_resolveDepthFbo = std::make_unique<Framebuffer>(depthResolveConfig);

    // SSAO FBOs (full-resolution to avoid upsample seam artifacts)
    FramebufferConfig ssaoConfig;
    ssaoConfig.width = width;
    ssaoConfig.height = height;
    ssaoConfig.samples = 1;
    ssaoConfig.hasColorAttachment = true;
    ssaoConfig.hasDepthAttachment = false;
    ssaoConfig.isFloatingPoint = true;

    m_ssaoFbo = std::make_unique<Framebuffer>(ssaoConfig);
    m_ssaoBlurFbo = std::make_unique<Framebuffer>(ssaoConfig);

    // Contact shadow FBO (same config as SSAO — RGBA16F)
    m_contactShadowFbo = std::make_unique<Framebuffer>(ssaoConfig);

    // TAA FBOs (created regardless, used when mode is TAA)
    // Non-MSAA scene FBO for TAA (scene rendered here instead of MSAA FBO)
    FramebufferConfig taaSceneConfig;
    taaSceneConfig.width = width;
    taaSceneConfig.height = height;
    taaSceneConfig.samples = 1;
    taaSceneConfig.hasColorAttachment = true;
    taaSceneConfig.hasDepthAttachment = true;
    taaSceneConfig.isFloatingPoint = true;
    m_taaSceneFbo = std::make_unique<Framebuffer>(taaSceneConfig);

    m_taa = std::make_unique<Taa>(width, height);

    // SMAA lookup textures and FBOs
    m_smaa = std::make_unique<Smaa>(width, height);

    // Generate SSAO kernel and noise texture
    generateSsaoKernel();
    generateSsaoNoiseTexture();

    // Upload SSAO kernel to shader once (it never changes)
    m_ssaoShader.use();
    m_ssaoShader.setInt("u_kernelSize", static_cast<int>(m_ssaoKernel.size()));
    for (size_t i = 0; i < m_ssaoKernel.size(); i++)
    {
        m_ssaoShader.setVec3("u_samples[" + std::to_string(i) + "]", m_ssaoKernel[i]);
    }

    // Initialize color grading LUT (neutral + built-in presets)
    m_colorGradingLut = std::make_unique<ColorGradingLut>();
    m_colorGradingLut->initialize();

    // Initialize instance buffer for instanced rendering
    m_instanceBuffer = std::make_unique<InstanceBuffer>();

    // Initialize MDI (Multi-Draw Indirect) infrastructure
    m_meshPool = std::make_unique<MeshPool>();
    m_indirectBuffer = std::make_unique<IndirectBuffer>();

    // IBL environment map (irradiance, prefilter, BRDF LUT)
    m_environmentMap = std::make_unique<EnvironmentMap>();
    if (m_environmentMap->initialize(m_assetPath))
    {
        GLuint skyboxCubemap = m_skybox ? m_skybox->getTextureId() : 0;
        bool hasCubemap = m_skybox && m_skybox->hasTexture();
        m_environmentMap->generate(skyboxCubemap, hasCubemap,
                                   *m_screenQuad, m_skyboxShader);
    }
    while (glGetError() != GL_NO_ERROR) {}

    // Light probe manager (shares irradiance/prefilter convolution shaders)
    m_lightProbeManager = std::make_unique<LightProbeManager>();
    m_lightProbeManager->initialize(m_assetPath);

    // SH probe grid (created here, configured and filled by the scene)
    m_shProbeGrid = std::make_unique<SHProbeGrid>();

    // Generate procedural caustics texture for underwater effects
    generateCausticsTexture();

    Logger::info("Framebuffer pipeline initialized: "
        + std::to_string(width) + "x" + std::to_string(height)
        + " with " + std::to_string(msaaSamples) + "x MSAA + shadow mapping + skybox + bloom + SSAO");
}

void Renderer::beginFrame()
{
    bool isTAA = (m_antiAliasMode == AntiAliasMode::TAA && m_taa && m_taaSceneFbo);
    bool isSMAA = (m_antiAliasMode == AntiAliasMode::SMAA && m_smaa && m_taaSceneFbo);

    if (isTAA || isSMAA)
    {
        m_taaSceneFbo->bind();
    }
    else if (m_msaaFbo)
    {
        m_msaaFbo->bind();
    }

    // Restore core depth state — post-processing and ImGui may leave these changed.
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_GEQUAL);
    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    glClearDepth(0.0);

    // Re-apply stored clear color every frame — editor mode clobbers glClearColor
    // for its own background, which would bleed into the scene FBO otherwise.
    glClearColor(m_clearColor.r, m_clearColor.g, m_clearColor.b, 1.0f);

    glViewport(0, 0, m_windowWidth, m_windowHeight);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Ensure clip distance is clean at frame start (water passes enable/disable it)
    glDisable(GL_CLIP_DISTANCE0);

    // Bind SSBOs that shaders declare but may not access on every code path.
    // Mesa requires ALL declared SSBOs to have valid buffers bound at draw time.
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_dummyModelSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_boneMatrixSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_dummyMorphSSBO);

    // Bind fallback textures to ALL sampler units that the scene shader declares.
    // Mesa requires valid textures even when the shader doesn't sample them.
    // Units used later (by material uploads, shadow binds, etc.) will be overwritten.
    glBindTextureUnit(0, m_fallbackTexture);     // u_diffuseTexture
    glBindTextureUnit(1, m_fallbackTexture);     // u_normalMap
    glBindTextureUnit(2, m_fallbackTexture);     // u_heightMap
    glBindTextureUnit(3, m_fallbackTexArray);    // u_cascadeShadowMap (sampler2DArray)
    glBindTextureUnit(4, m_fallbackCubemap);     // u_pointShadowMaps[0] (samplerCube)
    glBindTextureUnit(5, m_fallbackCubemap);     // u_pointShadowMaps[1] (samplerCube)
    glBindTextureUnit(6, m_fallbackTexture);     // u_metallicRoughnessMap
    glBindTextureUnit(7, m_fallbackTexture);     // u_emissiveMap
    glBindTextureUnit(8, m_fallbackTexture);     // u_aoMap
    glBindTextureUnit(9, m_fallbackTexture);     // u_causticsTex
    glBindTextureUnit(10, m_fallbackCubemap);    // u_probeIrradianceMap (samplerCube)
    glBindTextureUnit(11, m_fallbackCubemap);    // u_probePrefilterMap (samplerCube)
    glBindTextureUnit(14, m_fallbackCubemap);    // u_irradianceMap (samplerCube)
    glBindTextureUnit(15, m_fallbackCubemap);    // u_prefilterMap (samplerCube)
    glBindTextureUnit(16, m_fallbackTexture);    // u_brdfLUT

    // SH probe grid: 7 sampler3D fallbacks (units 17-23)
    for (int i = 0; i < SHProbeGrid::SH_TEXTURE_COUNT; i++)
    {
        glBindTextureUnit(static_cast<GLuint>(SHProbeGrid::FIRST_TEXTURE_UNIT + i), m_fallbackTex3D);
    }
}

void Renderer::endFrame(float deltaTime)
{
    if (!m_resolveFbo || !m_screenQuad)
    {
        return;
    }

    bool isTAA = (m_antiAliasMode == AntiAliasMode::TAA && m_taa && m_taaSceneFbo);
    bool isSMAA = (m_antiAliasMode == AntiAliasMode::SMAA && m_smaa && m_taaSceneFbo);
    bool usesNonMsaaFbo = (isTAA || isSMAA);

    // 1. Resolve scene color → non-multisampled resolve FBO
    if (usesNonMsaaFbo)
    {
        // TAA/SMAA mode: blit from non-MSAA scene FBO to resolve FBO
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_taaSceneFbo->getId());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFbo->getId());
        glBlitFramebuffer(0, 0, m_windowWidth, m_windowHeight,
                          0, 0, m_windowWidth, m_windowHeight,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    else if (m_msaaFbo)
    {
        // MSAA mode: resolve multisampled → non-multisampled
        m_msaaFbo->resolve(*m_resolveFbo);
    }

    // Invalidate the source FBO after resolve — tells the driver the multisampled
    // data is no longer needed, avoiding unnecessary writeback to VRAM.
    if (!usesNonMsaaFbo && m_msaaFbo && m_msaaFbo->isMultisampled())
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFbo->getId());
        GLenum attachments[] = { GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT };
        glInvalidateFramebuffer(GL_FRAMEBUFFER, 2, attachments);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // 2. Resolve depth → sampleable depth texture for SSAO and TAA motion vectors
    GLuint depthSourceFbo = usesNonMsaaFbo ? m_taaSceneFbo->getId()
                                            : (m_msaaFbo ? m_msaaFbo->getId() : 0);
    if (m_resolveDepthFbo && depthSourceFbo != 0)
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, depthSourceFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveDepthFbo->getId());
        glBlitFramebuffer(0, 0, m_windowWidth, m_windowHeight,
                          0, 0, m_windowWidth, m_windowHeight,
                          GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // 2b. SDSM depth reduction (dispatch compute shader on resolved depth)
    if (m_sdsmEnabled && m_depthReducer && m_resolveDepthFbo)
    {
        m_depthReducer->dispatch(m_resolveDepthFbo->getDepthTextureId(),
                                 m_windowWidth, m_windowHeight);
    }

    glDisable(GL_DEPTH_TEST);

    // 3. SSAO pass
    if (m_ssaoEnabled && m_ssaoFbo && m_ssaoBlurFbo && m_resolveDepthFbo)
    {
        m_ssaoFbo->bind();
        glViewport(0, 0, m_windowWidth, m_windowHeight);
        glClear(GL_COLOR_BUFFER_BIT);

        m_ssaoShader.use();

        m_resolveDepthFbo->bindDepthTexture(12);
        m_ssaoShader.setInt("u_depthTexture", 12);

        glBindTextureUnit(11, m_ssaoNoiseTexture);
        m_ssaoShader.setInt("u_noiseTexture", 11);

        // Kernel was uploaded once at init — only set per-frame uniforms
        m_ssaoShader.setFloat("u_radius", m_ssaoRadius);
        m_ssaoShader.setFloat("u_bias", m_ssaoBias);
        m_ssaoShader.setVec2("u_noiseScale",
            glm::vec2(static_cast<float>(m_windowWidth) / 4.0f,
                      static_cast<float>(m_windowHeight) / 4.0f));
        m_ssaoShader.setMat4("u_projection", m_lastProjection);
        m_ssaoShader.setMat4("u_invProjection", glm::inverse(m_lastProjection));

        m_screenQuad->draw();

        // SSAO blur pass
        m_ssaoBlurFbo->bind();
        glViewport(0, 0, m_windowWidth, m_windowHeight);
        glClear(GL_COLOR_BUFFER_BIT);

        m_ssaoBlurShader.use();
        m_ssaoFbo->bindColorTexture(0);
        m_ssaoBlurShader.setInt("u_ssaoInput", 0);
        m_screenQuad->draw();

        // Raw SSAO is consumed — invalidate to free tile memory
        glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFbo->getId());
        GLenum ssaoAttach[] = { GL_COLOR_ATTACHMENT0 };
        glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, ssaoAttach);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // 4. Anti-aliasing resolve (TAA or SMAA)
    // The FBO used as the HDR input for bloom and final composite
    Framebuffer* hdrSourceFbo = m_resolveFbo.get();

    if (isSMAA)
    {
        glm::vec4 rtMetrics(1.0f / static_cast<float>(m_windowWidth),
                            1.0f / static_cast<float>(m_windowHeight),
                            static_cast<float>(m_windowWidth),
                            static_cast<float>(m_windowHeight));

        // 4a. SMAA edge detection
        m_smaa->getEdgeFbo().bind();
        glViewport(0, 0, m_windowWidth, m_windowHeight);
        glClear(GL_COLOR_BUFFER_BIT);

        m_smaaEdgeShader.use();
        m_resolveFbo->bindColorTexture(0);
        m_smaaEdgeShader.setInt("u_colorTexture", 0);
        m_smaaEdgeShader.setVec4("u_rtMetrics", rtMetrics);
        m_screenQuad->draw();

        // 4b. SMAA blend weight calculation
        m_smaa->getBlendFbo().bind();
        glViewport(0, 0, m_windowWidth, m_windowHeight);
        glClear(GL_COLOR_BUFFER_BIT);

        m_smaaBlendShader.use();
        m_smaa->getEdgeFbo().bindColorTexture(0);
        m_smaaBlendShader.setInt("u_edgeTexture", 0);
        glBindTextureUnit(1, m_smaa->getAreaTexture());
        m_smaaBlendShader.setInt("u_areaTexture", 1);
        m_smaaBlendShader.setVec4("u_rtMetrics", rtMetrics);
        m_screenQuad->draw();

        // 4c. SMAA neighborhood blending → output to TAA scene FBO (reused as HDR target)
        m_taaSceneFbo->bind();
        glViewport(0, 0, m_windowWidth, m_windowHeight);
        glClear(GL_COLOR_BUFFER_BIT);

        m_smaaNeighborhoodShader.use();
        m_resolveFbo->bindColorTexture(0);
        m_smaaNeighborhoodShader.setInt("u_colorTexture", 0);
        m_smaa->getBlendFbo().bindColorTexture(1);
        m_smaaNeighborhoodShader.setInt("u_blendTexture", 1);
        m_smaaNeighborhoodShader.setVec4("u_rtMetrics", rtMetrics);
        m_screenQuad->draw();

        // Use SMAA output as bloom/composite input
        hdrSourceFbo = m_taaSceneFbo.get();
    }
    else if (isTAA)
    {
        // 4a. Motion vector pass — camera motion fallback for skybox + sky.
        // AUDIT.md §H15 / FIXPLAN G1: this full-screen pass writes the
        // camera-motion-only reprojection for every pixel; the per-object
        // overlay pass below then OVERWRITES motion where geometry sits,
        // using each entity's current-vs-previous world matrix. Depth
        // test off here (full-screen quad); on for the overlay.
        m_taa->getMotionVectorFbo().bind();
        glViewport(0, 0, m_windowWidth, m_windowHeight);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);

        m_motionVectorShader.use();
        m_resolveDepthFbo->bindDepthTexture(0);
        m_motionVectorShader.setInt("u_depthTexture", 0);
        m_motionVectorShader.setMat4("u_currentInvViewProjection",
            glm::inverse(m_lastViewProjection));
        m_motionVectorShader.setMat4("u_prevViewProjection", m_prevViewProjection);
        m_screenQuad->draw();

        // 4a'. Per-object motion vector overlay (AUDIT.md §H15 / FIXPLAN G1).
        // Re-render opaque geometry with per-draw u_model / u_prevModel
        // uniforms. Depth test + write is on so nearest geometry wins.
        // Dynamic / animated objects now produce correct motion; static
        // objects produce the same result as the camera-only pass above.
        // Skinning + morph paths are out of scope here — the overlay
        // still writes the rigid-body motion, which is better than the
        // camera-only fallback but will undershoot on animated meshes.
        // TODO: emit motion directly from the main geometry pass via MRT
        // for zero-cost correct motion on skinned/morphed objects.
        //
        // Diagnostic: --isolate-feature=motion-overlay flips
        // m_objectMotionOverlayEnabled false to skip this pass entirely
        // (TAA reverts to camera-only motion). Used to bisect the
        // 2026-04-13 visual regression.
        if (m_objectMotionOverlayEnabled) {
        glEnable(GL_DEPTH_TEST);
        // Reverse-Z is globally active (glClipControl ZERO_TO_ONE, clearDepth 0.0);
        // the motion FBO's depth was cleared to 0 (= far), so GL_LESS would never
        // pass. GL_GREATER matches the engine-wide reverse-Z convention.
        // (Fixes the 2026-04-13 visual regression referenced above.)
        glDepthFunc(GL_GREATER);
        glDepthMask(GL_TRUE);

        m_motionVectorObjectShader.use();
        m_motionVectorObjectShader.setMat4("u_viewProjection", m_lastViewProjection);
        m_motionVectorObjectShader.setMat4("u_prevViewProjection", m_prevViewProjection);

        if (m_currentRenderData)
        {
            for (const auto& item : m_currentRenderData->renderItems)
            {
                if (!item.mesh) continue;
                auto it = m_prevWorldMatrices.find(item.entityId);
                const glm::mat4 prevModel =
                    (it != m_prevWorldMatrices.end()) ? it->second : item.worldMatrix;

                m_motionVectorObjectShader.setMat4("u_model", item.worldMatrix);
                m_motionVectorObjectShader.setMat4("u_prevModel", prevModel);
                item.mesh->bind();
                glDrawElements(GL_TRIANGLES,
                               static_cast<GLsizei>(item.mesh->getIndexCount()),
                               GL_UNSIGNED_INT, nullptr);
                item.mesh->unbind();
            }
        }

        // Restore reverse-Z + depth state for the rest of the post-process
        // pipeline. All subsequent full-screen passes assume depth-test off.
        glDepthFunc(GL_GEQUAL);
        glDisable(GL_DEPTH_TEST);
        }  // end if (m_objectMotionOverlayEnabled)

        // 4b. TAA resolve pass
        m_taa->getCurrentFbo().bind();
        glViewport(0, 0, m_windowWidth, m_windowHeight);
        glClear(GL_COLOR_BUFFER_BIT);

        m_taaResolveShader.use();
        m_resolveFbo->bindColorTexture(0);
        m_taaResolveShader.setInt("u_currentTexture", 0);
        m_taa->getHistoryFbo().bindColorTexture(1);
        m_taaResolveShader.setInt("u_historyTexture", 1);
        m_taa->getMotionVectorFbo().bindColorTexture(2);
        m_taaResolveShader.setInt("u_motionVectorTexture", 2);
        m_taaResolveShader.setFloat("u_feedbackFactor", m_taa->getFeedbackFactor());
        m_taaResolveShader.setVec2("u_texelSize",
            glm::vec2(1.0f / static_cast<float>(m_windowWidth),
                      1.0f / static_cast<float>(m_windowHeight)));
        m_screenQuad->draw();

        // Use TAA output as bloom/composite input
        hdrSourceFbo = &m_taa->getCurrentFbo();
    }

    // 5. Mip-chain bloom (CoD: Advanced Warfare style)
    if (m_bloomEnabled && m_bloomTexture != 0 && m_bloomFbo != 0)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFbo);

        // --- Downsample pass: progressively halve the resolution ---
        m_bloomDownsampleShader.use();

        for (int mip = 0; mip < BLOOM_MIP_COUNT; mip++)
        {
            int mipW = m_bloomMipWidths[mip];
            int mipH = m_bloomMipHeights[mip];

            // Attach this mip level as the render target (DSA)
            glNamedFramebufferTexture(m_bloomFbo, GL_COLOR_ATTACHMENT0, m_bloomTexture, mip);
            glViewport(0, 0, mipW, mipH);

            // Source: for mip 0, use the HDR scene; for mip N>0, use mip N-1
            if (mip == 0)
            {
                hdrSourceFbo->bindColorTexture(0);
                m_bloomDownsampleShader.setVec2("u_srcTexelSize",
                    glm::vec2(1.0f / static_cast<float>(m_windowWidth),
                              1.0f / static_cast<float>(m_windowHeight)));
                m_bloomDownsampleShader.setBool("u_useKarisAverage", true);
                m_bloomDownsampleShader.setFloat("u_threshold", m_bloomThreshold);
            }
            else
            {
                // Bind the bloom texture and limit sampling to the previous mip
                glBindTextureUnit(0, m_bloomTexture);
                glTextureParameteri(m_bloomTexture, GL_TEXTURE_BASE_LEVEL, mip - 1);
                glTextureParameteri(m_bloomTexture, GL_TEXTURE_MAX_LEVEL, mip - 1);
                m_bloomDownsampleShader.setVec2("u_srcTexelSize",
                    glm::vec2(1.0f / static_cast<float>(m_bloomMipWidths[mip - 1]),
                              1.0f / static_cast<float>(m_bloomMipHeights[mip - 1])));
                m_bloomDownsampleShader.setBool("u_useKarisAverage", false);
                m_bloomDownsampleShader.setFloat("u_threshold", 0.0f);
            }

            m_bloomDownsampleShader.setInt("u_sourceTexture", 0);
            m_screenQuad->draw();

            // Barrier required: next iteration reads from the mip we just wrote
            glTextureBarrier();
        }

        // Restore texture mip range for sampling during upsample
        glTextureParameteri(m_bloomTexture, GL_TEXTURE_BASE_LEVEL, 0);
        glTextureParameteri(m_bloomTexture, GL_TEXTURE_MAX_LEVEL, BLOOM_MIP_COUNT - 1);

        // --- Upsample pass: progressively double resolution, additive blending ---
        m_bloomUpsampleShader.use();
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);  // Additive: each mip adds its contribution

        for (int mip = BLOOM_MIP_COUNT - 1; mip > 0; mip--)
        {
            int dstMip = mip - 1;
            int dstW = m_bloomMipWidths[dstMip];
            int dstH = m_bloomMipHeights[dstMip];

            // Render target: the next larger mip level (DSA)
            glNamedFramebufferTexture(m_bloomFbo, GL_COLOR_ATTACHMENT0, m_bloomTexture, dstMip);
            glViewport(0, 0, dstW, dstH);

            // Source: current (smaller) mip level
            glTextureParameteri(m_bloomTexture, GL_TEXTURE_BASE_LEVEL, mip);
            glTextureParameteri(m_bloomTexture, GL_TEXTURE_MAX_LEVEL, mip);
            glBindTextureUnit(0, m_bloomTexture);

            m_bloomUpsampleShader.setInt("u_sourceTexture", 0);
            m_bloomUpsampleShader.setVec2("u_srcTexelSize",
                glm::vec2(1.0f / static_cast<float>(m_bloomMipWidths[mip]),
                          1.0f / static_cast<float>(m_bloomMipHeights[mip])));
            m_bloomUpsampleShader.setFloat("u_filterRadius", m_bloomFilterRadius);
            m_screenQuad->draw();

            // Barrier required: next iteration reads from the mip we just wrote
            glTextureBarrier();
        }

        glDisable(GL_BLEND);

        // Restore mip range
        glTextureParameteri(m_bloomTexture, GL_TEXTURE_BASE_LEVEL, 0);
        glTextureParameteri(m_bloomTexture, GL_TEXTURE_MAX_LEVEL, BLOOM_MIP_COUNT - 1);
    }

    // 5b. Auto-exposure: compute average scene luminance from dedicated texture.
    //     Uses async PBO readback to avoid GPU→CPU sync stall.
    if (m_autoExposure && m_luminanceTexture != 0 && m_luminancePbo[0] != 0)
    {
        // Blit the HDR scene to the 256x256 luminance texture (hardware downscale)
        glNamedFramebufferTexture(m_bloomFbo, GL_COLOR_ATTACHMENT0, m_luminanceTexture, 0);

        glBlitNamedFramebuffer(hdrSourceFbo->getId(), m_bloomFbo,
                               0, 0, m_windowWidth, m_windowHeight,
                               0, 0, 256, 256, GL_COLOR_BUFFER_BIT, GL_LINEAR);

        // Generate mipmaps — hardware averages down to 1x1
        glGenerateTextureMipmap(m_luminanceTexture);

        // Async readback: issue glGetTextureImage into a PBO (non-blocking DMA transfer)
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_luminancePbo[m_pboWriteIndex]);
        glGetTextureImage(m_luminanceTexture, 8, GL_RGB, GL_FLOAT,
                          3 * static_cast<GLsizei>(sizeof(float)), nullptr);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

        // Read from the OTHER PBO (filled 1-2 frames ago — data is ready)
        if (m_pboReady)
        {
            int readIndex = 1 - m_pboWriteIndex;
            auto* data = static_cast<const float*>(
                glMapNamedBufferRange(m_luminancePbo[readIndex], 0,
                                      3 * sizeof(float), GL_MAP_READ_BIT));
            if (data)
            {
                float avgLuminance = 0.2126f * data[0] + 0.7152f * data[1] + 0.0722f * data[2];
                avgLuminance = std::max(avgLuminance, 0.001f);

                m_targetExposure = m_autoExposureTarget / avgLuminance;
                m_targetExposure = std::clamp(m_targetExposure, m_autoExposureMin, m_autoExposureMax);

                float adaptSpeed = 1.0f - std::exp(-m_autoExposureSpeed * deltaTime);
                m_exposure += (m_targetExposure - m_exposure) * adaptSpeed;

                glUnmapNamedBuffer(m_luminancePbo[readIndex]);
            }
        }

        // Swap PBO index for next frame
        m_pboWriteIndex = 1 - m_pboWriteIndex;
        m_pboReady = true;

        // Restore bloom FBO attachment to bloom texture mip 0
        glNamedFramebufferTexture(m_bloomFbo, GL_COLOR_ATTACHMENT0, m_bloomTexture, 0);
    }

    // 5c. Screen-space contact shadows
    if (m_contactShadowsEnabled && m_contactShadowFbo && m_resolveDepthFbo
        && m_hasDirectionalLight)
    {
        m_contactShadowFbo->bind();
        glViewport(0, 0, m_windowWidth, m_windowHeight);
        glClear(GL_COLOR_BUFFER_BIT);

        m_contactShadowShader.use();

        m_resolveDepthFbo->bindDepthTexture(12);
        m_contactShadowShader.setInt("u_depthTexture", 12);

        m_contactShadowShader.setMat4("u_projection", m_lastProjection);
        m_contactShadowShader.setMat4("u_invProjection", glm::inverse(m_lastProjection));
        m_contactShadowShader.setMat4("u_view", m_lastView);
        m_contactShadowShader.setVec3("u_lightDirection", m_directionalLight.direction);
        m_contactShadowShader.setVec2("u_texelSize",
            glm::vec2(1.0f / static_cast<float>(m_windowWidth),
                      1.0f / static_cast<float>(m_windowHeight)));
        m_contactShadowShader.setFloat("u_rayLength", m_contactShadowLength);
        m_contactShadowShader.setInt("u_numSteps", m_contactShadowSteps);

        m_screenQuad->draw();
    }

    // 6. Final screen quad composite (tone mapping + bloom + SSAO + contact shadows)
    //    Render to the output FBO (not the screen) so the editor can display it as a texture.
    if (m_outputFbo)
    {
        m_outputFbo->bind();
    }
    else
    {
        Framebuffer::unbind();
    }
    glViewport(0, 0, m_windowWidth, m_windowHeight);
    glClear(GL_COLOR_BUFFER_BIT);

    m_screenShader.use();
    hdrSourceFbo->bindColorTexture(0);
    m_screenShader.setInt("u_screenTexture", 0);
    m_screenShader.setFloat("u_exposure", m_exposure);
    m_screenShader.setInt("u_tonemapMode", m_tonemapMode);
    m_screenShader.setInt("u_debugMode", m_debugMode);

    // Bloom uniforms — ALWAYS bind texture (Mesa requires valid textures for declared samplers).
    // Phase 10.7 slice C1: photosensitive safe mode scales down the
    // intensity via `limitBloomIntensity` so a user-visible slider
    // change takes effect immediately without the composite shader
    // needing to branch on safe-mode state. When disabled the helper
    // returns the argument unchanged.
    m_screenShader.setBool("u_bloomEnabled", m_bloomEnabled);
    const float bloomIntensityUpload = limitBloomIntensity(
        m_bloomIntensity,
        m_photosensitiveEnabled,
        m_photosensitiveLimits);
    m_screenShader.setFloat("u_bloomIntensity", bloomIntensityUpload);
    if (m_bloomTexture != 0)
    {
        // Sample from mip 0 (the fully composited bloom result)
        glTextureParameteri(m_bloomTexture, GL_TEXTURE_BASE_LEVEL, 0);
        glTextureParameteri(m_bloomTexture, GL_TEXTURE_MAX_LEVEL, 0);
        glBindTextureUnit(9, m_bloomTexture);
        m_screenShader.setInt("u_bloomTexture", 9);
    }

    // SSAO uniforms — ALWAYS bind texture
    m_screenShader.setBool("u_ssaoEnabled", m_ssaoEnabled);
    if (m_ssaoBlurFbo)
    {
        m_ssaoBlurFbo->bindColorTexture(10);
        m_screenShader.setInt("u_ssaoTexture", 10);
    }

    // Contact shadow uniforms — ALWAYS bind texture
    bool contactShadowActive = m_contactShadowsEnabled && m_hasDirectionalLight;
    m_screenShader.setBool("u_contactShadowEnabled", contactShadowActive);
    if (m_contactShadowFbo)
    {
        m_contactShadowFbo->bindColorTexture(11);
        m_screenShader.setInt("u_contactShadowTexture", 11);
    }

    // Phase 10 fog uniforms — distance / height / sun inscatter.
    // Composed in linear HDR between contact shadows and bloom; see
    // docs/PHASE10_FOG_DESIGN.md §4 for the composition order rationale.
    //
    // Accessibility transform runs here (slice 11.9): authored state
    // stays on Renderer; the effective state goes to the GPU. The
    // pure function in fog.cpp lets tests pin every flag's behaviour
    // without a GL context.
    {
        const FogState authoredFog{
            m_fogMode,
            m_fogParams,
            m_heightFogEnabled,
            m_heightFogParams,
            m_sunInscatterEnabled,
            m_sunInscatterParams,
        };
        const FogState effective =
            applyFogAccessibilitySettings(authoredFog, m_postProcessAccessibility);

        m_screenShader.setInt("u_fogMode", static_cast<int>(effective.fogMode));
        m_screenShader.setVec3("u_fogColour", effective.fogParams.colour);
        m_screenShader.setFloat("u_fogStart",   effective.fogParams.start);
        m_screenShader.setFloat("u_fogEnd",     effective.fogParams.end);
        m_screenShader.setFloat("u_fogDensity", effective.fogParams.density);

        m_screenShader.setBool("u_heightFogEnabled", effective.heightFogEnabled);
        m_screenShader.setVec3("u_heightFogColour",  effective.heightFogParams.colour);
        m_screenShader.setFloat("u_heightFogY",        effective.heightFogParams.fogHeight);
        m_screenShader.setFloat("u_heightFogDensity",  effective.heightFogParams.groundDensity);
        m_screenShader.setFloat("u_heightFogFalloff",  effective.heightFogParams.heightFalloff);
        m_screenShader.setFloat("u_heightFogMaxOpacity", effective.heightFogParams.maxOpacity);

        m_screenShader.setBool("u_sunInscatterEnabled", effective.sunInscatterEnabled);
        m_screenShader.setVec3("u_sunInscatterColour",  effective.sunInscatterParams.colour);
        m_screenShader.setFloat("u_sunInscatterExponent", effective.sunInscatterParams.exponent);
        m_screenShader.setFloat("u_sunInscatterStart",    effective.sunInscatterParams.startDistance);
        m_screenShader.setVec3("u_sunDirection", m_directionalLight.direction);

        // Depth texture — ALWAYS bind (Mesa declared-sampler safety).
        // Unit 12 is shared with SSAO / contact-shadow passes, which
        // have already run by this point; re-bind to guarantee it's
        // still live for the composite pass.
        if (m_resolveDepthFbo)
        {
            m_resolveDepthFbo->bindDepthTexture(12);
        }
        else
        {
            glBindTextureUnit(12, m_fallbackTexture);
        }
        m_screenShader.setInt("u_fogDepthTexture", 12);
        m_screenShader.setMat4("u_fogInvViewProj",
                               glm::inverse(m_lastViewProjection));
        m_screenShader.setVec3("u_fogCameraWorldPos", m_cameraWorldPosition);
    }

    // Color grading LUT uniforms — ALWAYS bind texture
    bool lutActive = (m_colorGradingLut && m_colorGradingLut->isEnabled());
    m_screenShader.setBool("u_lutEnabled", lutActive);
    if (m_colorGradingLut)
    {
        m_colorGradingLut->bind(13);
        m_screenShader.setInt("u_lutTexture", 13);
        m_screenShader.setFloat("u_lutIntensity",
            lutActive ? m_colorGradingLut->getIntensity() : 0.0f);
    }

    // Accessibility: color-vision-deficiency simulation matrix
    bool cvdActive = (m_colorVisionMode != ColorVisionMode::Normal);
    m_screenShader.setBool("u_colorVisionEnabled", cvdActive);
    m_screenShader.setMat3("u_colorVisionMatrix",
        colorVisionMatrix(m_colorVisionMode));

    m_screenQuad->draw();

    // 7. Swap TAA history buffers
    if (isTAA)
    {
        m_taa->swapBuffers();
        m_taa->nextFrame();
        m_prevViewProjection = m_lastViewProjection;
    }

    // AUDIT.md §H15 / FIXPLAN G1: snapshot this frame's world
    // matrices so the per-object motion vector overlay next frame
    // can compute prev→curr per entity. Only scene items are
    // tracked; transients (particle quads, debug lines) do not
    // participate in TAA reprojection anyway.
    //
    // R10 (Phase 10.9 Slice 4): the clear is unconditional — non-TAA
    // modes (MSAA / SMAA / None) also wipe the cache so a subsequent
    // toggle-back to TAA doesn't read stale matrices that may belong
    // to entities destroyed (and their entityIds reused) in between.
    // Population stays gated on `isTAA` because only TAA's overlay
    // reads the cache.
    if (m_currentRenderData)
    {
        updateMotionOverlayPrevWorld(m_prevWorldMatrices, isTAA,
                                       m_currentRenderData->renderItems,
                                       m_currentRenderData->transparentItems);
    }

    if (isTAA)
    {
        // Clear the cached pointer — it must not be dereferenced after
        // endFrame returns since the scene may be mutated before the
        // next renderScene call.
        m_currentRenderData = nullptr;
    }

    glEnable(GL_DEPTH_TEST);
}

void Renderer::uploadMaterialUniforms(const Material& material)
{
    // UV tiling scale
    m_sceneShader.setFloat("u_uvScale", material.getUvScale());

    // PBR mode toggle
    bool usePBR = (material.getType() == MaterialType::PBR);
    m_sceneShader.setBool("u_usePBR", usePBR);

    // Material (Blinn-Phong uniforms — always set for the non-PBR path)
    m_sceneShader.setVec3("u_materialDiffuse", material.getDiffuseColor());
    m_sceneShader.setVec3("u_materialSpecular", material.getSpecularColor());
    m_sceneShader.setFloat("u_materialShininess", material.getShininess());
    m_sceneShader.setVec3("u_materialEmissive", material.getEmissive());
    m_sceneShader.setFloat("u_materialEmissiveStrength", material.getEmissiveStrength());

    // Diffuse/albedo texture (shared by both paths — unit 0)
    bool hasTexture = material.hasDiffuseTexture();
    m_sceneShader.setBool("u_hasTexture", hasTexture);
    if (hasTexture)
    {
        material.getDiffuseTexture()->bind(0);
    }
    else
    {
        glBindTextureUnit(0, m_fallbackTexture);
    }
    m_sceneShader.setInt("u_diffuseTexture", 0);

    // Normal map (shared — unit 1)
    bool hasNormalMap = material.hasNormalMap();
    m_sceneShader.setBool("u_hasNormalMap", hasNormalMap);
    if (hasNormalMap)
    {
        material.getNormalMap()->bind(1);
    }
    else
    {
        glBindTextureUnit(1, m_fallbackTexture);
    }
    m_sceneShader.setInt("u_normalMap", 1);

    // Height map (shared — unit 2)
    bool hasHeightMap = m_pomEnabled && material.isPomEnabled() && material.hasHeightMap();
    m_sceneShader.setBool("u_hasHeightMap", hasHeightMap);
    if (hasHeightMap)
    {
        material.getHeightMap()->bind(2);
        m_sceneShader.setFloat("u_heightScale", material.getHeightScale() * m_pomHeightMultiplier);
    }
    else
    {
        glBindTextureUnit(2, m_fallbackTexture);
    }
    m_sceneShader.setInt("u_heightMap", 2);

    // PBR uniforms
    if (usePBR)
    {
        m_sceneShader.setVec3("u_pbrAlbedo", material.getAlbedo());
        m_sceneShader.setFloat("u_pbrMetallic", material.getMetallic());
        m_sceneShader.setFloat("u_pbrRoughness", material.getRoughness());
        m_sceneShader.setFloat("u_pbrAo", material.getAo());
        m_sceneShader.setFloat("u_iblMultiplier",
            (m_iblMultiplierOverride >= 0.0f) ? m_iblMultiplierOverride
                                              : material.getIblMultiplier());
        m_sceneShader.setFloat("u_clearcoat", material.getClearcoat());
        m_sceneShader.setFloat("u_clearcoatRoughness", material.getClearcoatRoughness());
        m_sceneShader.setVec3("u_pbrEmissive", material.getEmissive());
        m_sceneShader.setFloat("u_pbrEmissiveStrength", material.getEmissiveStrength());

        // Metallic-roughness map (unit 6)
        bool hasMR = material.hasMetallicRoughnessTexture();
        m_sceneShader.setBool("u_hasMetallicRoughnessMap", hasMR);
        if (hasMR)
        {
            material.getMetallicRoughnessTexture()->bind(6);
        }
        else
        {
            glBindTextureUnit(6, m_fallbackTexture);
        }
        m_sceneShader.setInt("u_metallicRoughnessMap", 6);

        // Emissive map (unit 7)
        bool hasEmissive = material.hasEmissiveTexture();
        m_sceneShader.setBool("u_hasEmissiveMap", hasEmissive);
        if (hasEmissive)
        {
            material.getEmissiveTexture()->bind(7);
        }
        else
        {
            glBindTextureUnit(7, m_fallbackTexture);
        }
        m_sceneShader.setInt("u_emissiveMap", 7);

        // AO map (unit 8)
        bool hasAo = material.hasAoTexture();
        m_sceneShader.setBool("u_hasAoMap", hasAo);
        if (hasAo)
        {
            material.getAoTexture()->bind(8);
        }
        else
        {
            glBindTextureUnit(8, m_fallbackTexture);
        }
        m_sceneShader.setInt("u_aoMap", 8);
    }
    else
    {
        // Ensure PBR-only uniforms are zeroed when not in PBR mode
        m_sceneShader.setBool("u_hasMetallicRoughnessMap", false);
        m_sceneShader.setBool("u_hasEmissiveMap", false);
        m_sceneShader.setBool("u_hasAoMap", false);
        m_sceneShader.setFloat("u_iblMultiplier",
            (m_iblMultiplierOverride >= 0.0f) ? m_iblMultiplierOverride : 1.0f);
        m_sceneShader.setFloat("u_clearcoat", 0.0f);
        m_sceneShader.setFloat("u_clearcoatRoughness", 0.0f);
        // Mesa requires valid textures for all declared samplers
        glBindTextureUnit(6, m_fallbackTexture);
        glBindTextureUnit(7, m_fallbackTexture);
        glBindTextureUnit(8, m_fallbackTexture);
    }

    // Stochastic tiling
    m_sceneShader.setBool("u_stochasticTiling", material.isStochasticTiling());

    // Transparency uniforms
    m_sceneShader.setInt("u_alphaMode", static_cast<int>(material.getAlphaMode()));
    m_sceneShader.setFloat("u_alphaCutoff", material.getAlphaCutoff());
    m_sceneShader.setFloat("u_baseColorAlpha", material.getBaseColorAlpha());

    // Wireframe
    m_sceneShader.setBool("u_wireframe", m_isWireframe);
}

void Renderer::drawMesh(const Mesh& mesh, const glm::mat4& modelMatrix,
                         const Material& material, const Camera& camera,
                         float /*aspectRatio*/,
                         const std::vector<glm::mat4>* boneMatrices,
                         const float* morphWeights, int morphWeightCount)
{
    m_sceneShader.use();

    // Non-instanced draw — use uniform model matrix
    m_sceneShader.setBool("u_useInstancing", false);
    m_sceneShader.setBool("u_useMDI", false);
    m_sceneShader.setMat4("u_model", modelMatrix);
    m_sceneShader.setMat3("u_normalMatrix", computeNormalMatrix(modelMatrix));
    m_sceneShader.setMat4("u_view", camera.getViewMatrix());
    m_sceneShader.setMat4("u_projection", m_lastProjection);

    // Skeletal animation: upload bone matrices if present
    bool skinned = (boneMatrices != nullptr && !boneMatrices->empty());
    m_sceneShader.setBool("u_hasBones", skinned);
    if (skinned)
    {
        size_t boneCount = std::min(boneMatrices->size(),
                                     static_cast<size_t>(MAX_BONES));
        GLsizeiptr dataSize = static_cast<GLsizeiptr>(
            boneCount * sizeof(glm::mat4));
        glNamedBufferSubData(m_boneMatrixSSBO, 0, dataSize, boneMatrices->data());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_boneMatrixSSBO);
    }

    // Morph target deformation: bind SSBO and set weights
    int activeMorphCount = 0;
    if (morphWeights && morphWeightCount > 0 && mesh.getMorphSSBO() != 0)
    {
        activeMorphCount = std::min(morphWeightCount, mesh.getMorphTargetCount());
        activeMorphCount = std::min(activeMorphCount, Mesh::MAX_MORPH_TARGETS);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, mesh.getMorphSSBO());
        m_sceneShader.setInt("u_morphVertexCount", mesh.getMorphVertexCount());
        // Pre-cache the indexed uniform names so we don't build
        // "u_morphWeights[i]" on every draw. (AUDIT H7.)
        static const std::array<std::string, Mesh::MAX_MORPH_TARGETS> MORPH_NAMES = []{
            std::array<std::string, Mesh::MAX_MORPH_TARGETS> a;
            for (int i = 0; i < Mesh::MAX_MORPH_TARGETS; ++i)
            {
                a[static_cast<size_t>(i)] = "u_morphWeights[" + std::to_string(i) + "]";
            }
            return a;
        }();
        for (int i = 0; i < activeMorphCount; ++i)
        {
            m_sceneShader.setFloat(MORPH_NAMES[static_cast<size_t>(i)],
                                    morphWeights[i]);
        }
    }
    m_sceneShader.setInt("u_morphTargetCount", activeMorphCount);

    uploadMaterialUniforms(material);

    // Double-sided: disable face culling for this draw call
    bool doubleSided = material.isDoubleSided();
    if (doubleSided)
    {
        glDisable(GL_CULL_FACE);
    }

    // Set polygon mode
    if (m_isWireframe)
    {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }

    // Draw
    mesh.bind();
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.getIndexCount()),
                   GL_UNSIGNED_INT, nullptr);
    m_cullingStats.drawCalls++;
    mesh.unbind();

    // Restore polygon mode
    if (m_isWireframe)
    {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    // Restore face culling
    if (doubleSided)
    {
        glEnable(GL_CULL_FACE);
    }

    // Textures are left bound — the next draw call rebinds what it needs.
    // Unbinding is unnecessary overhead (6-12 extra GL calls per draw).
}

void Renderer::setClearColor(const glm::vec3& color)
{
    m_clearColor = color;
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

void Renderer::setExposure(float exposure)
{
    m_exposure = exposure;
}

float Renderer::getExposure() const
{
    return m_exposure;
}

void Renderer::setAutoExposure(bool enabled)
{
    m_autoExposure = enabled;
    Logger::debug(std::string("Auto-exposure: ") + (enabled ? "ON" : "OFF"));
}

bool Renderer::isAutoExposure() const
{
    return m_autoExposure;
}

void Renderer::setTonemapMode(int mode)
{
    m_tonemapMode = mode;
}

int Renderer::getTonemapMode() const
{
    return m_tonemapMode;
}

void Renderer::setDebugMode(int mode)
{
    m_debugMode = mode;
}

int Renderer::getDebugMode() const
{
    return m_debugMode;
}

void Renderer::setColorVisionMode(ColorVisionMode mode)
{
    m_colorVisionMode = mode;
}

ColorVisionMode Renderer::getColorVisionMode() const
{
    return m_colorVisionMode;
}

// Phase 10 fog setters — CPU state only. Uniforms are pushed in
// endFrame() every frame (no dirty tracking; the cost is < 20 setInt calls).

void Renderer::setFogMode(FogMode mode)              { m_fogMode = mode; }
FogMode Renderer::getFogMode() const                 { return m_fogMode; }

void Renderer::setFogParams(const FogParams& params) { m_fogParams = params; }
const FogParams& Renderer::getFogParams() const      { return m_fogParams; }

void Renderer::setHeightFogEnabled(bool enabled)     { m_heightFogEnabled = enabled; }
bool Renderer::isHeightFogEnabled() const            { return m_heightFogEnabled; }

void Renderer::setHeightFogParams(const HeightFogParams& params)
{
    m_heightFogParams = params;
}
const HeightFogParams& Renderer::getHeightFogParams() const
{
    return m_heightFogParams;
}

void Renderer::setSunInscatterEnabled(bool enabled)  { m_sunInscatterEnabled = enabled; }
bool Renderer::isSunInscatterEnabled() const         { return m_sunInscatterEnabled; }

void Renderer::setSunInscatterParams(const SunInscatterParams& params)
{
    m_sunInscatterParams = params;
}
const SunInscatterParams& Renderer::getSunInscatterParams() const
{
    return m_sunInscatterParams;
}

void Renderer::setPostProcessAccessibility(const PostProcessAccessibilitySettings& settings)
{
    m_postProcessAccessibility = settings;
}

const PostProcessAccessibilitySettings& Renderer::getPostProcessAccessibility() const
{
    return m_postProcessAccessibility;
}

void Renderer::setPomEnabled(bool isEnabled)
{
    m_pomEnabled = isEnabled;
    Logger::debug(std::string("POM: ") + (isEnabled ? "ON" : "OFF"));
}

bool Renderer::isPomEnabled() const
{
    return m_pomEnabled;
}

void Renderer::setPomHeightMultiplier(float multiplier)
{
    if (multiplier < 0.0f)
    {
        multiplier = 0.0f;
    }
    if (multiplier > 3.0f)
    {
        multiplier = 3.0f;
    }
    m_pomHeightMultiplier = multiplier;
}

float Renderer::getPomHeightMultiplier() const
{
    return m_pomHeightMultiplier;
}

void Renderer::setBloomEnabled(bool isEnabled)
{
    m_bloomEnabled = isEnabled;
    Logger::debug(std::string("Bloom: ") + (isEnabled ? "ON" : "OFF"));
}

bool Renderer::isBloomEnabled() const
{
    return m_bloomEnabled;
}

void Renderer::setObjectMotionOverlayEnabled(bool isEnabled)
{
    m_objectMotionOverlayEnabled = isEnabled;
    Logger::info(std::string("Per-object motion vector overlay: ") +
                 (isEnabled ? "ON" : "OFF"));
}

bool Renderer::isObjectMotionOverlayEnabled() const
{
    return m_objectMotionOverlayEnabled;
}

void Renderer::setIblMultiplierOverride(float multiplier)
{
    m_iblMultiplierOverride = multiplier;
    if (multiplier < 0.0f)
    {
        Logger::info("IBL multiplier override: cleared (per-material values)");
    }
    else
    {
        Logger::info("IBL multiplier override: " + std::to_string(multiplier));
    }
}

float Renderer::getIblMultiplierOverride() const
{
    return m_iblMultiplierOverride;
}

void Renderer::setIblSubScales(float diffuseScale, float specularScale)
{
    m_sceneShader.use();
    m_sceneShader.setFloat("u_iblDiffuseScale", diffuseScale);
    m_sceneShader.setFloat("u_iblSpecularScale", specularScale);
    Logger::info("IBL sub-scales: diffuse=" + std::to_string(diffuseScale) +
                 " specular=" + std::to_string(specularScale));
}

void Renderer::setShGridForceDisabled(bool isDisabled)
{
    m_shGridForceDisabled = isDisabled;
    Logger::info(std::string("SH grid force-disabled: ") +
                 (isDisabled ? "ON (fallback to cubemap/sky)" : "OFF"));
}

bool Renderer::isShGridForceDisabled() const
{
    return m_shGridForceDisabled;
}

void Renderer::setBloomThreshold(float threshold)
{
    m_bloomThreshold = threshold;
}

float Renderer::getBloomThreshold() const
{
    return m_bloomThreshold;
}

void Renderer::setBloomIntensity(float intensity)
{
    m_bloomIntensity = intensity;
}

float Renderer::getBloomIntensity() const
{
    return m_bloomIntensity;
}

void Renderer::setPhotosensitive(bool enabled,
                                 const PhotosensitiveLimits& limits)
{
    m_photosensitiveEnabled = enabled;
    m_photosensitiveLimits  = limits;
}

void Renderer::setSsaoEnabled(bool isEnabled)
{
    m_ssaoEnabled = isEnabled;
    Logger::debug(std::string("SSAO: ") + (isEnabled ? "ON" : "OFF"));
}

bool Renderer::isSsaoEnabled() const
{
    return m_ssaoEnabled;
}

void Renderer::setAntiAliasMode(AntiAliasMode mode)
{
    m_antiAliasMode = mode;
    const char* names[] = {"None", "MSAA 4x", "TAA", "SMAA"};
    Logger::info("Anti-aliasing: " + std::string(names[static_cast<int>(mode)]));
}

AntiAliasMode Renderer::getAntiAliasMode() const
{
    return m_antiAliasMode;
}

const Renderer::CullingStats& Renderer::getCullingStats() const
{
    return m_cullingStats;
}

int Renderer::getPointLightCount() const
{
    return static_cast<int>(m_pointLights.size());
}

int Renderer::getSpotLightCount() const
{
    return static_cast<int>(m_spotLights.size());
}

GLuint Renderer::getOutputTextureId() const
{
    if (m_outputFbo)
    {
        return m_outputFbo->getColorAttachmentId();
    }
    return 0;
}

void Renderer::blitToScreen(int screenWidth, int screenHeight)
{
    if (!m_outputFbo)
    {
        return;
    }

    // Default: screen matches render resolution
    if (screenWidth <= 0) screenWidth = m_windowWidth;
    if (screenHeight <= 0) screenHeight = m_windowHeight;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_outputFbo->getId());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    if (screenWidth == m_windowWidth && screenHeight == m_windowHeight)
    {
        // Same size — no scaling needed
        glBlitFramebuffer(0, 0, m_windowWidth, m_windowHeight,
                          0, 0, screenWidth, screenHeight,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    else
    {
        // Scale render FBO to window with aspect-ratio-preserving letterboxing
        float renderAspect = static_cast<float>(m_windowWidth) / static_cast<float>(m_windowHeight);
        float screenAspect = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);

        int dstX = 0, dstY = 0, dstW = screenWidth, dstH = screenHeight;
        if (renderAspect > screenAspect)
        {
            // Render is wider — letterbox top/bottom
            dstH = static_cast<int>(static_cast<float>(screenWidth) / renderAspect);
            dstY = (screenHeight - dstH) / 2;
        }
        else if (renderAspect < screenAspect)
        {
            // Render is taller — pillarbox left/right
            dstW = static_cast<int>(static_cast<float>(screenHeight) * renderAspect);
            dstX = (screenWidth - dstW) / 2;
        }

        // Clear letterbox bars to black
        glViewport(0, 0, screenWidth, screenHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glBlitFramebuffer(0, 0, m_windowWidth, m_windowHeight,
                          dstX, dstY, dstX + dstW, dstY + dstH,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool Renderer::loadSkyboxHDRI(const std::string& path)
{
    if (!m_skybox)
    {
        Logger::error("Cannot load HDRI: skybox not initialized");
        return false;
    }

    if (!m_skybox->loadEquirectangular(path))
    {
        return false;
    }

    // Regenerate IBL environment maps from the new cubemap
    if (m_environmentMap && m_environmentMap->isReady())
    {
        Logger::info("Regenerating IBL environment maps from HDRI...");

        // Save GL state that generate() might corrupt
        GLint prevDepthFunc = 0;
        glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
        GLboolean prevDepthMask = GL_TRUE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
        GLint prevClipOrigin = 0;
        GLint prevClipDepth = 0;
        glGetIntegerv(GL_CLIP_ORIGIN, &prevClipOrigin);
        glGetIntegerv(GL_CLIP_DEPTH_MODE, &prevClipDepth);

        m_environmentMap->generate(m_skybox->getTextureId(), true,
                                   *m_screenQuad, m_skyboxShader);

        // Restore GL state — reverse-Z requires these to be correct
        glDepthFunc(static_cast<GLenum>(prevDepthFunc));
        glDepthMask(prevDepthMask);
        glClipControl(static_cast<GLenum>(prevClipOrigin), static_cast<GLenum>(prevClipDepth));
        glUseProgram(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // CRITICAL: generate() leaves a cubemap bound on texture unit 0.
        // On Mesa AMD, a subsequent sampler2D read from unit 0 triggers
        // GL_INVALID_OPERATION (mismatched sampler types), silently failing
        // all draw calls. Bind a 2D fallback texture to clear the cubemap.
        for (int i = 0; i < 10; i++)
        {
            glBindTextureUnit(static_cast<GLuint>(i), m_fallbackTexture);
        }

        // Drain any GL errors from IBL generation
        while (glGetError() != GL_NO_ERROR) {}
    }

    return true;
}

void Renderer::captureLightProbe(int probeIndex, const SceneRenderData& renderData,
                                  const Camera& camera, float /*aspectRatio*/)
{
    if (!m_lightProbeManager || probeIndex < 0
        || probeIndex >= m_lightProbeManager->getProbeCount())
    {
        Logger::error("captureLightProbe: invalid probe index " + std::to_string(probeIndex));
        return;
    }

    const LightProbe* probe = m_lightProbeManager->getProbe(probeIndex);
    glm::vec3 probePos = probe->getPosition();
    int faceSize = LightProbe::CAPTURE_RESOLUTION;

    // Create temporary cubemap to capture the scene into
    GLuint captureCubemap = 0;
    int mipLevels = static_cast<int>(std::floor(std::log2(faceSize))) + 1;
    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &captureCubemap);
    glTextureStorage2D(captureCubemap, mipLevels, GL_RGB16F, faceSize, faceSize);
    glTextureParameteri(captureCubemap, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(captureCubemap, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(captureCubemap, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(captureCubemap, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(captureCubemap, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    // Create temporary FBO for cubemap face rendering
    GLuint captureFbo = 0;
    GLuint captureRbo = 0;
    glCreateFramebuffers(1, &captureFbo);
    glCreateRenderbuffers(1, &captureRbo);
    glNamedRenderbufferStorage(captureRbo, GL_DEPTH_COMPONENT24, faceSize, faceSize);
    glNamedFramebufferRenderbuffer(captureFbo, GL_DEPTH_ATTACHMENT,
                                   GL_RENDERBUFFER, captureRbo);
    // Placeholder color attachment (face 0) so completeness can be checked
    // before the per-face render loop starts swapping layers.
    glNamedFramebufferTextureLayer(captureFbo, GL_COLOR_ATTACHMENT0,
                                   captureCubemap, 0, 0);
    {
        GLenum status = glCheckNamedFramebufferStatus(captureFbo, GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            Logger::error("Light probe capture FBO incomplete — status: 0x"
                + std::to_string(status));
            glDeleteTextures(1, &captureCubemap);
            glDeleteFramebuffers(1, &captureFbo);
            glDeleteRenderbuffers(1, &captureRbo);
            return;
        }
    }

    // Cubemap face view matrices (looking from probe position)
    static const glm::vec3 TARGETS[6] = {
        { 1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0,-1, 0}, {0, 0, 1}, {0, 0,-1}
    };
    static const glm::vec3 UPS[6] = {
        {0,-1, 0}, {0,-1, 0}, {0, 0, 1}, {0, 0,-1}, {0,-1, 0}, {0,-1, 0}
    };
    glm::mat4 captureProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);

    Logger::info("Capturing light probe " + std::to_string(probeIndex)
        + " at (" + std::to_string(probePos.x) + ", "
        + std::to_string(probePos.y) + ", "
        + std::to_string(probePos.z) + ")");

    // Save GL state
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    {
        ScopedForwardZ forwardZ;  // reverse-Z is restored on scope exit

        // Render each cubemap face using geometryOnly mode
        for (int face = 0; face < 6; face++)
        {
            glm::mat4 faceView = glm::lookAt(probePos, probePos + TARGETS[face], UPS[face]);

            glNamedFramebufferTextureLayer(captureFbo, GL_COLOR_ATTACHMENT0,
                                           captureCubemap, 0, face);
            glBindFramebuffer(GL_FRAMEBUFFER, captureFbo);
            glViewport(0, 0, faceSize, faceSize);

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // Render scene geometry with lighting (no shadows, no post-processing)
            renderScene(renderData, camera, 1.0f, glm::vec4(0.0f),
                        true,  // geometryOnly
                        faceView, captureProj);
        }
    }

    // Restore FBO + viewport (clip/depth already restored by ScopedForwardZ)
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

    // Generate mipmaps for the capture cubemap
    glGenerateTextureMipmap(captureCubemap);

    // Generate irradiance + prefilter from the captured cubemap
    m_lightProbeManager->generateProbe(probeIndex, captureCubemap);

    // Clean up the temporary capture cubemap and FBO
    glDeleteTextures(1, &captureCubemap);
    glDeleteFramebuffers(1, &captureFbo);
    glDeleteRenderbuffers(1, &captureRbo);

    // Drain GL errors and rebind fallback textures
    while (glGetError() != GL_NO_ERROR) {}
    for (int i = 0; i < 12; i++)
    {
        glBindTextureUnit(static_cast<GLuint>(i), m_fallbackTexture);
    }
    glBindTextureUnit(4, m_fallbackCubemap);
    glBindTextureUnit(5, m_fallbackCubemap);
    glBindTextureUnit(10, m_fallbackCubemap);
    glBindTextureUnit(11, m_fallbackCubemap);

    // AUDIT.md §L4 / FIXPLAN: also rebind 3D sampler fallbacks after a
    // capture so a subsequent capture (or immediate frame render) cannot
    // read stale SH probe grid state on units 17-23.
    for (int i = 0; i < SHProbeGrid::SH_TEXTURE_COUNT; i++)
    {
        glBindTextureUnit(static_cast<GLuint>(SHProbeGrid::FIRST_TEXTURE_UNIT + i), m_fallbackTex3D);
    }

    Logger::info("Light probe " + std::to_string(probeIndex) + " captured and convolved");
}

void Renderer::captureSHGrid(const SceneRenderData& renderData,
                              const Camera& camera, float /*aspectRatio*/,
                              int faceSize)
{
    if (!m_shProbeGrid || !m_shProbeGrid->isInitialized())
    {
        Logger::error("captureSHGrid: SH grid not initialized");
        return;
    }

    glm::ivec3 res = m_shProbeGrid->getResolution();
    glm::vec3 worldMin = m_shProbeGrid->getWorldMin();
    glm::vec3 worldMax = m_shProbeGrid->getWorldMax();
    glm::vec3 step = (worldMax - worldMin) / glm::vec3(glm::max(res - 1, glm::ivec3(1)));

    int totalProbes = res.x * res.y * res.z;

    Logger::info("Capturing SH probe grid: " + std::to_string(totalProbes) + " probes at "
        + std::to_string(faceSize) + "x" + std::to_string(faceSize) + " per face");

    // Create temporary capture cubemap + FBO
    GLuint captureCubemap = 0;
    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &captureCubemap);
    glTextureStorage2D(captureCubemap, 1, GL_RGB16F, faceSize, faceSize);
    glTextureParameteri(captureCubemap, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(captureCubemap, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLuint captureFbo = 0;
    GLuint captureRbo = 0;
    glCreateFramebuffers(1, &captureFbo);
    glCreateRenderbuffers(1, &captureRbo);
    glNamedRenderbufferStorage(captureRbo, GL_DEPTH_COMPONENT24, faceSize, faceSize);
    glNamedFramebufferRenderbuffer(captureFbo, GL_DEPTH_ATTACHMENT,
                                   GL_RENDERBUFFER, captureRbo);
    // Placeholder color attachment (face 0) so completeness can be checked
    // once before the triple-nested capture loop swaps layers per probe/face.
    glNamedFramebufferTextureLayer(captureFbo, GL_COLOR_ATTACHMENT0,
                                   captureCubemap, 0, 0);
    {
        GLenum status = glCheckNamedFramebufferStatus(captureFbo, GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            Logger::error("SH-grid capture FBO incomplete — status: 0x"
                + std::to_string(status));
            glDeleteTextures(1, &captureCubemap);
            glDeleteFramebuffers(1, &captureFbo);
            glDeleteRenderbuffers(1, &captureRbo);
            return;
        }
    }

    // Face view targets + ups
    static const glm::vec3 TARGETS[6] = {
        { 1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0,-1, 0}, {0, 0, 1}, {0, 0,-1}
    };
    static const glm::vec3 UPS[6] = {
        {0,-1, 0}, {0,-1, 0}, {0, 0, 1}, {0, 0,-1}, {0,-1, 0}, {0,-1, 0}
    };
    glm::mat4 captureProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);

    // R2 (Phase 10.9 Slice 4 — stepping-stone fix): replace the
    // per-face synchronous `glReadPixels` (one full GPU stall per
    // face × 6 faces × N probes) with a single batched async PBO
    // readback per probe. After all 6 faces of a probe's cubemap
    // are rendered, we issue 6 `glGetTextureSubImage` calls into
    // a single Pixel Pack Buffer (async DMA), then `glMapNamedBufferRange`
    // once to read all 6 faces' data on the CPU side. Net: 6 stalls
    // -> 1 stall per probe. The full GPU compute SH projection
    // (the original R2 ROADMAP intent) is deferred to a follow-up
    // session where a GL test harness can support its CPU-vs-GPU
    // parity test.
    const size_t cubemapByteCount = 6 * static_cast<size_t>(faceSize)
                                       * static_cast<size_t>(faceSize)
                                       * 3 * sizeof(float);
    GLuint readbackPbo = 0;
    glCreateBuffers(1, &readbackPbo);
    glNamedBufferStorage(readbackPbo, static_cast<GLsizeiptr>(cubemapByteCount),
                         nullptr, GL_MAP_READ_BIT | GL_DYNAMIC_STORAGE_BIT);

    // Save GL state
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    int captured = 0;
    {
        ScopedForwardZ forwardZ;  // reverse-Z is restored on scope exit

        for (int z = 0; z < res.z; z++)
        {
            for (int y = 0; y < res.y; y++)
            {
                for (int x = 0; x < res.x; x++)
                {
                    glm::vec3 probePos = worldMin + glm::vec3(x, y, z) * step;

                    // Render 6 cubemap faces from this position
                    for (int face = 0; face < 6; face++)
                    {
                        glm::mat4 faceView = glm::lookAt(probePos,
                            probePos + TARGETS[face], UPS[face]);

                        glNamedFramebufferTextureLayer(captureFbo, GL_COLOR_ATTACHMENT0,
                                                       captureCubemap, 0, face);
                        glBindFramebuffer(GL_FRAMEBUFFER, captureFbo);
                        glViewport(0, 0, faceSize, faceSize);

                        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                        renderScene(renderData, camera, 1.0f, glm::vec4(0.0f),
                                    true, faceView, captureProj);
                    }

                    // R2: batch all 6 face readbacks into one PBO via
                    // `glGetTextureSubImage` (async DMA, no stall per
                    // call). The single subsequent map call forces one
                    // sync — versus 6 syncs from per-face `glReadPixels`.
                    const size_t bytesPerFace = static_cast<size_t>(faceSize)
                                              * static_cast<size_t>(faceSize)
                                              * 3 * sizeof(float);
                    glBindBuffer(GL_PIXEL_PACK_BUFFER, readbackPbo);
                    for (size_t face = 0; face < 6; face++)
                    {
                        const GLintptr byteOffset = static_cast<GLintptr>(face * bytesPerFace);
                        glGetTextureSubImage(captureCubemap, /*level=*/0,
                                             /*xoff=*/0, /*yoff=*/0, /*zoff=*/static_cast<GLint>(face),
                                             faceSize, faceSize, /*depth=*/1,
                                             GL_RGB, GL_FLOAT,
                                             static_cast<GLsizei>(bytesPerFace),
                                             reinterpret_cast<void*>(byteOffset));
                    }
                    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

                    auto* mappedData = static_cast<const float*>(
                        glMapNamedBufferRange(readbackPbo, 0,
                                              static_cast<GLsizeiptr>(cubemapByteCount),
                                              GL_MAP_READ_BIT));
                    if (!mappedData)
                    {
                        Logger::warning("captureSHGrid: PBO map failed at probe ("
                            + std::to_string(x) + "," + std::to_string(y) + ","
                            + std::to_string(z) + "); skipping");
                        continue;
                    }

                    // Project cubemap to SH (and apply CPU pipeline). The
                    // helper shape is shared with the unit tests so the
                    // bug-surface that R7 closed (double-A_ℓ on the
                    // CPU side + RH Eq. 13 in the shader) stays
                    // pinned regression-wise.
                    glm::vec3 shCoeffs[9];
                    SHProbeGrid::computeProbeShFromCubemap(mappedData,
                                                            faceSize, shCoeffs);
                    m_shProbeGrid->setProbeIrradiance(x, y, z, shCoeffs);

                    glUnmapNamedBuffer(readbackPbo);

                    captured++;
                }
            }
        }
    }

    // Restore FBO + viewport (clip/depth already restored by ScopedForwardZ)
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

    // Clean up temporary resources
    glDeleteBuffers(1, &readbackPbo);
    glDeleteTextures(1, &captureCubemap);
    glDeleteFramebuffers(1, &captureFbo);
    glDeleteRenderbuffers(1, &captureRbo);

    // Upload SH data to GPU
    m_shProbeGrid->upload();

    // Drain GL errors and rebind fallbacks
    while (glGetError() != GL_NO_ERROR) {}
    for (int i = 0; i < 12; i++)
    {
        glBindTextureUnit(static_cast<GLuint>(i), m_fallbackTexture);
    }
    glBindTextureUnit(4, m_fallbackCubemap);
    glBindTextureUnit(5, m_fallbackCubemap);
    glBindTextureUnit(10, m_fallbackCubemap);
    glBindTextureUnit(11, m_fallbackCubemap);

    // AUDIT.md §L4 / FIXPLAN: rebind 3D sampler fallbacks so a subsequent
    // capture or frame render cannot read stale SH probe grid state.
    for (int i = 0; i < SHProbeGrid::SH_TEXTURE_COUNT; i++)
    {
        glBindTextureUnit(static_cast<GLuint>(SHProbeGrid::FIRST_TEXTURE_UNIT + i), m_fallbackTex3D);
    }

    Logger::info("SH probe grid captured: " + std::to_string(captured)
        + " probes projected to L2 SH");
}

GLuint Renderer::getSkyboxTextureId() const
{
    return m_skybox ? m_skybox->getTextureId() : 0;
}

GLuint Renderer::getResolvedDepthTexture() const
{
    return m_resolveDepthFbo ? m_resolveDepthFbo->getDepthTextureId() : 0;
}

GLuint Renderer::getResolvedColorTexture() const
{
    return m_resolveFbo ? m_resolveFbo->getColorAttachmentId() : 0;
}

void Renderer::resolveSceneForWater()
{
    bool isTAA = (m_antiAliasMode == AntiAliasMode::TAA && m_taa && m_taaSceneFbo);
    bool isSMAA = (m_antiAliasMode == AntiAliasMode::SMAA && m_smaa && m_taaSceneFbo);
    bool usesNonMsaaFbo = (isTAA || isSMAA);

    // Resolve color: scene FBO → resolve FBO
    if (usesNonMsaaFbo)
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_taaSceneFbo->getId());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFbo->getId());
        glBlitFramebuffer(0, 0, m_windowWidth, m_windowHeight,
                          0, 0, m_windowWidth, m_windowHeight,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    else if (m_msaaFbo)
    {
        m_msaaFbo->resolve(*m_resolveFbo);
    }

    // Resolve depth: scene FBO → resolve depth FBO
    GLuint depthSourceFbo = usesNonMsaaFbo ? m_taaSceneFbo->getId()
                                            : (m_msaaFbo ? m_msaaFbo->getId() : 0);
    if (m_resolveDepthFbo && depthSourceFbo != 0)
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, depthSourceFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveDepthFbo->getId());
        glBlitFramebuffer(0, 0, m_windowWidth, m_windowHeight,
                          0, 0, m_windowWidth, m_windowHeight,
                          GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // Rebind the scene FBO so rendering can continue (water surface, particles)
    rebindSceneFbo();
}

CascadedShadowMap* Renderer::getCascadedShadowMap() const
{
    return m_cascadedShadowMap.get();
}

void Renderer::setFoliageShadowCaster(FoliageRenderer* foliageRenderer,
                                       FoliageManager* foliageManager)
{
    m_foliageShadowCaster = foliageRenderer;
    m_foliageShadowManager = foliageManager;
}

TextRenderer* Renderer::getTextRenderer()
{
    return m_textRenderer.get();
}

bool Renderer::initTextRenderer(const std::string& fontPath, const std::string& assetPath)
{
    m_textRenderer = std::make_unique<TextRenderer>();
    if (!m_textRenderer->initialize(fontPath, assetPath))
    {
        m_textRenderer.reset();
        return false;
    }
    return true;
}

void Renderer::setColorGradingEnabled(bool enabled)
{
    if (m_colorGradingLut)
    {
        m_colorGradingLut->setEnabled(enabled);
    }
}

bool Renderer::isColorGradingEnabled() const
{
    return m_colorGradingLut && m_colorGradingLut->isEnabled();
}

void Renderer::nextColorGradingPreset()
{
    if (m_colorGradingLut)
    {
        m_colorGradingLut->nextPreset();
    }
}

std::string Renderer::getColorGradingPresetName() const
{
    if (m_colorGradingLut)
    {
        return m_colorGradingLut->getCurrentPresetName();
    }
    return "None";
}

void Renderer::setColorGradingIntensity(float intensity)
{
    if (m_colorGradingLut)
    {
        m_colorGradingLut->setIntensity(intensity);
    }
}

float Renderer::getColorGradingIntensity() const
{
    if (m_colorGradingLut)
    {
        return m_colorGradingLut->getIntensity();
    }
    return 1.0f;
}

bool Renderer::loadColorGradingLut(const std::string& filePath, const std::string& name)
{
    if (m_colorGradingLut)
    {
        return m_colorGradingLut->loadCubeFile(filePath, name);
    }
    return false;
}

void Renderer::setDirectionalLightEnabled(bool isEnabled)
{
    m_hasDirectionalLight = isEnabled;
}

void Renderer::setCascadeDebug(bool enabled)
{
    m_cascadeDebug = enabled;
}

bool Renderer::isCascadeDebug() const
{
    return m_cascadeDebug;
}

void Renderer::setSdsmEnabled(bool enabled)
{
    m_sdsmEnabled = enabled;
    Logger::info(std::string("SDSM: ") + (enabled ? "ON" : "OFF"));
    if (!enabled && m_cascadedShadowMap)
    {
        m_cascadedShadowMap->clearDepthBounds();
    }
}

bool Renderer::isSdsmEnabled() const
{
    return m_sdsmEnabled;
}

/// Pre-built uniform name strings for cascade shadow uniforms (avoids per-frame string allocations).
struct CascadeNames
{
    std::string splits, lightSpaceMatrices;
};

static const std::array<CascadeNames, 4>& getCascadeNames()
{
    static const auto names = []()
    {
        std::array<CascadeNames, 4> arr;
        for (int i = 0; i < 4; i++)
        {
            std::string idx = "[" + std::to_string(i) + "]";
            arr[static_cast<size_t>(i)] = {
                "u_cascadeSplits" + idx,
                "u_cascadeLightSpaceMatrices" + idx
            };
        }
        return arr;
    }();
    return names;
}

/// Pre-built uniform name strings for point shadow uniforms.
struct PointShadowNames
{
    std::string maps, indices, farPlane;
};

static const std::array<PointShadowNames, 2>& getPointShadowNames()
{
    static const auto names = []()
    {
        std::array<PointShadowNames, 2> arr;
        for (int i = 0; i < 2; i++)
        {
            std::string idx = "[" + std::to_string(i) + "]";
            arr[static_cast<size_t>(i)] = {
                "u_pointShadowMaps" + idx,
                "u_pointShadowIndices" + idx,
                "u_pointShadowFarPlane" + idx
            };
        }
        return arr;
    }();
    return names;
}

std::vector<Renderer::InstanceBatch> Renderer::buildInstanceBatchesStatic(
    const std::vector<SceneRenderData::RenderItem>& items)
{
    struct PairHash
    {
        size_t operator()(const std::pair<const Mesh*, const Material*>& p) const
        {
            size_t h1 = std::hash<const void*>{}(p.first);
            size_t h2 = std::hash<const void*>{}(p.second);
            return h1 ^ (h2 * 2654435761u);
        }
    };

    std::unordered_map<std::pair<const Mesh*, const Material*>, size_t, PairHash> indexMap;
    std::vector<InstanceBatch> batches;

    for (const auto& item : items)
    {
        auto key = std::make_pair(item.mesh, item.material);
        auto it = indexMap.find(key);
        if (it != indexMap.end())
        {
            batches[it->second].modelMatrices.push_back(item.worldMatrix);
            batches[it->second].boneMatrixPtrs.push_back(item.boneMatrices);
            batches[it->second].morphWeightPtrs.push_back(item.morphWeights);
        }
        else
        {
            indexMap[key] = batches.size();
            InstanceBatch batch;
            batch.mesh = item.mesh;
            batch.material = item.material;
            batch.modelMatrices.push_back(item.worldMatrix);
            batch.boneMatrixPtrs.push_back(item.boneMatrices);
            batch.morphWeightPtrs.push_back(item.morphWeights);
            batches.push_back(std::move(batch));
        }
    }

    return batches;
}

void Renderer::buildInstanceBatches(
    const std::vector<SceneRenderData::RenderItem>& items)
{
    m_batchIndexMap.clear();
    m_batchIndexMap.reserve(items.size());
    m_instanceBatchCount = 0;

    for (const auto& item : items)
    {
        auto key = std::make_pair(item.mesh, item.material);
        auto it = m_batchIndexMap.find(key);
        if (it != m_batchIndexMap.end())
        {
            m_instanceBatches[it->second].modelMatrices.push_back(item.worldMatrix);
            m_instanceBatches[it->second].boneMatrixPtrs.push_back(item.boneMatrices);
            m_instanceBatches[it->second].morphWeightPtrs.push_back(item.morphWeights);
        }
        else
        {
            size_t idx = m_instanceBatchCount;
            m_batchIndexMap[key] = idx;

            if (idx < m_instanceBatches.size())
            {
                // Reuse existing batch (retains modelMatrices capacity)
                m_instanceBatches[idx].mesh = item.mesh;
                m_instanceBatches[idx].material = item.material;
                m_instanceBatches[idx].modelMatrices.clear();
                m_instanceBatches[idx].modelMatrices.push_back(item.worldMatrix);
                m_instanceBatches[idx].boneMatrixPtrs.clear();
                m_instanceBatches[idx].boneMatrixPtrs.push_back(item.boneMatrices);
                m_instanceBatches[idx].morphWeightPtrs.clear();
                m_instanceBatches[idx].morphWeightPtrs.push_back(item.morphWeights);
            }
            else
            {
                InstanceBatch batch;
                batch.mesh = item.mesh;
                batch.material = item.material;
                batch.modelMatrices.push_back(item.worldMatrix);
                batch.boneMatrixPtrs.push_back(item.boneMatrices);
                batch.morphWeightPtrs.push_back(item.morphWeights);
                m_instanceBatches.push_back(std::move(batch));
            }
            m_instanceBatchCount++;
        }
    }
}

void Renderer::rebindSceneFbo()
{
    bool isTAA = (m_antiAliasMode == AntiAliasMode::TAA && m_taa && m_taaSceneFbo);
    bool isSMAA = (m_antiAliasMode == AntiAliasMode::SMAA && m_smaa && m_taaSceneFbo);
    if (isTAA || isSMAA)
    {
        m_taaSceneFbo->bind();
    }
    else if (m_msaaFbo)
    {
        m_msaaFbo->bind();
    }
    glViewport(0, 0, m_windowWidth, m_windowHeight);
}

void Renderer::saveViewState()
{
    m_savedLastProjection = m_lastProjection;
    m_savedLastView = m_lastView;
    m_savedLastViewProjection = m_lastViewProjection;
}

void Renderer::restoreViewState()
{
    m_lastProjection = m_savedLastProjection;
    m_lastView = m_savedLastView;
    m_lastViewProjection = m_savedLastViewProjection;
}

void Renderer::renderScene(const SceneRenderData& renderData, const Camera& camera, float aspectRatio,
                            const glm::vec4& clipPlane, bool geometryOnly,
                            const glm::mat4& viewOverride, const glm::mat4& projOverride)
{
    // AUDIT.md §H15 / FIXPLAN G1: stash for endFrame()'s motion vector
    // overlay pass. Only the non-geometryOnly main render is used; capture
    // paths (light probes, SH grid) set geometryOnly=true and must not
    // overwrite the main frame's renderData pointer.
    if (!geometryOnly) m_currentRenderData = &renderData;

    bool hasOverrides = (viewOverride != glm::mat4(0.0f));

    if (!geometryOnly)
    {
        // Reset per-frame scratch allocator (all pmr::vectors from last frame are now invalid)
        resetFrameAllocator();

        // Reset per-frame stats
        m_cullingStats.drawCalls = 0;
        m_cullingStats.instanceBatches = 0;
    }

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

    // Compute shadow-casting point lights (needed for uniform upload even in geometry-only mode)
    selectShadowCastingPointLights();

    if (!geometryOnly)
    {
        // Build shadow caster list (filter out non-casting items like ground planes)
        m_shadowCasterItems.clear();
        m_shadowCasterItems.reserve(renderData.renderItems.size());
        for (const auto& item : renderData.renderItems)
        {
            if (item.castsShadow)
            {
                m_shadowCasterItems.push_back(item);
            }
        }
        m_cullingStats.shadowCastersTotal = static_cast<int>(m_shadowCasterItems.size());

        // --- SDSM: read depth bounds from previous frame and update cascade distribution ---
        if (m_sdsmEnabled && m_depthReducer && m_cascadedShadowMap)
        {
            float depthNear = 0.0f;
            float depthFar = 0.0f;
            if (m_depthReducer->readBounds(0.1f, depthNear, depthFar))
            {
                // Smooth transitions between frames to avoid shadow popping
                constexpr float LERP_SPEED = 0.15f;
                m_sdsmNear += (depthNear - m_sdsmNear) * LERP_SPEED;
                m_sdsmFar += (depthFar - m_sdsmFar) * LERP_SPEED;

                // Enforce a minimum depth range so cascades don't degenerate into
                // paper-thin slices when looking at nearby flat surfaces. Without
                // this, each cascade covers too little depth and the bounding sphere
                // in light-space becomes tiny, missing nearby shadow casters.
                constexpr float MIN_DEPTH_RANGE = 15.0f;
                if (m_sdsmFar - m_sdsmNear < MIN_DEPTH_RANGE)
                {
                    m_sdsmFar = m_sdsmNear + MIN_DEPTH_RANGE;
                }

                m_cascadedShadowMap->setDepthBounds(m_sdsmNear, m_sdsmFar);
            }
            else
            {
                m_cascadedShadowMap->clearDepthBounds();
            }
        }
        else if (m_cascadedShadowMap)
        {
            m_cascadedShadowMap->clearDepthBounds();
        }

        // --- Directional shadow pass (cascaded, per-cascade frustum culled) ---
        if (m_cascadedShadowMap && m_hasDirectionalLight)
        {
            renderShadowPass(m_shadowCasterItems, camera, aspectRatio);
        }

        // --- Point light shadow pass (uses all shadow casters — omnidirectional) ---
        buildInstanceBatches(m_shadowCasterItems);
        renderPointShadowPass(m_shadowCasters);
    }

    // --- Frustum cull for scene pass ---
    // Use the standard (non-reverse-Z) projection for frustum extraction so all
    // 6 planes are well-defined. The reverse-Z infinite projection produces a
    // degenerate near plane that breaks the Gribb-Hartmann extraction.
    glm::mat4 cullingVP = hasOverrides
        ? (projOverride * viewOverride)
        : (camera.getCullingProjectionMatrix(aspectRatio) * camera.getViewMatrix());
    auto frustumPlanes = extractFrustumPlanes(cullingVP);

    // Filter render items to only those inside the view frustum.
    // Items with zero-size bounds (no explicit AABB set) are always included —
    // it is safer to overdraw than to incorrectly cull visible geometry.
    m_culledItems.clear();
    m_culledItems.reserve(renderData.renderItems.size());
    for (const auto& item : renderData.renderItems)
    {
        if (item.worldBounds.getSize() == glm::vec3(0.0f)
            || isAabbInFrustum(item.worldBounds, frustumPlanes))
        {
            m_culledItems.push_back(item);
        }
    }

    // Update culling statistics
    m_cullingStats.totalItems = static_cast<int>(renderData.renderItems.size());
    m_cullingStats.culledItems = static_cast<int>(m_culledItems.size());
    m_cullingStats.transparentTotal = static_cast<int>(renderData.transparentItems.size());

    // Sort culled items front-to-back for early-Z efficiency
    glm::vec3 camPos = camera.getPosition();
    std::sort(m_culledItems.begin(), m_culledItems.end(),
        [&camPos](const SceneRenderData::RenderItem& a, const SceneRenderData::RenderItem& b)
        {
            glm::vec3 da = glm::vec3(a.worldMatrix[3]) - camPos;
            glm::vec3 db = glm::vec3(b.worldMatrix[3]) - camPos;
            return glm::dot(da, da) < glm::dot(db, db);  // Nearest first
        });

    // Build batches from culled+sorted items
    buildInstanceBatches(m_culledItems);

    // Re-bind the scene FBO after shadow passes (skip in geometry-only mode —
    // the caller has already bound the target FBO and we must not overwrite it)
    if (!geometryOnly)
    {
        bool isTAA = (m_antiAliasMode == AntiAliasMode::TAA && m_taa && m_taaSceneFbo);
        bool isSMAAScene = (m_antiAliasMode == AntiAliasMode::SMAA && m_smaa && m_taaSceneFbo);
        if (isTAA || isSMAAScene)
        {
            m_taaSceneFbo->bind();
        }
        else if (m_msaaFbo)
        {
            m_msaaFbo->bind();
        }
        glViewport(0, 0, m_windowWidth, m_windowHeight);
    }

    // --- Set cascaded shadow uniforms for the lighting pass ---
    m_sceneShader.use();

    // ALWAYS bind CSM texture to unit 3 (Mesa requires valid textures for declared samplers)
    if (m_cascadedShadowMap)
    {
        m_cascadedShadowMap->bindShadowTexture(3);
        m_sceneShader.setInt("u_cascadeShadowMap", 3);
    }

    // Set shadow state and cascade uniforms
    if (m_cascadedShadowMap && m_hasDirectionalLight)
    {
        int cascadeCount = m_cascadedShadowMap->getCascadeCount();
        m_sceneShader.setInt("u_cascadeCount", cascadeCount);
        const auto& cascadeNames = getCascadeNames();
        for (int i = 0; i < cascadeCount; i++)
        {
            m_sceneShader.setFloat(cascadeNames[static_cast<size_t>(i)].splits,
                m_cascadedShadowMap->getCascadeSplit(i));
            m_sceneShader.setMat4(cascadeNames[static_cast<size_t>(i)].lightSpaceMatrices,
                m_cascadedShadowMap->getLightSpaceMatrix(i));
        }
        m_sceneShader.setBool("u_hasShadows", true);
        m_sceneShader.setBool("u_cascadeDebug", m_cascadeDebug);
        m_sceneShader.setFloat("u_shadowLightSize", 2.0f);  // PCSS light size (texels, reduced for thin walls)
    }
    else
    {
        m_sceneShader.setBool("u_hasShadows", false);
        m_sceneShader.setBool("u_cascadeDebug", false);
    }

    // --- Set point shadow uniforms (reusing precomputed m_shadowCasters) ---
    int pointShadowCount = static_cast<int>(m_shadowCasters.size());
    m_sceneShader.setInt("u_pointShadowCount", pointShadowCount);

    const auto& pointShadowNames = getPointShadowNames();
    for (int i = 0; i < pointShadowCount; i++)
    {
        int textureUnit = 4 + i;  // Units 4-5 for point shadow cubemaps
        m_pointShadowMaps[static_cast<size_t>(i)]->bindShadowTexture(textureUnit);
        m_sceneShader.setInt(pointShadowNames[static_cast<size_t>(i)].maps, textureUnit);
        m_sceneShader.setInt(pointShadowNames[static_cast<size_t>(i)].indices, m_shadowCasters[static_cast<size_t>(i)]);
        m_sceneShader.setFloat(pointShadowNames[static_cast<size_t>(i)].farPlane,
            m_pointShadowMaps[static_cast<size_t>(i)]->getConfig().farPlane);
    }

    // --- Set IBL uniforms ---
    // ALWAYS bind IBL textures (Mesa requires valid textures for declared samplers)
    if (m_environmentMap)
    {
        m_environmentMap->bindIrradiance(14);
        m_sceneShader.setInt("u_irradianceMap", 14);
        m_environmentMap->bindPrefilter(15);
        m_sceneShader.setInt("u_prefilterMap", 15);
        m_environmentMap->bindBrdfLut(16);
        m_sceneShader.setInt("u_brdfLUT", 16);
        m_sceneShader.setFloat("u_maxPrefilterLod",
            static_cast<float>(EnvironmentMap::MAX_MIP_LEVELS - 1));

        // Only enable IBL shading when textures are fully generated
        m_sceneShader.setBool("u_hasIBL", m_environmentMap->isReady());

        // Probe defaults: no probe active (overridden per-entity in draw loop)
        m_sceneShader.setBool("u_hasProbe", false);
        m_sceneShader.setFloat("u_probeWeight", 0.0f);

        // SH probe grid: bind textures and set uniforms if ready
        if (m_shProbeGrid && m_shProbeGrid->isReady() && !m_shGridForceDisabled)
        {
            m_shProbeGrid->bind();
            m_sceneShader.setBool("u_hasSHGrid", true);
            m_sceneShader.setVec3("u_shGridWorldMin", m_shProbeGrid->getWorldMin());
            m_sceneShader.setVec3("u_shGridWorldMax", m_shProbeGrid->getWorldMax());
            m_sceneShader.setFloat("u_shNormalBias", m_shNormalBias);
        }
        else
        {
            m_sceneShader.setBool("u_hasSHGrid", false);
        }
    }
    else
    {
        // No environment map at all — bind fallback textures to satisfy Mesa
        // (Mesa AMD requires ALL declared samplers to have valid textures bound)
        glBindTextureUnit(14, m_fallbackCubemap);
        m_sceneShader.setInt("u_irradianceMap", 14);
        glBindTextureUnit(15, m_fallbackCubemap);
        m_sceneShader.setInt("u_prefilterMap", 15);
        glBindTextureUnit(16, m_fallbackTexture);
        m_sceneShader.setInt("u_brdfLUT", 16);
        m_sceneShader.setFloat("u_maxPrefilterLod", 0.0f);
        m_sceneShader.setBool("u_hasIBL", false);
    }

    // Store projection matrix for SSAO
    // Use overrides if provided (non-zero), otherwise compute from camera
    glm::mat4 projection = hasOverrides ? projOverride : camera.getProjectionMatrix(aspectRatio);

    // Apply TAA jitter to projection (only for main pass, not cubemap faces)
    if (!hasOverrides && m_antiAliasMode == AntiAliasMode::TAA && m_taa)
    {
        projection = m_taa->jitterProjection(projection, m_windowWidth, m_windowHeight);
    }
    m_lastProjection = projection;
    m_lastView = hasOverrides ? viewOverride : camera.getViewMatrix();
    m_lastViewProjection = projection * m_lastView;

    // Cache camera world position for the fog composite pass in endFrame().
    // When an override view is in use (e.g. cubemap-face capture) we
    // still want the pre-override camera, because fog is never run
    // during those offline captures — the state only matters for the
    // main composite and gets refreshed on the next real renderScene().
    if (!hasOverrides)
    {
        m_cameraWorldPosition = camera.getPosition();
    }

    // Upload light uniforms once per frame (not per batch)
    uploadLightUniforms(camera);

    // Water clip plane for reflection/refraction passes (vec4(0) = disabled)
    m_sceneShader.setVec4("u_clipPlane", clipPlane);

    // Water caustics — bind texture and set uniforms for underwater geometry
    m_sceneShader.setBool("u_causticsEnabled", m_causticsEnabled);
    if (m_causticsEnabled && m_causticsTexture != 0)
    {
        glBindTextureUnit(9, m_causticsTexture);
        m_sceneShader.setInt("u_causticsTex", 9);
        m_sceneShader.setFloat("u_causticsScale", m_causticsScale);
        m_sceneShader.setFloat("u_causticsIntensity", m_causticsIntensity);
        m_sceneShader.setFloat("u_causticsTime", m_causticsTime);
        m_sceneShader.setFloat("u_waterY", m_causticsWaterY);
        m_sceneShader.setVec2("u_waterCenter", m_causticsCenter);
        m_sceneShader.setVec2("u_waterHalfExtent", m_causticsHalfExtent);
        m_sceneShader.setInt("u_causticsQuality", m_causticsQuality);
    }
    else
    {
        // Mesa requires valid textures for declared samplers
        glBindTextureUnit(9, m_causticsTexture != 0 ? m_causticsTexture : m_fallbackTexture);
        m_sceneShader.setInt("u_causticsTex", 9);
    }

    // --- Scene pass: draw all opaque render items ---
    // MDI path: group batches by material, issue one glMultiDrawElementsIndirect per group.
    // Falls back to legacy instanced/single-draw when MDI is unavailable.
    if (m_mdiEnabled && m_meshPool && m_meshPool->hasData() && m_indirectBuffer)
    {
        // Group batches by material pointer. Clear each inner vector (preserving
        // its capacity) instead of clearing the whole map — the outer map erase
        // would free every per-material vector's heap buffer, forcing fresh
        // allocations on every push_back below each frame. (AUDIT H8.)
        for (auto& [material, batchPtrs] : m_materialGroups)
        {
            batchPtrs.clear();
        }
        for (size_t b = 0; b < m_instanceBatchCount; b++)
        {
            m_materialGroups[m_instanceBatches[b].material].push_back(&m_instanceBatches[b]);
        }

        m_sceneShader.use();
        m_sceneShader.setBool("u_useInstancing", false);
        m_sceneShader.setBool("u_useMDI", true);
        m_sceneShader.setBool("u_hasBones", false);  // MDI path doesn't support skinning
        m_sceneShader.setMat4("u_view", camera.getViewMatrix());
        m_sceneShader.setMat4("u_projection", m_lastProjection);

        m_meshPool->bind();

        for (const auto& [material, batchPtrs] : m_materialGroups)
        {
            // Build indirect commands for this material group
            m_indirectBuffer->clear();
            for (const auto* batchPtr : batchPtrs)
            {
                if (m_meshPool->hasMesh(batchPtr->mesh))
                {
                    MeshPoolEntry entry = m_meshPool->getEntry(batchPtr->mesh);
                    m_indirectBuffer->addCommand(entry, batchPtr->modelMatrices);
                }
            }
            m_indirectBuffer->upload();

            if (m_indirectBuffer->getCommandCount() > 0)
            {
                uploadMaterialUniforms(*material);

                bool doubleSided = material->isDoubleSided();
                if (doubleSided) glDisable(GL_CULL_FACE);
                if (m_isWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

                m_indirectBuffer->draw();
                m_cullingStats.drawCalls += m_indirectBuffer->getCommandCount();
                m_cullingStats.instanceBatches += m_indirectBuffer->getCommandCount();

                if (m_isWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                if (doubleSided) glEnable(GL_CULL_FACE);
            }
        }

        m_meshPool->unbind();
        m_sceneShader.setBool("u_useMDI", false);

        // Unbind SSBO
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
    }
    else
    {
        // Legacy path: instanced or single-draw per batch
        for (size_t b = 0; b < m_instanceBatchCount; b++)
        {
            const auto& batch = m_instanceBatches[b];
            int count = static_cast<int>(batch.modelMatrices.size());

            // Light probe assignment: use first entity position to determine probe for entire batch.
            // Indoor/outdoor meshes use different materials → different batches → correct probe per batch.
            if (m_lightProbeManager && m_lightProbeManager->getProbeCount() > 0)
            {
                glm::vec3 batchPos = glm::vec3(batch.modelMatrices[0][3]);
                auto assignment = m_lightProbeManager->assignProbe(batchPos);
                if (assignment.probe && assignment.weight > 0.0f)
                {
                    assignment.probe->bindIrradiance(10);
                    assignment.probe->bindPrefilter(11);
                    m_sceneShader.setBool("u_hasProbe", true);
                    m_sceneShader.setFloat("u_probeWeight", assignment.weight);
                }
                else
                {
                    glBindTextureUnit(10, m_fallbackCubemap);
                    glBindTextureUnit(11, m_fallbackCubemap);
                    m_sceneShader.setBool("u_hasProbe", false);
                    m_sceneShader.setFloat("u_probeWeight", 0.0f);
                }
            }

            if (count >= MIN_INSTANCE_BATCH_SIZE && m_instanceBuffer)
            {
                // Instanced path: set up material once, draw all instances
                m_sceneShader.use();
                m_sceneShader.setBool("u_useInstancing", true);
                m_sceneShader.setBool("u_useMDI", false);
                m_sceneShader.setBool("u_hasBones", false);  // Instancing doesn't support skinning

                // Upload instance matrices and bind to mesh VAO
                m_instanceBuffer->upload(batch.modelMatrices);
                batch.mesh->setupInstanceAttributes(m_instanceBuffer->getHandle());

                // Set a dummy u_model (unused but prevents warnings)
                m_sceneShader.setMat4("u_model", batch.modelMatrices[0]);
                m_sceneShader.setMat4("u_view", m_lastView);
                m_sceneShader.setMat4("u_projection", m_lastProjection);

                uploadMaterialUniforms(*batch.material);

                bool doubleSided = batch.material->isDoubleSided();
                if (doubleSided) glDisable(GL_CULL_FACE);
                if (m_isWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

                batch.mesh->bind();
                glDrawElementsInstanced(GL_TRIANGLES,
                    static_cast<GLsizei>(batch.mesh->getIndexCount()),
                    GL_UNSIGNED_INT, nullptr, count);
                m_cullingStats.drawCalls++;
                m_cullingStats.instanceBatches++;
                batch.mesh->unbind();

                if (m_isWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                if (doubleSided) glEnable(GL_CULL_FACE);

                m_sceneShader.setBool("u_useInstancing", false);
            }
            else
            {
                // Non-instanced path for single-instance batches
                for (size_t mi = 0; mi < batch.modelMatrices.size(); mi++)
                {
                    auto* bones = (mi < batch.boneMatrixPtrs.size()) ? batch.boneMatrixPtrs[mi] : nullptr;
                    auto* mwPtr = (mi < batch.morphWeightPtrs.size()) ? batch.morphWeightPtrs[mi] : nullptr;
                    const float* mw = (mwPtr && !mwPtr->empty()) ? mwPtr->data() : nullptr;
                    int mwc = mw ? static_cast<int>(mwPtr->size()) : 0;
                    drawMesh(*batch.mesh, batch.modelMatrices[mi], *batch.material,
                             camera, aspectRatio, bones, mw, mwc);
                }
            }
        }
    }

    // --- Cloth pass: draw dynamic cloth meshes (same shader as opaques) ---
    for (const auto& clothItem : renderData.clothItems)
    {
        if (!isAabbInFrustum(clothItem.worldBounds, frustumPlanes))
        {
            continue;
        }

        m_sceneShader.use();
        m_sceneShader.setBool("u_useInstancing", false);
        m_sceneShader.setBool("u_useMDI", false);
        m_sceneShader.setMat4("u_model", clothItem.worldMatrix);
        m_sceneShader.setMat3("u_normalMatrix",
                              computeNormalMatrix(clothItem.worldMatrix));
        m_sceneShader.setMat4("u_view", camera.getViewMatrix());
        m_sceneShader.setMat4("u_projection", m_lastProjection);
        m_sceneShader.setBool("u_hasBones", false);

        uploadMaterialUniforms(*clothItem.material);

        bool doubleSided = clothItem.material->isDoubleSided();
        if (doubleSided) glDisable(GL_CULL_FACE);
        if (m_isWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        clothItem.mesh->bind();
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(clothItem.mesh->getIndexCount()),
                       GL_UNSIGNED_INT, nullptr);
        m_cullingStats.drawCalls++;
        clothItem.mesh->unbind();

        if (m_isWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        if (doubleSided) glEnable(GL_CULL_FACE);
    }

    // --- Skybox pass: draw after opaque geometry, before transparent ---
    //
    // AUDIT.md §H18 (Z-convention-aware skybox; supersedes §M14 gate).
    // Previously the skybox vertex shader hard-coded `gl_Position.z = 0`,
    // which is the far plane in reverse-Z (main render) but the MIDDLE of
    // the depth buffer in forward-Z (capture paths). The §M14 fix worked
    // around that by gating the skybox out of geometryOnly captures, but
    // that left the SH probe-grid radiosity bake without a sky direct
    // contribution — multi-bounce on high-albedo materials (white linen,
    // gold) then compounded 60-70% per bounce instead of converging to a
    // sky-bounded equilibrium, blowing every textured surface to white.
    //
    // The skybox vertex shader now reads u_skyboxFarDepth and emits
    // z = u_skyboxFarDepth * w, so z/w = u_skyboxFarDepth after the
    // perspective divide. The right value depends on which Z convention
    // is active for THIS render:
    //   reverse-Z (main render, GL_GEQUAL, cleared 0): u_skyboxFarDepth = 0
    //   forward-Z (capture passes, GL_LESS,  cleared 1): u_skyboxFarDepth ≈ 1
    // For forward-Z we use 0.99999 (not exactly 1.0) so GL_LESS still
    // passes against the cleared-far buffer (0.99999 < 1.0 = true) but
    // fails against any opaque geometry (which has depth < 0.99999 in
    // forward-Z = closer than the far plane).
    //
    // `geometryOnly` is the cleanest proxy for the capture path — both
    // captureLightProbe and captureSHGrid call renderScene with
    // geometryOnly=true under forward-Z; main rendering uses reverse-Z.
    if (m_skybox && m_skyboxEnabled)
    {
        glDepthMask(GL_FALSE);   // Don't write to depth buffer
        glDisable(GL_CULL_FACE); // We're inside the cube — must see inner faces

        m_skyboxShader.use();
        m_skyboxShader.setMat4("u_view", camera.getViewMatrix());
        // Use the (potentially jittered) projection so TAA accumulates the skybox correctly
        m_skyboxShader.setMat4("u_projection", m_lastProjection);
        m_skyboxShader.setBool("u_hasCubemap", m_skybox->hasTexture());
        m_skyboxShader.setFloat("u_skyboxFarDepth", geometryOnly ? 0.99999f : 0.0f);

        if (m_skybox->hasTexture())
        {
            m_skyboxShader.setInt("u_skyboxTexture", 0);
        }
        else
        {
            // R6 Mesa fallback: u_skyboxTexture is samplerCube. The
            // procedural-skybox path doesn't bind a real cubemap, so
            // unit 0 may carry a sampler2D from a prior pass. Bind a
            // fallback samplerCube to satisfy Mesa's declared-sampler
            // check; the shader's procedural branch (`u_hasCubemap=false`)
            // never samples it.
            glBindTextureUnit(0, sharedSamplerFallback().getSamplerCube());
            m_skyboxShader.setInt("u_skyboxTexture", 0);
        }

        m_skybox->draw();

        glEnable(GL_CULL_FACE);  // Restore face culling
        glDepthMask(GL_TRUE);    // Restore depth writes
    }

    // --- Transparent pass: frustum cull + draw BLEND items back-to-front ---
    m_cullingStats.transparentCulled = 0;
    if (!renderData.transparentItems.empty())
    {
        // Frustum cull transparent items (reuse frustum planes from opaque pass)
        m_sortedTransparentItems.clear();
        m_sortedTransparentItems.reserve(renderData.transparentItems.size());
        for (const auto& item : renderData.transparentItems)
        {
            if (isAabbInFrustum(item.worldBounds, frustumPlanes))
            {
                m_sortedTransparentItems.push_back(item);
            }
        }
        m_cullingStats.transparentCulled = static_cast<int>(m_sortedTransparentItems.size());

        std::sort(m_sortedTransparentItems.begin(), m_sortedTransparentItems.end(),
            [&camPos](const SceneRenderData::RenderItem& a, const SceneRenderData::RenderItem& b)
            {
                glm::vec3 da = glm::vec3(a.worldMatrix[3]) - camPos;
                glm::vec3 db = glm::vec3(b.worldMatrix[3]) - camPos;
                return glm::dot(da, da) > glm::dot(db, db);  // Farthest first
            });

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);

        for (const auto& item : m_sortedTransparentItems)
        {
            const float* mw = (item.morphWeights && !item.morphWeights->empty())
                              ? item.morphWeights->data() : nullptr;
            int mwc = mw ? static_cast<int>(item.morphWeights->size()) : 0;
            drawMesh(*item.mesh, item.worldMatrix, *item.material, camera, aspectRatio,
                     item.boneMatrices, mw, mwc);
        }

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
}

void Renderer::renderShadowPass(const std::vector<SceneRenderData::RenderItem>& shadowCasterItems,
                                 const Camera& camera, float aspectRatio)
{
    // Shadow maps use standard forward-Z with [-1,1] NDC depth range.
    // Must restore GL_NEGATIVE_ONE_TO_ONE because glClipControl(GL_ZERO_TO_ONE)
    // clips z < 0 which discards half the shadow casters from glm::ortho's
    // [-1,1] range. This was the root cause of disappearing shadows at distance.
    ScopedForwardZ forwardZ;  // reverse-Z state is restored on function exit

    // Phase 10.9 R3: bracket GL_CLIP_DISTANCE0 (off — water passes may
    // have left it on; shaders that don't write gl_ClipDistance[0]
    // produce undefined clip values when enabled) and GL_DEPTH_CLAMP
    // (on — clamps shadow casters in front of the near plane to
    // depth 0 instead of clipping them, avoiding shadow pancaking).
    // The RAII restores both on function exit so the caller's prior
    // state is preserved.
    ScopedShadowDepthState shadowDepthState;

    // Update all cascade light-space matrices from the camera frustum
    m_cascadedShadowMap->update(m_directionalLight, camera, aspectRatio);

    m_shadowDepthShader.use();

    // Render geometry into each cascade layer with per-cascade frustum culling.
    // Far cascades update less frequently to reduce shadow pass cost:
    //   Cascade 0-1: every frame (near detail)
    //   Cascade 2:   every 2nd frame
    //   Cascade 3:   every 4th frame
    int cascadeCount = m_cascadedShadowMap->getCascadeCount();
    int totalCascadeCulled = 0;
    m_shadowFrameCount++;

    for (int c = 0; c < cascadeCount; c++)
    {
        // Skip the farthest cascade on alternating frames (its shadow map persists)
        if (c == 3 && (m_shadowFrameCount % 2) != 0) continue;
        const glm::mat4& lightSpaceMatrix = m_cascadedShadowMap->getLightSpaceMatrix(c);

        // Extract frustum planes from the cascade's orthographic light-space matrix
        auto cascadePlanes = extractFrustumPlanes(lightSpaceMatrix);

        // Cull shadow casters against this cascade's frustum
        m_cascadeCulledCasters.clear();
        m_cascadeCulledCasters.reserve(shadowCasterItems.size());
        for (const auto& item : shadowCasterItems)
        {
            if (item.worldBounds.getSize() == glm::vec3(0.0f)
                || isAabbInFrustum(item.worldBounds, cascadePlanes))
            {
                m_cascadeCulledCasters.push_back(item);
            }
        }
        totalCascadeCulled += static_cast<int>(m_cascadeCulledCasters.size());

        buildInstanceBatches(m_cascadeCulledCasters);

        m_cascadedShadowMap->beginCascade(c);
        m_shadowDepthShader.setMat4("u_lightSpaceMatrix", lightSpaceMatrix);

        for (size_t b = 0; b < m_instanceBatchCount; b++)
        {
            const auto& batch = m_instanceBatches[b];
            int count = static_cast<int>(batch.modelMatrices.size());
            if (count >= MIN_INSTANCE_BATCH_SIZE && m_instanceBuffer)
            {
                m_shadowDepthShader.setBool("u_useInstancing", true);
                m_instanceBuffer->upload(batch.modelMatrices);
                batch.mesh->setupInstanceAttributes(m_instanceBuffer->getHandle());

                batch.mesh->bind();
                glDrawElementsInstanced(GL_TRIANGLES,
                    static_cast<GLsizei>(batch.mesh->getIndexCount()),
                    GL_UNSIGNED_INT, nullptr, count);
                m_cullingStats.drawCalls++;
                batch.mesh->unbind();

                m_shadowDepthShader.setBool("u_useInstancing", false);
            }
            else
            {
                m_shadowDepthShader.setBool("u_useInstancing", false);
                for (size_t mi = 0; mi < batch.modelMatrices.size(); mi++)
                {
                    // Skeletal animation in shadow pass
                    const std::vector<glm::mat4>* bones =
                        (mi < batch.boneMatrixPtrs.size()) ? batch.boneMatrixPtrs[mi] : nullptr;
                    bool skinned = (bones != nullptr && !bones->empty());
                    m_shadowDepthShader.setBool("u_hasBones", skinned);
                    if (skinned)
                    {
                        glNamedBufferSubData(m_boneMatrixSSBO, 0,
                            static_cast<GLsizeiptr>(bones->size() * sizeof(glm::mat4)),
                            bones->data());
                        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_boneMatrixSSBO);
                    }

                    m_shadowDepthShader.setMat4("u_model", batch.modelMatrices[mi]);
                    batch.mesh->bind();
                    glDrawElements(GL_TRIANGLES,
                        static_cast<GLsizei>(batch.mesh->getIndexCount()),
                        GL_UNSIGNED_INT, nullptr);
                    m_cullingStats.drawCalls++;
                    batch.mesh->unbind();
                }
            }
        }

        // Render foliage shadows into every cascade so grass shadows are visible
        // at all camera angles. Use ALL chunks (not camera-frustum-culled) because
        // foliage behind the camera can cast shadows into the visible area. The
        // per-instance shadowMaxDistance culling keeps the count bounded.
        if (m_foliageShadowCaster && m_foliageShadowManager)
        {
            // Reuse the scratch vector across cascades to avoid per-cascade
            // heap allocation in the shadow pass hot path. (AUDIT H9.)
            m_foliageShadowManager->getAllChunks(m_scratchFoliageChunks);
            if (!m_scratchFoliageChunks.empty())
            {
                m_foliageShadowCaster->renderShadow(
                    m_scratchFoliageChunks, camera, lightSpaceMatrix,
                    m_foliageShadowTime);
                // Restore shadow depth shader — foliage shadow binds its own shader
                m_shadowDepthShader.use();
            }
        }

        m_cascadedShadowMap->endCascade();
    }

    // Average shadow casters across cascades for stats
    m_cullingStats.shadowCastersCulled = (cascadeCount > 0)
        ? totalCascadeCulled / cascadeCount : 0;

    // GL state restored by ScopedShadowDepthState + ScopedForwardZ on
    // function exit (R3 + R1 — was previously a manual `glDisable(GL_DEPTH_CLAMP)`
    // call that assumed the caller's prior state was "off" and left
    // `GL_CLIP_DISTANCE0` permanently disabled).
}

void Renderer::selectShadowCastingPointLights()
{
    m_shadowCasters.clear();
    for (int i = 0; i < static_cast<int>(m_pointLights.size()); i++)
    {
        if (m_pointLights[static_cast<size_t>(i)].castsShadow
            && static_cast<int>(m_shadowCasters.size()) < MAX_POINT_SHADOW_LIGHTS)
        {
            m_shadowCasters.push_back(i);
        }
    }
}

void Renderer::renderPointShadowPass(const std::vector<int>& shadowCasters)
{
    if (shadowCasters.empty())
    {
        return;
    }

    // Point shadow maps use forward-Z with [-1,1] NDC depth range
    ScopedForwardZ forwardZ;  // reverse-Z is restored on function exit
    m_pointShadowDepthShader.use();

    for (size_t s = 0; s < shadowCasters.size(); s++)
    {
        const PointLight& light = m_pointLights[static_cast<size_t>(shadowCasters[s])];
        auto& shadowMap = m_pointShadowMaps[s];

        shadowMap->update(light.position);

        float farPlane = shadowMap->getConfig().farPlane;
        m_pointShadowDepthShader.setVec3("u_lightPos", light.position);
        m_pointShadowDepthShader.setFloat("u_farPlane", farPlane);

        // Render all 6 cubemap faces
        for (int face = 0; face < 6; face++)
        {
            shadowMap->beginFace(face);
            m_pointShadowDepthShader.setMat4("u_lightSpaceMatrix",
                shadowMap->getLightSpaceMatrix(face));

            for (size_t b = 0; b < m_instanceBatchCount; b++)
            {
                const auto& batch = m_instanceBatches[b];
                int count = static_cast<int>(batch.modelMatrices.size());
                if (count >= MIN_INSTANCE_BATCH_SIZE && m_instanceBuffer)
                {
                    m_pointShadowDepthShader.setBool("u_useInstancing", true);
                    m_instanceBuffer->upload(batch.modelMatrices);
                    batch.mesh->setupInstanceAttributes(m_instanceBuffer->getHandle());

                    batch.mesh->bind();
                    glDrawElementsInstanced(GL_TRIANGLES,
                        static_cast<GLsizei>(batch.mesh->getIndexCount()),
                        GL_UNSIGNED_INT, nullptr, count);
                    m_cullingStats.drawCalls++;
                    batch.mesh->unbind();

                    m_pointShadowDepthShader.setBool("u_useInstancing", false);
                }
                else
                {
                    m_pointShadowDepthShader.setBool("u_useInstancing", false);
                    batch.mesh->bind();
                    for (const auto& matrix : batch.modelMatrices)
                    {
                        m_pointShadowDepthShader.setMat4("u_model", matrix);
                        glDrawElements(GL_TRIANGLES,
                            static_cast<GLsizei>(batch.mesh->getIndexCount()),
                            GL_UNSIGNED_INT, nullptr);
                        m_cullingStats.drawCalls++;
                    }
                    batch.mesh->unbind();
                }
            }

            shadowMap->endFace();
        }
    }
    // reverse-Z restored by ScopedForwardZ on function exit
}

/// Pre-built uniform name strings for point lights (avoids per-frame string allocations).
struct PointLightNames
{
    std::string position, ambient, diffuse, specular, constant, linear, quadratic;
};

/// Pre-built uniform name strings for spot lights.
struct SpotLightNames
{
    std::string position, direction, ambient, diffuse, specular;
    std::string innerCutoff, outerCutoff, constant, linear, quadratic;
};

static const std::array<PointLightNames, MAX_POINT_LIGHTS>& getPointLightNames()
{
    static const auto names = []()
    {
        std::array<PointLightNames, MAX_POINT_LIGHTS> arr;
        for (int i = 0; i < MAX_POINT_LIGHTS; i++)
        {
            std::string p = "u_pointLights_";
            std::string idx = "[" + std::to_string(i) + "]";
            arr[static_cast<size_t>(i)] = {
                p + "position" + idx, p + "ambient" + idx, p + "diffuse" + idx,
                p + "specular" + idx, p + "constant" + idx, p + "linear" + idx,
                p + "quadratic" + idx
            };
        }
        return arr;
    }();
    return names;
}

static const std::array<SpotLightNames, MAX_SPOT_LIGHTS>& getSpotLightNames()
{
    static const auto names = []()
    {
        std::array<SpotLightNames, MAX_SPOT_LIGHTS> arr;
        for (int i = 0; i < MAX_SPOT_LIGHTS; i++)
        {
            std::string p = "u_spotLights_";
            std::string idx = "[" + std::to_string(i) + "]";
            arr[static_cast<size_t>(i)] = {
                p + "position" + idx, p + "direction" + idx, p + "ambient" + idx,
                p + "diffuse" + idx, p + "specular" + idx, p + "innerCutoff" + idx,
                p + "outerCutoff" + idx, p + "constant" + idx, p + "linear" + idx,
                p + "quadratic" + idx
            };
        }
        return arr;
    }();
    return names;
}

void Renderer::uploadLightUniforms(const Camera& camera)
{
    // Camera position for specular calculation
    m_sceneShader.setVec3("u_viewPosition", camera.getPosition());

    // Directional light
    m_sceneShader.setBool("u_hasDirLight", m_hasDirectionalLight);
    if (m_hasDirectionalLight)
    {
        m_sceneShader.setVec3("u_dirLight_direction", m_directionalLight.direction);
        m_sceneShader.setVec3("u_dirLight_ambient", m_directionalLight.ambient);
        m_sceneShader.setVec3("u_dirLight_diffuse", m_directionalLight.diffuse);
        m_sceneShader.setVec3("u_dirLight_specular", m_directionalLight.specular);
    }

    // Point lights (using pre-built uniform names to avoid per-frame allocations)
    const auto& plNames = getPointLightNames();
    int pointCount = static_cast<int>(m_pointLights.size());
    m_sceneShader.setInt("u_pointLightCount", pointCount);
    for (int i = 0; i < pointCount; i++)
    {
        const auto& n = plNames[static_cast<size_t>(i)];
        const auto& pl = m_pointLights[static_cast<size_t>(i)];
        m_sceneShader.setVec3(n.position, pl.position);
        m_sceneShader.setVec3(n.ambient, pl.ambient);
        m_sceneShader.setVec3(n.diffuse, pl.diffuse);
        m_sceneShader.setVec3(n.specular, pl.specular);
        m_sceneShader.setFloat(n.constant, pl.constant);
        m_sceneShader.setFloat(n.linear, pl.linear);
        m_sceneShader.setFloat(n.quadratic, pl.quadratic);
    }

    // Spot lights (using pre-built uniform names)
    const auto& slNames = getSpotLightNames();
    int spotCount = static_cast<int>(m_spotLights.size());
    m_sceneShader.setInt("u_spotLightCount", spotCount);
    for (int i = 0; i < spotCount; i++)
    {
        const auto& n = slNames[static_cast<size_t>(i)];
        const auto& sl = m_spotLights[static_cast<size_t>(i)];
        m_sceneShader.setVec3(n.position, sl.position);
        m_sceneShader.setVec3(n.direction, sl.direction);
        m_sceneShader.setVec3(n.ambient, sl.ambient);
        m_sceneShader.setVec3(n.diffuse, sl.diffuse);
        m_sceneShader.setVec3(n.specular, sl.specular);
        m_sceneShader.setFloat(n.innerCutoff, sl.innerCutoff);
        m_sceneShader.setFloat(n.outerCutoff, sl.outerCutoff);
        m_sceneShader.setFloat(n.constant, sl.constant);
        m_sceneShader.setFloat(n.linear, sl.linear);
        m_sceneShader.setFloat(n.quadratic, sl.quadratic);
    }
}

void Renderer::generateSsaoKernel()
{
    m_ssaoKernel.clear();
    std::mt19937 gen(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    for (int i = 0; i < 64; i++)
    {
        glm::vec3 sample(
            dist(gen) * 2.0f - 1.0f,
            dist(gen) * 2.0f - 1.0f,
            dist(gen));

        sample = glm::normalize(sample);
        sample *= dist(gen);

        // Bias toward surface: lerp(0.1, 1.0, (i/64)^2)
        float scale = static_cast<float>(i) / 64.0f;
        scale = 0.1f + scale * scale * (1.0f - 0.1f);
        sample *= scale;

        m_ssaoKernel.push_back(sample);
    }
}

void Renderer::generateSsaoNoiseTexture()
{
    std::mt19937 gen(7);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    // 4x4 random rotation vectors (rotate around z-axis in tangent space)
    std::vector<glm::vec3> noise;
    for (int i = 0; i < 16; i++)
    {
        glm::vec3 n(
            dist(gen) * 2.0f - 1.0f,
            dist(gen) * 2.0f - 1.0f,
            0.0f);
        noise.push_back(n);
    }

    glCreateTextures(GL_TEXTURE_2D, 1, &m_ssaoNoiseTexture);
    glTextureStorage2D(m_ssaoNoiseTexture, 1, GL_RGB16F, 4, 4);
    glTextureSubImage2D(m_ssaoNoiseTexture, 0, 0, 0, 4, 4, GL_RGB, GL_FLOAT, noise.data());
    glTextureParameteri(m_ssaoNoiseTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_ssaoNoiseTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(m_ssaoNoiseTexture, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(m_ssaoNoiseTexture, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

void Renderer::generateCausticsTexture()
{
    // Procedural tileable caustic pattern using overlapping sine waves
    // that produce bright spots where waves converge (mimicking light refraction).
    constexpr int size = 256;
    std::vector<unsigned char> pixels(static_cast<size_t>(size * size));

    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            float u = static_cast<float>(x) / static_cast<float>(size);
            float v = static_cast<float>(y) / static_cast<float>(size);
            float pi2 = 2.0f * glm::pi<float>();

            // Multiple sine-wave layers at different frequencies and angles
            // to produce a caustic-like pattern with bright convergence points
            float c = 0.0f;
            c += std::sin(u * 8.0f * pi2 + v * 3.0f * pi2) * 0.5f + 0.5f;
            c *= std::sin(v * 6.0f * pi2 - u * 4.0f * pi2 + 1.3f) * 0.5f + 0.5f;
            c += (std::sin((u + v) * 10.0f * pi2 + 0.7f) * 0.5f + 0.5f) * 0.3f;
            c += (std::sin((u - v) * 7.0f * pi2 + 2.1f) * 0.5f + 0.5f) * 0.2f;

            // Sharpen — raise to a power so only bright spots remain
            c = std::pow(std::min(c, 1.0f), 2.5f);

            pixels[static_cast<size_t>(y * size + x)] =
                static_cast<unsigned char>(std::min(c * 255.0f, 255.0f));
        }
    }

    glCreateTextures(GL_TEXTURE_2D, 1, &m_causticsTexture);
    GLsizei mipLevels = 1 + static_cast<GLsizei>(std::floor(std::log2(size)));
    glTextureStorage2D(m_causticsTexture, mipLevels, GL_R8, size, size);
    glTextureSubImage2D(m_causticsTexture, 0, 0, 0, size, size,
                        GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    glTextureParameteri(m_causticsTexture, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(m_causticsTexture, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameteri(m_causticsTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(m_causticsTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateTextureMipmap(m_causticsTexture);

    Logger::info("Caustics texture generated (256x256, procedural)");
}

void Renderer::setCausticsParams(bool enabled, float waterY, float time,
                                  const glm::vec2& center, const glm::vec2& halfExtent,
                                  float intensity, float scale)
{
    m_causticsEnabled = enabled;
    m_causticsWaterY = waterY;
    m_causticsTime = time;
    m_causticsCenter = center;
    m_causticsHalfExtent = halfExtent;
    m_causticsIntensity = intensity;
    m_causticsScale = scale;
}

// ---------------------------------------------------------------------------
// Selection system: ID buffer picking + outline rendering
// ---------------------------------------------------------------------------

void Renderer::renderIdBuffer(const SceneRenderData& renderData,
                              const Camera& camera, float aspectRatio)
{
    if (!m_idBufferFbo)
    {
        return;
    }

    m_idBufferFbo->bind();
    glViewport(0, 0, m_windowWidth, m_windowHeight);

    // Clear to black (entity ID 0 = background/no entity)
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClearDepth(0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Use reverse-Z depth (already global state: glDepthFunc(GL_GEQUAL), glClipControl ZERO_TO_ONE)
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    m_idBufferShader.use();
    glm::mat4 vp = camera.getProjectionMatrix(aspectRatio) * camera.getViewMatrix();
    m_idBufferShader.setMat4("u_viewProjection", vp);

    // Draw all opaque items
    auto drawItems = [&](const std::vector<SceneRenderData::RenderItem>& items)
    {
        for (const auto& item : items)
        {
            if (item.entityId == 0 || item.isLocked)
            {
                continue;
            }

            // Encode entity ID as RGB color
            float r = static_cast<float>((item.entityId >> 0) & 0xFF) / 255.0f;
            float g = static_cast<float>((item.entityId >> 8) & 0xFF) / 255.0f;
            float b = static_cast<float>((item.entityId >> 16) & 0xFF) / 255.0f;

            // Skeletal animation
            bool skinned = (item.boneMatrices != nullptr && !item.boneMatrices->empty());
            m_idBufferShader.setBool("u_hasBones", skinned);
            if (skinned)
            {
                glNamedBufferSubData(m_boneMatrixSSBO, 0,
                    static_cast<GLsizeiptr>(item.boneMatrices->size() * sizeof(glm::mat4)),
                    item.boneMatrices->data());
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_boneMatrixSSBO);
            }

            m_idBufferShader.setVec3("u_entityColor", glm::vec3(r, g, b));
            m_idBufferShader.setMat4("u_model", item.worldMatrix);

            item.mesh->bind();
            glDrawElements(GL_TRIANGLES,
                static_cast<GLsizei>(item.mesh->getIndexCount()),
                GL_UNSIGNED_INT, nullptr);
            item.mesh->unbind();
        }
    };

    drawItems(renderData.renderItems);
    drawItems(renderData.transparentItems);

    // Restore clear color
    glClearColor(m_clearColor.r, m_clearColor.g, m_clearColor.b, 1.0f);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

uint32_t Renderer::pickEntityAt(int x, int y)
{
    if (!m_idBufferFbo)
    {
        return 0;
    }

    // Clamp to FBO bounds
    x = std::clamp(x, 0, m_windowWidth - 1);
    y = std::clamp(y, 0, m_windowHeight - 1);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_idBufferFbo->getId());

    unsigned char pixel[4] = {0, 0, 0, 0};
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    // Decode entity ID from RGB
    uint32_t id = static_cast<uint32_t>(pixel[0])
                | (static_cast<uint32_t>(pixel[1]) << 8)
                | (static_cast<uint32_t>(pixel[2]) << 16);
    return id;
}

std::vector<uint32_t> Renderer::pickEntitiesInRect(int x0, int y0, int x1, int y1)
{
    std::vector<uint32_t> result;
    if (!m_idBufferFbo)
    {
        return result;
    }

    // Normalize so x0 <= x1, y0 <= y1
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);

    x0 = std::clamp(x0, 0, m_windowWidth - 1);
    x1 = std::clamp(x1, 0, m_windowWidth - 1);
    y0 = std::clamp(y0, 0, m_windowHeight - 1);
    y1 = std::clamp(y1, 0, m_windowHeight - 1);

    int w = x1 - x0 + 1;
    int h = y1 - y0 + 1;
    if (w <= 0 || h <= 0)
    {
        return result;
    }

    // Read entire rectangle in one GPU call, then scan CPU-side buffer
    std::unordered_set<uint32_t> ids;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_idBufferFbo->getId());

    std::vector<unsigned char> pixels(static_cast<size_t>(w * h * 4));
    glReadPixels(x0, y0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    // Sample every Nth pixel for performance on large selections
    int step = std::max(1, std::min(w, h) / 64);
    for (int py = 0; py < h; py += step)
    {
        for (int px = 0; px < w; px += step)
        {
            size_t offset = static_cast<size_t>((py * w + px) * 4);
            uint32_t id = static_cast<uint32_t>(pixels[offset])
                        | (static_cast<uint32_t>(pixels[offset + 1]) << 8)
                        | (static_cast<uint32_t>(pixels[offset + 2]) << 16);
            if (id != 0)
            {
                ids.insert(id);
            }
        }
    }

    result.assign(ids.begin(), ids.end());
    return result;
}

void Renderer::bindOutputFbo()
{
    if (m_outputFbo)
    {
        m_outputFbo->bind();
        glViewport(0, 0, m_windowWidth, m_windowHeight);
    }
}

void Renderer::renderSelectionOutline(const SceneRenderData& renderData,
                                      const std::vector<uint32_t>& selectedIds,
                                      const Camera& camera, float aspectRatio)
{
    if (selectedIds.empty() || !m_outputFbo)
    {
        return;
    }

    // Build fast lookup set
    std::unordered_map<uint32_t, bool> selectedSet;
    for (uint32_t id : selectedIds)
    {
        selectedSet[id] = true;
    }

    // Collect render items that belong to selected entities
    struct OutlineItem
    {
        const Mesh* mesh;
        glm::mat4 worldMatrix;
        const std::vector<glm::mat4>* boneMatrices = nullptr;
    };
    std::vector<OutlineItem> outlineItems;

    auto collectFrom = [&](const std::vector<SceneRenderData::RenderItem>& items)
    {
        for (const auto& item : items)
        {
            if (selectedSet.count(item.entityId))
            {
                outlineItems.push_back({item.mesh, item.worldMatrix, item.boneMatrices});
            }
        }
    };
    collectFrom(renderData.renderItems);
    collectFrom(renderData.transparentItems);

    if (outlineItems.empty())
    {
        return;
    }

    // Bind the output FBO (has depth-stencil RBO attached)
    m_outputFbo->bind();
    glViewport(0, 0, m_windowWidth, m_windowHeight);

    // Clear stencil only (preserve the tonemapped color)
    glClear(GL_STENCIL_BUFFER_BIT);

    // Disable depth testing — outline shows through all geometry (standard editor behavior)
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_STENCIL_TEST);

    glm::mat4 vp = camera.getProjectionMatrix(aspectRatio) * camera.getViewMatrix();
    m_outlineShader.use();
    m_outlineShader.setMat4("u_viewProjection", vp);

    // Pass 1: Write stencil = 1 where selected entities draw (normal scale, no color)
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glStencilMask(0xFF);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

    for (const auto& oi : outlineItems)
    {
        bool skinned = (oi.boneMatrices != nullptr && !oi.boneMatrices->empty());
        m_outlineShader.setBool("u_hasBones", skinned);
        if (skinned)
        {
            glNamedBufferSubData(m_boneMatrixSSBO, 0,
                static_cast<GLsizeiptr>(oi.boneMatrices->size() * sizeof(glm::mat4)),
                oi.boneMatrices->data());
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_boneMatrixSSBO);
        }
        m_outlineShader.setMat4("u_model", oi.worldMatrix);
        oi.mesh->bind();
        glDrawElements(GL_TRIANGLES,
            static_cast<GLsizei>(oi.mesh->getIndexCount()),
            GL_UNSIGNED_INT, nullptr);
        oi.mesh->unbind();
    }

    // Pass 2: Draw scaled-up version where stencil != 1 → orange outline ring
    glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
    glStencilMask(0x00);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    // Bright orange — high contrast for accessibility
    m_outlineShader.setVec3("u_outlineColor", glm::vec3(0.95f, 0.55f, 0.10f));

    float outlineScale = 1.05f;
    for (const auto& oi : outlineItems)
    {
        bool skinned = (oi.boneMatrices != nullptr && !oi.boneMatrices->empty());
        m_outlineShader.setBool("u_hasBones", skinned);
        if (skinned)
        {
            glNamedBufferSubData(m_boneMatrixSSBO, 0,
                static_cast<GLsizeiptr>(oi.boneMatrices->size() * sizeof(glm::mat4)),
                oi.boneMatrices->data());
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_boneMatrixSSBO);
        }

        // Scale around the object's world-space center
        glm::vec3 center = glm::vec3(oi.worldMatrix[3]);
        glm::mat4 scaledModel = glm::translate(glm::mat4(1.0f), center)
            * glm::scale(glm::mat4(1.0f), glm::vec3(outlineScale))
            * glm::translate(glm::mat4(1.0f), -center)
            * oi.worldMatrix;

        m_outlineShader.setMat4("u_model", scaledModel);
        oi.mesh->bind();
        glDrawElements(GL_TRIANGLES,
            static_cast<GLsizei>(oi.mesh->getIndexCount()),
            GL_UNSIGNED_INT, nullptr);
        oi.mesh->unbind();
    }

    // Restore OpenGL state
    glStencilMask(0xFF);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::resizeRenderTarget(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }

    // Skip if unchanged — avoids expensive FBO reallocations every frame
    if (width == m_windowWidth && height == m_windowHeight)
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
    if (m_outputFbo)
    {
        m_outputFbo->resize(width, height);

        // Resize and re-attach the stencil renderbuffer (resize recreates the FBO
        // with a new ID, so the old attachment is lost)
        if (m_outlineStencilRbo != 0)
        {
            glBindRenderbuffer(GL_RENDERBUFFER, m_outlineStencilRbo);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);

            glBindFramebuffer(GL_FRAMEBUFFER, m_outputFbo->getId());
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                      GL_RENDERBUFFER, m_outlineStencilRbo);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }
    if (m_idBufferFbo)
    {
        m_idBufferFbo->resize(width, height);
    }
    // Shadow map does not resize with the window — it has a fixed resolution

    // Recreate bloom mip-chain texture (immutable storage requires delete+recreate on resize)
    if (m_bloomTexture != 0)
    {
        glDeleteTextures(1, &m_bloomTexture);

        int mipW = width / 2;
        int mipH = height / 2;
        if (mipW < 1) mipW = 1;
        if (mipH < 1) mipH = 1;

        glCreateTextures(GL_TEXTURE_2D, 1, &m_bloomTexture);
        glTextureStorage2D(m_bloomTexture, BLOOM_MIP_COUNT, GL_R11F_G11F_B10F, mipW, mipH);

        for (int i = 0; i < BLOOM_MIP_COUNT; i++)
        {
            m_bloomMipWidths[i] = mipW;
            m_bloomMipHeights[i] = mipH;
            mipW = std::max(1, mipW / 2);
            mipH = std::max(1, mipH / 2);
        }

        glTextureParameteri(m_bloomTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTextureParameteri(m_bloomTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(m_bloomTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_bloomTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_bloomTexture, GL_TEXTURE_BASE_LEVEL, 0);
        glTextureParameteri(m_bloomTexture, GL_TEXTURE_MAX_LEVEL, BLOOM_MIP_COUNT - 1);
    }

    // Resize SSAO FBOs (full-resolution)
    if (m_resolveDepthFbo) m_resolveDepthFbo->resize(width, height);
    if (m_ssaoFbo) m_ssaoFbo->resize(width, height);
    if (m_ssaoBlurFbo) m_ssaoBlurFbo->resize(width, height);

    // Resize TAA FBOs
    if (m_taaSceneFbo) m_taaSceneFbo->resize(width, height);
    if (m_taa) m_taa->resize(width, height);

    // Resize SMAA FBOs
    if (m_smaa) m_smaa->resize(width, height);
}

int Renderer::getRenderWidth() const
{
    return m_windowWidth;
}

int Renderer::getRenderHeight() const
{
    return m_windowHeight;
}

void Renderer::onWindowResize(int width, int height)
{
    // In editor mode, FBO size is driven by the viewport panel, not the window.
    // This callback still fires on window resize — only resize FBOs if we're
    // not in editor mode (no viewport panel). The engine drives
    // resizeRenderTarget() each frame based on viewport panel size.
    // For now, always resize — the engine will override next frame if in editor mode.
    resizeRenderTarget(width, height);
}

} // namespace Vestige
