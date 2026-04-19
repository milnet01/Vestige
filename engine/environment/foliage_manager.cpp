// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file foliage_manager.cpp
/// @brief FoliageManager implementation — chunk grid management, painting, and culling.
#include "environment/foliage_manager.h"
#include "core/logger.h"

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <random>

namespace Vestige
{

FoliageManager::FoliageManager()
{
}

std::vector<FoliageInstanceRef> FoliageManager::paintFoliage(
    uint32_t typeId,
    const glm::vec3& center,
    float radius,
    float density,
    float falloff,
    const FoliageTypeConfig& config,
    const DensityMap* densityMap)
{
    std::vector<FoliageInstanceRef> added;

    // Calculate number of instances to place based on area and density
    float area = glm::pi<float>() * radius * radius;
    int targetCount = static_cast<int>(area * density);
    if (targetCount <= 0)
    {
        return added;
    }

    // Use a seeded RNG for reproducible results within a stamp
    // (different seed each stamp via center position hash)
    std::mt19937 rng(static_cast<uint32_t>(
        std::hash<float>{}(center.x) ^
        (std::hash<float>{}(center.z) << 16)));

    std::uniform_real_distribution<float> angleDist(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> radiusDist(0.0f, 1.0f);
    std::uniform_real_distribution<float> rotDist(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> scaleDist(config.minScale, config.maxScale);
    std::uniform_real_distribution<float> tintDist(-1.0f, 1.0f);

    added.reserve(targetCount);

    for (int i = 0; i < targetCount; ++i)
    {
        // Random point within circle (uniform distribution via sqrt)
        float angle = angleDist(rng);
        float r = radius * std::sqrt(radiusDist(rng));
        float x = center.x + r * std::cos(angle);
        float z = center.z + r * std::sin(angle);

        // Apply falloff — reduce spawn probability near edges
        if (falloff > 0.0f)
        {
            float distRatio = r / radius;
            float falloffStart = 1.0f - falloff;
            if (distRatio > falloffStart)
            {
                float t = (distRatio - falloffStart) / falloff;
                float probability = 1.0f - t * t;  // Quadratic falloff
                if (radiusDist(rng) > probability)
                {
                    continue;
                }
            }
        }

        // Density map modulation — reject based on spatial density mask
        if (densityMap)
        {
            float mapDensity = densityMap->sample(x, z);
            if (radiusDist(rng) > mapDensity)
            {
                continue;
            }
        }

        FoliageInstance inst;
        inst.position = glm::vec3(x, center.y, z);
        inst.rotation = rotDist(rng);
        inst.scale = scaleDist(rng);
        inst.colorTint = glm::vec3(
            1.0f + tintDist(rng) * config.tintVariation.x,
            1.0f + tintDist(rng) * config.tintVariation.y,
            1.0f + tintDist(rng) * config.tintVariation.z);

        // Place into the appropriate chunk
        auto [gx, gz] = worldToGrid(inst.position);
        FoliageChunk& chunk = getOrCreateChunk(gx, gz);
        chunk.addFoliage(typeId, inst);

        FoliageInstanceRef ref;
        ref.chunkKey = packChunkKey(gx, gz);
        ref.typeId = typeId;
        ref.instance = inst;
        added.push_back(ref);
    }

    return added;
}

std::vector<FoliageInstanceRef> FoliageManager::eraseFoliage(
    uint32_t typeId,
    const glm::vec3& center,
    float radius)
{
    std::vector<FoliageInstanceRef> removed;

    // Find all chunks that could overlap the erase circle
    int minGx = static_cast<int>(std::floor((center.x - radius) / FoliageChunk::CHUNK_SIZE));
    int maxGx = static_cast<int>(std::floor((center.x + radius) / FoliageChunk::CHUNK_SIZE));
    int minGz = static_cast<int>(std::floor((center.z - radius) / FoliageChunk::CHUNK_SIZE));
    int maxGz = static_cast<int>(std::floor((center.z + radius) / FoliageChunk::CHUNK_SIZE));

    float radiusSq = radius * radius;

    for (int gx = minGx; gx <= maxGx; ++gx)
    {
        for (int gz = minGz; gz <= maxGz; ++gz)
        {
            uint64_t key = packChunkKey(gx, gz);
            auto it = m_chunks.find(key);
            if (it == m_chunks.end())
            {
                continue;
            }

            // Capture instances before removal for undo
            const auto& before = it->second->getFoliage(typeId);
            for (const auto& inst : before)
            {
                glm::vec3 diff = inst.position - center;
                if (glm::dot(diff, diff) <= radiusSq)
                {
                    FoliageInstanceRef ref;
                    ref.chunkKey = key;
                    ref.typeId = typeId;
                    ref.instance = inst;
                    removed.push_back(ref);
                }
            }

            it->second->removeFoliageInRadius(typeId, center, radius);
        }
    }

    return removed;
}

std::vector<FoliageInstanceRef> FoliageManager::eraseAllFoliage(
    const glm::vec3& center,
    float radius)
{
    std::vector<FoliageInstanceRef> removed;

    int minGx = static_cast<int>(std::floor((center.x - radius) / FoliageChunk::CHUNK_SIZE));
    int maxGx = static_cast<int>(std::floor((center.x + radius) / FoliageChunk::CHUNK_SIZE));
    int minGz = static_cast<int>(std::floor((center.z - radius) / FoliageChunk::CHUNK_SIZE));
    int maxGz = static_cast<int>(std::floor((center.z + radius) / FoliageChunk::CHUNK_SIZE));

    float radiusSq = radius * radius;

    for (int gx = minGx; gx <= maxGx; ++gx)
    {
        for (int gz = minGz; gz <= maxGz; ++gz)
        {
            uint64_t key = packChunkKey(gx, gz);
            auto it = m_chunks.find(key);
            if (it == m_chunks.end())
            {
                continue;
            }

            for (uint32_t typeId : it->second->getFoliageTypeIds())
            {
                const auto& before = it->second->getFoliage(typeId);
                for (const auto& inst : before)
                {
                    glm::vec3 diff = inst.position - center;
                    if (glm::dot(diff, diff) <= radiusSq)
                    {
                        FoliageInstanceRef ref;
                        ref.chunkKey = key;
                        ref.typeId = typeId;
                        ref.instance = inst;
                        removed.push_back(ref);
                    }
                }

                it->second->removeFoliageInRadius(typeId, center, radius);
            }
        }
    }

    return removed;
}

void FoliageManager::restoreFoliage(const std::vector<FoliageInstanceRef>& instances)
{
    for (const auto& ref : instances)
    {
        int gx, gz;
        unpackChunkKey(ref.chunkKey, gx, gz);
        FoliageChunk& chunk = getOrCreateChunk(gx, gz);
        chunk.addFoliage(ref.typeId, ref.instance);
    }
}

void FoliageManager::removeFoliage(const std::vector<FoliageInstanceRef>& instances)
{
    // Group removals by chunk for efficiency
    for (const auto& ref : instances)
    {
        auto it = m_chunks.find(ref.chunkKey);
        if (it == m_chunks.end())
        {
            continue;
        }

        // Remove the specific instance by matching position (effectively unique)
        auto* foliage = it->second->getFoliageMutable(ref.typeId);
        if (!foliage)
        {
            continue;
        }

        auto found = std::find_if(foliage->begin(), foliage->end(),
            [&](const FoliageInstance& inst)
            {
                return glm::distance(inst.position, ref.instance.position) < 0.001f;
            });

        if (found != foliage->end())
        {
            // Swap-and-pop for O(1) removal
            *found = foliage->back();
            foliage->pop_back();
        }
    }
}

// --- Scatter API ---

std::vector<std::pair<uint64_t, ScatterInstance>> FoliageManager::paintScatter(
    const ScatterTypeConfig& config,
    uint32_t meshIndex,
    const glm::vec3& center,
    float radius,
    float density,
    float falloff,
    const DensityMap* densityMap)
{
    std::vector<std::pair<uint64_t, ScatterInstance>> added;

    float area = glm::pi<float>() * radius * radius;
    int targetCount = static_cast<int>(area * density);
    if (targetCount <= 0)
    {
        return added;
    }

    std::mt19937 rng(static_cast<uint32_t>(
        std::hash<float>{}(center.x + 7.0f) ^
        (std::hash<float>{}(center.z + 13.0f) << 16)));

    std::uniform_real_distribution<float> angleDist(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> radiusDist(0.0f, 1.0f);
    std::uniform_real_distribution<float> scaleDist(config.minScale, config.maxScale);

    added.reserve(targetCount);

    for (int i = 0; i < targetCount; ++i)
    {
        float angle = angleDist(rng);
        float r = radius * std::sqrt(radiusDist(rng));
        float x = center.x + r * std::cos(angle);
        float z = center.z + r * std::sin(angle);

        // Falloff
        if (falloff > 0.0f)
        {
            float distRatio = r / radius;
            float falloffStart = 1.0f - falloff;
            if (distRatio > falloffStart)
            {
                float t = (distRatio - falloffStart) / falloff;
                if (radiusDist(rng) > (1.0f - t * t))
                {
                    continue;
                }
            }
        }

        // Density map modulation
        if (densityMap)
        {
            float mapDensity = densityMap->sample(x, z);
            if (radiusDist(rng) > mapDensity)
            {
                continue;
            }
        }

        ScatterInstance inst;
        inst.position = glm::vec3(x, center.y, z);
        inst.rotation = glm::quat(1, 0, 0, 0);  // Upright for now (surface alignment in later phase)
        inst.scale = scaleDist(rng);
        inst.meshIndex = meshIndex;

        auto [gx, gz] = worldToGrid(inst.position);
        FoliageChunk& chunk = getOrCreateChunk(gx, gz);
        chunk.addScatter(inst);

        uint64_t key = packChunkKey(gx, gz);
        added.emplace_back(key, inst);
    }

    return added;
}

std::vector<std::pair<uint64_t, ScatterInstance>> FoliageManager::eraseScatter(
    const glm::vec3& center, float radius)
{
    std::vector<std::pair<uint64_t, ScatterInstance>> removed;

    int minGx = static_cast<int>(std::floor((center.x - radius) / FoliageChunk::CHUNK_SIZE));
    int maxGx = static_cast<int>(std::floor((center.x + radius) / FoliageChunk::CHUNK_SIZE));
    int minGz = static_cast<int>(std::floor((center.z - radius) / FoliageChunk::CHUNK_SIZE));
    int maxGz = static_cast<int>(std::floor((center.z + radius) / FoliageChunk::CHUNK_SIZE));

    float radiusSq = radius * radius;

    for (int gx = minGx; gx <= maxGx; ++gx)
    {
        for (int gz = minGz; gz <= maxGz; ++gz)
        {
            uint64_t key = packChunkKey(gx, gz);
            auto it = m_chunks.find(key);
            if (it == m_chunks.end())
            {
                continue;
            }

            const auto& scatterBefore = it->second->getScatter();
            for (const auto& inst : scatterBefore)
            {
                glm::vec3 diff = inst.position - center;
                if (glm::dot(diff, diff) <= radiusSq)
                {
                    removed.emplace_back(key, inst);
                }
            }

            it->second->removeScatterInRadius(center, radius);
        }
    }

    return removed;
}

void FoliageManager::addScatterDirect(int gridX, int gridZ, const ScatterInstance& instance)
{
    FoliageChunk& chunk = getOrCreateChunk(gridX, gridZ);
    chunk.addScatter(instance);
}

void FoliageManager::removeScatterAt(uint64_t chunkKey, const glm::vec3& position)
{
    auto it = m_chunks.find(chunkKey);
    if (it == m_chunks.end())
    {
        return;
    }
    // Remove with a tiny radius to match position precisely
    it->second->removeScatterInRadius(position, 0.01f);
}

// --- Tree API ---

std::vector<std::pair<uint64_t, TreeInstance>> FoliageManager::placeTree(
    const TreeInstance& instance, float minSpacing)
{
    std::vector<std::pair<uint64_t, TreeInstance>> result;

    // Check minimum spacing against existing trees in nearby chunks
    float minSpacingSq = minSpacing * minSpacing;
    int minGx = static_cast<int>(std::floor((instance.position.x - minSpacing) / FoliageChunk::CHUNK_SIZE));
    int maxGx = static_cast<int>(std::floor((instance.position.x + minSpacing) / FoliageChunk::CHUNK_SIZE));
    int minGz = static_cast<int>(std::floor((instance.position.z - minSpacing) / FoliageChunk::CHUNK_SIZE));
    int maxGz = static_cast<int>(std::floor((instance.position.z + minSpacing) / FoliageChunk::CHUNK_SIZE));

    for (int gx = minGx; gx <= maxGx; ++gx)
    {
        for (int gz = minGz; gz <= maxGz; ++gz)
        {
            uint64_t key = packChunkKey(gx, gz);
            auto it = m_chunks.find(key);
            if (it == m_chunks.end()) continue;

            for (const auto& existing : it->second->getTrees())
            {
                glm::vec3 diff = existing.position - instance.position;
                if (glm::dot(diff, diff) < minSpacingSq)
                {
                    return result;  // Too close — reject placement
                }
            }
        }
    }

    auto [gx, gz] = worldToGrid(instance.position);
    FoliageChunk& chunk = getOrCreateChunk(gx, gz);
    chunk.addTree(instance);

    uint64_t key = packChunkKey(gx, gz);
    result.emplace_back(key, instance);
    return result;
}

std::vector<std::pair<uint64_t, TreeInstance>> FoliageManager::eraseTrees(
    const glm::vec3& center, float radius)
{
    std::vector<std::pair<uint64_t, TreeInstance>> removed;
    float radiusSq = radius * radius;

    int minGx = static_cast<int>(std::floor((center.x - radius) / FoliageChunk::CHUNK_SIZE));
    int maxGx = static_cast<int>(std::floor((center.x + radius) / FoliageChunk::CHUNK_SIZE));
    int minGz = static_cast<int>(std::floor((center.z - radius) / FoliageChunk::CHUNK_SIZE));
    int maxGz = static_cast<int>(std::floor((center.z + radius) / FoliageChunk::CHUNK_SIZE));

    for (int gx = minGx; gx <= maxGx; ++gx)
    {
        for (int gz = minGz; gz <= maxGz; ++gz)
        {
            uint64_t key = packChunkKey(gx, gz);
            auto it = m_chunks.find(key);
            if (it == m_chunks.end()) continue;

            for (const auto& tree : it->second->getTrees())
            {
                glm::vec3 diff = tree.position - center;
                if (glm::dot(diff, diff) <= radiusSq)
                {
                    removed.emplace_back(key, tree);
                }
            }
            it->second->removeTreesInRadius(center, radius);
        }
    }

    return removed;
}

void FoliageManager::addTreeDirect(int gridX, int gridZ, const TreeInstance& instance)
{
    FoliageChunk& chunk = getOrCreateChunk(gridX, gridZ);
    chunk.addTree(instance);
}

void FoliageManager::removeTreeAt(uint64_t chunkKey, const glm::vec3& position)
{
    auto it = m_chunks.find(chunkKey);
    if (it == m_chunks.end()) return;
    it->second->removeTreesInRadius(position, 0.01f);
}

// --- Path clearing ---

int FoliageManager::clearAlongPath(const SplinePath& path, float margin)
{
    if (path.getWaypointCount() < 2)
    {
        return 0;
    }

    int totalRemoved = 0;
    float totalLength = path.getLength();
    if (totalLength < 0.001f)
    {
        return 0;
    }

    float clearRadius = path.width + margin;

    // Sample the spline at intervals smaller than the clear radius
    // to ensure complete coverage
    float stepSize = clearRadius * 0.5f;
    int sampleCount = std::max(2, static_cast<int>(totalLength / stepSize) + 1);

    for (int i = 0; i < sampleCount; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(sampleCount - 1);
        glm::vec3 pos = path.evaluate(t);

        auto removedFoliage = eraseAllFoliage(pos, clearRadius);
        totalRemoved += static_cast<int>(removedFoliage.size());

        auto removedScatter = eraseScatter(pos, clearRadius);
        totalRemoved += static_cast<int>(removedScatter.size());

        auto removedTrees = eraseTrees(pos, clearRadius);
        totalRemoved += static_cast<int>(removedTrees.size());
    }

    if (totalRemoved > 0)
    {
        Logger::info("Cleared " + std::to_string(totalRemoved)
                     + " instances along path '" + path.name + "'");
    }

    return totalRemoved;
}

void FoliageManager::getVisibleChunks(
    const glm::mat4& viewProjection,
    std::vector<const FoliageChunk*>& out) const
{
    FrustumPlanes planes = extractFrustumPlanes(viewProjection);
    out.clear();
    out.reserve(m_chunks.size());

    for (const auto& [key, chunk] : m_chunks)
    {
        if (chunk->isEmpty())
        {
            continue;
        }

        if (isAabbInFrustum(chunk->getBounds(), planes))
        {
            out.push_back(chunk.get());
        }
    }
}

std::vector<const FoliageChunk*> FoliageManager::getVisibleChunks(
    const glm::mat4& viewProjection) const
{
    std::vector<const FoliageChunk*> visible;
    getVisibleChunks(viewProjection, visible);
    return visible;
}

void FoliageManager::getAllChunks(std::vector<const FoliageChunk*>& out) const
{
    out.clear();
    out.reserve(m_chunks.size());
    for (const auto& [key, chunk] : m_chunks)
    {
        if (!chunk->isEmpty())
        {
            out.push_back(chunk.get());
        }
    }
}

std::vector<const FoliageChunk*> FoliageManager::getAllChunks() const
{
    std::vector<const FoliageChunk*> result;
    getAllChunks(result);
    return result;
}

const FoliageChunk* FoliageManager::getChunk(int gridX, int gridZ) const
{
    auto it = m_chunks.find(packChunkKey(gridX, gridZ));
    if (it != m_chunks.end())
    {
        return it->second.get();
    }
    return nullptr;
}

int FoliageManager::getTotalFoliageCount() const
{
    int total = 0;
    for (const auto& [key, chunk] : m_chunks)
    {
        total += chunk->getTotalInstanceCount();
    }
    return total;
}

int FoliageManager::getChunkCount() const
{
    return static_cast<int>(m_chunks.size());
}

nlohmann::json FoliageManager::serialize() const
{
    nlohmann::json j;
    nlohmann::json chunksArr = nlohmann::json::array();

    for (const auto& [key, chunk] : m_chunks)
    {
        if (!chunk->isEmpty())
        {
            chunksArr.push_back(chunk->serialize());
        }
    }

    j["chunks"] = chunksArr;
    return j;
}

void FoliageManager::deserialize(const nlohmann::json& j)
{
    clear();

    if (!j.contains("chunks") || !j["chunks"].is_array())
    {
        return;
    }

    for (const auto& chunkJson : j["chunks"])
    {
        if (!chunkJson.contains("gridX") || !chunkJson.contains("gridZ"))
        {
            Logger::warning("Skipping foliage chunk with missing gridX/gridZ");
            continue;
        }
        int gx = chunkJson.value("gridX", 0);
        int gz = chunkJson.value("gridZ", 0);
        FoliageChunk& chunk = getOrCreateChunk(gx, gz);
        chunk.deserialize(chunkJson);
    }

    Logger::info("Loaded environment: " + std::to_string(m_chunks.size()) + " chunks, "
                 + std::to_string(getTotalFoliageCount()) + " total instances");
}

void FoliageManager::clear()
{
    m_chunks.clear();
}

uint64_t FoliageManager::packChunkKey(int gridX, int gridZ)
{
    // Pack two int32_t into a uint64_t
    uint32_t ux = static_cast<uint32_t>(gridX);
    uint32_t uz = static_cast<uint32_t>(gridZ);
    return (static_cast<uint64_t>(ux) << 32) | static_cast<uint64_t>(uz);
}

void FoliageManager::unpackChunkKey(uint64_t key, int& gridX, int& gridZ)
{
    gridX = static_cast<int>(static_cast<uint32_t>(key >> 32));
    gridZ = static_cast<int>(static_cast<uint32_t>(key & 0xFFFFFFFF));
}

FoliageChunk& FoliageManager::getOrCreateChunk(int gridX, int gridZ)
{
    uint64_t key = packChunkKey(gridX, gridZ);
    auto it = m_chunks.find(key);
    if (it != m_chunks.end())
    {
        return *it->second;
    }

    auto chunk = std::make_unique<FoliageChunk>(gridX, gridZ);
    auto& ref = *chunk;
    m_chunks[key] = std::move(chunk);
    return ref;
}

/*static*/ std::pair<int, int> FoliageManager::worldToGrid(const glm::vec3& pos)
{
    int gx = static_cast<int>(std::floor(pos.x / FoliageChunk::CHUNK_SIZE));
    int gz = static_cast<int>(std::floor(pos.z / FoliageChunk::CHUNK_SIZE));
    return {gx, gz};
}

} // namespace Vestige
