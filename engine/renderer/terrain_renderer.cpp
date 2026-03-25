/// @file terrain_renderer.cpp
/// @brief CDLOD terrain renderer with instanced grid mesh and splatmap texturing.

#include "renderer/terrain_renderer.h"
#include "renderer/cascaded_shadow_map.h"
#include "core/logger.h"

#include <glm/gtc/type_ptr.hpp>

namespace Vestige
{

TerrainRenderer::~TerrainRenderer()
{
    shutdown();
}

bool TerrainRenderer::init(const std::string& assetPath)
{
    if (m_initialized) return true;

    std::string shaderPath = assetPath + "/shaders/";

    if (!m_terrainShader.loadFromFiles(shaderPath + "terrain.vert.glsl",
                                       shaderPath + "terrain.frag.glsl"))
    {
        Logger::error("TerrainRenderer: failed to load terrain shader");
        return false;
    }

    if (!m_shadowShader.loadFromFiles(shaderPath + "terrain_shadow.vert.glsl",
                                      shaderPath + "terrain_shadow.frag.glsl"))
    {
        Logger::error("TerrainRenderer: failed to load terrain shadow shader");
        return false;
    }

    generateDefaultTextures();

    m_initialized = true;
    Logger::info("TerrainRenderer initialized");
    return true;
}

void TerrainRenderer::shutdown()
{
    if (m_gridVao)
    {
        glDeleteVertexArrays(1, &m_gridVao);
        m_gridVao = 0;
    }
    if (m_gridVbo)
    {
        glDeleteBuffers(1, &m_gridVbo);
        m_gridVbo = 0;
    }
    if (m_gridEbo)
    {
        glDeleteBuffers(1, &m_gridEbo);
        m_gridEbo = 0;
    }
    if (m_defaultAlbedo)
    {
        glDeleteTextures(1, &m_defaultAlbedo);
        m_defaultAlbedo = 0;
    }
    if (m_defaultNormal)
    {
        glDeleteTextures(1, &m_defaultNormal);
        m_defaultNormal = 0;
    }
    m_gridIndexCount = 0;
    m_gridResolution = 0;
    m_terrainShader.destroy();
    m_shadowShader.destroy();
    m_initialized = false;
}

void TerrainRenderer::render(const Terrain& terrain,
                             const Camera& camera,
                             float aspectRatio,
                             const SceneRenderData& sceneData,
                             CascadedShadowMap* csm,
                             const glm::vec4& clipPlane)
{
    if (!m_initialized || !terrain.isInitialized()) return;

    const auto& config = terrain.getConfig();

    // Ensure grid mesh matches terrain config
    if (m_gridResolution != config.gridResolution)
    {
        createGridMesh(config.gridResolution);
    }

    // Select visible CDLOD nodes
    m_drawNodes.clear();
    terrain.selectNodes(camera, aspectRatio, m_drawNodes);

    if (m_drawNodes.empty()) return;

    // Set up shader
    m_terrainShader.use();

    // Camera uniforms
    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 proj = camera.getProjectionMatrix(aspectRatio);
    glm::mat4 viewProj = proj * view;

    m_terrainShader.setMat4("u_viewProjection", viewProj);
    m_terrainShader.setMat4("u_view", view);
    m_terrainShader.setVec3("u_viewPos", camera.getPosition());
    m_terrainShader.setVec3("u_cameraPos", camera.getPosition());
    m_terrainShader.setVec4("u_clipPlane", clipPlane);

    // Terrain uniforms
    m_terrainShader.setFloat("u_heightScale", config.heightScale);
    m_terrainShader.setVec2("u_terrainSize", glm::vec2(terrain.getWorldWidth(),
                                                        terrain.getWorldDepth()));
    m_terrainShader.setVec2("u_terrainOrigin", glm::vec2(config.origin.x,
                                                          config.origin.z));
    m_terrainShader.setInt("u_gridResolution", config.gridResolution);

    // LOD ranges for per-vertex morphing
    const auto& lodRanges = terrain.getLodRanges();
    m_terrainShader.setInt("u_maxLodLevels", config.maxLodLevels);
    for (int i = 0; i < config.maxLodLevels && i < 8; ++i)
    {
        m_terrainShader.setFloat("u_lodRanges[" + std::to_string(i) + "]",
                                 lodRanges[static_cast<size_t>(i)]);
    }

    // Bind heightmap (texture unit 0)
    glBindTextureUnit(0, terrain.getHeightmapTexture());
    m_terrainShader.setInt("u_heightmap", 0);

    // Bind normal map (texture unit 1)
    glBindTextureUnit(1, terrain.getNormalMapTexture());
    m_terrainShader.setInt("u_normalMap", 1);

    // Bind splatmap (texture unit 2)
    glBindTextureUnit(2, terrain.getSplatmapTexture());
    m_terrainShader.setInt("u_splatmap", 2);

    // Lighting
    if (sceneData.hasDirectionalLight)
    {
        m_terrainShader.setVec3("u_lightDirection", sceneData.directionalLight.direction);
        m_terrainShader.setVec3("u_lightColor", sceneData.directionalLight.diffuse);
        m_terrainShader.setVec3("u_ambientColor", sceneData.directionalLight.ambient);
    }
    else
    {
        m_terrainShader.setVec3("u_lightDirection", glm::vec3(0.0f, -1.0f, 0.0f));
        m_terrainShader.setVec3("u_lightColor", glm::vec3(1.0f));
        m_terrainShader.setVec3("u_ambientColor", glm::vec3(0.15f));
    }

    // Caustics uniforms for underwater terrain
    m_terrainShader.setBool("u_causticsEnabled", m_causticsEnabled);
    if (m_causticsEnabled && m_causticsTexture != 0)
    {
        glBindTextureUnit(5, m_causticsTexture);
        m_terrainShader.setInt("u_causticsTex", 5);
        m_terrainShader.setFloat("u_causticsScale", 0.1f);
        m_terrainShader.setFloat("u_causticsIntensity", 0.15f);
        m_terrainShader.setFloat("u_causticsTime", m_causticsTime);
        m_terrainShader.setFloat("u_waterY", m_causticsWaterY);
        m_terrainShader.setVec2("u_waterCenter", m_causticsCenter);
        m_terrainShader.setVec2("u_waterHalfExtent", m_causticsHalfExtent);
    }
    else
    {
        glBindTextureUnit(5, 0);
        m_terrainShader.setInt("u_causticsTex", 5);
    }

    // Shadow uniforms
    bool hasShadows = (csm != nullptr && sceneData.hasDirectionalLight);
    m_terrainShader.setBool("u_hasShadows", hasShadows);
    if (hasShadows)
    {
        csm->bindShadowTexture(3);
        m_terrainShader.setInt("u_cascadeShadowMap", 3);

        int cascadeCount = csm->getCascadeCount();
        m_terrainShader.setInt("u_cascadeCount", cascadeCount);
        for (int i = 0; i < cascadeCount; ++i)
        {
            std::string idx = std::to_string(i);
            m_terrainShader.setFloat("u_cascadeSplits[" + idx + "]",
                                     csm->getCascadeSplit(i));
            m_terrainShader.setMat4("u_cascadeLightSpaceMatrices[" + idx + "]",
                                    csm->getLightSpaceMatrix(i));
        }
    }

    // Draw each selected node
    glBindVertexArray(m_gridVao);

    m_lastDrawCallCount = 0;
    m_lastTriangleCount = 0;

    for (const auto& node : m_drawNodes)
    {
        m_terrainShader.setVec2("u_nodeOffset", node.worldOffset);
        m_terrainShader.setFloat("u_nodeScale", node.scale);
        m_terrainShader.setInt("u_lodLevel", node.lodLevel);

        glDrawElements(GL_TRIANGLES, m_gridIndexCount, GL_UNSIGNED_INT, nullptr);
        m_lastDrawCallCount++;
        m_lastTriangleCount += m_gridIndexCount / 3;
    }
}

void TerrainRenderer::renderShadow(const Terrain& terrain,
                                   const Camera& camera,
                                   float aspectRatio,
                                   const glm::mat4& lightSpaceMatrix)
{
    if (!m_initialized || !terrain.isInitialized()) return;

    const auto& config = terrain.getConfig();

    // Ensure grid mesh
    if (m_gridResolution != config.gridResolution)
    {
        createGridMesh(config.gridResolution);
    }

    // Select visible nodes (from camera perspective for consistent geometry)
    m_drawNodes.clear();
    terrain.selectNodes(camera, aspectRatio, m_drawNodes);

    if (m_drawNodes.empty()) return;

    m_shadowShader.use();
    m_shadowShader.setMat4("u_lightSpaceMatrix", lightSpaceMatrix);
    m_shadowShader.setVec3("u_cameraPos", camera.getPosition());

    // Terrain uniforms for heightmap sampling
    m_shadowShader.setFloat("u_heightScale", config.heightScale);
    m_shadowShader.setVec2("u_terrainSize", glm::vec2(terrain.getWorldWidth(),
                                                       terrain.getWorldDepth()));
    m_shadowShader.setVec2("u_terrainOrigin", glm::vec2(config.origin.x,
                                                         config.origin.z));
    m_shadowShader.setInt("u_gridResolution", config.gridResolution);

    // LOD ranges for per-vertex morphing
    const auto& lodRanges = terrain.getLodRanges();
    m_shadowShader.setInt("u_maxLodLevels", config.maxLodLevels);
    for (int i = 0; i < config.maxLodLevels && i < 8; ++i)
    {
        m_shadowShader.setFloat("u_lodRanges[" + std::to_string(i) + "]",
                                 lodRanges[static_cast<size_t>(i)]);
    }

    // Bind heightmap
    glBindTextureUnit(0, terrain.getHeightmapTexture());
    m_shadowShader.setInt("u_heightmap", 0);

    glBindVertexArray(m_gridVao);

    for (const auto& node : m_drawNodes)
    {
        m_shadowShader.setVec2("u_nodeOffset", node.worldOffset);
        m_shadowShader.setFloat("u_nodeScale", node.scale);
        m_shadowShader.setInt("u_lodLevel", node.lodLevel);

        glDrawElements(GL_TRIANGLES, m_gridIndexCount, GL_UNSIGNED_INT, nullptr);
    }
}

// ---------------------------------------------------------------------------
// Grid mesh creation
// ---------------------------------------------------------------------------

void TerrainRenderer::createGridMesh(int resolution)
{
    // Clean up existing
    if (m_gridVao) glDeleteVertexArrays(1, &m_gridVao);
    if (m_gridVbo) glDeleteBuffers(1, &m_gridVbo);
    if (m_gridEbo) glDeleteBuffers(1, &m_gridEbo);

    m_gridResolution = resolution;

    // Generate vertices: regular grid from (0,0) to (1,1) + skirt ring.
    // Each vertex is vec3(u, v, skirtFlag). skirtFlag=0 for normal, 1 for skirt.
    // Skirt vertices duplicate the perimeter and are pushed down in the vertex shader
    // to fill any gaps between adjacent CDLOD nodes at different LOD levels.
    int mainVertCount = resolution * resolution;
    int perimeterVerts = (resolution - 1) * 4;  // One skirt vert per perimeter edge vertex
    int totalVertCount = mainVertCount + perimeterVerts;

    std::vector<glm::vec3> vertices;
    vertices.reserve(static_cast<size_t>(totalVertCount));

    // Main grid
    for (int z = 0; z < resolution; ++z)
    {
        for (int x = 0; x < resolution; ++x)
        {
            float u = static_cast<float>(x) / static_cast<float>(resolution - 1);
            float v = static_cast<float>(z) / static_cast<float>(resolution - 1);
            vertices.push_back(glm::vec3(u, v, 0.0f));
        }
    }

    // Skirt vertices: duplicates of perimeter vertices with skirt flag = 1
    // Bottom edge (z=0): x from 0 to resolution-2
    for (int x = 0; x < resolution - 1; ++x)
    {
        float u = static_cast<float>(x) / static_cast<float>(resolution - 1);
        vertices.push_back(glm::vec3(u, 0.0f, 1.0f));
    }
    // Right edge (x=resolution-1): z from 0 to resolution-2
    for (int z = 0; z < resolution - 1; ++z)
    {
        float v = static_cast<float>(z) / static_cast<float>(resolution - 1);
        vertices.push_back(glm::vec3(1.0f, v, 1.0f));
    }
    // Top edge (z=resolution-1): x from resolution-1 down to 1
    for (int x = resolution - 1; x > 0; --x)
    {
        float u = static_cast<float>(x) / static_cast<float>(resolution - 1);
        vertices.push_back(glm::vec3(u, 1.0f, 1.0f));
    }
    // Left edge (x=0): z from resolution-1 down to 1
    for (int z = resolution - 1; z > 0; --z)
    {
        float v = static_cast<float>(z) / static_cast<float>(resolution - 1);
        vertices.push_back(glm::vec3(0.0f, v, 1.0f));
    }

    // Generate main grid indices (two triangles per quad)
    int quadsPerSide = resolution - 1;
    int mainIndexCount = quadsPerSide * quadsPerSide * 6;
    int skirtIndexCount = perimeterVerts * 6;  // 2 triangles per skirt segment
    int indexCount = mainIndexCount + skirtIndexCount;
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(indexCount));

    // Main grid triangles
    for (int z = 0; z < quadsPerSide; ++z)
    {
        for (int x = 0; x < quadsPerSide; ++x)
        {
            uint32_t topLeft = static_cast<uint32_t>(z * resolution + x);
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = static_cast<uint32_t>((z + 1) * resolution + x);
            uint32_t bottomRight = bottomLeft + 1;

            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    // Skirt triangles: connect each perimeter vertex to its skirt duplicate.
    // For each edge segment, create a quad (2 tris) between the main edge and skirt edge.
    int skirtBase = mainVertCount;

    // Bottom edge skirt
    for (int x = 0; x < resolution - 1; ++x)
    {
        uint32_t mainA = static_cast<uint32_t>(x);           // main grid (z=0, x)
        uint32_t mainB = static_cast<uint32_t>(x + 1);       // main grid (z=0, x+1)
        uint32_t skirtA = static_cast<uint32_t>(skirtBase + x);
        uint32_t skirtB = static_cast<uint32_t>(skirtBase + x + 1);
        // Handle wrap for last segment
        if (x == resolution - 2) skirtB = static_cast<uint32_t>(skirtBase + resolution - 1);

        indices.push_back(mainA);
        indices.push_back(skirtA);
        indices.push_back(mainB);

        indices.push_back(mainB);
        indices.push_back(skirtA);
        indices.push_back(skirtB);
    }

    // Right edge skirt
    int rightSkirtBase = skirtBase + (resolution - 1);
    for (int z = 0; z < resolution - 1; ++z)
    {
        uint32_t mainA = static_cast<uint32_t>(z * resolution + (resolution - 1));
        uint32_t mainB = static_cast<uint32_t>((z + 1) * resolution + (resolution - 1));
        uint32_t skirtA = static_cast<uint32_t>(rightSkirtBase + z);
        uint32_t skirtB = static_cast<uint32_t>(rightSkirtBase + z + 1);
        if (z == resolution - 2) skirtB = static_cast<uint32_t>(rightSkirtBase + resolution - 1);

        indices.push_back(mainA);
        indices.push_back(mainB);
        indices.push_back(skirtA);

        indices.push_back(mainB);
        indices.push_back(skirtB);
        indices.push_back(skirtA);
    }

    // Top edge skirt
    int topSkirtBase = rightSkirtBase + (resolution - 1);
    for (int i = 0; i < resolution - 1; ++i)
    {
        int x = resolution - 1 - i;
        uint32_t mainA = static_cast<uint32_t>((resolution - 1) * resolution + x);
        uint32_t mainB = static_cast<uint32_t>((resolution - 1) * resolution + x - 1);
        uint32_t skirtA = static_cast<uint32_t>(topSkirtBase + i);
        uint32_t skirtB = static_cast<uint32_t>(topSkirtBase + i + 1);
        if (i == resolution - 2) skirtB = static_cast<uint32_t>(topSkirtBase + resolution - 1);

        indices.push_back(mainA);
        indices.push_back(skirtA);
        indices.push_back(mainB);

        indices.push_back(mainB);
        indices.push_back(skirtA);
        indices.push_back(skirtB);
    }

    // Left edge skirt
    int leftSkirtBase = topSkirtBase + (resolution - 1);
    for (int i = 0; i < resolution - 1; ++i)
    {
        int z = resolution - 1 - i;
        uint32_t mainA = static_cast<uint32_t>(z * resolution);
        uint32_t mainB = static_cast<uint32_t>((z - 1) * resolution);
        uint32_t skirtA = static_cast<uint32_t>(leftSkirtBase + i);
        uint32_t skirtB = static_cast<uint32_t>(leftSkirtBase + i + 1);
        if (i == resolution - 2) skirtB = static_cast<uint32_t>(skirtBase);  // Wrap to first skirt vert

        indices.push_back(mainA);
        indices.push_back(mainB);
        indices.push_back(skirtA);

        indices.push_back(mainB);
        indices.push_back(skirtB);
        indices.push_back(skirtA);
    }

    m_gridIndexCount = static_cast<int>(indices.size());

    // Create VAO/VBO/EBO (DSA)
    glCreateVertexArrays(1, &m_gridVao);
    glCreateBuffers(1, &m_gridVbo);
    glCreateBuffers(1, &m_gridEbo);

    // Upload data (immutable, static)
    glNamedBufferStorage(m_gridVbo,
                         static_cast<GLsizeiptr>(vertices.size() * sizeof(glm::vec3)),
                         vertices.data(), 0);

    glNamedBufferStorage(m_gridEbo,
                         static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                         indices.data(), 0);

    // Bind VBO to VAO binding point 0
    glVertexArrayVertexBuffer(m_gridVao, 0, m_gridVbo, 0, sizeof(glm::vec3));

    // Bind EBO to VAO
    glVertexArrayElementBuffer(m_gridVao, m_gridEbo);

    // Vertex attribute: location 0 = vec3 (gridPos.xy + skirtFlag.z)
    glEnableVertexArrayAttrib(m_gridVao, 0);
    glVertexArrayAttribFormat(m_gridVao, 0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(m_gridVao, 0, 0);

    Logger::info("TerrainRenderer: created " + std::to_string(resolution)
                 + "x" + std::to_string(resolution) + " grid mesh ("
                 + std::to_string(m_gridIndexCount / 3) + " triangles)");
}

// ---------------------------------------------------------------------------
// Default textures
// ---------------------------------------------------------------------------

void TerrainRenderer::generateDefaultTextures()
{
    // Simple 1x1 green albedo
    uint8_t greenPixel[4] = {80, 140, 50, 255};
    glCreateTextures(GL_TEXTURE_2D, 1, &m_defaultAlbedo);
    glTextureStorage2D(m_defaultAlbedo, 1, GL_RGBA8, 1, 1);
    glTextureSubImage2D(m_defaultAlbedo, 0, 0, 0, 1, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, greenPixel);
    glTextureParameteri(m_defaultAlbedo, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_defaultAlbedo, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Flat normal (0.5, 0.5, 1.0) = (0, 0, 1) in tangent space
    uint8_t normalPixel[3] = {128, 128, 255};
    glCreateTextures(GL_TEXTURE_2D, 1, &m_defaultNormal);
    glTextureStorage2D(m_defaultNormal, 1, GL_RGB8, 1, 1);
    glTextureSubImage2D(m_defaultNormal, 0, 0, 0, 1, 1,
                        GL_RGB, GL_UNSIGNED_BYTE, normalPixel);
    glTextureParameteri(m_defaultNormal, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_defaultNormal, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

void TerrainRenderer::setCausticsParams(bool enabled, float waterY, float time, GLuint causticsTexture,
                                         const glm::vec2& center, const glm::vec2& halfExtent)
{
    m_causticsEnabled = enabled;
    m_causticsWaterY = waterY;
    m_causticsTime = time;
    m_causticsTexture = causticsTexture;
    m_causticsCenter = center;
    m_causticsHalfExtent = halfExtent;
}

} // namespace Vestige
