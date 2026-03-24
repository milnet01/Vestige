/// @file renderer.cpp
/// @brief Renderer implementation with Blinn-Phong/PBR lighting, shadows, and FBO pipeline.
#include "renderer/renderer.h"
#include "renderer/foliage_renderer.h"
#include "environment/foliage_manager.h"
#include "scene/scene.h"
#include "core/logger.h"
#include "utils/frustum.h"

#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <random>

namespace Vestige
{

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

    // Subscribe to window resize events
    m_eventBus.subscribe<WindowResizeEvent>([this](const WindowResizeEvent& event)
    {
        onWindowResize(event.width, event.height);
    });

    // Initialize per-frame PMR arena
    m_frameResource = new std::pmr::monotonic_buffer_resource(
        m_frameArena, FRAME_ARENA_SIZE, std::pmr::null_memory_resource());

    Logger::info("Renderer initialized (OpenGL 4.5, reverse-Z)");
}

void Renderer::resetFrameAllocator()
{
    // Destroy and reconstruct the monotonic resource to "reset" it
    delete m_frameResource;
    m_frameResource = new std::pmr::monotonic_buffer_resource(
        m_frameArena, FRAME_ARENA_SIZE, std::pmr::null_memory_resource());
}

Renderer::~Renderer()
{
    delete m_frameResource;
    m_frameResource = nullptr;

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

    // Load screen-space reflections shader (disabled until G-buffer in Phase 5)
    std::string ssrFragPath = assetPath + "/shaders/ssr.frag.glsl";
    if (!m_ssrShader.loadFromFiles(screenVertPath, ssrFragPath))
    {
        Logger::error("Failed to load SSR shader");
        return false;
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

    // SSR FBO (RGBA16F — stores reflected color + confidence in alpha)
    m_ssrFbo = std::make_unique<Framebuffer>(ssaoConfig);

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

    Logger::info("Framebuffer pipeline initialized: "
        + std::to_string(width) + "x" + std::to_string(height)
        + " with " + std::to_string(msaaSamples) + "x MSAA + shadow mapping + skybox + bloom + SSAO");
}

void Renderer::beginFrame()
{
    bool isTAA = (m_antiAliasMode == AntiAliasMode::TAA && m_taa && m_taaSceneFbo);

    if (isTAA)
    {
        m_taaSceneFbo->bind();
    }
    else if (m_msaaFbo)
    {
        m_msaaFbo->bind();
    }

    glViewport(0, 0, m_windowWidth, m_windowHeight);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::endFrame(float deltaTime)
{
    if (!m_resolveFbo || !m_screenQuad)
    {
        return;
    }

    bool isTAA = (m_antiAliasMode == AntiAliasMode::TAA && m_taa && m_taaSceneFbo);

    // 1. Resolve scene color → non-multisampled resolve FBO
    if (isTAA)
    {
        // TAA mode: blit from non-MSAA scene FBO to resolve FBO
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
    if (!isTAA && m_msaaFbo && m_msaaFbo->isMultisampled())
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFbo->getId());
        GLenum attachments[] = { GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT };
        glInvalidateFramebuffer(GL_FRAMEBUFFER, 2, attachments);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // 2. Resolve depth → sampleable depth texture for SSAO and TAA motion vectors
    GLuint depthSourceFbo = isTAA ? m_taaSceneFbo->getId()
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

    // 4. TAA motion vectors and resolve
    // The FBO used as the HDR input for bloom and final composite
    Framebuffer* hdrSourceFbo = m_resolveFbo.get();

    if (isTAA)
    {
        // 4a. Motion vector pass
        m_taa->getMotionVectorFbo().bind();
        glViewport(0, 0, m_windowWidth, m_windowHeight);
        glClear(GL_COLOR_BUFFER_BIT);

        m_motionVectorShader.use();
        m_resolveDepthFbo->bindDepthTexture(0);
        m_motionVectorShader.setInt("u_depthTexture", 0);
        m_motionVectorShader.setMat4("u_currentInvViewProjection",
            glm::inverse(m_lastViewProjection));
        m_motionVectorShader.setMat4("u_prevViewProjection", m_prevViewProjection);
        m_motionVectorShader.setVec2("u_texelSize",
            glm::vec2(1.0f / static_cast<float>(m_windowWidth),
                      1.0f / static_cast<float>(m_windowHeight)));
        m_screenQuad->draw();

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
                glActiveTexture(GL_TEXTURE0);
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

    // Bloom uniforms — ALWAYS bind texture (Mesa requires valid textures for declared samplers)
    m_screenShader.setBool("u_bloomEnabled", m_bloomEnabled);
    m_screenShader.setFloat("u_bloomIntensity", m_bloomIntensity);
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

    m_screenQuad->draw();

    // 7. Swap TAA history buffers
    if (isTAA)
    {
        m_taa->swapBuffers();
        m_taa->nextFrame();
        m_prevViewProjection = m_lastViewProjection;
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
        m_sceneShader.setInt("u_diffuseTexture", 0);
    }

    // Normal map (shared — unit 1)
    bool hasNormalMap = material.hasNormalMap();
    m_sceneShader.setBool("u_hasNormalMap", hasNormalMap);
    if (hasNormalMap)
    {
        material.getNormalMap()->bind(1);
        m_sceneShader.setInt("u_normalMap", 1);
    }

    // Height map (shared — unit 2)
    bool hasHeightMap = m_pomEnabled && material.isPomEnabled() && material.hasHeightMap();
    m_sceneShader.setBool("u_hasHeightMap", hasHeightMap);
    if (hasHeightMap)
    {
        material.getHeightMap()->bind(2);
        m_sceneShader.setInt("u_heightMap", 2);
        m_sceneShader.setFloat("u_heightScale", material.getHeightScale() * m_pomHeightMultiplier);
    }

    // PBR uniforms
    if (usePBR)
    {
        m_sceneShader.setVec3("u_pbrAlbedo", material.getAlbedo());
        m_sceneShader.setFloat("u_pbrMetallic", material.getMetallic());
        m_sceneShader.setFloat("u_pbrRoughness", material.getRoughness());
        m_sceneShader.setFloat("u_pbrAo", material.getAo());
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
            m_sceneShader.setInt("u_metallicRoughnessMap", 6);
        }

        // Emissive map (unit 7)
        bool hasEmissive = material.hasEmissiveTexture();
        m_sceneShader.setBool("u_hasEmissiveMap", hasEmissive);
        if (hasEmissive)
        {
            material.getEmissiveTexture()->bind(7);
            m_sceneShader.setInt("u_emissiveMap", 7);
        }

        // AO map (unit 8)
        bool hasAo = material.hasAoTexture();
        m_sceneShader.setBool("u_hasAoMap", hasAo);
        if (hasAo)
        {
            material.getAoTexture()->bind(8);
            m_sceneShader.setInt("u_aoMap", 8);
        }
    }
    else
    {
        // Ensure PBR-only uniforms are zeroed when not in PBR mode
        m_sceneShader.setBool("u_hasMetallicRoughnessMap", false);
        m_sceneShader.setBool("u_hasEmissiveMap", false);
        m_sceneShader.setBool("u_hasAoMap", false);
        m_sceneShader.setFloat("u_clearcoat", 0.0f);
        m_sceneShader.setFloat("u_clearcoatRoughness", 0.0f);
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
                         float /*aspectRatio*/)
{
    m_sceneShader.use();

    // Non-instanced draw — use uniform model matrix
    m_sceneShader.setBool("u_useInstancing", false);
    m_sceneShader.setMat4("u_model", modelMatrix);
    m_sceneShader.setMat3("u_normalMatrix",
        glm::mat3(glm::transpose(glm::inverse(modelMatrix))));
    m_sceneShader.setMat4("u_view", camera.getViewMatrix());
    m_sceneShader.setMat4("u_projection", m_lastProjection);

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
    const char* names[] = {"None", "MSAA 4x", "TAA"};
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

void Renderer::blitToScreen()
{
    if (!m_outputFbo)
    {
        return;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_outputFbo->getId());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, m_windowWidth, m_windowHeight,
                      0, 0, m_windowWidth, m_windowHeight,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

GLuint Renderer::getSkyboxTextureId() const
{
    return m_skybox ? m_skybox->getTextureId() : 0;
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

std::vector<Renderer::InstanceBatch> Renderer::buildInstanceBatches(
    const std::vector<SceneRenderData::RenderItem>& items)
{
    // Hash functor for (mesh*, material*) pair
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
        }
        else
        {
            indexMap[key] = batches.size();
            InstanceBatch batch;
            batch.mesh = item.mesh;
            batch.material = item.material;
            batch.modelMatrices.push_back(item.worldMatrix);
            batches.push_back(std::move(batch));
        }
    }

    return batches;
}

void Renderer::renderScene(const SceneRenderData& renderData, const Camera& camera, float aspectRatio)
{
    // Reset per-frame scratch allocator (all pmr::vectors from last frame are now invalid)
    resetFrameAllocator();

    // Reset per-frame stats
    m_cullingStats.drawCalls = 0;
    m_cullingStats.instanceBatches = 0;

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

    // Build shadow caster list (filter out non-casting items like ground planes)
    std::vector<SceneRenderData::RenderItem> shadowCasterItems;
    for (const auto& item : renderData.renderItems)
    {
        if (item.castsShadow)
        {
            shadowCasterItems.push_back(item);
        }
    }
    m_cullingStats.shadowCastersTotal = static_cast<int>(shadowCasterItems.size());

    // Compute shadow-casting point lights once (used by both shadow pass and uniform upload)
    std::vector<int> shadowCasters = selectShadowCastingPointLights();

    // --- Directional shadow pass (cascaded, per-cascade frustum culled) ---
    if (m_cascadedShadowMap && m_hasDirectionalLight)
    {
        renderShadowPass(shadowCasterItems, camera, aspectRatio);
    }

    // --- Point light shadow pass (uses all shadow casters — omnidirectional) ---
    auto shadowBatches = buildInstanceBatches(shadowCasterItems);
    renderPointShadowPass(shadowBatches, shadowCasters);

    // --- Frustum cull for scene pass ---
    // Use the standard (non-reverse-Z) projection for frustum extraction so all
    // 6 planes are well-defined. The reverse-Z infinite projection produces a
    // degenerate near plane that breaks the Gribb-Hartmann extraction.
    glm::mat4 cullingVP = camera.getCullingProjectionMatrix(aspectRatio) * camera.getViewMatrix();
    auto frustumPlanes = extractFrustumPlanes(cullingVP);

    // Filter render items to only those inside the view frustum.
    // Items with zero-size bounds (no explicit AABB set) are always included —
    // it is safer to overdraw than to incorrectly cull visible geometry.
    m_culledItems.clear();
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
    auto batches = buildInstanceBatches(m_culledItems);

    // Re-bind the scene FBO after shadow passes
    bool isTAA = (m_antiAliasMode == AntiAliasMode::TAA && m_taa && m_taaSceneFbo);
    if (isTAA)
    {
        m_taaSceneFbo->bind();
    }
    else if (m_msaaFbo)
    {
        m_msaaFbo->bind();
    }
    glViewport(0, 0, m_windowWidth, m_windowHeight);

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
        for (int i = 0; i < cascadeCount; i++)
        {
            m_sceneShader.setFloat("u_cascadeSplits[" + std::to_string(i) + "]",
                m_cascadedShadowMap->getCascadeSplit(i));
            m_sceneShader.setMat4("u_cascadeLightSpaceMatrices[" + std::to_string(i) + "]",
                m_cascadedShadowMap->getLightSpaceMatrix(i));
            m_sceneShader.setFloat("u_cascadeTexelSize[" + std::to_string(i) + "]",
                m_cascadedShadowMap->getTexelWorldSize(i));
        }
        m_sceneShader.setBool("u_hasShadows", true);
        m_sceneShader.setBool("u_cascadeDebug", m_cascadeDebug);
        m_sceneShader.setFloat("u_shadowLightSize", 4.0f);  // PCSS light size (texels)
    }
    else
    {
        m_sceneShader.setBool("u_hasShadows", false);
        m_sceneShader.setBool("u_cascadeDebug", false);
    }

    // --- Set point shadow uniforms (reusing precomputed shadowCasters) ---
    int pointShadowCount = static_cast<int>(shadowCasters.size());
    m_sceneShader.setInt("u_pointShadowCount", pointShadowCount);

    for (int i = 0; i < pointShadowCount; i++)
    {
        int textureUnit = 4 + i;  // Units 4-5 for point shadow cubemaps
        m_pointShadowMaps[static_cast<size_t>(i)]->bindShadowTexture(textureUnit);
        m_sceneShader.setInt("u_pointShadowMaps[" + std::to_string(i) + "]", textureUnit);
        m_sceneShader.setInt("u_pointShadowIndices[" + std::to_string(i) + "]", shadowCasters[static_cast<size_t>(i)]);
        m_sceneShader.setFloat("u_pointShadowFarPlane[" + std::to_string(i) + "]",
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
    }
    else
    {
        // No environment map at all — bind dummy textures to satisfy Mesa
        // Cubemap samplers on units 14, 15 — bind zero textures
        glBindTextureUnit(14, 0);
        m_sceneShader.setInt("u_irradianceMap", 14);
        glBindTextureUnit(15, 0);
        m_sceneShader.setInt("u_prefilterMap", 15);
        // 2D sampler on unit 16 — bind zero texture
        glBindTextureUnit(16, 0);
        m_sceneShader.setInt("u_brdfLUT", 16);
        m_sceneShader.setFloat("u_maxPrefilterLod", 0.0f);
        m_sceneShader.setBool("u_hasIBL", false);
    }

    // Store projection matrix for SSAO
    glm::mat4 projection = camera.getProjectionMatrix(aspectRatio);

    // Apply TAA jitter to projection
    if (m_antiAliasMode == AntiAliasMode::TAA && m_taa)
    {
        projection = m_taa->jitterProjection(projection, m_windowWidth, m_windowHeight);
    }
    m_lastProjection = projection;
    m_lastView = camera.getViewMatrix();
    m_lastViewProjection = projection * m_lastView;

    // Upload light uniforms once per frame (not per batch)
    uploadLightUniforms(camera);

    // --- Scene pass: draw all opaque render items (instanced where possible) ---
    for (const auto& batch : batches)
    {
        int count = static_cast<int>(batch.modelMatrices.size());
        if (count >= MIN_INSTANCE_BATCH_SIZE && m_instanceBuffer)
        {
            // Instanced path: set up material once, draw all instances
            m_sceneShader.use();
            m_sceneShader.setBool("u_useInstancing", true);

            // Upload instance matrices and bind to mesh VAO
            m_instanceBuffer->upload(batch.modelMatrices);
            batch.mesh->setupInstanceAttributes(m_instanceBuffer->getHandle());

            // Set a dummy u_model (unused but prevents warnings)
            m_sceneShader.setMat4("u_model", batch.modelMatrices[0]);
            m_sceneShader.setMat4("u_view", camera.getViewMatrix());
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
            for (const auto& matrix : batch.modelMatrices)
            {
                drawMesh(*batch.mesh, matrix, *batch.material, camera, aspectRatio);
            }
        }
    }

    // --- Skybox pass: draw after opaque geometry, before transparent ---
    if (m_skybox)
    {
        // Reverse-Z: skybox at depth 0.0 (far plane). GL_GEQUAL ensures:
        //   empty pixels (depth=0.0): 0.0 >= 0.0 → PASS (skybox fills background)
        //   geometry pixels (depth>0): 0.0 >= 0.02 → FAIL (skybox hidden by geometry)
        // No depth func change needed — GL_GEQUAL is already set.
        glDepthMask(GL_FALSE);   // Don't write to depth buffer
        glDisable(GL_CULL_FACE); // We're inside the cube — must see inner faces

        m_skyboxShader.use();
        m_skyboxShader.setMat4("u_view", camera.getViewMatrix());
        // Use the (potentially jittered) projection so TAA accumulates the skybox correctly
        m_skyboxShader.setMat4("u_projection", m_lastProjection);
        m_skyboxShader.setBool("u_hasCubemap", m_skybox->hasTexture());

        if (m_skybox->hasTexture())
        {
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
            drawMesh(*item.mesh, item.worldMatrix, *item.material, camera, aspectRatio);
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
    glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
    glDepthFunc(GL_LESS);
    glClearDepth(1.0);

    // Update all cascade light-space matrices from the camera frustum
    m_cascadedShadowMap->update(m_directionalLight, camera, aspectRatio);

    m_shadowDepthShader.use();

    // Render geometry into each cascade layer with per-cascade frustum culling
    int cascadeCount = m_cascadedShadowMap->getCascadeCount();
    int totalCascadeCulled = 0;

    for (int c = 0; c < cascadeCount; c++)
    {
        const glm::mat4& lightSpaceMatrix = m_cascadedShadowMap->getLightSpaceMatrix(c);

        // Extract frustum planes from the cascade's orthographic light-space matrix
        auto cascadePlanes = extractFrustumPlanes(lightSpaceMatrix);

        // Cull shadow casters against this cascade's frustum
        std::vector<SceneRenderData::RenderItem> culledCasters;
        for (const auto& item : shadowCasterItems)
        {
            if (item.worldBounds.getSize() == glm::vec3(0.0f)
                || isAabbInFrustum(item.worldBounds, cascadePlanes))
            {
                culledCasters.push_back(item);
            }
        }
        totalCascadeCulled += static_cast<int>(culledCasters.size());

        auto batches = buildInstanceBatches(culledCasters);

        m_cascadedShadowMap->beginCascade(c);
        m_shadowDepthShader.setMat4("u_lightSpaceMatrix", lightSpaceMatrix);

        for (const auto& batch : batches)
        {
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
                for (const auto& matrix : batch.modelMatrices)
                {
                    m_shadowDepthShader.setMat4("u_model", matrix);
                    batch.mesh->bind();
                    glDrawElements(GL_TRIANGLES,
                        static_cast<GLsizei>(batch.mesh->getIndexCount()),
                        GL_UNSIGNED_INT, nullptr);
                    m_cullingStats.drawCalls++;
                    batch.mesh->unbind();
                }
            }
        }

        // Render foliage into nearby cascades for grass shadow casting
        if (m_foliageShadowCaster && m_foliageShadowManager && c == 0)
        {
            // Use camera's VP for chunk visibility (same as main pass)
            glm::mat4 viewProj = camera.getCullingProjectionMatrix(aspectRatio)
                               * camera.getViewMatrix();
            auto visibleChunks = m_foliageShadowManager->getVisibleChunks(viewProj);
            if (!visibleChunks.empty())
            {
                m_foliageShadowCaster->renderShadow(
                    visibleChunks, camera, lightSpaceMatrix,
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

    // Restore reverse-Z depth state
    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    glDepthFunc(GL_GEQUAL);
    glClearDepth(0.0);
}

std::vector<int> Renderer::selectShadowCastingPointLights() const
{
    std::vector<int> result;
    for (int i = 0; i < static_cast<int>(m_pointLights.size()); i++)
    {
        if (m_pointLights[static_cast<size_t>(i)].castsShadow
            && static_cast<int>(result.size()) < MAX_POINT_SHADOW_LIGHTS)
        {
            result.push_back(i);
        }
    }
    return result;
}

void Renderer::renderPointShadowPass(const std::vector<InstanceBatch>& batches,
                                      const std::vector<int>& shadowCasters)
{
    if (shadowCasters.empty())
    {
        return;
    }

    // Point shadow maps use forward-Z with [-1,1] NDC depth range
    glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
    glDepthFunc(GL_LESS);
    glClearDepth(1.0);
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

            for (const auto& batch : batches)
            {
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
                    for (const auto& matrix : batch.modelMatrices)
                    {
                        m_pointShadowDepthShader.setMat4("u_model", matrix);
                        batch.mesh->bind();
                        glDrawElements(GL_TRIANGLES,
                            static_cast<GLsizei>(batch.mesh->getIndexCount()),
                            GL_UNSIGNED_INT, nullptr);
                        m_cullingStats.drawCalls++;
                        batch.mesh->unbind();
                    }
                }
            }

            shadowMap->endFace();
        }
    }

    // Restore reverse-Z depth state
    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    glDepthFunc(GL_GEQUAL);
    glClearDepth(0.0);
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
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
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
    };
    std::vector<OutlineItem> outlineItems;

    auto collectFrom = [&](const std::vector<SceneRenderData::RenderItem>& items)
    {
        for (const auto& item : items)
        {
            if (selectedSet.count(item.entityId))
            {
                outlineItems.push_back({item.mesh, item.worldMatrix});
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
