/// @file brush_tool.cpp
/// @brief BrushTool implementation — mouse ray intersection, stamp logic, undo.
#include "editor/tools/brush_tool.h"
#include "editor/commands/paint_foliage_command.h"
#include "editor/commands/paint_scatter_command.h"
#include "editor/commands/place_tree_command.h"
#include "editor/command_history.h"
#include "renderer/camera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <random>

namespace Vestige
{

static std::mt19937& getRng()
{
    static std::mt19937 rng(std::random_device{}());
    return rng;
}

static float randomFloat(float minVal, float maxVal)
{
    std::uniform_real_distribution<float> dist(minVal, maxVal);
    return dist(getRng());
}

bool BrushTool::processInput(const Ray& mouseRay, bool mouseDown, float deltaTime,
                              FoliageManager& manager, CommandHistory& history)
{
    (void)deltaTime;

    if (!m_enabled)
    {
        return false;
    }

    // Intersect ray with ground plane (y=0)
    m_hasHit = rayGroundIntersect(mouseRay, m_currentHitPoint);
    m_currentHitNormal = glm::vec3(0.0f, 1.0f, 0.0f);

    if (!m_hasHit)
    {
        if (m_painting)
        {
            endStroke(manager, history);
        }
        return false;
    }

    if (mouseDown)
    {
        if (!m_painting)
        {
            beginStroke();
            // Stamp immediately at the starting position
            stampAt(m_currentHitPoint, manager);
            m_lastStampPos = m_currentHitPoint;
        }
        else
        {
            // Check stamp spacing
            float minDist = radius * stampSpacing;
            float dist = glm::distance(m_currentHitPoint, m_lastStampPos);
            if (dist >= minDist)
            {
                stampAt(m_currentHitPoint, manager);
                m_lastStampPos = m_currentHitPoint;
            }
        }
        return true;
    }
    else
    {
        if (m_painting)
        {
            endStroke(manager, history);
        }
        return false;
    }
}

bool BrushTool::getHitPoint(glm::vec3& outPoint, glm::vec3& outNormal) const
{
    if (!m_hasHit || !m_enabled)
    {
        return false;
    }
    outPoint = m_currentHitPoint;
    outNormal = m_currentHitNormal;
    return true;
}

bool BrushTool::rayGroundIntersect(const Ray& ray, glm::vec3& outPoint)
{
    // Intersect with y=0 plane
    if (std::abs(ray.direction.y) < 1e-6f)
    {
        return false;  // Ray is parallel to ground
    }

    float t = -ray.origin.y / ray.direction.y;
    if (t < 0.0f)
    {
        return false;  // Ground is behind camera
    }

    outPoint = ray.origin + ray.direction * t;
    return true;
}

Ray BrushTool::createRay(const Camera& camera, float screenX, float screenY,
                          float aspectRatio)
{
    // Convert screen coords (0..1) to NDC (-1..1)
    float ndcX = screenX * 2.0f - 1.0f;
    float ndcY = 1.0f - screenY * 2.0f;  // Flip Y

    glm::mat4 proj = camera.getProjectionMatrix(aspectRatio);
    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 invVP = glm::inverse(proj * view);

    glm::vec4 nearPoint = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farPoint = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);

    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;

    Ray ray;
    ray.origin = glm::vec3(nearPoint);
    ray.direction = glm::normalize(glm::vec3(farPoint - nearPoint));
    return ray;
}

void BrushTool::beginStroke()
{
    m_painting = true;
    m_strokeAdded.clear();
    m_strokeRemoved.clear();
    m_scatterAdded.clear();
    m_scatterRemoved.clear();
    m_treesAdded.clear();
    m_treesRemoved.clear();
}

void BrushTool::endStroke(FoliageManager& manager, CommandHistory& history)
{
    m_painting = false;

    // Create undo commands for the completed stroke
    if (mode == Mode::ERASER)
    {
        if (!m_strokeRemoved.empty())
        {
            history.execute(std::make_unique<PaintFoliageCommand>(
                manager, std::vector<FoliageInstanceRef>{}, m_strokeRemoved));
        }
        if (!m_scatterRemoved.empty())
        {
            history.execute(std::make_unique<PaintScatterCommand>(
                manager, std::vector<ScatterInstanceRef>{},
                convertScatterRefs(m_scatterRemoved)));
        }
        if (!m_treesRemoved.empty())
        {
            history.execute(std::make_unique<PlaceTreeCommand>(
                manager, std::vector<TreeInstanceRef>{},
                convertTreeRefs(m_treesRemoved)));
        }
    }
    else if (mode == Mode::FOLIAGE && !m_strokeAdded.empty())
    {
        history.execute(std::make_unique<PaintFoliageCommand>(
            manager, m_strokeAdded, std::vector<FoliageInstanceRef>{}));
    }
    else if (mode == Mode::SCATTER && !m_scatterAdded.empty())
    {
        history.execute(std::make_unique<PaintScatterCommand>(
            manager, convertScatterRefs(m_scatterAdded),
            std::vector<ScatterInstanceRef>{}));
    }
    else if (mode == Mode::TREE && !m_treesAdded.empty())
    {
        history.execute(std::make_unique<PlaceTreeCommand>(
            manager, convertTreeRefs(m_treesAdded),
            std::vector<TreeInstanceRef>{}));
    }

    m_strokeAdded.clear();
    m_strokeRemoved.clear();
    m_scatterAdded.clear();
    m_scatterRemoved.clear();
    m_treesAdded.clear();
    m_treesRemoved.clear();
}

void BrushTool::stampAt(const glm::vec3& position, FoliageManager& manager)
{
    if (mode == Mode::ERASER)
    {
        auto removedFoliage = manager.eraseAllFoliage(position, radius);
        m_strokeRemoved.insert(m_strokeRemoved.end(), removedFoliage.begin(), removedFoliage.end());

        auto removedScatter = manager.eraseScatter(position, radius);
        m_scatterRemoved.insert(m_scatterRemoved.end(), removedScatter.begin(), removedScatter.end());

        auto removedTrees = manager.eraseTrees(position, radius);
        m_treesRemoved.insert(m_treesRemoved.end(), removedTrees.begin(), removedTrees.end());
    }
    else if (mode == Mode::FOLIAGE)
    {
        auto added = manager.paintFoliage(
            selectedTypeId, position, radius, density, falloff, foliageConfig, densityMap);
        m_strokeAdded.insert(m_strokeAdded.end(), added.begin(), added.end());
    }
    else if (mode == Mode::SCATTER)
    {
        auto added = manager.paintScatter(
            scatterConfig, selectedTypeId, position, radius, density, falloff, densityMap);
        m_scatterAdded.insert(m_scatterAdded.end(), added.begin(), added.end());
    }
    else if (mode == Mode::TREE)
    {
        // Tree placement: single tree per click, with minimum spacing
        TreeInstance tree;
        tree.position = position;
        tree.rotation = randomFloat(0.0f, glm::two_pi<float>());
        tree.scale = randomFloat(treeConfig.minScale, treeConfig.maxScale);
        tree.speciesIndex = selectedSpeciesId;

        auto placed = manager.placeTree(tree, treeConfig.minSpacing);
        m_treesAdded.insert(m_treesAdded.end(), placed.begin(), placed.end());
    }
    else if (mode == Mode::DENSITY && densityMap)
    {
        densityMap->paint(position, radius, densityPaintValue,
                          densityPaintStrength, falloff);
    }
}

std::vector<ScatterInstanceRef> BrushTool::convertScatterRefs(
    const std::vector<std::pair<uint64_t, ScatterInstance>>& refs)
{
    std::vector<ScatterInstanceRef> result;
    result.reserve(refs.size());
    for (const auto& [key, inst] : refs)
    {
        result.push_back({key, inst});
    }
    return result;
}

std::vector<TreeInstanceRef> BrushTool::convertTreeRefs(
    const std::vector<std::pair<uint64_t, TreeInstance>>& refs)
{
    std::vector<TreeInstanceRef> result;
    result.reserve(refs.size());
    for (const auto& [key, inst] : refs)
    {
        result.push_back({key, inst});
    }
    return result;
}

} // namespace Vestige
