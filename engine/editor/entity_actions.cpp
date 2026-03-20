/// @file entity_actions.cpp
/// @brief EntityActions implementation — duplicate, delete, transform clipboard.
#include "editor/entity_actions.h"
#include "editor/selection.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "core/logger.h"

#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace Vestige
{

namespace EntityActions
{

std::string generateDuplicateName(const std::string& originalName, const Entity* parent)
{
    // Extract base name: strip trailing " (N)" suffix if present
    std::string baseName = originalName;
    if (originalName.size() > 3 && originalName.back() == ')')
    {
        auto parenOpen = originalName.rfind(" (");
        if (parenOpen != std::string::npos)
        {
            std::string numStr = originalName.substr(
                parenOpen + 2, originalName.size() - parenOpen - 3);
            bool allDigits = !numStr.empty();
            for (char c : numStr)
            {
                if (!std::isdigit(static_cast<unsigned char>(c)))
                {
                    allDigits = false;
                }
            }
            if (allDigits)
            {
                baseName = originalName.substr(0, parenOpen);
            }
        }
    }

    // Find the highest existing suffix number among siblings
    int maxSuffix = 0;
    if (parent)
    {
        for (const auto& child : parent->getChildren())
        {
            const std::string& cn = child->getName();
            if (cn == baseName)
            {
                // Base name without suffix counts as 0
                maxSuffix = std::max(maxSuffix, 0);
                continue;
            }

            // Check if cn matches "baseName (N)"
            if (cn.size() > baseName.size() + 3
                && cn.compare(0, baseName.size(), baseName) == 0
                && cn[baseName.size()] == ' '
                && cn[baseName.size() + 1] == '('
                && cn.back() == ')')
            {
                std::string numStr = cn.substr(
                    baseName.size() + 2, cn.size() - baseName.size() - 3);
                bool allDigits = !numStr.empty();
                for (char c : numStr)
                {
                    if (!std::isdigit(static_cast<unsigned char>(c)))
                    {
                        allDigits = false;
                    }
                }
                if (allDigits)
                {
                    maxSuffix = std::max(maxSuffix, std::stoi(numStr));
                }
            }
        }
    }

    return baseName + " (" + std::to_string(maxSuffix + 1) + ")";
}

Entity* duplicateEntity(Scene& scene, Selection& selection, uint32_t entityId)
{
    Entity* clone = scene.duplicateEntity(entityId);
    if (!clone)
    {
        return nullptr;
    }

    // Apply Unity-style name based on siblings
    clone->setName(generateDuplicateName(clone->getName(), clone->getParent()));

    // Offset +0.5m on X so it doesn't overlap the original exactly
    clone->transform.position.x += 0.5f;

    // Auto-select the clone
    selection.select(clone->getId());

    Logger::info("Duplicated: " + clone->getName());
    return clone;
}

void deleteSelectedEntities(Scene& scene, Selection& selection)
{
    if (!selection.hasSelection())
    {
        return;
    }

    // Copy IDs (selection will be modified during deletion)
    std::vector<uint32_t> ids = selection.getSelectedIds();
    selection.clearSelection();

    int deleted = 0;
    for (uint32_t id : ids)
    {
        if (scene.removeEntity(id))
        {
            ++deleted;
        }
    }

    if (deleted > 0)
    {
        Logger::info("Deleted " + std::to_string(deleted) + " entity(s)");
    }
}

void copyTransform(Scene& scene, uint32_t entityId, TransformClipboard& clipboard)
{
    Entity* entity = scene.findEntityById(entityId);
    if (!entity)
    {
        return;
    }

    clipboard.position = entity->transform.position;
    clipboard.rotation = entity->transform.rotation;
    clipboard.scale = entity->transform.scale;
    clipboard.hasData = true;

    Logger::info("Copied transform from '" + entity->getName() + "'");
}

void pasteTransform(Scene& scene, uint32_t entityId, const TransformClipboard& clipboard)
{
    if (!clipboard.hasData)
    {
        return;
    }

    Entity* entity = scene.findEntityById(entityId);
    if (!entity)
    {
        return;
    }

    entity->transform.position = clipboard.position;
    entity->transform.rotation = clipboard.rotation;
    entity->transform.scale = clipboard.scale;

    // Clear matrix override if present (user is setting explicit TRS values)
    if (entity->transform.hasMatrixOverride())
    {
        entity->transform.clearMatrixOverride();
    }

    Logger::info("Pasted transform to '" + entity->getName() + "'");
}

Entity* groupEntities(Scene& scene, Selection& selection)
{
    if (!selection.hasSelection())
    {
        return nullptr;
    }

    auto ids = selection.getSelectedIds();
    if (ids.size() < 2)
    {
        return nullptr;
    }

    // Compute centroid of selected entities' world positions
    glm::vec3 centroid(0.0f);
    int count = 0;
    std::vector<std::pair<uint32_t, glm::vec3>> entityPositions;

    for (uint32_t id : ids)
    {
        Entity* e = scene.findEntityById(id);
        if (e && e != scene.getRoot())
        {
            glm::vec3 wp = e->getWorldPosition();
            centroid += wp;
            entityPositions.emplace_back(id, wp);
            ++count;
        }
    }

    if (count < 2)
    {
        return nullptr;
    }

    centroid /= static_cast<float>(count);

    // Create group entity at root with position = centroid
    Entity* group = scene.createEntity("Group");
    group->transform.position = centroid;

    // Reparent all selected entities under the group
    for (const auto& [id, worldPos] : entityPositions)
    {
        scene.reparentEntity(id, group->getId());

        // Adjust local position to preserve world position
        // Group has no rotation/scale, so: new_local_pos = old_world_pos - centroid
        Entity* e = scene.findEntityById(id);
        if (e)
        {
            e->transform.position = worldPos - centroid;
        }
    }

    selection.select(group->getId());
    Logger::info("Grouped " + std::to_string(count) + " entities");
    return group;
}

} // namespace EntityActions

} // namespace Vestige
