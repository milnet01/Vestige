/// @file model.h
/// @brief Model, ModelNode, and ModelPrimitive — represents a loaded glTF scene graph.
#pragma once

#include "renderer/mesh.h"
#include "renderer/material.h"
#include "renderer/texture.h"
#include "animation/skeleton.h"
#include "animation/animation_clip.h"
#include "animation/morph_target.h"
#include "utils/aabb.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

class Scene;
class Entity;

/// @brief A single renderable primitive within a model (one mesh + one material).
struct ModelPrimitive
{
    std::shared_ptr<Mesh> mesh;
    int materialIndex = -1;
    AABB bounds;
    MorphTargetData morphTargets;  ///< Morph target data (empty for non-morphed meshes)
};

/// @brief A node in the model's scene graph hierarchy.
struct ModelNode
{
    std::string name;

    // TRS decomposition
    glm::vec3 translation = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // w,x,y,z identity
    glm::vec3 scale = glm::vec3(1.0f);

    // Optional direct matrix (overrides TRS if set)
    bool hasMatrix = false;
    glm::mat4 matrix = glm::mat4(1.0f);

    // Indices into Model::m_primitives
    std::vector<int> primitiveIndices;

    // Indices into Model::m_nodes (children of this node)
    std::vector<int> childIndices;

    /// @brief Computes the local transform matrix from TRS or direct matrix.
    glm::mat4 computeLocalMatrix() const;
};

/// @brief A loaded 3D model — owns shared GPU resources (meshes, materials, textures).
/// Can be instantiated into a scene multiple times without reloading GPU data.
class Model
{
public:
    Model() = default;
    ~Model() = default;

    /// @brief Instantiates the model into a scene, creating an entity hierarchy.
    /// @param scene The scene to add entities to.
    /// @param parent Parent entity (nullptr for scene root).
    /// @param name Name for the root entity.
    /// @return Pointer to the root entity of the instantiated model.
    Entity* instantiate(Scene& scene, Entity* parent, const std::string& name) const;

    /// @brief Gets the model name.
    const std::string& getName() const;

    /// @brief Gets the number of mesh primitives.
    size_t getMeshCount() const;

    /// @brief Gets the number of materials.
    size_t getMaterialCount() const;

    /// @brief Gets the number of textures.
    size_t getTextureCount() const;

    /// @brief Gets the number of nodes.
    size_t getNodeCount() const;

    /// @brief Gets the overall bounding box of the model.
    AABB getBounds() const;

    // Data populated by the loader
    std::string m_name;
    std::vector<ModelPrimitive> m_primitives;
    std::vector<std::shared_ptr<Material>> m_materials;
    std::vector<std::shared_ptr<Texture>> m_textures;
    std::vector<ModelNode> m_nodes;
    std::vector<int> m_rootNodes;

    // Skeletal animation data (empty/null for static models)
    std::shared_ptr<Skeleton> m_skeleton;
    std::vector<std::shared_ptr<AnimationClip>> m_animationClips;

private:
    /// @brief Recursively instantiates a node and its children.
    Entity* instantiateNode(Scene& scene, Entity* parent,
                            const ModelNode& node) const;
};

} // namespace Vestige
