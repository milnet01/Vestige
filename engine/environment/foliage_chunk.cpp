// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file foliage_chunk.cpp
/// @brief FoliageChunk implementation — spatial cell for environment instances.
#include "environment/foliage_chunk.h"

#include <algorithm>
#include <limits>

namespace Vestige
{

const std::vector<FoliageInstance> FoliageChunk::EMPTY_FOLIAGE;

FoliageChunk::FoliageChunk(int gridX, int gridZ)
    : m_gridX(gridX)
    , m_gridZ(gridZ)
{
}

// --- Foliage ---

void FoliageChunk::addFoliage(uint32_t typeId, const FoliageInstance& instance)
{
    m_foliage[typeId].push_back(instance);
}

int FoliageChunk::removeFoliageInRadius(uint32_t typeId, const glm::vec3& center, float radius)
{
    auto it = m_foliage.find(typeId);
    if (it == m_foliage.end())
    {
        return 0;
    }

    float radiusSq = radius * radius;
    auto& instances = it->second;
    int removed = 0;

    // Use erase-remove pattern with distance check
    auto newEnd = std::remove_if(instances.begin(), instances.end(),
        [&](const FoliageInstance& inst)
        {
            glm::vec3 diff = inst.position - center;
            if (glm::dot(diff, diff) <= radiusSq)
            {
                ++removed;
                return true;
            }
            return false;
        });

    instances.erase(newEnd, instances.end());

    // Clean up empty type entries
    if (instances.empty())
    {
        m_foliage.erase(it);
    }

    return removed;
}

const std::vector<FoliageInstance>& FoliageChunk::getFoliage(uint32_t typeId) const
{
    auto it = m_foliage.find(typeId);
    if (it != m_foliage.end())
    {
        return it->second;
    }
    return EMPTY_FOLIAGE;
}

std::vector<FoliageInstance>* FoliageChunk::getFoliageMutable(uint32_t typeId)
{
    auto it = m_foliage.find(typeId);
    if (it != m_foliage.end())
    {
        return &it->second;
    }
    return nullptr;
}

std::vector<uint32_t> FoliageChunk::getFoliageTypeIds() const
{
    std::vector<uint32_t> ids;
    ids.reserve(m_foliage.size());
    for (const auto& [typeId, instances] : m_foliage)
    {
        if (!instances.empty())
        {
            ids.push_back(typeId);
        }
    }
    return ids;
}

// --- Scatter ---

void FoliageChunk::addScatter(const ScatterInstance& instance)
{
    m_scatter.push_back(instance);
}

int FoliageChunk::removeScatterInRadius(const glm::vec3& center, float radius)
{
    float radiusSq = radius * radius;
    int removed = 0;

    auto newEnd = std::remove_if(m_scatter.begin(), m_scatter.end(),
        [&](const ScatterInstance& inst)
        {
            glm::vec3 diff = inst.position - center;
            if (glm::dot(diff, diff) <= radiusSq)
            {
                ++removed;
                return true;
            }
            return false;
        });

    m_scatter.erase(newEnd, m_scatter.end());
    return removed;
}

const std::vector<ScatterInstance>& FoliageChunk::getScatter() const
{
    return m_scatter;
}

// --- Trees ---

void FoliageChunk::addTree(const TreeInstance& instance)
{
    m_trees.push_back(instance);
}

int FoliageChunk::removeTreesInRadius(const glm::vec3& center, float radius)
{
    float radiusSq = radius * radius;
    int removed = 0;

    auto newEnd = std::remove_if(m_trees.begin(), m_trees.end(),
        [&](const TreeInstance& inst)
        {
            glm::vec3 diff = inst.position - center;
            if (glm::dot(diff, diff) <= radiusSq)
            {
                ++removed;
                return true;
            }
            return false;
        });

    m_trees.erase(newEnd, m_trees.end());
    return removed;
}

const std::vector<TreeInstance>& FoliageChunk::getTrees() const
{
    return m_trees;
}

// --- Spatial ---

AABB FoliageChunk::getBounds() const
{
    const float worldX = static_cast<float>(m_gridX) * CHUNK_SIZE;
    const float worldZ = static_cast<float>(m_gridZ) * CHUNK_SIZE;

    // Phase 10.9 E4: Y range derived from the chunk's own instance
    // positions (which were placed against the terrain at scatter time)
    // rather than the previous magic [-100, 200] ceiling. Empty chunks
    // get a small ±1 m default so the AABB is still valid — callers
    // (foliage_manager::getVisibleChunks, foliage_renderer) skip empty
    // chunks before invoking this anyway, so the fallback is purely
    // defensive.
    constexpr float kEmptyHalfRange = 1.0f;       // empty-chunk fallback
    constexpr float kCeilingMargin  = 50.0f;      // top-of-tree headroom
    constexpr float kFloorMargin    = 1.0f;       // pivot-below-ground margin

    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    auto extend = [&](float y) { if (y < minY) minY = y; if (y > maxY) maxY = y; };

    for (const auto& [typeId, instances] : m_foliage)
    {
        (void)typeId;
        for (const auto& inst : instances) { extend(inst.position.y); }
    }
    for (const auto& inst : m_scatter) { extend(inst.position.y); }
    for (const auto& inst : m_trees)   { extend(inst.position.y); }

    if (minY > maxY)
    {
        // No instances — emit a minimal valid AABB at y=0.
        minY = -kEmptyHalfRange;
        maxY =  kEmptyHalfRange;
    }
    else
    {
        minY -= kFloorMargin;
        maxY += kCeilingMargin;
    }

    return AABB{
        glm::vec3(worldX,             minY, worldZ),
        glm::vec3(worldX + CHUNK_SIZE, maxY, worldZ + CHUNK_SIZE)
    };
}

bool FoliageChunk::isEmpty() const
{
    if (!m_scatter.empty() || !m_trees.empty())
    {
        return false;
    }
    for (const auto& [typeId, instances] : m_foliage)
    {
        if (!instances.empty())
        {
            return false;
        }
    }
    return true;
}

int FoliageChunk::getTotalInstanceCount() const
{
    int count = static_cast<int>(m_scatter.size() + m_trees.size());
    for (const auto& [typeId, instances] : m_foliage)
    {
        count += static_cast<int>(instances.size());
    }
    return count;
}

// --- Serialization ---

nlohmann::json FoliageChunk::serialize() const
{
    nlohmann::json j;
    j["gridX"] = m_gridX;
    j["gridZ"] = m_gridZ;

    // Foliage
    nlohmann::json foliageJson = nlohmann::json::object();
    for (const auto& [typeId, instances] : m_foliage)
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& inst : instances)
        {
            arr.push_back({
                {"pos", {inst.position.x, inst.position.y, inst.position.z}},
                {"rot", inst.rotation},
                {"scale", inst.scale},
                {"tint", {inst.colorTint.x, inst.colorTint.y, inst.colorTint.z}}
            });
        }
        foliageJson[std::to_string(typeId)] = arr;
    }
    j["foliage"] = foliageJson;

    // Scatter
    nlohmann::json scatterArr = nlohmann::json::array();
    for (const auto& inst : m_scatter)
    {
        scatterArr.push_back({
            {"pos", {inst.position.x, inst.position.y, inst.position.z}},
            {"rot", {inst.rotation.w, inst.rotation.x, inst.rotation.y, inst.rotation.z}},
            {"scale", inst.scale},
            {"meshIdx", inst.meshIndex}
        });
    }
    j["scatter"] = scatterArr;

    // Trees
    nlohmann::json treeArr = nlohmann::json::array();
    for (const auto& inst : m_trees)
    {
        treeArr.push_back({
            {"pos", {inst.position.x, inst.position.y, inst.position.z}},
            {"rot", inst.rotation},
            {"scale", inst.scale},
            {"species", inst.speciesIndex}
        });
    }
    j["trees"] = treeArr;

    return j;
}

void FoliageChunk::deserialize(const nlohmann::json& j)
{
    // Clear existing data
    m_foliage.clear();
    m_scatter.clear();
    m_trees.clear();

    // Foliage
    if (j.contains("foliage") && j["foliage"].is_object())
    {
        for (auto& [key, arr] : j["foliage"].items())
        {
            uint32_t typeId = static_cast<uint32_t>(std::stoul(key));
            for (const auto& item : arr)
            {
                if (!item.contains("pos") || !item["pos"].is_array() || item["pos"].size() < 3)
                {
                    continue;
                }
                FoliageInstance inst;
                auto pos = item["pos"];
                inst.position = glm::vec3(pos[0].get<float>(), pos[1].get<float>(), pos[2].get<float>());
                inst.rotation = item.value("rot", 0.0f);
                inst.scale = item.value("scale", 1.0f);
                if (item.contains("tint") && item["tint"].is_array() && item["tint"].size() >= 3)
                {
                    auto tint = item["tint"];
                    inst.colorTint = glm::vec3(tint[0].get<float>(), tint[1].get<float>(), tint[2].get<float>());
                }
                m_foliage[typeId].push_back(inst);
            }
        }
    }

    // Scatter
    if (j.contains("scatter") && j["scatter"].is_array())
    {
        for (const auto& item : j["scatter"])
        {
            if (!item.contains("pos") || !item["pos"].is_array() || item["pos"].size() < 3)
            {
                continue;
            }
            ScatterInstance inst;
            auto pos = item["pos"];
            inst.position = glm::vec3(pos[0].get<float>(), pos[1].get<float>(), pos[2].get<float>());
            if (item.contains("rot") && item["rot"].is_array() && item["rot"].size() >= 4)
            {
                auto rot = item["rot"];
                inst.rotation = glm::quat(rot[0].get<float>(), rot[1].get<float>(),
                                           rot[2].get<float>(), rot[3].get<float>());
            }
            inst.scale = item.value("scale", 1.0f);
            inst.meshIndex = item.value("meshIdx", static_cast<uint32_t>(0));
            m_scatter.push_back(inst);
        }
    }

    // Trees
    if (j.contains("trees") && j["trees"].is_array())
    {
        for (const auto& item : j["trees"])
        {
            if (!item.contains("pos") || !item["pos"].is_array() || item["pos"].size() < 3)
            {
                continue;
            }
            TreeInstance inst;
            auto pos = item["pos"];
            inst.position = glm::vec3(pos[0].get<float>(), pos[1].get<float>(), pos[2].get<float>());
            inst.rotation = item.value("rot", 0.0f);
            inst.scale = item.value("scale", 1.0f);
            inst.speciesIndex = item.value("species", static_cast<uint32_t>(0));
            m_trees.push_back(inst);
        }
    }
}

} // namespace Vestige
