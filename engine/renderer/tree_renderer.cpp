// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file tree_renderer.cpp
/// @brief TreeRenderer — real artist trees, 3-bucket mesh LOD + billboard crossfade.
///
/// Design: docs/phases/phase_10_meadow_realism_c_trees_plants_design.md (3D_E-0033).
/// Deviation from §4.4: the doc describes flattening node transforms *into
/// vertices* at load. The engine's Mesh discards its CPU vertex data after
/// upload(), so that is not implementable against a ResourceManager-loaded
/// Model. The functionally identical path used here keeps the shared cached
/// Model + Mesh VAOs untouched and applies each node's baked world matrix as a
/// per-draw `u_nodeMatrix` uniform in the vertex shader — same rendered result,
/// full ResourceManager reuse (Rule 3), no per-instance vertex duplication.
#include "renderer/tree_renderer.h"
#include "renderer/cascaded_shadow_map.h"
#include "renderer/sampler_fallback.h"
#include "renderer/scoped_blend_state.h"
#include "renderer/scoped_cull_face.h"
#include "resource/model.h"
#include "resource/resource_manager.h"
#include "core/logger.h"

#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>

namespace Vestige
{

namespace
{
/// @brief Pre-sized shared instance buffer capacity — a meadow rarely has more
///        than a few hundred trees of one species in one tier at once, so this
///        holds without a mid-frame grow (256 KB mat4 + 8 KB alpha).
constexpr int INITIAL_MESH_CAPACITY = 4096;
}

TreeRenderer::~TreeRenderer()
{
    shutdown();
}

bool TreeRenderer::init(const std::string& assetPath)
{
    if (m_initialized)
    {
        return true;
    }

    if (!m_meshShader.loadFromFiles(assetPath + "/shaders/tree_mesh.vert.glsl",
                                     assetPath + "/shaders/tree_mesh.frag.glsl"))
    {
        Logger::error("Failed to load tree mesh shaders");
        return false;
    }
    // Shadow-caster shader (LOD0 + mid → cascade depth + RSM flux, T4). Trees
    // still render if this fails; they just won't cast ground shadows.
    if (!m_shadowShader.loadFromFiles(assetPath + "/shaders/tree_shadow.vert.glsl",
                                       assetPath + "/shaders/tree_shadow.frag.glsl"))
    {
        Logger::warning("Failed to load tree shadow shaders — trees won't cast shadows");
    }
    // Shared instanced-mesh streams (model mat4 @ binding 1, crossfade alpha @ binding 3).
    glCreateBuffers(1, &m_meshInstanceVbo);
    glCreateBuffers(1, &m_meshAlphaVbo);
    m_meshInstanceCapacity = INITIAL_MESH_CAPACITY;
    glNamedBufferStorage(m_meshInstanceVbo,
                         m_meshInstanceCapacity * static_cast<GLsizeiptr>(sizeof(glm::mat4)),
                         nullptr, GL_DYNAMIC_STORAGE_BIT);
    glNamedBufferStorage(m_meshAlphaVbo,
                         m_meshInstanceCapacity * static_cast<GLsizeiptr>(sizeof(float)),
                         nullptr, GL_DYNAMIC_STORAGE_BIT);

    m_initialized = true;
    Logger::info("Tree renderer initialized");
    return true;
}

void TreeRenderer::shutdown()
{
    if (m_meshInstanceVbo != 0) { glDeleteBuffers(1, &m_meshInstanceVbo); m_meshInstanceVbo = 0; }
    if (m_meshAlphaVbo != 0) { glDeleteBuffers(1, &m_meshAlphaVbo); m_meshAlphaVbo = 0; }
    m_meshInstanceCapacity = 0;
    m_species.clear();
    m_lod0BySpecies.clear();
    m_midBySpecies.clear();
    m_bbBySpecies.clear();
    m_shadowLod0BySpecies.clear();
    m_shadowMidBySpecies.clear();
    m_meshShader.destroy();
    m_shadowShader.destroy();
    m_initialized = false;
}

void TreeRenderer::buildDrawList(const Model& model, std::vector<PrimDraw>& out) const
{
    out.clear();
    // Recursive node walk composing world matrices (design §4.4). The produced
    // packs are flat (root nodes with their own TRS), but the walk handles a
    // nested hierarchy generically so any glTF loads correctly.
    struct Frame { int node; glm::mat4 parent; };
    std::vector<Frame> stack;
    for (auto it = model.m_rootNodes.rbegin(); it != model.m_rootNodes.rend(); ++it)
    {
        stack.push_back({*it, glm::mat4(1.0f)});
    }
    while (!stack.empty())
    {
        Frame f = stack.back();
        stack.pop_back();
        if (f.node < 0 || f.node >= static_cast<int>(model.m_nodes.size()))
        {
            continue;
        }
        const ModelNode& node = model.m_nodes[static_cast<size_t>(f.node)];
        glm::mat4 world = f.parent * node.computeLocalMatrix();
        for (int primIdx : node.primitiveIndices)
        {
            if (primIdx < 0 || primIdx >= static_cast<int>(model.m_primitives.size()))
            {
                continue;
            }
            const ModelPrimitive& prim = model.m_primitives[static_cast<size_t>(primIdx)];
            const Material* mat = nullptr;
            if (prim.materialIndex >= 0
                && prim.materialIndex < static_cast<int>(model.m_materials.size()))
            {
                mat = model.m_materials[static_cast<size_t>(prim.materialIndex)].get();
            }
            out.push_back({prim.mesh.get(), world, mat});
        }
        for (int child : node.childIndices)
        {
            stack.push_back({child, world});
        }
    }
}

void TreeRenderer::loadSpecies(const std::vector<TreeSpeciesConfig>& configs,
                               ResourceManager& resources)
{
    namespace fs = std::filesystem;
    m_species.clear();

    for (const TreeSpeciesConfig& cfg : configs)
    {
        TreeSpecies sp;
        sp.lod0 = resources.loadModel(cfg.meshPath);
        if (!sp.lod0)
        {
            Logger::warning("Tree species LOD0 missing, skipped: " + cfg.meshPath);
            continue;
        }
        buildDrawList(*sp.lod0, sp.lod0Prims);

        // Mid tier = the LOD0 path with the "_lod0" stem swapped for "_lod2".
        std::string midPath = cfg.meshPath;
        const std::string tag = "_lod0";
        size_t pos = midPath.rfind(tag);
        if (pos != std::string::npos)
        {
            midPath.replace(pos, tag.size(), "_lod2");
            if (fs::exists(midPath))
            {
                sp.lodMid = resources.loadModel(midPath);
                if (sp.lodMid)
                {
                    buildDrawList(*sp.lodMid, sp.midPrims);
                }
            }
        }

        // Far impostor (LOD3): the artist's billboard glTF is a small 3-D cloud
        // of leaf cards with the atlas UVs baked in. Draw it as a mesh tier — its
        // baked UVs crop the atlas correctly (a flat card would show the whole
        // 3×3 view-grid at once). Config's billboardTexturePath is that glTF path.
        if (!cfg.billboardTexturePath.empty() && fs::exists(cfg.billboardTexturePath))
        {
            sp.billboard = resources.loadModel(cfg.billboardTexturePath);
            if (sp.billboard)
            {
                buildDrawList(*sp.billboard, sp.billboardPrims);
            }
        }

        m_species.push_back(std::move(sp));
    }

    m_lod0BySpecies.assign(m_species.size(), {});
    m_midBySpecies.assign(m_species.size(), {});
    m_bbBySpecies.assign(m_species.size(), {});
    m_shadowLod0BySpecies.assign(m_species.size(), {});
    m_shadowMidBySpecies.assign(m_species.size(), {});
    bindInstanceBuffers();

    Logger::info("Tree renderer: " + std::to_string(m_species.size()) + " species loaded");
}

void TreeRenderer::bindInstanceBuffers()
{
    // Point every species-tier mesh VAO at the shared instance (mat4 @6-9,
    // binding 1) + crossfade-alpha (float @12, binding 3) streams. Re-run after
    // a grow reallocated a buffer. Repeated setup on a shared/duplicate mesh is
    // idempotent.
    auto setup = [this](const std::vector<PrimDraw>& prims)
    {
        for (const PrimDraw& p : prims)
        {
            if (!p.mesh)
            {
                continue;
            }
            p.mesh->setupInstanceAttributes(m_meshInstanceVbo);  // binding 1, loc 6-9
            GLuint vao = p.mesh->getVao();
            glVertexArrayVertexBuffer(vao, 3, m_meshAlphaVbo, 0, sizeof(float));
            glVertexArrayBindingDivisor(vao, 3, 1);
            glEnableVertexArrayAttrib(vao, 12);
            glVertexArrayAttribFormat(vao, 12, 1, GL_FLOAT, GL_FALSE, 0);
            glVertexArrayAttribBinding(vao, 12, 3);
        }
    };
    for (const TreeSpecies& sp : m_species)
    {
        setup(sp.lod0Prims);
        setup(sp.midPrims);
        setup(sp.billboardPrims);
    }
}

void TreeRenderer::ensureInstanceCapacity(int count)
{
    if (count <= m_meshInstanceCapacity)
    {
        return;
    }
    m_meshInstanceCapacity = count + count / 4 + 64;
    glDeleteBuffers(1, &m_meshInstanceVbo);
    glDeleteBuffers(1, &m_meshAlphaVbo);
    glCreateBuffers(1, &m_meshInstanceVbo);
    glCreateBuffers(1, &m_meshAlphaVbo);
    glNamedBufferStorage(m_meshInstanceVbo,
                         m_meshInstanceCapacity * static_cast<GLsizeiptr>(sizeof(glm::mat4)),
                         nullptr, GL_DYNAMIC_STORAGE_BIT);
    glNamedBufferStorage(m_meshAlphaVbo,
                         m_meshInstanceCapacity * static_cast<GLsizeiptr>(sizeof(float)),
                         nullptr, GL_DYNAMIC_STORAGE_BIT);
    bindInstanceBuffers();  // re-point every VAO at the new buffers
}

void TreeRenderer::setLightingUniforms(Shader& shader, CascadedShadowMap* csm,
                                       const DirectionalLight* dirLight)
{
    // Copy of the foliage renderer's shared CSM + light block (§4.6), incl. the
    // Mesa fallback: an unbound sampler2DArray defaults to unit 0 (a sampler2D),
    // which fails GL_INVALID_OPERATION on Mesa — so bind a fallback every draw.
    bool hasShadows = (csm != nullptr && dirLight != nullptr);
    shader.setBool("u_hasShadows", hasShadows);

    if (dirLight)
    {
        shader.setVec3("u_lightDirection", dirLight->direction);
        shader.setVec3("u_lightColor", dirLight->diffuse);
        shader.setVec3("u_ambientColor", dirLight->ambient);
        shader.setBool("u_hasDirectionalLight", true);
    }
    else
    {
        shader.setBool("u_hasDirectionalLight", false);
    }

    if (hasShadows)
    {
        csm->bindShadowTexture(3);
        shader.setInt("u_cascadeShadowMap", 3);
        int cascadeCount = csm->getCascadeCount();
        shader.setInt("u_cascadeCount", cascadeCount);
        static const char* splitNames[4] = {
            "u_cascadeSplits[0]", "u_cascadeSplits[1]",
            "u_cascadeSplits[2]", "u_cascadeSplits[3]"};
        static const char* matNames[4] = {
            "u_cascadeLightSpaceMatrices[0]", "u_cascadeLightSpaceMatrices[1]",
            "u_cascadeLightSpaceMatrices[2]", "u_cascadeLightSpaceMatrices[3]"};
        for (int i = 0; i < cascadeCount && i < 4; ++i)
        {
            shader.setFloat(splitNames[i], csm->getCascadeSplit(i));
            shader.setMat4(matNames[i], csm->getLightSpaceMatrix(i));
        }
    }
    else
    {
        glBindTextureUnit(3, sharedSamplerFallback().getSampler2DArray());
        shader.setInt("u_cascadeShadowMap", 3);
        shader.setInt("u_cascadeCount", 0);
    }
}

void TreeRenderer::drawMeshTier(const std::vector<PrimDraw>& prims,
                                const std::vector<Bucketed>& bucket,
                                bool forceAlphaTest,
                                float forceCutoff)
{
    if (prims.empty() || bucket.empty())
    {
        return;
    }
    int count = static_cast<int>(bucket.size());
    ensureInstanceCapacity(count);

    m_matScratch.resize(bucket.size());
    m_alphaScratch.resize(bucket.size());
    for (size_t i = 0; i < bucket.size(); ++i)
    {
        m_matScratch[i] = bucket[i].model;
        m_alphaScratch[i] = bucket[i].alpha;
    }
    glNamedBufferSubData(m_meshInstanceVbo, 0,
                         count * static_cast<GLsizeiptr>(sizeof(glm::mat4)), m_matScratch.data());
    glNamedBufferSubData(m_meshAlphaVbo, 0,
                         count * static_cast<GLsizeiptr>(sizeof(float)), m_alphaScratch.data());

    for (const PrimDraw& prim : prims)
    {
        if (!prim.mesh)
        {
            continue;
        }
        m_meshShader.setMat4("u_nodeMatrix", prim.nodeMatrix);

        bool hasTex = false;
        glm::vec3 albedo(0.30f, 0.40f, 0.20f);
        bool doubleSided = false;
        bool useAlphaTest = false;
        float cutoff = 0.5f;
        if (prim.material)
        {
            if (prim.material->hasDiffuseTexture())
            {
                glBindTextureUnit(0, prim.material->getDiffuseTexture()->getId());
                hasTex = true;
            }
            albedo = (prim.material->getType() == MaterialType::PBR)
                         ? prim.material->getAlbedo()
                         : prim.material->getDiffuseColor();
            doubleSided = prim.material->isDoubleSided();
            // Leaf/foliage atlases are tagged MASK on some species (fir) but BLEND on
            // others (maple, pine) — both are cutout foliage, not translucent glass.
            // Treat ANY non-OPAQUE material as a two-sided alpha-cutout leaf so it gets
            // the clean silhouette + two-sided abs() lighting (the u_useAlphaTest path).
            // A BLEND cluster left on the signed bark path renders near-black once
            // normal-mapped (T8). Bark is OPAQUE → stays on the signed one-sided path.
            AlphaMode am = prim.material->getAlphaMode();
            useAlphaTest = (am != AlphaMode::OPAQUE);
            // BLEND carries no authored glTF cutoff; use the tuned value the far
            // impostor's leaf atlas already uses. MASK provides its own.
            cutoff = (am == AlphaMode::MASK) ? prim.material->getAlphaCutoff() : 0.4f;
        }
        // The far impostor's atlas is BLEND; without depth sorting that haloes,
        // so cut it out at a fixed threshold instead (order-independent).
        if (forceAlphaTest && hasTex)
        {
            useAlphaTest = true;
            cutoff = forceCutoff;
        }
        m_meshShader.setBool("u_hasTexture", hasTex);
        m_meshShader.setVec3("u_albedo", albedo);
        m_meshShader.setBool("u_useAlphaTest", useAlphaTest);
        m_meshShader.setFloat("u_alphaCutoff", cutoff);

        // Normal map (unit 1, T8): per-pixel relief on leaf/bark cards so light
        // plays across the canopy. Bind a fallback sampler2D when absent so unit 1
        // is never left unbound (Mesa-safe, mirrors the CSM fallback on unit 3).
        bool hasNormalMap = prim.material && prim.material->hasNormalMap()
                            && prim.material->getNormalMap();
        glBindTextureUnit(1, hasNormalMap ? prim.material->getNormalMap()->getId()
                                          : sharedSamplerFallback().getSampler2D());
        m_meshShader.setInt("u_normalMap", 1);
        m_meshShader.setBool("u_hasNormalMap", hasNormalMap);

        if (doubleSided)
        {
            glDisable(GL_CULL_FACE);
        }
        else
        {
            glEnable(GL_CULL_FACE);
        }

        glBindVertexArray(prim.mesh->getVao());
        glDrawElementsInstanced(GL_TRIANGLES,
                                static_cast<GLsizei>(prim.mesh->getIndexCount()),
                                GL_UNSIGNED_INT, nullptr, count);
    }
}

void TreeRenderer::render(const std::vector<const FoliageChunk*>& chunks,
                          const Camera& camera,
                          const glm::mat4& viewProjection,
                          float time,
                          CascadedShadowMap* csm,
                          const DirectionalLight* dirLight,
                          const glm::vec4& clipPlane)
{
    if (!m_initialized || m_species.empty() || chunks.empty())
    {
        return;
    }

    const glm::vec3 camPos = camera.getPosition();

    for (auto& v : m_lod0BySpecies) { v.clear(); }
    for (auto& v : m_midBySpecies) { v.clear(); }
    for (auto& v : m_bbBySpecies) { v.clear(); }

    // --- Bucket every visible tree into a tier (with crossfade alpha) -------
    for (const FoliageChunk* chunk : chunks)
    {
        for (const TreeInstance& tree : chunk->getTrees())
        {
            if (tree.speciesIndex >= m_species.size())
            {
                continue;
            }
            const float dist = glm::distance(camPos, tree.position);
            if (dist > maxDistance)
            {
                continue;
            }
            const uint32_t si = tree.speciesIndex;
            const TreeSpecies& sp = m_species[si];

            glm::mat4 model = glm::translate(glm::mat4(1.0f), tree.position);
            model = glm::rotate(model, tree.rotation, glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::scale(model, glm::vec3(tree.scale));

            const bool hasMid = !sp.midPrims.empty();
            const bool hasBB = !sp.billboardPrims.empty();

            auto pushLod0 = [&](float a) { m_lod0BySpecies[si].push_back({model, a}); };
            auto pushMid = [&](float a) { m_midBySpecies[si].push_back({model, a}); };
            auto pushBB = [&](float a) { m_bbBySpecies[si].push_back({model, a}); };

            // Effective near→far switch: mid collapses to LOD0-then-billboard,
            // billboard-less species hold their last mesh tier to maxDistance.
            const float nearSwitch = lodDistance;
            const float farSwitch = hasMid ? billboardDistance : lodDistance;

            if (dist < nearSwitch)
            {
                pushLod0(1.0f);
            }
            else if (hasMid && dist < nearSwitch + fadeRange)
            {
                const float t = (dist - nearSwitch) / fadeRange;
                pushLod0(1.0f - t);
                pushMid(t);
            }
            else if (dist < farSwitch)
            {
                // Steady mid band (only reached when hasMid).
                pushMid(1.0f);
            }
            else if (hasBB && dist < farSwitch + fadeRange)
            {
                // Crossfade the last mesh tier (mid if present, else LOD0) → billboard.
                const float t = (dist - farSwitch) / fadeRange;
                if (hasMid) { pushMid(1.0f - t); }
                else { pushLod0(1.0f - t); }
                pushBB(t);
            }
            else if (hasBB)
            {
                pushBB(1.0f);
            }
            else if (hasMid)
            {
                pushMid(1.0f);   // no billboard → mid holds to maxDistance
            }
            else
            {
                pushLod0(1.0f);  // no mid, no billboard → LOD0 holds to maxDistance
            }
        }
    }

    // --- Mesh tiers (LOD0 + mid), per species, per material ----------------
    m_meshShader.use();
    m_meshShader.setMat4("u_viewProjection", viewProjection);
    m_meshShader.setMat4("u_view", camera.getViewMatrix());
    m_meshShader.setVec3("u_cameraPos", camPos);
    m_meshShader.setFloat("u_time", time);
    m_meshShader.setVec3("u_windDirection", glm::normalize(windDirection));
    m_meshShader.setFloat("u_windAmplitude", windAmplitude);
    m_meshShader.setFloat("u_windFrequency", windFrequency);
    m_meshShader.setVec4("u_clipPlane", clipPlane);
    m_meshShader.setInt("u_texture", 0);
    setLightingUniforms(m_meshShader, csm, dirLight);

    {
        // Blend on for the crossfade bands; cull toggled per-material inside.
        // All three tiers are meshes; the far impostor forces an alpha-cutout
        // (its atlas is BLEND — cutout is order-independent, no halo).
        ScopedBlendState blendGuard{true, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA};
        ScopedCullFace cullGuard{true};
        for (size_t si = 0; si < m_species.size(); ++si)
        {
            drawMeshTier(m_species[si].lod0Prims, m_lod0BySpecies[si]);
            drawMeshTier(m_species[si].midPrims, m_midBySpecies[si]);
            drawMeshTier(m_species[si].billboardPrims, m_bbBySpecies[si], true, 0.4f);
        }
    }
}

void TreeRenderer::drawShadowTier(const std::vector<PrimDraw>& prims,
                                  const std::vector<Bucketed>& bucket)
{
    if (prims.empty() || bucket.empty())
    {
        return;
    }
    int count = static_cast<int>(bucket.size());
    ensureInstanceCapacity(count);

    // Depth pass needs only the model matrices (no crossfade alpha — @12 is
    // unread by tree_shadow.vert).
    m_matScratch.resize(bucket.size());
    for (size_t i = 0; i < bucket.size(); ++i)
    {
        m_matScratch[i] = bucket[i].model;
    }
    glNamedBufferSubData(m_meshInstanceVbo, 0,
                         count * static_cast<GLsizeiptr>(sizeof(glm::mat4)), m_matScratch.data());

    for (const PrimDraw& prim : prims)
    {
        if (!prim.mesh)
        {
            continue;
        }
        m_shadowShader.setMat4("u_nodeMatrix", prim.nodeMatrix);

        bool hasTex = false;
        glm::vec3 albedo(0.30f, 0.40f, 0.20f);
        bool doubleSided = false;
        bool useAlphaTest = false;
        float cutoff = 0.5f;
        if (prim.material)
        {
            if (prim.material->hasDiffuseTexture())
            {
                glBindTextureUnit(0, prim.material->getDiffuseTexture()->getId());
                hasTex = true;
            }
            albedo = (prim.material->getType() == MaterialType::PBR)
                         ? prim.material->getAlbedo()
                         : prim.material->getDiffuseColor();
            doubleSided = prim.material->isDoubleSided();
            useAlphaTest = (prim.material->getAlphaMode() == AlphaMode::MASK);
            cutoff = prim.material->getAlphaCutoff();
        }
        m_shadowShader.setBool("u_hasTexture", hasTex);
        m_shadowShader.setVec3("u_albedoFactor", albedo);
        m_shadowShader.setBool("u_useAlphaTest", useAlphaTest);
        m_shadowShader.setFloat("u_alphaCutoff", cutoff);

        // Two-sided leaves must not cull or half the canopy shadow drops out.
        if (doubleSided)
        {
            glDisable(GL_CULL_FACE);
        }
        else
        {
            glEnable(GL_CULL_FACE);
        }

        glBindVertexArray(prim.mesh->getVao());
        glDrawElementsInstanced(GL_TRIANGLES,
                                static_cast<GLsizei>(prim.mesh->getIndexCount()),
                                GL_UNSIGNED_INT, nullptr, count);
    }
}

void TreeRenderer::renderShadow(const std::vector<const FoliageChunk*>& chunks,
                                const Camera& camera,
                                const glm::mat4& lightSpaceMatrix,
                                float time,
                                const glm::vec3& lightRadiance,
                                const glm::vec3& lightDir)
{
    if (!m_initialized || m_species.empty() || chunks.empty() || m_shadowShader.getId() == 0)
    {
        return;
    }

    const glm::vec3 camPos = camera.getPosition();

    for (auto& v : m_shadowLod0BySpecies) { v.clear(); }
    for (auto& v : m_shadowMidBySpecies) { v.clear(); }

    // Bucket casters by camera distance into the SAME mesh tier the viewer sees
    // (LOD0 near, mid beyond) so the shadow tracks the drawn mesh. Trees past
    // billboardDistance are impostor-tier and do NOT cast (D4). No crossfade in
    // depth — the dominant tier casts across the transition band.
    for (const FoliageChunk* chunk : chunks)
    {
        for (const TreeInstance& tree : chunk->getTrees())
        {
            if (tree.speciesIndex >= m_species.size())
            {
                continue;
            }
            const float dist = glm::distance(camPos, tree.position);
            if (dist > billboardDistance)
            {
                continue;
            }
            const uint32_t si = tree.speciesIndex;
            const TreeSpecies& sp = m_species[si];

            glm::mat4 model = glm::translate(glm::mat4(1.0f), tree.position);
            model = glm::rotate(model, tree.rotation, glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::scale(model, glm::vec3(tree.scale));

            if (dist < lodDistance || sp.midPrims.empty())
            {
                m_shadowLod0BySpecies[si].push_back({model, 1.0f});
            }
            else
            {
                m_shadowMidBySpecies[si].push_back({model, 1.0f});
            }
        }
    }

    m_shadowShader.use();
    m_shadowShader.setMat4("u_lightSpaceMatrix", lightSpaceMatrix);
    m_shadowShader.setFloat("u_time", time);
    m_shadowShader.setVec3("u_windDirection", glm::normalize(windDirection));
    m_shadowShader.setFloat("u_windAmplitude", windAmplitude);
    m_shadowShader.setFloat("u_windFrequency", windFrequency);
    m_shadowShader.setVec3("u_lightRadiance", lightRadiance);
    m_shadowShader.setVec3("u_lightDir", lightDir);
    m_shadowShader.setInt("u_texture", 0);

    // Per-material cull toggling inside; RAII restores the shadow pass's prior
    // cull state so the caller (renderer shadow pass) stays composable.
    ScopedCullFace cullGuard{true};
    for (size_t si = 0; si < m_species.size(); ++si)
    {
        drawShadowTier(m_species[si].lod0Prims, m_shadowLod0BySpecies[si]);
        drawShadowTier(m_species[si].midPrims, m_shadowMidBySpecies[si]);
    }
}

} // namespace Vestige
