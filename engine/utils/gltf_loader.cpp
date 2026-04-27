// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file gltf_loader.cpp
/// @brief glTF 2.0 loading implementation with native PBR material support.
#include "utils/gltf_loader.h"
#include "animation/skeleton.h"
#include "animation/animation_clip.h"
#include "animation/morph_target.h"
#include "core/logger.h"
#include "utils/path_sandbox.h"

// Must match defines in gltf_loader_impl.cpp to avoid stb_image conflicts
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>
#include <stb_image.h>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <limits>
#include <set>
#include <unordered_map>

namespace Vestige
{

/// @brief Resolves a relative glTF URI against the gltf-file directory.
///
/// Forwards to `Vestige::PathSandbox::resolveUriIntoBase` (single source
/// of truth per Phase 10.9 Slice 5 D1). The local wrapper retains
/// glTF-specific Logger::warning messages on rejection.
static std::string resolveUri(const std::string& gltfDir, const std::string& uri)
{
    if (uri.empty())
        return {};

    auto resolved = Vestige::PathSandbox::resolveUriIntoBase(
        std::filesystem::path(gltfDir), uri);
    if (resolved.empty())
    {
        Logger::warning("glTF: URI escapes asset directory or cannot be resolved: " + uri);
    }
    return resolved;
}

/// @brief Reads a vec3 from a byte buffer using memcpy (avoids strict aliasing violation).
static glm::vec3 readVec3(const unsigned char* ptr)
{
    float v[3];
    std::memcpy(v, ptr, sizeof(v));
    return glm::vec3(v[0], v[1], v[2]);
}

/// @brief Reads a vec2 from a byte buffer using memcpy.
static glm::vec2 readVec2(const unsigned char* ptr)
{
    float v[2];
    std::memcpy(v, ptr, sizeof(v));
    return glm::vec2(v[0], v[1]);
}

/// @brief Reads a vec4 from a byte buffer using memcpy.
static glm::vec4 readVec4(const unsigned char* ptr)
{
    float v[4];
    std::memcpy(v, ptr, sizeof(v));
    return glm::vec4(v[0], v[1], v[2], v[3]);
}

/// @brief Reads a typed value from a byte buffer using memcpy.
template <typename T>
static T readValue(const unsigned char* ptr)
{
    T value;
    std::memcpy(&value, ptr, sizeof(T));
    return value;
}

/// @brief Validates accessor bounds and returns the base data pointer if safe, nullptr otherwise.
/// Checks that accessor, bufferView, and buffer indices are in range, and that the data
/// region (offset + stride * count) fits within the buffer.
static const unsigned char* validateAccessorData(
    const tinygltf::Model& model,
    int accessorIndex,
    size_t elementSize,
    const char* attributeName)
{
    if (accessorIndex < 0
        || static_cast<size_t>(accessorIndex) >= model.accessors.size())
    {
        Logger::warning("glTF: accessor index out of range for "
            + std::string(attributeName));
        return nullptr;
    }

    const auto& accessor = model.accessors[static_cast<size_t>(accessorIndex)];

    if (accessor.bufferView < 0
        || static_cast<size_t>(accessor.bufferView) >= model.bufferViews.size())
    {
        Logger::warning("glTF: bufferView index out of range for "
            + std::string(attributeName));
        return nullptr;
    }

    const auto& bufferView = model.bufferViews[static_cast<size_t>(accessor.bufferView)];

    if (bufferView.buffer < 0
        || static_cast<size_t>(bufferView.buffer) >= model.buffers.size())
    {
        Logger::warning("glTF: buffer index out of range for "
            + std::string(attributeName));
        return nullptr;
    }

    const auto& buffer = model.buffers[static_cast<size_t>(bufferView.buffer)];

    // Validate offsets don't exceed buffer
    size_t totalOffset = bufferView.byteOffset + accessor.byteOffset;
    if (totalOffset > buffer.data.size())
    {
        Logger::warning("glTF: data offset exceeds buffer for "
            + std::string(attributeName));
        return nullptr;
    }

    // Validate stride
    size_t stride = bufferView.byteStride > 0
        ? bufferView.byteStride : elementSize;
    if (stride < elementSize)
    {
        Logger::warning("glTF: stride smaller than element for "
            + std::string(attributeName));
        return nullptr;
    }

    // Validate that all elements fit within buffer
    if (accessor.count > 0)
    {
        size_t requiredSize = totalOffset
            + (accessor.count - 1) * stride + elementSize;
        if (requiredSize > buffer.data.size())
        {
            Logger::warning("glTF: accessor data extends beyond buffer for "
                + std::string(attributeName));
            return nullptr;
        }
    }

    return buffer.data.data() + totalOffset;
}

/// @brief Pre-scans materials to determine which image indices are used as sRGB (color) textures.
/// Images used as baseColor or emissive textures are sRGB; all others (normal, metallic-roughness, AO) are linear.
static std::set<int> determineSrgbImages(const tinygltf::Model& gltfModel)
{
    std::set<int> srgbImageIndices;
    for (const auto& mat : gltfModel.materials)
    {
        // Base color texture → sRGB
        int bcTexIdx = mat.pbrMetallicRoughness.baseColorTexture.index;
        if (bcTexIdx >= 0 && bcTexIdx < static_cast<int>(gltfModel.textures.size()))
        {
            int imgIdx = gltfModel.textures[static_cast<size_t>(bcTexIdx)].source;
            if (imgIdx >= 0)
            {
                srgbImageIndices.insert(imgIdx);
            }
        }

        // Emissive texture → sRGB
        int emTexIdx = mat.emissiveTexture.index;
        if (emTexIdx >= 0 && emTexIdx < static_cast<int>(gltfModel.textures.size()))
        {
            int imgIdx = gltfModel.textures[static_cast<size_t>(emTexIdx)].source;
            if (imgIdx >= 0)
            {
                srgbImageIndices.insert(imgIdx);
            }
        }
    }
    return srgbImageIndices;
}

/// @brief Loads all textures from a glTF model with correct sRGB/linear format.
static void loadTextures(const tinygltf::Model& gltfModel,
                          const std::string& gltfDir,
                          ResourceManager& resourceManager,
                          Model& outModel,
                          const std::set<int>& srgbImageIndices)
{
    for (size_t imgIdx = 0; imgIdx < gltfModel.images.size(); imgIdx++)
    {
        const auto& image = gltfModel.images[imgIdx];
        bool linear = (srgbImageIndices.find(static_cast<int>(imgIdx)) == srgbImageIndices.end());

        if (!image.uri.empty() && image.bufferView < 0)
        {
            // External image file — load via ResourceManager (cached).
            // Phase 10.9 Slice 5 D5: if resolveUri rejected the path
            // (returns empty), substitute the default texture explicitly
            // instead of passing "" through to loadTexture (which would
            // also default but log a redundant "Failed to load texture: "
            // warning with no path information).
            std::string fullPath = resolveUri(gltfDir, image.uri);
            if (fullPath.empty())
            {
                outModel.m_textures.push_back(resourceManager.getDefaultTexture());
            }
            else
            {
                auto texture = resourceManager.loadTexture(fullPath, linear);
                outModel.m_textures.push_back(texture);
            }
        }
        else if (image.bufferView >= 0)
        {
            // Embedded in GLB buffer — decode from memory
            if (static_cast<size_t>(image.bufferView) >= gltfModel.bufferViews.size())
            {
                Logger::warning("glTF: invalid bufferView for embedded image: " + image.name);
                outModel.m_textures.push_back(resourceManager.getDefaultTexture());
                continue;
            }
            const auto& bufferView = gltfModel.bufferViews[static_cast<size_t>(image.bufferView)];
            if (bufferView.buffer < 0
                || static_cast<size_t>(bufferView.buffer) >= gltfModel.buffers.size())
            {
                Logger::warning("glTF: invalid buffer for embedded image: " + image.name);
                outModel.m_textures.push_back(resourceManager.getDefaultTexture());
                continue;
            }
            const auto& buffer = gltfModel.buffers[static_cast<size_t>(bufferView.buffer)];
            if (bufferView.byteOffset + bufferView.byteLength > buffer.data.size())
            {
                Logger::warning("glTF: buffer data out of range for embedded image: " + image.name);
                outModel.m_textures.push_back(resourceManager.getDefaultTexture());
                continue;
            }
            const unsigned char* data = buffer.data.data() + bufferView.byteOffset;
            size_t dataSize = bufferView.byteLength;

            auto texture = std::make_shared<Texture>();
            if (texture->loadFromMemory(data, dataSize, linear))
            {
                outModel.m_textures.push_back(texture);
            }
            else
            {
                Logger::warning("Failed to decode embedded glTF texture: " + image.name);
                outModel.m_textures.push_back(resourceManager.getDefaultTexture());
            }
        }
        else if (!image.image.empty())
        {
            // Pre-decoded raw pixel data (tinygltf already decoded it)
            auto texture = std::make_shared<Texture>();
            if (texture->loadFromMemory(image.image.data(),
                                         image.width, image.height, image.component, linear))
            {
                outModel.m_textures.push_back(texture);
            }
            else
            {
                Logger::warning("Failed to load raw glTF texture: " + image.name);
                outModel.m_textures.push_back(resourceManager.getDefaultTexture());
            }
        }
        else
        {
            Logger::warning("glTF image has no data: " + image.name);
            outModel.m_textures.push_back(resourceManager.getDefaultTexture());
        }
    }
}

/// @brief Gets the texture shared_ptr from the model's texture list, given a glTF texture index.
static std::shared_ptr<Texture> getTextureByIndex(const tinygltf::Model& gltfModel,
                                                    const Model& outModel,
                                                    int textureIndex)
{
    if (textureIndex < 0 || textureIndex >= static_cast<int>(gltfModel.textures.size()))
    {
        return nullptr;
    }
    int imageIndex = gltfModel.textures[static_cast<size_t>(textureIndex)].source;
    if (imageIndex < 0 || imageIndex >= static_cast<int>(outModel.m_textures.size()))
    {
        return nullptr;
    }
    return outModel.m_textures[static_cast<size_t>(imageIndex)];
}

/// @brief Loads all materials from a glTF model as native PBR materials.
static void loadMaterials(const tinygltf::Model& gltfModel, Model& outModel)
{
    for (const auto& gltfMat : gltfModel.materials)
    {
        auto material = std::make_shared<Material>();
        material->name = gltfMat.name;
        material->setType(MaterialType::PBR);

        const auto& pbr = gltfMat.pbrMetallicRoughness;

        // Base color → PBR albedo
        glm::vec3 baseColor(
            static_cast<float>(pbr.baseColorFactor[0]),
            static_cast<float>(pbr.baseColorFactor[1]),
            static_cast<float>(pbr.baseColorFactor[2]));
        material->setAlbedo(baseColor);

        // Also set diffuse color for backward compat (used by shared texture path)
        material->setDiffuseColor(baseColor);

        // Base color texture → diffuse/albedo texture (shared slot, unit 0)
        auto diffuseTex = getTextureByIndex(gltfModel, outModel, pbr.baseColorTexture.index);
        if (diffuseTex)
        {
            material->setDiffuseTexture(diffuseTex);
        }

        // Normal map
        auto normalTex = getTextureByIndex(gltfModel, outModel, gltfMat.normalTexture.index);
        if (normalTex)
        {
            material->setNormalMap(normalTex);
        }

        // Metallic and roughness factors
        material->setMetallic(static_cast<float>(pbr.metallicFactor));
        material->setRoughness(static_cast<float>(pbr.roughnessFactor));

        // Metallic-roughness texture (glTF packing: G=roughness, B=metallic)
        auto mrTex = getTextureByIndex(gltfModel, outModel,
            pbr.metallicRoughnessTexture.index);
        if (mrTex)
        {
            material->setMetallicRoughnessTexture(mrTex);
        }

        // Occlusion texture
        auto aoTex = getTextureByIndex(gltfModel, outModel, gltfMat.occlusionTexture.index);
        if (aoTex)
        {
            material->setAoTexture(aoTex);
        }

        // Emissive factor and texture
        glm::vec3 emissive(
            static_cast<float>(gltfMat.emissiveFactor[0]),
            static_cast<float>(gltfMat.emissiveFactor[1]),
            static_cast<float>(gltfMat.emissiveFactor[2]));
        material->setEmissive(emissive);

        auto emissiveTex = getTextureByIndex(gltfModel, outModel, gltfMat.emissiveTexture.index);
        if (emissiveTex)
        {
            material->setEmissiveTexture(emissiveTex);
        }

        // Alpha mode
        if (gltfMat.alphaMode == "MASK")
        {
            material->setAlphaMode(AlphaMode::MASK);
        }
        else if (gltfMat.alphaMode == "BLEND")
        {
            material->setAlphaMode(AlphaMode::BLEND);
        }
        // else OPAQUE (default)

        // Alpha cutoff (glTF default is 0.5, same as ours)
        material->setAlphaCutoff(static_cast<float>(gltfMat.alphaCutoff));

        // Double-sided
        material->setDoubleSided(gltfMat.doubleSided);

        // Base color alpha from the 4th component of baseColorFactor
        material->setBaseColorAlpha(static_cast<float>(pbr.baseColorFactor[3]));

        // POM disabled for glTF materials (no height maps in standard glTF)
        material->setPomEnabled(false);

        outModel.m_materials.push_back(material);
    }
}

/// @brief Generates flat normals from triangle faces.
static void generateFlatNormals(std::vector<Vertex>& vertices,
                                 const std::vector<uint32_t>& indices)
{
    // Zero out existing normals
    for (auto& v : vertices)
    {
        v.normal = glm::vec3(0.0f);
    }

    // Accumulate face normals
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        // H8: Bounds check — skip triangles with out-of-range indices
        if (indices[i] >= vertices.size()
            || indices[i + 1] >= vertices.size()
            || indices[i + 2] >= vertices.size())
        {
            continue;
        }

        const glm::vec3& p0 = vertices[indices[i]].position;
        const glm::vec3& p1 = vertices[indices[i + 1]].position;
        const glm::vec3& p2 = vertices[indices[i + 2]].position;

        glm::vec3 edge1 = p1 - p0;
        glm::vec3 edge2 = p2 - p0;
        glm::vec3 faceNormal = glm::cross(edge1, edge2);

        float len = glm::length(faceNormal);
        if (len > 1e-8f)
        {
            faceNormal /= len;
        }

        vertices[indices[i]].normal += faceNormal;
        vertices[indices[i + 1]].normal += faceNormal;
        vertices[indices[i + 2]].normal += faceNormal;
    }

    // Normalize
    for (auto& v : vertices)
    {
        float len = glm::length(v.normal);
        if (len > 1e-8f)
        {
            v.normal /= len;
        }
        else
        {
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }
}

/// @brief Per-glTF-mesh primitive range in `outModel.m_primitives`.
///
/// Slice 5 D10: returned from `loadMeshes` so `buildNodeHierarchy` can
/// map mesh→primitive offsets without rerunning a separate pre-scan.
/// The pre-scan was a drift hazard — its skip predicate (`mode != TRIANGLES
/// && mode != -1` plus POSITION presence) had to stay in lockstep with
/// every `continue` inside the real loader, which it didn't.
struct MeshPrimRange
{
    int startIdx;
    int count;
};

/// @brief Loads all mesh primitives from a glTF model.
/// @return Per-glTF-mesh `{startIdx, count}` ranges into `outModel.m_primitives`.
static std::vector<MeshPrimRange> loadMeshes(const tinygltf::Model& gltfModel,
                                             Model& outModel)
{
    std::vector<MeshPrimRange> ranges;
    ranges.reserve(gltfModel.meshes.size());

    for (const auto& gltfMesh : gltfModel.meshes)
    {
        const int rangeStart = static_cast<int>(outModel.m_primitives.size());

        for (const auto& primitive : gltfMesh.primitives)
        {
            // Only support triangles
            if (primitive.mode != TINYGLTF_MODE_TRIANGLES && primitive.mode != -1)
            {
                Logger::warning("glTF: skipping non-triangle primitive (mode "
                    + std::to_string(primitive.mode) + ") in mesh '" + gltfMesh.name + "'");
                continue;
            }

            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;

            // --- Read POSITION ---
            // Every path that doesn't populate `vertices` continue's; no separate
            // `hasPositions` flag needed post-block.
            AABB bounds;
            {
                auto it = primitive.attributes.find("POSITION");
                if (it == primitive.attributes.end())
                {
                    Logger::warning("glTF: primitive has no POSITION attribute, skipping");
                    continue;
                }

                const unsigned char* base = validateAccessorData(
                    gltfModel, it->second, sizeof(float) * 3, "POSITION");
                if (!base)
                {
                    continue;
                }

                const auto& accessor = gltfModel.accessors[static_cast<size_t>(it->second)];
                const auto& bufferView = gltfModel.bufferViews[static_cast<size_t>(accessor.bufferView)];
                size_t stride = bufferView.byteStride > 0
                    ? bufferView.byteStride : sizeof(float) * 3;

                vertices.resize(accessor.count);
                glm::vec3 minPos(std::numeric_limits<float>::max());
                glm::vec3 maxPos(std::numeric_limits<float>::lowest());

                for (size_t i = 0; i < accessor.count; i++)
                {
                    vertices[i].position = readVec3(base + stride * i);
                    vertices[i].color = glm::vec3(1.0f);  // Default white
                    minPos = glm::min(minPos, vertices[i].position);
                    maxPos = glm::max(maxPos, vertices[i].position);
                }

                bounds = {minPos, maxPos};
            }

            // --- Read NORMAL ---
            bool hasNormals = false;
            {
                auto it = primitive.attributes.find("NORMAL");
                if (it != primitive.attributes.end())
                {
                    const unsigned char* base = validateAccessorData(
                        gltfModel, it->second, sizeof(float) * 3, "NORMAL");
                    if (base)
                    {
                        const auto& accessor = gltfModel.accessors[static_cast<size_t>(it->second)];
                        const auto& bufferView = gltfModel.bufferViews[static_cast<size_t>(accessor.bufferView)];
                        size_t stride = bufferView.byteStride > 0
                            ? bufferView.byteStride : sizeof(float) * 3;

                        for (size_t i = 0; i < std::min(accessor.count, vertices.size()); i++)
                        {
                            vertices[i].normal = readVec3(base + stride * i);
                        }
                        hasNormals = true;
                    }
                }
            }

            // --- Read TEXCOORD_0 ---
            {
                auto it = primitive.attributes.find("TEXCOORD_0");
                if (it != primitive.attributes.end())
                {
                    const unsigned char* base = validateAccessorData(
                        gltfModel, it->second, sizeof(float) * 2, "TEXCOORD_0");
                    if (base)
                    {
                        const auto& accessor = gltfModel.accessors[static_cast<size_t>(it->second)];
                        const auto& bufferView = gltfModel.bufferViews[static_cast<size_t>(accessor.bufferView)];
                        size_t stride = bufferView.byteStride > 0
                            ? bufferView.byteStride : sizeof(float) * 2;

                        for (size_t i = 0; i < std::min(accessor.count, vertices.size()); i++)
                        {
                            vertices[i].texCoord = readVec2(base + stride * i);
                        }
                    }
                }
            }

            // --- Read TANGENT ---
            bool hasTangents = false;
            {
                auto it = primitive.attributes.find("TANGENT");
                if (it != primitive.attributes.end())
                {
                    // glTF tangent is vec4 (w = handedness)
                    const unsigned char* base = validateAccessorData(
                        gltfModel, it->second, sizeof(float) * 4, "TANGENT");
                    if (base)
                    {
                        const auto& accessor = gltfModel.accessors[static_cast<size_t>(it->second)];
                        const auto& bufferView = gltfModel.bufferViews[static_cast<size_t>(accessor.bufferView)];
                        size_t stride = bufferView.byteStride > 0
                            ? bufferView.byteStride : sizeof(float) * 4;

                        for (size_t i = 0; i < std::min(accessor.count, vertices.size()); i++)
                        {
                            glm::vec4 t = readVec4(base + stride * i);
                            vertices[i].tangent = glm::vec3(t.x, t.y, t.z);
                            // bitangent = cross(N, T.xyz) * T.w
                            vertices[i].bitangent = glm::cross(vertices[i].normal,
                                vertices[i].tangent) * t.w;
                        }
                        hasTangents = true;
                    }
                }
            }

            // --- Read COLOR_0 ---
            {
                auto it = primitive.attributes.find("COLOR_0");
                if (it != primitive.attributes.end())
                {
                    size_t elemSize = sizeof(float) * 3;  // Default for float vec3
                    if (static_cast<size_t>(it->second) < gltfModel.accessors.size())
                    {
                        const auto& acc = gltfModel.accessors[static_cast<size_t>(it->second)];
                        if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                        {
                            elemSize = acc.type == TINYGLTF_TYPE_VEC4 ? 4 : 3;
                        }
                        else
                        {
                            elemSize = sizeof(float) * (acc.type == TINYGLTF_TYPE_VEC4 ? 4 : 3);
                        }
                    }

                    const unsigned char* base = validateAccessorData(
                        gltfModel, it->second, elemSize, "COLOR_0");
                    if (base)
                    {
                        const auto& accessor = gltfModel.accessors[static_cast<size_t>(it->second)];
                        const auto& bufferView = gltfModel.bufferViews[static_cast<size_t>(accessor.bufferView)];

                        if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
                        {
                            size_t stride = bufferView.byteStride > 0
                                ? bufferView.byteStride : elemSize;

                            for (size_t i = 0; i < std::min(accessor.count, vertices.size()); i++)
                            {
                                vertices[i].color = readVec3(base + stride * i);
                            }
                        }
                        else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                        {
                            size_t stride = bufferView.byteStride > 0
                                ? bufferView.byteStride : elemSize;

                            for (size_t i = 0; i < std::min(accessor.count, vertices.size()); i++)
                            {
                                const unsigned char* c = base + stride * i;
                                vertices[i].color = glm::vec3(
                                    c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f);
                            }
                        }
                    }
                }
            }

            // --- Read JOINTS_0 (bone indices per vertex) ---
            {
                auto it = primitive.attributes.find("JOINTS_0");
                if (it != primitive.attributes.end())
                {
                    const auto& accessor = gltfModel.accessors[static_cast<size_t>(it->second)];

                    // Determine element size based on component type
                    size_t elemSize = 4;  // UNSIGNED_BYTE vec4
                    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                    {
                        elemSize = sizeof(uint16_t) * 4;
                    }

                    const unsigned char* base = validateAccessorData(
                        gltfModel, it->second, elemSize, "JOINTS_0");
                    if (base)
                    {
                        const auto& bufferView = gltfModel.bufferViews[static_cast<size_t>(accessor.bufferView)];
                        size_t stride = bufferView.byteStride > 0
                            ? bufferView.byteStride : elemSize;

                        for (size_t i = 0; i < std::min(accessor.count, vertices.size()); i++)
                        {
                            const unsigned char* ptr = base + stride * i;
                            if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                            {
                                vertices[i].boneIds = glm::ivec4(
                                    ptr[0], ptr[1], ptr[2], ptr[3]);
                            }
                            else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                            {
                                uint16_t joints[4];
                                std::memcpy(joints, ptr, sizeof(joints));
                                vertices[i].boneIds = glm::ivec4(
                                    joints[0], joints[1], joints[2], joints[3]);
                            }
                        }
                    }
                }
            }

            // --- Read WEIGHTS_0 (bone weights per vertex) ---
            {
                auto it = primitive.attributes.find("WEIGHTS_0");
                if (it != primitive.attributes.end())
                {
                    const auto& accessor = gltfModel.accessors[static_cast<size_t>(it->second)];

                    size_t elemSize = sizeof(float) * 4;  // Default float vec4
                    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                    {
                        elemSize = 4;
                    }
                    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                    {
                        elemSize = sizeof(uint16_t) * 4;
                    }

                    const unsigned char* base = validateAccessorData(
                        gltfModel, it->second, elemSize, "WEIGHTS_0");
                    if (base)
                    {
                        const auto& bufferView = gltfModel.bufferViews[static_cast<size_t>(accessor.bufferView)];
                        size_t stride = bufferView.byteStride > 0
                            ? bufferView.byteStride : elemSize;

                        for (size_t i = 0; i < std::min(accessor.count, vertices.size()); i++)
                        {
                            const unsigned char* ptr = base + stride * i;
                            glm::vec4 w(0.0f);
                            if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
                            {
                                std::memcpy(&w, ptr, sizeof(float) * 4);
                            }
                            else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                            {
                                w = glm::vec4(ptr[0], ptr[1], ptr[2], ptr[3]) / 255.0f;
                            }
                            else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                            {
                                uint16_t raw[4];
                                std::memcpy(raw, ptr, sizeof(raw));
                                w = glm::vec4(raw[0], raw[1], raw[2], raw[3]) / 65535.0f;
                            }

                            // Normalize weights so they sum to 1.0
                            float sum = w.x + w.y + w.z + w.w;
                            if (sum > 0.0f)
                            {
                                w /= sum;
                            }
                            vertices[i].boneWeights = w;
                        }
                    }
                }
            }

            // --- Read morph targets (primitive.targets) ---
            MorphTargetData morphData;
            if (!primitive.targets.empty())
            {
                morphData.vertexCount = vertices.size();

                for (size_t ti = 0; ti < primitive.targets.size(); ++ti)
                {
                    const auto& targetAttrs = primitive.targets[ti];
                    MorphTarget mt;

                    // Target name from mesh extras or gltfMesh.extras
                    if (ti < gltfMesh.extras.Size())
                    {
                        // Some exporters store target names in mesh extras
                    }
                    mt.name = "target_" + std::to_string(ti);

                    // Position deltas
                    auto posIt = targetAttrs.find("POSITION");
                    if (posIt != targetAttrs.end() && posIt->second >= 0)
                    {
                        const unsigned char* data = validateAccessorData(
                            gltfModel, posIt->second, sizeof(float) * 3, "morph POSITION");
                        if (data)
                        {
                            const auto& acc = gltfModel.accessors[static_cast<size_t>(posIt->second)];
                            const auto& bv = gltfModel.bufferViews[static_cast<size_t>(acc.bufferView)];
                            size_t stride = bv.byteStride > 0 ? bv.byteStride : sizeof(float) * 3;
                            // Copy via memcpy rather than reinterpret_cast —
                            // glTF byteStride is not guaranteed to keep the
                            // float triple 4-byte aligned, and reinterpret
                            // of unsigned char* to float* is a strict-aliasing
                            // violation regardless of alignment. Mirrors the
                            // nav_mesh_builder fix from engine 0.1.4 (commit
                            // 1f6fd24); completes that sweep.
                            mt.positionDeltas.resize(acc.count);
                            for (size_t i = 0; i < acc.count; ++i)
                            {
                                float fp[3];
                                std::memcpy(fp, data + stride * i, sizeof(fp));
                                mt.positionDeltas[i] = glm::vec3(fp[0], fp[1], fp[2]);
                            }
                        }
                    }

                    // Normal deltas
                    auto norIt = targetAttrs.find("NORMAL");
                    if (norIt != targetAttrs.end() && norIt->second >= 0)
                    {
                        const unsigned char* data = validateAccessorData(
                            gltfModel, norIt->second, sizeof(float) * 3, "morph NORMAL");
                        if (data)
                        {
                            const auto& acc = gltfModel.accessors[static_cast<size_t>(norIt->second)];
                            const auto& bv = gltfModel.bufferViews[static_cast<size_t>(acc.bufferView)];
                            size_t stride = bv.byteStride > 0 ? bv.byteStride : sizeof(float) * 3;
                            mt.normalDeltas.resize(acc.count);
                            for (size_t i = 0; i < acc.count; ++i)
                            {
                                float fp[3];
                                std::memcpy(fp, data + stride * i, sizeof(fp));
                                mt.normalDeltas[i] = glm::vec3(fp[0], fp[1], fp[2]);
                            }
                        }
                    }

                    // Tangent deltas
                    auto tanIt = targetAttrs.find("TANGENT");
                    if (tanIt != targetAttrs.end() && tanIt->second >= 0)
                    {
                        const unsigned char* data = validateAccessorData(
                            gltfModel, tanIt->second, sizeof(float) * 3, "morph TANGENT");
                        if (data)
                        {
                            const auto& acc = gltfModel.accessors[static_cast<size_t>(tanIt->second)];
                            const auto& bv = gltfModel.bufferViews[static_cast<size_t>(acc.bufferView)];
                            size_t stride = bv.byteStride > 0 ? bv.byteStride : sizeof(float) * 3;
                            mt.tangentDeltas.resize(acc.count);
                            for (size_t i = 0; i < acc.count; ++i)
                            {
                                float fp[3];
                                std::memcpy(fp, data + stride * i, sizeof(fp));
                                mt.tangentDeltas[i] = glm::vec3(fp[0], fp[1], fp[2]);
                            }
                        }
                    }

                    morphData.targets.push_back(std::move(mt));
                }

                // Default weights from mesh
                for (double w : gltfMesh.weights)
                {
                    morphData.defaultWeights.push_back(static_cast<float>(w));
                }
                // Pad with zeros if fewer weights than targets
                morphData.defaultWeights.resize(morphData.targets.size(), 0.0f);

                Logger::info("glTF: loaded " + std::to_string(morphData.targets.size())
                    + " morph target(s) for mesh '" + gltfMesh.name + "'");
            }

            // --- Read indices ---
            if (primitive.indices >= 0)
            {
                // Determine element size for the index component type
                size_t indexElemSize = sizeof(uint32_t);
                if (static_cast<size_t>(primitive.indices) < gltfModel.accessors.size())
                {
                    int ct = gltfModel.accessors[static_cast<size_t>(primitive.indices)].componentType;
                    if (ct == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                        indexElemSize = sizeof(uint16_t);
                    else if (ct == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                        indexElemSize = 1;
                }

                const unsigned char* base = validateAccessorData(
                    gltfModel, primitive.indices, indexElemSize, "INDICES");
                if (base)
                {
                    const auto& accessor = gltfModel.accessors[static_cast<size_t>(primitive.indices)];
                    const auto& bufferView = gltfModel.bufferViews[static_cast<size_t>(accessor.bufferView)];

                    indices.resize(accessor.count);

                    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                    {
                        size_t stride = bufferView.byteStride > 0
                            ? bufferView.byteStride : sizeof(uint32_t);
                        for (size_t i = 0; i < accessor.count; i++)
                        {
                            indices[i] = readValue<uint32_t>(base + stride * i);
                        }
                    }
                    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                    {
                        size_t stride = bufferView.byteStride > 0
                            ? bufferView.byteStride : sizeof(uint16_t);
                        for (size_t i = 0; i < accessor.count; i++)
                        {
                            indices[i] = static_cast<uint32_t>(
                                readValue<uint16_t>(base + stride * i));
                        }
                    }
                    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                    {
                        size_t stride = bufferView.byteStride > 0
                            ? bufferView.byteStride : 1;
                        for (size_t i = 0; i < accessor.count; i++)
                        {
                            indices[i] = static_cast<uint32_t>(*(base + stride * i));
                        }
                    }
                }
            }
            else
            {
                // No indices — generate sequential indices
                indices.resize(vertices.size());
                for (size_t i = 0; i < vertices.size(); i++)
                {
                    indices[i] = static_cast<uint32_t>(i);
                }
            }

            // Generate flat normals if missing
            if (!hasNormals)
            {
                generateFlatNormals(vertices, indices);
            }

            // Compute tangents if missing
            if (!hasTangents && !indices.empty())
            {
                calculateTangents(vertices, indices);
            }

            // Upload to GPU
            auto mesh = std::make_shared<Mesh>();
            mesh->upload(vertices, indices);

            // Upload morph target deltas to GPU SSBO (if present)
            if (!morphData.empty())
            {
                mesh->uploadMorphTargets(morphData);
            }

            ModelPrimitive modelPrim;
            modelPrim.mesh = mesh;
            modelPrim.materialIndex = primitive.material;
            modelPrim.bounds = bounds;
            modelPrim.morphTargets = std::move(morphData);
            outModel.m_primitives.push_back(std::move(modelPrim));
        }

        const int rangeCount = static_cast<int>(outModel.m_primitives.size())
                             - rangeStart;
        ranges.push_back({rangeStart, rangeCount});
    }

    return ranges;
}

/// @brief Builds the node hierarchy from glTF nodes.
///
/// `meshRanges` is `loadMeshes`'s authoritative `{startIdx, count}` per
/// `gltfModel.meshes[i]` — Slice 5 D10 removed the previous local pre-scan
/// that could drift from the actual loaded set.
static void buildNodeHierarchy(const tinygltf::Model& gltfModel,
                                const std::vector<MeshPrimRange>& meshRanges,
                                Model& outModel)
{
    // Build ModelNode for each glTF node
    outModel.m_nodes.resize(gltfModel.nodes.size());

    for (size_t i = 0; i < gltfModel.nodes.size(); i++)
    {
        const auto& gltfNode = gltfModel.nodes[i];
        ModelNode& node = outModel.m_nodes[i];

        node.name = gltfNode.name;

        // Transform — check for direct matrix first
        if (gltfNode.matrix.size() == 16)
        {
            node.hasMatrix = true;
            // glTF matrix is column-major, same as GLM
            for (int col = 0; col < 4; col++)
            {
                for (int row = 0; row < 4; row++)
                {
                    node.matrix[col][row] = static_cast<float>(
                        gltfNode.matrix[static_cast<size_t>(col * 4 + row)]);
                }
            }
        }
        else
        {
            node.hasMatrix = false;

            if (gltfNode.translation.size() == 3)
            {
                node.translation = glm::vec3(
                    static_cast<float>(gltfNode.translation[0]),
                    static_cast<float>(gltfNode.translation[1]),
                    static_cast<float>(gltfNode.translation[2]));
            }

            if (gltfNode.rotation.size() == 4)
            {
                // glTF quaternion: [x, y, z, w]
                // GLM quat constructor: (w, x, y, z)
                node.rotation = glm::quat(
                    static_cast<float>(gltfNode.rotation[3]),  // w
                    static_cast<float>(gltfNode.rotation[0]),  // x
                    static_cast<float>(gltfNode.rotation[1]),  // y
                    static_cast<float>(gltfNode.rotation[2])); // z
            }

            if (gltfNode.scale.size() == 3)
            {
                node.scale = glm::vec3(
                    static_cast<float>(gltfNode.scale[0]),
                    static_cast<float>(gltfNode.scale[1]),
                    static_cast<float>(gltfNode.scale[2]));
            }
        }

        // Map mesh → primitives
        if (gltfNode.mesh >= 0
            && static_cast<size_t>(gltfNode.mesh) < meshRanges.size())
        {
            const MeshPrimRange& r = meshRanges[static_cast<size_t>(gltfNode.mesh)];
            for (int p = r.startIdx; p < r.startIdx + r.count; p++)
            {
                node.primitiveIndices.push_back(p);
            }
        }

        // Children — D10 bounds-check: skip and warn on out-of-range indices
        // rather than storing them and letting downstream traversal blow up.
        for (int childIdx : gltfNode.children)
        {
            if (childIdx >= 0
                && childIdx < static_cast<int>(gltfModel.nodes.size()))
            {
                node.childIndices.push_back(childIdx);
            }
            else
            {
                Logger::warning("glTF: out-of-range child index "
                    + std::to_string(childIdx) + " in node "
                    + std::to_string(i) + " (skipping)");
            }
        }
    }

    // Root nodes from the default scene
    if (!gltfModel.scenes.empty())
    {
        int sceneIdx = gltfModel.defaultScene >= 0 ? gltfModel.defaultScene : 0;
        if (static_cast<size_t>(sceneIdx) >= gltfModel.scenes.size())
        {
            sceneIdx = 0;
        }
        const auto& scene = gltfModel.scenes[static_cast<size_t>(sceneIdx)];
        // D10 bounds-check: skip and warn on out-of-range root-node indices.
        for (int nodeIdx : scene.nodes)
        {
            if (nodeIdx >= 0
                && nodeIdx < static_cast<int>(gltfModel.nodes.size()))
            {
                outModel.m_rootNodes.push_back(nodeIdx);
            }
            else
            {
                Logger::warning("glTF: out-of-range root-node index "
                    + std::to_string(nodeIdx) + " in scene "
                    + std::to_string(sceneIdx) + " (skipping)");
            }
        }
    }
    else
    {
        // No scenes defined — treat all top-level nodes as roots
        // (find nodes that aren't children of any other node)
        std::vector<bool> isChild(gltfModel.nodes.size(), false);
        for (const auto& node : gltfModel.nodes)
        {
            for (int child : node.children)
            {
                if (child >= 0 && child < static_cast<int>(gltfModel.nodes.size()))
                {
                    isChild[static_cast<size_t>(child)] = true;
                }
            }
        }
        for (size_t i = 0; i < gltfModel.nodes.size(); i++)
        {
            if (!isChild[i])
            {
                outModel.m_rootNodes.push_back(static_cast<int>(i));
            }
        }
    }
}

/// @brief Computes the local transform matrix for a glTF node (TRS or direct matrix).
static glm::mat4 computeGltfNodeMatrix(const tinygltf::Node& node)
{
    if (node.matrix.size() == 16)
    {
        glm::mat4 m;
        for (int col = 0; col < 4; col++)
        {
            for (int row = 0; row < 4; row++)
            {
                m[col][row] = static_cast<float>(
                    node.matrix[static_cast<size_t>(col * 4 + row)]);
            }
        }
        return m;
    }

    glm::vec3 t(0.0f);
    glm::quat r(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 s(1.0f);

    if (node.translation.size() == 3)
    {
        t = glm::vec3(static_cast<float>(node.translation[0]),
                      static_cast<float>(node.translation[1]),
                      static_cast<float>(node.translation[2]));
    }
    if (node.rotation.size() == 4)
    {
        r = glm::quat(static_cast<float>(node.rotation[3]),   // w
                       static_cast<float>(node.rotation[0]),   // x
                       static_cast<float>(node.rotation[1]),   // y
                       static_cast<float>(node.rotation[2]));  // z
    }
    if (node.scale.size() == 3)
    {
        s = glm::vec3(static_cast<float>(node.scale[0]),
                      static_cast<float>(node.scale[1]),
                      static_cast<float>(node.scale[2]));
    }

    return glm::translate(glm::mat4(1.0f), t)
         * glm::mat4_cast(r)
         * glm::scale(glm::mat4(1.0f), s);
}

/// @brief Loads skeletal data from the first glTF skin.
static void loadSkin(const tinygltf::Model& gltfModel, Model& outModel)
{
    if (gltfModel.skins.empty())
    {
        return;
    }

    const auto& skin = gltfModel.skins[0];
    auto skeleton = std::make_shared<Skeleton>();

    int jointCount = static_cast<int>(skin.joints.size());
    if (jointCount > Skeleton::MAX_JOINTS)
    {
        Logger::warning("glTF: skin has " + std::to_string(jointCount)
            + " joints, clamping to " + std::to_string(Skeleton::MAX_JOINTS));
        jointCount = Skeleton::MAX_JOINTS;
    }

    skeleton->m_joints.resize(static_cast<size_t>(jointCount));

    // Read inverse bind matrices
    std::vector<glm::mat4> inverseBindMatrices(static_cast<size_t>(jointCount), glm::mat4(1.0f));
    if (skin.inverseBindMatrices >= 0)
    {
        const unsigned char* base = validateAccessorData(
            gltfModel, skin.inverseBindMatrices, sizeof(float) * 16, "inverseBindMatrices");
        if (base)
        {
            const auto& accessor = gltfModel.accessors[static_cast<size_t>(skin.inverseBindMatrices)];
            const auto& bufferView = gltfModel.bufferViews[static_cast<size_t>(accessor.bufferView)];
            size_t stride = bufferView.byteStride > 0
                ? bufferView.byteStride : sizeof(float) * 16;

            for (size_t i = 0; i < std::min(accessor.count, static_cast<size_t>(jointCount)); i++)
            {
                std::memcpy(&inverseBindMatrices[i], base + stride * i, sizeof(glm::mat4));
            }
        }
    }

    // Build a lookup: glTF node index → joint index in our skeleton
    std::unordered_map<int, int> nodeToJoint;
    for (int i = 0; i < jointCount; i++)
    {
        nodeToJoint[skin.joints[static_cast<size_t>(i)]] = i;
    }

    // Populate joints
    for (int i = 0; i < jointCount; i++)
    {
        int gltfNodeIndex = skin.joints[static_cast<size_t>(i)];
        const auto& gltfNode = gltfModel.nodes[static_cast<size_t>(gltfNodeIndex)];

        Joint& joint = skeleton->m_joints[static_cast<size_t>(i)];
        joint.name = gltfNode.name;
        joint.inverseBindMatrix = inverseBindMatrices[static_cast<size_t>(i)];
        joint.localBindTransform = computeGltfNodeMatrix(gltfNode);

        // Find parent: search glTF node hierarchy for a parent that is also a joint
        joint.parentIndex = -1;
        for (size_t n = 0; n < gltfModel.nodes.size(); n++)
        {
            const auto& potentialParent = gltfModel.nodes[n];
            for (int child : potentialParent.children)
            {
                if (child == gltfNodeIndex)
                {
                    auto parentIt = nodeToJoint.find(static_cast<int>(n));
                    if (parentIt != nodeToJoint.end())
                    {
                        joint.parentIndex = parentIt->second;
                    }
                    break;
                }
            }
            if (joint.parentIndex >= 0) break;
        }

        if (joint.parentIndex < 0)
        {
            skeleton->m_rootJoints.push_back(i);
        }
    }

    outModel.m_skeleton = skeleton;

    Logger::info("glTF skin loaded: " + std::to_string(jointCount) + " joints");
}

/// @brief Reads float data from an accessor into a flat vector.
static std::vector<float> readFloatAccessor(const tinygltf::Model& gltfModel,
                                             int accessorIndex,
                                             int componentsPerElement,
                                             const char* label)
{
    std::vector<float> result;

    const unsigned char* base = validateAccessorData(
        gltfModel, accessorIndex,
        sizeof(float) * static_cast<size_t>(componentsPerElement), label);
    if (!base)
    {
        return result;
    }

    const auto& accessor = gltfModel.accessors[static_cast<size_t>(accessorIndex)];
    const auto& bufferView = gltfModel.bufferViews[static_cast<size_t>(accessor.bufferView)];
    size_t stride = bufferView.byteStride > 0
        ? bufferView.byteStride : sizeof(float) * static_cast<size_t>(componentsPerElement);

    // Guard against attacker-controlled accessor.count * componentsPerElement
    // overflowing size_t — silent overflow would size result to a tiny vector
    // and the memcpy below would walk off the end. (AUDIT H2.)
    const size_t cpe = static_cast<size_t>(componentsPerElement);
    if (componentsPerElement <= 0 || accessor.count > SIZE_MAX / cpe)
    {
        Logger::warning("glTF: accessor.count * componentsPerElement overflows size_t for "
            + std::string(label));
        return result;
    }
    result.resize(accessor.count * cpe);

    for (size_t i = 0; i < accessor.count; i++)
    {
        std::memcpy(&result[i * static_cast<size_t>(componentsPerElement)],
                    base + stride * i,
                    sizeof(float) * static_cast<size_t>(componentsPerElement));
    }

    return result;
}

/// @brief Loads animation clips from glTF.
static void loadAnimations(const tinygltf::Model& gltfModel, Model& outModel)
{
    if (gltfModel.animations.empty() || !outModel.m_skeleton)
    {
        return;
    }

    // Build node → joint lookup from the first skin
    const auto& skin = gltfModel.skins[0];
    std::unordered_map<int, int> nodeToJoint;
    for (int i = 0; i < static_cast<int>(skin.joints.size()); i++)
    {
        if (i < Skeleton::MAX_JOINTS)
        {
            nodeToJoint[skin.joints[static_cast<size_t>(i)]] = i;
        }
    }

    for (size_t animIdx = 0; animIdx < gltfModel.animations.size(); animIdx++)
    {
        const auto& gltfAnim = gltfModel.animations[animIdx];

        auto clip = std::make_shared<AnimationClip>();
        clip->m_name = gltfAnim.name.empty()
            ? "Animation_" + std::to_string(animIdx) : gltfAnim.name;

        for (const auto& channel : gltfAnim.channels)
        {
            // Map glTF target node to skeleton joint index.
            // WEIGHTS channels target the mesh node, not necessarily a joint.
            auto it = nodeToJoint.find(channel.target_node);
            bool isWeightsChannel = (channel.target_path == "weights");
            if (it == nodeToJoint.end() && !isWeightsChannel)
            {
                continue;  // Not a skeleton joint and not WEIGHTS — skip
            }

            // H9: Bounds check on animation sampler index
            if (channel.sampler < 0
                || static_cast<size_t>(channel.sampler) >= gltfAnim.samplers.size())
            {
                Logger::warning("glTF: animation '" + gltfAnim.name
                    + "' has out-of-range sampler index " + std::to_string(channel.sampler));
                continue;
            }
            const auto& sampler = gltfAnim.samplers[static_cast<size_t>(channel.sampler)];

            AnimationChannel animChannel;
            animChannel.jointIndex = (it != nodeToJoint.end()) ? it->second : -1;

            // Target path
            if (channel.target_path == "translation")
            {
                animChannel.targetPath = AnimTargetPath::TRANSLATION;
            }
            else if (channel.target_path == "rotation")
            {
                animChannel.targetPath = AnimTargetPath::ROTATION;
            }
            else if (channel.target_path == "scale")
            {
                animChannel.targetPath = AnimTargetPath::SCALE;
            }
            else if (channel.target_path == "weights")
            {
                animChannel.targetPath = AnimTargetPath::WEIGHTS;
            }
            else
            {
                continue;  // Unknown target path
            }

            // Interpolation
            if (sampler.interpolation == "STEP")
            {
                animChannel.interpolation = AnimInterpolation::STEP;
            }
            else if (sampler.interpolation == "CUBICSPLINE")
            {
                animChannel.interpolation = AnimInterpolation::CUBICSPLINE;
            }
            else
            {
                animChannel.interpolation = AnimInterpolation::LINEAR;
            }

            // Read timestamps (input accessor — always float scalars)
            animChannel.timestamps = readFloatAccessor(
                gltfModel, sampler.input, 1, "animation timestamps");

            // Read values (output accessor)
            int components = 3;  // translation, scale
            if (channel.target_path == "rotation")
            {
                components = 4;  // quaternion xyzw
            }
            else if (channel.target_path == "weights")
            {
                // For morph targets, components = number of morph targets
                // The accessor count = timestamps * morphTargetCount
                // We read all floats and let the consumer interpret them
                const auto& outAcc = gltfModel.accessors[static_cast<size_t>(sampler.output)];
                if (!animChannel.timestamps.empty())
                {
                    components = static_cast<int>(outAcc.count / animChannel.timestamps.size());
                    if (components < 1) components = 1;
                }
            }
            animChannel.values = readFloatAccessor(
                gltfModel, sampler.output, components, "animation values");

            if (!animChannel.timestamps.empty() && !animChannel.values.empty())
            {
                clip->m_channels.push_back(std::move(animChannel));
            }
        }

        if (!clip->m_channels.empty())
        {
            clip->computeDuration();
            outModel.m_animationClips.push_back(clip);

            Logger::info("glTF animation loaded: '" + clip->getName()
                + "' (" + std::to_string(clip->m_channels.size()) + " channels, "
                + std::to_string(clip->getDuration()) + "s)");
        }
    }
}

std::unique_ptr<Model> GltfLoader::load(const std::string& filePath,
                                         ResourceManager& resourceManager)
{
    // H7: File size limit (256 MB) — reject before handing to tinygltf
    constexpr std::uintmax_t MAX_GLTF_SIZE = 256ULL * 1024ULL * 1024ULL;
    {
        std::error_code ec;
        auto fileSize = std::filesystem::file_size(filePath, ec);
        if (ec)
        {
            Logger::error("glTF: cannot stat file: " + filePath
                + " — " + ec.message());
            return nullptr;
        }
        if (fileSize > MAX_GLTF_SIZE)
        {
            Logger::error("glTF: file exceeds 256 MB limit ("
                + std::to_string(fileSize / (1024 * 1024)) + " MB): " + filePath);
            return nullptr;
        }
    }

    tinygltf::Model gltfModel;
    tinygltf::TinyGLTF loader;

    // Provide stb_image callback for embedded/external images (we disabled tinygltf's
    // built-in stb_image to avoid duplicate symbol conflicts with our stb_image_impl.cpp).
    loader.SetImageLoader(
        [](tinygltf::Image* image, const int /*imageIdx*/, std::string* err,
           std::string* /*warn*/, int /*reqWidth*/, int /*reqHeight*/,
           const unsigned char* bytes, int size, void* /*userData*/) -> bool
        {
            int w = 0, h = 0, comp = 0;
            unsigned char* data = stbi_load_from_memory(
                bytes, size, &w, &h, &comp, 4);  // Force RGBA
            if (!data)
            {
                if (err)
                {
                    *err = "stb_image: failed to decode image";
                }
                return false;
            }
            image->width = w;
            image->height = h;
            image->component = 4;
            image->bits = 8;
            image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
            image->image.assign(data, data + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
            stbi_image_free(data);
            return true;
        },
        nullptr);

    // Phase 10.9 Slice 5 D2: install sandboxed FsCallbacks so tinygltf
    // cannot read external buffers / images from outside the .gltf
    // file's parent directory. Closes the confused-deputy / TOCTOU
    // window where the default callbacks would open() bytes off disk
    // before our resolveUri saw the URI. Wraps tinygltf's default
    // implementations with a PathSandbox::validateInsideRoots check
    // against the gltfDir.
    std::filesystem::path gltfFilePath(filePath);
    std::filesystem::path gltfDirPath = gltfFilePath.parent_path();
    if (gltfDirPath.empty())
    {
        // Bare filename — resolve relative to CWD (matches tinygltf's
        // own default base-dir behaviour).
        std::error_code cwdEc;
        gltfDirPath = std::filesystem::current_path(cwdEc);
        if (cwdEc)
        {
            gltfDirPath = ".";
        }
    }

    const std::vector<std::filesystem::path> kFsRoots { gltfDirPath };
    tinygltf::FsCallbacks fsCallbacks {
        // FileExists — tinygltf calls this first when probing where an
        // external URI lives. Returning false on sandbox-escape causes
        // the search to fall through to "File not found" without ever
        // calling ReadWholeFile, so this is where we log the rejection.
        // (A genuinely missing file inside the sandbox returns false
        // here too, but quietly — that path is a normal lookup miss,
        // not an attack.)
        [kFsRoots](const std::string& abs, void*) -> bool
        {
            if (PathSandbox::validateInsideRoots(abs, kFsRoots).empty())
            {
                Logger::warning("glTF FsCallbacks: rejected read outside "
                    "gltfDir: " + abs);
                return false;
            }
            return tinygltf::FileExists(abs, nullptr);
        },
        // ExpandFilePath — pass through (tinygltf's default is a no-op
        // for security; we keep that and let our validate step handle
        // the rest).
        &tinygltf::ExpandFilePath,
        // ReadWholeFile
        [kFsRoots](std::vector<unsigned char>* out, std::string* readErr,
                   const std::string& path, void*) -> bool
        {
            if (PathSandbox::validateInsideRoots(path, kFsRoots).empty())
            {
                if (readErr)
                {
                    *readErr = "Vestige sandbox: path outside gltf directory: "
                        + path;
                }
                Logger::warning("glTF FsCallbacks: rejected read outside "
                    "gltfDir: " + path);
                return false;
            }
            return tinygltf::ReadWholeFile(out, readErr, path, nullptr);
        },
        // WriteWholeFile — not used during load, but SetFsCallbacks
        // requires every slot non-null. Pass through.
        &tinygltf::WriteWholeFile,
        // GetFileSizeInBytes
        [kFsRoots](size_t* sz, std::string* sizeErr,
                   const std::string& path, void*) -> bool
        {
            if (PathSandbox::validateInsideRoots(path, kFsRoots).empty())
            {
                if (sizeErr)
                {
                    *sizeErr = "Vestige sandbox: path outside gltf directory: "
                        + path;
                }
                return false;
            }
            return tinygltf::GetFileSizeInBytes(sz, sizeErr, path, nullptr);
        },
        nullptr,
    };
    {
        std::string fsErr;
        if (!loader.SetFsCallbacks(fsCallbacks, &fsErr))
        {
            Logger::error("glTF: SetFsCallbacks failed: " + fsErr);
            return nullptr;
        }
    }

    std::string err;
    std::string warn;

    // Determine file type
    std::string extension = filePath;
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

    bool success = false;
    if (extension.size() >= 4
        && extension.substr(extension.size() - 4) == ".glb")
    {
        success = loader.LoadBinaryFromFile(&gltfModel, &err, &warn, filePath);
    }
    else
    {
        success = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, filePath);
    }

    if (!warn.empty())
    {
        Logger::warning("glTF warning: " + warn);
    }

    if (!success)
    {
        Logger::error("Failed to load glTF file: " + filePath
            + (err.empty() ? "" : " — " + err));
        return nullptr;
    }

    // Phase 10.9 Slice 5 D12: enforce extensionsRequired allowlist per
    // glTF 2.0 §3.12. Files declaring required extensions our loader
    // doesn't implement must be REJECTED, not silently rendered with
    // missing geometry / materials. The current Vestige loader does not
    // explicitly handle any glTF extension (tinygltf transparently
    // handles a few on the parse side, but our material / mesh paths
    // ignore extension data entirely), so the allowlist is deliberately
    // empty — every required extension is unknown to us. Add entries
    // here as specific extension support lands (KHR_materials_unlit,
    // KHR_lights_punctual, etc.).
    static const std::set<std::string> kSupportedRequiredExtensions = {
        // (intentionally empty — see comment above)
    };
    if (!gltfModel.extensionsRequired.empty())
    {
        std::string unsupported;
        for (const auto& ext : gltfModel.extensionsRequired)
        {
            if (kSupportedRequiredExtensions.count(ext) == 0)
            {
                if (!unsupported.empty()) unsupported += ", ";
                unsupported += ext;
            }
        }
        if (!unsupported.empty())
        {
            Logger::error("glTF: unsupported required extensions: "
                + unsupported + " in " + filePath
                + " — refusing to load (per glTF 2.0 §3.12)");
            return nullptr;
        }
    }

    auto model = std::make_unique<Model>();

    // Extract filename for model name (gltfFilePath is already canonicalised
    // for the FsCallbacks sandbox; reuse it here).
    model->m_name = gltfFilePath.stem().string();

    // Use the same directory the FsCallbacks sandbox was rooted at — this
    // way `loadTextures`'s resolveUri check and tinygltf's read-time
    // sandbox agree on what counts as "inside" the asset directory.
    std::string gltfDir = gltfDirPath.string();

    // Pre-scan materials to determine sRGB vs linear images
    std::set<int> srgbImages = determineSrgbImages(gltfModel);

    // Load in dependency order: textures → materials → meshes → skins → animations → nodes
    loadTextures(gltfModel, gltfDir, resourceManager, *model, srgbImages);
    loadMaterials(gltfModel, *model);
    auto meshRanges = loadMeshes(gltfModel, *model);
    loadSkin(gltfModel, *model);
    loadAnimations(gltfModel, *model);
    buildNodeHierarchy(gltfModel, meshRanges, *model);

    Logger::info("glTF loaded: " + filePath + " ("
        + std::to_string(model->getMeshCount()) + " primitives, "
        + std::to_string(model->getMaterialCount()) + " materials, "
        + std::to_string(model->getTextureCount()) + " textures, "
        + std::to_string(model->getNodeCount()) + " nodes"
        + (model->m_skeleton ? ", " + std::to_string(model->m_skeleton->getJointCount()) + " joints" : "")
        + ", " + std::to_string(model->m_animationClips.size()) + " animations)");

    return model;
}

} // namespace Vestige
