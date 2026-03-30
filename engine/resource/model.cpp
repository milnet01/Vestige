/// @file model.cpp
/// @brief Model instantiation implementation.
#include "resource/model.h"
#include "animation/skeleton_animator.h"
#include "scene/scene.h"
#include "scene/entity.h"
#include "scene/mesh_renderer.h"
#include "core/logger.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Vestige
{

glm::mat4 ModelNode::computeLocalMatrix() const
{
    if (hasMatrix)
    {
        return matrix;
    }

    // T * R * S
    glm::mat4 t = glm::translate(glm::mat4(1.0f), translation);
    glm::mat4 r = glm::mat4_cast(rotation);
    glm::mat4 s = glm::scale(glm::mat4(1.0f), scale);
    return t * r * s;
}

Entity* Model::instantiate(Scene& scene, Entity* parent, const std::string& name) const
{
    // Create root entity for this model instance
    Entity* root = nullptr;
    if (parent)
    {
        auto rootEntity = std::make_unique<Entity>(name);
        root = parent->addChild(std::move(rootEntity));
    }
    else
    {
        root = scene.createEntity(name);
    }

    // Instantiate each root node as a child of the model root
    for (int rootIndex : m_rootNodes)
    {
        if (rootIndex >= 0 && rootIndex < static_cast<int>(m_nodes.size()))
        {
            instantiateNode(scene, root, m_nodes[static_cast<size_t>(rootIndex)]);
        }
    }

    // Attach skeletal animation if this model has a skin and clips
    if (m_skeleton && !m_animationClips.empty())
    {
        auto* animator = root->addComponent<SkeletonAnimator>();
        animator->setSkeleton(m_skeleton);
        for (const auto& clip : m_animationClips)
        {
            animator->addClip(clip);
        }
        // Auto-play the first clip
        animator->play(m_animationClips[0]->getName());
        Logger::info("Attached SkeletonAnimator with "
            + std::to_string(m_animationClips.size()) + " clip(s) to '" + name + "'");
    }

    return root;
}

Entity* Model::instantiateNode(Scene& scene, Entity* parent,
                                const ModelNode& node) const
{
    // Create entity for this node
    std::string nodeName = node.name.empty() ? "Node" : node.name;
    auto entity = std::make_unique<Entity>(nodeName);
    Entity* entityPtr = entity.get();

    // Set transform via matrix override (preserves quaternion precision)
    entityPtr->transform.setLocalMatrix(node.computeLocalMatrix());

    // Attach mesh renderer(s) for this node's primitives
    if (node.primitiveIndices.size() == 1)
    {
        // Single primitive — attach directly to this entity
        int primIdx = node.primitiveIndices[0];
        if (primIdx >= 0 && primIdx < static_cast<int>(m_primitives.size()))
        {
            const auto& prim = m_primitives[static_cast<size_t>(primIdx)];
            std::shared_ptr<Material> mat = nullptr;
            if (prim.materialIndex >= 0
                && prim.materialIndex < static_cast<int>(m_materials.size()))
            {
                mat = m_materials[static_cast<size_t>(prim.materialIndex)];
            }
            auto* renderer = entityPtr->addComponent<MeshRenderer>(prim.mesh, mat);
            renderer->setBounds(prim.bounds);
        }
    }
    else if (node.primitiveIndices.size() > 1)
    {
        // Multiple primitives — create a child entity for each
        int subIndex = 0;
        for (int primIdx : node.primitiveIndices)
        {
            if (primIdx < 0 || primIdx >= static_cast<int>(m_primitives.size()))
            {
                continue;
            }
            const auto& prim = m_primitives[static_cast<size_t>(primIdx)];
            std::shared_ptr<Material> mat = nullptr;
            if (prim.materialIndex >= 0
                && prim.materialIndex < static_cast<int>(m_materials.size()))
            {
                mat = m_materials[static_cast<size_t>(prim.materialIndex)];
            }

            auto subEntity = std::make_unique<Entity>(
                nodeName + "_prim" + std::to_string(subIndex));
            Entity* subPtr = subEntity.get();
            auto* renderer = subPtr->addComponent<MeshRenderer>(prim.mesh, mat);
            renderer->setBounds(prim.bounds);
            entityPtr->addChild(std::move(subEntity));
            subIndex++;
        }
    }

    // Add this entity to parent — use returned pointer (safe after move)
    entityPtr = parent->addChild(std::move(entity));

    // Recurse for child nodes
    for (int childIdx : node.childIndices)
    {
        if (childIdx >= 0 && childIdx < static_cast<int>(m_nodes.size()))
        {
            instantiateNode(scene, entityPtr,
                            m_nodes[static_cast<size_t>(childIdx)]);
        }
    }

    return entityPtr;
}

const std::string& Model::getName() const
{
    return m_name;
}

size_t Model::getMeshCount() const
{
    return m_primitives.size();
}

size_t Model::getMaterialCount() const
{
    return m_materials.size();
}

size_t Model::getTextureCount() const
{
    return m_textures.size();
}

size_t Model::getNodeCount() const
{
    return m_nodes.size();
}

AABB Model::getBounds() const
{
    if (m_primitives.empty())
    {
        return AABB{};
    }

    glm::vec3 overallMin(std::numeric_limits<float>::max());
    glm::vec3 overallMax(std::numeric_limits<float>::lowest());

    for (const auto& prim : m_primitives)
    {
        overallMin = glm::min(overallMin, prim.bounds.min);
        overallMax = glm::max(overallMax, prim.bounds.max);
    }

    return {overallMin, overallMax};
}

} // namespace Vestige
