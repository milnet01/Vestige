/// @file gltf_loader.cpp
/// @brief glTF 2.0 loading implementation with native PBR material support.
#include "utils/gltf_loader.h"
#include "core/logger.h"

// Must match defines in gltf_loader_impl.cpp to avoid stb_image conflicts
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <limits>
#include <set>

namespace Vestige
{

/// @brief Resolves a relative URI against the directory of the glTF file.
/// Validates the resolved path stays within the base directory to prevent path traversal.
static std::string resolveUri(const std::string& gltfDir, const std::string& uri)
{
    if (uri.empty())
    {
        return {};
    }

    std::filesystem::path base(gltfDir);
    std::filesystem::path resolved = base / uri;

    // Canonicalize to collapse ".." and symlinks
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(resolved, ec);
    if (ec)
    {
        Logger::warning("glTF: cannot resolve URI: " + uri);
        return {};
    }

    auto canonicalBase = std::filesystem::weakly_canonical(base, ec);
    std::string canonStr = canonical.string();
    std::string baseStr = canonicalBase.string();
    if (canonStr.compare(0, baseStr.size(), baseStr) != 0)
    {
        Logger::warning("glTF: URI escapes asset directory: " + uri);
        return {};
    }

    return canonStr;
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
            // External image file — load via ResourceManager (cached)
            std::string fullPath = resolveUri(gltfDir, image.uri);
            auto texture = resourceManager.loadTexture(fullPath, linear);
            outModel.m_textures.push_back(texture);
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

/// @brief Loads all mesh primitives from a glTF model.
static void loadMeshes(const tinygltf::Model& gltfModel, Model& outModel)
{
    // Track the starting primitive index for each glTF mesh
    // (used later when building node hierarchy)
    for (const auto& gltfMesh : gltfModel.meshes)
    {
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
            bool hasPositions = false;
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
                hasPositions = true;
            }

            if (!hasPositions)
            {
                continue;
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

            ModelPrimitive modelPrim;
            modelPrim.mesh = mesh;
            modelPrim.materialIndex = primitive.material;
            modelPrim.bounds = bounds;
            outModel.m_primitives.push_back(modelPrim);
        }
    }
}

/// @brief Builds the node hierarchy from glTF nodes.
static void buildNodeHierarchy(const tinygltf::Model& gltfModel, Model& outModel)
{
    // First, compute the starting primitive index for each glTF mesh.
    // glTF meshes can have multiple primitives, and we've flattened them
    // into outModel.m_primitives. We need to map mesh index → primitive range.
    std::vector<int> meshPrimitiveStart(gltfModel.meshes.size(), 0);
    std::vector<int> meshPrimitiveCount(gltfModel.meshes.size(), 0);

    int primOffset = 0;
    for (size_t meshIdx = 0; meshIdx < gltfModel.meshes.size(); meshIdx++)
    {
        meshPrimitiveStart[meshIdx] = primOffset;
        // Count how many primitives from this mesh were actually loaded
        // (non-triangle primitives may have been skipped)
        int count = 0;
        for (const auto& prim : gltfModel.meshes[meshIdx].primitives)
        {
            if (prim.mode == TINYGLTF_MODE_TRIANGLES || prim.mode == -1)
            {
                // Check it has POSITION
                if (prim.attributes.find("POSITION") != prim.attributes.end())
                {
                    count++;
                }
            }
        }
        meshPrimitiveCount[meshIdx] = count;
        primOffset += count;
    }

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
            && gltfNode.mesh < static_cast<int>(gltfModel.meshes.size()))
        {
            int start = meshPrimitiveStart[static_cast<size_t>(gltfNode.mesh)];
            int count = meshPrimitiveCount[static_cast<size_t>(gltfNode.mesh)];
            for (int p = start; p < start + count; p++)
            {
                node.primitiveIndices.push_back(p);
            }
        }

        // Children
        for (int childIdx : gltfNode.children)
        {
            node.childIndices.push_back(childIdx);
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
        for (int nodeIdx : scene.nodes)
        {
            outModel.m_rootNodes.push_back(nodeIdx);
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

std::unique_ptr<Model> GltfLoader::load(const std::string& filePath,
                                         ResourceManager& resourceManager)
{
    tinygltf::Model gltfModel;
    tinygltf::TinyGLTF loader;
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

    auto model = std::make_unique<Model>();

    // Extract filename for model name
    std::filesystem::path path(filePath);
    model->m_name = path.stem().string();

    std::string gltfDir = path.parent_path().string();

    // Pre-scan materials to determine sRGB vs linear images
    std::set<int> srgbImages = determineSrgbImages(gltfModel);

    // Load in dependency order: textures → materials → meshes → nodes
    loadTextures(gltfModel, gltfDir, resourceManager, *model, srgbImages);
    loadMaterials(gltfModel, *model);
    loadMeshes(gltfModel, *model);
    buildNodeHierarchy(gltfModel, *model);

    Logger::info("glTF loaded: " + filePath + " ("
        + std::to_string(model->getMeshCount()) + " primitives, "
        + std::to_string(model->getMaterialCount()) + " materials, "
        + std::to_string(model->getTextureCount()) + " textures, "
        + std::to_string(model->getNodeCount()) + " nodes)");

    return model;
}

} // namespace Vestige
